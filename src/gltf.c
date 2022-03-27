/* =====================
 * src/gltf.c
 * 03/06/2022
 * glTF file parser.
 * ====================
 */

#include <inttypes.h>

#define JSMN_STATIC
#include <jsmn.h>

#include <miur/mem.h>
#include <miur/log.h>
#include <miur/gltf.h>

#define INIT_SCENES 4

#define JSMN_NUMBER 1 << 4
#define JSMN_BOOL 1 << 5
#define JSMN_NULL 1 << 6

#define TOKEN_CHUNK_SIZE 30
#define ASSERT_TOKEN(_type) { if (!assert_type(parser->buf.data,                \
                                               parser->tokens[parser->cur],     \
                                               _type))                          \
      return false; }
#define ASSERT_KEY() { if (parser->tokens[parser->cur].type != JSMN_STRING \
                                || parser->tokens[parser->cur].size == 0)       \
      return false; }
#define TOKEN_STRCMP(str) json_strcmp(parser->buf.data,                         \
                                     parser->tokens[parser->cur], str)
#define TOKEN_PREFIX(prefix) json_prefix(parser->buf.data,                      \
                                         parser->tokens[parser->cur], prefix)
#define GET_INDEX_WITH_PREFIX(prefix) json_get_index_with_prefix(               \
    parser->buf.data, parser->tokens[parser->cur], prefix)

#define IS_EOF() (parser->cur >= parser->token_count)
#define TOKEN_SIZE() (parser->tokens[parser->cur].size)
#define TOKEN_LEN() (parser->tokens[parser->cur].end -                          \
                     parser->tokens[parser->cur].start)
#define TOKEN_STRING() (parser->buf.data + parser->tokens[parser->cur].start)

#define NEXT_TOKEN() (parser->cur++)
#define TOKEN_MAKE_STRING() make_token_string(parser->buf.data,                 \
                                              parser->tokens[parser->cur])
#define TOKEN_INT() token_to_int(parser->buf.data, parser->tokens[parser->cur])
#define TOKEN_FLOAT() token_to_float(parser->buf.data, parser->tokens[parser->cur])

typedef struct
{
  const char *name;
  float scale[3];
  int mesh;
} GLTFNode;

typedef struct
{
  const char *name;
  int *nodes;
  size_t node_count;
} GLTFScene;

typedef struct
{
  int buffer;
  int byte_length;
  int byte_offset;
  int byte_stride;
  const char *name;
} GLTFBufferView;

typedef struct {
  const char *uri;
  int byte_length;
  Membuf buf;
} GLTFBuffer;

typedef struct
{
  int normal, position, tangent;
  int *tex_coords;
  int tex_coords_alloc;
  int *colors;
  int colors_alloc;
  int *joints;
  int joints_alloc;
  int *weights;
  int weights_alloc;
  int indices;
} GLTFPrimitive;

typedef struct
{
  GLTFPrimitive *primitives;
  int primitive_count;
  const char *name;
} GLTFMesh;

typedef enum
{
  GLTF_COMPONENT_TYPE_I8,
  GLTF_COMPONENT_TYPE_U8,
  GLTF_COMPONENT_TYPE_I16,
  GLTF_COMPONENT_TYPE_U16,
  GLTF_COMPONENT_TYPE_U32,
  GLTF_COMPONENT_TYPE_FLOAT,
} GLTFComponentType;

typedef enum
{
  GLTF_TYPE_SCALAR,
  GLTF_TYPE_VEC2,
  GLTF_TYPE_VEC3,
  GLTF_TYPE_VEC4,
  GLTF_TYPE_MAT2,
  GLTF_TYPE_MAT3,
  GLTF_TYPE_MAT4,
} GLTFType;

typedef struct
{
  int buffer_view;
  GLTFComponentType component_type;
  int count;
  GLTFType type;
  const char *name;
  float max[16];
  float min[16];
  int byte_offset;
} GLTFAccessor;

typedef struct
{
  jsmntok_t *tokens;
  size_t token_count;
  size_t cur;

  Membuf buf;

  struct {
    const char *version;
    const char *generator;
  } asset;

  uint32_t start_scene;
  GLTFScene *scenes;
  size_t scene_count;

  GLTFNode *nodes;
  size_t node_count;

  GLTFMesh *meshes;
  int mesh_count;

  GLTFAccessor *accessors;
  int accessor_count;

  GLTFBufferView *buffer_views;
  int buffer_view_count;

  GLTFBuffer *buffers;
  int buffer_count;
  const char *filename;
  const char *local_prefix;
  size_t local_prefix_len;
} GLTFParser;

