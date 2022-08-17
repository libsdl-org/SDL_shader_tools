/**
 * SDL_shader_tools; tools for SDL GPU shader support.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

/* !!! FIXME: review all comments, to make sure it matches the altered functionality */

#ifndef _INCL_SDL_SHADER_COMPILER_H_
#define _INCL_SDL_SHADER_COMPILER_H_

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * These allocators work like the C runtime's malloc() and free()
 *  (in fact, they use SDL_malloc() and SDL_free() internally if you don't
 *  specify your own allocator).
 * (data) is the pointer you supplied when specifying these allocator
 *  callbacks, in case you need instance-specific data...it is passed through
 *  to your allocator unmolested, and can be NULL if you like.
 */
typedef void *(SDLCALL *SDL_SHADER_Malloc)(size_t bytes, void *data);
typedef void (SDLCALL *SDL_SHADER_Free)(void *ptr, void *data);


/*
 * These are used with SDL_SHADER_error as special case positions.
 */
#define SDL_SHADER_POSITION_NONE (-3)
#define SDL_SHADER_POSITION_BEFORE (-2)
#define SDL_SHADER_POSITION_AFTER (-1)

typedef struct SDL_SHADER_Error
{
    /*
     * SDL_TRUE if an error, SDL_FALSE if a warning.
     */
    SDL_bool is_error;

    /*
     * Human-readable error message, if there is one. Will be NULL if there was no
     *  error. The string will be UTF-8 encoded, and English only. Most of
     *  these shouldn't be shown to the end-user anyhow.
     */
    const char *message;

    /*
     * Filename where error happened. This can be NULL if the information
     *  isn't available.
     */
    const char *filename;

    /*
     * Position of error, if there is one. Will be SDL_SHADER_POSITION_NONE if
     *  there was no error, SDL_SHADER_POSITION_BEFORE if there was an error
     *  before processing started, and SDL_SHADER_POSITION_AFTER if there was
     *  an error during final processing.
     */
    Sint32 error_position;
} SDL_SHADER_Error;


/* Preprocessor interface... */

/*
 * Structure used to pass predefined macros.
 *  You can have macro arguments: set identifier to "a(b, c)" or whatever.
 */
typedef struct SDL_SHADER_PreprocessorDefine
{
    const char *identifier;
    const char *definition;
} SDL_SHADER_PreprocessorDefine;

/*
 * Used with the SDL_SHADER_includeOpen callback.
 */
typedef enum SDL_SHADER_IncludeType
{
    SDL_SHADER_INCLUDETYPE_LOCAL,   /* local header: #include "blah.h" */
    SDL_SHADER_INCLUDETYPE_SYSTEM   /* system header: #include <blah.h> */
} SDL_SHADER_IncludeType;


/*
 * Structure used to return data from preprocessing of a shader...
 */
typedef struct SDL_SHADER_PreprocessData
{
    /* The number of elements pointed to by (errors). */
    size_t error_count;

    /*
     * (error_count) elements of data that specify errors that were generated
     *  by parsing this shader.
     * This can be NULL if there were no errors or if (error_count) is zero.
     */
    const SDL_SHADER_Error *errors;

    /*
     * Bytes of output from preprocessing. This is a UTF-8 string. We
     *  guarantee it to be NULL-terminated. Will be NULL on error.
     */
    const char *output;

    /* Byte count for output, not counting any null terminator. Will be 0 on error. */
    size_t output_len;

    /* This is the malloc implementation you passed in. */
    SDL_SHADER_Malloc malloc;

    /* This is the free implementation you passed in. */
    SDL_SHADER_Free free;

    /* This is the pointer you passed as opaque data for your allocator. */
    void *malloc_data;
} SDL_SHADER_PreprocessData;


/*
 * This callback allows an app to handle #include statements for the
 *  preprocessor. When the preprocessor sees an #include, it will call this
 *  function to obtain the contents of the requested file. This is optional;
 *  the preprocessor will open files directly if no callback is supplied, but
 *  this allows an app to retrieve data from something other than the
 *  traditional filesystem (for example, headers packed in a .zip file or
 *  headers generated on-the-fly).
 *
 * (inctype) specifies the type of header we wish to include.
 * (fname) specifies the name of the file specified on the #include line.
 * (parent_fname) is a string of the filename containing the include. This
 *  might be NULL, or nonsense, depending on how files have been included
 *  and what your callback accepts.
 * (parent_data) is a string of the entire source file containing the include, in
 *  its original, not-yet-preprocessed state. Note that this is just the
 *  contents of the specific file, not all source code that the preprocessor
 *  has seen through other includes, etc.
 * (outdata) will be set by the callback to a pointer to the included file's
 *  contents. The callback is responsible for allocating this however they
 *  see fit (we provide allocator functions, but you may ignore them). This
 *  pointer must remain valid until the includeClose callback runs. This
 *  string does not need to be NULL-terminated.
 * (outbytes) will be set by the callback to the number of bytes pointed to
 *  by (outdata).
 * (include_paths) is an array of (include_path_count) strings that are
 *  paths that make up the include directories. Note that this list will
 *  only include system or local paths, depending on what (inctype) is.
 * If returning SDL_FALSE, fill in (failstr) with up to (failstrlen) bytes
 *  to describe the error condition.
 * (m),(f), and (d) are the allocator details that the application passed in.
 *  If these were NULL, they will be replaced them with internal allocators.
 *
 * The callback returns the file opened (so that relative paths will be
 * correct for future includes), or NULL on error. If the file opened was
 * unchanged, it's legal to return `fname` here. If the returned string
 * is a different pointer than `fname`, it will be free'd later with the
 * `f(str, d);`.
 *
 * If you supply an includeOpen callback, you must supply includeClose, too.
 */
