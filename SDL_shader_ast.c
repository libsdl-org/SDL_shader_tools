/**
 * SDL_shader_language; tools for SDL GPU shader support.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

/* !!! FIXME: this needs to be split into separate source files:
   !!! FIXME:  parse, AST, IR, etc. The problem is we need to deal with the
   !!! FIXME:  "Context" struct being passed around everywhere. */

#define __SDL_SHADER_INTERNAL__ 1
#include "SDL_shader_internal.h"

#if DEBUG_COMPILER_PARSER
#define LEMON_SUPPORT_TRACING 1
#endif

/* !!! FIXME: I'd like to lose this. It's really inefficient. Just keep a
   !!! FIXME:  (tail) on these list structures instead? */
#define REVERSE_LINKED_LIST(typ, head) { \
    if ((head) && (head->next)) { \
        typ *tmp = NULL; \
        typ *tmp1 = NULL; \
        while (head != NULL) { \
            tmp = head; \
            head = head->next; \
            tmp->next = tmp1; \
            tmp1 = tmp; \
        } \
        head = tmp; \
    } \
}


/* !!! FIXME: new_* and delete_* should take an allocator, not a context. */

/* These functions are mostly for construction and cleanup of nodes in the
   parse tree. Mostly this is simple allocation and initialization, so we
   can do as little in the lemon code as possible, and then sort it all out
   afterwards. */

#define NEW_AST_NODE(retval, cls, typ) \
    cls *retval = (cls *) Malloc(ctx, sizeof (cls)); \
    do { \
        if (retval == NULL) { return NULL; } \
        retval->ast.type = typ; \
        retval->ast.filename = ctx->sourcefile; \
        retval->ast.line = ctx->sourceline; \
    } while (0)

#define DELETE_AST_NODE(cls) do { \
    if (!cls) return; \
} while (0)


static void delete_compilation_unit(Context*, SDL_SHADER_astCompilationUnit*);
static void delete_statement(Context *ctx, SDL_SHADER_astStatement *stmt);

static SDL_SHADER_astExpression *new_identifier_expr(Context *ctx, const char *string)
{
    NEW_AST_NODE(retval, SDL_SHADER_astExpressionIdentifier, SDL_SHADER_AST_OP_IDENTIFIER);
    retval->datatype = NULL;
    retval->identifier = string;  /* cached; don't copy string. */
    retval->index = 0;
    return (SDL_SHADER_astExpression *) retval;
}

static SDL_SHADER_astExpression *new_callfunc_expr(Context *ctx,
                                        const char *identifier,
                                        SDL_SHADER_astArguments *args)
{
    NEW_AST_NODE(retval, SDL_SHADER_astExpressionCallFunction, SDL_SHADER_AST_OP_CALLFUNC);
    SDL_SHADER_astExpression *expr = new_identifier_expr(ctx, identifier);
    retval->datatype = NULL;
    retval->identifier = (SDL_SHADER_astExpressionIdentifier *) expr;
    retval->args = args;
    return (SDL_SHADER_astExpression *) retval;
}

static SDL_SHADER_astExpression *new_constructor_expr(Context *ctx,
                                            const SDL_SHADER_astDataType *dt,
                                            SDL_SHADER_astArguments *args)
{
    NEW_AST_NODE(retval, SDL_SHADER_astExpressionConstructor, SDL_SHADER_AST_OP_CONSTRUCTOR);
    retval->datatype = dt;
    retval->args = args;
    return (SDL_SHADER_astExpression *) retval;
}

static SDL_SHADER_astExpression *new_cast_expr(Context *ctx, const SDL_SHADER_astDataType *dt, SDL_SHADER_astExpression *operand)
{
    NEW_AST_NODE(retval, SDL_SHADER_astExpressionCast, SDL_SHADER_AST_OP_CAST);
    retval->datatype = dt;
    retval->operand = operand;
    return (SDL_SHADER_astExpression *) retval;
}

static SDL_SHADER_astExpression *new_unary_expr(Context *ctx,
                                            const SDL_SHADER_astNodeType op,
                                            SDL_SHADER_astExpression *operand)
{
    NEW_AST_NODE(retval, SDL_SHADER_astExpressionUnary, op);
    SDL_assert(operator_is_unary(op));
    retval->datatype = NULL;
    retval->operand = operand;
    return (SDL_SHADER_astExpression *) retval;
}

static SDL_SHADER_astExpression *new_binary_expr(Context *ctx,
                                            const SDL_SHADER_astNodeType op,
                                            SDL_SHADER_astExpression *left,
                                            SDL_SHADER_astExpression *right)
{
    NEW_AST_NODE(retval, SDL_SHADER_astExpressionBinary, op);
    SDL_assert(operator_is_binary(op));
    retval->datatype = NULL;
    retval->left = left;
    retval->right = right;
    return (SDL_SHADER_astExpression *) retval;
}

static SDL_SHADER_astExpression *new_ternary_expr(Context *ctx,
                                            const SDL_SHADER_astNodeType op,
                                            SDL_SHADER_astExpression *left,
                                            SDL_SHADER_astExpression *center,
                                            SDL_SHADER_astExpression *right)
{
    NEW_AST_NODE(retval, SDL_SHADER_astExpressionTernary, op);
    SDL_assert(operator_is_ternary(op));
    SDL_assert(op == SDL_SHADER_AST_OP_CONDITIONAL);
    retval->datatype = &ctx->dt_bool;
    retval->left = left;
    retval->center = center;
    retval->right = right;
    return (SDL_SHADER_astExpression *) retval;
}

static SDL_SHADER_astExpression *new_deref_struct_expr(Context *ctx,
                                        SDL_SHADER_astExpression *identifier,
                                        const char *member)
{
    NEW_AST_NODE(retval, SDL_SHADER_astExpressionDerefStruct, SDL_SHADER_AST_OP_DEREF_STRUCT);
    retval->datatype = NULL;
    retval->identifier = identifier;
    retval->member = member;  /* cached; don't copy string. */
    retval->isswizzle = 0;  /* may change during semantic analysis. */
    retval->member_index = 0;  /* set during semantic analysis. */
    return (SDL_SHADER_astExpression *) retval;
}

static SDL_SHADER_astExpression *new_literal_int_expr(Context *ctx, const int value)
{
    NEW_AST_NODE(retval, SDL_SHADER_astExpressionIntLiteral, SDL_SHADER_AST_OP_INT_LITERAL);
    retval->datatype = &ctx->dt_int;
    retval->value = value;
    return (SDL_SHADER_astExpression *) retval;
}

