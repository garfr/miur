/*
 * MIT License
 *
 * Copyright (c) 2010 Serge Zaitsev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <math.h>

#include <miur/json.h>
#include <miur/mem.h>
#include <miur/log.h>

/**
 * JSON parser. Contains an array of token blocks available. Also stores
 * the string being parsed now and current position in that string.
 */
typedef struct JsonParser {
  unsigned int pos;     /* offset in the JSON string */
  unsigned int toknext; /* next token to allocate */
  int toksuper;         /* superior token node, e.g. parent object or array */
} JsonParser;

/**
 * Create JSON parser over an array of tokens
 */
void json_init(JsonParser *parser);

/**
 * Run JSON parser. It parses a JSON data string into and array of tokens, each
 * describing
 * a single JSON object.
 */
int json_parse(JsonParser *parser, const char *js, const size_t len,
              JsonTok *tokens, const unsigned int num_tokens);

/**
 * Allocates a fresh unused token from the token pool.
 */
static JsonTok *json_alloc_token(JsonParser *parser, JsonTok *tokens,
                                   const size_t num_tokens) {
  JsonTok *tok;
  if (parser->toknext >= num_tokens) {
    return NULL;
  }
  tok = &tokens[parser->toknext++];
  tok->start = tok->end = -1;
  tok->size = 0;
  return tok;
}

/**
 * Fills token type and boundaries.
 */
static void json_fill_token(JsonTok *token, const JsonType type,
                            const int start, const int end) {
  token->type = type;
  token->start = start;
  token->end = end;
  token->size = 0;
}

/**
 * Fills next available token with JSON primitive.
 */
static int json_parse_primitive(JsonParser *parser, const char *js,
                                const size_t len, JsonTok *tokens,
                                const size_t num_tokens, JsonType typee) {
  JsonTok *token;
  int start;

  start = parser->pos;

  for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
    switch (js[parser->pos]) {
    case '\t':
    case '\r':
    case '\n':
    case ' ':
    case ',':
    case ']':
    case '}':
      goto found;
    default:
                   /* to quiet a warning from gcc*/
      break;
    }
    if (js[parser->pos] < 32 || js[parser->pos] >= 127) {
      parser->pos = start;
      return JSON_ERROR_INVAL;
    }
  }
  /* In strict mode primitive must be followed by a comma/object/array */
  parser->pos = start;
  return JSON_ERROR_PART;

found:
  if (tokens == NULL) {
    parser->pos--;
    return 0;
  }
  token = json_alloc_token(parser, tokens, num_tokens);
  if (token == NULL) {
    parser->pos = start;
    return JSON_ERROR_NOMEM;
  }
  json_fill_token(token, typee, start, parser->pos);
  parser->pos--;
  return 0;
}

/**
 * Fills next token with JSON string.
 */
static int json_parse_string(JsonParser *parser, const char *js,
                             const size_t len, JsonTok *tokens,
                             const size_t num_tokens) {
  JsonTok *token;

  int start = parser->pos;

  /* Skip starting quote */
  parser->pos++;

  for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
    char c = js[parser->pos];

    /* Quote: end of string */
    if (c == '\"') {

      if (tokens == NULL) {
        return 0;
      }
      token = json_alloc_token(parser, tokens, num_tokens);
      if (token == NULL) {

        parser->pos = start;
        return JSON_ERROR_NOMEM;
      }

      json_fill_token(token, JSON_STRING, start + 1, parser->pos);
      return 0;
    }

    /* Backslash: Quoted symbol expected */
    if (c == '\\' && parser->pos + 1 < len) {
      int i;
      parser->pos++;
      switch (js[parser->pos]) {
      /* Allowed escaped symbols */
      case '\"':
      case '/':
      case '\\':
      case 'b':
      case 'f':
      case 'r':
      case 'n':
      case 't':
        break;
      /* Allows escaped symbol \uXXXX */
      case 'u':
        parser->pos++;
        for (i = 0; i < 4 && parser->pos < len && js[parser->pos] != '\0';
             i++) {
          /* If it isn't a hex character we have an error */
          if (!((js[parser->pos] >= 48 && js[parser->pos] <= 57) ||   /* 0-9 */
                (js[parser->pos] >= 65 && js[parser->pos] <= 70) ||   /* A-F */
                (js[parser->pos] >= 97 && js[parser->pos] <= 102))) { /* a-f */
            parser->pos = start;
            return JSON_ERROR_INVAL;
          }
          parser->pos++;
        }
        parser->pos--;
        break;
      /* Unexpected symbol */
      default:
        parser->pos = start;
        return JSON_ERROR_INVAL;
      }
    }
  }
  parser->pos = start;
  return JSON_ERROR_PART;
}

