/* =====================
 * src/bsl.c
 * 03/08/2022
 * The Beans shading language.
 * ====================
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include <vulkan/vulkan.h>

#if VK_HEADER_VERSION >= 135
#include <spirv-headers/spirv.h>
#else
#include <vulkan/spirv.h>
#endif

#include <miur/mem.h>
#include <miur/log.h>
#include <miur/bsl.h>

/* === MACROS === */

#define IS_EOF() (parser->cur_end >= parser->buf.size)
#define PEEK_C() (parser->buf.data[parser->cur_end])
#define NEXT_C() (parser->end_col++, parser->buf.data[parser->cur_end++])
#define BACKUP_C() (parser->cur_end--)
#define RESET() (parser->cur_start = parser->cur_end, parser->start_line =      \
                 parser->end_line, parser->start_col = parser->end_col)

#define PARSER_LOG_TOKEN(parser, token, ...) parser_log_error(parser,           \
                                                              token.line,       \
                                                              token.col,        \
                                                              __VA_ARGS__);

#define EXPECT_TOKEN(type) { BSLToken tok;                                      \
  if ((tok = parser_next(parser)).t != type) {                                  \
  PARSER_LOG_TOKEN(parser, tok, "expected '%s', not '%s'",                      \
                   token_type_string_map[type], token_type_string_map[tok.t]);  \
  return false;                                                                 \
  } }

#define EXPECT_TOKEN_WITH(name, type)                                           \
  if ((name = parser_next(parser)).t != type) {                                 \
  PARSER_LOG_TOKEN(parser, name, "expected '%s', not '%s'",                     \
                   token_type_string_map[type], token_type_string_map[name.t]); \
  return false;                                                                 \
  }

#define SYM_EQ(tok, string) (tok.sym.len == strlen(string) &&                   \
                             strncmp(tok.sym.start, string, tok.sym.len) == 0)
#define SYM_EQN(_tok, _start, _len) (_tok.sym.len == _len &&                    \
                             strncmp(_tok.sym.start, _start, _tok.sym.len) == 0)

#define RESERVE_CODE(amount) if ((parser->spirv_used += amount) >=              \
                                 BSL_MAX_SPIRV) { return false; }

#define ADD_INST(nargs, type, ...) if (!add_inst(parser, proc, nargs, type,     \
                                                 __VA_ARGS__)) {        \
    return false;                                                       \
      }

#define GENERATE_EXPR(expr, loc_ptr) if (!generate_expr(parser, proc, expr,    \
                                                        loc_ptr)) { return     \
      false; }

#define GENSYM() (parser->next_spirv_addr++)

#define KEYWORD_MIN BSL_TOKEN_PROCEDURE
#define KEYWORD_MAX BSL_TOKEN_AT + 1

#define BSL_MAX_ENTRY_POINTS 2
#define BSL_MAX_PROCEDURES 10
#define BSL_MAX_GLOBALS 10
#define BSL_MAX_TYPES 100
#define BSL_MAX_EXPRS 100
#define BSL_MAX_RECORD_MEMBERS 100
#define BSL_MAX_EXPR_ARR 200
#define BSL_MAX_SPIRV 1000
#define BSL_MAX_NESTED_SCOPES 10
#define BSL_MAX_LOCALS 50
#define BSL_MAX_CONSTANTS 50
#define BSL_MAX_INTERFACES 15
#define BSL_MAX_LOCATIONS 10

#define BASE_SPIRV_ADDR 1

#define BSL_F32_TYPE_INDEX 1
#define BSL_VOID_TYPE_INDEX 0

/* === TYPES === */
typedef enum
{
  BSL_TOKEN_PROCEDURE,
  BSL_TOKEN_END,
  BSL_TOKEN_IN,
  BSL_TOKEN_OUT,
  BSL_TOKEN_RECORD,
  BSL_TOKEN_VAR,
  BSL_TOKEN_RETURN,
  BSL_TOKEN_AT,

  BSL_TOKEN_PERIOD,
  BSL_TOKEN_ASSN,
  BSL_TOKEN_EQ,
  BSL_TOKEN_LPAREN,
  BSL_TOKEN_RPAREN,
  BSL_TOKEN_LCURLY,
  BSL_TOKEN_RCURLY,
  BSL_TOKEN_LBRACKET,
  BSL_TOKEN_RBRACKET,
  BSL_TOKEN_COMMA,
  BSL_TOKEN_ADD,
  BSL_TOKEN_SUB,
  BSL_TOKEN_MUL,
  BSL_TOKEN_DIV,
  BSL_TOKEN_SEMICOLON,
  BSL_TOKEN_COLON,
  BSL_TOKEN_ADDRESS,

  BSL_TOKEN_ARROW,
  BSL_TOKEN_LT,
  BSL_TOKEN_GT,

  BSL_TOKEN_SYM,
  BSL_TOKEN_STRING,
  BSL_TOKEN_INTEGER,
  BSL_TOKEN_NUMBER,

  BSL_TOKEN_EOF,
  BSL_TOKEN_ERROR,
} BSLTokenType;

typedef struct
{
  BSLTokenType t;
  int line, col;
  union {
    struct {
      const char *start;
      size_t len;
    } sym;
    struct {
      const char *start;
      size_t len;
    } string;

    int64_t integer;
    float number; // @Todo: change this to a bignum, for constant folding.
  };
} BSLToken;

typedef struct {
  enum {
    BSL_CONSTANT_FLOAT,
  } t;
  uint32_t spirv_addr;
  union {
    uint32_t u32;
    float f;
  };
} BSLConstant;

struct BSLProcedure;

typedef struct
{
  const char *name;
  size_t len;
  enum
  {
    BSL_ENTRY_POINT_VERTEX,
    BSL_ENTRY_POINT_FRAGMENT,
  } type;
  uint32_t spirv_addr;
  struct BSLProcedure *proc;
} BSLEntryPoint;

struct BSLType;

typedef enum {
  BSL_BUILTIN_POSITION_BIT = 1 << 0,
} BSLBuiltinFlags;

typedef struct
{
  const char *name;
  size_t len;
  struct BSLType *type;
  enum BSLBuiltinFlags flags;
} BSLRecordMember;

typedef struct BSLType
{
  const char *name;
  size_t len;
  uint32_t spirv_addr;
  enum {
    BSL_TYPE_BOOL,
    BSL_TYPE_VOID,
    BSL_TYPE_F32,
    BSL_TYPE_F64,
    BSL_TYPE_I32,
    BSL_TYPE_U32,
    BSL_TYPE_VECTOR,
    BSL_TYPE_POINTER,
    BSL_TYPE_PROCEDURE,
    BSL_TYPE_RECORD,
  } type;

  SpvStorageClass storage_class;
  struct BSLType *subtype; /* Vector type, or return type. */
  uint32_t size; /* Vector size, number of struct members. */
  BSLRecordMember *members; /* Number of struct members. */
} BSLType;

struct BSLGlobal;

typedef struct {
  const char *name;
  size_t len;
  uint32_t spirv_addr;
  BSLType *type;
  BSLType *ptr_type;
  struct BSLGlobal *global; /* NULL if this is not a global. */
} BSLLocal;

typedef struct BSLGlobal
{
  const char *name;
  size_t len;
  enum
  {
    BSL_GLOBAL_IN,
    BSL_GLOBAL_OUT,
  } io_type;
  int64_t location;
  uint32_t spirv_addr;
  BSLBuiltinFlags builtin_flags;
  BSLType *type;
  BSLType *ptr_type;
  BSLLocal *local;
} BSLGlobal;

typedef struct BSLExpr
{
  enum {
    BSL_EXPR_FLOAT,
    BSL_EXPR_VAR,
    BSL_EXPR_VECTOR,
    BSL_EXPR_ADD,
    BSL_EXPR_SUB,
    BSL_EXPR_MUL,
    BSL_EXPR_DIV,
    BSL_EXPR_SCALAR_MUL,
    BSL_EXPR_SCALAR_DIV,
  } t;
  union {
    BSLConstant *constant;
    BSLLocal *local;
    struct {
      struct BSLExpr **exprs;
      size_t size;
    } vector;
    struct {
      struct BSLExpr *left;
      struct BSLExpr *right;
    } binop;
    struct {
      struct BSLExpr *scalar;
      struct BSLExpr *vector;
    } scalar;
  };
  BSLType *type;
} BSLExpr;

typedef struct BSLProcedure
{
  uint32_t spirv_addr;
  BSLType *type;
  uint32_t parameter_count;
  uint32_t *code;
  size_t code_sz;
  bool has_returned;
  BSLGlobal **interfaces;
  size_t interface_count;
} BSLProcedure;

