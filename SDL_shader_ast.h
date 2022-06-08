/**
 * SDL_shader_language; tools for SDL GPU shader support.
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
 *  - SDL_SHADER_AstDataType info is not reliable when returned from
 *    SDL_SHADER_ParseAst()! Most of the datatype info will be missing or have
 *    inaccurate data types. We sort these out during semantic analysis, which
 *    happens after the AST parsing is complete. A few are filled in, or can
 *    be deduced fairly trivially by processing several pieces into one.
 *    It's enough that you can reproduce the original source code, more or
 *    less, from the AST.
 */

/* High-level datatypes for AST nodes. */
typedef enum SDL_SHADER_AstDataTypeType
{
    SDL_SHADER_AST_DATATYPE_NONE,
    SDL_SHADER_AST_DATATYPE_BOOL,
    SDL_SHADER_AST_DATATYPE_INT,
    SDL_SHADER_AST_DATATYPE_UINT,
    SDL_SHADER_AST_DATATYPE_FLOAT,
    SDL_SHADER_AST_DATATYPE_FLOAT_SNORM,
    SDL_SHADER_AST_DATATYPE_FLOAT_UNORM,
    SDL_SHADER_AST_DATATYPE_HALF,
    SDL_SHADER_AST_DATATYPE_DOUBLE,
    SDL_SHADER_AST_DATATYPE_STRING,
    SDL_SHADER_AST_DATATYPE_SAMPLER_1D,
    SDL_SHADER_AST_DATATYPE_SAMPLER_2D,
    SDL_SHADER_AST_DATATYPE_SAMPLER_3D,
    SDL_SHADER_AST_DATATYPE_SAMPLER_CUBE,
    SDL_SHADER_AST_DATATYPE_SAMPLER_STATE,
    SDL_SHADER_AST_DATATYPE_SAMPLER_COMPARISON_STATE,
    SDL_SHADER_AST_DATATYPE_STRUCT,
    SDL_SHADER_AST_DATATYPE_ARRAY,
    SDL_SHADER_AST_DATATYPE_VECTOR,
    SDL_SHADER_AST_DATATYPE_MATRIX,
    SDL_SHADER_AST_DATATYPE_BUFFER,
    SDL_SHADER_AST_DATATYPE_FUNCTION,
    SDL_SHADER_AST_DATATYPE_USER,
} SDL_SHADER_AstDataTypeType;
#define SDL_SHADER_AST_DATATYPE_CONST (1 << 31)

typedef union SDL_SHADER_AstDataType SDL_SHADER_AstDataType;

/* This is just part of DataTypeStruct, never appears outside of it. */
typedef struct SDL_SHADER_AstDataTypeStructMember
{
    const SDL_SHADER_AstDataType *datatype;
    const char *identifier;
} SDL_SHADER_AstDataTypeStructMember;

typedef struct SDL_SHADER_AstDataTypeStruct
{
    SDL_SHADER_AstDataTypeType type;
    const SDL_SHADER_AstDataTypeStructMember *members;
    int member_count;
} SDL_SHADER_AstDataTypeStruct;

typedef struct SDL_SHADER_AstDataTypeArray
{
    SDL_SHADER_AstDataTypeType type;
    const SDL_SHADER_AstDataType *base;
    int elements;
} SDL_SHADER_AstDataTypeArray;

typedef SDL_SHADER_AstDataTypeArray SDL_SHADER_AstDataTypeVector;

typedef struct SDL_SHADER_AstDataTypeMatrix
{
    SDL_SHADER_AstDataTypeType type;
    const SDL_SHADER_AstDataType *base;
    int rows;
    int columns;
} SDL_SHADER_AstDataTypeMatrix;

typedef struct SDL_SHADER_AstDataTypeBuffer
{
    SDL_SHADER_AstDataTypeType type;
    const SDL_SHADER_AstDataType *base;
} SDL_SHADER_AstDataTypeBuffer;

