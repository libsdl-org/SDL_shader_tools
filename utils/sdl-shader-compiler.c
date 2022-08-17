/**
 * SDL_shader_tools; tools for SDL GPU shader support.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "SDL_shader_compiler.h"
#include "SDL_shader_ast.h"

#define SDL_SHADER_DEBUG_MALLOC 0

#if SDL_SHADER_DEBUG_MALLOC
static void *UtilMalloc(int len, void *d)
{
    void *ptr = SDL_malloc(len + sizeof (int));
    int *store = (int *) ptr;
    printf("malloc() %d bytes (%p)\n", len, ptr);
    if (ptr == NULL) { return NULL; }
    *store = len;
    return (void *) (store + 1);
}


static void UtilFree(void *_ptr, void *d)
{
    int *ptr = (((int *) _ptr) - 1);
    int len = *ptr;
    printf("free() %d bytes (%p)\n", len, ptr);
    SDL_free(ptr);
}
#else
#define UtilMalloc NULL
#define UtilFree NULL
#endif


static void fail(const char *err)
{
    fprintf(stderr, "%s.\n", err);
    exit(1);
}


static void print_errors(const SDL_SHADER_Error *errors, const size_t error_count)
{
    size_t i;
    for (i = 0; i < error_count; i++) {
        fprintf(stderr, "%s:%d: %s: %s\n",
                errors[i].filename ? errors[i].filename : "???",
                errors[i].error_position,
                errors[i].is_error ? "error" : "warning",
                errors[i].message);
    }
}

/* These _HAVE_ to be in the same order as SDL_SHADER_AstNodeType! */
static const char *binary[] = {
    "*", "/", "%", "+", "-", "<<", ">>", "<", ">", "<=", ">=", "==", "!=", "&", "^", "|", "&&", "||"
};

static const char *assign[] = {
    "=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=", "|="
};

static const char *pre_unary[] = { "+", "-", "~", "!", "()" };

static const char *simple_stmt[] = { "", "break", "continue", "discard" };

static int indent = 0;
static void do_indent(FILE *io)
{
    int i;
    for (i = 0; i < indent; i++) {
        fprintf(io, "    ");
    }
}

