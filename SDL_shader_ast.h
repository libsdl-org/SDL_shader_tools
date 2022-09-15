/**
 * SDL_shader_tools; tools for SDL GPU shader support.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#ifndef _INCLUDE_SDL_SHADER_AST_H_
#define _INCLUDE_SDL_SHADER_AST_H_

#include "SDL_shader_compiler.h"

/* Abstract Syntax Tree interface... */

/*
 * ATTENTION: This adds a lot of stuff to the API, but almost everyone can
 *  ignore this section. Seriously, go ahead and skip over anything that has
 *  "AST" in it, unless you know why you'd want to use it.
 *
 * ALSO: This API is still evolving! We make no promises at this time to keep
 *  source or binary compatibility for the AST pieces.
 *
 * Important notes:
 *  - ASTs are the result of parsing the source code: a program that fails to
 *    compile will often parse successfully. Undeclared variables,
 *    type incompatibilities, etc, aren't detected at this point.
 *  - Vector swizzles (the ".xyzw" part of "MyVec4.xyzw") will look like
 *    structure dereferences. We don't realize these are actually swizzles
 *    until semantic analysis.
 *  - SDL_SHADER_AstDataType info is not available when returned from
 *    SDL_SHADER_ParseAst()! We sort these out during semantic analysis, which
 *    happens after the AST parsing is complete. A few are filled in, or can
 *    be deduced fairly trivially by processing several pieces into one.
 *    It's enough that you can reproduce the original source code, more or
 *    less, from the AST.
 */

