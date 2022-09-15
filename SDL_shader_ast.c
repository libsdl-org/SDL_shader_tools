/**
 * SDL_shader_tools; tools for SDL GPU shader support.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#define __SDL_SHADER_INTERNAL__ 1
#include "SDL_shader_internal.h"

#if DEBUG_COMPILER_PARSER
#define LEMON_SUPPORT_TRACING 1
#endif


/* These functions are mostly for construction and cleanup of nodes in the
   parse tree. Mostly this is simple allocation and initialization, so we
   can do as little in the lemon code as possible, and then sort it all out
   afterwards. */

#define NEW_AST_NODE(retval, cls, typ) \
    cls *retval = (cls *) Malloc(ctx, sizeof (cls)); \
    do { \
        if (retval == NULL) { return NULL; } \
        retval->ast.type = typ; \
        retval->ast.filename = ctx->filename; \
        retval->ast.line = ctx->position; \
        retval->ast.dt = NULL; \
    } while (0)

#define DELETE_AST_NODE(cls) do { \
    if (!cls) return; \
} while (0)

#define NEW_AST_LIST(retval, cls, first) \
    cls *retval = (cls *) Malloc(ctx, sizeof (cls)); \
    do { \
        if (retval == NULL) { return NULL; } \
        retval->head = retval->tail = first; \
    } while (0)

#define DELETE_AST_LIST(list, listedcls, deletefn) do { \
    listedcls *i; \
    listedcls *next; \
    if (!list) return; \
    for (i = list->head; i != NULL; i = next) { \
        next = i->next; \
        i->next = NULL; \
        deletefn(ctx, i); \
    } \
    Free(ctx, list); \
} while (0)

#define NEW_AST_STATEMENT_NODE(retval, cls, typ) \
    NEW_AST_NODE(retval, cls, typ); \
    retval->next = NULL;

#define DELETE_AST_STATEMENT_NODE(stmt) do { \
    DELETE_AST_NODE(stmt); \
    delete_statement(ctx, stmt->next); \
} while (0)


typedef union TokenData
{
    Sint64 i64;
    double dbl;
    const char *string;
} TokenData;


// these functions create and delete AST nodes, moving the work out of the lemon parser code.

static void delete_expression(Context *ctx, SDL_SHADER_AstExpression *expr);
static void delete_statement(Context *ctx, SDL_SHADER_AstStatement *stmt);
static void delete_translation_unit(Context *ctx, SDL_SHADER_AstTranslationUnit *unit);

static SDL_SHADER_AstAtAttribute *new_at_attribute(Context *ctx, const char *name, const Sint64 *argument)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstAtAttribute, SDL_SHADER_AST_AT_ATTRIBUTE);
    retval->name = name;  /* strcache'd */
    if (argument != NULL) {
        retval->has_argument = SDL_TRUE;
        retval->argument = *argument;
    } else {
        retval->has_argument = SDL_FALSE;
        retval->argument = 0;
    }
    return retval;
}

static void delete_at_attribute(Context *ctx, SDL_SHADER_AstAtAttribute *atattr)
{
    DELETE_AST_NODE(atattr);
    Free(ctx, atattr);
}

static SDL_SHADER_AstExpression *new_identifier_expression(Context *ctx, const char *name)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstIdentifierExpression, SDL_SHADER_AST_OP_IDENTIFIER);
    retval->name = name;  /* strcache'd */
    return (SDL_SHADER_AstExpression *) retval;
}

static void delete_identifier_expression(Context *ctx, SDL_SHADER_AstIdentifierExpression *expr)
{
    DELETE_AST_NODE(expr);
    Free(ctx, expr);
}

static SDL_SHADER_AstExpression *new_int_expression(Context *ctx, Sint64 value)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstIntLiteralExpression, SDL_SHADER_AST_OP_INT_LITERAL);
    retval->value = value;
    return (SDL_SHADER_AstExpression *) retval;
}

static void delete_int_expression(Context *ctx, SDL_SHADER_AstIntLiteralExpression *expr)
{
    DELETE_AST_NODE(expr);
    Free(ctx, expr);
}

static SDL_SHADER_AstExpression *new_float_expression(Context *ctx, double value)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstFloatLiteralExpression, SDL_SHADER_AST_OP_FLOAT_LITERAL);
    retval->value = value;
    return (SDL_SHADER_AstExpression *) retval;
}

static void delete_float_expression(Context *ctx, SDL_SHADER_AstFloatLiteralExpression *expr)
{
    DELETE_AST_NODE(expr);
    Free(ctx, expr);
}

static SDL_SHADER_AstExpression *new_bool_expression(Context *ctx, int value)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstBooleanLiteralExpression, SDL_SHADER_AST_OP_BOOLEAN_LITERAL);
    retval->value = value ? SDL_TRUE : SDL_FALSE;
    return (SDL_SHADER_AstExpression *) retval;
}

static void delete_bool_expression(Context *ctx, SDL_SHADER_AstBooleanLiteralExpression *expr)
{
    DELETE_AST_NODE(expr);
    Free(ctx, expr);
}

static SDL_SHADER_AstArgument *new_argument(Context *ctx, SDL_SHADER_AstExpression *arg)
{
    SDL_SHADER_AstArgument *retval = (SDL_SHADER_AstArgument *) Malloc(ctx, sizeof (SDL_SHADER_AstArgument));
    if (retval) {
        retval->arg = arg;
        retval->next = NULL;
    }
    return retval;
}

static void delete_argument(Context *ctx, SDL_SHADER_AstArgument *arg)
{
    if (arg) {
        delete_expression(ctx, arg->arg);
        delete_argument(ctx, arg->next);
        Free(ctx, arg);
    }
}

static SDL_SHADER_AstArguments *new_arguments(Context *ctx, SDL_SHADER_AstArgument *first)
{
    NEW_AST_LIST(retval, SDL_SHADER_AstArguments, first);
    return retval;
}

static void delete_arguments(Context *ctx, SDL_SHADER_AstArguments *arguments)
{
    DELETE_AST_LIST(arguments, SDL_SHADER_AstArgument, delete_argument);
}

static SDL_SHADER_AstExpression *new_fncall_expression(Context *ctx, const char *fnname, SDL_SHADER_AstArguments *arguments)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstFunctionCallExpression, SDL_SHADER_AST_OP_CALLFUNC);
    retval->fnname = fnname;  /* strcache'd */
    retval->arguments = arguments;
    retval->fn = NULL;
    return (SDL_SHADER_AstExpression *) retval;
}

static void delete_fncall_expression(Context *ctx, SDL_SHADER_AstFunctionCallExpression *expr)
{
    DELETE_AST_NODE(expr);
    delete_arguments(ctx, expr->arguments);
    Free(ctx, expr);
}