static void print_ast(FILE *io, const SDL_bool substmt, const void *_ast)
{
    const SDL_SHADER_AstNode *ast = (const SDL_SHADER_AstNode *) _ast;
    const char *nl = substmt ? "" : "\n";
    const int typeint = ast ? ((int) ast->ast.type) : 0;
    int isblock = 0;

    if (!ast) {
        return;
    }

    #define DO_INDENT do { if (!substmt) { do_indent(io); } } while (SDL_FALSE)

    switch (ast->ast.type) {
        case SDL_SHADER_AST_OP_POSITIVE:
        case SDL_SHADER_AST_OP_NEGATE:
        case SDL_SHADER_AST_OP_COMPLEMENT:
        case SDL_SHADER_AST_OP_NOT:
            fprintf(io, "%s", pre_unary[(typeint-SDL_SHADER_AST_OP_START_RANGE_UNARY)-1]);
            print_ast(io, SDL_TRUE, ast->unary.operand);
            break;

        case SDL_SHADER_AST_OP_PARENTHESES:
            fprintf(io, "(");
            print_ast(io, SDL_TRUE, ast->unary.operand);
            fprintf(io, ")");
            break;

        case SDL_SHADER_AST_OP_MULTIPLY:
        case SDL_SHADER_AST_OP_DIVIDE:
        case SDL_SHADER_AST_OP_MODULO:
        case SDL_SHADER_AST_OP_ADD:
        case SDL_SHADER_AST_OP_SUBTRACT:
        case SDL_SHADER_AST_OP_LSHIFT:
        case SDL_SHADER_AST_OP_RSHIFT:
        case SDL_SHADER_AST_OP_LESSTHAN:
        case SDL_SHADER_AST_OP_GREATERTHAN:
        case SDL_SHADER_AST_OP_LESSTHANOREQUAL:
        case SDL_SHADER_AST_OP_GREATERTHANOREQUAL:
        case SDL_SHADER_AST_OP_EQUAL:
        case SDL_SHADER_AST_OP_NOTEQUAL:
        case SDL_SHADER_AST_OP_BINARYAND:
        case SDL_SHADER_AST_OP_BINARYXOR:
        case SDL_SHADER_AST_OP_BINARYOR:
        case SDL_SHADER_AST_OP_LOGICALAND:
        case SDL_SHADER_AST_OP_LOGICALOR:
            print_ast(io, SDL_TRUE, ast->binary.left);
            fprintf(io, " %s ", binary[(typeint - SDL_SHADER_AST_OP_START_RANGE_BINARY) - 1]);
            print_ast(io, SDL_TRUE, ast->binary.right);
            break;

        case SDL_SHADER_AST_OP_DEREF_ARRAY:
            print_ast(io, SDL_TRUE, ast->binary.left);
            fprintf(io, "[");
            print_ast(io, SDL_TRUE, ast->binary.right);
            fprintf(io, "]");
            break;

        case SDL_SHADER_AST_OP_DEREF_STRUCT:
            print_ast(io, SDL_TRUE, ast->structderef.expr);
            fprintf(io, ".");
            fprintf(io, "%s", ast->structderef.field);
            break;

        case SDL_SHADER_AST_OP_CONDITIONAL:
            print_ast(io, SDL_TRUE, ast->ternary.left);
            fprintf(io, " ? ");
            print_ast(io, SDL_TRUE, ast->ternary.center);
            fprintf(io, " : ");
            print_ast(io, SDL_TRUE, ast->ternary.right);
            break;

        case SDL_SHADER_AST_OP_IDENTIFIER:
            fprintf(io, "%s", ast->identifier.name);
            break;

        case SDL_SHADER_AST_OP_INT_LITERAL:
            fprintf(io, "%lld", (long long) ast->intliteral.value);
            break;

        case SDL_SHADER_AST_OP_FLOAT_LITERAL: {
            const double f = ast->floatliteral.value;
            const long long flr = (long long) f;
            if (((float) flr) == f) {
                fprintf(io, "%lld.0", flr);
            } else {
                fprintf(io, "%.16g", f);
            }
            break;
        }

        case SDL_SHADER_AST_OP_BOOLEAN_LITERAL:
            fprintf(io, "%s", ast->boolliteral.value ? "true" : "false");
            break;

        case SDL_SHADER_AST_OP_CALLFUNC:
            fprintf(io, "%s", ast->fncall.fnname);
            fprintf(io, "(");
            if (ast->fncall.arguments) {
                SDL_SHADER_AstArgument *i;
                for (i = ast->fncall.arguments->head; i != NULL; i = i->next) {
                    print_ast(io, SDL_TRUE, i->arg);
                    if (i->next) {
                        fprintf(io, ", ");
                    }
                }
            }
            fprintf(io, ")");
            break;

        case SDL_SHADER_AST_STATEMENT_EMPTY:
        case SDL_SHADER_AST_STATEMENT_BREAK:
        case SDL_SHADER_AST_STATEMENT_CONTINUE:
        case SDL_SHADER_AST_STATEMENT_DISCARD:
            DO_INDENT;
            fprintf(io, "%s;%s", simple_stmt[(typeint-SDL_SHADER_AST_STATEMENT_START_RANGE)-1], nl);
            break;

        case SDL_SHADER_AST_STATEMENT_VARDECL:
            DO_INDENT;
            fprintf(io, "var ");
            print_ast(io, SDL_TRUE, ast->vardeclstmt.vardecl);
            if (ast->vardeclstmt.initializer) {
                fprintf(io, " = ");
                print_ast(io, SDL_TRUE, ast->vardeclstmt.initializer);
            }
            fprintf(io, ";%s", nl);
            break;

        case SDL_SHADER_AST_STATEMENT_DO:
            DO_INDENT;
            fprintf(io, "do\n");

            isblock = ast->dostmt.code->ast.type == SDL_SHADER_AST_STATEMENT_BLOCK;
            if (!isblock) { indent++; }
            print_ast(io, SDL_FALSE, ast->dostmt.code);
            if (!isblock) { indent--; }

            DO_INDENT;
            fprintf(io, "while ");
            print_ast(io, SDL_FALSE, ast->dostmt.condition);
            fprintf(io, ";\n");
            break;

        case SDL_SHADER_AST_STATEMENT_WHILE:
            DO_INDENT;
            fprintf(io, "while ");
            print_ast(io, SDL_FALSE, ast->whilestmt.condition);
            fprintf(io, "\n");

            isblock = ast->whilestmt.code->ast.type == SDL_SHADER_AST_STATEMENT_BLOCK;
            if (!isblock) { indent++; }
            print_ast(io, SDL_FALSE, ast->whilestmt.code);
            if (!isblock) { indent--; }
            break;

        case SDL_SHADER_AST_STATEMENT_FOR: {
            SDL_SHADER_AstForDetails *details = ast->forstmt.details;
            DO_INDENT;
            fprintf(io, "for (");
            if (details->initializer) {
                print_ast(io, SDL_TRUE, details->initializer);
            } else {
                fprintf(io, ";");
            }
            fprintf(io, " ");

            if (details->condition) {
                print_ast(io, SDL_TRUE, details->condition);
            }
            fprintf(io, "; ");

            if (details->step) {
                print_ast(io, SDL_TRUE, details->step);
            }
            fprintf(io, ")\n");

            isblock = ast->forstmt.code->ast.type == SDL_SHADER_AST_STATEMENT_BLOCK;
            if (!isblock) { indent++; }
            print_ast(io, SDL_FALSE, ast->forstmt.code);
            if (!isblock) { indent--; }
            break;
        }

        case SDL_SHADER_AST_STATEMENT_IF:
            DO_INDENT;
            fprintf(io, "if ");
            print_ast(io, SDL_TRUE, ast->ifstmt.condition);
            fprintf(io, "\n");
            isblock = ast->ifstmt.code->ast.type == SDL_SHADER_AST_STATEMENT_BLOCK;
            if (!isblock) { indent++; }
            print_ast(io, SDL_FALSE, ast->ifstmt.code);
            if (!isblock) { indent--; }

            if (ast->ifstmt.else_code) {
                printf("else\n");
                isblock = ast->ifstmt.else_code->ast.type == SDL_SHADER_AST_STATEMENT_BLOCK;
                if (!isblock) { indent++; }
                print_ast(io, SDL_FALSE, ast->ifstmt.else_code);
                if (!isblock) { indent--; }
            }
            break;

        case SDL_SHADER_AST_STATEMENT_SWITCH:
            DO_INDENT;
            fprintf(io, "switch ");
            print_ast(io, SDL_TRUE, ast->switchstmt.condition);
            fprintf(io, "\n");
            DO_INDENT;
            fprintf(io, "{\n");
            indent++;

            if (ast->switchstmt.cases) {
                SDL_SHADER_AstSwitchCase *i;
                for (i = ast->switchstmt.cases->head; i != NULL; i = i->next) {
                    DO_INDENT;
                    if (i->condition) {
                        fprintf(io, "case ");
                        print_ast(io, SDL_TRUE, i->condition);
                        fprintf(io, ":\n");
                    } else {
                        fprintf(io, "default:\n");
                    }
                    if (i->code) {
                        isblock = i->code->ast.type == SDL_SHADER_AST_STATEMENT_BLOCK;
                        if (!isblock) { indent++; }
                        print_ast(io, SDL_FALSE, i->code);
                        if (!isblock) { indent--; }
                        indent--;
                    }
                }
            }

            indent--;
            fprintf(io, "\n");
            DO_INDENT;
            fprintf(io, "}\n");
            break;

        case SDL_SHADER_AST_STATEMENT_RETURN:
            DO_INDENT;
            fprintf(io, "return");
            if (ast->returnstmt.value) {
                fprintf(io, " ");
                print_ast(io, SDL_TRUE, ast->returnstmt.value);
            }
            fprintf(io, ";%s", nl);
            break;

        case SDL_SHADER_AST_STATEMENT_BLOCK: {
            SDL_SHADER_AstStatement *i;
            DO_INDENT;
            fprintf(io, "{\n");
            indent++;
            for (i = ast->stmtblock.head; i != NULL; i = i->next) {
                print_ast(io, SDL_FALSE, i);
            }
            indent--;
            DO_INDENT;
            fprintf(io, "}\n");
            break;
        }

        case SDL_SHADER_AST_STATEMENT_PREINCREMENT:
            DO_INDENT;
            fprintf(io, "++");
            print_ast(io, SDL_TRUE, ast->incrementstmt.assignment);
            fprintf(io, ";%s", nl);
            break;

        case SDL_SHADER_AST_STATEMENT_POSTINCREMENT:
            DO_INDENT;
            print_ast(io, SDL_TRUE, ast->incrementstmt.assignment);
            fprintf(io, "++;%s", nl);
            break;

        case SDL_SHADER_AST_STATEMENT_PREDECREMENT:
            DO_INDENT;
            fprintf(io, "--");
            print_ast(io, SDL_TRUE, ast->incrementstmt.assignment);
            fprintf(io, ";%s", nl);
            break;

        case SDL_SHADER_AST_STATEMENT_POSTDECREMENT:
            DO_INDENT;
            print_ast(io, SDL_TRUE, ast->incrementstmt.assignment);
            fprintf(io, "--;%s", nl);
            break;

        case SDL_SHADER_AST_STATEMENT_FUNCTION_CALL:
            DO_INDENT;
            print_ast(io, SDL_TRUE, ast->fncallstmt.expr);
            fprintf(io, ";%s", nl);
            break;

        case SDL_SHADER_AST_STATEMENT_ASSIGNMENT:
            DO_INDENT;
            if (!ast->assignstmt.assignments) {
                SDL_assert(!"Assignment statement without targets? This is a bug!");
            } else {
                SDL_SHADER_AstAssignment *i;
                for (i = ast->assignstmt.assignments->head; i != NULL; i = i->next) {
                    print_ast(io, SDL_TRUE, i->expr);
                    fprintf(io, " %s ", assign[(typeint - SDL_SHADER_AST_STATEMENT_ASSIGNMENT_START_RANGE) - 1]);
                }
            }
            print_ast(io, SDL_TRUE, ast->assignstmt.value);
            fprintf(io, ";%s", nl);
            break;

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
            DO_INDENT;
            print_ast(io, SDL_TRUE, ast->compoundassignstmt.assignment);
            fprintf(io, " %s ", assign[(typeint - SDL_SHADER_AST_STATEMENT_ASSIGNMENT_START_RANGE) - 1]);
            print_ast(io, SDL_TRUE, ast->compoundassignstmt.value);
            fprintf(io, ";%s", nl);
            break;

        case SDL_SHADER_AST_TRANSUNIT_FUNCTION: {
            SDL_SHADER_AstFunction *fn = ast->fnunit.fn;
            SDL_SHADER_AstVarDeclaration *vardecl = fn->vardecl;
            DO_INDENT;

            fprintf(io, "function");
            if (vardecl->attribute) {
                print_ast(io, SDL_TRUE, vardecl->attribute);
            }
            fprintf(io, " ");

            if (vardecl->c_style) {
                fprintf(io, "%s %s(", vardecl->datatype_name ? vardecl->datatype_name : "void", vardecl->name);
            } else {
                fprintf(io, "%s(", vardecl->name);
            }

            if (!fn->params) {
                fprintf(io, "void");
            } else {
                SDL_SHADER_AstFunctionParam *i;
                for (i = fn->params->head; i != NULL; i = i->next) {
                    print_ast(io, SDL_TRUE, i->vardecl);
                    if (i->next) {
                        fprintf(io, ", ");
                    }
                }
            }
            fprintf(io, ")");

            if (!vardecl->c_style) {
                fprintf(io, " : %s", vardecl->datatype_name ? vardecl->datatype_name : "void");
            }

            fprintf(io, "\n");
            print_ast(io, SDL_FALSE, fn->code);
            break;
        }

        case SDL_SHADER_AST_VARIABLE_DECLARATION: {
            const SDL_SHADER_AstVarDeclaration *vardecl = &ast->vardecl;
            DO_INDENT;
            if (vardecl->c_style) {
                fprintf(io, "%s %s", vardecl->datatype_name, vardecl->name);
            } else {
                fprintf(io, "%s : %s", vardecl->name, vardecl->datatype_name);
            }

            if (vardecl->arraybounds) {
                SDL_SHADER_AstArrayBounds *i;
                for (i = vardecl->arraybounds->head; i != NULL; i = i->next) {
                    print_ast(io, SDL_TRUE, i);
                }
            }

            if (vardecl->attribute) {
                print_ast(io, SDL_TRUE, vardecl->attribute);
            }
            break;
        }

        case SDL_SHADER_AST_ARRAY_BOUNDS: {
            DO_INDENT;
            fprintf(io, "[");
            print_ast(io, SDL_TRUE, ast->arraybounds.size);
            fprintf(io, "]");
            break;
        }

        case SDL_SHADER_AST_TRANSUNIT_STRUCT:
            print_ast(io, SDL_FALSE, ast->structdeclunit.decl);
            break;

        case SDL_SHADER_AST_STRUCT_DECLARATION:
            fprintf(io, "struct %s\n", ast->structdecl.name);
            DO_INDENT;
            fprintf(io, "{\n");
            if (ast->structdecl.members) {
                SDL_SHADER_AstStructMember *i;
                indent++;
                for (i = ast->structdecl.members->head; i != NULL; i = i->next) {
                    print_ast(io, SDL_FALSE, i->vardecl);
                    fprintf(io, ";\n");
                }
                indent--;
            }
            DO_INDENT;
            fprintf(io, "};\n");
            break;


        case SDL_SHADER_AST_AT_ATTRIBUTE:
            fprintf(io, " @%s", ast->at_attribute.name);
            if (ast->at_attribute.has_argument) {
                fprintf(io, "(%lld)", (long long) ast->at_attribute.argument);
            }
            break;

        case SDL_SHADER_AST_SHADER:
            fprintf(io, "// begin shader\n\n");
            if (ast->shader.units) {
                SDL_SHADER_AstTranslationUnit *i;
                for (i = ast->shader.units->head; i != NULL; i = i->next) {
                    print_ast(io, SDL_FALSE, i);
                    fprintf(io, "\n");
                }
            }
            fprintf(io, "// end shader\n\n");
            break;

        default:
            SDL_assert(!"Unexpected AST type");
            break;
    }

    #undef DO_INDENT
}