typedef struct SDL_SHADER_AstDataTypeFunction
{
    SDL_SHADER_AstDataTypeType type;
    const SDL_SHADER_AstDataType *retval;
    const SDL_SHADER_AstDataType **params;
    int num_params;
    int intrinsic;  /* non-zero for built-in functions */
} SDL_SHADER_AstDataTypeFunction;

typedef struct SDL_SHADER_AstDataTypeUser
{
    SDL_SHADER_AstDataTypeType type;
    const SDL_SHADER_AstDataType *details;
    const char *name;
} SDL_SHADER_AstDataTypeUser;

union SDL_SHADER_AstDataType
{
    SDL_SHADER_AstDataTypeType type;
    SDL_SHADER_AstDataTypeArray array;
    SDL_SHADER_AstDataTypeStruct structure;
    SDL_SHADER_AstDataTypeVector vector;
    SDL_SHADER_AstDataTypeMatrix matrix;
    SDL_SHADER_AstDataTypeBuffer buffer;
    SDL_SHADER_AstDataTypeUser user;
    SDL_SHADER_AstDataTypeFunction function;
};

/* Structures that make up the parse tree... */

typedef enum SDL_SHADER_AstNodeType
{
    SDL_SHADER_AST_OP_START_RANGE,         /* expression operators. */

    SDL_SHADER_AST_OP_START_RANGE_UNARY,   /* unary operators. */
    SDL_SHADER_AST_OP_PREINCREMENT,
    SDL_SHADER_AST_OP_PREDECREMENT,
    SDL_SHADER_AST_OP_NEGATE,
    SDL_SHADER_AST_OP_COMPLEMENT,
    SDL_SHADER_AST_OP_NOT,
    SDL_SHADER_AST_OP_POSTINCREMENT,
    SDL_SHADER_AST_OP_POSTDECREMENT,
    SDL_SHADER_AST_OP_CAST,
    SDL_SHADER_AST_OP_END_RANGE_UNARY,

    SDL_SHADER_AST_OP_START_RANGE_BINARY,  /* binary operators. */
    SDL_SHADER_AST_OP_COMMA,
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
    SDL_SHADER_AST_OP_ASSIGN,
    SDL_SHADER_AST_OP_MULASSIGN,
    SDL_SHADER_AST_OP_DIVASSIGN,
    SDL_SHADER_AST_OP_MODASSIGN,
    SDL_SHADER_AST_OP_ADDASSIGN,
    SDL_SHADER_AST_OP_SUBASSIGN,
    SDL_SHADER_AST_OP_LSHIFTASSIGN,
    SDL_SHADER_AST_OP_RSHIFTASSIGN,
    SDL_SHADER_AST_OP_ANDASSIGN,
    SDL_SHADER_AST_OP_XORASSIGN,
    SDL_SHADER_AST_OP_ORASSIGN,
    SDL_SHADER_AST_OP_DEREF_ARRAY,
    SDL_SHADER_AST_OP_END_RANGE_BINARY,

    SDL_SHADER_AST_OP_START_RANGE_TERNARY,  /* ternary operators. */
    SDL_SHADER_AST_OP_CONDITIONAL,
    SDL_SHADER_AST_OP_END_RANGE_TERNARY,

    SDL_SHADER_AST_OP_START_RANGE_DATA,     /* expression operands. */
    SDL_SHADER_AST_OP_IDENTIFIER,
    SDL_SHADER_AST_OP_INT_LITERAL,
    SDL_SHADER_AST_OP_FLOAT_LITERAL,
    SDL_SHADER_AST_OP_STRING_LITERAL,
    SDL_SHADER_AST_OP_BOOLEAN_LITERAL,
    SDL_SHADER_AST_OP_END_RANGE_DATA,

    SDL_SHADER_AST_OP_START_RANGE_MISC,     /* other expression things. */
    SDL_SHADER_AST_OP_DEREF_STRUCT,
    SDL_SHADER_AST_OP_CALLFUNC,
    SDL_SHADER_AST_OP_CONSTRUCTOR,
    SDL_SHADER_AST_OP_END_RANGE_MISC,
    SDL_SHADER_AST_OP_END_RANGE,

    SDL_SHADER_AST_COMPUNIT_START_RANGE,    /* things in global scope. */
    SDL_SHADER_AST_COMPUNIT_FUNCTION,
    SDL_SHADER_AST_COMPUNIT_TYPEDEF,
    SDL_SHADER_AST_COMPUNIT_STRUCT,
    SDL_SHADER_AST_COMPUNIT_VARIABLE,
    SDL_SHADER_AST_COMPUNIT_END_RANGE,

    SDL_SHADER_AST_STATEMENT_START_RANGE,   /* statements in function scope. */
    SDL_SHADER_AST_STATEMENT_EMPTY,
    SDL_SHADER_AST_STATEMENT_BREAK,
    SDL_SHADER_AST_STATEMENT_CONTINUE,
    SDL_SHADER_AST_STATEMENT_DISCARD,
    SDL_SHADER_AST_STATEMENT_BLOCK,
    SDL_SHADER_AST_STATEMENT_EXPRESSION,
    SDL_SHADER_AST_STATEMENT_IF,
    SDL_SHADER_AST_STATEMENT_SWITCH,
    SDL_SHADER_AST_STATEMENT_FOR,
    SDL_SHADER_AST_STATEMENT_DO,
    SDL_SHADER_AST_STATEMENT_WHILE,
    SDL_SHADER_AST_STATEMENT_RETURN,
    SDL_SHADER_AST_STATEMENT_TYPEDEF,
    SDL_SHADER_AST_STATEMENT_STRUCT,
    SDL_SHADER_AST_STATEMENT_VARDECL,
    SDL_SHADER_AST_STATEMENT_END_RANGE,

    SDL_SHADER_AST_MISC_START_RANGE,        /* misc. syntactic glue. */
    SDL_SHADER_AST_FUNCTION_PARAMS,
    SDL_SHADER_AST_FUNCTION_SIGNATURE,
    SDL_SHADER_AST_SCALAR_OR_ARRAY,
    SDL_SHADER_AST_TYPEDEF,
    SDL_SHADER_AST_PACK_OFFSET,
    SDL_SHADER_AST_VARIABLE_LOWLEVEL,
    SDL_SHADER_AST_ANNOTATION,
    SDL_SHADER_AST_VARIABLE_DECLARATION,
    SDL_SHADER_AST_STRUCT_DECLARATION,
    SDL_SHADER_AST_STRUCT_MEMBER,
    SDL_SHADER_AST_SWITCH_CASE,
    SDL_SHADER_AST_ARGUMENTS,
    SDL_SHADER_AST_MISC_END_RANGE,

    SDL_SHADER_AST_END_RANGE
} SDL_SHADER_AstNodeType;

