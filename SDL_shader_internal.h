/**
 * SDL_shader_language; tools for SDL GPU shader support.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#ifndef _INCLUDE_SDL_SHADER_INTERNAL_H_
#define _INCLUDE_SDL_SHADER_INTERNAL_H_

#ifndef __SDL_SHADER_INTERNAL__
#error Do not include this header from your applications.
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "SDL_shader_compiler.h"

#define DEBUG_LEXER 0
#define DEBUG_PREPROCESSOR 0
#define DEBUG_ASSEMBLER_PARSER 0
#define DEBUG_COMPILER_PARSER 0
#define DEBUG_TOKENIZER \
    (DEBUG_PREPROCESSOR || DEBUG_ASSEMBLER_PARSER || DEBUG_LEXER)

/* Get basic wankery out of the way here... */

#ifdef __WINDOWS__
#define ENDLINE_STR "\r\n"
#else
#define ENDLINE_STR "\n"
#endif

typedef unsigned int uint;  /* this is a printf() helper. don't use for code. */

#ifdef _MSC_VER
#include <float.h>
#include <malloc.h>
#define va_copy(a, b) a = b
#define isnan _isnan // !!! FIXME: not a safe replacement!
#if _MSC_VER < 1900 // pre MSVC 2015
#define isinf(x) (!_isfinite(x)) // FIXME: not a safe replacement!
#endif
// Warning Level 4 considered harmful.  :)
#pragma warning(disable: 4100)  /* "unreferenced formal parameter" */
#pragma warning(disable: 4389)  /* "signed/unsigned mismatch" */
#endif

#ifdef sun
#include <alloca.h>
#endif


/* Hashtables... */

typedef struct HashTable HashTable;
typedef Uint32 (*HashTable_HashFn)(const void *key, void *data);
typedef int (*HashTable_KeyMatchFn)(const void *a, const void *b, void *data);
typedef void (*HashTable_NukeFn)(const void *key, const void *value, void *data);

HashTable *hash_create(void *data, const HashTable_HashFn hashfn,
                       const HashTable_KeyMatchFn keymatchfn,
                       const HashTable_NukeFn nukefn,
                       const SDL_bool stackable,
                       SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d);
void hash_destroy(HashTable *table);
int hash_insert(HashTable *table, const void *key, const void *value);
SDL_bool hash_remove(HashTable *table, const void *key);
SDL_bool hash_find(const HashTable *table, const void *key, const void **_value);
SDL_bool hash_iter(const HashTable *table, const void *key, const void **_value, void **iter);
SDL_bool hash_iter_keys(const HashTable *table, const void **_key, void **iter);

Uint32 hash_hash_string(const void *sym, void *unused);
int hash_keymatch_string(const void *a, const void *b, void *unused);


/* String -> String map ... */

typedef HashTable StringMap;
StringMap *stringmap_create(const int copy, SDL_SHADER_Malloc m,
                            SDL_SHADER_Free f, void *d);
void stringmap_destroy(StringMap *smap);
int stringmap_insert(StringMap *smap, const char *key, const char *value);
int stringmap_remove(StringMap *smap, const char *key);
SDL_bool stringmap_find(const StringMap *smap, const char *key, const char **_val);


/* String caching... */