typedef struct
{
  size_t cur_start, cur_end;
  int start_line, start_col;
  int end_line, end_col;
  Membuf buf;
  BSLToken peek;
  bool has_peek, has_error;
  int err_line, err_col;
  BSLEntryPoint entry_points[BSL_MAX_ENTRY_POINTS];
  size_t entry_points_used;
  BSLCompileResult *result;
  int next_entry_point;

  BSLProcedure procedures[BSL_MAX_PROCEDURES];
  size_t procedures_used;

  BSLGlobal globals[BSL_MAX_GLOBALS];
  size_t globals_used;

  BSLType types[BSL_MAX_TYPES];
  size_t types_used;

  uint32_t next_spirv_addr;

  BSLRecordMember record_members[BSL_MAX_RECORD_MEMBERS];
  size_t record_members_used;

  BSLExpr exprs[BSL_MAX_EXPRS];
  size_t exprs_used;

  BSLExpr *expr_arr[BSL_MAX_EXPR_ARR];
  size_t expr_arr_used;

  uint32_t spirv[BSL_MAX_SPIRV];
  size_t spirv_used;

  BSLLocal locals[BSL_MAX_LOCALS];

  size_t scopes[BSL_MAX_NESTED_SCOPES];
  size_t scope_top;

  BSLConstant constants[BSL_MAX_CONSTANTS];
  size_t constants_used;

  BSLBuiltinFlags next_builtin;

  BSLGlobal *interfaces[BSL_MAX_INTERFACES];
  size_t interfaces_used;

  uint32_t locations[BSL_MAX_LOCATIONS];
} BSLParser;

const char *keyword_string_map[] =
{
  [BSL_TOKEN_PROCEDURE] = "procedure",
  [BSL_TOKEN_IN] = "in",
  [BSL_TOKEN_OUT] = "out",
  [BSL_TOKEN_AT] = "at",
  [BSL_TOKEN_VAR] = "var",
  [BSL_TOKEN_RETURN] = "return",
  [BSL_TOKEN_RECORD] = "record",
  [BSL_TOKEN_END] = "end",
};

const char *token_type_string_map[] =
{
  [BSL_TOKEN_PROCEDURE] = "Procedure",
  [BSL_TOKEN_IN] = "In",
  [BSL_TOKEN_OUT] = "Out",
  [BSL_TOKEN_AT] = "At",
  [BSL_TOKEN_VAR] = "Var",
  [BSL_TOKEN_RETURN] = "Return",
  [BSL_TOKEN_END] = "End",
  [BSL_TOKEN_RECORD] = "Record",

  [BSL_TOKEN_PERIOD] = "Period",
  [BSL_TOKEN_ASSN] = "Assn",
  [BSL_TOKEN_EQ] = "Equal",
  [BSL_TOKEN_LPAREN] = "LParen",
  [BSL_TOKEN_RPAREN] = "RParen",
  [BSL_TOKEN_LCURLY] = "LCurly",
  [BSL_TOKEN_RCURLY] = "RCurly",
  [BSL_TOKEN_LBRACKET] = "LBracket",
  [BSL_TOKEN_RBRACKET] = "RBracket",
  [BSL_TOKEN_ADD] = "Add",
  [BSL_TOKEN_SUB] = "Sub",
  [BSL_TOKEN_MUL] = "Mul",
  [BSL_TOKEN_DIV] = "Div",
  [BSL_TOKEN_COMMA] = "Comma",
  [BSL_TOKEN_SEMICOLON] = "Semicolon",
  [BSL_TOKEN_COLON] = "Colon",
  [BSL_TOKEN_ADDRESS] = "Address",
  [BSL_TOKEN_ARROW] = "Arrow",
  [BSL_TOKEN_LT] = "LessThan",
  [BSL_TOKEN_GT] = "GreaterThan",

  [BSL_TOKEN_SYM] = "Sym",
  [BSL_TOKEN_STRING] = "String",
  [BSL_TOKEN_INTEGER] = "Integer",
  [BSL_TOKEN_NUMBER] = "Number",

  [BSL_TOKEN_EOF] = "Eof",
  [BSL_TOKEN_ERROR] = "Error",
};

/* === PROTOTYPES === */

BSLToken parser_next(BSLParser *parser);
BSLToken parser_peek(BSLParser *parser);
BSLToken parser_get_token(BSLParser *parser);
BSLToken make_token(BSLParser *parser, BSLTokenType type);
BSLToken lex_sym(BSLParser *parser);
BSLToken lex_number(BSLParser *parser, bool negative);
BSLToken lex_string(BSLParser *parser);
void skip_whitespace(BSLParser *parser);
bool skip_comments(BSLParser *parser);
void parser_log_error(BSLParser *parser, int line, int col,
                      const char *fmt, ...);
void find_file_pos(BSLParser *parser, size_t index, int *line_out,
                       int *col_out);
void print_token(BSLToken tok);

bool parse_toplevel(BSLParser *parser);
bool parse_attribute(BSLParser *parser);
bool parse_procedure(BSLParser *parser);
bool parse_global(BSLParser *parser, int io_type);
bool parse_record(BSLParser *parser);
bool parse_type(BSLParser *parser, BSLType **type);
bool parse_stmt(BSLParser *parser, BSLProcedure *proc);
bool parse_expr(BSLParser *parser, BSLExpr **expr);
bool generate_expr(BSLParser *parser, BSLProcedure *proc, BSLExpr *expr,
                   uint32_t *location);
bool add_interface(BSLParser *parser, BSLProcedure *proc, BSLLocal *local);
BSLConstant *alloc_constant(BSLParser *parser, int type);
BSLConstant *alloc_float_constant(BSLParser *parser, float f);
bool parse_aexpr(BSLParser *parser, BSLExpr **out);
bool parse_mul_expr(BSLParser *parser, BSLExpr **out);
bool parse_add_expr(BSLParser *parser, BSLExpr **out);
void print_expr(BSLParser *parser, BSLExpr *expr);

BSLLocal *add_var(BSLParser *parser, const char *name, size_t len,
                  BSLType *type);
BSLLocal *lookup_var(BSLParser *parser, const char *name, size_t len);
bool push_scope(BSLParser *parser);
bool pop_scope(BSLParser *parser);

bool add_inst(BSLParser *parser, BSLProcedure *proc, uint32_t size,
              uint32_t type, ...);

void init_types(BSLParser *parser);
BSLType *alloc_type(BSLParser *parser);
BSLExpr *alloc_expr(BSLParser *parser);
BSLType *make_procedure_type(BSLParser *parser, BSLType *return_type);
BSLType *make_vector_type(BSLParser *parser, BSLType *subtype, int members);
BSLType *make_pointer_type(BSLParser *parser, BSLType *subtype,
                           SpvStorageClass class);
uint32_t encode_op(uint32_t *spirv, uint16_t size, uint32_t op, ...);
bool pack_spirv(BSLParser *parser);

/* === PUBLIC FUNCTIONS === */

bool bsl_compile(BSLCompileResult *result, Membuf buf,
            BSLCompileFlags *flags)
{
  BSLParser parser = {
    .buf = buf,
    .has_peek = false,
    .result = result,
    .has_error = false,
    .start_line = 1,
    .start_col = 1,
    .next_spirv_addr = BASE_SPIRV_ADDR + 1,
  };
  BSLToken tok;

  init_types(&parser);

  while ((tok = parser_peek(&parser)).t != BSL_TOKEN_EOF &&
         tok.t != BSL_TOKEN_ERROR)
  {
    if (!parse_toplevel(&parser))
    {
      return false;
    }
  }

  if (parser.has_error)
  {
    return false;
  }

  if (!pack_spirv(&parser))
  {
    return false;
  }
  return true;
}

/* === PRIVATE_FUNCTIONS === */

BSLToken parser_next(BSLParser *parser)
{
  if (parser->has_peek)
  {
    parser->has_peek = false;
    return parser->peek;
  }
  else
  {
    return parser_get_token(parser);
  }
}

BSLToken parser_peek(BSLParser *parser)
{
  if (parser->has_peek)
  {
    return parser->peek;
  }

  parser->has_peek = true;
  return parser->peek = parser_get_token(parser);
}