typedef struct SDL_SHADER_AstNodeInfo
{
    SDL_SHADER_AstNodeType type;
    const char *filename;
    size_t line;
} SDL_SHADER_AstNodeInfo;

typedef enum SDL_SHADER_AstVariableAttributes
{
    SDL_SHADER_AST_VARATTR_EXTERN = (1 << 0),
    SDL_SHADER_AST_VARATTR_NOINTERPOLATION = (1 << 1),
    SDL_SHADER_AST_VARATTR_SHARED = (1 << 2),
    SDL_SHADER_AST_VARATTR_STATIC = (1 << 3),
    SDL_SHADER_AST_VARATTR_UNIFORM = (1 << 4),
    SDL_SHADER_AST_VARATTR_VOLATILE = (1 << 5),
    SDL_SHADER_AST_VARATTR_CONST = (1 << 6),
    SDL_SHADER_AST_VARATTR_ROWMAJOR = (1 << 7),
    SDL_SHADER_AST_VARATTR_COLUMNMAJOR = (1 << 8)
} SDL_SHADER_AstVariableAttributes;

typedef enum SDL_SHADER_AstIfAttributes
{
    SDL_SHADER_AST_IFATTR_NONE,
    SDL_SHADER_AST_IFATTR_BRANCH,
    SDL_SHADER_AST_IFATTR_FLATTEN,
    SDL_SHADER_AST_IFATTR_IFALL,
    SDL_SHADER_AST_IFATTR_IFANY,
    SDL_SHADER_AST_IFATTR_PREDICATE,
    SDL_SHADER_AST_IFATTR_PREDICATEBLOCK,
} SDL_SHADER_AstIfAttributes;

