/**
 * SDL_shader_tools; tools for SDL GPU shader support.
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
#include "SDL_shader_ast.h"

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


/*
 * Source profile strings. !!! FIXME: put in public API eventually.
 */
#define SDL_SHADER_SRC_SDLSL_1_0 "sdlsl_1_0"


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
SDL_bool errorlist_add(ErrorList *list, const SDL_bool is_error, const char *fname, const Sint32 errpos, const char *str);
SDL_bool errorlist_add_fmt(ErrorList *list, const SDL_bool is_error, const char *fname, const Sint32 errpos, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(5);
SDL_bool errorlist_add_va(ErrorList *list, const SDL_bool is_error, const char *_fname, const Sint32 errpos, SDL_PRINTF_FORMAT_STRING const char *fmt, va_list va);
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
    TOKEN_PP_ERROR,  /* caught in the preprocessor, not passed to caller */
    TOKEN_PP_PRAGMA,
    TOKEN_PP_BAD,
    TOKEN_INCOMPLETE_STRING_LITERAL,
    TOKEN_INCOMPLETE_COMMENT,
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
    Sint32 line;
    Conditional *conditional_stack;
    SDL_SHADER_IncludeClose close_callback;
    const Define *current_define;
    struct IncludeState *next;
} IncludeState;

Token preprocessor_lexer(IncludeState *s);  /* this is the interface to the re2c-generated code. */

void SDL_SHADER_print_debug_token(const char *subsystem, const char *token, const size_t tokenlen, const Token tokenval);


/* This tracks data types and variables, and notes when they enter/leave scope. */

typedef SDL_SHADER_AstDataType DataType;

typedef enum DataTypeType
{
    DT_VOID,
    DT_BOOLEAN,
    DT_INT,
    DT_UINT,
    DT_HALF,
    DT_FLOAT,
    DT_VECTOR,
    DT_MATRIX,
    DT_ARRAY,
    DT_STRUCT
} DataTypeType;

typedef struct DataTypeArray
{
    const DataType *childdt;
    Uint32 elements;
} DataTypeArray;

typedef struct DataTypeMatrix
{
    const DataType *childdt;   /* MUST be a vector type (that plus `rows` makes a matrix) */
    Uint32 rows;
} DataTypeMatrix;

typedef struct DataTypeStructMembers
{
    const char *name;
    const DataType *dt;
} DataTypeStructMembers;

typedef struct DataTypeStruct
{
    Uint32 num_members;
    const DataTypeStructMembers *members;
} DataTypeStruct;

struct SDL_SHADER_AstDataType
{
    const char *name;  /* strcache'd */
    DataTypeType dtype;
    union
    {
        DataTypeArray vector;
        DataTypeMatrix matrix;
        DataTypeArray array;
        DataTypeStruct structure;
    } info;
};

typedef struct ScopeItem
{
    SDL_SHADER_AstNode *ast;
    struct ScopeItem *next;
} ScopeItem;

typedef struct Context
{
    SDL_bool isfail;
    SDL_bool out_of_memory;
    SDL_SHADER_Malloc malloc;
    SDL_SHADER_Free free;
    void *malloc_data;
    const char *filename;  /* comes from a stringcache, don't free or modify it! */
    Sint32 position;
    ErrorList *errors;

    /* preprocessor stuff... */
    SDL_bool uses_preprocessor;
    SDL_bool asm_comments;
    SDL_bool parsing_pragma;
    SDL_bool allow_dotdot_includes;  /* if SDL_FALSE, fail on `#include "path/with/../in/it"` */
    SDL_bool allow_absolute_includes;  /* if SDL_FALSE, fail on `#include "/absolute/path"` */
    Conditional *conditional_pool;
    IncludeState *include_stack;
    IncludeState *include_pool;
    Define *define_hashtable[256];
    Define *define_pool;
    Define *file_macro;
    Define *line_macro;
    StringCache *filename_cache;
    const char **system_include_paths;
    size_t system_include_path_count;
    const char **local_include_paths;
    size_t local_include_path_count;
    SDL_SHADER_IncludeOpen open_callback;
    SDL_SHADER_IncludeClose close_callback;

    /* AST stuff ... */
    SDL_bool uses_ast;
    const char *source_profile;  /* static string, don't free */
    SDL_SHADER_AstShader *shader;  /* Abstract Syntax Tree */
    StringCache *strcache;

    /* compiler stuff... */
    SDL_bool uses_compiler;
    SDL_bool isiced;  /* triggered an Internal Compiler Error. */
    SDL_SHADER_AstFunction *functions;  /* global function linked list, linked on nextfn (do not free) */
    SDL_SHADER_AstStructDeclaration *structs;  /* global struct decl linked list, linked on nextstruct (do not free) */
    HashTable *datatypes;
    SDL_SHADER_AstNodeInfo ast_before;  /* for fail_ast's use, for errors that count as "before" the source file */
    SDL_SHADER_AstNodeInfo ast_after;  /* for fail_ast's use, for errors that count as "after" the source file */
    const DataType *datatype_void;  /* just a pointer to a value in datatypes (do not free) */
    const DataType *datatype_int;  /* just a pointer to a value in datatypes (do not free) */
    const DataType *datatype_float;  /* just a pointer to a value in datatypes (do not free) */
    const DataType *datatype_boolean;  /* just a pointer to a value in datatypes (do not free) */
    ScopeItem *scope_stack;
    ScopeItem *scope_pool;
    Uint8 *compile_output;
    size_t compile_output_len;
    SDL_bool reported_undefined;
    const char *undefined_identifiers[16];
    size_t num_undefined_identifiers;

#if 0 /* !!! FIXME, compiler code isn't built into the project yet! */
    SymbolMap variables;
    SDL_bool is_func_scope; // SDL_TRUE if semantic analysis is in function scope.
    Uint32 loop_count;
    Uint32 switch_count;
    Sint32 var_index;  // next variable index for current function.
    Sint32 global_var_index;  // next variable index for global scope.
    Sint32 user_func_index;  // next function index for user-defined functions.
    Sint32 intrinsic_func_index;  // next function index for intrinsic functions.

    Buffer *garbage;  // this is sort of hacky.
#endif
} Context;