static void print_ast_xml(FILE *io, const void *_ast)
{
    const SDL_SHADER_AstNode *ast = (const SDL_SHADER_AstNode *) _ast;
    const int typeint = ast ? ((int) ast->ast.type) : 0;

    if (!ast) {
        return;
    }

    #define DO_INDENT do_indent(io)

    switch (ast->ast.type) {
        case SDL_SHADER_AST_OP_POSITIVE:
        case SDL_SHADER_AST_OP_NEGATE:
        case SDL_SHADER_AST_OP_COMPLEMENT:
        case SDL_SHADER_AST_OP_NOT:
        case SDL_SHADER_AST_OP_PARENTHESES:
            DO_INDENT; fprintf(io, "<unary_expression operator='%s'>\n", pre_unary[(typeint-SDL_SHADER_AST_OP_START_RANGE_UNARY)-1]);
            indent++;
            print_ast_xml(io, ast->unary.operand);
            indent--;
            DO_INDENT; fprintf(io, "</unary_expression>\n");
            break;

        case SDL_SHADER_AST_OP_MULTIPLY:
        case SDL_SHADER_AST_OP_DIVIDE:
        case SDL_SHADER_AST_OP_MODULO:
        case SDL_SHADER_AST_OP_ADD:
        case SDL_SHADER_AST_OP_SUBTRACT:
        case SDL_SHADER_AST_OP_LSHIFT:
        case SDL_SHADER_AST_OP_RSHIFT:
        case SDL_SHADER_AST_OP_LESSTHAN:
        case SDL_SHADER_AST_OP_GREATERTHAN:
        case SDL_SHADER_AST_OP_LESSTHANOREQUAL:
        case SDL_SHADER_AST_OP_GREATERTHANOREQUAL:
        case SDL_SHADER_AST_OP_EQUAL:
        case SDL_SHADER_AST_OP_NOTEQUAL:
        case SDL_SHADER_AST_OP_BINARYAND:
        case SDL_SHADER_AST_OP_BINARYXOR:
        case SDL_SHADER_AST_OP_BINARYOR:
        case SDL_SHADER_AST_OP_LOGICALAND:
        case SDL_SHADER_AST_OP_LOGICALOR:
        case SDL_SHADER_AST_OP_DEREF_ARRAY:
            DO_INDENT; fprintf(io, "<binary_expression operator='%s'>\n", binary[(typeint - SDL_SHADER_AST_OP_START_RANGE_BINARY) - 1]);
            indent++;
            DO_INDENT; fprintf(io, "<left>\n");
            indent++;
            print_ast_xml(io, ast->binary.left);
            indent--;
            DO_INDENT; fprintf(io, "</left>\n");
            DO_INDENT; fprintf(io, "<right>\n");
            indent++;
            print_ast_xml(io, ast->binary.right);
            indent--;
            DO_INDENT; fprintf(io, "</right>\n");
            indent--;
            DO_INDENT; fprintf(io, "</binary_expression>\n");
            break;

        case SDL_SHADER_AST_OP_DEREF_STRUCT:
            DO_INDENT; fprintf(io, "<deref_struct_expression field='%s'>\n", ast->structderef.field);
            indent++;
            DO_INDENT; fprintf(io, "<object>\n");
            indent++;
            print_ast_xml(io, ast->structderef.expr);
            indent--;
            DO_INDENT; fprintf(io, "</object>\n");
            indent--;
            DO_INDENT; fprintf(io, "</deref_struct_expression>\n");
            break;

        case SDL_SHADER_AST_OP_CONDITIONAL:
            DO_INDENT; fprintf(io, "<ternary_expression operator='%s'>\n", "?");
            indent++;
            DO_INDENT; fprintf(io, "<left>\n");
            indent++;
            print_ast_xml(io, ast->ternary.left);
            indent--;
            DO_INDENT; fprintf(io, "</left>\n");
            DO_INDENT; fprintf(io, "<center>\n");
            indent++;
            print_ast_xml(io, ast->ternary.center);
            indent--;
            DO_INDENT; fprintf(io, "</center>\n");
            DO_INDENT; fprintf(io, "<right>\n");
            indent++;
            print_ast_xml(io, ast->ternary.right);
            indent--;
            DO_INDENT; fprintf(io, "</right>\n");
            indent--;
            DO_INDENT; fprintf(io, "</ternary_expression>\n");
            break;

        case SDL_SHADER_AST_OP_IDENTIFIER:
            DO_INDENT; fprintf(io, "<identifier_expression name='%s' />\n", ast->identifier.name);
            break;

        case SDL_SHADER_AST_OP_INT_LITERAL:
            DO_INDENT; fprintf(io, "<int_literal_expression value='%lld' />\n", (long long) ast->intliteral.value);
            break;

        case SDL_SHADER_AST_OP_FLOAT_LITERAL: {
            const double f = ast->floatliteral.value;
            const long long flr = (long long) f;
            DO_INDENT; fprintf(io, "<float_literal_expression value='");
            if (((float) flr) == f) {
                fprintf(io, "%lld.0", flr);
            } else {
                fprintf(io, "%.16g", f);
            }
            fprintf(io, "' />\n");
            break;
        }

        case SDL_SHADER_AST_OP_BOOLEAN_LITERAL:
            DO_INDENT; fprintf(io, "<boolean_literal_expression value='%s' />\n", ast->boolliteral.value ? "true" : "false");
            break;

        case SDL_SHADER_AST_OP_CALLFUNC:
            DO_INDENT; fprintf(io, "<function_call_expression name='%s'", ast->fncall.fnname);
            if (!ast->fncall.arguments) {
                fprintf(io, " />\n");
            } else {
                SDL_SHADER_AstArgument *i;
                fprintf(io, ">\n");
                indent++;
                DO_INDENT; fprintf(io, "<arguments>\n");
                indent++;
                for (i = ast->fncall.arguments->head; i != NULL; i = i->next) {
                    DO_INDENT; fprintf(io, "<argument>\n");
                    indent++;
                    print_ast_xml(io, i->arg);
                    indent--;
                    DO_INDENT; fprintf(io, "</argument>\n");
                }
                indent--;
                DO_INDENT; fprintf(io, "</arguments>\n");
                indent--;
                DO_INDENT; fprintf(io, "</function_call_expression>\n");
            }
            break;

        case SDL_SHADER_AST_STATEMENT_EMPTY:
            DO_INDENT; fprintf(io, "<empty_statement/>\n");
            break;

        case SDL_SHADER_AST_STATEMENT_BREAK:
        case SDL_SHADER_AST_STATEMENT_CONTINUE:
        case SDL_SHADER_AST_STATEMENT_DISCARD:
            DO_INDENT; fprintf(io, "<%s_statement/>\n", simple_stmt[(typeint-SDL_SHADER_AST_STATEMENT_START_RANGE)-1]);
            break;

        case SDL_SHADER_AST_STATEMENT_VARDECL:
            DO_INDENT; fprintf(io, "<variable_declaration_statement>\n");
            indent++;
            print_ast_xml(io, ast->vardeclstmt.vardecl);
            if (ast->vardeclstmt.initializer) {
                DO_INDENT; fprintf(io, "<variable_declaration_initializer>\n");
                indent++;
                print_ast_xml(io, ast->vardeclstmt.initializer);
                indent--;
                DO_INDENT; fprintf(io, "</variable_declaration_intializer>\n");
            }
            indent--;
            DO_INDENT; fprintf(io, "</variable_declaration_statement>\n");
            break;

        case SDL_SHADER_AST_STATEMENT_DO:
            DO_INDENT; fprintf(io, "<do_statement>\n");
            indent++;
            DO_INDENT; fprintf(io, "<code>\n");
            indent++;
            print_ast_xml(io, ast->dostmt.code);
            indent--;
            DO_INDENT; fprintf(io, "</code>\n");
            DO_INDENT; fprintf(io, "<condition>\n");
            indent++;
            print_ast_xml(io, ast->dostmt.condition);
            indent--;
            DO_INDENT; fprintf(io, "</condition>\n");
            indent--;
            DO_INDENT; fprintf(io, "</do_statement>\n");
            break;

        case SDL_SHADER_AST_STATEMENT_WHILE:
            DO_INDENT; fprintf(io, "<while_statement>\n");
            indent++;
            DO_INDENT; fprintf(io, "<condition>\n");
            indent++;
            print_ast_xml(io, ast->whilestmt.condition);
            indent--;
            DO_INDENT; fprintf(io, "</condition>\n");
            DO_INDENT; fprintf(io, "<code>\n");
            indent++;
            print_ast_xml(io, ast->whilestmt.code);
            indent--;
            DO_INDENT; fprintf(io, "</code>\n");
            indent--;
            DO_INDENT; fprintf(io, "</while_statement>\n");
            break;

        case SDL_SHADER_AST_STATEMENT_FOR: {
            SDL_SHADER_AstForDetails *details = ast->forstmt.details;
            DO_INDENT; fprintf(io, "<for_statement>\n");
            indent++;
            if (details->initializer) {
                DO_INDENT; fprintf(io, "<initializer>\n");
                indent++;
                print_ast_xml(io, details->initializer);
                indent--;
                DO_INDENT; fprintf(io, "</initializer>\n");
            }
            if (details->condition) {
                DO_INDENT; fprintf(io, "<condition>\n");
                indent++;
                print_ast_xml(io, details->condition);
                indent--;
                DO_INDENT; fprintf(io, "</condition>\n");
            }
            if (details->step) {
                DO_INDENT; fprintf(io, "<step>\n");
                indent++;
                print_ast_xml(io, details->step);
                indent--;
                DO_INDENT; fprintf(io, "</step>\n");
            }

            DO_INDENT; fprintf(io, "<code>\n");
            indent++;
            print_ast_xml(io, ast->forstmt.code);
            indent--;
            DO_INDENT; fprintf(io, "</code>\n");
            indent--;
            DO_INDENT; fprintf(io, "</for_statement>\n");
            break;
        }

        case SDL_SHADER_AST_STATEMENT_IF:
            DO_INDENT; fprintf(io, "<if_statement>\n");
            indent++;
            DO_INDENT; fprintf(io, "<condition>\n");
            indent++;
            print_ast_xml(io, ast->ifstmt.condition);
            indent--;
            DO_INDENT; fprintf(io, "</condition>\n");
            DO_INDENT; fprintf(io, "<code>\n");
            indent++;
            print_ast_xml(io, ast->ifstmt.code);
            indent--;
            DO_INDENT; fprintf(io, "</code>\n");
            if (ast->ifstmt.else_code) {
                DO_INDENT; fprintf(io, "<else_code>\n");
                indent++;
                print_ast_xml(io, ast->ifstmt.else_code);
                indent--;
                DO_INDENT; fprintf(io, "</else_code>\n");
            }
            indent--;
            DO_INDENT; fprintf(io, "</if_statement>\n");
            break;

        case SDL_SHADER_AST_STATEMENT_SWITCH:
            DO_INDENT; fprintf(io, "<switch_statement>\n");
            indent++;
            DO_INDENT; fprintf(io, "<condition>\n");
            indent++;
            print_ast_xml(io, ast->switchstmt.condition);
            indent--;
            DO_INDENT; fprintf(io, "</condition>\n");

            if (ast->switchstmt.cases) {
                SDL_SHADER_AstSwitchCase *i;
                DO_INDENT; fprintf(io, "<cases>\n");
                indent++;
                for (i = ast->switchstmt.cases->head; i != NULL; i = i->next) {
                    if (i->condition) {
                        DO_INDENT; fprintf(io, "<case>\n");
                        indent++;
                        if (i->condition) {
                            DO_INDENT; fprintf(io, "<condition>\n");
                            indent++;
                            print_ast_xml(io, i->condition);
                            indent--;
                            DO_INDENT; fprintf(io, "</condition>\n");
                        }
                        if (i->code) {
                            DO_INDENT; fprintf(io, "<code>\n");
                            indent++;
                            print_ast_xml(io, i->code);
                            indent--;
                            DO_INDENT; fprintf(io, "</code>\n");
                        }
                        indent--;
                        DO_INDENT; fprintf(io, "</case>\n");
                    } else {
                        DO_INDENT; fprintf(io, "<default_case>\n");
                        indent++;
                        if (i->code) {
                            DO_INDENT; fprintf(io, "<code>\n");
                            indent++;
                            print_ast_xml(io, i->code);
                            indent--;
                            DO_INDENT; fprintf(io, "</code>\n");
                        }
                        indent--;
                        DO_INDENT; fprintf(io, "</default_case>\n");
                    }
                }
                indent--;
                DO_INDENT; fprintf(io, "</cases>\n");
            }
            indent--;
            DO_INDENT; fprintf(io, "</switch_statement>\n");
            break;

        case SDL_SHADER_AST_STATEMENT_RETURN:
            DO_INDENT; fprintf(io, "<return_statement");
            if (!ast->returnstmt.value) {
                fprintf(io, "/>\n");
            } else {
                fprintf(io, ">\n");
                indent++;
                DO_INDENT; fprintf(io, "<value>\n");
                indent++;
                print_ast_xml(io, ast->returnstmt.value);
                indent--;
                DO_INDENT; fprintf(io, "</value>\n");
                indent--;
                DO_INDENT; fprintf(io, "</return_statement>\n");
            }
            break;

        case SDL_SHADER_AST_STATEMENT_BLOCK: {
            SDL_SHADER_AstStatement *i;
            DO_INDENT; fprintf(io, "<statement_block>\n");
            indent++;
            for (i = ast->stmtblock.head; i != NULL; i = i->next) {
                print_ast_xml(io, i);
            }
            indent--;
            DO_INDENT; fprintf(io, "</statement_block>\n");
            break;
        }

        case SDL_SHADER_AST_STATEMENT_PREINCREMENT:
            DO_INDENT; fprintf(io, "<statement_preincrement>\n");
            indent++;
            print_ast_xml(io, ast->incrementstmt.assignment);
            indent--;
            DO_INDENT; fprintf(io, "</statement_preincrement>\n");
            break;

        case SDL_SHADER_AST_STATEMENT_POSTINCREMENT:
            DO_INDENT; fprintf(io, "<statement_postincrement>\n");
            indent++;
            print_ast_xml(io, ast->incrementstmt.assignment);
            indent--;
            DO_INDENT; fprintf(io, "</statement_postincrement>\n");
            break;

        case SDL_SHADER_AST_STATEMENT_PREDECREMENT:
            DO_INDENT; fprintf(io, "<statement_predecrement>\n");
            indent++;
            print_ast_xml(io, ast->incrementstmt.assignment);
            indent--;
            DO_INDENT; fprintf(io, "</statement_predecrement>\n");
            break;

        case SDL_SHADER_AST_STATEMENT_POSTDECREMENT:
            DO_INDENT; fprintf(io, "<statement_postdecrement>\n");
            indent++;
            print_ast_xml(io, ast->incrementstmt.assignment);
            indent--;
            DO_INDENT; fprintf(io, "</statement_postdecrement>\n");
            break;

        case SDL_SHADER_AST_STATEMENT_FUNCTION_CALL:
            DO_INDENT; fprintf(io, "<function_call_statement name='%s'", ast->fncallstmt.expr->fnname);
            if (!ast->fncallstmt.expr->arguments) {
                fprintf(io, " />\n");
            } else {
                SDL_SHADER_AstArgument *i;
                fprintf(io, ">\n");
                indent++;
                for (i = ast->fncallstmt.expr->arguments->head; i != NULL; i = i->next) {
                    print_ast_xml(io, i->arg);
                }
                indent--;
                DO_INDENT; fprintf(io, "</function_call_statement>\n");
            }

            fprintf(io, "<function_call_statement name='%s'", ast->fncallstmt.expr->fnname);
            if (!ast->fncallstmt.expr->arguments) {
                fprintf(io, " />\n");
            } else {
                SDL_SHADER_AstArgument *i;
                fprintf(io, ">\n");
                indent++;
                DO_INDENT; fprintf(io, "<arguments>\n");
                indent++;
                for (i = ast->fncallstmt.expr->arguments->head; i != NULL; i = i->next) {
                    DO_INDENT; fprintf(io, "<argument>\n");
                    indent++;
                    print_ast_xml(io, i->arg);
                    indent--;
                    DO_INDENT; fprintf(io, "</argument>\n");
                }
                indent--;
                DO_INDENT; fprintf(io, "</arguments>\n");
                indent--;
                DO_INDENT; fprintf(io, "</function_call_statement>\n");
            }
            break;

        case SDL_SHADER_AST_STATEMENT_ASSIGNMENT:
            DO_INDENT; fprintf(io, "<assignment_statement>\n");
            indent++;
            if (!ast->assignstmt.assignments) {
                SDL_assert(!"Assignment statement without targets? This is a bug!");
            } else {
                SDL_SHADER_AstAssignment *i;
                DO_INDENT; fprintf(io, "<assignments>\n");
                indent++;
                for (i = ast->assignstmt.assignments->head; i != NULL; i = i->next) {
                    print_ast_xml(io, i->expr);
                }
                indent--;
                DO_INDENT; fprintf(io, "</assignments>\n");
            }
            DO_INDENT; fprintf(io, "<value>\n");
            indent++;
            print_ast_xml(io, ast->assignstmt.value);
            indent--;
            DO_INDENT; fprintf(io, "</value>\n");
            indent--;
            DO_INDENT; fprintf(io, "</assignment_statement>\n");
            break;

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
            DO_INDENT; fprintf(io, "<compound_assignment_statement operator='%s'>\n", assign[(typeint - SDL_SHADER_AST_STATEMENT_ASSIGNMENT_START_RANGE) - 1]);
            indent++;
            DO_INDENT; fprintf(io, "<assignment>\n");
            indent++;
            print_ast_xml(io, ast->compoundassignstmt.assignment);
            indent--;
            DO_INDENT; fprintf(io, "</assignment>\n");
            DO_INDENT; fprintf(io, "<value>\n");
            indent++;
            print_ast_xml(io, ast->compoundassignstmt.value);
            indent--;
            DO_INDENT; fprintf(io, "</value>\n");
            indent--;
            DO_INDENT; fprintf(io, "</compound_assignment_statement>\n");
            break;


        case SDL_SHADER_AST_TRANSUNIT_FUNCTION: {
            SDL_SHADER_AstFunction *fn = ast->fnunit.fn;
            SDL_SHADER_AstVarDeclaration *vardecl = fn->vardecl;
            DO_INDENT; fprintf(io, "<function name='%s' return_type='%s' c_style='%s'>", vardecl->name, vardecl->datatype_name ? vardecl->datatype_name : "void", vardecl->c_style ? "true" : "false");
            indent++;
            print_ast_xml(io, vardecl->attribute);
            if (fn->params) {
                SDL_SHADER_AstFunctionParam *i;
                DO_INDENT; fprintf(io, "<params>\n");
                indent++;
                for (i = fn->params->head; i != NULL; i = i->next) {
                    print_ast_xml(io, i->vardecl);
                }
                indent--;
                DO_INDENT; fprintf(io, "</params>\n");
            }
            print_ast_xml(io, vardecl->attribute);
            DO_INDENT; fprintf(io, "<code>\n");
            indent++;
            print_ast_xml(io, fn->code);
            indent--;
            DO_INDENT; fprintf(io, "</code>\n");
            indent--;
            DO_INDENT; fprintf(io, "</function>\n");
            break;
        }

        case SDL_SHADER_AST_TRANSUNIT_STRUCT:
            print_ast_xml(io, ast->structdeclunit.decl);
            break;

        case SDL_SHADER_AST_STRUCT_DECLARATION:
            DO_INDENT; fprintf(io, "<struct_declaration name='%s'>\n", ast->structdecl.name);
            indent++;
            if (ast->structdecl.members) {
                SDL_SHADER_AstStructMember *i;
                DO_INDENT; fprintf(io, "<struct_members>\n");
                indent++;
                for (i = ast->structdecl.members->head; i != NULL; i = i->next) {
                    print_ast_xml(io, i->vardecl);
                }
                indent--;
                DO_INDENT; fprintf(io, "</struct_members>\n");
            }
            indent--;
            DO_INDENT; fprintf(io, "</struct_declaration>\n");
            break;

        case SDL_SHADER_AST_VARIABLE_DECLARATION: {
            const SDL_SHADER_AstVarDeclaration *vardecl = &ast->vardecl;
            SDL_SHADER_AstArrayBounds *i;
            const SDL_bool flat = (vardecl->arraybounds || vardecl->attribute) ? SDL_FALSE : SDL_TRUE;
            DO_INDENT; fprintf(io, "<variable_declaration name='%s' datatype='%s' c_style='%s'%s>\n", vardecl->name, vardecl->datatype_name, vardecl->c_style ? "true" : "false", flat ? " /" : "");
            indent++;
            if (vardecl->arraybounds) {
                DO_INDENT; fprintf(io, "<array_bounds>\n");
                indent++;
                for (i = vardecl->arraybounds->head; i != NULL; i = i->next) {
                    print_ast_xml(io, i);
                }
                indent--;
                DO_INDENT; fprintf(io, "</array_bounds>\n");
            }
            print_ast_xml(io, vardecl->attribute);
            indent--;
            if (!flat) {
                DO_INDENT; fprintf(io, "</variable_declaration>\n");
            }
            break;
        }

        case SDL_SHADER_AST_ARRAY_BOUNDS:
            DO_INDENT; fprintf(io, "<dimension>\n");
            indent++;
            print_ast_xml(io, ast->arraybounds.size);
            indent--;
            DO_INDENT; fprintf(io, "</dimension>\n");
            break;

        case SDL_SHADER_AST_AT_ATTRIBUTE:
            DO_INDENT; fprintf(io, "<attribute name='%s'", ast->at_attribute.name);
            if (ast->at_attribute.has_argument) {
                fprintf(io, " value='%lld'", (long long) ast->at_attribute.argument);
            }
            fprintf(io, " />\n");
            break;

        case SDL_SHADER_AST_SHADER:
            DO_INDENT; fprintf(io, "<shader>\n");
            indent++;
            if (ast->shader.units) {
                SDL_SHADER_AstTranslationUnit *i;
                for (i = ast->shader.units->head; i != NULL; i = i->next) {
                    print_ast_xml(io, i);
                }
            }
            indent--;
            DO_INDENT; fprintf(io, "</shader>\n");
            break;

        default:
            SDL_assert(!"Unexpected AST type");
            break;
    }

    #undef DO_INDENT
}