typedef enum SDL_SHADER_AstSwitchAttributes
{
    SDL_SHADER_AST_SWITCHATTR_NONE,
    SDL_SHADER_AST_SWITCHATTR_FLATTEN,
    SDL_SHADER_AST_SWITCHATTR_BRANCH,
    SDL_SHADER_AST_SWITCHATTR_FORCECASE,
    SDL_SHADER_AST_SWITCHATTR_CALL
} SDL_SHADER_AstSwitchAttributes;

/* You can cast any AST node pointer to this. */
typedef struct SDL_SHADER_AstGeneric
{
    SDL_SHADER_AstNodeInfo ast;
} SDL_SHADER_AstGeneric;

typedef struct SDL_SHADER_AstExpression
{
    SDL_SHADER_AstNodeInfo ast;
    const SDL_SHADER_AstDataType *datatype;
} SDL_SHADER_AstExpression;

typedef struct SDL_SHADER_AstArguments
{
    SDL_SHADER_AstNodeInfo ast;  /* Always SDL_SHADER_AST_ARGUMENTS */
    SDL_SHADER_AstExpression *argument;
    struct SDL_SHADER_AstArguments *next;
} SDL_SHADER_AstArguments;

typedef struct SDL_SHADER_AstExpressionUnary
{
    SDL_SHADER_AstNodeInfo ast;
    const SDL_SHADER_AstDataType *datatype;
    SDL_SHADER_AstExpression *operand;
} SDL_SHADER_AstExpressionUnary;

typedef struct SDL_SHADER_AstExpressionBinary
{
    SDL_SHADER_AstNodeInfo ast;
    const SDL_SHADER_AstDataType *datatype;
    SDL_SHADER_AstExpression *left;
    SDL_SHADER_AstExpression *right;
} SDL_SHADER_AstExpressionBinary;

typedef struct SDL_SHADER_AstExpressionTernary
{
    SDL_SHADER_AstNodeInfo ast;
    const SDL_SHADER_AstDataType *datatype;
    SDL_SHADER_AstExpression *left;
    SDL_SHADER_AstExpression *center;
    SDL_SHADER_AstExpression *right;
} SDL_SHADER_AstExpressionTernary;

/* Identifier indexes aren't available until semantic analysis phase completes.
 *  It provides a unique id for this identifier's variable.
 *  It will be negative for global scope, positive for function scope
 *  (global values are globally unique, function values are only
 *  unique within the scope of the given function). There's a different
 *  set of indices if this identifier is a function (positive for
 *  user-defined functions, negative for intrinsics).
 *  May be zero for various reasons (unknown identifier, etc).
 */
typedef struct SDL_SHADER_AstExpressionIdentifier
{
    SDL_SHADER_AstNodeInfo ast;  /* Always SDL_SHADER_AST_OP_IDENTIFIER */
    const SDL_SHADER_AstDataType *datatype;
    const char *identifier;
    int index;
} SDL_SHADER_AstExpressionIdentifier;

typedef struct SDL_SHADER_AstExpressionIntLiteral
{
    SDL_SHADER_AstNodeInfo ast;  /* Always SDL_SHADER_AST_OP_INT_LITERAL */
    const SDL_SHADER_AstDataType *datatype;  /* always AST_DATATYPE_INT */
    int value;
} SDL_SHADER_AstExpressionIntLiteral;

typedef struct SDL_SHADER_AstExpressionFloatLiteral
{
    SDL_SHADER_AstNodeInfo ast;  /* Always SDL_SHADER_AST_OP_FLOAT_LITERAL */
    const SDL_SHADER_AstDataType *datatype;  /* always AST_DATATYPE_FLOAT */
    double value;
} SDL_SHADER_AstExpressionFloatLiteral;

