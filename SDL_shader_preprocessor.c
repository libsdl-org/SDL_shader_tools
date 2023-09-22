/**
 * SDL_shader_tools; tools for SDL GPU shader support.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#define __SDL_SHADER_INTERNAL__ 1
#include "SDL_shader_internal.h"

/* !!! FIXME: replace printf debugging with SDL_Log? */

#if DEBUG_PREPROCESSOR
    #define print_debug_token(token, len, val) \
        SDL_SHADER_print_debug_token("PREPROCESSOR", token, len, val)
#else
    #define print_debug_token(token, len, val)
#endif

#if DEBUG_LEXER
static Token debug_preprocessor_lexer(IncludeState *s)
{
    const Token retval = preprocessor_lexer(s);
    SDL_SHADER_print_debug_token("LEXER", s->token, s->tokenlen, retval);
    return retval;
}
#define preprocessor_lexer(s) debug_preprocessor_lexer(s)
#endif

#if DEBUG_TOKENIZER
static void print_debug_lexing_position(const Context *ctx)
{
    printf("NOW LEXING %s:%d ...\n", ctx->filename, (int) ctx->position);
}
#else
#define print_debug_lexing_position(s)
#endif

#if DEBUG_TOKENIZER
void SDL_SHADER_print_debug_token(const char *subsystem, const char *token,
                                  const size_t tokenlen, const Token tokenval)
{
    printf("%s TOKEN: \"", subsystem);
    size_t i;
    for (i = 0; i < tokenlen; i++) {
        if (token[i] == '\n') {
            printf("\\n");
        } else if (token[i] == '\\') {
            printf("\\\\");
        } else {
            printf("%c", token[i]);
        }
    }
    printf("\" (");
    switch (tokenval) {
        #define TOKENCASE(x) case x: printf("%s", #x); break
        TOKENCASE(TOKEN_UNKNOWN);
        TOKENCASE(TOKEN_IDENTIFIER);
        TOKENCASE(TOKEN_INT_LITERAL);
        TOKENCASE(TOKEN_FLOAT_LITERAL);
        TOKENCASE(TOKEN_STRING_LITERAL);
        TOKENCASE(TOKEN_ADDASSIGN);
        TOKENCASE(TOKEN_SUBASSIGN);
        TOKENCASE(TOKEN_MULTASSIGN);
        TOKENCASE(TOKEN_DIVASSIGN);
        TOKENCASE(TOKEN_MODASSIGN);
        TOKENCASE(TOKEN_XORASSIGN);
        TOKENCASE(TOKEN_ANDASSIGN);
        TOKENCASE(TOKEN_ORASSIGN);
        TOKENCASE(TOKEN_INCREMENT);
        TOKENCASE(TOKEN_DECREMENT);
        TOKENCASE(TOKEN_RSHIFT);
        TOKENCASE(TOKEN_LSHIFT);
        TOKENCASE(TOKEN_ANDAND);
        TOKENCASE(TOKEN_OROR);
        TOKENCASE(TOKEN_LEQ);
        TOKENCASE(TOKEN_GEQ);
        TOKENCASE(TOKEN_EQL);
        TOKENCASE(TOKEN_NEQ);
        TOKENCASE(TOKEN_HASH);
        TOKENCASE(TOKEN_HASHHASH);
        TOKENCASE(TOKEN_PP_INCLUDE);
        TOKENCASE(TOKEN_PP_LINE);
        TOKENCASE(TOKEN_PP_DEFINE);
        TOKENCASE(TOKEN_PP_UNDEF);
        TOKENCASE(TOKEN_PP_IF);
        TOKENCASE(TOKEN_PP_IFDEF);
        TOKENCASE(TOKEN_PP_IFNDEF);
        TOKENCASE(TOKEN_PP_ELSE);
        TOKENCASE(TOKEN_PP_ELIF);
        TOKENCASE(TOKEN_PP_ENDIF);
        TOKENCASE(TOKEN_PP_ERROR);
        TOKENCASE(TOKEN_PP_PRAGMA);
        TOKENCASE(TOKEN_PP_BAD);
        TOKENCASE(TOKEN_INCOMPLETE_STRING_LITERAL);
        TOKENCASE(TOKEN_INCOMPLETE_COMMENT);
        TOKENCASE(TOKEN_BAD_CHARS);
        TOKENCASE(TOKEN_SINGLE_COMMENT);
        TOKENCASE(TOKEN_MULTI_COMMENT);
        TOKENCASE(TOKEN_EOI);
        #undef TOKENCASE

        case ((Token) '\n'):
            printf("'\\n'");
            break;

        case ((Token) '\\'):
            printf("'\\\\'");
            break;

        default:
            SDL_assert(((int)tokenval) < 256);
            printf("'%c'", (char) tokenval);
            break;
    }
    printf(")\n");
}
#endif


static const char *attempt_include_open(const char *path, const char *fname,
                                        const char **outdata, size_t *outbytes,
                                        char *failstr, size_t failstrlen,
                                        SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d)
{
    char *ptr;
    Sint64 flen;
    SDL_RWops *io = NULL;
    const size_t len = SDL_strlen(path) + SDL_strlen(fname) + 3;
    char *fullpath = (char *) m(len, d);
    if (fullpath == NULL) {
        SDL_snprintf(failstr, failstrlen, "Out of memory");
        return NULL;
    }

    *failstr = '\0';

    SDL_snprintf(fullpath, len, "%s/%s", path, fname);

    ptr = fullpath;

    #if defined(__WINDOWS__) || defined(__OS2__)
    while (SDL_TRUE) {
        const char ch = *ptr;
        if (ch == '\0') {
            break;
        } else if (ch == '/') {
            *ptr = '\\';
        }
        ptr++;
    }
    #endif

    io = SDL_RWFromFile(fullpath, "rb");

    if (!io) {
        f(fullpath, d);
        return NULL;  /* !!! FIXME: fill in failstr if permission denied, etc. Leave alone for not found. */
    }

    flen = SDL_RWsize(io);
    if (flen < 0) {
        SDL_snprintf(failstr, failstrlen, "Failed to get file length of '%s': %s", fullpath, SDL_GetError());
        SDL_RWclose(io);
        f(fullpath, d);
        return NULL;
    }

    *outdata = m((size_t) flen, d);
    if (!*outdata) {
        SDL_snprintf(failstr, failstrlen, "Out of memory");
        SDL_RWclose(io);
        f(fullpath, d);
        return NULL;
    }

    if (SDL_RWread(io, (char *) *outdata, (size_t) flen, 1) != 1) {
        SDL_snprintf(failstr, failstrlen, "Failed to read '%s': %s", fullpath, SDL_GetError());
        SDL_RWclose(io);
        f((void *) *outdata, d);
        *outdata = NULL;
        f(fullpath, d);
        return NULL;
    }

    SDL_RWclose(io);

    *outbytes = (size_t) flen;
    return fullpath;
}

static const char *internal_include_open(SDL_SHADER_IncludeType inctype,
                                         const char *fname, const char *parent_fname,
                                         const char *parent_data,
                                         const char **outdata, size_t *outbytes,
                                         const char **include_paths, size_t include_path_count,
                                         char *failstr, size_t failstrlen,
                                         SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d)
{
    const char *rc = NULL;
    size_t i;

    if (parent_fname) {
        const size_t slen = SDL_strlen(parent_fname) + 1;
        char *ptr;
        char *ptr2;
        char *parent_dir = m(slen, d);
        if (!parent_dir) {
            SDL_snprintf(failstr, failstrlen, "Out of memory");
            return NULL;
        }
        SDL_snprintf(parent_dir, slen, "%s", parent_fname);
        ptr = SDL_strrchr(parent_dir, '/');
        ptr2 = SDL_strrchr(ptr ? (ptr + 1) : parent_dir, '\\');
        if (ptr2) {
            ptr = ptr2;
        }

        if (!ptr) {
            f(parent_dir, d);
        } else {
            if (ptr == parent_dir) {
                ptr++;  /* if this was going to turn "/absolute_path" into "", make it "/" instead. */
            }
            *ptr = '\0';  /* will open "parent_dir/fname" */
            rc = attempt_include_open(parent_dir, fname, outdata, outbytes, failstr, failstrlen, m, f, d);
            f(parent_dir, d);
            if (rc != NULL) {
                return rc;
            } else if (*failstr != '\0') {
                return NULL;
            }
        }
    }

    for (i = 0; i < include_path_count; i++) {
        rc = attempt_include_open(include_paths[i], fname, outdata, outbytes, failstr, failstrlen, m, f, d);
        if (rc != NULL) {
            return rc;
        } else if (*failstr != '\0') {
            return NULL;
        }
    }

    if (*failstr == '\0') {
        SDL_snprintf(failstr, failstrlen, "%s: no such file or directory", fname);
    }

    return NULL;
}

static void internal_include_close(const char *data, SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d)
{
    f((void *) data, d);
}


/* !!! FIXME: maybe use these pool magic elsewhere? */
/* !!! FIXME: maybe just get rid of this? (maybe the fragmentation isn't a big deal?) */

/* Pool stuff... */
/* ugh, I hate this macro salsa. */
#define FREE_POOL(type, poolname) \
    static void free_##poolname##_pool(Context *ctx) { \
        type *item = ctx->poolname##_pool; \
        while (item != NULL) { \
            type *next = item->next; \
            Free(ctx, item); \
            item = next; \
        } \
    }

#define GET_POOL(type, poolname) \
    static type *get_##poolname(Context *ctx) { \
        type *retval = ctx->poolname##_pool; \
        if (retval != NULL) { \
            ctx->poolname##_pool = retval->next; \
        } else { \
            retval = (type *) Malloc(ctx, sizeof (type)); \
        } \
        if (retval != NULL) { \
            SDL_zerop(retval); \
        } \
        return retval; \
    }