static SDL_SHADER_AstExpression *new_unary_expression(Context *ctx, SDL_SHADER_AstNodeType asttype, SDL_SHADER_AstExpression *operand)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstUnaryExpression, asttype);
    retval->operand = operand;
    return (SDL_SHADER_AstExpression *) retval;
}

static void delete_unary_expression(Context *ctx, SDL_SHADER_AstUnaryExpression *expr)
{
    DELETE_AST_NODE(expr);
    delete_expression(ctx, expr->operand);
    Free(ctx, expr);
}

static SDL_SHADER_AstExpression *new_unaryminus_expression(Context *ctx, SDL_SHADER_AstExpression *operand) { return new_unary_expression(ctx, SDL_SHADER_AST_OP_NEGATE, operand); }
static SDL_SHADER_AstExpression *new_unaryplus_expression(Context *ctx, SDL_SHADER_AstExpression *operand) { return new_unary_expression(ctx, SDL_SHADER_AST_OP_POSITIVE, operand); }
static SDL_SHADER_AstExpression *new_unarycompl_expression(Context *ctx, SDL_SHADER_AstExpression *operand) { return new_unary_expression(ctx, SDL_SHADER_AST_OP_COMPLEMENT, operand); }
static SDL_SHADER_AstExpression *new_unarynot_expression(Context *ctx, SDL_SHADER_AstExpression *operand) { return new_unary_expression(ctx, SDL_SHADER_AST_OP_NOT, operand); }
static SDL_SHADER_AstExpression *new_parentheses_expression(Context *ctx, SDL_SHADER_AstExpression *operand) { return new_unary_expression(ctx, SDL_SHADER_AST_OP_PARENTHESES, operand); }

static SDL_SHADER_AstExpression *new_binary_expression(Context *ctx, SDL_SHADER_AstNodeType asttype, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstBinaryExpression, asttype);
    retval->left = left;
    retval->right = right;
    return (SDL_SHADER_AstExpression *) retval;
}

static void delete_binary_expression(Context *ctx, SDL_SHADER_AstBinaryExpression *expr)
{
    DELETE_AST_NODE(expr);
    delete_expression(ctx, expr->left);
    delete_expression(ctx, expr->right);
    Free(ctx, expr);
}

static SDL_SHADER_AstExpression *new_multiply_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_MULTIPLY, left, right); }
static SDL_SHADER_AstExpression *new_divide_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_DIVIDE, left, right); }
static SDL_SHADER_AstExpression *new_mod_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_MODULO, left, right); }
static SDL_SHADER_AstExpression *new_addition_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_ADD, left, right); }
static SDL_SHADER_AstExpression *new_subtraction_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_SUBTRACT, left, right); }
static SDL_SHADER_AstExpression *new_lshift_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_LSHIFT, left, right); }
static SDL_SHADER_AstExpression *new_rshift_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_RSHIFT, left, right); }
static SDL_SHADER_AstExpression *new_lt_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_LESSTHAN, left, right); }
static SDL_SHADER_AstExpression *new_gt_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_GREATERTHAN, left, right); }
static SDL_SHADER_AstExpression *new_leq_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_LESSTHANOREQUAL, left, right); }
static SDL_SHADER_AstExpression *new_geq_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_GREATERTHANOREQUAL, left, right); }
static SDL_SHADER_AstExpression *new_eql_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_EQUAL, left, right); }
static SDL_SHADER_AstExpression *new_neq_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_NOTEQUAL, left, right); }
static SDL_SHADER_AstExpression *new_and_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_BINARYAND, left, right); }
static SDL_SHADER_AstExpression *new_xor_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_BINARYXOR, left, right); }
static SDL_SHADER_AstExpression *new_or_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_BINARYOR, left, right); }
static SDL_SHADER_AstExpression *new_andand_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_LOGICALAND, left, right); }
static SDL_SHADER_AstExpression *new_oror_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_LOGICALOR, left, right); }
static SDL_SHADER_AstExpression *new_array_dereference_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *right) { return new_binary_expression(ctx, SDL_SHADER_AST_OP_DEREF_ARRAY, left, right); }
static SDL_SHADER_AstExpression *new_ternary_expression(Context *ctx, SDL_SHADER_AstNodeType asttype, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *center, SDL_SHADER_AstExpression *right)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstTernaryExpression, asttype);
    retval->left = left;
    retval->center = center;
    retval->right = right;
    return (SDL_SHADER_AstExpression *) retval;
}

static void delete_ternary_expression(Context *ctx, SDL_SHADER_AstTernaryExpression *expr)
{
    DELETE_AST_NODE(expr);
    delete_expression(ctx, expr->left);
    delete_expression(ctx, expr->center);
    delete_expression(ctx, expr->right);
    Free(ctx, expr);
}

static SDL_SHADER_AstExpression *new_conditional_expression(Context *ctx, SDL_SHADER_AstExpression *left, SDL_SHADER_AstExpression *center, SDL_SHADER_AstExpression *right) { return new_ternary_expression(ctx, SDL_SHADER_AST_OP_CONDITIONAL, left, center, right); }

static SDL_SHADER_AstExpression *new_struct_dereference_expression(Context *ctx, SDL_SHADER_AstExpression *expr, const char *field)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstStructDerefExpression, SDL_SHADER_AST_OP_DEREF_STRUCT);
    retval->expr = expr;
    retval->field = field;  /* strcache'd */
    return (SDL_SHADER_AstExpression *) retval;
}

static void delete_struct_dereference_expression(Context *ctx, SDL_SHADER_AstStructDerefExpression *expr)
{
    DELETE_AST_NODE(expr);
    delete_expression(ctx, expr->expr);
    Free(ctx, expr);
}

static void delete_expression(Context *ctx, SDL_SHADER_AstExpression *expr)
{
    SDL_SHADER_AstNode *ast = (SDL_SHADER_AstNode *) expr;
    if (!ast) {
        return;
    } else if (operator_is_binary(ast->ast.type)) {
        delete_binary_expression(ctx, &ast->binary);
    } else if (operator_is_unary(ast->ast.type)) {
        delete_unary_expression(ctx, &ast->unary);
    } else if (operator_is_ternary(ast->ast.type)) {
        delete_ternary_expression(ctx, &ast->ternary);
    } else {
        switch (ast->ast.type) {
            case SDL_SHADER_AST_OP_IDENTIFIER: delete_identifier_expression(ctx, &ast->identifier); return;
            case SDL_SHADER_AST_OP_INT_LITERAL: delete_int_expression(ctx, &ast->intliteral); return;
            case SDL_SHADER_AST_OP_FLOAT_LITERAL: delete_float_expression(ctx, &ast->floatliteral); return;
            case SDL_SHADER_AST_OP_BOOLEAN_LITERAL: delete_bool_expression(ctx, &ast->boolliteral); return;
            case SDL_SHADER_AST_OP_CALLFUNC: delete_fncall_expression(ctx, &ast->fncall); return;
            case SDL_SHADER_AST_OP_DEREF_STRUCT: delete_struct_dereference_expression(ctx, &ast->structderef); return;
            default: break;
        }
        SDL_assert(!"Unexpected statement type");
    }
}