static SDL_SHADER_astExpression *new_literal_float_expr(Context *ctx, const double dbl)
{
    NEW_AST_NODE(retval, SDL_SHADER_astExpressionFloatLiteral, SDL_SHADER_AST_OP_FLOAT_LITERAL);
    retval->datatype = &ctx->dt_float;
    retval->value = dbl;
    return (SDL_SHADER_astExpression *) retval;
}

static SDL_SHADER_astExpression *new_literal_string_expr(Context *ctx, const char *string)
{
    NEW_AST_NODE(retval, SDL_SHADER_astExpressionStringLiteral, SDL_SHADER_AST_OP_STRING_LITERAL);
    retval->datatype = &ctx->dt_string;
    retval->string = string;  /* cached; don't copy string. */
    return (SDL_SHADER_astExpression *) retval;
}

static SDL_SHADER_astExpression *new_literal_boolean_expr(Context *ctx,
                                                          const int value)
{
    NEW_AST_NODE(retval, SDL_SHADER_astExpressionBooleanLiteral, SDL_SHADER_AST_OP_BOOLEAN_LITERAL);
    retval->datatype = &ctx->dt_bool;
    retval->value = value;
    return (SDL_SHADER_astExpression *) retval;
}

static void delete_arguments(Context *ctx, SDL_SHADER_astArguments *args);

static void delete_expr(Context *ctx, SDL_SHADER_astExpression *_expr)
{
    SDL_SHADER_astNode *expr = (SDL_SHADER_astNode *) _expr;

    DELETE_AST_NODE(expr);

    if (expr->ast.type == SDL_SHADER_AST_OP_CAST) {
        delete_expr(ctx, expr->cast.operand);
    } else if (expr->ast.type == SDL_SHADER_AST_OP_CONSTRUCTOR) {
        delete_arguments(ctx, expr->constructor.args);
    } else if (expr->ast.type == SDL_SHADER_AST_OP_DEREF_STRUCT) {
        delete_expr(ctx, expr->derefstruct.identifier);
    } else if (operator_is_unary(expr->ast.type)) {
        delete_expr(ctx, expr->unary.operand);
    } else if (operator_is_binary(expr->ast.type)) {
        delete_expr(ctx, expr->binary.left);
        delete_expr(ctx, expr->binary.right);
    } else if (operator_is_ternary(expr->ast.type)) {
        delete_expr(ctx, expr->ternary.left);
        delete_expr(ctx, expr->ternary.center);
        delete_expr(ctx, expr->ternary.right);
    } else if (expr->ast.type == SDL_SHADER_AST_OP_CALLFUNC) {
        delete_expr(ctx, (SDL_SHADER_astExpression*)expr->callfunc.identifier);
        delete_arguments(ctx, expr->callfunc.args);
    }

    /* rest of operators don't have extra data to free. */

    Free(ctx, expr);
}

static SDL_SHADER_astArguments *new_argument(Context *ctx, SDL_SHADER_astExpression *arg)
{
    NEW_AST_NODE(retval, SDL_SHADER_astArguments, SDL_SHADER_AST_ARGUMENTS);
    retval->argument = arg;
    retval->next = NULL;
    return retval;
}

static void delete_arguments(Context *ctx, SDL_SHADER_astArguments *args)
{
    DELETE_AST_NODE(args);
    delete_arguments(ctx, args->next);
    delete_expr(ctx, args->argument);
    Free(ctx, args);
}

static SDL_SHADER_astFunctionParameters *new_function_param(Context *ctx,
                        const SDL_SHADER_astInputModifier inputmod,
                        const SDL_SHADER_astDataType *dt,
                        const char *identifier, const char *semantic,
                        const SDL_SHADER_astInterpolationModifier interpmod,
                        SDL_SHADER_astExpression *initializer)
{
    NEW_AST_NODE(retval, SDL_SHADER_astFunctionParameters, SDL_SHADER_AST_FUNCTION_PARAMS);
    retval->datatype = dt;
    retval->input_modifier = inputmod;
    retval->identifier = identifier;
    retval->semantic = semantic;
    retval->interpolation_modifier = interpmod;
    retval->initializer = initializer;
    retval->next = NULL;
    return retval;
}

static void delete_function_params(Context *ctx, SDL_SHADER_astFunctionParameters *params)
{
    DELETE_AST_NODE(params);
    delete_function_params(ctx, params->next);
    delete_expr(ctx, params->initializer);
    Free(ctx, params);
}

static SDL_SHADER_astFunctionSignature *new_function_signature(Context *ctx,
                                    const SDL_SHADER_astDataType *dt,
                                    const char *identifier,
                                    SDL_SHADER_astFunctionParameters *params)
{
    NEW_AST_NODE(retval, SDL_SHADER_astFunctionSignature, SDL_SHADER_AST_FUNCTION_SIGNATURE);
    retval->datatype = dt;
    retval->identifier = identifier;
    retval->params = params;
    retval->storage_class = SDL_SHADER_AST_FNSTORECLS_NONE;
    retval->semantic = NULL;
    return retval;
}

static void delete_function_signature(Context *ctx,
                                      SDL_SHADER_astFunctionSignature *sig)
{
    DELETE_AST_NODE(sig);
    delete_function_params(ctx, sig->params);
    Free(ctx, sig);
}

static SDL_SHADER_astCompilationUnit *new_function(Context *ctx,
                                SDL_SHADER_astFunctionSignature *declaration,
                                SDL_SHADER_astStatement *definition)
{
    NEW_AST_NODE(retval, SDL_SHADER_astCompilationUnitFunction, SDL_SHADER_AST_COMPUNIT_FUNCTION);
    retval->next = NULL;
    retval->declaration = declaration;
    retval->definition = definition;
    retval->index = 0;
    return (SDL_SHADER_astCompilationUnit *) retval;
}

static void delete_function(Context *ctx,
                            SDL_SHADER_astCompilationUnitFunction *unitfn)
{
    DELETE_AST_NODE(unitfn);
    delete_compilation_unit(ctx, unitfn->next);
    delete_function_signature(ctx, unitfn->declaration);
    delete_statement(ctx, unitfn->definition);
    Free(ctx, unitfn);
}

static SDL_SHADER_astScalarOrArray *new_scalar_or_array(Context *ctx,
                                          const char *ident, const int isvec,
                                          SDL_SHADER_astExpression *dim)
{
    NEW_AST_NODE(retval, SDL_SHADER_astScalarOrArray, SDL_SHADER_AST_SCALAR_OR_ARRAY);
    retval->identifier = ident;
    retval->isarray = isvec;
    retval->dimension = dim;
    return retval;
}