BSLToken parser_get_token(BSLParser *parser)
{
  BSLToken tok;
  while (1)
  {
    skip_whitespace(parser);
    if (!skip_comments(parser))
    {
      break;
    }
  }

  skip_whitespace(parser);

  if (IS_EOF())
  {
    return make_token(parser, BSL_TOKEN_EOF);
  }

  int c = PEEK_C();
  if (isalpha(c) || c == '_')
  {
    return lex_sym(parser);
  }

  if (c == '-') {
    NEXT_C();
    if (isdigit(PEEK_C()))
    {
      return lex_number(parser, true);
    } else
    {
      BACKUP_C();
    }
  }

  if (isdigit(c))
  {
    return lex_number(parser, false);
  }

  if (c == '"')
  {
    return lex_string(parser);
  }

  switch (c)
  {
  case ':':
    NEXT_C();
    if (PEEK_C() == '=')
    {
      NEXT_C();
      return make_token(parser, BSL_TOKEN_ASSN);
    }
    return make_token(parser, BSL_TOKEN_COLON);
  case '.':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_PERIOD);
  case '=':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_EQ);
  case '@':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_ADDRESS);
  case ',':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_COMMA);
  case '(':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_LPAREN);
  case ')':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_RPAREN);
  case '[':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_LBRACKET);
  case ']':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_RBRACKET);
  case '<':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_LT);
  case '>':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_GT);
  case '{':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_LCURLY);
  case '}':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_RCURLY);
  case '+':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_ADD);
  case '-':
    NEXT_C();
    if (PEEK_C() == '>')
    {
      NEXT_C();
      return make_token(parser, BSL_TOKEN_ARROW);
    }
    return make_token(parser, BSL_TOKEN_SUB);
  case '*':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_MUL);
  case '/':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_DIV);
  case ';':
    NEXT_C();
    return make_token(parser, BSL_TOKEN_SEMICOLON);
  }
  tok.t = BSL_TOKEN_ERROR;
  parser_log_error(parser, parser->start_line, parser->start_col,
                   "unexpected character '%c'", c);
  parser->has_error = true;
  return tok;
}

BSLToken make_token(BSLParser *parser, BSLTokenType type)
{
  BSLToken tok;
  tok.line = parser->start_line;
  tok.col = parser->start_col;
  tok.t = type;

  RESET();

  return tok;
}

BSLToken lex_string(BSLParser *parser)
{
  BSLToken tok;
  int c;
  NEXT_C();
  while ((c = PEEK_C()) != '"')
  {
    NEXT_C();
  }

  NEXT_C();
  tok.line = parser->start_line;
  tok.col = parser->start_col;

  tok.t = BSL_TOKEN_STRING;
  tok.string.start = parser->buf.data + parser->cur_start + 1;
  tok.string.len = parser->cur_end - parser->cur_start - 2;

  RESET();
  return tok;
}

BSLToken lex_sym(BSLParser *parser)
{
  BSLToken tok;
  int c;
  while (!IS_EOF() && (isalpha((c = PEEK_C())) || isdigit(c) || c == '_'))
  {
    NEXT_C();
  }

  size_t len = parser->cur_end - parser->cur_start;

  for (int i = KEYWORD_MIN; i < KEYWORD_MAX; i++)
  {
    if (len == strlen(keyword_string_map[i]) &&
        strncmp(parser->buf.data + parser->cur_start,
                keyword_string_map[i], len) == 0)
    {
      tok.t = i;
      tok.line = parser->start_line;
      tok.col = parser->start_col;
      RESET();

      return tok;
    }
  }

  tok.t = BSL_TOKEN_SYM;
  tok.sym.start = parser->buf.data + parser->cur_start;
  tok.sym.len = len;
  tok.line = parser->start_line;
  tok.col = parser->start_col;

  RESET();
  return tok;
}

BSLToken lex_number(BSLParser *parser, bool negative)
{
  BSLToken tok;
  tok.t = BSL_TOKEN_INTEGER;
  tok.integer = 0;
  while (isdigit(PEEK_C()))
  {
    tok.integer *= 10;
    tok.integer += NEXT_C() - '0';
  }
  if (PEEK_C() == '.')
  {
    double position = 10.0;
    tok.number = (double) tok.integer;
    NEXT_C();
    while (isdigit(PEEK_C()))
    {
      tok.number += ((double) (NEXT_C() - '0')) / position;
      position *= 10;
    }
    tok.t = BSL_TOKEN_NUMBER;
    if (negative)
    {
      tok.number *= -1.0;
    }
  } else if (negative) {
    tok.integer *= -1;
  }

  tok.line = parser->start_line;
  tok.col = parser->start_col;
  RESET();
  return tok;
}

bool skip_comments(BSLParser *parser)
{
  if (PEEK_C() == '/')
  {
    NEXT_C();
    if (PEEK_C() == '/')
    {
      while (NEXT_C() != '\n')
        ; // Do nothing.
      parser->end_line++;
      parser->end_col = 1;
      RESET();
      return true;
    }
    else {
      BACKUP_C();
    }
  }

  return false;
}

void skip_whitespace(BSLParser *parser)
{
  while (!IS_EOF() && isspace(PEEK_C()))
  {
    if (NEXT_C() == '\n')
    {
      parser->end_line++;
      parser->end_col = 1;
    }
  }
  RESET();
}

void parser_log_error(BSLParser *parser, int line, int col,
                      const char *fmt, ...)
{
  if (!parser->has_error)
  {
    va_list args;
    va_start(args, fmt);

    parser->has_error = true;
    parser->err_line = line;
    parser->err_col = col;
    vsnprintf(parser->result->error, BSL_MAX_ERROR_LENGTH, fmt, args);
  }
}

void find_file_pos(BSLParser *parser, size_t end_point, int *line_out,
                   int *col_out)
{
  int line = 1, col = 1;

  for (size_t idx = 0; idx < parser->buf.size && idx <= end_point; idx++)
  {
    if (parser->buf.data[idx] == '\n')
    {
      line++;
      col = 1;
    } else
    {
      col++;
    }
  }

  *line_out = line;
  *col_out = col;
}

void print_token(BSLToken tok)
{
  switch (tok.t)
  {
  case BSL_TOKEN_NUMBER:
    MIUR_LOG_INFO("(%d, %d) Number : %f", tok.line, tok.col, tok.number);
    break;
  case BSL_TOKEN_INTEGER:
    MIUR_LOG_INFO("(%d, %d) Integer : %" PRId64, tok.line, tok.col,
                  tok.integer);
    break;
  case BSL_TOKEN_SYM:
    MIUR_LOG_INFO("(%d, %d) Symbol : '%.*s'", tok.line, tok.col,
                  (int) tok.sym.len, tok.sym.start);
    break;
  case BSL_TOKEN_STRING:
    MIUR_LOG_INFO("(%d, %d) String : '%.*s'", tok.line, tok.col,
                  (int) tok.string.len, tok.string.start);
    break;
  default:
    MIUR_LOG_INFO("(%d, %d) %s", tok.line, tok.col,
                  token_type_string_map[tok.t]);
    break;
  }
}

uint32_t encode_op(uint32_t *spirv, uint16_t size, uint32_t op, ...)
{
  va_list args;
  va_start(args, op);
  uint32_t idx = 0;
  spirv[idx++] = (size << 16) | op;

  for (uint16_t i = 0; i < size; i++)
  {
    spirv[idx++] = va_arg(args, uint32_t);
  }

  return idx;
}

BSLExpr *alloc_expr(BSLParser *parser)
{
  if (parser->exprs_used + 1 > BSL_MAX_EXPRS)
  {
    parser_log_error(parser, parser->start_line, parser->start_col,
                     "exceeded maximum number of expressions (%d)",
                     BSL_MAX_EXPRS);
    return NULL;
  }
  return &parser->exprs[parser->exprs_used++];
}

BSLType *alloc_type(BSLParser *parser)
{
  if (parser->types_used + 1 > BSL_MAX_TYPES)
  {
    parser_log_error(parser, parser->start_line, parser->start_col,
                   "exceeded maximum number of types (%d)", BSL_MAX_TYPES);
    return NULL;
  }

  return &parser->types[parser->types_used++];
}

BSLType *make_pointer_type(BSLParser *parser, BSLType *subtype,
                           SpvStorageClass class)
{
  for (uint32_t i = 0; i < parser->types_used; i++)
  {
    BSLType *type = &parser->types[i];
    if (type->type == BSL_TYPE_POINTER &&
        type->subtype == subtype &&
        type->storage_class == class)
    {
      return type;
    }
  }

  BSLType *ret = alloc_type(parser);
  if (ret == NULL)
  {
    return NULL;
  }
  ret->type = BSL_TYPE_POINTER;
  ret->subtype = subtype;
  ret->name = NULL;
  ret->spirv_addr = parser->next_spirv_addr++;
  ret->storage_class = class;
  return ret;
}

BSLType *make_vector_type(BSLParser *parser, BSLType *subtype, int size)
{
  for (uint32_t i = 0; i < parser->types_used; i++)
  {
    BSLType *type = &parser->types[i];
    if (type->type == BSL_TYPE_VECTOR &&
        type->subtype == subtype &&
        type->size == size)
    {
      return type;
    }
  }

  BSLType *ret = alloc_type(parser);
  if (ret == NULL)
  {
    return NULL;
  }
  ret->type = BSL_TYPE_VECTOR;
  ret->subtype = subtype;
  ret->size = size;
  ret->name = NULL;
  ret->spirv_addr = parser->next_spirv_addr++;
  return ret;
}