static SDL_SHADER_AstStatement *new_simple_statement(Context *ctx, SDL_SHADER_AstNodeType asttype)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstSimpleStatement, asttype);
    return (SDL_SHADER_AstStatement *) retval;
}

static void delete_simple_statement(Context *ctx, SDL_SHADER_AstSimpleStatement *simple)
{
    DELETE_AST_STATEMENT_NODE(simple);
    delete_statement(ctx, simple->next);
    Free(ctx, simple);
}

static SDL_SHADER_AstStatement *new_empty_statement(Context *ctx) { return new_simple_statement(ctx, SDL_SHADER_AST_STATEMENT_EMPTY); }
static SDL_SHADER_AstStatement *new_discard_statement(Context *ctx) { return new_simple_statement(ctx, SDL_SHADER_AST_STATEMENT_DISCARD); }

static SDL_SHADER_AstStatement *new_break_statement(Context *ctx)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstBreakStatement, SDL_SHADER_AST_STATEMENT_BREAK);
    retval->parent = NULL;
    return (SDL_SHADER_AstStatement *) retval;
}

static void delete_break_statement(Context *ctx, SDL_SHADER_AstBreakStatement *brstmt)
{
    DELETE_AST_STATEMENT_NODE(brstmt);
    delete_statement(ctx, brstmt->next);
    /* don't delete parent; it's not allocated by us */
    Free(ctx, brstmt);
}

static SDL_SHADER_AstStatement *new_continue_statement(Context *ctx)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstContinueStatement, SDL_SHADER_AST_STATEMENT_CONTINUE);
    retval->parent = NULL;
    return (SDL_SHADER_AstStatement *) retval;
}

static void delete_continue_statement(Context *ctx, SDL_SHADER_AstContinueStatement *contstmt)
{
    DELETE_AST_STATEMENT_NODE(contstmt);
    delete_statement(ctx, contstmt->next);
    /* don't delete parent; it's not allocated by us */
    Free(ctx, contstmt);
}

static SDL_SHADER_AstArrayBounds *new_array_bounds(Context *ctx, SDL_SHADER_AstExpression *size)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstArrayBounds, SDL_SHADER_AST_ARRAY_BOUNDS);
    retval->size = size;
    retval->next = NULL;
    return retval;
}

static void delete_array_bounds(Context *ctx, SDL_SHADER_AstArrayBounds *ab)
{
    DELETE_AST_NODE(ab);
    delete_expression(ctx, ab->size);
    delete_array_bounds(ctx, ab->next);
    Free(ctx, ab);
}

static SDL_SHADER_AstArrayBoundsList *new_array_bounds_list(Context *ctx, SDL_SHADER_AstArrayBounds *first)
{
    NEW_AST_LIST(retval, SDL_SHADER_AstArrayBoundsList, first);
    return retval;
}

static void delete_array_bounds_list(Context *ctx, SDL_SHADER_AstArrayBoundsList *abl)
{
    DELETE_AST_LIST(abl, SDL_SHADER_AstArrayBounds, delete_array_bounds);
}

static SDL_SHADER_AstVarDeclaration *new_var_declaration(Context *ctx, int c_style, const char *datatype_name, const char *name, SDL_SHADER_AstArrayBoundsList *arraybounds, SDL_SHADER_AstAtAttribute *attribute)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstVarDeclaration, SDL_SHADER_AST_VARIABLE_DECLARATION);
    retval->c_style = c_style ? SDL_TRUE : SDL_FALSE;
    retval->datatype_name = datatype_name;  /* strcache'd */
    retval->name = name; /* strcache'd */
    retval->arraybounds = arraybounds;
    retval->attribute = attribute;
    return retval;
}

static void delete_var_declaration(Context *ctx, SDL_SHADER_AstVarDeclaration *vardecl)
{
    DELETE_AST_NODE(vardecl);
    delete_array_bounds_list(ctx, vardecl->arraybounds);
    delete_at_attribute(ctx, vardecl->attribute);
    Free(ctx, vardecl);
}

static SDL_SHADER_AstStatement *new_var_declaration_statement(Context *ctx, SDL_SHADER_AstVarDeclaration *vardecl, SDL_SHADER_AstExpression *initializer)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstVarDeclStatement, SDL_SHADER_AST_STATEMENT_VARDECL);
    retval->vardecl = vardecl;
    retval->initializer = initializer;
    return (SDL_SHADER_AstStatement *) retval;
}

static void delete_var_declaration_statement(Context *ctx, SDL_SHADER_AstVarDeclStatement *vdstmt)
{
    DELETE_AST_STATEMENT_NODE(vdstmt);
    delete_var_declaration(ctx, vdstmt->vardecl);
    delete_expression(ctx, vdstmt->initializer);
    delete_statement(ctx, vdstmt->next);
    Free(ctx, vdstmt);
}

static SDL_SHADER_AstStatementBlock *new_statement_block(Context *ctx, SDL_SHADER_AstStatement *first)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstStatementBlock, SDL_SHADER_AST_STATEMENT_BLOCK);
    retval->head = retval->tail = first;
    return retval;
}

static void delete_statement_block(Context *ctx, SDL_SHADER_AstStatementBlock *stmtblock)
{
    DELETE_AST_STATEMENT_NODE(stmtblock);
    DELETE_AST_LIST(stmtblock, SDL_SHADER_AstStatement, delete_statement);  /* this happens to work, but watch out if that macro changes! */
}

static SDL_SHADER_AstStatement *new_do_statement(Context *ctx, SDL_SHADER_AstStatementBlock *code, SDL_SHADER_AstExpression *condition)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstDoStatement, SDL_SHADER_AST_STATEMENT_DO);
    retval->code = code;
    retval->condition = condition;
    return (SDL_SHADER_AstStatement *) retval;
}

static void delete_do_statement(Context *ctx, SDL_SHADER_AstDoStatement *dostmt)
{
    DELETE_AST_STATEMENT_NODE(dostmt);
    delete_statement_block(ctx, dostmt->code);
    delete_expression(ctx, dostmt->condition);
    delete_statement(ctx, dostmt->next);
    Free(ctx, dostmt);
}