typedef enum SDL_SHADER_AstNodeType
{
    SDL_SHADER_AST_OP_START_RANGE,         /* expression operators. */

    SDL_SHADER_AST_OP_START_RANGE_UNARY,   /* unary operators. */
    SDL_SHADER_AST_OP_POSITIVE,
    SDL_SHADER_AST_OP_NEGATE,
    SDL_SHADER_AST_OP_COMPLEMENT,
    SDL_SHADER_AST_OP_NOT,
    SDL_SHADER_AST_OP_PARENTHESES,
    SDL_SHADER_AST_OP_END_RANGE_UNARY,

    SDL_SHADER_AST_OP_START_RANGE_BINARY,  /* binary operators. */
    SDL_SHADER_AST_OP_MULTIPLY,
    SDL_SHADER_AST_OP_DIVIDE,
    SDL_SHADER_AST_OP_MODULO,
    SDL_SHADER_AST_OP_ADD,
    SDL_SHADER_AST_OP_SUBTRACT,
    SDL_SHADER_AST_OP_LSHIFT,
    SDL_SHADER_AST_OP_RSHIFT,
    SDL_SHADER_AST_OP_LESSTHAN,
    SDL_SHADER_AST_OP_GREATERTHAN,
    SDL_SHADER_AST_OP_LESSTHANOREQUAL,
    SDL_SHADER_AST_OP_GREATERTHANOREQUAL,
    SDL_SHADER_AST_OP_EQUAL,
    SDL_SHADER_AST_OP_NOTEQUAL,
    SDL_SHADER_AST_OP_BINARYAND,
    SDL_SHADER_AST_OP_BINARYXOR,
    SDL_SHADER_AST_OP_BINARYOR,
    SDL_SHADER_AST_OP_LOGICALAND,
    SDL_SHADER_AST_OP_LOGICALOR,
    SDL_SHADER_AST_OP_DEREF_ARRAY,  /* !!! FIXME: is this _really_ binary? */
    SDL_SHADER_AST_OP_END_RANGE_BINARY,

    SDL_SHADER_AST_OP_START_RANGE_TERNARY,  /* ternary operators. */
    SDL_SHADER_AST_OP_CONDITIONAL,
    SDL_SHADER_AST_OP_END_RANGE_TERNARY,

    SDL_SHADER_AST_OP_START_RANGE_DATA,     /* expression operands. */
    SDL_SHADER_AST_OP_IDENTIFIER,
    SDL_SHADER_AST_OP_INT_LITERAL,
    SDL_SHADER_AST_OP_FLOAT_LITERAL,
    SDL_SHADER_AST_OP_BOOLEAN_LITERAL,
    SDL_SHADER_AST_OP_END_RANGE_DATA,

    SDL_SHADER_AST_OP_START_RANGE_MISC,     /* other expression things. */
    SDL_SHADER_AST_OP_DEREF_STRUCT,
    SDL_SHADER_AST_OP_CALLFUNC,
    SDL_SHADER_AST_OP_END_RANGE_MISC,
    SDL_SHADER_AST_OP_END_RANGE,

    SDL_SHADER_AST_STATEMENT_START_RANGE,   /* statements in function scope. */
    SDL_SHADER_AST_STATEMENT_EMPTY,
    SDL_SHADER_AST_STATEMENT_BREAK,
    SDL_SHADER_AST_STATEMENT_CONTINUE,
    SDL_SHADER_AST_STATEMENT_DISCARD,
    SDL_SHADER_AST_STATEMENT_VARDECL,
    SDL_SHADER_AST_STATEMENT_DO,
    SDL_SHADER_AST_STATEMENT_WHILE,
    SDL_SHADER_AST_STATEMENT_FOR,
    SDL_SHADER_AST_STATEMENT_IF,
    SDL_SHADER_AST_STATEMENT_RETURN,
    SDL_SHADER_AST_STATEMENT_BLOCK,
    SDL_SHADER_AST_STATEMENT_PREINCREMENT,
    SDL_SHADER_AST_STATEMENT_POSTINCREMENT,
    SDL_SHADER_AST_STATEMENT_PREDECREMENT,
    SDL_SHADER_AST_STATEMENT_POSTDECREMENT,
    SDL_SHADER_AST_STATEMENT_FUNCTION_CALL,
    SDL_SHADER_AST_STATEMENT_ASSIGNMENT_START_RANGE,
    SDL_SHADER_AST_STATEMENT_ASSIGNMENT,
    SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNMUL,
    SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNDIV,
    SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNMOD,
    SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNADD,
    SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNSUB,
    SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNLSHIFT,
    SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNRSHIFT,
    SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNAND,
    SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNXOR,
    SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNOR,
    SDL_SHADER_AST_STATEMENT_ASSIGNMENT_END_RANGE,
    SDL_SHADER_AST_STATEMENT_END_RANGE,

    SDL_SHADER_AST_TRANSUNIT_START_RANGE,    /* translation units; things in global scope. */
    SDL_SHADER_AST_TRANSUNIT_FUNCTION,
    SDL_SHADER_AST_TRANSUNIT_STRUCT,
    SDL_SHADER_AST_TRANSUNIT_END_RANGE,

    SDL_SHADER_AST_MISC_START_RANGE,        /* misc. syntactic glue, etc */
    SDL_SHADER_AST_AT_ATTRIBUTE,
    SDL_SHADER_AST_FUNCTION_PARAM,
    SDL_SHADER_AST_FUNCTION,
    SDL_SHADER_AST_VARIABLE_DECLARATION,
    SDL_SHADER_AST_ARRAY_BOUNDS,
    SDL_SHADER_AST_STRUCT_DECLARATION,
    SDL_SHADER_AST_STRUCT_MEMBER,
    SDL_SHADER_AST_SHADER,  /* this is the top level thing. */
    SDL_SHADER_AST_MISC_END_RANGE,

    SDL_SHADER_AST_END_RANGE
} SDL_SHADER_AstNodeType;


typedef struct SDL_SHADER_AstDataType SDL_SHADER_AstDataType;  /* this is opaque to external apps, for now at least. */

typedef struct SDL_SHADER_AstNodeInfo
{
    SDL_SHADER_AstNodeType type;
    const char *filename;
    size_t line;
    /* !!! FIXME: position */
    const SDL_SHADER_AstDataType *dt;  /* this is NULL for everything before semantic analysis. Not every node has a datatype. */
} SDL_SHADER_AstNodeInfo;

/* You can cast any AST node pointer to this. */
typedef struct SDL_SHADER_AstGeneric
{
    SDL_SHADER_AstNodeInfo ast;
} SDL_SHADER_AstGeneric;

typedef struct SDL_SHADER_AstAtAttribute
{
    SDL_SHADER_AstNodeInfo ast;
    const char *name;
    SDL_bool has_argument;  /* this will maybe expand later */
    Sint64 argument;
} SDL_SHADER_AstAtAttribute;

typedef struct SDL_SHADER_AstExpression
{
    SDL_SHADER_AstNodeInfo ast;
} SDL_SHADER_AstExpression;