/* === PROTOTYPES === */

bool parse_root(GLTFParser *parser);
bool parse_asset_toplevel(GLTFParser *parser);
bool parse_scene(GLTFParser *parser, GLTFScene *scene);
bool parse_node(GLTFParser *parser, GLTFNode *node);
bool parse_mesh(GLTFParser *parser, GLTFMesh *mesh);
bool parse_primitive(GLTFParser *parser, GLTFPrimitive *primitive);
bool parse_accessor(GLTFParser *parser, GLTFAccessor *accessor);
bool parse_buffer_view(GLTFParser *parser, GLTFBufferView *view);
bool parse_buffer(GLTFParser *parser, GLTFBuffer *buffer);

bool json_strcmp(const uint8_t *buf, jsmntok_t tok, const char *str);
bool json_prefix(const uint8_t *buf, jsmntok_t tok, const char *prefix);
int json_get_index_with_prefix(const uint8_t *buf, jsmntok_t tok,
                               const char *prefix);

float token_to_float(const uint8_t *buf, jsmntok_t tok);
const char *make_token_string(const uint8_t *buf, jsmntok_t tok);
bool assert_type(const uint8_t *buf, jsmntok_t tok, int type);
int64_t token_to_int(const uint8_t *buf, jsmntok_t tok);
int roundup(int base);

bool translate_to_model(GLTFParser *parser, StaticModel *out);

/* === PUBLIC FUNCTIONS === */

bool
gltf_parse(StaticModel *out, const char *filename)
{
  GLTFParser parser;

  parser.asset.version = NULL;
  jsmn_parser json;
  if (!membuf_load_file(&parser.buf, filename))
  {
    return false;
  }
  jsmn_init(&json);

  parser.filename = filename;
  parser.token_count = jsmn_parse(&json, parser.buf.data, parser.buf.size,
                                  NULL, 0);

  size_t filename_len = strlen(filename);
  const char *c = filename + (filename_len - 1);
  while (*c != '\\' && *c != '/')
  {
    c--;
  }
  c++;
  parser.local_prefix_len = c - filename;
  char *local_prefix = MIUR_ARR(char, parser.local_prefix_len);
  memcpy(local_prefix, filename, parser.local_prefix_len);
  parser.local_prefix = local_prefix;

  if (parser.token_count <= 0)
  {
    return false;
  }

  parser.tokens = MIUR_ARR(jsmntok_t, parser.token_count);

  jsmn_init(&json);

  parser.cur = 0;
  jsmn_parse(&json, parser.buf.data, parser.buf.size, parser.tokens,
             parser.token_count);
  if (!parse_root(&parser))
  {
    return false;
  }
  
  if (!translate_to_model(&parser, out))
  {
    return false;
  }

  return true;
}

/* === PRIVATE FUNCTIONS === */