static SDL_SHADER_AstStatement *new_while_statement(Context *ctx, SDL_SHADER_AstExpression *condition, SDL_SHADER_AstStatementBlock *code)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstWhileStatement, SDL_SHADER_AST_STATEMENT_WHILE);
    retval->code = code;
    retval->condition = condition;
    return (SDL_SHADER_AstStatement *) retval;
}

static void delete_while_statement(Context *ctx, SDL_SHADER_AstWhileStatement *wstmt)
{
    DELETE_AST_STATEMENT_NODE(wstmt);
    delete_statement_block(ctx, wstmt->code);
    delete_expression(ctx, wstmt->condition);
    delete_statement(ctx, wstmt->next);
    Free(ctx, wstmt);
}

static SDL_SHADER_AstForDetails *new_for_details(Context *ctx, SDL_SHADER_AstStatement *initializer, SDL_SHADER_AstExpression *condition, SDL_SHADER_AstStatement *step)
{
    SDL_SHADER_AstForDetails *retval = (SDL_SHADER_AstForDetails *) Malloc(ctx, sizeof (SDL_SHADER_AstForDetails));
    if (retval) {
        retval->initializer = initializer;
        retval->condition = condition;
        retval->step = step;
    }
    return retval;
}

static void delete_for_details(Context *ctx, SDL_SHADER_AstForDetails *fordetails)
{
    if (fordetails) {
        delete_statement(ctx, fordetails->initializer);
        delete_expression(ctx, fordetails->condition);
        delete_statement(ctx, fordetails->step);
        Free(ctx, fordetails);
    }
}

static SDL_SHADER_AstStatement *new_for_statement(Context *ctx, SDL_SHADER_AstForDetails *details, SDL_SHADER_AstStatementBlock *code)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstForStatement, SDL_SHADER_AST_STATEMENT_FOR);
    retval->details = details;
    retval->code = code;
    return (SDL_SHADER_AstStatement *) retval;
}

static void delete_for_statement(Context *ctx, SDL_SHADER_AstForStatement *forstmt)
{
    DELETE_AST_STATEMENT_NODE(forstmt);
    delete_for_details(ctx, forstmt->details);
    delete_statement_block(ctx, forstmt->code);
    delete_statement(ctx, forstmt->next);
    Free(ctx, forstmt);
}

static SDL_SHADER_AstStatement *new_if_statement(Context *ctx, SDL_SHADER_AstExpression *condition, SDL_SHADER_AstStatementBlock *code, SDL_SHADER_AstStatementBlock *else_code)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstIfStatement, SDL_SHADER_AST_STATEMENT_IF);
    retval->condition = condition;
    retval->code = code;
    retval->else_code = else_code;
    return (SDL_SHADER_AstStatement *) retval;
}

static void delete_if_statement(Context *ctx, SDL_SHADER_AstIfStatement *ifstmt)
{
    DELETE_AST_STATEMENT_NODE(ifstmt);
    delete_expression(ctx, ifstmt->condition);
    delete_statement_block(ctx, ifstmt->code);
    delete_statement_block(ctx, ifstmt->else_code);
    delete_statement(ctx, ifstmt->next);
    Free(ctx, ifstmt);
}

static SDL_SHADER_AstStatement *new_return_statement(Context *ctx, SDL_SHADER_AstExpression *value)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstReturnStatement, SDL_SHADER_AST_STATEMENT_RETURN);
    retval->value = value;
    return (SDL_SHADER_AstStatement *) retval;
}

static void delete_return_statement(Context *ctx, SDL_SHADER_AstReturnStatement *rtstmt)
{
    DELETE_AST_STATEMENT_NODE(rtstmt);
    delete_expression(ctx, rtstmt->value);
    delete_statement(ctx, rtstmt->next);
    Free(ctx, rtstmt);
}

static SDL_SHADER_AstAssignment *new_assignment(Context *ctx, SDL_SHADER_AstExpression *expr)
{
    SDL_SHADER_AstAssignment *retval = (SDL_SHADER_AstAssignment *) Malloc(ctx, sizeof (SDL_SHADER_AstAssignment));
    if (retval) {
        retval->expr = expr;
        retval->next = NULL;
    }
    return retval;
}

static void delete_assignment(Context *ctx, SDL_SHADER_AstAssignment *assignment)
{
    if (assignment) {
        delete_expression(ctx, assignment->expr);
        delete_assignment(ctx, assignment->next);
        Free(ctx, assignment);
    }
}

static SDL_SHADER_AstAssignments *new_assignments(Context *ctx, SDL_SHADER_AstAssignment *first)
{
    NEW_AST_LIST(retval, SDL_SHADER_AstAssignments, first);
    return retval;
}

static void delete_assignments(Context *ctx, SDL_SHADER_AstAssignments *assignments)
{
    DELETE_AST_LIST(assignments, SDL_SHADER_AstAssignment, delete_assignment);
}

static SDL_SHADER_AstStatement *new_assignment_statement(Context *ctx, SDL_SHADER_AstAssignments *assignments, SDL_SHADER_AstExpression *value)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstAssignStatement, SDL_SHADER_AST_STATEMENT_ASSIGNMENT);
    retval->assignments = assignments;
    retval->value = value;
    return (SDL_SHADER_AstStatement *) retval;
}

static void delete_assignment_statement(Context *ctx, SDL_SHADER_AstAssignStatement *asstmt)
{
    DELETE_AST_STATEMENT_NODE(asstmt);
    delete_assignments(ctx, asstmt->assignments);
    delete_expression(ctx, asstmt->value);
    delete_statement(ctx, asstmt->next);
    Free(ctx, asstmt);
}

static SDL_SHADER_AstStatement *new_compound_assignment_statement(Context *ctx, SDL_SHADER_AstExpression *assignment, SDL_SHADER_AstNodeType asttype, SDL_SHADER_AstExpression *value)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstCompoundAssignStatement, asttype);
    retval->assignment = assignment;
    retval->value = value;
    return (SDL_SHADER_AstStatement *) retval;
}

static void delete_compound_assignment_statement(Context *ctx, SDL_SHADER_AstCompoundAssignStatement *asstmt)
{
    DELETE_AST_STATEMENT_NODE(asstmt);
    delete_expression(ctx, asstmt->assignment);
    delete_expression(ctx, asstmt->value);
    delete_statement(ctx, asstmt->next);
    Free(ctx, asstmt);
}

static SDL_SHADER_AstStatement *new_increment_statement(Context *ctx, const SDL_SHADER_AstNodeType asttype, SDL_SHADER_AstExpression *assignment)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstIncrementStatement, asttype);
    retval->assignment = assignment;
    return (SDL_SHADER_AstStatement *) retval;
}