typedef struct SDL_SHADER_AstExpressionStringLiteral
{
    SDL_SHADER_AstNodeInfo ast;  /* Always SDL_SHADER_AST_OP_STRING_LITERAL */
    const SDL_SHADER_AstDataType *datatype;  /* always AST_DATATYPE_STRING */
    const char *string;
} SDL_SHADER_AstExpressionStringLiteral;

typedef struct SDL_SHADER_AstExpressionBooleanLiteral
{
    SDL_SHADER_AstNodeInfo ast;  /* Always SDL_SHADER_AST_OP_BOOLEAN_LITERAL */
    const SDL_SHADER_AstDataType *datatype;  /* always AST_DATATYPE_BOOL */
    int value;  /* Always 1 or 0. */
} SDL_SHADER_AstExpressionBooleanLiteral;

typedef struct SDL_SHADER_AstExpressionConstructor
{
    SDL_SHADER_AstNodeInfo ast;  /* Always SDL_SHADER_AST_OP_CONSTRUCTOR */
    const SDL_SHADER_AstDataType *datatype;
    SDL_SHADER_AstArguments *args;
} SDL_SHADER_AstExpressionConstructor;

typedef struct SDL_SHADER_AstExpressionDerefStruct
{
    SDL_SHADER_AstNodeInfo ast;  /* Always SDL_SHADER_AST_OP_DEREF_STRUCT */
    const SDL_SHADER_AstDataType *datatype;
    /* !!! FIXME:
     *  "identifier" is misnamed; this might not be an identifier at all:
     *    x = FunctionThatReturnsAStruct().SomeMember;
     */
    SDL_SHADER_AstExpression *identifier;
    const char *member;
    int isswizzle;  /* Always 1 or 0. Never set by parseAst()! */
    int member_index;  /* Never set by parseAst()! */
} SDL_SHADER_AstExpressionDerefStruct;

typedef struct SDL_SHADER_AstExpressionCallFunction
{
    SDL_SHADER_AstNodeInfo ast;  /* Always SDL_SHADER_AST_OP_CALLFUNC */
    const SDL_SHADER_AstDataType *datatype;
    SDL_SHADER_AstExpressionIdentifier *identifier;
    SDL_SHADER_AstArguments *args;
} SDL_SHADER_AstExpressionCallFunction;

typedef struct SDL_SHADER_AstExpressionCast
{
    SDL_SHADER_AstNodeInfo ast;  /* Always SDL_SHADER_AST_OP_CAST */
    const SDL_SHADER_AstDataType *datatype;
    SDL_SHADER_AstExpression *operand;
} SDL_SHADER_AstExpressionCast;

typedef struct SDL_SHADER_AstCompilationUnit
{
    SDL_SHADER_AstNodeInfo ast;
    struct SDL_SHADER_AstCompilationUnit *next;
} SDL_SHADER_AstCompilationUnit;

typedef enum SDL_SHADER_AstFunctionStorageClass
{
    SDL_SHADER_AST_FNSTORECLS_NONE,
    SDL_SHADER_AST_FNSTORECLS_INLINE
} SDL_SHADER_AstFunctionStorageClass;

typedef enum SDL_SHADER_AstInputModifier
{
    SDL_SHADER_AST_INPUTMOD_NONE,
    SDL_SHADER_AST_INPUTMOD_IN,
    SDL_SHADER_AST_INPUTMOD_OUT,
    SDL_SHADER_AST_INPUTMOD_INOUT,
    SDL_SHADER_AST_INPUTMOD_UNIFORM
} SDL_SHADER_AstInputModifier;

typedef enum SDL_SHADER_AstInterpolationModifier
{
    SDL_SHADER_AST_INTERPMOD_NONE,
    SDL_SHADER_AST_INTERPMOD_LINEAR,
    SDL_SHADER_AST_INTERPMOD_CENTROID,
    SDL_SHADER_AST_INTERPMOD_NOINTERPOLATION,
    SDL_SHADER_AST_INTERPMOD_NOPERSPECTIVE,
    SDL_SHADER_AST_INTERPMOD_SAMPLE
} SDL_SHADER_AstInterpolationModifier;