Context *context_create(SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d);
void context_destroy(Context *ctx);

/* This will only fail if the allocator fails, so it doesn't return any error code...NULL on failure. */
SDL_bool preprocessor_start(Context *ctx, const SDL_SHADER_CompilerParams *params, SDL_bool asm_comments);

void preprocessor_end(Context *ctx);  /* destroying the context will call this for you, too. Safe to call directly as well. */
const char *preprocessor_nexttoken(Context *ctx, size_t *_len, Token *_token);

void ast_end(Context *ctx);
void compiler_end(Context *ctx);

Context *parse_to_ast(const SDL_SHADER_CompilerParams *params);


/* Somehow there isn't an SDL_memchr ... */
const void *MemChr(const void *buf, const Uint8 b, size_t buflen);

/* Can't use SDL_strdup because we need to handle custom allocators and Context::out_of_memory */
char *StrDup(Context *ctx, const char *str);

/* Helpers for memory allocation inside a Context */
void *Malloc(Context *ctx, const size_t len);
void Free(Context *ctx, void *ptr);

/* These are for things that need SDL_SHADER_Malloc/Free and want to use a Context's
   existing allocators. The "data" must be the Context pointer. */
void *MallocContextBridge(size_t bytes, void *data);
void FreeContextBridge(void *ptr, void *data);

void fail(Context *ctx, const char *reason);
void failf(Context *ctx, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
void warn(Context *ctx, const char *reason);
void warnf(Context *ctx, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
void fail_ast(Context *ctx, const SDL_SHADER_AstNodeInfo *ast, const char *reason);
void failf_ast(Context *ctx, const SDL_SHADER_AstNodeInfo *ast, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(3);
void warn_ast(Context *ctx, const SDL_SHADER_AstNodeInfo *ast, const char *reason);
void warnf_ast(Context *ctx, const SDL_SHADER_AstNodeInfo *ast, SDL_PRINTF_FORMAT_STRING const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(3);

SDL_FORCE_INLINE SDL_bool operator_is_unary(const SDL_SHADER_AstNodeType op)
{
    return ( (op > SDL_SHADER_AST_OP_START_RANGE_UNARY) && (op < SDL_SHADER_AST_OP_END_RANGE_UNARY) ) ? SDL_TRUE : SDL_FALSE;
}

SDL_FORCE_INLINE int operator_is_binary(const SDL_SHADER_AstNodeType op)
{
    return ( (op > SDL_SHADER_AST_OP_START_RANGE_BINARY) && (op < SDL_SHADER_AST_OP_END_RANGE_BINARY) ) ? SDL_TRUE : SDL_FALSE;
}

SDL_FORCE_INLINE int operator_is_ternary(const SDL_SHADER_AstNodeType op)
{
    return ( (op > SDL_SHADER_AST_OP_START_RANGE_TERNARY) && (op < SDL_SHADER_AST_OP_END_RANGE_TERNARY) ) ? SDL_TRUE : SDL_FALSE;
}

#endif  /* _INCLUDE_SDL_SHADER_INTERNAL_H_ */

/* end of SDL_shader_internal.h ... */