bool parse_root(GLTFParser *parser)
{
  ASSERT_TOKEN(JSMN_OBJECT);

  int size = TOKEN_SIZE();
  NEXT_TOKEN();

  for (int i = 0; i < size; i++)
  {
    ASSERT_KEY();
    if (TOKEN_STRCMP("asset"))
    {
      NEXT_TOKEN();
      if (!parse_asset_toplevel(parser))
      {
        return false;
      }
    }
    else if (TOKEN_STRCMP("scene"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_NUMBER);
      parser->start_scene = TOKEN_INT();
      NEXT_TOKEN();
    }
    else if (TOKEN_STRCMP("scenes"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_ARRAY);
      parser->scene_count = TOKEN_SIZE();
      parser->scenes = MIUR_ARR(GLTFScene, parser->scene_count);
      NEXT_TOKEN();

      for (int j = 0; j < parser->scene_count; j++)
      {
        if (!parse_scene(parser, &parser->scenes[j]))
        {
          return false;
        }
      }
    } else if (TOKEN_STRCMP("nodes"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_ARRAY);
      parser->node_count = TOKEN_SIZE();
      parser->nodes = MIUR_ARR(GLTFNode, parser->node_count);
      NEXT_TOKEN();


      for (int j = 0; j < parser->node_count; j++)
      {
        if (!parse_node(parser, &parser->nodes[j]))
        {
          return false;
        }
      }
    } else if (TOKEN_STRCMP("meshes"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_ARRAY);
      parser->mesh_count = TOKEN_SIZE();
      parser->meshes = MIUR_ARR(GLTFMesh, parser->mesh_count);
      NEXT_TOKEN();

      for (int j = 0; j < parser->mesh_count; j++)
      {
        if (!parse_mesh(parser, &parser->meshes[j]))
        {
          return false;
        }
      }
    } else if (TOKEN_STRCMP("accessors"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_ARRAY);
      parser->accessor_count = TOKEN_SIZE();
      parser->accessors = MIUR_ARR(GLTFAccessor, parser->accessor_count);
      NEXT_TOKEN();

      for (int j = 0; j < parser->accessor_count; j++)
      {
        if (!parse_accessor(parser, &parser->accessors[j]))
        {
          return false;
        }
      }
    } else if (TOKEN_STRCMP("bufferViews"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_ARRAY);
      parser->buffer_view_count = TOKEN_SIZE();
      parser->buffer_views = MIUR_ARR(GLTFBufferView,
                                      parser->buffer_view_count);
      NEXT_TOKEN();

      for (int j = 0; j < parser->buffer_view_count; j++)
      {
        if (!parse_buffer_view(parser, &parser->buffer_views[j]))
        {
          return false;
        }
      }
    } else if (TOKEN_STRCMP("buffers"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_ARRAY);
      parser->buffer_count = TOKEN_SIZE();
      parser->buffers = MIUR_ARR(GLTFBuffer, parser->buffer_count);
      NEXT_TOKEN();

      for (int j = 0; j < parser->buffer_count; j++)
      {
        if (!parse_buffer(parser, &parser->buffers[j]))
        {
          return false;
        }
      }
    }
    else
    {
      MIUR_LOG_ERR("Found unknown root field '%s'", TOKEN_MAKE_STRING());
      return false;
    }
  }

  return true;
}

bool json_strcmp(const uint8_t *buf, jsmntok_t tok, const char *str)
{
  size_t len = strlen(str);
  return len == (tok.end - tok.start) &&
    strncmp(str, buf + tok.start, len) == 0;
}

bool parse_asset_toplevel(GLTFParser *parser)
{
  ASSERT_TOKEN(JSMN_OBJECT);
  int size = TOKEN_SIZE();
  NEXT_TOKEN();

  for (int i = 0; i < size; i++)
  {
    ASSERT_KEY();
    if (TOKEN_STRCMP("version"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_STRING);
      parser->asset.version = TOKEN_MAKE_STRING();
      NEXT_TOKEN();
    }
    else if (TOKEN_STRCMP("generator"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_STRING);
      parser->asset.generator = TOKEN_MAKE_STRING();
      NEXT_TOKEN();
    }
  }

  return true;
}

const char *make_token_string(const uint8_t *buf, jsmntok_t tok)
{
  size_t sz = tok.end - tok.start;
  char *str = MIUR_ARR(char, sz + 1);
  memcpy(str, buf + tok.start, sz);
  str[sz] = '\0';
  return str;
}

bool assert_type(const uint8_t *buf, jsmntok_t tok, int type)
{
  switch (type)
  {
  case JSMN_UNDEFINED:
  case JSMN_OBJECT:
  case JSMN_ARRAY:
  case JSMN_STRING:
    return (type == tok.type);
  case JSMN_NUMBER:
    return (tok.type == JSMN_PRIMITIVE) && (isdigit(buf[tok.start]) ||
                                            buf[tok.start == '-']);
  case JSMN_BOOL:
    return (tok.type == JSMN_PRIMITIVE) && (buf[tok.start] == 't' ||
                                            buf[tok.start] == 'f');
  case JSMN_NULL:
    return (tok.type == JSMN_PRIMITIVE) && buf[tok.start] == 'n';
  }
  return false;
}

int64_t
token_to_int(const uint8_t *buf, jsmntok_t tok)
{
  int64_t ret = 0;
  for (int i = tok.start; i < tok.end; i++)
  {
    ret *= 10;
    ret += buf[i] - '0';
  }
  return ret;

}