/**
 * Parse JSON string and fill tokens.
 */
int json_parse(JsonParser *parser, const char *js, const size_t len,
               JsonTok *tokens, const unsigned int num_tokens) {
  int r;
  int i;
  JsonTok *token;
  int count = parser->toknext;

  for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
    char c;
    JsonType type;

    c = js[parser->pos];

    switch (c) {
    case '{':
    case '[':
      count++;
      if (tokens == NULL) {
        break;
      }
      token = json_alloc_token(parser, tokens, num_tokens);
      if (token == NULL) {
        return JSON_ERROR_NOMEM;
      }
      if (parser->toksuper != -1) {
        JsonTok *t = &tokens[parser->toksuper];
        /* In strict mode an object or array can't become a key */
        if (t->type == JSON_OBJECT) {
          return JSON_ERROR_INVAL;
        }
        t->size++;
      }
      token->type = (c == '{' ? JSON_OBJECT : JSON_ARRAY);
      token->start = parser->pos;
      parser->toksuper = parser->toknext - 1;
      break;
    case '}':
    case ']':
      if (tokens == NULL) {
        break;
      }
      type = (c == '}' ? JSON_OBJECT : JSON_ARRAY);
      for (i = parser->toknext - 1; i >= 0; i--) {
        token = &tokens[i];
        if (token->start != -1 && token->end == -1) {
          if (token->type != type) {
            return JSON_ERROR_INVAL;
          }
          parser->toksuper = -1;
          token->end = parser->pos + 1;
          break;
        }
      }
      /* Error if unmatched closing bracket */
      if (i == -1) {
        return JSON_ERROR_INVAL;
      }
      for (; i >= 0; i--) {
        token = &tokens[i];
        if (token->start != -1 && token->end == -1) {
          parser->toksuper = i;
          break;
        }
      }
      break;
    case '\"':
      r = json_parse_string(parser, js, len, tokens, num_tokens);
      if (r < 0) {
        return r;
      }
      count++;
      if (parser->toksuper != -1 && tokens != NULL) {
        tokens[parser->toksuper].size++;
      }
      break;
    case '\t':
    case '\r':
    case '\n':
    case ' ':
      break;
    case ':':
      parser->toksuper = parser->toknext - 1;
      break;
    case ',':
      if (tokens != NULL && parser->toksuper != -1 &&
          tokens[parser->toksuper].type != JSON_ARRAY &&
          tokens[parser->toksuper].type != JSON_OBJECT) {
        for (i = parser->toknext - 1; i >= 0; i--) {
          if (tokens[i].type == JSON_ARRAY || tokens[i].type == JSON_OBJECT) {
            if (tokens[i].start != -1 && tokens[i].end == -1) {
              parser->toksuper = i;
              break;
            }
          }
        }
      }
      break;
    /* In strict mode primitives are: numbers and booleans */
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case 't':
    case 'f':
    case 'n': {
      /* And they must not be keys of the object */
      if (tokens != NULL && parser->toksuper != -1) {
        const JsonTok *t = &tokens[parser->toksuper];
        if (t->type == JSON_OBJECT ||
            (t->type == JSON_STRING && t->size != 0)) {
          return JSON_ERROR_INVAL;
        }
      }
      JsonType typee;
      if (c == '-' || isdigit(c))
      {
        typee = JSON_NUMBER;
      }
      else if (c == 'f')
      {
        typee = JSON_FALSE;
      }
      else if (c == 't')
      {
        typee = JSON_TRUE;
      }
      else if (c == 'n')
      {
        typee = JSON_NULL;
      }
      r = json_parse_primitive(parser, js, len, tokens, num_tokens, typee);
      if (r < 0) {
        return r;
      }
      count++;
      if (parser->toksuper != -1 && tokens != NULL) {
        tokens[parser->toksuper].size++;
      }
      break;

    }
    /* Unexpected char in strict mode */
    default:
      return JSON_ERROR_INVAL;
    }
  }

  if (tokens != NULL) {
    for (i = parser->toknext - 1; i >= 0; i--) {
      /* Unmatched opened object or array */
      if (tokens[i].start != -1 && tokens[i].end == -1) {
        return JSON_ERROR_PART;
      }
    }
  }

  return count;
}

