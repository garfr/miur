/* =====================
 * include/miur/json.h
 * 03/20/2022
 * JSON parsing.
 * ====================
 */

/*
 * This code is designed off of jsmn.h, a simple JSON tokenizer.
 * I have rewritten the library, making a few things more concise and adding
 * some extra functionality, to turn this into more of a "JSON recursive
 * descent utility library".
 */

#ifndef MIUR_JSON_H
#define MIUR_JSON_H

#include <miur/membuf.h>
#include <miur/string.h>

/**
 * JSON type identifier. Basic types are:
 * 	o Object
 * 	o Array
 * 	o String
 * 	o Other primitive: number, boolean (true/false) or null
 */
typedef enum {
  JSON_UNDEFINED,
  JSON_OBJECT,
  JSON_ARRAY,
  JSON_STRING,
  JSON_PRIMITIVE,
  JSON_NUMBER,
  JSON_TRUE,
  JSON_FALSE,
  JSON_NULL,
  JSON_EOF,
} JsonType;

enum JsonErr {
  /* Not enough tokens were provided */
  JSON_ERROR_NOMEM = -1,
  /* Invalid character inside JSON string */
  JSON_ERROR_INVAL = -2,
  /* The string is not a full JSON packet, more bytes expected */
  JSON_ERROR_PART = -3
};

/**
 * JSON token description.
 * type		type (object, array, string etc.)
 * start	start position in JSON data string
 * end		end position in JSON data string
 */
typedef struct {
  JsonType type;
  int start;
  int end;
  int size;
} JsonTok;

typedef struct
{
  JsonTok *toks;
  size_t toks_size;
  size_t cur;
  Membuf buf;
  JsonTok eof;
} JsonStream;

void json_stream_init(JsonStream *stream, Membuf buf);
void json_stream_deinit(JsonStream *stream);

#define JSON_NEXT(_stream) ((_stream)->cur < ((_stream)->toks_size) ?          \
                            ((_stream)->toks[(_stream)->cur++]) :              \
                            ((_stream)->eof))

#define JSON_PEEK(_stream) ((_stream)->cur < ((_stream)->toks_size) ?          \
                            ((_stream)->toks[(_stream)->cur]) :                \
                            ((_stream)->eof))

#define JSON_SKIP(_stream) ((_stream)->cur++)

#define JSON_IS_TYPE(_stream, _type) (JSON_PEEK(_stream).type == (_type))

#define JSON_FOR_ARRAY(_stream, _tok, _var) for (JsonTok __reserved_tok =  \
                                                   _tok, _var =         \
                                                   JSON_NEXT(_stream);  \
                                                 __reserved_tok.size >= 0; \
                                                 __reserved_tok.size--, \
                                                   _var =               \
                                                   JSON_NEXT(_stream))

#define JSON_FOR_OBJECT(_stream, _tok, _key) for (JsonTok _reserv = _tok, \
                                                    _key = JSON_NEXT(_stream); _reserv.size > 0; _reserv.size--, _key = JSON_NEXT(_stream))

#define JSON_EXPECT(_stream, _type) (JSON_IS_TYPE(_stream, _type) ? JSON_SKIP( \
                                       _stream), true : false)

#define JSON_EXPECT_WITH(_stream, _type, _out) (JSON_IS_TYPE(_stream, _type) ? \
                                                *_out = JSON_NEXT(_stream),    \
                                                true : (*_out =         \
                                                        JSON_NEXT(_stream), false))

void json_print(JsonStream *stream, JsonTok tok);

double json_get_number(JsonStream *stream, JsonTok tok);
String json_get_string(JsonStream *stream, JsonTok tok);

void json_get_position_info(JsonStream *stream, JsonTok tok, int *line,
                            int *col);

#endif