bool parse_scene(GLTFParser *parser, GLTFScene *scene)
{
  ASSERT_TOKEN(JSMN_OBJECT);

  int fields = TOKEN_SIZE();
  NEXT_TOKEN();

  for (int i = 0; i < fields; i++)
  {

    ASSERT_KEY();
    if (TOKEN_STRCMP("nodes"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_ARRAY);

      scene->node_count = TOKEN_SIZE();
      scene->nodes = MIUR_ARR(int, scene->node_count);

      NEXT_TOKEN();

      for (int i = 0; i < scene->node_count; i++)
      {
        ASSERT_TOKEN(JSMN_NUMBER);
        scene->nodes[i] = TOKEN_INT();
        NEXT_TOKEN();
      }
    }
  }
  return true;
}

bool parse_node(GLTFParser *parser, GLTFNode *node)
{
  node->name = NULL;
  node->mesh = 0;

  ASSERT_TOKEN(JSMN_OBJECT);
  int fields = TOKEN_SIZE();
  NEXT_TOKEN();

  for (int i = 0; i < fields; i++)
  {
    ASSERT_KEY();

    if (TOKEN_STRCMP("name"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_STRING);
      node->name = TOKEN_MAKE_STRING();
      NEXT_TOKEN();
    } else if (TOKEN_STRCMP("mesh"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_NUMBER);
      node->mesh = TOKEN_INT();
      NEXT_TOKEN();
    } else if (TOKEN_STRCMP("scale"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_ARRAY);
      int scale_fields = TOKEN_SIZE();
      NEXT_TOKEN();
      for (int j = 0; j < scale_fields; j++)
      {
        ASSERT_TOKEN(JSMN_NUMBER);
        node->scale[j] = TOKEN_FLOAT();
        NEXT_TOKEN();
      }
    } else
    {
      MIUR_LOG_INFO("Unkown mesh field: '%s'", TOKEN_MAKE_STRING());
      return false;
    }
  }
  return true;
}

bool parse_mesh(GLTFParser *parser, GLTFMesh *mesh)
{
  ASSERT_TOKEN(JSMN_OBJECT);
  int fields = TOKEN_SIZE();
  NEXT_TOKEN();

  for (int i = 0; i < fields; i++)
  {
    ASSERT_KEY();
    if (TOKEN_STRCMP("primitives"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_ARRAY);
      mesh->primitive_count = TOKEN_SIZE();
      mesh->primitives = MIUR_ARR(GLTFPrimitive, mesh->primitive_count);

      NEXT_TOKEN();

      for (int i = 0; i < mesh->primitive_count; i++)
      {
        if (!parse_primitive(parser, &mesh->primitives[i]))
        {
          return false;
        }
      }
    } else if (TOKEN_STRCMP("name"))
    {

      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_STRING);
      mesh->name = TOKEN_MAKE_STRING();
      NEXT_TOKEN();
    }
    else {
      MIUR_LOG_INFO("unknown mesh field: %s", TOKEN_MAKE_STRING());
      return false;

    }
  }
  return true;
}