static int preprocess(const SDL_SHADER_CompilerParams *params, const char *outfile, FILE *io)
{
    const SDL_SHADER_PreprocessData *pd;
    const char *srcprofile = NULL;  /* for now */
    int retval = 0;

    pd = SDL_SHADER_Preprocess(params, SDL_TRUE);

    if (pd->error_count > 0) {
        print_errors(pd->errors, pd->error_count);
    } else {
        if (pd->output != NULL) {
            const size_t len = pd->output_len;
            if ((len) && (fwrite(pd->output, len, 1, io) != 1)) {
                fprintf(stderr, " ... fwrite('%s') failed.\n", outfile);
            } else if ((outfile != NULL) && (fclose(io) == EOF)) {
                fprintf(stderr, " ... fclose('%s') failed.\n", outfile);
            } else {
                retval = 1;
            }
        }
    }

    SDL_SHADER_FreePreprocessData(pd);

    return retval;
}

static int ast(const SDL_SHADER_CompilerParams *params, const char *outfile, FILE *io)
{
    int retval = 0;
    const SDL_SHADER_AstData *ad = SDL_SHADER_ParseAst(params);
    
    if (ad->error_count > 0) {
        print_errors(ad->errors, ad->error_count);
    } else {
        print_ast(io, SDL_FALSE, ad->shader);
        if ((outfile != NULL) && (fclose(io) == EOF)) {
            fprintf(stderr, " ... fclose('%s') failed.\n", outfile);
        } else {
            retval = 1;
        }
    }

    SDL_SHADER_FreeAstData(ad);

    return retval;
}