BSLType *make_procedure_type(BSLParser *parser, BSLType *return_type)
{
  BSLType *ret = alloc_type(parser);
  if (ret == NULL)
  {
    return NULL;
  }
  ret->type = BSL_TYPE_PROCEDURE;
  ret->subtype = return_type;
  ret->spirv_addr = parser->next_spirv_addr++;
  ret->name = NULL;
  return ret;
}

void init_types(BSLParser *parser)
{
  parser->types[BSL_VOID_TYPE_INDEX].name = "void";
  parser->types[BSL_VOID_TYPE_INDEX].len = 4;
  parser->types[BSL_VOID_TYPE_INDEX].type = BSL_TYPE_VOID;
  parser->types[BSL_VOID_TYPE_INDEX].spirv_addr = parser->next_spirv_addr++;

  parser->types[BSL_F32_TYPE_INDEX].name = "f32";
  parser->types[BSL_F32_TYPE_INDEX].len = 3;
  parser->types[BSL_F32_TYPE_INDEX].type = BSL_TYPE_F32;
  parser->types[BSL_F32_TYPE_INDEX].spirv_addr = parser->next_spirv_addr++;

  /*
  parser->types[2].name = "f64";
  parser->types[2].len = 3;
  parser->types[2].type = BSL_TYPE_F64;
  parser->types[2].spirv_addr = parser->next_spirv_addr++;
  */

  parser->types_used = 2;
}

bool parse_expr(BSLParser *parser, BSLExpr **out)
{
  bool result = parse_add_expr(parser, out);
  return result;
}

bool parse_add_expr(BSLParser *parser, BSLExpr **out)
{
  BSLToken token;
  BSLExpr *left;
  if (!parse_mul_expr(parser, &left))
  {
    return false;
  }
  while ((token = parser_peek(parser)).t == BSL_TOKEN_ADD ||
         token.t == BSL_TOKEN_SUB)
  {
    parser_next(parser);
    BSLExpr *right;
    if (!parse_mul_expr(parser, &right))
    {
      return false;
    }
    BSLExpr *full = alloc_expr(parser);

    full->t = token.t == BSL_TOKEN_ADD ? BSL_EXPR_ADD : BSL_EXPR_SUB;
    full->binop.right = right;
    full->binop.left = left;

    if (left->type != right->type)
    {
      PARSER_LOG_TOKEN(parser, token, "cannot %s expressions of different types",
                         full->t == BSL_EXPR_ADD ? "add" : "subtract");
      return false;
    }
    full->type = left->type;

    left = full;
  }
  *out = left;
  return true;
}

bool parse_mul_expr(BSLParser *parser, BSLExpr **out)
{
  BSLToken token;
  BSLExpr *left;
  if (!parse_aexpr(parser, &left))
  {
    return false;
  }
  while ((token = parser_peek(parser)).t == BSL_TOKEN_MUL ||
         token.t == BSL_TOKEN_DIV)
  {
    parser_next(parser);
    BSLExpr *right;
    if (!parse_aexpr(parser, &right))
    {
      return false;
    }
    BSLExpr *full = alloc_expr(parser);
    if ((left->type->type == BSL_TYPE_VECTOR &&
         right->type == left->type->subtype) ||
        (right->type->type == BSL_TYPE_VECTOR &&
         left->type == right->type->subtype))
    {
      full->t = token.t == BSL_TOKEN_MUL ? BSL_EXPR_SCALAR_MUL :
        BSL_EXPR_SCALAR_DIV;
      full->scalar.scalar = left->type->type == BSL_TYPE_VECTOR ? right : left;
      full->scalar.vector = left->type->type == BSL_TYPE_VECTOR ? left : right;
      full->type = left->type->type == BSL_TYPE_VECTOR ? left->type : right->type;
    } else
    {
      PARSER_LOG_TOKEN(parser, token, "can only multiply a scalar by a vector");
      return false;
    }
    left = full;
  }

  *out = left;
  return true;
}

void print_expr(BSLParser *parser, BSLExpr *expr)
{
  switch (expr->t)
  {
  case BSL_EXPR_FLOAT:
    MIUR_LOG_INFO("Float: %f", expr->constant->f);
    break;
  case BSL_EXPR_VAR:
    MIUR_LOG_INFO("Var: %.*s", expr->local->len, expr->local->name);
    break;
  case BSL_EXPR_VECTOR:
    MIUR_LOG_INFO("Vector: %d", expr->vector.size);
    for (int i= 0; i < expr->vector.size; i++)
    {
      print_expr(parser, expr->vector.exprs[i]);
    }
    break;
  case BSL_EXPR_ADD:
    MIUR_LOG_INFO("Add");
    print_expr(parser, expr->binop.left);
    print_expr(parser, expr->binop.right);
    break;
  case BSL_EXPR_SUB:
    MIUR_LOG_INFO("Sub");
    print_expr(parser, expr->binop.left);
    print_expr(parser, expr->binop.right);
    break;
  case BSL_EXPR_SCALAR_MUL:
    MIUR_LOG_INFO("Mul");
    print_expr(parser, expr->scalar.scalar);
    print_expr(parser, expr->scalar.vector);
    break;
  case BSL_EXPR_SCALAR_DIV:
    MIUR_LOG_INFO("Div");
    print_expr(parser, expr->scalar.scalar);
    print_expr(parser, expr->scalar.vector);
    break;
  }
}
bool parse_aexpr(BSLParser *parser, BSLExpr **out)
{
  BSLToken token;
  BSLExpr *expr = alloc_expr(parser);
  switch ((token = parser_peek(parser)).t)
  {
  case BSL_TOKEN_LPAREN:
    parser_next(parser);
    if (!parse_expr(parser, out))
    {
      return false;
    }
    EXPECT_TOKEN(BSL_TOKEN_RPAREN);
    return true;
  case BSL_TOKEN_SYM:
    parser_next(parser);
    expr->t = BSL_EXPR_VAR;
    expr->local = lookup_var(parser, token.sym.start, token.sym.len);
    if (expr->local == NULL)
    {
      PARSER_LOG_TOKEN(parser, token, "couldn't find variable '%.*s' in scope",
                       (int) token.sym.len, (char *) token.sym.start);
      return false;
    }
    expr->type = expr->local->type;
    break;
  case BSL_TOKEN_NUMBER:
    parser_next(parser);
    expr->constant = alloc_float_constant(parser, token.number);
    expr->t = BSL_EXPR_FLOAT;
    expr->type = &parser->types[BSL_F32_TYPE_INDEX];
    break;
  case BSL_TOKEN_LCURLY: {
    parser_next(parser);
    expr->t = BSL_EXPR_VECTOR;
    expr->vector.exprs = &parser->expr_arr[parser->expr_arr_used];
    parser->expr_arr_used += 4;

    expr->vector.size = 1;
    BSLType *type;
    size_t vector_members = 0;
    if (parser->expr_arr_used + 1 > BSL_MAX_EXPR_ARR)
    {
      PARSER_LOG_TOKEN(parser, parser_peek(parser),
                       "exceeded maximum number of vector parameters (%d)",
                       BSL_MAX_EXPR_ARR);
      return false;
    }
    if (!parse_expr(parser, &expr->vector.exprs[0]))
    {
      return false;
    }
    type = expr->vector.exprs[0]->type;
    if (type->type == BSL_TYPE_VECTOR)
    {
      vector_members += type->size;
      type = type->subtype;
    } else
    {
      vector_members++;
    }
    while (parser_next(parser).t == BSL_TOKEN_COMMA)
    {
      if (parser->expr_arr_used + expr->vector.size > BSL_MAX_EXPR_ARR)
      {
        PARSER_LOG_TOKEN(parser, parser_peek(parser),
                         "exceeded maximum number of vector parameters (%d)",
                         BSL_MAX_EXPR_ARR);
        return false;
      }
      BSLToken tok = parser_peek(parser);
      BSLExpr *subexpr;
      if (!parse_expr(parser, &expr->vector.exprs[expr->vector.size++]))
      {
        return false;
      }
      subexpr = expr->vector.exprs[expr->vector.size - 1];
      if ((subexpr->type->type == BSL_TYPE_VECTOR &&
           subexpr->type->subtype != type)
          && (subexpr->type != type))
      {
        PARSER_LOG_TOKEN(parser, tok,
                         "all types in vector exprsesion are not the same");
        return false;
      }
      if (subexpr->type->type == BSL_TYPE_VECTOR)
      {
        vector_members += subexpr->type->size;
      } else
      {
        vector_members++;
      }
    }
    expr->type = make_vector_type(parser, type, vector_members);
    break;
  }
  default:
    PARSER_LOG_TOKEN(parser, token, "expected expression");
    return false;
    break;
  }
  *out = expr;
  return true;
}