typedef struct SDL_SHADER_AstUnaryExpression
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstExpression *operand;
} SDL_SHADER_AstUnaryExpression;

typedef struct SDL_SHADER_AstBinaryExpression
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstExpression *left;
    SDL_SHADER_AstExpression *right;
} SDL_SHADER_AstBinaryExpression;

typedef struct SDL_SHADER_AstTernaryExpression
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstExpression *left;
    SDL_SHADER_AstExpression *center;
    SDL_SHADER_AstExpression *right;
} SDL_SHADER_AstTernaryExpression;

typedef struct SDL_SHADER_AstIdentifierExpression
{
    SDL_SHADER_AstNodeInfo ast;
    const char *name;
} SDL_SHADER_AstIdentifierExpression;

typedef struct SDL_SHADER_AstIntLiteralExpression
{
    SDL_SHADER_AstNodeInfo ast;
    Sint64 value;
} SDL_SHADER_AstIntLiteralExpression;

typedef struct SDL_SHADER_AstFloatLiteralExpression
{
    SDL_SHADER_AstNodeInfo ast;
    double value;
} SDL_SHADER_AstFloatLiteralExpression;

typedef struct SDL_SHADER_AstBooleanLiteralExpression
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_bool value;
} SDL_SHADER_AstBooleanLiteralExpression;

typedef struct SDL_SHADER_AstArgument
{
    SDL_SHADER_AstExpression *arg;
    struct SDL_SHADER_AstArgument *next;
} SDL_SHADER_AstArgument;

typedef struct SDL_SHADER_AstArguments
{
    SDL_SHADER_AstArgument *head;
    SDL_SHADER_AstArgument *tail;
} SDL_SHADER_AstArguments;

typedef struct SDL_SHADER_AstStructDerefExpression
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstExpression *expr;
    const char *field;
} SDL_SHADER_AstStructDerefExpression;

struct SDL_SHADER_AstFunction;

typedef struct SDL_SHADER_AstFunctionCallExpression
{
    SDL_SHADER_AstNodeInfo ast;
    const char *fnname;
    SDL_SHADER_AstArguments *arguments;  /* NULL if there are no arguments ("()") */
    struct SDL_SHADER_AstFunction *fn;  /* always NULL until semantic analysis (and will remain NULL for constructors) */
} SDL_SHADER_AstFunctionCallExpression;

typedef struct SDL_SHADER_AstArrayBounds
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstExpression *size;
    struct SDL_SHADER_AstArrayBounds *next;
} SDL_SHADER_AstArrayBounds;

typedef struct SDL_SHADER_AstArrayBoundsList
{
    SDL_SHADER_AstArrayBounds *head;
    SDL_SHADER_AstArrayBounds *tail;
} SDL_SHADER_AstArrayBoundsList;

typedef struct SDL_SHADER_AstVarDeclaration
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_bool c_style;  /* SDL_TRUE if "float x;", SDL_FALSE if "x : float;" */
    const char *datatype_name;  /* not resolved until semantic analysis */
    const char *name;
    SDL_SHADER_AstArrayBoundsList *arraybounds;
    SDL_SHADER_AstAtAttribute *attribute;
} SDL_SHADER_AstVarDeclaration;

typedef struct SDL_SHADER_AstStructMember
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstVarDeclaration *vardecl;
    struct SDL_SHADER_AstStructMember *next;
} SDL_SHADER_AstStructMember;

typedef struct SDL_SHADER_AstStructMembers
{
    SDL_SHADER_AstStructMember *head;
    SDL_SHADER_AstStructMember *tail;
} SDL_SHADER_AstStructMembers;

typedef struct SDL_SHADER_AstStructDeclaration
{
    SDL_SHADER_AstNodeInfo ast;
    const char *name;
    SDL_SHADER_AstStructMembers *members;
    struct SDL_SHADER_AstStructDeclaration *nextstruct;  /* semantic analysis uses this, you should ignore it. */
} SDL_SHADER_AstStructDeclaration;

typedef struct SDL_SHADER_AstStatement
{
    SDL_SHADER_AstNodeInfo ast;
    struct SDL_SHADER_AstStatement *next;
} SDL_SHADER_AstStatement;

typedef SDL_SHADER_AstStatement SDL_SHADER_AstSimpleStatement;
typedef SDL_SHADER_AstSimpleStatement SDL_SHADER_AstEmptyStatement;
typedef SDL_SHADER_AstSimpleStatement SDL_SHADER_AstDiscardStatement;