typedef struct SDL_SHADER_AstFunctionParameters
{
    SDL_SHADER_AstNodeInfo ast;
    const SDL_SHADER_AstDataType *datatype;
    SDL_SHADER_AstInputModifier input_modifier;
    const char *identifier;
    const char *semantic;
    SDL_SHADER_AstInterpolationModifier interpolation_modifier;
    SDL_SHADER_AstExpression *initializer;
    struct SDL_SHADER_AstFunctionParameters *next;
} SDL_SHADER_AstFunctionParameters;

typedef struct SDL_SHADER_AstFunctionSignature
{
    SDL_SHADER_AstNodeInfo ast;
    const SDL_SHADER_AstDataType *datatype;
    const char *identifier;
    SDL_SHADER_AstFunctionParameters *params;
    SDL_SHADER_AstFunctionStorageClass storage_class;
    const char *semantic;
} SDL_SHADER_AstFunctionSignature;

typedef struct SDL_SHADER_AstScalarOrArray
{
    SDL_SHADER_AstNodeInfo ast;
    const char *identifier;
    int isarray;  /* boolean: 1 or 0 */
    SDL_SHADER_AstExpression *dimension;
} SDL_SHADER_AstScalarOrArray;

typedef struct SDL_SHADER_AstAnnotations
{
    SDL_SHADER_AstNodeInfo ast;
    const SDL_SHADER_AstDataType *datatype;
    SDL_SHADER_AstExpression *initializer;
    struct SDL_SHADER_AstAnnotations *next;
} SDL_SHADER_AstAnnotations;

typedef struct SDL_SHADER_AstPackOffset
{
    SDL_SHADER_AstNodeInfo ast;
    const char *ident1;   /* !!! FIXME: rename this. */
    const char *ident2;
} SDL_SHADER_AstPackOffset;

typedef struct SDL_SHADER_AstVariableLowLevel
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstPackOffset *packoffset;
    const char *register_name;
} SDL_SHADER_AstVariableLowLevel;

typedef struct SDL_SHADER_AstStructMembers
{
    SDL_SHADER_AstNodeInfo ast;
    const SDL_SHADER_AstDataType *datatype;
    const char *semantic;
    SDL_SHADER_AstScalarOrArray *details;
    SDL_SHADER_AstInterpolationModifier interpolation_mod;
    struct SDL_SHADER_AstStructMembers *next;
} SDL_SHADER_AstStructMembers;

typedef struct SDL_SHADER_AstStructDeclaration
{
    SDL_SHADER_AstNodeInfo ast;
    const SDL_SHADER_AstDataType *datatype;
    const char *name;
    SDL_SHADER_AstStructMembers *members;
} SDL_SHADER_AstStructDeclaration;

typedef struct SDL_SHADER_AstVariableDeclaration
{
    SDL_SHADER_AstNodeInfo ast;
    int attributes;
    const SDL_SHADER_AstDataType *datatype;
    SDL_SHADER_AstStructDeclaration *anonymous_datatype;
    SDL_SHADER_AstScalarOrArray *details;
    const char *semantic;
    SDL_SHADER_AstAnnotations *annotations;
    SDL_SHADER_AstExpression *initializer;
    SDL_SHADER_AstVariableLowLevel *lowlevel;
    struct SDL_SHADER_AstVariableDeclaration *next;
} SDL_SHADER_AstVariableDeclaration;

typedef struct SDL_SHADER_AstStatement
{
    SDL_SHADER_AstNodeInfo ast;
    struct SDL_SHADER_AstStatement *next;
} SDL_SHADER_AstStatement;

typedef SDL_SHADER_AstStatement SDL_SHADER_AstEmptyStatement;
typedef SDL_SHADER_AstStatement SDL_SHADER_AstBreakStatement;
typedef SDL_SHADER_AstStatement SDL_SHADER_AstContinueStatement;
typedef SDL_SHADER_AstStatement SDL_SHADER_AstDiscardStatement;

/* something enclosed in "{}" braces. */
typedef struct SDL_SHADER_AstBlockStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstStatement *statements;  /* list of child statements. */
} SDL_SHADER_AstBlockStatement;

typedef struct SDL_SHADER_AstReturnStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstExpression *expr;
} SDL_SHADER_AstReturnStatement;