#define PUT_POOL(type, poolname) \
    static void put_##poolname(Context *ctx, type *item) { \
        item->next = ctx->poolname##_pool; \
        ctx->poolname##_pool = item; \
    }

#define IMPLEMENT_POOL(type, poolname) \
    FREE_POOL(type, poolname) \
    GET_POOL(type, poolname) \
    PUT_POOL(type, poolname)

IMPLEMENT_POOL(Conditional, conditional)
IMPLEMENT_POOL(IncludeState, include)
IMPLEMENT_POOL(Define, define)


/* Preprocessor define hashtable stuff... */

/* !!! FIXME: why isn't this using SDL_shader_common.c's code? */

/* this is djb's xor hashing function. */
static inline Uint32 hash_string_djbxor(const char *sym)
{
    Uint32 hash = 5381;
    while (*sym) {
        hash = ((hash << 5) + hash) ^ *(sym++);
    }
    return hash;
}

static inline Uint8 hash_define(const char *sym)
{
    return (Uint8) hash_string_djbxor(sym);
}

static int add_define(Context *ctx, const char *sym, const char *val,
                      char **parameters, int paramcount)
{
    const Uint8 hash = hash_define(sym);
    Define *bucket = ctx->define_hashtable[hash];
    while (bucket) {
        if (SDL_strcmp(bucket->identifier, sym) == 0) {
            warnf(ctx, "'%s' already defined", sym);
            /* !!! FIXME: gcc reports the location of previous #define here. */
            return 0;
        }
        bucket = bucket->next;
    }

    bucket = get_define(ctx);
    if (bucket == NULL) {
        return 0;
    }
    bucket->definition = val;
    bucket->original = NULL;
    bucket->identifier = sym;
    bucket->parameters = (const char **) parameters;
    bucket->paramcount = paramcount;
    bucket->next = ctx->define_hashtable[hash];
    ctx->define_hashtable[hash] = bucket;
    return 1;
}

static void free_define(Context *ctx, Define *def)
{
    if (def != NULL) {
        int i;
        for (i = 0; i < def->paramcount; i++) {
            Free(ctx, (void *) def->parameters[i]);
        }
        Free(ctx, (void *) def->parameters);
        Free(ctx, (void *) def->identifier);
        Free(ctx, (void *) def->definition);
        Free(ctx, (void *) def->original);
        put_define(ctx, def);
    }
}

static int remove_define(Context *ctx, const char *sym)
{
    const Uint8 hash = hash_define(sym);
    Define *bucket = ctx->define_hashtable[hash];
    Define *prev = NULL;
    while (bucket) {
        if (SDL_strcmp(bucket->identifier, sym) == 0) {
            if (prev == NULL) {
                ctx->define_hashtable[hash] = bucket->next;
            } else {
                prev->next = bucket->next;
            }
            free_define(ctx, bucket);
            return 1;
        }
        prev = bucket;
        bucket = bucket->next;
    }

    return 0;
}

static const Define *find_define(Context *ctx, const char *sym)
{
    const Uint8 filestrhash = 67;
    const Uint8 linestrhash = 75;
    const Uint8 hash = hash_define(sym);
    Define *bucket = ctx->define_hashtable[hash];
    while (bucket) {
        if (SDL_strcmp(bucket->identifier, sym) == 0) {
            return bucket;
        }
        bucket = bucket->next;
    }

    SDL_assert(hash_define("__FILE__") == filestrhash);
    SDL_assert(hash_define("__LINE__") == linestrhash);

    if ( (hash == filestrhash) && (ctx->file_macro) && (SDL_strcmp(sym, "__FILE__") == 0) ) {
        const IncludeState *state = ctx->include_stack;
        const char *fname = state ? state->filename : "";
        const size_t len = SDL_strlen(fname) + 3;
        char *str;

        Free(ctx, (char *) ctx->file_macro->definition);
        str = (char *) Malloc(ctx, len);
        if (!str) {
            return NULL;
        }
        str[0] = '\"';
        SDL_memcpy(str + 1, fname, len - 3);
        str[len - 2] = '\"';
        str[len - 1] = '\0';
        ctx->file_macro->definition = str;
        return ctx->file_macro;
    } else if ( (hash == linestrhash) && (ctx->line_macro) && (SDL_strcmp(sym, "__LINE__") == 0) ) {
        const IncludeState *state = ctx->include_stack;
        const size_t bufsize = 32;
        char *str;

        Free(ctx, (char *) ctx->line_macro->definition);
        str = (char *) Malloc(ctx, bufsize);
        if (!str) {
            return 0;
        }
        const size_t len = SDL_snprintf(str, bufsize, "%u", state->line);
        SDL_assert(len < bufsize);
        ctx->line_macro->definition = str;
        return ctx->line_macro;
    }

    return NULL;
}

static const Define *find_define_by_token(Context *ctx)
{
    IncludeState *state = ctx->include_stack;
    char *sym;
    SDL_assert(state->tokenval == TOKEN_IDENTIFIER);
    sym = (char *) alloca(state->tokenlen+1);
    SDL_memcpy(sym, state->token, state->tokenlen);
    sym[state->tokenlen] = '\0';
    return find_define(ctx, sym);
}

static const Define *find_macro_arg(const IncludeState *state,
                                    const Define *defines)
{
    const Define *def = NULL;
    char *sym = (char *) alloca(state->tokenlen + 1);
    SDL_memcpy(sym, state->token, state->tokenlen);
    sym[state->tokenlen] = '\0';

    for (def = defines; def != NULL; def = def->next) {
        SDL_assert(def->parameters == NULL);  /* args can't have args! */
        SDL_assert(def->paramcount == 0);  /* args can't have args! */
        if (SDL_strcmp(def->identifier, sym) == 0) {
            break;
        }
    }

    return def;
}

static void put_all_defines(Context *ctx)
{
    size_t i;
    for (i = 0; i < SDL_arraysize(ctx->define_hashtable); i++) {
        Define *bucket = ctx->define_hashtable[i];
        ctx->define_hashtable[i] = NULL;
        while (bucket) {
            Define *next = bucket->next;
            free_define(ctx, bucket);
            bucket = next;
        }
    }
}

static SDL_bool push_source(Context *ctx, const char *fname, const char *source, size_t srclen, Sint32 linenum, SDL_SHADER_IncludeClose close_callback)
{
    IncludeState *state = get_include(ctx);
    if (state == NULL) {
        return SDL_FALSE;
    }

    /*printf("PUSH SOURCE [fname='%s' linenum=%u, source='%s']\n", fname, (uint) linenum, source);*/

    if (fname != NULL) {
        state->filename = stringcache(ctx->filename_cache, fname);
        if (state->filename == NULL) {
            put_include(ctx, state);
            return SDL_FALSE;
        }
    }

    state->close_callback = close_callback;
    state->source_base = source;
    state->source = source;
    state->token = source;
    state->tokenval = ((Token) '\n');
    state->orig_length = srclen;
    state->bytes_left = srclen;
    state->line = linenum;
    state->next = ctx->include_stack;
    state->asm_comments = ctx->asm_comments;

    if (ctx->include_stack) {
        state->current_define = ctx->include_stack->current_define;
    }

    ctx->include_stack = state;

    ctx->filename = state->filename;
    ctx->position = linenum;

    print_debug_lexing_position(ctx);

    return SDL_TRUE;
}

static SDL_bool push_source_define(Context *ctx, const char *fname, const Define *def, Uint32 linenum)
{
    const SDL_bool retval = push_source(ctx, fname, def->definition, SDL_strlen(def->definition), linenum, NULL);
    if (retval) {
        ctx->include_stack->current_define = def;
    }
    return retval;
}

static void pop_source(Context *ctx)
{
    IncludeState *state = ctx->include_stack;
    Conditional *cond;

    /*printf("POP SOURCE\n");*/

    SDL_assert(state != NULL);  /* more pops than pushes! */
    if (state == NULL) {
        return;
    }

    if (state->close_callback) {
        state->close_callback(state->source_base, ctx->malloc,
                              ctx->free, ctx->malloc_data);
    }

    /* state->filename is a pointer to the filename cache; don't free it here! */

    cond = state->conditional_stack;
    while (cond) {
        Conditional *next = cond->next;
        put_conditional(ctx, cond);
        cond = next;
    }

    ctx->include_stack = state->next;
    ctx->filename = ctx->include_stack ? ctx->include_stack->filename : NULL;
    ctx->position = ctx->include_stack ? ctx->include_stack->line : 0;

    print_debug_lexing_position(ctx);

    put_include(ctx, state);
}


static void close_define_include(const char *data, SDL_SHADER_Malloc m,
                                 SDL_SHADER_Free f, void *d)
{
    f((void *) data, d);
}