bool add_inst(BSLParser *parser, BSLProcedure *proc, uint32_t size,
              uint32_t op, ...)
{
  va_list args;
  va_start(args, op);

  RESERVE_CODE(size + 1);

  proc->code[proc->code_sz++] = (((size + 1) << 16) | op);
  for (uint32_t i = 0; i < size; i++)
  {
    proc->code[proc->code_sz++] = va_arg(args, uint32_t);
  }

  return true;
}

BSLConstant *alloc_float_constant(BSLParser *parser, float f)
{
  for (uint32_t i = 0; i < parser->constants_used; i++)
  {
    BSLConstant *constant = &parser->constants[i];
    if (constant->t == BSL_CONSTANT_FLOAT &&
        constant->f == f)
    {
      return constant;
    }
  }
  BSLConstant *constant = alloc_constant(parser, BSL_CONSTANT_FLOAT);
  constant->f = f;
  return constant;
}

BSLConstant *alloc_constant(BSLParser *parser, int type)
{
  if (parser->constants_used + 1 > BSL_MAX_CONSTANTS)
  {
    parser_log_error(parser, parser->start_line, parser->start_col,
                     "exceeded maximum number of constants (%d)",
                     BSL_MAX_CONSTANTS);
    return NULL;
  }

  BSLConstant *ret = &parser->constants[parser->constants_used++];
  ret->t = type;
  ret->spirv_addr = GENSYM();
  return ret;
}

BSLLocal *lookup_var(BSLParser *parser, const char *name, size_t len)
{
  for (int32_t i = parser->scopes[parser->scope_top] - 1; i >= 0; i--)
  {
    if (len == parser->locals[i].len &&
        strncmp(name, parser->locals[i].name, len) == 0)
    {
      return &parser->locals[i];
    }
  }
  return NULL;
}

BSLLocal *add_var(BSLParser *parser, const char *name, size_t len,
                  BSLType *type)
{
  if (parser->scopes[parser->scope_top] + 1 > BSL_MAX_LOCALS)
  {
    parser_log_error(parser, parser->start_line, parser->start_col,
                     "exceeded maximum number of local variables (%d)",
                     BSL_MAX_LOCALS);
    return NULL;
  }

  BSLLocal *local = &parser->locals[parser->scopes[parser->scope_top]];

  parser->scopes[parser->scope_top]++;
  local->name = name;
  local->len = len;
  local->global = NULL;
  local->type = type;
  return local;
}

bool push_scope(BSLParser *parser)
{
  if (parser->scope_top + 1 > BSL_MAX_NESTED_SCOPES)
  {
    parser_log_error(parser, parser->start_line, parser->start_col,
                     "exceeded maximum number of nested scopes (%d)",
                     BSL_MAX_NESTED_SCOPES);
    return false;
  }

  parser->scope_top++;
  parser->scopes[parser->scope_top] = parser->scopes[parser->scope_top - 1];
  return true;
}

bool pop_scope(BSLParser *parser)
{
  parser->scope_top--;
  return true;
}

bool add_interface(BSLParser *parser, BSLProcedure *proc, BSLLocal *local)
{
  if (local->global != NULL && local->global->io_type != -1)
  {
    for (uint32_t i = 0; i < proc->interface_count; i++)
    {
      if (proc->interfaces[i] == local->global)
      {
        return true;
      }
    }
  } else {
    return true;
  }
  if (parser->interfaces_used + proc->interface_count > BSL_MAX_INTERFACES)
  {
    parser_log_error(parser, parser->start_line, parser->start_col,
                     "exceeded maximum number of interfaces (%d)",
                     BSL_MAX_INTERFACES);
    return false;
  }
  proc->interfaces[proc->interface_count++] = local->global;
  return true;
}

bool generate_expr(BSLParser *parser, BSLProcedure *proc, BSLExpr *expr,
                   uint32_t *location)
{
  switch (expr->t)
  {
  case BSL_EXPR_FLOAT:
    *location = expr->constant->spirv_addr;
    return true;
  case BSL_EXPR_VAR:
    *location = GENSYM();
    ADD_INST(3, SpvOpLoad, expr->local->type->spirv_addr, *location,
             expr->local->spirv_addr);
    if (!add_interface(parser, proc, expr->local))
    {
      return false;
    }
    return true;
  case BSL_EXPR_VECTOR: {
    size_t sz = 3 + expr->vector.size;
    uint32_t temp;
    for (uint32_t i = 0; i < expr->vector.size; i++)
    {
      GENERATE_EXPR(expr->vector.exprs[i], &temp);
      parser->locations[i] = temp;
    }
    *location = GENSYM();
    RESERVE_CODE(sz);
    proc->code[proc->code_sz++] = (sz << 16) | SpvOpCompositeConstruct;
    proc->code[proc->code_sz++] = expr->type->spirv_addr;
    proc->code[proc->code_sz++] = *location;
    for (uint32_t i = 0; i < expr->vector.size; i++)
    {
      proc->code[proc->code_sz++] = parser->locations[i];
    }
    return true;
  }
  case BSL_EXPR_ADD: {
    uint32_t loc1, loc2;
    GENERATE_EXPR(expr->binop.left, &loc1);
    GENERATE_EXPR(expr->binop.right, &loc2);
    *location = GENSYM();
    RESERVE_CODE(5);
    proc->code[proc->code_sz++] = (5 << 16) | SpvOpFAdd;
    proc->code[proc->code_sz++] = expr->type->spirv_addr;
    proc->code[proc->code_sz++] = *location;
    proc->code[proc->code_sz++] = loc1;
    proc->code[proc->code_sz++] = loc2;
    return true;
  }
  case BSL_EXPR_SUB: {
    uint32_t loc1, loc2;
    GENERATE_EXPR(expr->binop.left, &loc1);
    GENERATE_EXPR(expr->binop.right, &loc2);
    *location = GENSYM();
    RESERVE_CODE(5);
    proc->code[proc->code_sz++] = (5 << 16) | SpvOpFSub;
    proc->code[proc->code_sz++] = expr->type->spirv_addr;
    proc->code[proc->code_sz++] = *location;
    proc->code[proc->code_sz++] = loc1;
    proc->code[proc->code_sz++] = loc2;
    return true;
  }
  case BSL_EXPR_SCALAR_MUL: {
    uint32_t loc1, loc2;
    GENERATE_EXPR(expr->scalar.scalar, &loc1);
    GENERATE_EXPR(expr->scalar.vector, &loc2);
    *location = GENSYM();
    RESERVE_CODE(5);
    proc->code[proc->code_sz++] = (5 << 16) | SpvOpVectorTimesScalar;
    proc->code[proc->code_sz++] = expr->type->spirv_addr;
    proc->code[proc->code_sz++] = *location;
    proc->code[proc->code_sz++] = loc2;
    proc->code[proc->code_sz++] = loc1;
    return true;
  };
  }

  parser_log_error(parser, parser->start_line, parser->start_col,
                   "Cannot generate expression type: %d", expr->t);
  return false;
}