bool parse_primitive(GLTFParser *parser, GLTFPrimitive *primitive)
{
  ASSERT_TOKEN(JSMN_OBJECT);
  int fields = TOKEN_SIZE();
  NEXT_TOKEN();
  primitive->tex_coords_alloc = 0;
  primitive->tex_coords = NULL;

  for (int i = 0; i < fields; i++) {
    ASSERT_KEY();

    if (TOKEN_STRCMP("attributes"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_OBJECT);
      int fields = TOKEN_SIZE();
      NEXT_TOKEN();

      for (int j = 0; j < fields; j++)
      {
        ASSERT_KEY();

        if (TOKEN_STRCMP("POSITION"))
        {
          NEXT_TOKEN();
          ASSERT_TOKEN(JSMN_NUMBER);
          primitive->position = TOKEN_INT();
          NEXT_TOKEN();
        } else if (TOKEN_STRCMP("NORMAL"))
        {
          NEXT_TOKEN();
          ASSERT_TOKEN(JSMN_NUMBER);
          primitive->normal = TOKEN_INT();
          NEXT_TOKEN();
        } else if (TOKEN_STRCMP("TANGENT"))
        {
          NEXT_TOKEN();
          ASSERT_TOKEN(JSMN_NUMBER);
          primitive->tangent = TOKEN_INT();
          NEXT_TOKEN();
        }
        else if (TOKEN_PREFIX("TEXCOORD_"))
        {
          int num = GET_INDEX_WITH_PREFIX("TEXCOORD_");
          if (primitive->tex_coords_alloc <= num)
          {
            primitive->tex_coords_alloc = roundup(num);
            if (primitive->tex_coords == NULL)
            {
              primitive->tex_coords = MIUR_ARR(int,
                                               primitive->tex_coords_alloc);
            }
            primitive->tex_coords = MIUR_REALLOC(int, primitive->tex_coords,
                                            primitive->tex_coords_alloc);
          }
          NEXT_TOKEN();
          ASSERT_TOKEN(JSMN_NUMBER);
          primitive->tex_coords[num] = TOKEN_INT();
          NEXT_TOKEN();
        }
        else {
          MIUR_LOG_INFO("Unknown primitive field '%s'", TOKEN_MAKE_STRING());
          return false;
        }

      }
    } else if (TOKEN_STRCMP("indices"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_NUMBER);
      primitive->indices = TOKEN_INT();
      NEXT_TOKEN();
    }
  }
  return true;
}

int roundup(int base)
{
  int ret = 2;
  while (ret < base)
  {
    ret *= 2;
  }
  return ret;
}

bool json_prefix(const uint8_t *buf, jsmntok_t tok, const char *prefix)
{
  int len = strlen(prefix);
  int tok_len = tok.end - tok.start;

  return len <= tok_len && strncmp(buf + tok.start, prefix, len) == 0;
}

int json_get_index_with_prefix(const uint8_t *buf, jsmntok_t tok,
                               const char *prefix)
{
  int len = tok.end - tok.start;
  const char *c = buf + tok.start;
  while (*c == *prefix)
  {
    c++;
    prefix++;
    len--;
  }

  int ret = 0;
  while (len)
  {
    ret *= 10;
    ret += *c - '0';
    len--;
    c++;
  }

  return ret;
}

GLTFComponentType component_type_map[] = {
  [5120] = GLTF_COMPONENT_TYPE_I8,
  [5121] = GLTF_COMPONENT_TYPE_U8,
  [5122] = GLTF_COMPONENT_TYPE_I16,
  [5123] = GLTF_COMPONENT_TYPE_U16,
  [5125] = GLTF_COMPONENT_TYPE_U32,
  [5126] = GLTF_COMPONENT_TYPE_FLOAT,
};

bool parse_accessor(GLTFParser *parser, GLTFAccessor *accessor)
{
  ASSERT_TOKEN(JSMN_OBJECT);

  int fields = TOKEN_SIZE();
  NEXT_TOKEN();

  for (int i = 0; i < fields; i++)
  {
    ASSERT_KEY();
    if (TOKEN_STRCMP("bufferView"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_NUMBER);
      accessor->buffer_view = TOKEN_INT();
      NEXT_TOKEN();
    } else if (TOKEN_STRCMP("componentType"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_NUMBER);
      accessor->component_type = component_type_map[TOKEN_INT()];
      NEXT_TOKEN();
    } else if (TOKEN_STRCMP("byteOffset"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_NUMBER);
      accessor->byte_offset = TOKEN_INT();
      NEXT_TOKEN();
    } else if (TOKEN_STRCMP("count"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_NUMBER);
      accessor->count = TOKEN_INT();
      NEXT_TOKEN();
    } else if (TOKEN_STRCMP("max"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_ARRAY);
      int nums = TOKEN_SIZE();
      NEXT_TOKEN();
      for (int j = 0; j < nums; j++)
      {
        ASSERT_TOKEN(JSMN_NUMBER);
        accessor->max[j] = TOKEN_FLOAT();
        NEXT_TOKEN();
      }
    } else if (TOKEN_STRCMP("min"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_ARRAY);
      int nums = TOKEN_SIZE();
      NEXT_TOKEN();

      for (int j = 0; j < nums; j++)
      {
        ASSERT_TOKEN(JSMN_NUMBER);

        accessor->min[j] = TOKEN_FLOAT();
        NEXT_TOKEN();

      }
    } else if (TOKEN_STRCMP("type"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_STRING);
      if (TOKEN_STRCMP("SCALAR"))
      {
        accessor->type = GLTF_TYPE_SCALAR;
      } else if (TOKEN_STRCMP("VEC2"))
      {
        accessor->type = GLTF_TYPE_VEC2;
      } else if (TOKEN_STRCMP("VEC3"))
      {
        accessor->type = GLTF_TYPE_VEC3;
      } else if (TOKEN_STRCMP("VEC4"))
      {
        accessor->type = GLTF_TYPE_VEC4;
      } else if (TOKEN_STRCMP("MAT2"))
      {
        accessor->type = GLTF_TYPE_MAT2;
      } else if (TOKEN_STRCMP("MAT3"))
      {
        accessor->type = GLTF_TYPE_MAT3;
      } else if (TOKEN_STRCMP("MAT4"))
      {
        accessor->type = GLTF_TYPE_MAT4;
      } else {
        MIUR_LOG_INFO("unknown type '%s'", TOKEN_MAKE_STRING());
        return false;
      }
      NEXT_TOKEN();
    } else if (TOKEN_STRCMP("name"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_STRING);
      accessor->name = TOKEN_MAKE_STRING();
      NEXT_TOKEN();
    } else {
      MIUR_LOG_INFO("unknown accessor field '%s'", TOKEN_MAKE_STRING());
      return false;
    }
  }
  return true;
}