static void delete_increment_statement(Context *ctx, SDL_SHADER_AstIncrementStatement *incstmt)
{
    DELETE_AST_STATEMENT_NODE(incstmt);
    delete_expression(ctx, incstmt->assignment);
    delete_statement(ctx, incstmt->next);
    Free(ctx, incstmt);
}

static SDL_SHADER_AstStatement *new_preincrement_statement(Context *ctx, SDL_SHADER_AstExpression *assignment)
{
    return new_increment_statement(ctx, SDL_SHADER_AST_STATEMENT_PREINCREMENT, assignment);
}

static SDL_SHADER_AstStatement *new_predecrement_statement(Context *ctx, SDL_SHADER_AstExpression *assignment)
{
    return new_increment_statement(ctx, SDL_SHADER_AST_STATEMENT_PREDECREMENT, assignment);
}

static SDL_SHADER_AstStatement *new_postincrement_statement(Context *ctx, SDL_SHADER_AstExpression *assignment)
{
    return new_increment_statement(ctx, SDL_SHADER_AST_STATEMENT_POSTINCREMENT, assignment);
}

static SDL_SHADER_AstStatement *new_postdecrement_statement(Context *ctx, SDL_SHADER_AstExpression *assignment)
{
    return new_increment_statement(ctx, SDL_SHADER_AST_STATEMENT_POSTDECREMENT, assignment);
}

static SDL_SHADER_AstStatement *new_fncall_statement(Context *ctx, const char *fnname, SDL_SHADER_AstArguments *arguments)
{
    NEW_AST_STATEMENT_NODE(retval, SDL_SHADER_AstFunctionCallStatement, SDL_SHADER_AST_STATEMENT_FUNCTION_CALL);
    retval->expr = (SDL_SHADER_AstFunctionCallExpression *) new_fncall_expression(ctx, fnname, arguments);
    return (SDL_SHADER_AstStatement *) retval;
}

static void delete_fncall_statement(Context *ctx, SDL_SHADER_AstFunctionCallStatement *fnstmt)
{
    DELETE_AST_STATEMENT_NODE(fnstmt);
    delete_fncall_expression(ctx, fnstmt->expr);
    delete_statement(ctx, fnstmt->next);
    Free(ctx, fnstmt);
}

static void delete_statement(Context *ctx, SDL_SHADER_AstStatement *stmt)
{
    SDL_SHADER_AstNode *ast = (SDL_SHADER_AstNode *) stmt;
    if (!ast) { return; }
    switch (ast->ast.type) {
        case SDL_SHADER_AST_STATEMENT_EMPTY: delete_simple_statement(ctx, &ast->simplestmt); return;
        case SDL_SHADER_AST_STATEMENT_BREAK: delete_break_statement(ctx, &ast->breakstmt); return;
        case SDL_SHADER_AST_STATEMENT_CONTINUE: delete_continue_statement(ctx, &ast->contstmt); return;
        case SDL_SHADER_AST_STATEMENT_DISCARD: delete_simple_statement(ctx, &ast->simplestmt); return;
        case SDL_SHADER_AST_STATEMENT_VARDECL: delete_var_declaration_statement(ctx, &ast->vardeclstmt); return;
        case SDL_SHADER_AST_STATEMENT_DO: delete_do_statement(ctx, &ast->dostmt); return;
        case SDL_SHADER_AST_STATEMENT_WHILE: delete_while_statement(ctx, &ast->whilestmt); return;
        case SDL_SHADER_AST_STATEMENT_FOR: delete_for_statement(ctx, &ast->forstmt); return;
        case SDL_SHADER_AST_STATEMENT_IF: delete_if_statement(ctx, &ast->ifstmt); return;
        case SDL_SHADER_AST_STATEMENT_RETURN: delete_return_statement(ctx, &ast->returnstmt); return;
        case SDL_SHADER_AST_STATEMENT_ASSIGNMENT: delete_assignment_statement(ctx, &ast->assignstmt); return;
        case SDL_SHADER_AST_STATEMENT_FUNCTION_CALL: delete_fncall_statement(ctx, &ast->fncallstmt); return;
        case SDL_SHADER_AST_STATEMENT_BLOCK: delete_statement_block(ctx, &ast->stmtblock); return;

        case SDL_SHADER_AST_STATEMENT_PREINCREMENT:
        case SDL_SHADER_AST_STATEMENT_PREDECREMENT:
        case SDL_SHADER_AST_STATEMENT_POSTINCREMENT:
        case SDL_SHADER_AST_STATEMENT_POSTDECREMENT:
            delete_increment_statement(ctx, &ast->incrementstmt);
            return;

        case SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNMUL:
        case SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNDIV:
        case SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNMOD:
        case SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNADD:
        case SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNSUB:
        case SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNLSHIFT:
        case SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNRSHIFT:
        case SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNAND:
        case SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNXOR:
        case SDL_SHADER_AST_STATEMENT_COMPOUNDASSIGNOR:
            delete_compound_assignment_statement(ctx, &ast->compoundassignstmt);
            return;

        default: break;
    }
    SDL_assert(!"Unexpected statement type");
}

static SDL_SHADER_AstStructMember *new_struct_member(Context *ctx, SDL_SHADER_AstVarDeclaration *vardecl)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstStructMember,SDL_SHADER_AST_STRUCT_MEMBER);
    retval->vardecl = vardecl;
    retval->next = NULL;
    return retval;
}

static void delete_struct_member(Context *ctx, SDL_SHADER_AstStructMember *member)
{
    DELETE_AST_NODE(member);
    delete_var_declaration(ctx, member->vardecl);
    delete_struct_member(ctx, member->next);
    Free(ctx, member);
}

static SDL_SHADER_AstStructMembers *new_struct_members(Context *ctx, SDL_SHADER_AstStructMember *first)
{
    NEW_AST_LIST(retval, SDL_SHADER_AstStructMembers, first);
    return retval;
}

static void delete_struct_members(Context *ctx, SDL_SHADER_AstStructMembers *members)
{
    DELETE_AST_LIST(members, SDL_SHADER_AstStructMember, delete_struct_member);
}

static SDL_SHADER_AstStructDeclaration *new_struct_declaration(Context *ctx, const char *name, SDL_SHADER_AstStructMembers *members)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstStructDeclaration, SDL_SHADER_AST_STRUCT_DECLARATION);
    retval->name = name;  /* strcache'd */
    retval->members = members;
    retval->nextstruct = NULL;
    return retval;
}

static void delete_struct_declaration(Context *ctx, SDL_SHADER_AstStructDeclaration *decl)
{
    DELETE_AST_NODE(decl);
    delete_struct_members(ctx, decl->members);
    /* don't delete decl->nextstruct, it's a separate linked list and you'll delete things twice. */
    Free(ctx, decl);
}