typedef struct SDL_SHADER_AstBreakStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstStatement *parent;  /* NULL until semantic analysis. */
} SDL_SHADER_AstBreakStatement;

typedef SDL_SHADER_AstBreakStatement SDL_SHADER_AstContinueStatement;

typedef struct SDL_SHADER_AstStatementBlock
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstStatement *head;
    SDL_SHADER_AstStatement *tail;
} SDL_SHADER_AstStatementBlock;

typedef struct SDL_SHADER_AstForDetails
{
    SDL_SHADER_AstStatement *initializer;
    SDL_SHADER_AstExpression *condition;
    SDL_SHADER_AstStatement *step;
} SDL_SHADER_AstForDetails;

typedef struct SDL_SHADER_AstForStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstForDetails *details;
    SDL_SHADER_AstStatementBlock *code;
} SDL_SHADER_AstForStatement;

typedef struct SDL_SHADER_AstVarDeclStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstVarDeclaration *vardecl;
    SDL_SHADER_AstExpression *initializer;
} SDL_SHADER_AstVarDeclStatement;

typedef struct SDL_SHADER_AstDoStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstStatementBlock *code;
    SDL_SHADER_AstExpression *condition;
} SDL_SHADER_AstDoStatement;

typedef struct SDL_SHADER_AstWhileStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstStatementBlock *code;
    SDL_SHADER_AstExpression *condition;
} SDL_SHADER_AstWhileStatement;

typedef struct SDL_SHADER_AstIfStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstExpression *condition;
    SDL_SHADER_AstStatementBlock *code;
    SDL_SHADER_AstStatementBlock *else_code;  /* NULL if there's no else clause. */
} SDL_SHADER_AstIfStatement;

typedef struct SDL_SHADER_AstSwitchCase
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstExpression *condition;  /* NULL for default */
    SDL_SHADER_AstStatement *code;  /* NULL for fallthrough */
    struct SDL_SHADER_AstSwitchCase *next;
} SDL_SHADER_AstSwitchCase;

typedef struct SDL_SHADER_AstSwitchCases
{
    SDL_SHADER_AstSwitchCase *head;
    SDL_SHADER_AstSwitchCase *tail;
} SDL_SHADER_AstSwitchCases;

typedef struct SDL_SHADER_AstSwitchStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstExpression *condition;
    SDL_SHADER_AstSwitchCases *cases;
} SDL_SHADER_AstSwitchStatement;

typedef struct SDL_SHADER_AstReturnStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstExpression *value;  /* NULL if no expression (for void returns). */
} SDL_SHADER_AstReturnStatement;

typedef struct SDL_SHADER_AstAssignment
{
    SDL_SHADER_AstExpression *expr;
    struct SDL_SHADER_AstAssignment *next;
} SDL_SHADER_AstAssignment;

typedef struct SDL_SHADER_AstAssignments
{
    SDL_SHADER_AstAssignment *head;
    SDL_SHADER_AstAssignment *tail;
} SDL_SHADER_AstAssignments;

typedef struct SDL_SHADER_AstAssignStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstAssignments *assignments;
    SDL_SHADER_AstExpression *value;
} SDL_SHADER_AstAssignStatement;

typedef struct SDL_SHADER_AstCompoundAssignStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstExpression *assignment;
    SDL_SHADER_AstExpression *value;
} SDL_SHADER_AstCompoundAssignStatement;

typedef struct SDL_SHADER_AstIncrementStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstExpression *assignment;
} SDL_SHADER_AstIncrementStatement;

typedef struct SDL_SHADER_AstFunctionCallStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstFunctionCallExpression *expr;
} SDL_SHADER_AstFunctionCallStatement;

union SDL_SHADER_AstForInitializer
{
    SDL_SHADER_AstNodeInfo *ast;
    SDL_SHADER_AstExpression *expr;
    SDL_SHADER_AstVarDeclaration *vardecl;
    SDL_SHADER_AstAssignStatement *assignment;
};

typedef struct SDL_SHADER_AstFunctionParam
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstVarDeclaration *vardecl;
    struct SDL_SHADER_AstFunctionParam *next;
} SDL_SHADER_AstFunctionParam;