static void delete_scalar_or_array(Context *ctx, SDL_SHADER_astScalarOrArray *s)
{
    DELETE_AST_NODE(s);
    delete_expr(ctx, s->dimension);
    Free(ctx, s);
}

static SDL_SHADER_astTypedef *new_typedef(Context *ctx, const int isconst,
                                          const SDL_SHADER_astDataType *dt,
                                          SDL_SHADER_astScalarOrArray *soa)
{
    /* we correct this datatype to the final version during semantic analysis. */
    NEW_AST_NODE(retval, SDL_SHADER_astTypedef, SDL_SHADER_AST_TYPEDEF);
    retval->datatype = dt;
    retval->isconst = isconst;
    retval->details = soa;
    return retval;
}

static void delete_typedef(Context *ctx, SDL_SHADER_astTypedef *td)
{
    DELETE_AST_NODE(td);
    delete_scalar_or_array(ctx, td->details);
    Free(ctx, td);
}

static SDL_SHADER_astPackOffset *new_pack_offset(Context *ctx, const char *a, const char *b)
{
    NEW_AST_NODE(retval, SDL_SHADER_astPackOffset, SDL_SHADER_AST_PACK_OFFSET);
    retval->ident1 = a;
    retval->ident2 = b;
    return retval;
}

static void delete_pack_offset(Context *ctx, SDL_SHADER_astPackOffset *o)
{
    DELETE_AST_NODE(o);
    Free(ctx, o);
}

static SDL_SHADER_astVariableLowLevel *new_variable_lowlevel(Context *ctx, SDL_SHADER_astPackOffset *po, const char *reg)
{
    NEW_AST_NODE(retval, SDL_SHADER_astVariableLowLevel, SDL_SHADER_AST_VARIABLE_LOWLEVEL);
    retval->packoffset = po;
    retval->register_name = reg;
    return retval;
}

static void delete_variable_lowlevel(Context *ctx, SDL_SHADER_astVariableLowLevel *vll)
{
    DELETE_AST_NODE(vll);
    delete_pack_offset(ctx, vll->packoffset);
    Free(ctx, vll);
}

static SDL_SHADER_astAnnotations *new_annotation(Context *ctx,
                                        const SDL_SHADER_astDataType *dt,
                                        SDL_SHADER_astExpression *initializer)
{
    NEW_AST_NODE(retval, SDL_SHADER_astAnnotations, SDL_SHADER_AST_ANNOTATION);
    retval->datatype = dt;
    retval->initializer = initializer;
    retval->next = NULL;
    return retval;
}

static void delete_annotation(Context *ctx, SDL_SHADER_astAnnotations *annos)
{
    DELETE_AST_NODE(annos);
    delete_annotation(ctx, annos->next);
    delete_expr(ctx, annos->initializer);
    Free(ctx, annos);
}

static SDL_SHADER_astVariableDeclaration *new_variable_declaration(
                            Context *ctx, SDL_SHADER_astScalarOrArray *soa,
                            const char *semantic,
                            SDL_SHADER_astAnnotations *annotations,
                            SDL_SHADER_astExpression *init,
                            SDL_SHADER_astVariableLowLevel *vll)
{
    NEW_AST_NODE(retval, SDL_SHADER_astVariableDeclaration, SDL_SHADER_AST_VARIABLE_DECLARATION);
    retval->datatype = NULL;
    retval->attributes = 0;
    retval->anonymous_datatype = NULL;
    retval->details = soa;
    retval->semantic = semantic;
    retval->annotations = annotations;
    retval->initializer = init;
    retval->lowlevel = vll;
    retval->next = NULL;
    return retval;
}

static void delete_variable_declaration(Context *ctx, SDL_SHADER_astVariableDeclaration *dcl)
{
    DELETE_AST_NODE(dcl);
    delete_variable_declaration(ctx, dcl->next);
    delete_scalar_or_array(ctx, dcl->details);
    delete_annotation(ctx, dcl->annotations);
    delete_expr(ctx, dcl->initializer);
    delete_variable_lowlevel(ctx, dcl->lowlevel);
    Free(ctx, dcl);
}

static SDL_SHADER_astCompilationUnit *new_global_variable(Context *ctx, SDL_SHADER_astVariableDeclaration *decl)
{
    NEW_AST_NODE(retval, SDL_SHADER_astCompilationUnitVariable, SDL_SHADER_AST_COMPUNIT_VARIABLE);
    retval->next = NULL;
    retval->declaration = decl;
    return (SDL_SHADER_astCompilationUnit *) retval;
}

static void delete_global_variable(Context *ctx, SDL_SHADER_astCompilationUnitVariable *var)
{
    DELETE_AST_NODE(var);
    delete_compilation_unit(ctx, var->next);
    delete_variable_declaration(ctx, var->declaration);
    Free(ctx, var);
}

static SDL_SHADER_astCompilationUnit *new_global_typedef(Context *ctx, SDL_SHADER_astTypedef *td)
{
    NEW_AST_NODE(retval, SDL_SHADER_astCompilationUnitTypedef, SDL_SHADER_AST_COMPUNIT_TYPEDEF);
    retval->next = NULL;
    retval->type_info = td;
    return (SDL_SHADER_astCompilationUnit *) retval;
}

static void delete_global_typedef(Context *ctx, SDL_SHADER_astCompilationUnitTypedef *unit)
{
    DELETE_AST_NODE(unit);
    delete_compilation_unit(ctx, unit->next);
    delete_typedef(ctx, unit->type_info);
    Free(ctx, unit);
}

static SDL_SHADER_astStructMembers *new_struct_member(Context *ctx, SDL_SHADER_astScalarOrArray *soa, const char *semantic)
{
    NEW_AST_NODE(retval, SDL_SHADER_astStructMembers, SDL_SHADER_AST_STRUCT_MEMBER);
    retval->datatype = NULL;
    retval->semantic = semantic;
    retval->details = soa;
    retval->interpolation_mod = SDL_SHADER_AST_INTERPMOD_NONE;
    retval->next = NULL;
    return retval;
}

static void delete_struct_member(Context *ctx, SDL_SHADER_astStructMembers *member)
{
    DELETE_AST_NODE(member);
    delete_struct_member(ctx, member->next);
    delete_scalar_or_array(ctx, member->details);
    Free(ctx, member);
}