static int ast_xml(const SDL_SHADER_CompilerParams *params, const char *outfile, FILE *io)
{
    int retval = 0;
    const SDL_SHADER_AstData *ad = SDL_SHADER_ParseAst(params);

    if (ad->error_count > 0) {
        print_errors(ad->errors, ad->error_count);
    } else {
        print_ast_xml(io, ad->shader);
        if ((outfile != NULL) && (fclose(io) == EOF)) {
            fprintf(stderr, " ... fclose('%s') failed.\n", outfile);
        } else {
            retval = 1;
        }
    }

    SDL_SHADER_FreeAstData(ad);

    return retval;
}

static int compile(const SDL_SHADER_CompilerParams *params, const char *outfile, FILE *io)
{
    const SDL_SHADER_CompileData *cd;
    int retval = 0;

    cd = SDL_SHADER_Compile(params);

    if (cd->error_count > 0) {
        print_errors(cd->errors, cd->error_count);
    } else {
        if (cd->output != NULL) {
            const size_t len = cd->output_len;
            if ((len) && (fwrite(cd->output, len, 1, io) != 1)) {
                fprintf(stderr, " ... fwrite('%s') failed.\n", outfile);
            } else if ((outfile != NULL) && (fclose(io) == EOF)) {
                fprintf(stderr, " ... fclose('%s') failed.\n", outfile);
            } else {
                retval = 1;
            }
        }
    }

    SDL_SHADER_FreeCompileData(cd);

    return retval;
}