bool parse_stmt(BSLParser *parser, BSLProcedure *proc)
{
  BSLToken token;

  switch ((token = parser_peek(parser)).t)
  {
  case BSL_TOKEN_VAR:
  {
    parser_next(parser);
    BSLToken name;
    BSLType *type;
    BSLExpr *expr;
    EXPECT_TOKEN_WITH(name, BSL_TOKEN_SYM);
    EXPECT_TOKEN(BSL_TOKEN_COLON);
    if (!parse_type(parser, &type))
    {
      return false;
    }
    BSLLocal *local = add_var(parser, name.sym.start, name.sym.len, type);

    if (local == NULL)
    {
      return false;
    }

    local->ptr_type = make_pointer_type(parser, type, SpvStorageClassFunction);

    if (local->ptr_type == NULL)
    {
      return false;
    }
    EXPECT_TOKEN(BSL_TOKEN_EQ);
    if (!parse_expr(parser, &expr))
    {
      return false;
    }

    uint32_t val_loc;

    uint32_t var_loc = GENSYM();
    local->spirv_addr = var_loc;
    ADD_INST(3, SpvOpVariable, local->ptr_type->spirv_addr, var_loc,
             SpvStorageClassFunction);
    GENERATE_EXPR(expr, &val_loc);

    ADD_INST(2, SpvOpStore, var_loc, val_loc);
    EXPECT_TOKEN(BSL_TOKEN_SEMICOLON);
    return true;
  }
  case BSL_TOKEN_RETURN:
  {
    BSLExpr *expr;
    uint32_t location;
    parser_next(parser);
    if (parser_peek(parser).t == BSL_TOKEN_SEMICOLON)
    {
      EXPECT_TOKEN(BSL_TOKEN_SEMICOLON);
      RESERVE_CODE(1);
      proc->code[proc->code_sz++] = ((1 << 16) | SpvOpReturn);
      proc->has_returned = true;
      return true;
    }
    if (!parse_expr(parser, &expr))
    {
      return false;
    }
    EXPECT_TOKEN(BSL_TOKEN_SEMICOLON);
    GENERATE_EXPR(expr, &location);
    RESERVE_CODE(2);
    ADD_INST(1, SpvOpReturnValue, location);
    proc->has_returned = true;
    return true;
  }
  default: {
    BSLExpr *lhs, *rhs = NULL;
    BSLToken next;
    if (!parse_expr(parser, &lhs))
    {
      return false;
    }
    switch ((next = parser_peek(parser)).t)
    {
    case BSL_TOKEN_SEMICOLON: {
      uint32_t location;
      GENERATE_EXPR(lhs, &location);
      parser_next(parser);
      return true;
    }
    case BSL_TOKEN_ASSN: {
      parser_next(parser);
      if (!parse_expr(parser, &rhs))
      {
        return true;
      }
      if (rhs->type != lhs->type)
      {
        PARSER_LOG_TOKEN(parser, next, "cannot assign incompatible types");
        return false;
      }
      uint32_t new_val;
      GENERATE_EXPR(rhs, &new_val);
      if (lhs->t != BSL_EXPR_VAR)
      {
        PARSER_LOG_TOKEN(parser, token, "can only assign to variables");
        return false;
      }
      if (!add_interface(parser, proc, lhs->local))
      {
        return false;
      }
      ADD_INST(2, SpvOpStore, lhs->local->spirv_addr, new_val);
      EXPECT_TOKEN(BSL_TOKEN_SEMICOLON);
      return true;
    }
    default:
      PARSER_LOG_TOKEN(parser, next, "expected assignment or expression statement");
      return false;
    }


    return false;
  }
  }
}

bool parse_type(BSLParser *parser, BSLType **type)
{
  BSLToken name;
  BSLType *subtype;
  int size = 0;
  EXPECT_TOKEN_WITH(name, BSL_TOKEN_SYM);

  if (SYM_EQ(name, "vec2"))
  {
    EXPECT_TOKEN(BSL_TOKEN_LT);
    if (!parse_type(parser, &subtype))
    {
      return false;
    }
    size = 2;
    EXPECT_TOKEN(BSL_TOKEN_GT);
    return true;
  } else if (SYM_EQ(name, "vec3"))
  {
    EXPECT_TOKEN(BSL_TOKEN_LT);
    if (!parse_type(parser, &subtype))
    {
      return false;
    }
    size = 3;
    EXPECT_TOKEN(BSL_TOKEN_GT);
  } else if (SYM_EQ(name, "vec4"))
  {
    EXPECT_TOKEN(BSL_TOKEN_LT);
    if (!parse_type(parser, &subtype))
    {
      return false;
    }
    size = 4;
    EXPECT_TOKEN(BSL_TOKEN_GT);
  }

  if (size != 0)
  {
    *type = make_vector_type(parser, subtype, size);
    if (*type == NULL)
    {
      return false;
    }
    return true;
  }

  for (uint32_t i = 0; i < parser->types_used; i++)
  {
    if (parser->types[i].name != NULL &&
        SYM_EQN(name, parser->types[i].name, parser->types[i].len))
    {
      *type = &parser->types[i];
      return true;
    }
  }

  PARSER_LOG_TOKEN(parser, name, "expected type, not '%.*s'",
                   (int) name.sym.len, (char *) name.sym.start);
  return false;
}

bool parse_global(BSLParser *parser, int io_type)
{
  if (parser->globals_used + 1 > BSL_MAX_GLOBALS)
  {
    PARSER_LOG_TOKEN(parser, parser_next(parser),
                     "exceeded maximum number of globals (%d)",
                     BSL_MAX_GLOBALS);
    return false;
  }

  BSLGlobal *global = &parser->globals[parser->globals_used++];

  BSLToken name;

  global->spirv_addr = parser->next_spirv_addr++;
  EXPECT_TOKEN_WITH(name, BSL_TOKEN_SYM);
  EXPECT_TOKEN(BSL_TOKEN_COLON);

  if (!parse_type(parser, &global->type))
  {
    return false;
  }

  SpvStorageClass class;
  switch (io_type)
  {
  case -1:
    class = SpvStorageClassPrivate;
    break;
  case BSL_GLOBAL_IN:
    class = SpvStorageClassInput;
    break;
  case BSL_GLOBAL_OUT:
    class = SpvStorageClassOutput;
    break;
  }

  global->name = name.sym.start;
  global->len = name.sym.len;
  global->ptr_type = make_pointer_type(parser, global->type, class);
  if (global->ptr_type == NULL)
  {
    return false;
  }

  global->local = add_var(parser, global->name, global->len, global->type);
  global->local->spirv_addr = global->spirv_addr;
  global->local->ptr_type = global->ptr_type;
  global->local->global = global;

  if (io_type != -1)
  {
    BSLToken at_tok;
    at_tok = parser_peek(parser);
    if (at_tok.t == BSL_TOKEN_AT)
    {
      parser_next(parser);
      BSLToken location;
      EXPECT_TOKEN_WITH(location, BSL_TOKEN_INTEGER);
      global->location = location.integer;
    } else {
      global->location = -1;
    }
  }

  global->builtin_flags = parser->next_builtin;
  global->io_type = io_type;
  return true;
}

bool parse_record(BSLParser *parser)
{
  BSLToken name, member_name;
  EXPECT_TOKEN_WITH(name, BSL_TOKEN_SYM);
  bool position_next = false;
  BSLRecordMember *members = parser->record_members +
    parser->record_members_used;
  int size = 0;
  while (parser_peek(parser).t != BSL_TOKEN_END)
  {
    BSLToken attrib_name, builtin_name;
    BSLRecordMember *member = &parser->record_members[
      parser->record_members_used++];
    size++;
    if (parser_peek(parser).t == BSL_TOKEN_LBRACKET)
    {
      parser_next(parser);
      EXPECT_TOKEN_WITH(attrib_name, BSL_TOKEN_SYM);
      if (!SYM_EQ(attrib_name, "builtin"))
      {
        PARSER_LOG_TOKEN(parser, attrib_name, "expected [builtin(x)] attribute"
                         "before record members");
        return false;
      }
      EXPECT_TOKEN(BSL_TOKEN_LPAREN);
      EXPECT_TOKEN_WITH(builtin_name, BSL_TOKEN_SYM);
      if (!SYM_EQ(builtin_name, "position"))
      {
        PARSER_LOG_TOKEN(parser, builtin_name, "expected builtin type, not '%s'",
                         (int) builtin_name.sym.len,
                         (char *) builtin_name.sym.start);
        return false;
      }
      EXPECT_TOKEN(BSL_TOKEN_RPAREN);
      EXPECT_TOKEN(BSL_TOKEN_RBRACKET);
      position_next = true;
    }

    EXPECT_TOKEN_WITH(member_name, BSL_TOKEN_SYM);
    member->name = member_name.sym.start;
    member->len = member_name.sym.len;
    EXPECT_TOKEN(BSL_TOKEN_COLON);
    BSLToken first_type_token;
    first_type_token = parser_peek(parser);
    if (!parse_type(parser, &member->type))
    {
      return false;
    }
    EXPECT_TOKEN_WITH(member_name, BSL_TOKEN_SEMICOLON);

    if (position_next)
    {
      if (member->type->type != BSL_TYPE_VECTOR ||
          member->type->size != 4 ||
          member->type->subtype->type != BSL_TYPE_F32)
      {
        PARSER_LOG_TOKEN(parser, first_type_token,
                         "expected vec4<f32> for builtin type position");
        return false;
      }
      member->flags |= BSL_BUILTIN_POSITION_BIT;
    }
  }

  parser_next(parser);

  BSLType *type = alloc_type(parser);
  if (type == NULL)
  {
    return false;
  }

  type->type = BSL_TYPE_RECORD;
  type->size = size;
  type->members = members;
  type->name = name.sym.start;
  type->len = name.sym.len;
  type->spirv_addr = parser->next_spirv_addr++;
  return true;
}