typedef struct SDL_SHADER_AstFunctionParams
{
    SDL_SHADER_AstFunctionParam *head;
    SDL_SHADER_AstFunctionParam *tail;
} SDL_SHADER_AstFunctionParams;

typedef enum SDL_SHADER_AstFunctionType
{
    SDL_SHADER_AST_FNTYPE_UNKNOWN,
    SDL_SHADER_AST_FNTYPE_NORMAL,
    SDL_SHADER_AST_FNTYPE_VERTEX,
    SDL_SHADER_AST_FNTYPE_FRAGMENT
} SDL_SHADER_AstFunctionType;

typedef struct SDL_SHADER_AstFunction
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstFunctionType fntype;  /* SDL_SHADER_AST_FNTYPE_UNKNOWN until semantic analysis */
    SDL_SHADER_AstVarDeclaration *vardecl;
    SDL_SHADER_AstFunctionParams *params;  /* NULL==void */
    SDL_SHADER_AstStatementBlock *code;
    struct SDL_SHADER_AstFunction *nextfn;  /* semantic analysis uses this, you should ignore it. */
} SDL_SHADER_AstFunction;

typedef struct SDL_SHADER_AstTranslationUnit
{
    SDL_SHADER_AstNodeInfo ast;
    struct SDL_SHADER_AstTranslationUnit *next;
} SDL_SHADER_AstTranslationUnit;

typedef struct SDL_SHADER_AstTranslationUnits
{
    SDL_SHADER_AstTranslationUnit *head;
    SDL_SHADER_AstTranslationUnit *tail;
} SDL_SHADER_AstTranslationUnits;

typedef struct SDL_SHADER_AstStructDeclarationUnit
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstTranslationUnit *next;
    SDL_SHADER_AstStructDeclaration *decl;
} SDL_SHADER_AstStructDeclarationUnit;

typedef struct SDL_SHADER_AstFunctionUnit
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstTranslationUnit *next;
    SDL_SHADER_AstFunction *fn;
} SDL_SHADER_AstFunctionUnit;

typedef struct SDL_SHADER_AstShader
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstTranslationUnits *units;
} SDL_SHADER_AstShader;

typedef union SDL_SHADER_AstNode
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstGeneric generic;
    SDL_SHADER_AstAtAttribute at_attribute;
    SDL_SHADER_AstExpression expression;
    SDL_SHADER_AstUnaryExpression unary;
    SDL_SHADER_AstBinaryExpression binary;
    SDL_SHADER_AstTernaryExpression ternary;
    SDL_SHADER_AstIdentifierExpression identifier;
    SDL_SHADER_AstIntLiteralExpression intliteral;
    SDL_SHADER_AstFloatLiteralExpression floatliteral;
    SDL_SHADER_AstBooleanLiteralExpression boolliteral;
    SDL_SHADER_AstStructDerefExpression structderef;
    SDL_SHADER_AstFunctionCallExpression fncall;
    SDL_SHADER_AstStructMember structmember;
    SDL_SHADER_AstStructDeclaration structdecl;
    SDL_SHADER_AstVarDeclaration vardecl;
    SDL_SHADER_AstArrayBounds arraybounds;
    SDL_SHADER_AstStatement stmt;
    SDL_SHADER_AstSimpleStatement simplestmt;
    SDL_SHADER_AstEmptyStatement emptystmt;
    SDL_SHADER_AstBreakStatement breakstmt;
    SDL_SHADER_AstContinueStatement contstmt;
    SDL_SHADER_AstDiscardStatement discardstmt;
    SDL_SHADER_AstForStatement forstmt;
    SDL_SHADER_AstVarDeclStatement vardeclstmt;
    SDL_SHADER_AstDoStatement dostmt;
    SDL_SHADER_AstWhileStatement whilestmt;
    SDL_SHADER_AstIfStatement ifstmt;
    SDL_SHADER_AstSwitchCase switchcase;
    SDL_SHADER_AstSwitchStatement switchstmt;
    SDL_SHADER_AstReturnStatement returnstmt;
    SDL_SHADER_AstAssignStatement assignstmt;
    SDL_SHADER_AstCompoundAssignStatement compoundassignstmt;
    SDL_SHADER_AstIncrementStatement incrementstmt;
    SDL_SHADER_AstFunctionCallStatement fncallstmt;
    SDL_SHADER_AstStatementBlock stmtblock;
    SDL_SHADER_AstFunctionParam fnparam;
    SDL_SHADER_AstFunction fn;
    SDL_SHADER_AstTranslationUnit unit;
    SDL_SHADER_AstStructDeclarationUnit structdeclunit;
    SDL_SHADER_AstFunctionUnit fnunit;
    SDL_SHADER_AstShader shader;
} SDL_SHADER_AstNode;