SDL_bool preprocessor_start(Context *ctx, const SDL_SHADER_CompilerParams *params, SDL_bool asm_comments)
{
    char *define_include = NULL;
    size_t define_include_len = 0;
    int okay = 1;
    size_t i = 0;

    if ((params->include_open == NULL) != (params->include_close == NULL)) {
        return SDL_FALSE;
    }

    /* we don't own the *_include_paths arrays, don't free them. */
    ctx->uses_preprocessor = SDL_TRUE;
    ctx->allow_dotdot_includes = params->allow_dotdot_includes;
    ctx->allow_absolute_includes = params->allow_absolute_includes;
    ctx->system_include_paths = params->system_include_paths;
    ctx->system_include_path_count = params->system_include_path_count;
    ctx->local_include_paths = params->local_include_paths;
    ctx->local_include_path_count = params->local_include_path_count;
    ctx->open_callback = params->include_open ? params->include_open : internal_include_open;
    ctx->close_callback = params->include_close ? params->include_close : internal_include_close;
    ctx->asm_comments = asm_comments;

    ctx->filename_cache = stringcache_create(MallocContextBridge, FreeContextBridge, ctx);
    okay = ((okay) && (ctx->filename_cache != NULL));

    ctx->file_macro = get_define(ctx);
    okay = ((okay) && (ctx->file_macro != NULL));
    if ((okay) && (ctx->file_macro)) {
        okay = ((ctx->file_macro->identifier = StrDup(ctx, "__FILE__")) != 0);
    }

    ctx->line_macro = get_define(ctx);
    okay = ((okay) && (ctx->line_macro != NULL));
    if ((okay) && (ctx->line_macro)) {
        okay = ((ctx->line_macro->identifier = StrDup(ctx, "__LINE__")) != 0);
    }

    /* let the usual preprocessor parser sort these out. */
    if ((okay) && (params->define_count > 0)) {
        Buffer *predefbuf = buffer_create(256, MallocContextBridge, FreeContextBridge, ctx);
        okay = okay && (predefbuf != NULL);
        for (i = 0; okay && (i < params->define_count); i++) {
            okay = okay && buffer_append_fmt(predefbuf, "#define %s %s\n", params->defines[i].identifier, params->defines[i].definition);
        }

        define_include_len = buffer_size(predefbuf);
        if (define_include_len > 0) {
            define_include = buffer_flatten(predefbuf);
            okay = okay && (define_include != NULL);
        }
        buffer_destroy(predefbuf);
    }

    if ((okay) && (!push_source(ctx, params->filename, params->source, params->sourcelen, 1, NULL))) {
        okay = 0;
    }

    if ((okay) && (define_include_len > 0)) {
        SDL_assert(define_include != NULL);
        okay = push_source(ctx, "<predefined macros>", define_include, define_include_len, SDL_SHADER_POSITION_BEFORE, close_define_include);
    }

    if (!okay) {
        return SDL_FALSE;
    }

    return SDL_TRUE;
}

void preprocessor_end(Context *ctx)
{
    if (!ctx || !ctx->uses_preprocessor) {
        return;
    }

    while (ctx->include_stack != NULL) {
        pop_source(ctx);
    }

    put_all_defines(ctx);

    if (ctx->filename_cache != NULL) {
        stringcache_destroy(ctx->filename_cache);
    }

    free_define(ctx, ctx->file_macro);
    free_define(ctx, ctx->line_macro);
    free_define_pool(ctx);
    free_conditional_pool(ctx);
    free_include_pool(ctx);

    ctx->uses_preprocessor = SDL_FALSE;
}


static inline void pushback(IncludeState *state)
{
    #if DEBUG_PREPROCESSOR
    printf("PREPROCESSOR PUSHBACK\n");
    #endif
    SDL_assert(!state->pushedback);
    state->pushedback = SDL_TRUE;
}


static Token lexer(IncludeState *state)
{
    if (state->pushedback) {
        state->pushedback = SDL_FALSE;
        return state->tokenval;
    }
    return preprocessor_lexer(state);
}


/* !!! FIXME: parsing fails on preprocessor directives should skip rest of line. */
static int require_newline(IncludeState *state)
{
    Token token;
    while (SDL_TRUE) {
        token = lexer(state);
        if (token == ((Token) ' ')) {
            continue;  /* whitespace, keep looking */
        } else if (token == TOKEN_MULTI_COMMENT) {
            if (MemChr(state->token, '\n', state->tokenlen) != NULL) {  /* there was a newline in the comment, it counts. */
                pushback(state);
                return 1;
            }
            continue;
        } else if (token == TOKEN_SINGLE_COMMENT) {  /* should drop a newline next call. */
            continue;
        } else {
            break;
        }
    }

    pushback(state);  /* rewind no matter what. */
    return ( (token == TOKEN_INCOMPLETE_STRING_LITERAL) || /* call it an eol. */
             (token == TOKEN_INCOMPLETE_COMMENT) || /* call it an eol. */
             (token == ((Token) '\n')) || (token == TOKEN_EOI) );
}

/* !!! FIXME: didn't we implement this by hand elsewhere? */
static int token_to_int(IncludeState *state)
{
    char *buf;
    SDL_assert(state->tokenval == TOKEN_INT_LITERAL);
    buf = (char *) alloca(state->tokenlen+1);
    SDL_memcpy(buf, state->token, state->tokenlen);
    buf[state->tokenlen] = '\0';
    return SDL_atoi(buf);
}


static void handle_pp_include(Context *ctx)
{
    char failstr[128];
    IncludeState *state = ctx->include_stack;
    Token token = lexer(state);
    SDL_SHADER_IncludeType incltype;
    const char *newdata = NULL;
    size_t newbytes = 0;
    char *filename = NULL;
    const char *updated_filename = NULL;
    int bogus = 0;
    const char **include_paths = NULL;
    size_t include_path_count = 0;
    char *ptr = NULL;

    if (token == TOKEN_STRING_LITERAL) {
        incltype = SDL_SHADER_INCLUDETYPE_LOCAL;
        include_paths = ctx->local_include_paths;
        include_path_count = ctx->local_include_path_count;
    } else if (token == ((Token) '<')) {
        incltype = SDL_SHADER_INCLUDETYPE_SYSTEM;
        include_paths = ctx->system_include_paths;
        include_path_count = ctx->system_include_path_count;
        /* can't use lexer, since every byte between the < > pair is considered part of the filename.  :/  */
        while (!bogus) {
            if ( !(bogus = (state->bytes_left == 0)) ) {
                const char ch = *state->source;
                if ( !(bogus = ((ch == '\r') || (ch == '\n'))) ) {
                    state->source++;
                    state->bytes_left--;

                    if (ch == '>') {
                        break;
                    }
                }
            }
        }
    } else {
        bogus = 1;
    }

    if (!bogus) {
        size_t len;
        state->token++;  /* skip '<' or '\"'... */
        len = (size_t) (state->source - state->token);
        filename = (char *) alloca(len);  /* !!! FIXME: maybe don't alloca this. */
        SDL_memcpy(filename, state->token, len-1);
        filename[len-1] = '\0';
        bogus = !require_newline(state);
    }

    if (bogus) {
        fail(ctx, "Invalid #include directive");
        return;
    }

    ptr = filename;
    while (SDL_TRUE) {
        const char ch = *(ptr++);
        if (ch == '\0') {
            break;
        } else if (ch == '\\') {
            fail(ctx, "'\\' characters in #include directives are forbidden (use '/' instead)");  /* sorry, Windows. */
            return;
        }
    }

    if (!ctx->allow_absolute_includes) {
        if (*filename == '/') {
            fail(ctx, "Absolute paths in #include directives are forbidden");
            return;
        }
    }

    if (!ctx->allow_dotdot_includes) {
        ptr = filename;
        while (SDL_TRUE) {
            const char ch = *(ptr++);
            if (ch == '\0') {
                break;
            } else if ((ch == '/') && ((ptr[0] == '.') && (ptr[1] == '.') && ((ptr[2] == '/') || (ptr[2] == '\0')))) {
                fail(ctx, "'..' paths in #include directives are forbidden");
                return;
            }
        }
    }

    /* We _should_ have provided internal implementations in this case. */
    SDL_assert(ctx->open_callback != NULL);
    SDL_assert(ctx->close_callback != NULL);

    failstr[0] = '\0';
    updated_filename = ctx->open_callback(incltype, filename, state->filename, state->source_base,
                                          &newdata, &newbytes, include_paths, include_path_count,
                                          failstr, sizeof (failstr), ctx->malloc, ctx->free, ctx->malloc_data);

    if (!updated_filename) {
        fail(ctx, failstr[0] ? failstr : "Include callback failed");
        return;
    }

    if (!push_source(ctx, updated_filename, newdata, newbytes, 1, ctx->close_callback)) {
        SDL_assert(ctx->out_of_memory);
        ctx->close_callback(newdata, ctx->malloc, ctx->free, ctx->malloc_data);
    }

    if (updated_filename != filename) {
        ctx->free((void *) updated_filename, ctx->malloc_data);
    }
}


static void handle_pp_line(Context *ctx)
{
    IncludeState *state = ctx->include_stack;
    const char *filename = NULL;
    int linenum = 0;
    int bogus = 0;

    if (lexer(state) != TOKEN_INT_LITERAL) {
        bogus = 1;
    } else {
        linenum = token_to_int(state);
    }

    if (!bogus) {
        Token t = lexer(state);
        if (t == ((Token) '\n')) {
            state->line = linenum;
            return;
        }
        bogus = (t != TOKEN_STRING_LITERAL);
    }

    if (!bogus) {
        state->token++;  /* skip '\"'... */
        filename = stringcache_len(ctx->filename_cache, state->token, state->tokenlen-1);  /* may be NULL if stringcache() failed. */
        if (!filename) {
            return;
        }
        bogus = !require_newline(state);
    }

    if (bogus) {
        fail(ctx, "Invalid #line directive");
    } else {
        ctx->filename = state->filename = filename;  /* already stringcache'd. */
        ctx->position = state->line = linenum;
    }
}