bool parse_procedure(BSLParser *parser)
{
  BSLProcedure *proc;
  if (parser->procedures_used + 1 > BSL_MAX_PROCEDURES)
  {
    parser_log_error(parser, parser->start_line, parser->start_col,
                     "exceeded maximum number of procedures in one shader (%d)",
                     BSL_MAX_PROCEDURES);
    return false;
  }

  BSLToken name;
  EXPECT_TOKEN_WITH(name, BSL_TOKEN_SYM);

  BSLToken token;

  EXPECT_TOKEN(BSL_TOKEN_LPAREN);
  EXPECT_TOKEN(BSL_TOKEN_RPAREN);
  EXPECT_TOKEN(BSL_TOKEN_ARROW);
  BSLType *return_type;
  if (!parse_type(parser, &return_type))
  {
    return false;
  }

  proc = &parser->procedures[parser->procedures_used++];
  proc->spirv_addr = parser->next_spirv_addr++;
  proc->type = make_procedure_type(parser, return_type);
  proc->code = parser->spirv + parser->spirv_used;
  proc->code_sz = 0;
  proc->has_returned = false;
  proc->interfaces = parser->interfaces + parser->interfaces_used;
  proc->interface_count = 0;
  if (proc->type == NULL)
  {
    return false;
  }
  proc->parameter_count = 0;

  if (!push_scope(parser))
  {
    return false;
  }

  ADD_INST(1, SpvOpLabel, GENSYM());

  while ((token = parser_peek(parser)).t != BSL_TOKEN_END &&
         token.t != BSL_TOKEN_ERROR)
  {
    if (proc->has_returned)
    {
      PARSER_LOG_TOKEN(parser, token,
                       "'return' must be the last statement in a block");
      return false;
    }

    if (!parse_stmt(parser, proc))
    {
      return false;
    }
  }

  BSLToken last = parser_next(parser);

  if (!proc->has_returned)
  {
    if (proc->type->subtype->type == BSL_TYPE_VOID)
    {
      proc->code[proc->code_sz++] = ((1 << 16) | SpvOpReturn);
    } else
    {
      PARSER_LOG_TOKEN(parser, last, "non-void function must return");
      return false;
    }
  }
  if (parser->has_error)
  {
    return false;
  }

  if (!pop_scope(parser))
  {
    return false;
  }

  if (parser->next_entry_point != -1)
  {
    if (parser->entry_points_used + 1 > BSL_MAX_ENTRY_POINTS)
    {
      PARSER_LOG_TOKEN(parser, name, "exceeded maximum entry points (%d)",
                       BSL_MAX_ENTRY_POINTS);
      return false;
    }

    BSLEntryPoint *point = &parser->entry_points[parser->entry_points_used++];
    point->proc = proc;
    point->spirv_addr = proc->spirv_addr;
    point->type = parser->next_entry_point;
    point->name = name.sym.start;
    point->len = name.sym.len;
  }

  parser->interfaces_used += proc->interface_count;
  parser->next_entry_point = -1;
  return true;
}

bool parse_attribute(BSLParser *parser)
{
  BSLToken attr_name;
  EXPECT_TOKEN_WITH(attr_name, BSL_TOKEN_SYM);
  if (SYM_EQ(attr_name, "entry_point"))
  {
    BSLToken entry_point_kind;
    EXPECT_TOKEN(BSL_TOKEN_LPAREN);
    EXPECT_TOKEN_WITH(entry_point_kind, BSL_TOKEN_SYM);
    EXPECT_TOKEN(BSL_TOKEN_RPAREN);
    if (SYM_EQ(entry_point_kind, "vertex"))
    {
      parser->next_entry_point = BSL_ENTRY_POINT_VERTEX;
    } else if (SYM_EQ(entry_point_kind, "fragment"))
    {
      parser->next_entry_point = BSL_ENTRY_POINT_FRAGMENT;
    } else
    {
      PARSER_LOG_TOKEN(parser, entry_point_kind,
                       "unknown entry point type '%.*s'",
                       (int) entry_point_kind.sym.len,
                       (char *) entry_point_kind.sym.start);
      return false;
    }
  }
  else if (SYM_EQ(attr_name, "builtin"))
  {
    BSLToken builtin_kind;
    EXPECT_TOKEN(BSL_TOKEN_LPAREN);
    EXPECT_TOKEN_WITH(builtin_kind, BSL_TOKEN_SYM);
    if (SYM_EQ(builtin_kind, "position"))
    {
      parser->next_builtin |= BSL_BUILTIN_POSITION_BIT;
    } else {
      PARSER_LOG_TOKEN(parser, builtin_kind,
                       "unknown builtin type '%.*s'",
                       (int) builtin_kind.sym.len,
                       (char *) builtin_kind.sym.start);
      return false;
    }
    EXPECT_TOKEN(BSL_TOKEN_RPAREN);
  }
  else {
    PARSER_LOG_TOKEN(parser, attr_name,
                     "unknown attribute '%.*s'",
                     (int) attr_name.sym.len,
                     (char *) attr_name.sym.start);
    return false;
  }
  EXPECT_TOKEN(BSL_TOKEN_RBRACKET);
  return true;
}

bool parse_toplevel(BSLParser *parser)
{
  BSLToken token;

  switch ((token = parser_next(parser)).t)
  {
  case BSL_TOKEN_LBRACKET:
    return parse_attribute(parser);
  case BSL_TOKEN_PROCEDURE:
    return parse_procedure(parser);
  case BSL_TOKEN_RECORD:
    return parse_record(parser);
  case BSL_TOKEN_IN:
    return parse_global(parser, BSL_GLOBAL_IN);
  case BSL_TOKEN_VAR:
    return parse_global(parser, -1);
  case BSL_TOKEN_OUT:
    return parse_global(parser, BSL_GLOBAL_OUT);
  }

  parser_log_error(parser, token.line, token.col, "expected toplevel");
  return false;
}

/* Values that begin every spirv file. */
const uint32_t spirv_init[] = {
  0x07230203,
  0x00010000,
  0x0,        /* No registered generator magic number. */
  0x0, /* Will contain bound. */
  0x0, /* Instruction scheme???. */
  (2 << 16) | SpvOpCapability,
  SpvCapabilityShader,
};

const SpvExecutionModel entry_point_type_map[] = {
  [BSL_ENTRY_POINT_VERTEX] = SpvExecutionModelVertex,
  [BSL_ENTRY_POINT_FRAGMENT] = SpvExecutionModelFragment,
};
const char gl450_spirv_extension[] = "GLSL.std.450";

