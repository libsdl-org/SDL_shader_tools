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

static void print_ast(FILE *io, const SDL_bool substmt, const void *_ast)
{
    const SDL_SHADER_AstNode *ast = (const SDL_SHADER_AstNode *) _ast;
    const char *nl = substmt ? "" : "\n";
    const int typeint = ast ? ((int) ast->ast.type) : 0;
    static int indent = 0;
    int isblock = 0;
    int indenti;

    /* These _HAVE_ to be in the same order as SDL_SHADER_AstNodeType! */
    static const char *binary[] = {
        "*", "/", "%", "+", "-", "<<", ">>", "<", ">", "<=", ">=", "==", "!=", "&", "^", "|", "&&", "||"
    };

    static const char *assign[] = {
        "=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=", "|="
    };

    static const char *pre_unary[] = { "++", "--", "+", "-", "~", "!" };
    static const char *post_unary[] = { "++", "--" };
    static const char *simple_stmt[] = { "", "break", "continue", "discard" };
    static const char *inpmod[] = { "", "in ", "out ", "in out ", "uniform " };
    static const char *fnstorage[] = { "", "inline " };

    if (!ast) {
        return;
    }

    #define DO_INDENT do { \
        if (!substmt) { for (indenti = 0; indenti < indent; indenti++) fprintf(io, "    "); } \
    } while (SDL_FALSE)

    switch (ast->ast.type) {
        case SDL_SHADER_AST_OP_PREINCREMENT:
        case SDL_SHADER_AST_OP_PREDECREMENT:
        case SDL_SHADER_AST_OP_POSITIVE:
        case SDL_SHADER_AST_OP_NEGATE:
        case SDL_SHADER_AST_OP_COMPLEMENT:
        case SDL_SHADER_AST_OP_NOT:
            fprintf(io, "%s", pre_unary[(typeint-SDL_SHADER_AST_OP_START_RANGE_UNARY)-1]);
            print_ast(io, SDL_TRUE, ast->unary.operand);
            break;

        case SDL_SHADER_AST_OP_POSTINCREMENT:
        case SDL_SHADER_AST_OP_POSTDECREMENT:
            print_ast(io, SDL_TRUE, ast->unary.operand);
            fprintf(io, "%s", post_unary[typeint-SDL_SHADER_AST_OP_POSTINCREMENT]);
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

        case SDL_SHADER_AST_STATEMENT_VARDECL: {
            SDL_SHADER_AstVarDeclaration *vardecl = ast->vardeclstmt.vardecl;
            DO_INDENT;
            fprintf(io, "var %s %s", vardecl->datatype_name, vardecl->name);
            if (vardecl->initializer) {
                fprintf(io, " = ");
                print_ast(io, SDL_TRUE, vardecl->initializer);
            }
            fprintf(io, ";%s", nl);
            break;
        }

        case SDL_SHADER_AST_STATEMENT_DO:
            DO_INDENT;
            fprintf(io, "do\n");

            isblock = ast->dostmt.code->ast.type == SDL_SHADER_AST_STATEMENT_BLOCK;
            if (!isblock) { indent++; }
            print_ast(io, SDL_FALSE, ast->dostmt.code);
            if (!isblock) { indent--; }

            DO_INDENT;
            fprintf(io, "while (");
            print_ast(io, SDL_FALSE, ast->dostmt.condition);
            fprintf(io, ");\n");
            break;

        case SDL_SHADER_AST_STATEMENT_WHILE:
            DO_INDENT;
            fprintf(io, "while (");
            print_ast(io, SDL_FALSE, ast->whilestmt.condition);
            fprintf(io, ")\n");

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
                print_ast(io, SDL_TRUE, &details->initializer->ast);
            }
            fprintf(io, "; ");

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
            fprintf(io, "if (");
            print_ast(io, SDL_TRUE, ast->ifstmt.condition);
            fprintf(io, ")\n");
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
            fprintf(io, "switch (");
            print_ast(io, SDL_TRUE, ast->switchstmt.condition);
            fprintf(io, ")\n");
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

        case SDL_SHADER_AST_STATEMENT_ASSIGNMENT: {
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
        }

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
            DO_INDENT;
            fprintf(io, "function %s %s(", fn->datatype_name, fn->name);
            if (!fn->params) {
                fprintf(io, "void");
            } else {
                SDL_SHADER_AstFunctionParam *i;
                for (i = fn->params->head; i != NULL; i = i->next) {
                    fprintf(io, "%s %s", i->datatype_name, i->name);
                    print_ast(io, SDL_TRUE, i->attribute);
                    if (i->next) {
                        fprintf(io, ", ");
                    }
                }
            }
            fprintf(io, ")");
            print_ast(io, SDL_TRUE, fn->attribute);
            fprintf(io, "\n");
            print_ast(io, SDL_FALSE, fn->code);
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
                    DO_INDENT;
                    fprintf(io, "%s %s", i->datatype_name, i->name);
                    if (i->arraysize) {
                        fprintf(io, "[");
                        print_ast(io, SDL_TRUE, i->arraysize);
                        fprintf(io, "]");
                    }
                    print_ast(io, SDL_TRUE, i->attribute);
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
    const SDL_SHADER_AstData *ad;
    int retval = 0;

    ad = SDL_SHADER_ParseAst(params);
    
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