static void handle_pp_error(Context *ctx)
{
    IncludeState *state = ctx->include_stack;
    const char *prefix = "#error";
    const size_t prefixlen = SDL_strlen(prefix);
    char failstr[256];
    size_t avail = sizeof (failstr);
    size_t cpy = 0;
    SDL_bool done = SDL_FALSE;
    char *ptr;

    SDL_strlcpy(failstr, prefix, avail);
    avail -= prefixlen + 1;  /* +1 for the null terminator */
    ptr = failstr + prefixlen;

    state->report_whitespace = SDL_TRUE;
    while (!done) {
        const Token token = lexer(state);
        switch (token) {
            case ((Token) '\n'):
                state->line--;  /* make sure error is on the right line. */
                /* fall through! */
            case TOKEN_INCOMPLETE_STRING_LITERAL:
            case TOKEN_INCOMPLETE_COMMENT:
            case TOKEN_EOI:
                pushback(state);  /* move back so we catch this later. */
                done = SDL_TRUE;
                break;

            case ((Token) ' '):
                if (!avail) {
                    break;
                }
                *(ptr++) = ' ';
                avail--;
                break;

            default:
                cpy = SDL_min(avail, state->tokenlen);
                if (cpy) {
                    SDL_memcpy(ptr, state->token, cpy);
                }
                ptr += cpy;
                avail -= cpy;
                break;
        }
    }

    *ptr = '\0';
    state->report_whitespace = SDL_FALSE;

    fail(ctx, failstr);
}


static void handle_pp_bad(Context *ctx)
{
    IncludeState *state = ctx->include_stack;

    failf(ctx, "unknown directive \"%.*s\"", state->tokenlen, state->token);
}


static void handle_pp_define(Context *ctx)
{
    static const char space = ' ';
    Buffer *buffer = NULL;
    IncludeState *state = ctx->include_stack;
    SDL_bool done = SDL_FALSE;
    SDL_bool hashhash_error = SDL_FALSE;
    size_t buflen;
    int params = 0;
    char **idents = NULL;
    char *definition = NULL;
    char *sym;

    if (lexer(state) != TOKEN_IDENTIFIER) {
        fail(ctx, "Macro names must be identifiers");
        return;
    }

    sym = (char *) Malloc(ctx, state->tokenlen+1);
    if (sym == NULL) {
        return;
    }
    SDL_memcpy(sym, state->token, state->tokenlen);
    sym[state->tokenlen] = '\0';

    if (SDL_strcmp(sym, "defined") == 0) {
        Free(ctx, sym);
        fail(ctx, "'defined' cannot be used as a macro name");
        return;
    }

    /* Don't treat these symbols as special anymore if they get (re)#defined. */
    if (SDL_strcmp(sym, "__FILE__") == 0) {
        if (ctx->file_macro) {
            warnf(ctx, "'%s' already defined", sym);
            free_define(ctx, ctx->file_macro);
            ctx->file_macro = NULL;
        }
    } else if (SDL_strcmp(sym, "__LINE__") == 0) {
        if (ctx->line_macro) {
            warnf(ctx, "'%s' already defined", sym);
            free_define(ctx, ctx->line_macro);
            ctx->line_macro = NULL;
        }
    }

    /* #define a(b) is different than #define a (b)    :(  */
    state->report_whitespace = SDL_TRUE;
    lexer(state);
    state->report_whitespace = SDL_FALSE;

    if (state->tokenval == ((Token) ' ')) {
        lexer(state);  /* skip it. */
    } else if (state->tokenval == ((Token) '(')) {
        IncludeState saved;
        SDL_bool invalid = SDL_FALSE;

        SDL_memcpy(&saved, state, sizeof (IncludeState));

        if (lexer(state) != ((Token) ')')) {
            pushback(state);
            while (SDL_TRUE) {
                lexer(state);
                if (state->tokenval != TOKEN_IDENTIFIER) {
                    invalid = SDL_TRUE;
                    break;
                }

                params++;

                lexer(state);
                if (state->tokenval == ((Token) ')')) {
                    break;  /* we're done! */
                }

                if (state->tokenval != ((Token) ',')) {
                    invalid = SDL_TRUE;
                    break;
                }
            }
        }

        if (invalid) {
            fail(ctx, "syntax error in macro parameter list");
            goto handle_pp_define_failed;
        }

        if (params == 0) {  /* special case for void args: "#define a() b" */
            params = -1;
        } else {
            idents = (char **) Malloc(ctx, sizeof (char *) * params);
            if (idents == NULL) {
                goto handle_pp_define_failed;
            }

            /* roll all the way back, do it again. */
            SDL_memcpy(state, &saved, sizeof (IncludeState));
            SDL_memset(idents, '\0', sizeof (char *) * params);

            int i;
            for (i = 0; i < params; i++) {
                char *dst;

                lexer(state);
                SDL_assert(state->tokenval == TOKEN_IDENTIFIER);

                dst = (char *) Malloc(ctx, state->tokenlen+1);
                if (dst == NULL) {
                    break;
                }

                SDL_memcpy(dst, state->token, state->tokenlen);
                dst[state->tokenlen] = '\0';
                idents[i] = dst;

                if (i < (params-1)) {
                    lexer(state);
                    SDL_assert(state->tokenval == ((Token) ','));
                }
            }

            if (i != params) {
                SDL_assert(ctx->out_of_memory);
                goto handle_pp_define_failed;
            }

            lexer(state);
            SDL_assert(state->tokenval == ((Token) ')'));
        }

        lexer(state);
    }

    pushback(state);

    buffer = buffer_create(128, MallocContextBridge, FreeContextBridge, ctx);

    state->report_whitespace = SDL_TRUE;
    while ((!done) && (!ctx->out_of_memory)) {
        const Token token = lexer(state);
        switch (token) {
            case TOKEN_INCOMPLETE_STRING_LITERAL:
            case TOKEN_INCOMPLETE_COMMENT:
            case TOKEN_EOI:
                pushback(state);  /* move back so we catch this later. */
                done = SDL_TRUE;
                break;

            case ((Token) '\n'):
                done = SDL_TRUE;
                break;

            case ((Token) ' '):  /* may not actually point to ' '. */
                SDL_assert(buffer_size(buffer) > 0);
                buffer_append(buffer, &space, 1);
                break;

            default:
                buffer_append(buffer, state->token, state->tokenlen);
                break;
        }
    }
    state->report_whitespace = SDL_FALSE;

    buflen = buffer_size(buffer) + 1;
    if (!ctx->out_of_memory) {
        definition = buffer_flatten(buffer);
    }

    buffer_destroy(buffer);

    if (ctx->out_of_memory) {
        goto handle_pp_define_failed;
    }

    if ((buflen > 2) && (definition[0] == '#') && (definition[1] == '#')) {
        hashhash_error = SDL_TRUE;
        buflen -= 2;
        SDL_memmove(definition, definition + 2, buflen);
    }

    if (buflen > 2) {
        char *ptr = (definition + buflen) - 2;
        if (*ptr == ' ') {
            ptr--;
            buflen--;
        }

        if ((buflen > 2) && (ptr[0] == '#') && (ptr[-1] == '#')) {
            hashhash_error = 1;
            buflen -= 2;
            ptr[-1] = '\0';
        }
    }

    if (hashhash_error) {
        fail(ctx, "'##' cannot appear at either end of a macro expansion");
    }

    SDL_assert(done);

    if (!add_define(ctx, sym, definition, idents, params)) {
        goto handle_pp_define_failed;
    }

    return;

handle_pp_define_failed:
    Free(ctx, sym);
    Free(ctx, definition);
    if (idents != NULL) {
        while (params--) {
            Free(ctx, idents[params]);
        }
    }
    Free(ctx, idents);
}

static void handle_pp_undef(Context *ctx)
{
    IncludeState *state = ctx->include_stack;
    char *sym;

    if (lexer(state) != TOKEN_IDENTIFIER) {
        fail(ctx, "Macro names must be indentifiers");
        return;
    }

    sym = (char *) alloca(state->tokenlen+1);
    SDL_memcpy(sym, state->token, state->tokenlen);
    sym[state->tokenlen] = '\0';

    if (!require_newline(state)) {
        fail(ctx, "Invalid #undef directive");
        return;
    }

    if (SDL_strcmp(sym, "__FILE__") == 0) {
        if (ctx->file_macro) {
            warnf(ctx, "undefining \"%s\"", sym);
            free_define(ctx, ctx->file_macro);
            ctx->file_macro = NULL;
        }
    } else if (SDL_strcmp(sym, "__LINE__") == 0) {
        if (ctx->line_macro) {
            warnf(ctx, "undefining \"%s\"", sym);
            free_define(ctx, ctx->line_macro);
            ctx->line_macro = NULL;
        }
    }

    remove_define(ctx, sym);
}

static Conditional *_handle_pp_ifdef(Context *ctx, const Token type)
{
    IncludeState *state = ctx->include_stack;
    int found, chosen, skipping;
    Conditional *conditional;
    Conditional *parent;
    char *sym;

    SDL_assert((type == TOKEN_PP_IFDEF) || (type == TOKEN_PP_IFNDEF));

    if (lexer(state) != TOKEN_IDENTIFIER) {
        fail(ctx, "Macro names must be indentifiers");
        return NULL;
    }

    sym = (char *) alloca(state->tokenlen+1);
    SDL_memcpy(sym, state->token, state->tokenlen);
    sym[state->tokenlen] = '\0';

    if (!require_newline(state)) {
        if (type == TOKEN_PP_IFDEF) {
            fail(ctx, "Invalid #ifdef directive");
        } else {
            fail(ctx, "Invalid #ifndef directive");
        }
        return NULL;
    }

    conditional = get_conditional(ctx);
    SDL_assert((conditional != NULL) || (ctx->out_of_memory));
    if (conditional == NULL) {
        return NULL;
    }

    parent = state->conditional_stack;
    found = (find_define(ctx, sym) != NULL);
    chosen = (type == TOKEN_PP_IFDEF) ? found : !found;
    skipping = ( (((parent) && (parent->skipping))) || (!chosen) );

    conditional->type = type;
    conditional->linenum = state->line - 1;
    conditional->skipping = skipping ? SDL_TRUE : SDL_FALSE;
    conditional->chosen = chosen ? SDL_TRUE : SDL_FALSE;
    conditional->next = parent;
    state->conditional_stack = conditional;
    return conditional;
}