typedef enum
{
    ACTION_UNKNOWN,
    ACTION_VERSION,
    ACTION_PREPROCESS,
    ACTION_AST,
    ACTION_AST_XML,
    ACTION_COMPILE,
} Action;


int main(int argc, char **argv)
{
    SDL_SHADER_CompilerParams params;
    Action action = ACTION_UNKNOWN;
    int retval = 1;
    const char *outfile = NULL;
    FILE *outio = NULL;
    int i;

    SDL_zero(params);
    params.srcprofile = NULL;
    params.filename = NULL;
    params.source = NULL;
    params.sourcelen = 0;
    params.defines = NULL;
    params.define_count = 0;
    params.system_include_paths = NULL;
    params.system_include_path_count = 0;
    params.local_include_paths = NULL;
    params.local_include_path_count = 0;
    params.include_open = NULL;
    params.include_close = NULL;
    params.allocate = UtilMalloc;
    params.deallocate = UtilFree;
    params.allocate_data = NULL;

    /* !!! FIXME: check for allocation failure! */
    params.local_include_paths = (const char **) SDL_malloc(sizeof (char *));
    params.local_include_paths[0] = ".";
    params.local_include_path_count = 1;

    /* !!! FIXME: clean this up. */
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-P") == 0) {
            if ((action != ACTION_UNKNOWN) && (action != ACTION_PREPROCESS)) {
                fail("Multiple actions specified");
            }
            action = ACTION_PREPROCESS;
        } else if (strcmp(arg, "-T") == 0) {
            if ((action != ACTION_UNKNOWN) && (action != ACTION_AST)) {
                fail("Multiple actions specified");
            }
            action = ACTION_AST;
        } else if (strcmp(arg, "-X") == 0) {
            if ((action != ACTION_UNKNOWN) && (action != ACTION_AST_XML)) {
                fail("Multiple actions specified");
            }
            action = ACTION_AST_XML;
        } else if (strcmp(arg, "-C") == 0) {
            if ((action != ACTION_UNKNOWN) && (action != ACTION_COMPILE)) {
                fail("Multiple actions specified");
            }
            action = ACTION_COMPILE;
        } else if ((strcmp(arg, "-V") == 0) || (strcmp(arg, "--version") == 0)) {
            if ((action != ACTION_UNKNOWN) && (action != ACTION_VERSION)) {
                fail("Multiple actions specified");
            }
            action = ACTION_VERSION;
        } else if (strcmp(arg, "-o") == 0) {
            if (outfile != NULL) {
                fail("multiple output files specified");
            }
            arg = argv[++i];
            if (arg == NULL) {
                fail("no filename after '-o'");
            }
            outfile = arg;
        } else if (strcmp(arg, "-I") == 0) {
            arg = argv[++i];
            if (arg == NULL) {
                fail("no path after '-I'");
            }
            /* !!! FIXME: check for allocation failure */
            params.local_include_paths = (const char **) SDL_realloc(params.local_include_paths, (params.local_include_path_count + 1) * sizeof (char *));
            params.local_include_paths[params.local_include_path_count] = arg;
            params.local_include_path_count++;
        } else if (strncmp(arg, "-D", 2) == 0) {
            SDL_SHADER_PreprocessorDefine *defs = (SDL_SHADER_PreprocessorDefine *) params.defines;
            char *ident = strdup(arg + 2);
            char *ptr = strchr(ident, '=');
            const char *val = "";

            arg += 2;

            if (ptr) {
                *ptr = '\0';
                val = ptr + 1;
            }

            defs = (SDL_SHADER_PreprocessorDefine *) SDL_realloc(defs, (params.define_count + 1) * sizeof (SDL_SHADER_PreprocessorDefine));
            if (defs == NULL) {
                fail("Out of memory");
            }
            defs[params.define_count].identifier = ident;
            defs[params.define_count].definition = val;
            params.defines = defs;
            params.define_count++;
        } else {
            if (params.filename != NULL) {
                fail("multiple input files specified");
            }
            params.filename = arg;
        }
    }

    if (action == ACTION_UNKNOWN) {
        action = ACTION_COMPILE;
    }