typedef struct StringCache StringCache;
StringCache *stringcache_create(SDL_SHADER_Malloc m,SDL_SHADER_Free f,void *d);
const char *stringcache(StringCache *cache, const char *str);
const char *stringcache_len(StringCache *cache, const char *str, const size_t len);
const char *stringcache_fmt(StringCache *cache, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
SDL_bool stringcache_iscached(StringCache *cache, const char *str);
void stringcache_destroy(StringCache *cache);


/* Error lists... */

typedef struct ErrorList ErrorList;
ErrorList *errorlist_create(SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d);
SDL_bool errorlist_add(ErrorList *list, const char *fname, const int errpos, const char *str);
SDL_bool errorlist_add_fmt(ErrorList *list, const char *fname, const int errpos, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(4);
SDL_bool errorlist_add_va(ErrorList *list, const char *_fname, const int errpos, SDL_PRINTF_FORMAT_STRING const char *fmt, va_list va);
size_t errorlist_count(ErrorList *list);
SDL_SHADER_Error *errorlist_flatten(ErrorList *list); // resets the list!
void errorlist_destroy(ErrorList *list);



/* Dynamic buffers... */

typedef struct Buffer Buffer;
Buffer *buffer_create(size_t blksz, SDL_SHADER_Malloc m,SDL_SHADER_Free f,void *d);
char *buffer_reserve(Buffer *buffer, const size_t len);
SDL_bool buffer_append(Buffer *buffer, const void *_data, size_t len);
SDL_bool buffer_append_fmt(Buffer *buffer, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
SDL_bool buffer_append_va(Buffer *buffer, SDL_PRINTF_FORMAT_STRING const char *fmt, va_list va);
size_t buffer_size(Buffer *buffer);
void buffer_empty(Buffer *buffer);
char *buffer_flatten(Buffer *buffer);
char *buffer_merge(Buffer **buffers, const size_t n, size_t *_len);
void buffer_destroy(Buffer *buffer);
ssize_t buffer_find(Buffer *buffer, const size_t start, const void *data, const size_t len);



void * SDLCALL SDL_SHADER_internal_malloc(size_t bytes, void *d);
void SDLCALL SDL_SHADER_internal_free(void *ptr, void *d);

SDL_bool SDLCALL SDL_SHADER_internal_include_open(SDL_SHADER_IncludeType inctype,
                                     const char *fname, const char *parent,
                                     const char **outdata, size_t *outbytes,
                                     const char **include_paths, size_t include_path_count,
                                     char *failstr, size_t failstrlen,
                                     SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d);

void SDLCALL SDL_SHADER_internal_include_close(const char *data, SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d);


extern SDL_SHADER_Error SDL_SHADER_out_of_mem_error;


/* preprocessor stuff. */

typedef enum
{
    TOKEN_UNKNOWN = 256,  // start past ASCII character values.

    // These are all C-like constructs. Tokens < 256 may be single
    //  chars (like '+' or whatever). These are just multi-char sequences
    //  (like "+=" or whatever).
    TOKEN_IDENTIFIER,
    TOKEN_INT_LITERAL,
    TOKEN_FLOAT_LITERAL,
    TOKEN_STRING_LITERAL,
    TOKEN_RSHIFTASSIGN,
    TOKEN_LSHIFTASSIGN,
    TOKEN_ADDASSIGN,
    TOKEN_SUBASSIGN,
    TOKEN_MULTASSIGN,
    TOKEN_DIVASSIGN,
    TOKEN_MODASSIGN,
    TOKEN_XORASSIGN,
    TOKEN_ANDASSIGN,
    TOKEN_ORASSIGN,
    TOKEN_INCREMENT,
    TOKEN_DECREMENT,
    TOKEN_RSHIFT,
    TOKEN_LSHIFT,
    TOKEN_ANDAND,
    TOKEN_OROR,
    TOKEN_LEQ,
    TOKEN_GEQ,
    TOKEN_EQL,
    TOKEN_NEQ,
    TOKEN_HASH,
    TOKEN_HASHHASH,

    /* This is returned if the preprocessor isn't stripping comments. Note
       that in asm files, the ';' counts as a single-line comment, same as
       "//". Note that both eat newline tokens: all of the ones inside a
       multiline comment, and the ending newline on a single-line comment. */
    TOKEN_MULTI_COMMENT,
    TOKEN_SINGLE_COMMENT,

    /* This is returned at the end of input...no more to process. */
    TOKEN_EOI,

    /* This is returned for char sequences we think are bogus. You'll have
       to judge for yourself. In most cases, you'll probably just fail with
       bogus syntax without explicitly checking for this token. */
    TOKEN_BAD_CHARS,

    /* This is returned if there's an error condition (the error is returned
       as a NULL-terminated string from preprocessor_nexttoken(), instead
       of actual token data). You can continue getting tokens after this
       is reported. It happens for things like missing #includes, etc. */
    TOKEN_PREPROCESSING_ERROR,

    /* These are all caught by the preprocessor. Caller won't ever see them,
       except TOKEN_PP_PRAGMA.
       They control the preprocessor (#includes new files, etc). */
    TOKEN_PP_INCLUDE,
    TOKEN_PP_LINE,
    TOKEN_PP_DEFINE,
    TOKEN_PP_UNDEF,
    TOKEN_PP_IF,
    TOKEN_PP_IFDEF,
    TOKEN_PP_IFNDEF,
    TOKEN_PP_ELSE,
    TOKEN_PP_ELIF,
    TOKEN_PP_ENDIF,
    TOKEN_PP_ERROR,  /* caught, becomes TOKEN_PREPROCESSING_ERROR */
    TOKEN_PP_PRAGMA,
    TOKEN_INCOMPLETE_COMMENT,  /* caught, becomes TOKEN_PREPROCESSING_ERROR */
    TOKEN_PP_UNARY_MINUS,  /* used internally, never returned. */
    TOKEN_PP_UNARY_PLUS,   /* used internally, never returned. */
} Token;


/* This is opaque. */
struct Preprocessor;
typedef struct Preprocessor Preprocessor;

typedef struct Conditional
{
    Token type;
    int linenum;
    SDL_bool skipping;
    SDL_bool chosen;
    struct Conditional *next;
} Conditional;

typedef struct Define
{
    const char *identifier;
    const char *definition;
    const char *original;
    const char **parameters;
    int paramcount;
    struct Define *next;
} Define;

typedef struct IncludeState
{
    const char *filename;
    const char *source_base;
    const char *source;
    const char *token;
    size_t tokenlen;
    Token tokenval;
    SDL_bool pushedback;
    const unsigned char *lexer_marker;
    SDL_bool report_whitespace;
    SDL_bool asm_comments;
    size_t orig_length;
    size_t bytes_left;
    Uint32 line;
    Conditional *conditional_stack;
    SDL_SHADER_IncludeClose close_callback;
    struct IncludeState *next;
} IncludeState;

Token preprocessor_lexer(IncludeState *s);

/* This will only fail if the allocator fails, so it doesn't return any error code...NULL on failure. */
Preprocessor *preprocessor_start(const char *fname, const char *source,
                            size_t sourcelen,
                            const char **system_include_paths,
                            size_t system_include_path_count,
                            const char **local_include_paths,
                            size_t local_include_path_count,
                            SDL_SHADER_IncludeOpen open_callback,
                            SDL_SHADER_IncludeClose close_callback,
                            const SDL_SHADER_PreprocessorDefine *defines,
                            size_t define_count, SDL_bool asm_comments,
                            SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d);

void preprocessor_end(Preprocessor *pp);
SDL_bool preprocessor_outofmemory(Preprocessor *pp);
const char *preprocessor_nexttoken(Preprocessor *_ctx, size_t *_len, Token *_token);
const char *preprocessor_sourcepos(Preprocessor *pp, size_t *pos);


void SDL_SHADER_print_debug_token(const char *subsystem, const char *token, const size_t tokenlen, const Token tokenval);

#endif  /* _INCLUDE_SDL_SHADER_INTERNAL_H_ */

/* end of SDL_shader_internal.h ... */