typedef const char * (SDLCALL *SDL_SHADER_IncludeOpen)(SDL_SHADER_IncludeType inctype,
                            const char *fname, const char *parent_fname,
                            const char *parent_data,
                            const char **outdata, size_t *outbytes,
                            const char **include_paths,
                            size_t include_path_count,
                            char *failstr, size_t failstrlen,
                            SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d);

/*
 * This callback allows an app to clean up the results of a previous
 *  includeOpen callback.
 *
 * (data) is the data that was returned from a previous call to includeOpen.
 *  It is now safe to deallocate this data.
 * (m),(f), and (d) are the same allocator details that were passed to your
 *  includeOpen callback.
 *
 * If you supply an includeClose callback, you must supply includeOpen, too.
 */
typedef void (SDLCALL *SDL_SHADER_IncludeClose)(const char *data,
                            SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d);


/* there's too many options to a compiler, so now they all live in a struct
   so you don't call these APIs with 17 different parameters. */
typedef struct SDL_SHADER_CompilerParams
{
    const char *srcprofile;
    const char *filename;
    const char *source;
    size_t sourcelen;
    SDL_bool allow_dotdot_includes;  /* if SDL_FALSE, fail on `#include "path/with/../in/it"` */
    SDL_bool allow_absolute_includes;  /* if SDL_FALSE, fail on `#include "/absolute/path"` */
    const SDL_SHADER_PreprocessorDefine *defines;
    size_t define_count;
    const char **system_include_paths;
    size_t system_include_path_count;
    const char **local_include_paths;
    size_t local_include_path_count;
    SDL_SHADER_IncludeOpen include_open;
    SDL_SHADER_IncludeClose include_close;
    SDL_SHADER_Malloc allocate;
    SDL_SHADER_Free deallocate;
    void *allocate_data;
} SDL_SHADER_CompilerParams;


/*
 * This function is optional. Even if you are dealing with shader source
 *  code, you don't need to explicitly use the preprocessor, as the compiler
 *  will use it behind the scenes. In fact, you probably never need this
 *  function unless you are debugging a custom tool (or debugging this library
 *  itself), but it's possible having access to an easy to embed, C callable
 *  C preprocessor could be useful in some situations, too.
 *
 * !!! FIXME: move most of this documentation to SDL_SHADER_CompilerParams
 *
 * Preprocessing roughly follows the syntax of an ANSI C preprocessor.
 *
 * (filename) is a NULL-terminated UTF-8 filename. It can be NULL. We do not
 *  actually access this file, as we obtain our data from (source). This
 *  string is copied when we need to report errors while processing (source),
 *  as opposed to errors in a file referenced via the #include directive in
 *  (source). If this is NULL, then errors will report the filename as NULL,
 *  too.
 *
 * (source) is an string of UTF-8 text to preprocess. It does not need to be
 *  NULL-terminated.
 *
 * (sourcelen) is the length of the string pointed to by (source), in bytes.
 *
 * (defines) points to (define_count) preprocessor definitions, and can be
 *  NULL. These are treated by the preprocessor as if the source code started
 *  with one #define for each entry you pass in here.
 *
 * (include_open) and (include_close) let the app control the preprocessor's
 *  behaviour for #include statements. Both are optional and can be NULL, but
 *  both must be specified if either is specified.
 *
 * This will return a SDL_SHADER_PreprocessorData. You should pass this
 *  return value to SDL_SHADER_FreePreprocessData() when you are done with
 *  it.
 *
 * This function will never return NULL, even if the system is completely
 *  out of memory upon entry (in which case, this function returns a static
 *  SDL_SHADER_PreprocessData object, which is still safe to pass to
 *  SDL_SHADER_FreePreprocessData()).
 *
 * As preprocessing requires some memory to be allocated, you may provide a
 *  custom allocator to this function, which will be used to allocate/free
 *  memory. They function just like malloc() and free(). We do not use
 *  realloc(). If you don't care, pass NULL in for the allocator functions.
 *  If your allocator needs instance-specific data, you may supply it with the
 *  (d) parameter. This pointer is passed as-is to your (m) and (f) functions.
 *
 * This function is thread safe, so long as the various callback functions
 *  are, too, and that the parameters remains intact for the duration of the
 *  call. This allows you to preprocess several shaders on separate CPU cores
 *  at the same time.
 */