static SDL_SHADER_AstTranslationUnit *new_struct_declaration_unit(Context *ctx, SDL_SHADER_AstStructDeclaration *decl)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstStructDeclarationUnit, SDL_SHADER_AST_TRANSUNIT_STRUCT);
    retval->next = NULL;
    retval->decl = decl;
    return (SDL_SHADER_AstTranslationUnit *) retval;
}

static void delete_struct_declaration_unit(Context *ctx, SDL_SHADER_AstStructDeclarationUnit *strunit)
{
    DELETE_AST_NODE(strunit);
    delete_struct_declaration(ctx, strunit->decl);
    delete_translation_unit(ctx, strunit->next);
    Free(ctx, strunit);
}

static SDL_SHADER_AstFunctionParam *new_function_param(Context *ctx, SDL_SHADER_AstVarDeclaration *vardecl)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstFunctionParam, SDL_SHADER_AST_FUNCTION_PARAM);
    retval->vardecl = vardecl;
    retval->next = NULL;
    return retval;
}

static void delete_function_param(Context *ctx, SDL_SHADER_AstFunctionParam *param)
{
    DELETE_AST_NODE(param);
    delete_var_declaration(ctx, param->vardecl);
    delete_function_param(ctx, param->next);
    Free(ctx, param);
}

static SDL_SHADER_AstFunctionParams *new_function_params(Context *ctx, SDL_SHADER_AstFunctionParam *first)
{
    NEW_AST_LIST(retval, SDL_SHADER_AstFunctionParams, first);
    return retval;
}

static void delete_function_params(Context *ctx, SDL_SHADER_AstFunctionParams *params)
{
    DELETE_AST_LIST(params, SDL_SHADER_AstFunctionParam, delete_function_param);
}

static SDL_SHADER_AstFunction *new_function(Context *ctx, int c_style, const char *rettype, const char *name, SDL_SHADER_AstFunctionParams *params, SDL_SHADER_AstAtAttribute *atattr, SDL_SHADER_AstStatementBlock *code)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstFunction, SDL_SHADER_AST_FUNCTION);
    retval->fntype = SDL_SHADER_AST_FNTYPE_UNKNOWN;  /* until semantic analysis */
    retval->vardecl = new_var_declaration(ctx, c_style, rettype, name, NULL, atattr);
    retval->params = params;  /* NULL==void */
    retval->code = code;
    retval->nextfn = NULL;
    return retval;
}

static void delete_function(Context *ctx, SDL_SHADER_AstFunction *fn)
{
    DELETE_AST_NODE(fn);
    delete_var_declaration(ctx, fn->vardecl);
    delete_function_params(ctx, fn->params);
    delete_statement_block(ctx, fn->code);
    /* don't delete fn->nextfn, it's a separate linked list and you'll delete things twice. */
    Free(ctx, fn);
}

static SDL_SHADER_AstTranslationUnit *new_function_unit(Context *ctx, SDL_SHADER_AstFunction *fn)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstFunctionUnit, SDL_SHADER_AST_TRANSUNIT_FUNCTION);
    retval->next = NULL;
    retval->fn = fn;
    return (SDL_SHADER_AstTranslationUnit *) retval;
}

static void delete_function_unit(Context *ctx, SDL_SHADER_AstFunctionUnit *fnunit)
{
    DELETE_AST_NODE(fnunit);
    delete_function(ctx, fnunit->fn);
    delete_translation_unit(ctx, fnunit->next);
    Free(ctx, fnunit);
}

static void delete_translation_unit(Context *ctx, SDL_SHADER_AstTranslationUnit *unit)
{
    SDL_SHADER_AstNode *ast = (SDL_SHADER_AstNode *) unit;
    if (!ast) { return; }
    switch (ast->ast.type) {
        case SDL_SHADER_AST_TRANSUNIT_FUNCTION: delete_function_unit(ctx, &ast->fnunit); return;
        case SDL_SHADER_AST_TRANSUNIT_STRUCT: delete_struct_declaration_unit(ctx, &ast->structdeclunit); return;
        default: break;
    }
    SDL_assert(!"Unexpected translation unit type");
}

static SDL_SHADER_AstTranslationUnits *new_translation_units(Context *ctx, SDL_SHADER_AstTranslationUnit *first)
{
    NEW_AST_LIST(retval, SDL_SHADER_AstTranslationUnits, first);
    return retval;
}

static void delete_translation_units(Context *ctx, SDL_SHADER_AstTranslationUnits *units)
{
    DELETE_AST_LIST(units, SDL_SHADER_AstTranslationUnit, delete_translation_unit);
}

static SDL_SHADER_AstShader *new_shader(Context *ctx, SDL_SHADER_AstTranslationUnits *units)
{
    NEW_AST_NODE(retval, SDL_SHADER_AstShader, SDL_SHADER_AST_SHADER);
    retval->units = units;
    return retval;
}

static void delete_shader(Context *ctx, SDL_SHADER_AstShader *shader)
{
    DELETE_AST_NODE(shader);
    delete_translation_units(ctx, shader->units);
    Free(ctx, shader);
}


// This is where the actual parsing happens. It's Lemon-generated!
#define __SDL_SHADER_SDLSL_COMPILER__ 1
#include "SDL_shader_parser.h"

static Sint64 strtoi64(const char *str, const size_t slen)
{
    Sint64 retval = 0;
    char buf[64];
    if (slen < sizeof (buf)) {
        char *endp = NULL;
        SDL_memcpy(buf, str, slen);
        buf[slen] = '\0';
        retval = SDL_strtoll(buf, &endp, 0);
        if (*endp != '\0') {
            SDL_assert(!"Tokenizer let a bogus int64 through...?");
            retval = 0;  /* invalid string (empty string returns zero anyhow, so that works here too). */
        }
    }
    return retval;
}

static double strtodouble(const char *str, const size_t slen)
{
    double retval = 0.0;
    char buf[64];
    if (slen < sizeof (buf)) {
        char *endp = NULL;
        SDL_memcpy(buf, str, slen);
        buf[slen] = '\0';
        retval = SDL_strtod(buf, &endp);
        if (*endp != '\0') {
            SDL_assert(!"Tokenizer let a bogus double through...?");
            retval = 0.0;  /* invalid string (empty string returns zero anyhow, so that works here too). */
        }
    }
    return retval;
}