float token_to_float(const uint8_t *buf, jsmntok_t tok)
{
  const char *c = buf + tok.start;
  int len = tok.end - tok.start;
  float sign = *c == '-' ? -1.0 : 1.0;
  bool past_point = false;
  float total = 0.0;
  float multiplier = 10.0f;
  while (len)
  {
    if (*c == '.')
    {
      past_point = true;
      multiplier = 0.1f;
    } else {
      if (past_point) {
        total += ((float) (*c - '0')) * multiplier;
        multiplier /= 10.0f;
      } else {
        total *= multiplier;
        total += ((float) (*c - '0'));
      }
    }
    c++;
    len--;
  }
  return total;
}

bool parse_buffer_view(GLTFParser *parser, GLTFBufferView *view)
{
  ASSERT_TOKEN(JSMN_OBJECT);
  int fields = TOKEN_SIZE();
  NEXT_TOKEN();

  for (int i = 0; i < fields; i++)
  {
    ASSERT_KEY();

    if (TOKEN_STRCMP("buffer"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_NUMBER);
      view->buffer = TOKEN_INT();
      NEXT_TOKEN();
    } else if (TOKEN_STRCMP("byteLength"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_NUMBER);
      view->byte_length = TOKEN_INT();
      NEXT_TOKEN();
    } else if (TOKEN_STRCMP("byteStride"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_NUMBER);
      view->byte_stride = TOKEN_INT();
      NEXT_TOKEN();
    } else if (TOKEN_STRCMP("byteOffset"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_NUMBER);
      view->byte_offset = TOKEN_INT();
      NEXT_TOKEN();
    } else if (TOKEN_STRCMP("name"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_STRING);
      view->name = TOKEN_MAKE_STRING();
      NEXT_TOKEN();
    } else {
      MIUR_LOG_ERR("Unexpected buffer view field '%s'", TOKEN_MAKE_STRING());
      return false;
    }
  }
  return true;
}

bool parse_buffer(GLTFParser *parser, GLTFBuffer *buffer)
{
  ASSERT_TOKEN(JSMN_OBJECT);
  int fields = TOKEN_SIZE();
  NEXT_TOKEN();

  for (int i = 0; i < fields; i++)
  {
    ASSERT_KEY();
    if (TOKEN_STRCMP("uri"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_STRING);
      buffer->uri = TOKEN_MAKE_STRING();
      NEXT_TOKEN();
    } else if (TOKEN_STRCMP("byteLength"))
    {
      NEXT_TOKEN();
      ASSERT_TOKEN(JSMN_NUMBER);
      buffer->byte_length = TOKEN_INT();
      NEXT_TOKEN();
    } else {
      MIUR_LOG_ERR("Unexpected buffer field '%s'", TOKEN_MAKE_STRING());
      return false;
    }
  }

  if (buffer->uri == NULL)
  {
    MIUR_LOG_ERR("Expected UIR for buffer");
    return false;
  }

  size_t uri_len = strlen(buffer->uri);
  size_t full_name_len = parser->local_prefix_len + uri_len;

  char *full_name = MIUR_ARR(char, full_name_len);
  memcpy(full_name, parser->local_prefix, parser->local_prefix_len);
  memcpy(full_name + parser->local_prefix_len, buffer->uri, uri_len);
  full_name[full_name_len] = '\0';

  if (!membuf_load_file(&buffer->buf, full_name))
  {
    MIUR_LOG_ERR("Couldn't open buffer file '%s'", full_name);
    return false;
  }
  return true;
}