bool pack_spirv(BSLParser *parser)
{
  size_t init_size = sizeof(spirv_init) / sizeof(uint32_t);
  size_t extension_size = 2 + ((sizeof(gl450_spirv_extension) + 3) / 4);
  size_t memory_model_size = 3;
  size_t entry_point_size = 0;
  for (int i = 0; i < parser->entry_points_used; i++)
  {
    entry_point_size += 3 + ((parser->entry_points[i].len + 4) / 4);
    entry_point_size += parser->entry_points[i].proc->interface_count;
    if (parser->entry_points[i].type == BSL_ENTRY_POINT_FRAGMENT)
    {
      entry_point_size += 3;
    }
  }

  size_t decorate_size = 0;
  size_t global_size = 0;
  for (int i = 0; i < parser->globals_used; i++)
  {
    global_size += 4;
    if (parser->globals[i].io_type != -1 && parser->globals[i].location != -1)
    {
      decorate_size += 4;
    }
    if (parser->globals[i].builtin_flags & BSL_BUILTIN_POSITION_BIT)
    {
      decorate_size += 5;
    }
  }

  for (int i = 0; i < parser->types_used; i++)
  {
    BSLType *type = &parser->types[i];
    if (type->type != BSL_TYPE_RECORD)
    {
      continue;
    }
    for (int j = 0; j < type->size; j++)
    {
      if (type->members[j].flags & BSL_BUILTIN_POSITION_BIT)
      {
        decorate_size += 5;
      }
    }
  }


  size_t type_size = 0;
  for (int i = 0; i < parser->types_used; i++)
  {
    BSLType *type = &parser->types[i];
    switch (type->type)
    {
    case BSL_TYPE_F32:
    case BSL_TYPE_F64:
      type_size += 3;
      break;
    case BSL_TYPE_VECTOR:
      type_size += 4;
      break;
    case BSL_TYPE_POINTER:
      type_size += 4;
      break;
    case BSL_TYPE_VOID:
      type_size += 2;
      break;
    case BSL_TYPE_PROCEDURE:
      type_size += 3;
      break;
    case BSL_TYPE_RECORD:
      type_size += 2 + (type->size);
      break;
    }
  }

  size_t constant_size = 0;
  for (uint32_t i = 0; i < parser->constants_used; i++)
  {
    BSLConstant *constant = &parser->constants[i];
    switch (constant->t)
    {
    case BSL_CONSTANT_FLOAT:
      constant_size += 4;
      break;
    }
  }

  size_t procedure_size = 0;
  for (uint32_t i = 0; i < parser->procedures_used; i++)
  {
    BSLProcedure *proc = &parser->procedures[i];
    procedure_size += 6;
  }

  procedure_size += parser->spirv_used;

  size_t size_needed = extension_size + init_size + memory_model_size +
    entry_point_size + decorate_size + global_size + type_size +
    constant_size + procedure_size;

  uint32_t *spirv = MIUR_ARR(uint32_t, size_needed);

  size_t cur_idx = 0;
  parser->result->spirv.data = (uint8_t *) spirv;
  parser->result->spirv.size = size_needed * sizeof(uint32_t);

  memcpy(spirv, spirv_init, init_size * sizeof(uint32_t));

  cur_idx += init_size;

  spirv[cur_idx++] = ((extension_size) << 16) | SpvOpExtInstImport;
  spirv[cur_idx++] = 1;
  memcpy(spirv + cur_idx, gl450_spirv_extension, sizeof(gl450_spirv_extension));
  cur_idx += (sizeof(gl450_spirv_extension) + 3) / 4;

  spirv[cur_idx++] = (3 << 16) | SpvOpMemoryModel;
  spirv[cur_idx++] = SpvAddressingModelLogical;
  spirv[cur_idx++] = SpvMemoryModelGLSL450;

  for (int i = 0; i < parser->entry_points_used; i++)
  {
    BSLEntryPoint *point = &parser->entry_points[i];
    size_t size = 3 + point->proc->interface_count + ((point->len + 4) / 4);
    spirv[cur_idx++] = (size << 16) | SpvOpEntryPoint;
    spirv[cur_idx++] = entry_point_type_map[point->type];
    spirv[cur_idx++] = point->spirv_addr;
    memcpy(spirv + cur_idx, point->name, point->len);
    cur_idx += (point->len + 4) / 4;
    for (uint32_t i = 0; i < point->proc->interface_count; i++)
    {
      spirv[cur_idx++] = point->proc->interfaces[i]->spirv_addr;
    }
  }

  for (int i = 0; i < parser->entry_points_used; i++)
  {
    BSLEntryPoint *point = &parser->entry_points[i];
    if (point->type == BSL_ENTRY_POINT_FRAGMENT)
    {
      spirv[cur_idx++] = (3 << 16) | SpvOpExecutionMode;
      spirv[cur_idx++] = point->spirv_addr;
      spirv[cur_idx++] = SpvExecutionModeOriginUpperLeft;
    }
  }

  for (int i = 0; i < parser->globals_used; i++)
  {
    if (parser->globals[i].io_type != -1 && parser->globals[i].location != -1)
    {
      spirv[cur_idx++] = (4 << 16) | SpvOpDecorate;
      spirv[cur_idx++] = parser->globals[i].spirv_addr;
      spirv[cur_idx++] = SpvDecorationLocation;
      spirv[cur_idx++] = parser->globals[i].location;
    }
    if (parser->globals[i].builtin_flags & BSL_BUILTIN_POSITION_BIT)
    {
      spirv[cur_idx++] = (4 << 16) | SpvOpDecorate;
      spirv[cur_idx++] = parser->globals[i].spirv_addr;
      spirv[cur_idx++] = SpvDecorationBuiltIn;
      spirv[cur_idx++] = SpvBuiltInPosition;
    }
  }

  for (int i = 0; i < parser->types_used; i++)
  {
    BSLType *type = &parser->types[i];
    if (type->type != BSL_TYPE_RECORD)
    {
      continue;
    }
    for (int j = 0; j < type->size; j++)
    {
      if (type->members[j].flags & BSL_BUILTIN_POSITION_BIT)
      {
        spirv[cur_idx++] = (5 << 16) | SpvOpMemberDecorate;
        spirv[cur_idx++] = type->spirv_addr;
        spirv[cur_idx++] = j;
        spirv[cur_idx++] = SpvDecorationBuiltIn;
        spirv[cur_idx++] = SpvBuiltInPosition;
      }
    }
  }

  for (int i = 0; i < parser->types_used; i++)
  {
    BSLType *type = &parser->types[i];
    switch (type->type)
    {
    case BSL_TYPE_F32: {
      spirv[cur_idx++] = (3 << 16) | SpvOpTypeFloat;
      spirv[cur_idx++] = type->spirv_addr;
      spirv[cur_idx++] = 32;
      break;
    }
    case BSL_TYPE_F64: {
      spirv[cur_idx++] = (3 << 16) | SpvOpTypeFloat;
      spirv[cur_idx++] = type->spirv_addr;
      spirv[cur_idx++] = 64;
      break;
    }
    case BSL_TYPE_VECTOR: {
      spirv[cur_idx++] = (4 << 16) | SpvOpTypeVector;
      spirv[cur_idx++] = type->spirv_addr;
      spirv[cur_idx++] = type->subtype->spirv_addr;
      spirv[cur_idx++] = type->size;
      break;
    }
    case BSL_TYPE_POINTER: {
      spirv[cur_idx++] = (4 << 16) | SpvOpTypePointer;
      spirv[cur_idx++] = type->spirv_addr;
      spirv[cur_idx++] = type->storage_class;
      spirv[cur_idx++] = type->subtype->spirv_addr;
      break;
    }
    case BSL_TYPE_PROCEDURE: {
      spirv[cur_idx++] = (3 << 16) | SpvOpTypeFunction;
      spirv[cur_idx++] = type->spirv_addr;
      spirv[cur_idx++] = type->subtype->spirv_addr;
      break;
    }
    case BSL_TYPE_RECORD: {
      spirv[cur_idx++] = ((2 + type->size) << 16) | SpvOpTypeStruct;

      spirv[cur_idx++] = type->spirv_addr;
      for (uint32_t i = 0; i < type->size; i++)
      {
        spirv[cur_idx++] = type->members[i].type->spirv_addr;
      }
      break;
    }
    case BSL_TYPE_VOID:  {
      spirv[cur_idx++] = (2 << 16) | SpvOpTypeVoid;
      spirv[cur_idx++] = type->spirv_addr;
    }
    }
  }

  for (int i = 0; i < parser->globals_used; i++)
  {
    BSLGlobal *global = &parser->globals[i];
    switch (global->io_type)
    {
    case -1:
      spirv[cur_idx++] = (4 << 16) | SpvOpVariable;
      spirv[cur_idx++] = global->ptr_type->spirv_addr;
      spirv[cur_idx++] = global->spirv_addr;
      spirv[cur_idx++] = SpvStorageClassPrivate;
      break;
    case BSL_GLOBAL_IN:
      spirv[cur_idx++] = (4 << 16) | SpvOpVariable;
      spirv[cur_idx++] = global->ptr_type->spirv_addr;
      spirv[cur_idx++] = global->spirv_addr;
      spirv[cur_idx++] = SpvStorageClassInput;
      break;
    case BSL_GLOBAL_OUT:
      spirv[cur_idx++] = (4 << 16) | SpvOpVariable;
      spirv[cur_idx++] = global->ptr_type->spirv_addr;
      spirv[cur_idx++] = global->spirv_addr;
      spirv[cur_idx++] = SpvStorageClassOutput;
      break;
    }
  }

  for (uint32_t i = 0; i < parser->constants_used; i++)
  {
    BSLConstant *constant = &parser->constants[i];
    switch (constant->t)
    {
    case BSL_CONSTANT_FLOAT:
      spirv[cur_idx++] = (4 << 16) | SpvOpConstant;
      spirv[cur_idx++] = parser->types[BSL_F32_TYPE_INDEX].spirv_addr;
      spirv[cur_idx++] = constant->spirv_addr;
      spirv[cur_idx++] = constant->u32;
      break;
    }
  }

  for (int i = 0; i < parser->procedures_used; i++)
  {
    BSLProcedure *proc = &parser->procedures[i];
    spirv[cur_idx++] = (5 << 16) | SpvOpFunction;
    spirv[cur_idx++] = proc->type->subtype->spirv_addr;
    spirv[cur_idx++] = proc->spirv_addr;
    spirv[cur_idx++] = 0;
    spirv[cur_idx++] = proc->type->spirv_addr;

    memcpy(spirv + cur_idx, proc->code, proc->code_sz * sizeof(uint32_t));
    cur_idx += proc->code_sz;

    spirv[cur_idx++] = (1 << 16) | SpvOpFunctionEnd;
  }

  spirv[3] = parser->next_spirv_addr;

  parser->result->spirv.size = cur_idx * sizeof(uint32_t);
  return true;
}