static inline void handle_pp_ifdef(Context *ctx)
{
    _handle_pp_ifdef(ctx, TOKEN_PP_IFDEF);
}

static inline void handle_pp_ifndef(Context *ctx)
{
    _handle_pp_ifdef(ctx, TOKEN_PP_IFNDEF);
}

static SDL_bool replace_and_push_macro(Context *ctx, const Define *def, const Define *params)
{
    char *final = NULL;
    IncludeState *state;
    Buffer *buffer;

    /* We push the #define and lex it, building a buffer with argument replacement, stringification, and concatenation. */
    buffer = buffer_create(128, MallocContextBridge, FreeContextBridge, ctx);
    if (buffer == NULL) {
        return SDL_FALSE;
    }

    state = ctx->include_stack;
    if (!push_source_define(ctx, state->filename, def, state->line)) {
        buffer_destroy(buffer);
        return SDL_FALSE;
    }

    state = ctx->include_stack;
    while (lexer(state) != TOKEN_EOI) {
        SDL_bool wantorig = SDL_FALSE;
        const Define *arg = NULL;
        const char *data;
        size_t len;

        /* put a space between tokens if we're not concatenating. */
        if (state->tokenval == TOKEN_HASHHASH) { /* concatenate? */
            wantorig = SDL_TRUE;
            lexer(state);
            SDL_assert(state->tokenval != TOKEN_EOI);
        } else {
            if (buffer_size(buffer) > 0) {
                if (!buffer_append(buffer, " ", 1)) {
                    goto replace_and_push_macro_failed;
                }
            }
        }

        data = state->token;
        len = state->tokenlen;

        if (state->tokenval == TOKEN_HASH) { /* stringify? */
            SDL_bool valid = SDL_TRUE;

            lexer(state);
            if (state->tokenval == TOKEN_EOI) {
                valid = SDL_FALSE;
            } else if (state->tokenval == TOKEN_IDENTIFIER) {
                arg = find_macro_arg(state, params);
                if (arg == NULL) {
                    valid = SDL_FALSE;
                } else {
                    data = arg->original;
                    len = SDL_strlen(data);
                }
            }

            if (!valid) {
                fail(ctx, "'#' without a valid macro parameter");
            } else {
                if ( !buffer_append(buffer, "\"", 1) ||
                     !buffer_append(buffer, data, len) ||
                     !buffer_append(buffer, "\"", 1) ) {
                    goto replace_and_push_macro_failed;
                }
                continue;
            }
        }

        if (state->tokenval == TOKEN_IDENTIFIER) {
            arg = find_macro_arg(state, params);
            if (arg != NULL) {
                if (!wantorig) {
                    wantorig = (lexer(state) == TOKEN_HASHHASH) ? SDL_TRUE : SDL_FALSE;
                    pushback(state);
                }
                data = wantorig ? arg->original : arg->definition;
                len = SDL_strlen(data);
            }
        }

        if (!buffer_append(buffer, data, len)) {
            goto replace_and_push_macro_failed;
        }
    }

    final = buffer_flatten(buffer);
    if (!final) {
        goto replace_and_push_macro_failed;
    }

    buffer_destroy(buffer);
    pop_source(ctx);  /* ditch the macro. */
    state = ctx->include_stack;
    if (!push_source(ctx, state->filename, final, SDL_strlen(final), state->line, close_define_include)) {
        Free(ctx, final);
        return SDL_FALSE;
    }

    ctx->include_stack->current_define = def;

    return SDL_TRUE;

replace_and_push_macro_failed:
    pop_source(ctx);
    buffer_destroy(buffer);
    return SDL_FALSE;
}


static SDL_bool handle_macro_args(Context *ctx, const char *sym, const Define *def)
{
    SDL_bool retval = SDL_FALSE;
    IncludeState *state = ctx->include_stack;
    Define *params = NULL;
    const int expected = (def->paramcount < 0) ? 0 : def->paramcount;
    int saw_params = 0;
    IncludeState saved;  /* can't pushback, we need the original token. */
    SDL_bool void_call = SDL_FALSE;
    int paren = 1;

    SDL_memcpy(&saved, state, sizeof (IncludeState));
    if (lexer(state) != ((Token) '(')) {
        SDL_memcpy(state, &saved, sizeof (IncludeState));
        goto handle_macro_args_failed;  /* gcc abandons replacement, too. */
    }

    state->report_whitespace = SDL_TRUE;

    while (paren > 0) {
        Buffer *buffer = buffer_create(128, MallocContextBridge, FreeContextBridge, ctx);
        Buffer *origbuffer = buffer_create(128, MallocContextBridge, FreeContextBridge, ctx);

        Token t = lexer(state);

        SDL_assert(!void_call);

        while (SDL_TRUE) {
            const char *origexpr = state->token;
            size_t origexprlen = state->tokenlen;
            const char *expr = state->token;
            size_t exprlen = state->tokenlen;

            if (t == ((Token) '(')) {
                paren++;
            } else if (t == ((Token) ')')) {
                paren--;
                if (paren < 1) { /* end of macro? */
                    break;
                }
            } else if (t == ((Token) ',')) {
                if (paren == 1) {  /* new macro arg? */
                    break;
                }
            } else if (t == ((Token) ' ')) {
                /* don't add whitespace to the start, so we recognize void calls correctly. */
                origexpr = expr = " ";
                origexprlen = (buffer_size(origbuffer) == 0) ? 0 : 1;
                exprlen = (buffer_size(buffer) == 0) ? 0 : 1;
            } else if (t == TOKEN_IDENTIFIER) {
                const Define *def = find_define_by_token(ctx);
                /* don't replace macros with arguments so they replace correctly, later. */
                if ((def) && (def->paramcount == 0)) {
                    expr = def->definition;
                    exprlen = SDL_strlen(def->definition);
                }
            } else if ((t == TOKEN_INCOMPLETE_STRING_LITERAL) || (t == TOKEN_INCOMPLETE_COMMENT) || (t == TOKEN_EOI)) {
                pushback(state);
                fail(ctx, "Unterminated macro list");
                buffer_destroy(origbuffer);
                buffer_destroy(buffer);
                goto handle_macro_args_failed;
            }

            SDL_assert(expr != NULL);

            if (!buffer_append(buffer, expr, exprlen)) {
                buffer_destroy(origbuffer);
                buffer_destroy(buffer);
                goto handle_macro_args_failed;
            }

            if (!buffer_append(origbuffer, origexpr, origexprlen)) {
                buffer_destroy(origbuffer);
                buffer_destroy(buffer);
                goto handle_macro_args_failed;
            }

            t = lexer(state);
        }

        if (buffer_size(buffer) == 0) {
            void_call = ((saw_params == 0) && (paren == 0)) ? SDL_TRUE : SDL_FALSE;
        }

        if (saw_params < expected) {
            const int origdeflen = (int) buffer_size(origbuffer);
            char *origdefinition = buffer_flatten(origbuffer);
            const int deflen = (int) buffer_size(buffer);
            char *definition = buffer_flatten(buffer);
            Define *p = get_define(ctx);
            int i;

            if ((!origdefinition) || (!definition) || (!p)) {
                Free(ctx, origdefinition);
                Free(ctx, definition);
                buffer_destroy(origbuffer);
                buffer_destroy(buffer);
                free_define(ctx, p);
                goto handle_macro_args_failed;
            }

            /* trim any whitespace from the end of the string... */
            for (i = deflen - 1; i >= 0; i--) {
                if (definition[i] == ' ') {
                    definition[i] = '\0';
                } else {
                    break;
                }
            }

            for (i = origdeflen - 1; i >= 0; i--) {
                if (origdefinition[i] == ' ') {
                    origdefinition[i] = '\0';
                } else {
                    break;
                }
            }

            p->identifier = def->parameters[saw_params];
            p->definition = definition;
            p->original = origdefinition;
            p->next = params;
            params = p;
        }

        buffer_destroy(buffer);
        buffer_destroy(origbuffer);
        saw_params++;
    }

    SDL_assert(paren == 0);

    /* "a()" should match "#define a()" ... */
    if ((expected == 0) && (saw_params == 1) && (void_call)) {
        SDL_assert(params == NULL);
        saw_params = 0;
    }

    if (saw_params != expected) {
        failf(ctx, "macro '%s' passed %d arguments, but requires %d",
              sym, saw_params, expected);
        goto handle_macro_args_failed;
    }

    /* this handles arg replacement and the '##' and '#' operators. */
    retval = replace_and_push_macro(ctx, def, params);

handle_macro_args_failed:
    while (params) {
        Define *next = params->next;
        params->identifier = NULL;
        free_define(ctx, params);
        params = next;
    }

    state->report_whitespace = SDL_FALSE;
    return retval;
}

static SDL_bool currently_preprocessing_macro(Context *ctx, const Define *def)
{
    IncludeState *i;
    for (i = ctx->include_stack; i != NULL; i = i->next) {
        if (i->current_define == def) {
            return SDL_TRUE;
        }
    }
    return SDL_FALSE;
}