/**
 * Creates a new parser based over a given buffer with an array of tokens
 * available.
 */
void json_init(JsonParser *parser) {
  parser->pos = 0;
  parser->toknext = 0;
  parser->toksuper = -1;
}

void json_stream_init(JsonStream *stream, Membuf buf)
{
  JsonParser parser;
  json_init(&parser);

  stream->eof.type = JSON_EOF;
  stream->cur = 0;
  stream->toks_size = json_parse(&parser, buf.data, buf.size, NULL, 0);
  stream->toks = MIUR_ARR(JsonTok, stream->toks_size);
  stream->buf = buf;
  json_init(&parser);
  json_parse(&parser, buf.data, buf.size, stream->toks,
             stream->toks_size);
}

void json_print(JsonStream *stream, JsonTok tok)
{
  switch (tok.type)
  {
  case JSON_PRIMITIVE:
    MIUR_LOG_INFO("JsonPrimitive");
    break;
  case JSON_NUMBER:
    MIUR_LOG_INFO("JsonNumber: %f", json_get_number(stream, tok));
    break;
  case JSON_FALSE:
    MIUR_LOG_INFO("JsonFalse");
    break;
  case JSON_TRUE:
    MIUR_LOG_INFO("JsonTrue");
    break;
  case JSON_NULL:
    MIUR_LOG_INFO("JsonNull");
    break;
  case JSON_UNDEFINED:
    MIUR_LOG_INFO("JsonUndefined");
    break;
  case JSON_OBJECT:
    MIUR_LOG_INFO("JsonObject");
    break;
  case JSON_ARRAY:
    MIUR_LOG_INFO("JsonArray");
    break;
  case JSON_STRING: {
    String str = json_get_string(stream, tok);
    MIUR_LOG_INFO("JsonString: '%.*s'", (int) str.size, str.data);
    break;
  }
  case JSON_EOF:
    MIUR_LOG_INFO("JsonEof");
    break;
  default:
    MIUR_LOG_ERR("Invalid JSON type: %d", tok.type);
    break;
  }
}

void json_stream_deinit(JsonStream *stream)
{
  MIUR_FREE(stream->toks);
}

double json_get_number(JsonStream *stream, JsonTok tok)
{
  double sum = 0.0;
  bool is_negative = false, has_exponent = false;
  int i = tok.start;
  if (stream->buf.data[i] == '-')
  {
    is_negative = true;
    i++;
  }

  for (; i < tok.end && isdigit(stream->buf.data[i]); i++)
  {
    sum *= 10.0;
    sum += (double) (stream->buf.data[i] - '0');
  }

  if (i < tok.end && stream->buf.data[i] == '.')
  {
    i++;
    double dec = 10.0;
    for (; i < tok.end && isdigit(stream->buf.data[i]); i++)
    {
      sum += ((double) (stream->buf.data[i] - '0')) / dec;
      dec *= 10.0;
    }
  }

  if (i < tok.end && (stream->buf.data[i] == 'e' ||
                      stream->buf.data[i] == 'E'))
  {
    double exp = 0.0;
    i++;
    for (; i < tok.end && isdigit(stream->buf.data[i]); i++)
    {
      exp *= 10.0;
      exp += ((double) (stream->buf.data[i] - '0'));
    }
    sum = pow(sum, exp);
  }

  if (is_negative)
  {
    sum *= -1.0;
  }

  return sum;
}

String json_get_string(JsonStream *stream, JsonTok tok)
{
  String str = {
    .data = stream->buf.data + tok.start,
    .size = tok.end - tok.start,
  };
  return str;
}

void json_get_position_info(JsonStream *stream, JsonTok tok, int *line_out,
                            int *col_out)
{
  int line = 1, col = 1;

  for (int i = 0; i < tok.start; i++)
  {
    col++;
    if (stream->buf.data[i] == '\n')
    {
      line++;
      col = 1;
    }
  }
  *line_out = line;
  *col_out = col;
}