static SDL_SHADER_astStructDeclaration *new_struct_declaration(Context *ctx, const char *name, SDL_SHADER_astStructMembers *members)
{
    NEW_AST_NODE(retval, SDL_SHADER_astStructDeclaration, SDL_SHADER_AST_STRUCT_DECLARATION);
    retval->datatype = NULL;
    retval->name = name;
    retval->members = members;
    return retval;
}

static void delete_struct_declaration(Context *ctx, SDL_SHADER_astStructDeclaration *decl)
{
    DELETE_AST_NODE(decl);
    delete_struct_member(ctx, decl->members);
    Free(ctx, decl);
}

static SDL_SHADER_astCompilationUnit *new_global_struct(Context *ctx, SDL_SHADER_astStructDeclaration *sd)
{
    NEW_AST_NODE(retval, SDL_SHADER_astCompilationUnitStruct, SDL_SHADER_AST_COMPUNIT_STRUCT);
    retval->next = NULL;
    retval->struct_info = sd;
    return (SDL_SHADER_astCompilationUnit *) retval;
}

static void delete_global_struct(Context *ctx, SDL_SHADER_astCompilationUnitStruct *unit)
{
    DELETE_AST_NODE(unit);
    delete_compilation_unit(ctx, unit->next);
    delete_struct_declaration(ctx, unit->struct_info);
    Free(ctx, unit);
}

static void delete_compilation_unit(Context *ctx, SDL_SHADER_astCompilationUnit *unit)
{
    if (!unit) {
        return;
    }

    /* It's important to not recurse too deeply here, since you may have thousands of items in this linked list (each
       line of a massive function, for example). To avoid this, we iterate the list here, deleting all children and
       making them think they have no reason to recurse in their own delete methods. Please note that everyone should
       _try_ to delete their "next" member, just in case, but hopefully this cleaned it out. */

    SDL_SHADER_astCompilationUnit *i = unit->next;
    unit->next = NULL;
    while (i)
    {
        SDL_SHADER_astCompilationUnit *next = i->next;
        i->next = NULL;
        delete_compilation_unit(ctx, i);
        i = next;
    }

    switch (unit->ast.type)
    {
        #define DELETE_UNIT(typ, cls, fn) case SDL_SHADER_AST_COMPUNIT_##typ: delete_##fn(ctx, (cls *) unit); break;
        DELETE_UNIT(FUNCTION, SDL_SHADER_astCompilationUnitFunction, function);
        DELETE_UNIT(TYPEDEF, SDL_SHADER_astCompilationUnitTypedef, global_typedef);
        DELETE_UNIT(VARIABLE, SDL_SHADER_astCompilationUnitVariable, global_variable);
        DELETE_UNIT(STRUCT, SDL_SHADER_astCompilationUnitStruct, global_struct);
        #undef DELETE_UNIT
        default: SDL_assert(!"missing cleanup code"); break;
    }

    /* don't free (unit) here, the class-specific functions do it. */
}

static SDL_SHADER_astStatement *new_typedef_statement(Context *ctx, SDL_SHADER_astTypedef *td)
{
    NEW_AST_NODE(retval, SDL_SHADER_astTypedefStatement, SDL_SHADER_AST_STATEMENT_TYPEDEF);
    retval->next = NULL;
    retval->type_info = td;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_typedef_statement(Context *ctx, SDL_SHADER_astTypedefStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->next);
    delete_typedef(ctx, stmt->type_info);
    Free(ctx, stmt);
}

static SDL_SHADER_astStatement *new_return_statement(Context *ctx, SDL_SHADER_astExpression *expr)
{
    NEW_AST_NODE(retval, SDL_SHADER_astReturnStatement, SDL_SHADER_AST_STATEMENT_RETURN);
    retval->next = NULL;
    retval->expr = expr;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_return_statement(Context *ctx, SDL_SHADER_astReturnStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->next);
    delete_expr(ctx, stmt->expr);
    Free(ctx, stmt);
}

static SDL_SHADER_astStatement *new_block_statement(Context *ctx, SDL_SHADER_astStatement *stmts)
{
    NEW_AST_NODE(retval, SDL_SHADER_astBlockStatement, SDL_SHADER_AST_STATEMENT_BLOCK);
    retval->next = NULL;
    retval->statements = stmts;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_block_statement(Context *ctx, SDL_SHADER_astBlockStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->statements);
    delete_statement(ctx, stmt->next);
    Free(ctx, stmt);
}

static SDL_SHADER_astStatement *new_for_statement(Context *ctx,
                                    SDL_SHADER_astVariableDeclaration *decl,
                                    SDL_SHADER_astExpression *initializer,
                                    SDL_SHADER_astExpression *looptest,
                                    SDL_SHADER_astExpression *counter,
                                    SDL_SHADER_astStatement *statement)
{
    NEW_AST_NODE(retval, SDL_SHADER_astForStatement, SDL_SHADER_AST_STATEMENT_FOR);
    retval->next = NULL;
    retval->unroll = -1;
    retval->var_decl = decl;
    retval->initializer = initializer;
    retval->looptest = looptest;
    retval->counter = counter;
    retval->statement = statement;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_for_statement(Context *ctx,SDL_SHADER_astForStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->next);
    delete_variable_declaration(ctx, stmt->var_decl);
    delete_expr(ctx, stmt->initializer);
    delete_expr(ctx, stmt->looptest);
    delete_expr(ctx, stmt->counter);
    delete_statement(ctx, stmt->statement);
    Free(ctx, stmt);
}

static SDL_SHADER_astStatement *new_do_statement(Context *ctx,
                                                const int unroll,
                                                SDL_SHADER_astStatement *stmt,
                                                SDL_SHADER_astExpression *expr)
{
    NEW_AST_NODE(retval,SDL_SHADER_astDoStatement,SDL_SHADER_AST_STATEMENT_DO);
    retval->next = NULL;
    retval->unroll = unroll;
    retval->expr = expr;
    retval->statement = stmt;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_do_statement(Context *ctx, SDL_SHADER_astDoStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->next);
    delete_statement(ctx, stmt->statement);
    delete_expr(ctx, stmt->expr);
    Free(ctx, stmt);
}