typedef struct SDL_SHADER_AstExpressionStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstExpression *expr;
} SDL_SHADER_AstExpressionStatement;

typedef struct SDL_SHADER_AstIfStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    int attributes;
    SDL_SHADER_AstExpression *expr;
    SDL_SHADER_AstStatement *statement;
    SDL_SHADER_AstStatement *else_statement;
} SDL_SHADER_AstIfStatement;

typedef struct SDL_SHADER_AstSwitchCases
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstExpression *expr;
    SDL_SHADER_AstStatement *statement;
    struct SDL_SHADER_AstSwitchCases *next;
} SDL_SHADER_AstSwitchCases;

typedef struct SDL_SHADER_AstSwitchStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    int attributes;
    SDL_SHADER_AstExpression *expr;
    SDL_SHADER_AstSwitchCases *cases;
} SDL_SHADER_AstSwitchStatement;

typedef struct SDL_SHADER_AstWhileStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    int unroll;  /* # times to unroll, 0 to loop, < 0 == compiler's choice. */
    SDL_SHADER_AstExpression *expr;
    SDL_SHADER_AstStatement *statement;
} SDL_SHADER_AstWhileStatement;

typedef SDL_SHADER_AstWhileStatement SDL_SHADER_AstDoStatement;

typedef struct SDL_SHADER_AstForStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    int unroll;  /* # times to unroll, 0 to loop, < 0 == compiler's choice. */
    SDL_SHADER_AstVariableDeclaration *var_decl;  /* either this ... */
    SDL_SHADER_AstExpression *initializer;        /*  ... or this will used. */
    SDL_SHADER_AstExpression *looptest;
    SDL_SHADER_AstExpression *counter;
    SDL_SHADER_AstStatement *statement;
} SDL_SHADER_AstForStatement;

typedef struct SDL_SHADER_AstTypedef
{
    SDL_SHADER_AstNodeInfo ast;
    const SDL_SHADER_AstDataType *datatype;
    int isconst;  /* boolean: 1 or 0 */
    SDL_SHADER_AstScalarOrArray *details;
} SDL_SHADER_AstTypedef;

typedef struct SDL_SHADER_AstTypedefStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstTypedef *type_info;
} SDL_SHADER_AstTypedefStatement;

typedef struct SDL_SHADER_AstVarDeclStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstVariableDeclaration *declaration;
} SDL_SHADER_AstVarDeclStatement;

typedef struct SDL_SHADER_AstStructStatement
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstStatement *next;
    SDL_SHADER_AstStructDeclaration *struct_info;
} SDL_SHADER_AstStructStatement;

typedef struct SDL_SHADER_AstCompilationUnitFunction
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstCompilationUnit *next;
    SDL_SHADER_AstFunctionSignature *declaration;
    SDL_SHADER_AstStatement *definition;
    int index;  /* unique id. Will be 0 until semantic analysis runs. */
} SDL_SHADER_AstCompilationUnitFunction;

typedef struct SDL_SHADER_AstCompilationUnitTypedef
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstCompilationUnit *next;
    SDL_SHADER_AstTypedef *type_info;
} SDL_SHADER_AstCompilationUnitTypedef;

typedef struct SDL_SHADER_AstCompilationUnitStruct
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstCompilationUnit *next;
    SDL_SHADER_AstStructDeclaration *struct_info;
} SDL_SHADER_AstCompilationUnitStruct;

typedef struct SDL_SHADER_AstCompilationUnitVariable
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstCompilationUnit *next;
    SDL_SHADER_AstVariableDeclaration *declaration;
} SDL_SHADER_AstCompilationUnitVariable;