#if 0  /* !!! FIXME */
    if (action == ACTION_VERSION) {
        printf("sdl-shader-compiler, changeset %s\n", "!!! FIXME" /*SDL_SHADER_CHANGESET*/ );
        return 0;
    }
#endif

    if (params.filename == NULL) {
        fail("no input file specified");
    }

    params.source = (const char *) SDL_LoadFile(params.filename, &params.sourcelen);
    if (params.source == NULL) {
        fail("failed to read input file");  /* !!! FIXME: need failf, pass SDL_GetError(). */
    }

    outio = outfile ? fopen(outfile, "wb") : stdout;
    if (outio == NULL) {
        fail("failed to open output file");
    }

    if (action == ACTION_PREPROCESS) {
        retval = (!preprocess(&params, outfile, outio));
    } else if (action == ACTION_AST) {
        retval = (!ast(&params, outfile, outio));
    } else if (action == ACTION_AST_XML) {
        retval = (!ast_xml(&params, outfile, outio));
    } else if (action == ACTION_COMPILE) {
        retval = (!compile(&params, outfile, outio));
    }

    if ((retval != 0) && (outfile != NULL)) {
        remove(outfile);
    }

    SDL_free((void *) params.source);

    for (i = 0; i < params.define_count; i++) {
        SDL_free((void *) params.defines[i].identifier);
    }
    SDL_free((void *) params.defines);

    SDL_free(params.local_include_paths);

    return retval;
}

/* end of sdl-shader-compiler.c ... */