static SDL_SHADER_astStatement *new_while_statement(Context *ctx,
                                                const int unroll,
                                                SDL_SHADER_astExpression *expr,
                                                SDL_SHADER_astStatement *stmt)
{
    NEW_AST_NODE(retval, SDL_SHADER_astWhileStatement, SDL_SHADER_AST_STATEMENT_WHILE);
    retval->next = NULL;
    retval->unroll = unroll;
    retval->expr = expr;
    retval->statement = stmt;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_while_statement(Context *ctx, SDL_SHADER_astWhileStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->next);
    delete_statement(ctx, stmt->statement);
    delete_expr(ctx, stmt->expr);
    Free(ctx, stmt);
}

static SDL_SHADER_astStatement *new_if_statement(Context *ctx,
                                            const int attr,
                                            SDL_SHADER_astExpression *expr,
                                            SDL_SHADER_astStatement *stmt,
                                            SDL_SHADER_astStatement *elsestmt)
{
    NEW_AST_NODE(retval,SDL_SHADER_astIfStatement,SDL_SHADER_AST_STATEMENT_IF);
    retval->next = NULL;
    retval->attributes = attr;
    retval->expr = expr;
    retval->statement = stmt;
    retval->else_statement = elsestmt;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_if_statement(Context *ctx, SDL_SHADER_astIfStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->next);
    delete_expr(ctx, stmt->expr);
    delete_statement(ctx, stmt->statement);
    delete_statement(ctx, stmt->else_statement);
    Free(ctx, stmt);
}

static SDL_SHADER_astSwitchCases *new_switch_case(Context *ctx,
                                                SDL_SHADER_astExpression *expr,
                                                SDL_SHADER_astStatement *stmt)
{
    NEW_AST_NODE(retval, SDL_SHADER_astSwitchCases, SDL_SHADER_AST_SWITCH_CASE);
    retval->expr = expr;
    retval->statement = stmt;
    retval->next = NULL;
    return retval;
}

static void delete_switch_case(Context *ctx, SDL_SHADER_astSwitchCases *sc)
{
    DELETE_AST_NODE(sc);
    delete_switch_case(ctx, sc->next);
    delete_expr(ctx, sc->expr);
    delete_statement(ctx, sc->statement);
    Free(ctx, sc);
}

static SDL_SHADER_astStatement *new_empty_statement(Context *ctx)
{
    NEW_AST_NODE(retval, SDL_SHADER_astEmptyStatement, SDL_SHADER_AST_STATEMENT_EMPTY);
    retval->next = NULL;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_empty_statement(Context *ctx, SDL_SHADER_astEmptyStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->next);
    Free(ctx, stmt);
}

static SDL_SHADER_astStatement *new_break_statement(Context *ctx)
{
    NEW_AST_NODE(retval, SDL_SHADER_astBreakStatement, SDL_SHADER_AST_STATEMENT_BREAK);
    retval->next = NULL;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_break_statement(Context *ctx, SDL_SHADER_astBreakStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->next);
    Free(ctx, stmt);
}

static SDL_SHADER_astStatement *new_continue_statement(Context *ctx)
{
    NEW_AST_NODE(retval, SDL_SHADER_astContinueStatement, SDL_SHADER_AST_STATEMENT_CONTINUE);
    retval->next = NULL;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_continue_statement(Context *ctx, SDL_SHADER_astContinueStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->next);
    Free(ctx, stmt);
}

static SDL_SHADER_astStatement *new_discard_statement(Context *ctx)
{
    NEW_AST_NODE(retval, SDL_SHADER_astDiscardStatement, SDL_SHADER_AST_STATEMENT_DISCARD);
    retval->next = NULL;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_discard_statement(Context *ctx, SDL_SHADER_astDiscardStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->next);
    Free(ctx, stmt);
}

static SDL_SHADER_astStatement *new_expr_statement(Context *ctx, SDL_SHADER_astExpression *expr)
{
    NEW_AST_NODE(retval, SDL_SHADER_astExpressionStatement, SDL_SHADER_AST_STATEMENT_EXPRESSION);
    retval->next = NULL;
    retval->expr = expr;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_expr_statement(Context *ctx, SDL_SHADER_astExpressionStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->next);
    delete_expr(ctx, stmt->expr);
    Free(ctx, stmt);
}