/*
 * Structure used to return data from parsing of a shader into an AST...
 */
/* !!! FIXME: most of these ints should be unsigned. */
typedef struct SDL_SHADER_AstData
{
    /*
     * The number of elements pointed to by (errors).
     */
    size_t error_count;

    /*
     * (error_count) elements of data that specify errors that were generated
     *  by parsing this shader.
     * This can be NULL if there were no errors or if (error_count) is zero.
     *  Note that this will only produce errors for syntax problems. Most of
     *  the things we expect a compiler to produce errors for--incompatible
     *  types, unknown identifiers, etc--are not checked at all during
     *  initial generation of the syntax tree...bogus programs that would
     *  fail to compile will pass here without error, if they are syntactically
     *  correct!
     */
    const SDL_SHADER_Error *errors;

    /*
     * The name of the source profile used to parse the shader. Will be NULL
     *  on error.
     */
    const char *source_profile;

    /*
     * The actual syntax tree. You are responsible for walking it yourself.
     *  Will be NULL on error.
     */
    const SDL_SHADER_AstShader *shader;

    /*
     * This is the malloc implementation you passed to SDL_SHADER_Parse().
     */
    SDL_SHADER_Malloc malloc;

    /*
     * This is the free implementation you passed to SDL_SHADER_Parse().
     */
    SDL_SHADER_Free free;

    /*
     * This is the pointer you passed as opaque data for your allocator.
     */
    void *malloc_data;

    /*
     * This is internal data, and not for the application to touch.
     */
    void *opaque;
} SDL_SHADER_AstData;


/*
 * You almost certainly don't need this function, unless you absolutely know
 *  why you need it without hesitation. This is almost certainly only good for
 *  building code analysis tools on top of.
 *
 * This is intended to parse SDLSL source code, turning it into an abstract
 *  syntax tree.
 *
 * (srcprofile) specifies the source language of the shader. You can specify
 *  a shader model with this, too. See SDL_SHADER_SRC_PROFILE_* constants.
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
 * This will return a SDL_SHADER_AstData. The data supplied here gives the
 *  application a tree-like structure they can walk to see the layout of
 *  a given program. When you are done with this data, pass it to
 *  SDL_SHADER_FreeAstData() to deallocate resources.
 *
 * This function will never return NULL, even if the system is completely
 *  out of memory upon entry (in which case, this function returns a static
 *  SDL_SHADER_AstData object, which is still safe to pass to
 *  SDL_SHADER_FreeAstData()).
 *
 * As parsing requires some memory to be allocated, you may provide a
 *  custom allocator to this function, which will be used to allocate/free
 *  memory. They function just like malloc() and free(). We do not use
 *  realloc(). If you don't care, pass NULL in for the allocator functions.
 *  If your allocator needs instance-specific data, you may supply it with the
 *  (d) parameter. This pointer is passed as-is to your (m) and (f) functions.
 *
 * This function is thread safe, so long as the various callback functions
 *  are, too, and that the parameters remains intact for the duration of the
 *  call. This allows you to parse several shaders on separate CPU cores
 *  at the same time.
 */
extern DECLSPEC const SDL_SHADER_AstData * SDLCALL SDL_SHADER_ParseAst(const SDL_SHADER_CompilerParams *params);


/* !!! FIXME: expose semantic analysis to the public API? */


/*
 * Call this to dispose of AST parsing results when you are done with them.
 *  This will call the SDL_SHADER_free function you provided to
 *  SDL_SHADER_parseAst() multiple times, if you provided one.
 *  Passing a NULL here is a safe no-op.
 *
 * This function is thread safe, so long as any allocator you passed into
 *  SDL_SHADER_parseAst() is, too.
 */
extern DECLSPEC void SDLCALL SDL_SHADER_FreeAstData(const SDL_SHADER_AstData *data);

#endif

/* end of SDL_shader_ast.h ... */