static SDL_bool handle_pp_identifier(Context *ctx)
{
    IncludeState *state = ctx->include_stack;
    const char *fname = state->filename;
    const Uint32 line = state->line;
    char *sym = (char *) alloca(state->tokenlen+1);
    const Define *def;

    SDL_memcpy(sym, state->token, state->tokenlen);
    sym[state->tokenlen] = '\0';

    /* Is this identifier #defined? */
    def = find_define(ctx, sym);

    if ((def == NULL) || currently_preprocessing_macro(ctx, def)) {
        return SDL_FALSE;   /* just send the token through unchanged. */
    } else if (def->paramcount != 0) {
        return handle_macro_args(ctx, sym, def);
    }

    return push_source_define(ctx, fname, def, line);
}


static int find_precedence(const Token token)
{
    /* operator precedence, left and right associative... */
    typedef struct { int precedence; Token token; } Precedence;
    static const Precedence ops[] = {
        { 0, TOKEN_OROR }, { 1, TOKEN_ANDAND }, { 2, ((Token) '|') },
        { 3, ((Token) '^') }, { 4, ((Token) '&') }, { 5, TOKEN_NEQ },
        { 6, TOKEN_EQL }, { 7, ((Token) '<') }, { 7, ((Token) '>') },
        { 7, TOKEN_LEQ }, { 7, TOKEN_GEQ }, { 8, TOKEN_LSHIFT },
        { 8, TOKEN_RSHIFT }, { 9, ((Token) '-') }, { 9, ((Token) '+') },
        { 10, ((Token) '%') }, { 10, ((Token) '/') }, { 10, ((Token) '*') },
        { 11, TOKEN_PP_UNARY_PLUS }, { 11, TOKEN_PP_UNARY_MINUS },
        { 11, ((Token) '!') }, { 11, ((Token) '~') },
    };

    size_t i;
    for (i = 0; i < SDL_arraysize(ops); i++) {
        if (ops[i].token == token)
            return ops[i].precedence;
    }

    return -1;
}

#define DECLARE_RPN_ARRAY(name, typ, initial_stack_count) \
    typ name##_stacked[initial_stack_count]; \
    typ *name##_malloced = NULL; \
    typ *name = name##_stacked; \
    size_t name##_allocsize = initial_stack_count; \
    size_t name##_size = 0;