static int convert_to_lemon_token(Context *ctx, const char *token, size_t tokenlen, const Token tokenval, TokenData *data)
{
    data->i64 = 0;

    switch (tokenval) {
        case ((Token) TOKEN_INT_LITERAL):
            data->i64 = strtoi64(token, tokenlen);
            return TOKEN_SDLSL_INT_CONSTANT;

        case ((Token) TOKEN_FLOAT_LITERAL):
            data->dbl = strtodouble(token, tokenlen);
            return TOKEN_SDLSL_FLOAT_CONSTANT;

        /* The language has no string literals atm!
        case ((Token) TOKEN_STRING_LITERAL):
            data->string = stringcache_len(ctx->strcache, token, tokenlen);
            return TOKEN_SDLSL_STRING_LITERAL;*/

        case ((Token) ','): return TOKEN_SDLSL_COMMA;
        case ((Token) '='): return TOKEN_SDLSL_ASSIGN;
        case ((Token) TOKEN_ADDASSIGN): return TOKEN_SDLSL_PLUSASSIGN;
        case ((Token) TOKEN_SUBASSIGN): return TOKEN_SDLSL_MINUSASSIGN;
        case ((Token) TOKEN_MULTASSIGN): return TOKEN_SDLSL_STARASSIGN;
        case ((Token) TOKEN_DIVASSIGN): return TOKEN_SDLSL_SLASHASSIGN;
        case ((Token) TOKEN_MODASSIGN): return TOKEN_SDLSL_PERCENTASSIGN;
        case ((Token) TOKEN_LSHIFTASSIGN): return TOKEN_SDLSL_LSHIFTASSIGN;
        case ((Token) TOKEN_RSHIFTASSIGN): return TOKEN_SDLSL_RSHIFTASSIGN;
        case ((Token) TOKEN_ANDASSIGN): return TOKEN_SDLSL_ANDASSIGN;
        case ((Token) TOKEN_ORASSIGN): return TOKEN_SDLSL_ORASSIGN;
        case ((Token) TOKEN_XORASSIGN): return TOKEN_SDLSL_XORASSIGN;
        case ((Token) '?'): return TOKEN_SDLSL_QUESTION;
        case ((Token) TOKEN_OROR): return TOKEN_SDLSL_OROR;
        case ((Token) TOKEN_ANDAND): return TOKEN_SDLSL_ANDAND;
        case ((Token) '|'): return TOKEN_SDLSL_OR;
        case ((Token) '^'): return TOKEN_SDLSL_XOR;
        case ((Token) '&'): return TOKEN_SDLSL_AND;
        case ((Token) TOKEN_EQL): return TOKEN_SDLSL_EQL;
        case ((Token) TOKEN_NEQ): return TOKEN_SDLSL_NEQ;
        case ((Token) '<'): return TOKEN_SDLSL_LT;
        case ((Token) TOKEN_LEQ): return TOKEN_SDLSL_LEQ;
        case ((Token) '>'): return TOKEN_SDLSL_GT;
        case ((Token) TOKEN_GEQ): return TOKEN_SDLSL_GEQ;
        case ((Token) TOKEN_LSHIFT): return TOKEN_SDLSL_LSHIFT;
        case ((Token) TOKEN_RSHIFT): return TOKEN_SDLSL_RSHIFT;
        case ((Token) '+'): return TOKEN_SDLSL_PLUS;
        case ((Token) '-'): return TOKEN_SDLSL_MINUS;
        case ((Token) '*'): return TOKEN_SDLSL_STAR;
        case ((Token) '/'): return TOKEN_SDLSL_SLASH;
        case ((Token) '%'): return TOKEN_SDLSL_PERCENT;
        case ((Token) '!'): return TOKEN_SDLSL_EXCLAMATION;
        case ((Token) '~'): return TOKEN_SDLSL_COMPLEMENT;
        case ((Token) TOKEN_DECREMENT): return TOKEN_SDLSL_MINUSMINUS;
        case ((Token) TOKEN_INCREMENT): return TOKEN_SDLSL_PLUSPLUS;
        case ((Token) '.'): return TOKEN_SDLSL_DOT;
        case ((Token) '['): return TOKEN_SDLSL_LBRACKET;
        case ((Token) ']'): return TOKEN_SDLSL_RBRACKET;
        case ((Token) '('): return TOKEN_SDLSL_LPAREN;
        case ((Token) ')'): return TOKEN_SDLSL_RPAREN;
        case ((Token) ':'): return TOKEN_SDLSL_COLON;
        case ((Token) ';'): return TOKEN_SDLSL_SEMICOLON;
        case ((Token) '{'): return TOKEN_SDLSL_LBRACE;
        case ((Token) '}'): return TOKEN_SDLSL_RBRACE;
        case ((Token) '@'): return TOKEN_SDLSL_AT;

        case ((Token) TOKEN_IDENTIFIER): {
            const char *str = stringcache_len(ctx->strcache, token, tokenlen);
            data->string = str;
            #define tokencmp(t) (SDL_strcmp(data->string, t) == 0)
            if (tokencmp("function")) return TOKEN_SDLSL_FUNCTION;
            if (tokencmp("var")) return TOKEN_SDLSL_VAR;
            if (tokencmp("else")) return TOKEN_SDLSL_ELSE;
            if (tokencmp("void")) return TOKEN_SDLSL_VOID;
            if (tokencmp("struct")) return TOKEN_SDLSL_STRUCT;
            if (tokencmp("break")) return TOKEN_SDLSL_BREAK;
            if (tokencmp("continue")) return TOKEN_SDLSL_CONTINUE;
            if (tokencmp("discard")) return TOKEN_SDLSL_DISCARD;
            if (tokencmp("return")) return TOKEN_SDLSL_RETURN;
            if (tokencmp("while")) return TOKEN_SDLSL_WHILE;
            if (tokencmp("for")) return TOKEN_SDLSL_FOR;
            if (tokencmp("do")) return TOKEN_SDLSL_DO;
            if (tokencmp("if")) return TOKEN_SDLSL_IF;
            if (tokencmp("true")) return TOKEN_SDLSL_TRUE;
            if (tokencmp("false")) return TOKEN_SDLSL_FALSE;
            #undef tokencmp
            return TOKEN_SDLSL_IDENTIFIER;
        }

        case TOKEN_EOI: return 0;
        default: SDL_assert(!"unexpected token from lexer"); return 0;
    }

    return 0;
}