extern DECLSPEC const SDL_SHADER_PreprocessData * SDLCALL SDL_SHADER_Preprocess(const SDL_SHADER_CompilerParams *params, SDL_bool strip_comments);

/*
 * Call this to dispose of preprocessing results when you are done with them.
 *  This will call the SDL_SHADER_free function you provided to
 *  SDL_SHADER_Preprocess() multiple times, if you provided one.
 *  Passing a NULL here is a safe no-op.
 *
 * This function is thread safe, so long as any allocator you passed into
 *  SDL_SHADER_Preprocess() is, too.
 */
extern DECLSPEC void SDLCALL SDL_SHADER_FreePreprocessData(const SDL_SHADER_PreprocessData *data);


/* Compiler interface... */

/* Structure used to return data from parsing of a shader... */
typedef struct SDL_SHADER_CompileData
{
    /*
     * The number of elements pointed to by (errors).
     */
    size_t error_count;

    /*
     * (error_count) elements of data that specify errors that were generated
     *  by parsing this shader.
     */
    const SDL_SHADER_Error *errors;

    /*
     * The name of the source profile used to parse the shader. Will be NULL
     *  on error.
     */
    const char *source_profile;

    /*
     * Bytes of output from compiling.
     */
    const Uint8 *output;

    /*
     * Byte count for output, not counting any null terminator.
     *  Will be 0 on error.
     */
    size_t output_len;

    /*
     * This is the malloc implementation you passed to SDL_SHADER_Compile().
     */
    SDL_SHADER_Malloc malloc;

    /*
     * This is the free implementation you passed to SDL_SHADER_Compile().
     */
    SDL_SHADER_Free free;

    /*
     * This is the pointer you passed as opaque data for your allocator.
     */
    void *malloc_data;
} SDL_SHADER_CompileData;


/*
 * Use this function to compile shader program source code.
 *
 * !!! FIXME: move most of this documentation to SDL_SHADER_CompilerParams
 *
 * (srcprofile) specifies the source language of the shader. For now, this
 *  must be NULL to signify a default.
 *
 * (filename) is a NULL-terminated UTF-8 filename. It can be NULL. We do not
 *  actually access this file, as we obtain our data from (source). This
 *  string is copied when we need to report errors while processing (source),
 *  as opposed to errors in a file referenced via the #include directive in
 *  (source). If this is NULL, then errors will report the filename as NULL,
 *  too.
 *
 * (source) is an UTF-8 string of valid high-level shader source code.
 *  It does not need to be NULL-terminated.
 *
 * (sourcelen) is the length of the string pointed to by (source), in bytes.
 *
 * (defines) points to (define_count) preprocessor definitions, and can be
 *  NULL. These are treated by the preprocessor as if the source code started
 *  with one #define for each entry you pass in here.
 *
 * (include_open) and (include_close) let the app control the preprocessor's
 *  behaviour for #include statements. Both are optional and can be NULL, but
 *  both must be specified if either is specified.
 *
 * This will return a SDL_SHADER_CompileData.
 *  When you are done with this data, pass it to SDL_SHADER_FreeCompileData()
 *  to deallocate resources.
 *
 * This function will never return NULL, even if the system is completely
 *  out of memory upon entry (in which case, this function returns a static
 *  SDL_SHADER_CompileData object, which is still safe to pass to
 *  SDL_SHADER_FreeCompileData()).
 *
 * As compiling requires some memory to be allocated, you may provide a
 *  custom allocator to this function, which will be used to allocate/free
 *  memory. They function just like malloc() and free(). We do not use
 *  realloc(). If you don't care, pass NULL in for the allocator functions.
 *  If your allocator needs instance-specific data, you may supply it with the
 *  (d) parameter. This pointer is passed as-is to your (m) and (f) functions.
 *
 * This function is thread safe, so long as the various callback functions
 *  are, too, and that the parameters remains intact for the duration of the
 *  call. This allows you to compile several shaders on separate CPU cores
 *  at the same time.
 */
extern DECLSPEC const SDL_SHADER_CompileData * SDLCALL SDL_SHADER_Compile(const SDL_SHADER_CompilerParams *params);


/*
 * Call this to dispose of compile results when you are done with them.
 *  This will call the SDL_SHADER_Free function you provided to
 *  SDL_SHADER_Compile() multiple times, if you provided one.
 *  Passing a NULL here is a safe no-op.
 *
 * This function is thread safe, so long as any allocator you passed into
 *  SDL_SHADER_Compile() is, too.
 */
extern DECLSPEC void SDLCALL SDL_SHADER_FreeCompileData(const SDL_SHADER_CompileData *data);

#ifdef __cplusplus
}
#endif

#endif  /* include-once blocker. */

/* end of SDL_shader_compiler.h ... */