#define VERIFY_RPN_ARRAY_ALLOCATION(name, typ, increase, run_if_oom) \
    if (name##_size >= name##_allocsize) { \
        void *ptr = Malloc(ctx, sizeof (typ) * (name##_allocsize + increase)); \
        if (!ptr) { \
            run_if_oom; \
        } else { \
            SDL_memcpy(ptr, name##_malloced ? name##_malloced : name##_stacked, sizeof (typ) * name##_size); \
            Free(ctx, name##_malloced); \
            name = name##_malloced = (typ *) ptr; \
            name##_allocsize += increase; \
        } \
    }

#define FREE_RPN_ARRAY(name) \
    Free(ctx, name##_malloced);

typedef struct RpnTokens
{
    int isoperator;
    int value;
} RpnTokens;

static long interpret_rpn(Context *ctx, const RpnTokens *tokens, int tokencount, SDL_bool *_error)
{
    DECLARE_RPN_ARRAY(stack, long, 16);
    DECLARE_RPN_ARRAY(error_stack, const char *, 16);
    long retval = 0;
    *_error = SDL_TRUE;  /* by default */

    #define NEED_X_TOKENS(x) do { if (stack_size < x) goto interpret_rpn_failed; } while (SDL_FALSE)

    #define BINARY_OPERATION(op) do {                                         \
        NEED_X_TOKENS(2);                                                     \
        if (error_stack[stack_size-2]) {                                      \
            /* FIXME: error_stack[stack_size-1] gets dropped */               \
        } else if (error_stack[stack_size-1]) {                               \
            error_stack[stack_size-2] = error_stack[stack_size-1];            \
        } else {                                                              \
            stack[stack_size-2] = stack[stack_size-2] op stack[stack_size-1]; \
        }                                                                     \
        stack_size--;                                                         \
    } while (SDL_FALSE)

    #define UNARY_OPERATION(op) do {                      \
        NEED_X_TOKENS(1);                                 \
        if (!error_stack[stack_size-1]) {                 \
            stack[stack_size-1] = op stack[stack_size-1]; \
        }                                                 \
    } while (SDL_FALSE)

    while (tokencount-- > 0) {
        if (!tokens->isoperator) {
            VERIFY_RPN_ARRAY_ALLOCATION(stack, long, 128, { goto interpret_rpn_failed; });
            error_stack[stack_size] = NULL;
            stack[stack_size++] = (long) tokens->value;
            tokens++;
            continue;
        }

        /* operators. */
        switch (tokens->value) {
            case '!': UNARY_OPERATION(!); break;
            case '~': UNARY_OPERATION(~); break;
            case TOKEN_PP_UNARY_MINUS: UNARY_OPERATION(-); break;
            case TOKEN_PP_UNARY_PLUS: UNARY_OPERATION(+); break;
            case '|': BINARY_OPERATION(|); break;
            case '^': BINARY_OPERATION(^); break;
            case '&': BINARY_OPERATION(&); break;
            case TOKEN_NEQ: BINARY_OPERATION(!=); break;
            case TOKEN_EQL: BINARY_OPERATION(==); break;
            case '<': BINARY_OPERATION(<); break;
            case '>': BINARY_OPERATION(>); break;
            case TOKEN_LEQ: BINARY_OPERATION(<=); break;
            case TOKEN_GEQ: BINARY_OPERATION(>=); break;
            case TOKEN_LSHIFT: BINARY_OPERATION(<<); break;
            case TOKEN_RSHIFT: BINARY_OPERATION(>>); break;
            case '-': BINARY_OPERATION(-); break;
            case '+': BINARY_OPERATION(+); break;
            case '*': BINARY_OPERATION(*); break;

            case TOKEN_OROR:
                NEED_X_TOKENS(2);
                if (error_stack[stack_size-2]) {
                    stack_size--;
                    break;
                }
                if (stack[stack_size-2]) {
                    stack_size--;
                    break;
                }
                if (error_stack[stack_size-1]) {
                    error_stack[stack_size-2] = error_stack[stack_size-1];
                    stack_size--;
                    break;
                }
                stack[stack_size-2] = stack[stack_size-1];
                stack_size--;
                break;

            case TOKEN_ANDAND:
                NEED_X_TOKENS(2);
                if (error_stack[stack_size-2]) {
                    stack_size--;
                    break;
                }
                if (!stack[stack_size-2]) {
                    stack_size--;
                    break;
                }
                if (error_stack[stack_size-1]) {
                    error_stack[stack_size-2] = error_stack[stack_size-1];
                    stack_size--;
                    break;
                }
                stack[stack_size-2] = stack[stack_size-1];
                stack_size--;
                break;

            case '/':
                NEED_X_TOKENS(2);
                if (stack[stack_size-1] == 0) {
                    error_stack[stack_size-2] = "division by zero";
                    stack_size--;
                    break;
                }
                BINARY_OPERATION(/);
                break;

            case '%':
                NEED_X_TOKENS(2);
                if (stack[stack_size-1] == 0) {
                    error_stack[stack_size-2] = "division by zero";
                    stack_size--;
                    break;
                }
                BINARY_OPERATION(%);
                break;

            default: goto interpret_rpn_failed;
        }

        tokens++;
    }

    #undef NEED_X_TOKENS
    #undef BINARY_OPERATION
    #undef UNARY_OPERATION

    if (stack_size == 1) {
        retval = stack[0];
        if (error_stack[0]) {
            fail(ctx, error_stack[0]);
        }
        *_error = SDL_FALSE;
    }

interpret_rpn_failed:

    FREE_RPN_ARRAY(stack);

    return retval;
}

/* https://wikipedia.org/wiki/Shunting_yard_algorithm
   Convert from infix to postfix, then use this for constant folding.
   Everything that parses should fold down to a constant value: any
   identifiers that aren't resolved as macros become zero. Anything we
   don't explicitly expect becomes a parsing error.
   returns 1 (true), 0 (false), or -1 (error) */
static int reduce_pp_expression(Context *ctx)
{
    IncludeState *orig_state = ctx->include_stack;
    DECLARE_RPN_ARRAY(output, RpnTokens, 16);
    DECLARE_RPN_ARRAY(stack, Token, 16);
    Token previous_token = TOKEN_UNKNOWN;
    SDL_bool error = SDL_FALSE;
    SDL_bool matched = SDL_FALSE;
    SDL_bool done = SDL_FALSE;
    int retval = -1;
    long val;

    #define ADD_TO_OUTPUT(op, val) \
        VERIFY_RPN_ARRAY_ALLOCATION(output, RpnTokens, 128, { goto reduce_pp_expression_failed; }) \
        output[output_size].isoperator = op; \
        output[output_size].value = val; \
        output_size++;

    #define PUSH_TO_STACK(t) \
        VERIFY_RPN_ARRAY_ALLOCATION(stack, Token, 128, { goto reduce_pp_expression_failed; }) \
        stack[stack_size] = t; \
        stack_size++;

    while (!done) {
        IncludeState *state = ctx->include_stack;
        Token token = lexer(state);
        SDL_bool isleft = SDL_TRUE;
        int precedence = -1;

        if (token == TOKEN_SINGLE_COMMENT) {
            continue;  /* dump it. */
        } else if (token == TOKEN_MULTI_COMMENT) {
            if (MemChr(state->token, '\n', state->tokenlen) != NULL) {  /* there was a newline in the comment, it counts. */
                done = SDL_TRUE;
                break;  /* we're done! */
            }
        } else if ( (token == ((Token) '!')) || (token == ((Token) '~')) ) {
            isleft = SDL_FALSE;
        } else if (token == ((Token) '-')) {
            isleft = ((previous_token == TOKEN_INT_LITERAL) || (previous_token == ((Token) ')'))) ? SDL_TRUE : SDL_FALSE;
            if (!isleft) {
                token = TOKEN_PP_UNARY_MINUS;
            }
        } else if (token == ((Token) '+')) {
            isleft = ((previous_token == TOKEN_INT_LITERAL) || (previous_token == ((Token) ')'))) ? SDL_TRUE : SDL_FALSE;
            if (!isleft) {
                token = TOKEN_PP_UNARY_PLUS;
            }
        }

        switch (token) {
            case TOKEN_EOI:
                if (state != orig_state) {  /* end of a substate, or the expr? */
                    pop_source(ctx);
                    continue;  /* substate, go again with the parent state. */
                }
                done = SDL_TRUE;  /* the expression itself is done. */
                break;

            case ((Token) '\n'):
                done = SDL_TRUE;
                break;  /* we're done! */

            case TOKEN_IDENTIFIER:
                if (handle_pp_identifier(ctx)) {
                    continue;  /* go again with new IncludeState. */
                }

                if ( (state->tokenlen == 7) && (memcmp(state->token, "defined", 7) == 0) ) {
                    int found;
                    const SDL_bool paren = ((token = lexer(state)) == ((Token) '(')) ? SDL_TRUE : SDL_FALSE;
                    if (paren) {   /* gcc doesn't let us nest parens here, either. */
                        token = lexer(state);
                    }
                    if (token != TOKEN_IDENTIFIER) {
                        fail(ctx, "operator 'defined' requires an identifier");
                        goto reduce_pp_expression_failed;
                    }

                    found = (find_define_by_token(ctx) != NULL);

                    if (paren) {
                        if (lexer(state) != ((Token) ')')) {
                            fail(ctx, "Unmatched ')'");
                            goto reduce_pp_expression_failed;
                        }
                    }

                    ADD_TO_OUTPUT(0, found);
                    continue;
                }

                /* can't replace identifier with a number? It becomes zero. */
                token = TOKEN_INT_LITERAL;
                ADD_TO_OUTPUT(0, 0);
                break;

            case TOKEN_INT_LITERAL:
                ADD_TO_OUTPUT(0, token_to_int(state));
                break;

            case ((Token) '('):
                PUSH_TO_STACK((Token) '(');
                break;

            case ((Token) ')'):
                matched = SDL_FALSE;
                while (stack_size > 0) {
                    const Token t = stack[--stack_size];
                    if (t == ((Token) '(')) {
                        matched = SDL_TRUE;
                        break;
                    }
                    ADD_TO_OUTPUT(1, t);
                }

                if (!matched) {
                    fail(ctx, "Unmatched ')'");
                    goto reduce_pp_expression_failed;
                }
                break;

            default:
                precedence = find_precedence(token);
                /* bogus token, or two operators together. */
                if (precedence < 0) {
                    pushback(state);
                    fail(ctx, "Invalid expression");
                    goto reduce_pp_expression_failed;
                } else { /* it's an operator. */
                    while (stack_size > 0) {
                        const Token t = stack[stack_size-1];
                        const int p = find_precedence(t);
                        if ( (p >= 0) &&
                             ( ((isleft) && (precedence <= p)) ||
                               ((!isleft) && (precedence < p)) ) ) {
                            stack_size--;
                            ADD_TO_OUTPUT(1, t);
                        } else {
                            break;
                        }
                    }
                    PUSH_TO_STACK(token);
                }
                break;
        }
        previous_token = token;
    }

    while (stack_size > 0) {
        const Token t = stack[--stack_size];
        if (t == ((Token) '(')) {
            fail(ctx, "Unmatched ')'");
            goto reduce_pp_expression_failed;
        }
        ADD_TO_OUTPUT(1, t);
    }

    #undef ADD_TO_OUTPUT
    #undef PUSH_TO_STACK

    /* okay, you now have some validated data in reverse polish notation. */
    #if DEBUG_PREPROCESSOR
    printf("PREPROCESSOR EXPRESSION RPN:");
    {
        int i;
        for (i = 0; i < output_size; i++) {
            if (!output[i].isoperator) {
                printf(" %d", output[i].value);
            } else {
                switch (output[i].value) {
                    case TOKEN_OROR: printf(" ||"); break;
                    case TOKEN_ANDAND: printf(" &&"); break;
                    case TOKEN_NEQ: printf(" !="); break;
                    case TOKEN_EQL: printf(" =="); break;
                    case TOKEN_LEQ: printf(" <="); break;
                    case TOKEN_GEQ: printf(" >="); break;
                    case TOKEN_LSHIFT: printf(" <<"); break;
                    case TOKEN_RSHIFT: printf(" >>"); break;
                    case TOKEN_PP_UNARY_PLUS: printf(" +"); break;
                    case TOKEN_PP_UNARY_MINUS: printf(" -"); break;
                    default: printf(" %c", output[i].value); break;
                }
            }
        }
    }
    printf("\n");
    #endif

    val = interpret_rpn(ctx, output, output_size, &error);

    #if DEBUG_PREPROCESSOR
    printf("PREPROCESSOR RPN RESULT: %ld%s\n", val, error ? " (ERROR)" : "");
    #endif

    if (error) {
        fail(ctx, "Invalid expression");
        goto reduce_pp_expression_failed;
    }

    retval = ((val) ? 1 : 0);

reduce_pp_expression_failed:
    FREE_RPN_ARRAY(output);
    FREE_RPN_ARRAY(stack);
    return retval;
}

#undef DECLARE_RPN_ARRAY
#undef VERIFY_RPN_ARRAY_ALLOCATION
#undef FREE_RPN_ARRAY


static Conditional *handle_pp_if(Context *ctx)
{
    IncludeState *state = ctx->include_stack;
    Conditional *conditional;
    Conditional *parent;
    int chosen, skipping, result;

    result = reduce_pp_expression(ctx);
    if (result == -1) {
        /* reduce_pp_expression emitted an error internally. Assume result is false to continue processing. */
        result = 0;
    }

    conditional = get_conditional(ctx);
    SDL_assert((conditional != NULL) || (ctx->out_of_memory));
    if (conditional == NULL) {
        return NULL;
    }

    parent = state->conditional_stack;
    chosen = result;
    skipping = ( (((parent) && (parent->skipping))) || (!chosen) );

    conditional->type = TOKEN_PP_IF;
    conditional->linenum = state->line - 1;
    conditional->skipping = skipping ? SDL_TRUE : SDL_FALSE;
    conditional->chosen = chosen ? SDL_TRUE : SDL_FALSE;
    conditional->next = parent;
    state->conditional_stack = conditional;
    return conditional;
}

static void handle_pp_elif(Context *ctx)
{
    int rc = reduce_pp_expression(ctx);
    IncludeState *state;
    Conditional *cond;

    if (rc == -1) {
        /* reduce_pp_expression emitted an error internally. Assume result is false to continue processing. */
        rc = 0;
    }

    state = ctx->include_stack;
    cond = state->conditional_stack;
    if (cond == NULL) {
        fail(ctx, "#elif without #if");
    } else if (cond->type == TOKEN_PP_ELSE) {
        fail(ctx, "#elif after #else");
    } else {
        const Conditional *parent = cond->next;
        cond->type = TOKEN_PP_ELIF;
        cond->skipping = ((parent && parent->skipping) || cond->chosen || !rc) ? SDL_TRUE : SDL_FALSE;
        if (!cond->chosen) {
            cond->chosen = rc ? SDL_TRUE : SDL_FALSE;
        }
    }
}

static void handle_pp_else(Context *ctx)
{
    IncludeState *state = ctx->include_stack;
    Conditional *cond = state->conditional_stack;

    if (!require_newline(state)) {
        fail(ctx, "Invalid #else directive");
    } else if (cond == NULL) {
        fail(ctx, "#else without #if");
    } else if (cond->type == TOKEN_PP_ELSE) {
        fail(ctx, "#else after #else");
    } else {
        const Conditional *parent = cond->next;
        cond->type = TOKEN_PP_ELSE;
        cond->skipping = ((parent && parent->skipping) || cond->chosen) ? SDL_TRUE : SDL_FALSE;
        if (!cond->chosen) {
            cond->chosen = SDL_TRUE;
        }
    }
}

static void handle_pp_endif(Context *ctx)
{
    IncludeState *state = ctx->include_stack;
    Conditional *cond = state->conditional_stack;

    if (!require_newline(state)) {
        fail(ctx, "Invalid #endif directive");
    } else if (cond == NULL) {
        fail(ctx, "Unmatched #endif");
    } else {
        state->conditional_stack = cond->next;  /* pop it. */
        put_conditional(ctx, cond);
    }
}

static void unterminated_pp_condition(Context *ctx)
{
    IncludeState *state = ctx->include_stack;
    Conditional *cond = state->conditional_stack;

    /* !!! FIXME: report the line number where the #if is, not the EOI. */
    switch (cond->type) {
        case TOKEN_PP_IF: fail(ctx, "Unterminated #if"); break;
        case TOKEN_PP_IFDEF: fail(ctx, "Unterminated #ifdef"); break;
        case TOKEN_PP_IFNDEF: fail(ctx, "Unterminated #ifndef"); break;
        case TOKEN_PP_ELSE: fail(ctx, "Unterminated #else"); break;
        case TOKEN_PP_ELIF: fail(ctx, "Unterminated #elif"); break;
        default: SDL_assert(0 && "Shouldn't hit this case"); break;
    }

    /* pop this conditional, we'll report the next error next time... */

    state->conditional_stack = cond->next;  /* pop it. */
    put_conditional(ctx, cond);
}

static inline const char *_preprocessor_nexttoken(Context *ctx, size_t *_len, Token *_token)
{
    while (SDL_TRUE) {
        IncludeState *state = ctx->include_stack;
        const Conditional *cond;
        SDL_bool skipping;
        Token token;

        if (state == NULL) {
            *_token = TOKEN_EOI;
            *_len = 0;
            return NULL;  /* we're done! */
        }

        ctx->position = state->line;
        SDL_assert(ctx->filename == state->filename);  /* should be same pointer in a stringcache */

        cond = state->conditional_stack;
        skipping = ((cond != NULL) && (cond->skipping)) ? SDL_TRUE : SDL_FALSE;

        state->report_whitespace = SDL_TRUE;

        token = lexer(state);

        state->report_whitespace = SDL_FALSE;

        if (token == TOKEN_EOI) {
            SDL_assert(state->bytes_left == 0);
            if (state->conditional_stack != NULL) {
                unterminated_pp_condition(ctx);
                continue;  /* returns an error. */
            }

            pop_source(ctx);
            continue;  /* pick up again after parent's #include line. */
        } else if (token == TOKEN_INCOMPLETE_STRING_LITERAL) {
            fail(ctx, "Incomplete string literal");
            continue;  /* will return at top of loop. */
        } else if (token == TOKEN_INCOMPLETE_COMMENT) {
            fail(ctx, "Incomplete multiline comment");
            continue;  /* will return at top of loop. */
        } else if (token == TOKEN_PP_IFDEF) {
            handle_pp_ifdef(ctx);
            continue;  /* get the next thing. */
        } else if (token == TOKEN_PP_IFNDEF) {
            handle_pp_ifndef(ctx);
            continue;  /* get the next thing. */
        } else if (token == TOKEN_PP_IF) {
            handle_pp_if(ctx);
            continue;  /* get the next thing. */
        } else if (token == TOKEN_PP_ELIF) {
            handle_pp_elif(ctx);
            continue;  /* get the next thing. */
        } else if (token == TOKEN_PP_ENDIF) {
            handle_pp_endif(ctx);
            continue;  /* get the next thing. */
        } else if (token == TOKEN_PP_ELSE) {
            handle_pp_else(ctx);
            continue;  /* get the next thing. */

        /* NOTE: Conditionals must be above (skipping) test. */

        } else if (skipping) {
            continue;  /* just keep dumping tokens until we get end of block. */
        } else if (token == TOKEN_PP_INCLUDE) {
            handle_pp_include(ctx);
            continue;  /* will return error or use new top of include_stack. */
        } else if (token == TOKEN_PP_LINE) {
            handle_pp_line(ctx);
            continue;  /* get the next thing. */
        } else if (token == TOKEN_PP_ERROR) {
            handle_pp_error(ctx);
            continue;  /* will return at top of loop. */
        } else if (token == TOKEN_PP_DEFINE) {
            handle_pp_define(ctx);
            continue;  /* will return at top of loop. */
        } else if (token == TOKEN_PP_UNDEF) {
            handle_pp_undef(ctx);
            continue;  /* will return at top of loop. */
        } else if (token == TOKEN_PP_PRAGMA) {
            ctx->parsing_pragma = SDL_TRUE;
        } else if (token == TOKEN_PP_BAD) {
            handle_pp_bad(ctx);
            continue;  /* will return at top of loop. */
        }

        /* !!! FIXME: was this meant to be an "else if"? */
        if (token == TOKEN_IDENTIFIER) {
            if (handle_pp_identifier(ctx)) {
                continue;  /* pushed the include_stack. */
            }
        } else if ((token == TOKEN_SINGLE_COMMENT) || (token == TOKEN_MULTI_COMMENT)) {
            ctx->position = state->line;  /* in case line changed in a multicomment. */
            print_debug_lexing_position(ctx);
        } else if (token == ((Token) '\n')) {
            ctx->position = state->line;
            print_debug_lexing_position(ctx);
            if (ctx->parsing_pragma) {  /* let this one through. */
                ctx->parsing_pragma = SDL_FALSE;
            }
        }

        SDL_assert(!skipping);
        *_token = token;
        *_len = state->tokenlen;
        return state->token;
    }

    SDL_assert(0 && "shouldn't hit this code");
    *_token = TOKEN_UNKNOWN;
    *_len = 0;
    return NULL;
}


const char *preprocessor_nexttoken(Context *ctx, size_t *len, Token *token)
{
    const char *retval = _preprocessor_nexttoken(ctx, len, token);
    print_debug_token(retval, *len, *token);
    return retval;
}


static const SDL_SHADER_PreprocessData out_of_mem_data_preprocessor = {
    1, &SDL_SHADER_out_of_mem_error, 0, 0, 0, 0, 0
};


/* public API... */

const SDL_SHADER_PreprocessData *SDL_SHADER_Preprocess(const SDL_SHADER_CompilerParams *params, SDL_bool strip_comments)
{
    SDL_SHADER_PreprocessData *retval = NULL;
    Context *ctx = NULL;
    Buffer *buffer = NULL;
    Token token = TOKEN_UNKNOWN;
    const char *tokstr = NULL;
    int indent = 0;
    size_t len = 0;
    char *output = NULL;
    size_t errcount = 0;
    size_t total_bytes = 0;
    Token prev_token = TOKEN_UNKNOWN;
    SDL_bool whitespace_pending = SDL_FALSE;

    ctx = context_create(params->allocate, params->deallocate, params->allocate_data);
    if (ctx == NULL) {
        return &out_of_mem_data_preprocessor;
    }

    if (!preprocessor_start(ctx, params, SDL_FALSE)) {
        goto preprocess_out_of_mem;
    }

    buffer = buffer_create(4096, MallocContextBridge, FreeContextBridge, ctx);
    if (buffer == NULL) {
        goto preprocess_out_of_mem;
    }

    while ((tokstr = preprocessor_nexttoken(ctx, &len, &token)) != NULL) {
        SDL_assert(token != TOKEN_EOI);

        if (ctx->out_of_memory) {
            goto preprocess_out_of_mem;
        }

        if (whitespace_pending) {
            if ( (token != ((Token) '\n')) && (token != ((Token) ' ')) ) {
                buffer_append(buffer, " " , 1);
            }
            whitespace_pending = SDL_FALSE;
        }

        if (token == ((Token) '\n')) {
            buffer_append(buffer, ENDLINE_STR, SDL_strlen(ENDLINE_STR));
        } else {
            if (strip_comments && (token == TOKEN_SINGLE_COMMENT)) {
                /* just drop this one. */
            } else if (strip_comments && (token == TOKEN_MULTI_COMMENT)) {
                switch (prev_token) {
                    case ' ':
                    case '\n':
                    case TOKEN_MULTI_COMMENT:
                    case TOKEN_UNKNOWN:
                        break;  /* just drop this one */
                    default:
                        /* add a space (even without report_whitespace), so aMLCOMMENTb becomes a b, not ab */
                        whitespace_pending = SDL_TRUE;
                        break;
                }
            } else {
                buffer_append(buffer, tokstr, len);  /* add this token's string. */
            }
        }

        prev_token = token;
    }

    SDL_assert(token == TOKEN_EOI);

    total_bytes = buffer_size(buffer);
    output = buffer_flatten(buffer);
    buffer_destroy(buffer);
    buffer = NULL;  /* don't free this pointer again. */

    if (output == NULL) {
        goto preprocess_out_of_mem;
    }

    retval = (SDL_SHADER_PreprocessData *) Malloc(ctx, sizeof (*retval));
    if (retval == NULL) {
        goto preprocess_out_of_mem;
    }

    SDL_zerop(retval);
    errcount = errorlist_count(ctx->errors);
    if (errcount > 0) {
        retval->error_count = errcount;
        retval->errors = errorlist_flatten(ctx->errors);
        if (retval->errors == NULL) {
            goto preprocess_out_of_mem;
        }
    }

    retval->output = output;
    retval->output_len = total_bytes;
    retval->malloc = params->allocate;
    retval->free = params->deallocate;
    retval->malloc_data = params->allocate_data;

    context_destroy(ctx);

    return retval;

preprocess_out_of_mem:
    SDL_assert(ctx != NULL);
    if (retval != NULL) {
        size_t i;
        for (i = 0; i < retval->error_count; i++) {
            Free(ctx, (void *) retval->errors[i].message);
            Free(ctx, (void *) retval->errors[i].filename);
        }
        Free(ctx, retval);
    }
    Free(ctx, output);
    buffer_destroy(buffer);
    context_destroy(ctx);
    return &out_of_mem_data_preprocessor;
}

void SDL_SHADER_FreePreprocessData(const SDL_SHADER_PreprocessData *_data)
{
    SDL_SHADER_PreprocessData *data = (SDL_SHADER_PreprocessData *) _data;
    SDL_SHADER_Free f;
    void *d;
    int i;

    if ((data == NULL) || (data == &out_of_mem_data_preprocessor)) {
        return;
    }

    f = (data->free == NULL) ? SDL_SHADER_internal_free : data->free;
    d = data->malloc_data;

    f((void *) data->output, d);

    for (i = 0; i < data->error_count; i++) {
        f((void *) data->errors[i].message, d);
        f((void *) data->errors[i].filename, d);
    }
    f((void *) data->errors, d);

    f(data, d);
}

/* end of SDL_shader_preprocessor.c ... */