/* parse the source code into an AST. */
static void parse_sdlsl_source(Context *ctx, const SDL_SHADER_CompilerParams *params)
{
    void *parser;
    Token tokenval;
    const char *token;
    size_t tokenlen;
    int lemon_token;
    TokenData data;

    if (!preprocessor_start(ctx, params, SDL_FALSE)) {
        SDL_assert(ctx->isfail);
        SDL_assert(ctx->out_of_memory);  /* shouldn't fail for any other reason. */
        return;
    }

    parser = ParseSDLSLAlloc(ctx->malloc, ctx->malloc_data);
    if (parser == NULL) {
        SDL_assert(ctx->isfail);
        SDL_assert(ctx->out_of_memory);  /* shouldn't fail for any other reason. */
        return;
    }

    #if DEBUG_COMPILER_PARSER
    ParseSDLSLTrace(stdout, "COMPILER: ");
    #endif

    /* Run the preprocessor/lexer/parser... */
    do {
        if (ctx->out_of_memory) { break; }  /* !!! FIXME: I just sprinkled these everywhere, just in case. */

        token = preprocessor_nexttoken(ctx, &tokenlen, &tokenval);
        if (ctx->out_of_memory) { break; }

        if ((tokenval == TOKEN_HASH) || (tokenval == TOKEN_HASHHASH)) {
            tokenval = TOKEN_BAD_CHARS;  /* just treat these as bad chars, since we don't have any pragma things atm. */
        }

        switch (tokenval) {
            case TOKEN_BAD_CHARS:
                fail(ctx, "Bad characters in source file");
                continue;

            case TOKEN_INCOMPLETE_STRING_LITERAL:
                fail(ctx, "String literal without an ending '\"'");
                continue;

            case TOKEN_INCOMPLETE_COMMENT:
                fail(ctx, "Multiline comment without an ending '*/'");
                continue;

            case TOKEN_SINGLE_COMMENT:
            case TOKEN_MULTI_COMMENT:
            case ((Token) ' '):
            case ((Token) '\n'):
                continue;  /* just ignore these. */

            default: break;
        }

        lemon_token = convert_to_lemon_token(ctx, token, tokenlen, tokenval, &data);
        if (ctx->out_of_memory) { break; }

        ParseSDLSL(parser, lemon_token, data, ctx);  /* run another iteration of the Lemon parser. */
        if (ctx->out_of_memory) { break; }
    } while (tokenval != TOKEN_EOI);

    ParseSDLSLFree(parser, ctx->free, ctx->malloc_data);
}


static const SDL_SHADER_AstData SDL_SHADER_out_of_mem_data_ast = {
    1, &SDL_SHADER_out_of_mem_error, 0, 0, 0, 0, 0, 0
};


static const SDL_SHADER_AstData *build_astdata(Context *ctx)
{
    SDL_SHADER_AstData *retval = NULL;

    if (ctx->out_of_memory) {
        return &SDL_SHADER_out_of_mem_data_ast;
    }

    retval = (SDL_SHADER_AstData *) Malloc(ctx, sizeof (SDL_SHADER_AstData));
    if (retval == NULL) {
        return &SDL_SHADER_out_of_mem_data_ast;
    }

    SDL_zerop(retval);
    retval->malloc = (ctx->malloc == SDL_SHADER_internal_malloc) ? NULL : ctx->malloc;
    retval->free = (ctx->free == SDL_SHADER_internal_free) ? NULL : ctx->free;
    retval->malloc_data = ctx->malloc_data;
    retval->error_count = errorlist_count(ctx->errors);
    retval->errors = errorlist_flatten(ctx->errors);
    if (ctx->out_of_memory) {
        Free(ctx, retval);
        return &SDL_SHADER_out_of_mem_data_ast;
    }

    if (!ctx->isfail) {
        retval->source_profile = ctx->source_profile;
        retval->shader = ctx->shader;
        retval->opaque = ctx;
    }

    return retval;
}

static void choose_src_profile(Context *ctx, const char *srcprofile)
{
    /* make sure this points to an internal static string so we don't have to free it. */

    if (!srcprofile) {
        ctx->source_profile = SDL_SHADER_SRC_SDLSL_1_0;
        return;
    }

    #define TEST_PROFILE(x) if (SDL_strcmp(srcprofile, x) == 0) { ctx->source_profile = x; return; }

    TEST_PROFILE(SDL_SHADER_SRC_SDLSL_1_0);

    #undef TEST_PROFILE

    fail(ctx, "Unknown profile");
}

void ast_end(Context *ctx)
{
    if (!ctx || !ctx->uses_ast) {
        return;
    }

    delete_shader(ctx, ctx->shader);

    stringcache_destroy(ctx->strcache);

    ctx->uses_ast = SDL_FALSE;
}


Context *parse_to_ast(const SDL_SHADER_CompilerParams *params)
{
    Context *ctx = context_create(params->allocate, params->deallocate, params->allocate_data);
    if (ctx == NULL) {
        return NULL;
    }

    ctx->uses_ast = SDL_TRUE;
    ctx->strcache = stringcache_create(MallocContextBridge, FreeContextBridge, ctx);
    if (!ctx->strcache) {
        context_destroy(ctx);
        return NULL;
    }

    choose_src_profile(ctx, params->srcprofile);

    if (!ctx->isfail) {
        if (SDL_strcmp(ctx->source_profile, SDL_SHADER_SRC_SDLSL_1_0) == 0) {
            parse_sdlsl_source(ctx, params);
        } else {
            fail(ctx, "Internal compiler error. This is a bug, sorry!");
            SDL_assert(!"choose_src_profile should have caught this");
        }
    }
    return ctx;
}

/* API entry point... */

const SDL_SHADER_AstData *SDL_SHADER_ParseAst(const SDL_SHADER_CompilerParams *params)
{
    const SDL_SHADER_AstData *retval = NULL;
    Context *ctx = parse_to_ast(params);
    if (ctx == NULL) {
        return &SDL_SHADER_out_of_mem_data_ast;
    } else {
        retval = build_astdata(ctx);  /* ctx shouldn't be destroyed yet if we didn't fail! */
        SDL_assert(retval != NULL);
        SDL_assert((retval->shader == NULL) == (retval->shader == NULL));
        if (retval->opaque == NULL) {
            context_destroy(ctx);  /* done with it, don't need to save it for return data. */
        }
    }
    return retval;
}

void SDL_SHADER_FreeAstData(const SDL_SHADER_AstData *_data)
{
    SDL_SHADER_AstData *data = (SDL_SHADER_AstData *) _data;
    if ((data != NULL) && (data != &SDL_SHADER_out_of_mem_data_ast)) {
        /* !!! FIXME: this needs to live for deleting the stringcache and the ast. */
        Context *ctx = (Context *) data->opaque;
        SDL_SHADER_Free f = (data->free == NULL) ? SDL_SHADER_internal_free : data->free;
        void *d = data->malloc_data;
        int i;

        /* we don't f(data->source_profile), because that's internal static data. */

        for (i = 0; i < data->error_count; i++) {
            f((void *) data->errors[i].message, d);
            f((void *) data->errors[i].filename, d);
        }
        f((void *) data->errors, d);

        /* don't delete data->ast (it'll delete with the context). */
        f(data, d);

        context_destroy(ctx);  /* finally safe to destroy this. */
    }
}

/* end of SDL_shader_ast.c ... */