static SDL_SHADER_astStatement *new_switch_statement(Context *ctx,
                                            const int attr,
                                            SDL_SHADER_astExpression *expr,
                                            SDL_SHADER_astSwitchCases *cases)
{
    NEW_AST_NODE(retval, SDL_SHADER_astSwitchStatement, SDL_SHADER_AST_STATEMENT_SWITCH);
    retval->next = NULL;
    retval->attributes = attr;
    retval->expr = expr;
    retval->cases = cases;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_switch_statement(Context *ctx, SDL_SHADER_astSwitchStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_expr(ctx, stmt->expr);
    delete_switch_case(ctx, stmt->cases);
    Free(ctx, stmt);
}

static SDL_SHADER_astStatement *new_struct_statement(Context *ctx, SDL_SHADER_astStructDeclaration *sd)
{
    NEW_AST_NODE(retval, SDL_SHADER_astStructStatement, SDL_SHADER_AST_STATEMENT_STRUCT);
    retval->next = NULL;
    retval->struct_info = sd;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_struct_statement(Context *ctx, SDL_SHADER_astStructStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->next);
    delete_struct_declaration(ctx, stmt->struct_info);
    Free(ctx, stmt);
}

static SDL_SHADER_astStatement *new_vardecl_statement(Context *ctx, SDL_SHADER_astVariableDeclaration *vd)
{
    NEW_AST_NODE(retval, SDL_SHADER_astVarDeclStatement, SDL_SHADER_AST_STATEMENT_VARDECL);
    retval->next = NULL;
    retval->declaration = vd;
    return (SDL_SHADER_astStatement *) retval;
}

static void delete_vardecl_statement(Context *ctx, SDL_SHADER_astVarDeclStatement *stmt)
{
    DELETE_AST_NODE(stmt);
    delete_statement(ctx, stmt->next);
    delete_variable_declaration(ctx, stmt->declaration);
    Free(ctx, stmt);
}

static void delete_statement(Context *ctx, SDL_SHADER_astStatement *stmt)
{
    if (!stmt) {
        return;
    }

    /* It's important to not recurse too deeply here, since you may have thousands of items in this linked list (each
       line of a massive function, for example). To avoid this, we iterate the list here, deleting all children and
       making them think they have no reason to recurse in their own delete methods. Please note that everyone should
       _try_ to delete their "next" member, just in case, but hopefully this cleaned it out. */

    SDL_SHADER_astStatement *i = stmt->next;
    stmt->next = NULL;
    while (i) {
        SDL_SHADER_astStatement *next = i->next;
        i->next = NULL;
        delete_statement(ctx, i);
        i = next;
    }

    switch (stmt->ast.type) {
        #define DELETE_STATEMENT(typ, cls, fn) case SDL_SHADER_AST_STATEMENT_##typ: delete_##fn##_statement(ctx, (cls *) stmt); break;
        DELETE_STATEMENT(BLOCK, SDL_SHADER_astBlockStatement, block);
        DELETE_STATEMENT(EMPTY, SDL_SHADER_astEmptyStatement, empty);
        DELETE_STATEMENT(IF, SDL_SHADER_astIfStatement, if);
        DELETE_STATEMENT(SWITCH, SDL_SHADER_astSwitchStatement, switch);
        DELETE_STATEMENT(EXPRESSION, SDL_SHADER_astExpressionStatement, expr);
        DELETE_STATEMENT(FOR, SDL_SHADER_astForStatement, for);
        DELETE_STATEMENT(DO, SDL_SHADER_astDoStatement, do);
        DELETE_STATEMENT(WHILE, SDL_SHADER_astWhileStatement, while);
        DELETE_STATEMENT(RETURN, SDL_SHADER_astReturnStatement, return);
        DELETE_STATEMENT(BREAK, SDL_SHADER_astBreakStatement, break);
        DELETE_STATEMENT(CONTINUE, SDL_SHADER_astContinueStatement, continue);
        DELETE_STATEMENT(DISCARD, SDL_SHADER_astDiscardStatement, discard);
        DELETE_STATEMENT(TYPEDEF, SDL_SHADER_astTypedefStatement, typedef);
        DELETE_STATEMENT(STRUCT, SDL_SHADER_astStructStatement, struct);
        DELETE_STATEMENT(VARDECL, SDL_SHADER_astVarDeclStatement, vardecl);
        #undef DELETE_STATEMENT
        default: SDL_assert(!"missing cleanup code"); break;
    }

    /* don't free (stmt) here, the class-specific functions do it. */
}


static int convert_to_lemon_token(Context *ctx, const char *token, size_t tokenlen, const Token tokenval)
{
    switch (tokenval) {
        case ((Token) ','): return TOKEN_SDLSL_COMMA;
        case ((Token) '='): return TOKEN_SDLSL_ASSIGN;
        case ((Token) TOKEN_ADDASSIGN): return TOKEN_SDLSL_ADDASSIGN;
        case ((Token) TOKEN_SUBASSIGN): return TOKEN_SDLSL_SUBASSIGN;
        case ((Token) TOKEN_MULTASSIGN): return TOKEN_SDLSL_MULASSIGN;
        case ((Token) TOKEN_DIVASSIGN): return TOKEN_SDLSL_DIVASSIGN;
        case ((Token) TOKEN_MODASSIGN): return TOKEN_SDLSL_MODASSIGN;
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
        case ((Token) TOKEN_INT_LITERAL): return TOKEN_SDLSL_INT_CONSTANT;
        case ((Token) TOKEN_FLOAT_LITERAL): return TOKEN_SDLSL_FLOAT_CONSTANT;
        case ((Token) TOKEN_STRING_LITERAL): return TOKEN_SDLSL_STRING_LITERAL;
        case ((Token) ':'): return TOKEN_SDLSL_COLON;
        case ((Token) ';'): return TOKEN_SDLSL_SEMICOLON;
        case ((Token) '{'): return TOKEN_SDLSL_LBRACE;
        case ((Token) '}'): return TOKEN_SDLSL_RBRACE;
        /* case ((Token) TOKEN_PP_PRAGMA): return TOKEN_SDLSL_PRAGMA; */
        /* case ((Token) '\n'): return TOKEN_SDLSL_NEWLINE; */

        case ((Token) TOKEN_IDENTIFIER):
            #define tokencmp(t) ((tokenlen == strlen(t)) && (SDL_memcmp(token, t, tokenlen) == 0))
            /* case ((Token) ''): return TOKEN_SDLSL_TYPECAST */
            /* if (tokencmp("")) return TOKEN_SDLSL_TYPE_NAME */
            /* if (tokencmp("...")) return TOKEN_SDLSL_ELIPSIS */
            if (tokencmp("else")) return TOKEN_SDLSL_ELSE;
            if (tokencmp("inline")) return TOKEN_SDLSL_INLINE;
            if (tokencmp("void")) return TOKEN_SDLSL_VOID;
            if (tokencmp("in")) return TOKEN_SDLSL_IN;
            if (tokencmp("inout")) return TOKEN_SDLSL_INOUT;
            if (tokencmp("out")) return TOKEN_SDLSL_OUT;
            if (tokencmp("uniform")) return TOKEN_SDLSL_UNIFORM;
            if (tokencmp("linear")) return TOKEN_SDLSL_LINEAR;
            if (tokencmp("centroid")) return TOKEN_SDLSL_CENTROID;
            if (tokencmp("nointerpolation")) return TOKEN_SDLSL_NOINTERPOLATION;
            if (tokencmp("noperspective")) return TOKEN_SDLSL_NOPERSPECTIVE;
            if (tokencmp("sample")) return TOKEN_SDLSL_SAMPLE;
            if (tokencmp("struct")) return TOKEN_SDLSL_STRUCT;
            if (tokencmp("typedef")) return TOKEN_SDLSL_TYPEDEF;
            if (tokencmp("const")) return TOKEN_SDLSL_CONST;
            if (tokencmp("packoffset")) return TOKEN_SDLSL_PACKOFFSET;
            if (tokencmp("register")) return TOKEN_SDLSL_REGISTER;
            if (tokencmp("extern")) return TOKEN_SDLSL_EXTERN;
            if (tokencmp("shared")) return TOKEN_SDLSL_SHARED;
            if (tokencmp("static")) return TOKEN_SDLSL_STATIC;
            if (tokencmp("volatile")) return TOKEN_SDLSL_VOLATILE;
            if (tokencmp("row_major")) return TOKEN_SDLSL_ROWMAJOR;
            if (tokencmp("column_major")) return TOKEN_SDLSL_COLUMNMAJOR;
            if (tokencmp("bool")) return TOKEN_SDLSL_BOOL;
            if (tokencmp("int")) return TOKEN_SDLSL_INT;
            if (tokencmp("uint")) return TOKEN_SDLSL_UINT;
            if (tokencmp("half")) return TOKEN_SDLSL_HALF;
            if (tokencmp("float")) return TOKEN_SDLSL_FLOAT;
            if (tokencmp("double")) return TOKEN_SDLSL_DOUBLE;
            if (tokencmp("string")) return TOKEN_SDLSL_STRING;
            if (tokencmp("snorm")) return TOKEN_SDLSL_SNORM;
            if (tokencmp("unorm")) return TOKEN_SDLSL_UNORM;
            if (tokencmp("buffer")) return TOKEN_SDLSL_BUFFER;
            if (tokencmp("vector")) return TOKEN_SDLSL_VECTOR;
            if (tokencmp("matrix")) return TOKEN_SDLSL_MATRIX;
            if (tokencmp("break")) return TOKEN_SDLSL_BREAK;
            if (tokencmp("continue")) return TOKEN_SDLSL_CONTINUE;
            if (tokencmp("discard")) return TOKEN_SDLSL_DISCARD;
            if (tokencmp("return")) return TOKEN_SDLSL_RETURN;
            if (tokencmp("while")) return TOKEN_SDLSL_WHILE;
            if (tokencmp("for")) return TOKEN_SDLSL_FOR;
            if (tokencmp("unroll")) return TOKEN_SDLSL_UNROLL;
            if (tokencmp("loop")) return TOKEN_SDLSL_LOOP;
            if (tokencmp("do")) return TOKEN_SDLSL_DO;
            if (tokencmp("if")) return TOKEN_SDLSL_IF;
            if (tokencmp("branch")) return TOKEN_SDLSL_BRANCH;
            if (tokencmp("flatten")) return TOKEN_SDLSL_FLATTEN;
            if (tokencmp("switch")) return TOKEN_SDLSL_SWITCH;
            if (tokencmp("forcecase")) return TOKEN_SDLSL_FORCECASE;
            if (tokencmp("call")) return TOKEN_SDLSL_CALL;
            if (tokencmp("case")) return TOKEN_SDLSL_CASE;
            if (tokencmp("default")) return TOKEN_SDLSL_DEFAULT;
            if (tokencmp("sampler")) return TOKEN_SDLSL_SAMPLER;
            if (tokencmp("sampler1D")) return TOKEN_SDLSL_SAMPLER1D;
            if (tokencmp("sampler2D")) return TOKEN_SDLSL_SAMPLER2D;
            if (tokencmp("sampler3D")) return TOKEN_SDLSL_SAMPLER3D;
            if (tokencmp("samplerCUBE")) return TOKEN_SDLSL_SAMPLERCUBE;
            if (tokencmp("sampler_state")) return TOKEN_SDLSL_SAMPLER_STATE;
            if (tokencmp("SamplerState")) return TOKEN_SDLSL_SAMPLERSTATE;
            if (tokencmp("true")) return TOKEN_SDLSL_TRUE;
            if (tokencmp("false")) return TOKEN_SDLSL_FALSE;
            if (tokencmp("SamplerComparisonState")) return TOKEN_SDLSL_SAMPLERCOMPARISONSTATE;
            if (tokencmp("isolate")) return TOKEN_SDLSL_ISOLATE;
            if (tokencmp("maxInstructionCount")) return TOKEN_SDLSL_MAXINSTRUCTIONCOUNT;
            if (tokencmp("noExpressionOptimizations")) return TOKEN_SDLSL_NOEXPRESSIONOPTIMIZATIONS;
            if (tokencmp("unused")) return TOKEN_SDLSL_UNUSED;
            if (tokencmp("xps")) return TOKEN_SDLSL_XPS;
            #undef tokencmp

            /* get a canonical copy of the string now, as we'll need it. */
            token = stringcache_len(ctx->strcache, token, tokenlen);
            if (get_usertype(ctx, token) != NULL) {
                return TOKEN_SDLSL_USERTYPE;
            }
            return TOKEN_SDLSL_IDENTIFIER;

        case TOKEN_EOI: return 0;
        default: SDL_assert(!"unexpected token from lexer\n"); return 0;
    }

    return 0;
}


/* parse the source code into an AST. */
static void parse_source(Context *ctx, const char *filename,
                         const char *source, size_t sourcelen,
                         const char **system_include_paths,
                         size_t system_include_path_count,
                         const char **local_include_paths,
                         size_t local_include_path_count,
                         SDL_SHADER_includeOpen include_open,
                         SDL_SHADER_includeClose include_close,
                         const SDL_SHADER_preprocessorDefine *defines,
                         size_t define_count)
{
    TokenData data;
    size_t tokenlen;
    Token tokenval;
    const char *token;
    int lemon_token;
    const char *fname;
    void *parser;

    if (!preprocessor_start(ctx, filename, source, sourcelen,
                            system_include_paths, system_include_path_count,
                            local_include_paths, local_include_path_count,
                            include_open, include_close,
                            defines, define_count, SDL_FALSE)) {
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

    init_builtins(ctx);

    SymbolScope *start_scope = ctx->usertypes.scope;

    #if DEBUG_COMPILER_PARSER
    ParseSDLSLTrace(stdout, "COMPILER: ");
    #endif

    /* Run the preprocessor/lexer/parser... */
    SDL_bool is_pragma = SDL_FALSE;   /* !!! FIXME: remove this later when we can parse #pragma. */
    SDL_bool skipping = SDL_FALSE; /* !!! FIXME: remove this later when we can parse #pragma. */
    do {
        token = preprocessor_nexttoken(pp, &tokenlen, &tokenval);

        if (ctx->out_of_memory) {
            break;
        }

        if ((tokenval == TOKEN_HASH) || (tokenval == TOKEN_HASHHASH)) {
            tokenval = TOKEN_BAD_CHARS;
        }

        if (tokenval == TOKEN_BAD_CHARS) {
            fail(ctx, "Bad characters in source file");
            continue;
        } else if (tokenval == TOKEN_PP_PRAGMA) {
            SDL_assert(!is_pragma);
            is_pragma = SDL_TRUE;
            skipping = SDL_TRUE;
            continue;
        } else if (tokenval == ((Token) '\n')) {
            SDL_assert(is_pragma);
            is_pragma = SDL_FALSE;
            skipping = SDL_FALSE;
            continue;
        } else if (skipping) {
            continue;
        }

        /* !!! FIXME: this is a mess, decide who should be doing this stuff, and only do it once. */
        lemon_token = convert_to_lemon_token(ctx, token, tokenlen, tokenval);
        switch (lemon_token) {
            case TOKEN_SDLSL_INT_CONSTANT:
                data.i64 = strtoi64(token, tokenlen);
                break;

            case TOKEN_SDLSL_FLOAT_CONSTANT:
                data.dbl = strtodouble(token, tokenlen);
                break;

            case TOKEN_SDLSL_USERTYPE:
                data.string = stringcache_len(ctx->strcache, token, tokenlen);
                data.datatype = get_usertype(ctx, data.string);  /* !!! FIXME: do we need this? It's kind of useless during parsing. */
                SDL_assert(data.datatype != NULL);
                break;

            case TOKEN_SDLSL_STRING_LITERAL:
            case TOKEN_SDLSL_IDENTIFIER:
                data.string = stringcache_len(ctx->strcache, token, tokenlen);
                break;

            default:
                data.i64 = 0;
                break;
        }

        ParseSDLSL(parser, lemon_token, data, ctx);

        /* this probably isn't perfect, but it's good enough for surviving */
        /* the parse. We'll sort out correctness once we have a tree. */
        if (lemon_token == TOKEN_SDLSL_LBRACE) {
            push_scope(ctx);
        } else if (lemon_token == TOKEN_SDLSL_RBRACE) {
            pop_scope(ctx);
        }
    } while (tokenval != TOKEN_EOI);

    /* Clean out extra usertypes; they are dummies until semantic analysis. */
    while (ctx->usertypes.scope != start_scope) {
        pop_symbol(ctx, &ctx->usertypes);
    }

    ParseSDLSLFree(parser, ctx->free, ctx->malloc_data);
    preprocessor_end(pp);
}


static SDL_SHADER_astData SDL_SHADER_out_of_mem_data_ast = {
    1, &SDL_SHADER_out_of_mem_error, 0, 0, 0, 0, 0, 0
};


/* !!! FIXME: cut and paste from assembler. */
static const SDL_SHADER_astData *build_failed_ast(Context *ctx)
{
    SDL_assert(ctx->isfail);

    if (ctx->out_of_memory) {
        return &SDL_SHADER_out_of_mem_data_ast;
    }

    SDL_SHADER_astData *retval = NULL;
    retval = (SDL_SHADER_astData *) Malloc(ctx, sizeof (SDL_SHADER_astData));
    if (retval == NULL) {
        return &SDL_SHADER_out_of_mem_data_ast;
    }

    SDL_zerop(retval);
    retval->source_profile = ctx->source_profile;
    retval->malloc = (ctx->malloc == SDL_SHADER_internal_malloc) ? NULL : ctx->malloc;
    retval->free = (ctx->free == SDL_SHADER_internal_free) ? NULL : ctx->free;
    retval->malloc_data = ctx->malloc_data;
    retval->error_count = errorlist_count(ctx->errors);
    retval->errors = errorlist_flatten(ctx->errors);

    if (ctx->out_of_memory) {
        Free(ctx, retval);
        return &SDL_SHADER_out_of_mem_data_ast;
    }

    return retval;
}


static const SDL_SHADER_astData *build_astdata(Context *ctx)
{
    SDL_SHADER_astData *retval = NULL;

    if (ctx->out_of_memory) {
        return &SDL_SHADER_out_of_mem_data_ast;
    }

    retval = (SDL_SHADER_astData *) Malloc(ctx, sizeof (SDL_SHADER_astData));
    if (retval == NULL) {
        return &SDL_SHADER_out_of_mem_data_ast;
    }

    SDL_zerop(retval);
    retval->malloc = (ctx->malloc == SDL_SHADER_internal_malloc) ? NULL : ctx->malloc;
    retval->free = (ctx->free == SDL_SHADER_internal_free) ? NULL : ctx->free;
    retval->malloc_data = ctx->malloc_data;

    if (!ctx->isfail) {
        retval->source_profile = ctx->source_profile;
        retval->ast = ctx->ast;
    }

    retval->error_count = errorlist_count(ctx->errors);
    retval->errors = errorlist_flatten(ctx->errors);
    if (ctx->out_of_memory) {
        Free(ctx, retval);
        return &SDL_SHADER_out_of_mem_data_ast;
    }

    retval->opaque = ctx;

    return retval;
}


static void choose_src_profile(Context *ctx, const char *srcprofile)
{
    /* make sure this points to an internal static string so we don't have to free it. */

    if (!srcprofile) {
        ctx->srcprofile = SDL_SHADER_SRC_SDLSL_1_0;
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

    delete_compilation_unit(ctx, (SDL_SHADER_astCompilationUnit *) ctx->ast);

    ctx->uses_ast = SDL_FALSE;
}


/* API entry point... */

const SDL_SHADER_AstData *SDL_SHADER_ParseAst(const char *srcprofile,
                                    const char *filename, const char *source,
                                    size_t sourcelen,
                                    const char **system_include_paths,
                                    size_t system_include_path_count,
                                    const char **local_include_paths,
                                    size_t local_include_path_count,
                                    SDL_SHADER_includeOpen include_open,
                                    SDL_SHADER_includeClose include_close,
                                    const SDL_SHADER_preprocessorDefine *defines,
                                    size_t define_count,
                                    SDL_SHADER_malloc m, SDL_SHADER_free f, void *d)

{
    const SDL_SHADER_astData *retval = NULL;
    Context *ctx = context_create(m, f, d);
    if (ctx == NULL) {
        return &SDL_SHADER_out_of_mem_data_ast;
    }

    choose_src_profile(ctx, srcprofile);

    if (!ctx->isfail) {
        parse_source(ctx, filename, source, sourcelen, system_include_paths, system_include_path_count, local_include_paths, local_include_path_count, include_open, include_close, defines, define_count);
    }

    if (!ctx->isfail) {
        retval = build_astdata(ctx);  /* ctx isn't destroyed yet! */
    } else {
        retval = (SDL_SHADER_astData *) build_failed_ast(ctx);
        destroy_context(ctx);
    }

    return retval;
}


void SDL_SHADER_freeAstData(const SDL_SHADER_astData *_data)
{
    SDL_SHADER_astData *data = (SDL_SHADER_astData *) _data;
    if ((data != NULL) && (data != &SDL_SHADER_out_of_mem_data_ast)) {
        /* !!! FIXME: this needs to live for deleting the stringcache and the ast. */
        Context *ctx = (Context *) data->opaque;
        SDL_SHADER_free f = (data->free == NULL) ? SDL_SHADER_internal_free : data->free;
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

        destroy_context(ctx);  /* finally safe to destroy this. */
    }
}

/* end of SDL_shader_ast.c ... */