/* this is way cleaner than all the nasty typecasting. */
typedef union SDL_SHADER_AstNode
{
    SDL_SHADER_AstNodeInfo ast;
    SDL_SHADER_AstGeneric generic;
    SDL_SHADER_AstExpression expression;
    SDL_SHADER_AstArguments arguments;
    SDL_SHADER_AstExpressionUnary unary;
    SDL_SHADER_AstExpressionBinary binary;
    SDL_SHADER_AstExpressionTernary ternary;
    SDL_SHADER_AstExpressionIdentifier identifier;
    SDL_SHADER_AstExpressionIntLiteral intliteral;
    SDL_SHADER_AstExpressionFloatLiteral floatliteral;
    SDL_SHADER_AstExpressionStringLiteral stringliteral;
    SDL_SHADER_AstExpressionBooleanLiteral boolliteral;
    SDL_SHADER_AstExpressionConstructor constructor;
    SDL_SHADER_AstExpressionDerefStruct derefstruct;
    SDL_SHADER_AstExpressionCallFunction callfunc;
    SDL_SHADER_AstExpressionCast cast;
    SDL_SHADER_AstCompilationUnit compunit;
    SDL_SHADER_AstFunctionParameters params;
    SDL_SHADER_AstFunctionSignature funcsig;
    SDL_SHADER_AstScalarOrArray soa;
    SDL_SHADER_AstAnnotations annotations;
    SDL_SHADER_AstPackOffset packoffset;
    SDL_SHADER_AstVariableLowLevel varlowlevel;
    SDL_SHADER_AstStructMembers structmembers;
    SDL_SHADER_AstStructDeclaration structdecl;
    SDL_SHADER_AstVariableDeclaration vardecl;
    SDL_SHADER_AstStatement stmt;
    SDL_SHADER_AstEmptyStatement emptystmt;
    SDL_SHADER_AstBreakStatement breakstmt;
    SDL_SHADER_AstContinueStatement contstmt;
    SDL_SHADER_AstDiscardStatement discardstmt;
    SDL_SHADER_AstBlockStatement blockstmt;
    SDL_SHADER_AstReturnStatement returnstmt;
    SDL_SHADER_AstExpressionStatement exprstmt;
    SDL_SHADER_AstIfStatement ifstmt;
    SDL_SHADER_AstSwitchCases cases;
    SDL_SHADER_AstSwitchStatement switchstmt;
    SDL_SHADER_AstWhileStatement whilestmt;
    SDL_SHADER_AstDoStatement dostmt;
    SDL_SHADER_AstForStatement forstmt;
    SDL_SHADER_AstTypedef typdef;
    SDL_SHADER_AstTypedefStatement typedefstmt;
    SDL_SHADER_AstVarDeclStatement vardeclstmt;
    SDL_SHADER_AstStructStatement structstmt;
    SDL_SHADER_AstCompilationUnitFunction funcunit;
    SDL_SHADER_AstCompilationUnitTypedef typedefunit;
    SDL_SHADER_AstCompilationUnitStruct structunit;
    SDL_SHADER_AstCompilationUnitVariable varunit;
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
    int error_count;

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
     *  CompilationUnits are always the top of the tree (functions, typedefs,
     *  global variables, etc). Will be NULL on error.
     */
    const SDL_SHADER_AstNode *ast;

    /*
     * This is the malloc implementation you passed to SDL_SHADER_parse().
     */
    SDL_SHADER_Malloc malloc;

    /*
     * This is the free implementation you passed to SDL_SHADER_parse().
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
 *  SDL_SHADER_freeCompileData() to deallocate resources.
 *
 * This function will never return NULL, even if the system is completely
 *  out of memory upon entry (in which case, this function returns a static
 *  SDL_SHADER_AstData object, which is still safe to pass to
 *  SDL_SHADER_freeAstData()).
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
extern DECLSPEC const SDL_SHADER_AstData * SDLCALL SDL_SHADER_ParseAst(const char *srcprofile,
                                    const char *filename, const char *source,
                                    size_t sourcelen,
                                    const char **system_include_paths,
                                    size_t system_include_path_count,
                                    const char **local_include_paths,
                                    size_t local_include_path_count,
                                    SDL_SHADER_IncludeOpen include_open,
                                    SDL_SHADER_IncludeClose include_close,
                                    const SDL_SHADER_PreprocessorDefine *defines,
                                    size_t define_count,
                                    SDL_SHADER_Malloc m, SDL_SHADER_Free f, void *d);



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