bool
translate_to_model(GLTFParser *parser, StaticModel *out)
{
  out->mesh_count = parser->node_count;
  out->meshes = MIUR_ARR(StaticMesh, parser->node_count);
  for (int i = 0; i < parser->node_count; i++)
  {
    GLTFNode *node = &parser->nodes[i];
    GLTFMesh *gmesh = &parser->meshes[node->mesh];
    StaticMesh *mesh = &out->meshes[i];
    if (gmesh->primitive_count > 1)
    {
      MIUR_LOG_ERR("cannot handle more than one primitive");
      return false;
    }


    GLTFPrimitive *prim = &gmesh->primitives[0];

    GLTFAccessor *pos_acc = &parser->accessors[prim->position];
    if (pos_acc->type != GLTF_TYPE_VEC3 ||
        pos_acc->component_type != GLTF_COMPONENT_TYPE_FLOAT) {
      MIUR_LOG_ERR("expected position to a be a vec3 float");
      return false;
    }

    GLTFAccessor *norm_acc = &parser->accessors[prim->position];
    if (norm_acc->type != GLTF_TYPE_VEC3 ||
        norm_acc->component_type != GLTF_COMPONENT_TYPE_FLOAT) {
      MIUR_LOG_ERR("expected normal to a be a vec3 float");
      return false;
    }

    GLTFAccessor *index_acc = &parser->accessors[prim->indices];
    if (index_acc->type != GLTF_TYPE_SCALAR ||
        index_acc->component_type != GLTF_COMPONENT_TYPE_U16) {
      MIUR_LOG_ERR("expected normal to a be a vec3 float");
      return false;
    }

    GLTFBufferView *pos_view = &parser->buffer_views[pos_acc->buffer_view];
    GLTFBufferView *norm_view = &parser->buffer_views[norm_acc->buffer_view];
    GLTFBufferView *index_view = &parser->buffer_views[index_acc->buffer_view];

    GLTFBuffer *pos_buf = &parser->buffers[pos_view->buffer];
    GLTFBuffer *norm_buf = &parser->buffers[norm_view->buffer];
    GLTFBuffer *index_buf = &parser->buffers[index_view->buffer];

    mesh->vert_count = pos_acc->count / 3;

    mesh->verts_pos = MIUR_ARR(float, pos_acc->count);
    memcpy(mesh->verts_pos, pos_buf->buf.data + pos_view->byte_offset +
           pos_acc->byte_offset, pos_acc->count * sizeof(float));

    mesh->verts_norm = MIUR_ARR(float, norm_acc->count);
    memcpy(mesh->verts_norm, norm_buf->buf.data + norm_view->byte_offset +
           norm_acc->byte_offset, norm_acc->count * sizeof(float));

    if (node->scale[0] != 1 || node->scale[1] || node->scale[2])
    {
      for (int i = 0; i < mesh->vert_count; i++)
      {
        mesh->verts_pos[i * 3] *= node->scale[0];
        mesh->verts_pos[i * 3 + 1] *= node->scale[1];
        mesh->verts_pos[i * 3 + 2] *= node->scale[2];
        mesh->verts_norm[i * 3] *= node->scale[0];
        mesh->verts_norm[i * 3 + 1] *= node->scale[1];
        mesh->verts_norm[i * 3 + 2] *= node->scale[2];
      }
    }

    mesh->index_count = index_acc->count;
    mesh->indices = MIUR_ARR(uint16_t, mesh->index_count);
    memcpy(mesh->indices, index_buf->buf.data + index_view->byte_offset +
           index_view->byte_offset, index_acc->count * sizeof(uint16_t));
  }
  return true;
}
