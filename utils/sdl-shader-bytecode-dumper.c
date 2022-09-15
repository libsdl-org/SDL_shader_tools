#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>

#include "SDL_shader_bytecode.h"

typedef union Uint32_Float_Reinterpreter {
    Uint32 ui32;
    float f;
} Uint32_Float_Reinterpreter;

static void crc32_init(Uint32 *context)
{
    *context = (Uint32) 0xFFFFFFFF;
}

static void crc32_append(Uint32 *_crc, const Uint8 *buf, const Uint32 len)
{
    Uint32 crc = *_crc;
    Uint32 n;

    for (n = 0; n < len; n++) {
        Uint32 xorval = (Uint32) ((crc ^ buf[n]) & 0xFF);
        xorval = ((xorval & 1) ? (0xEDB88320 ^ (xorval >> 1)) : (xorval >> 1));
        xorval = ((xorval & 1) ? (0xEDB88320 ^ (xorval >> 1)) : (xorval >> 1));
        xorval = ((xorval & 1) ? (0xEDB88320 ^ (xorval >> 1)) : (xorval >> 1));
        xorval = ((xorval & 1) ? (0xEDB88320 ^ (xorval >> 1)) : (xorval >> 1));
        xorval = ((xorval & 1) ? (0xEDB88320 ^ (xorval >> 1)) : (xorval >> 1));
        xorval = ((xorval & 1) ? (0xEDB88320 ^ (xorval >> 1)) : (xorval >> 1));
        xorval = ((xorval & 1) ? (0xEDB88320 ^ (xorval >> 1)) : (xorval >> 1));
        xorval = ((xorval & 1) ? (0xEDB88320 ^ (xorval >> 1)) : (xorval >> 1));
        crc = xorval ^ (crc >> 8);
    }

    *_crc = crc;
}

static Uint32 crc32_finish(Uint32 *context)
{
    *context ^= 0xFFFFFFFF;
    return *context;
}

static Uint32 readui32(Uint8 **_ui8, size_t *_bclen)
{
    const Uint8 *ui8 = *_ui8;
    if (*_bclen < 4) {
        return 0;
    }

    *_bclen -= 4;
    *_ui8 += 4;
    return (((Uint32) ui8[0]) << 0) | (((Uint32) ui8[1]) << 8) | (((Uint32) ui8[2]) << 16) | (((Uint32) ui8[3]) << 24);
}

static int instruction_num_words_okay(const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words, Uint32 expected_words)
{
    num_words += 2;  /* we ate two for the tag and num_words */
    if (num_words != expected_words) {
        fprintf(stderr, "%s: Instruction %s should have %u words, has %u, corrupt file?\n", fname, opcode, (unsigned int) expected_words, (unsigned int) num_words);
        *bytecode += (num_words-2) * 4;
        *bclen -= (num_words-2) * 4;
        return 0;
    }
    return 1;
}

static int instruction_num_words_atleast(const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words, Uint32 expected_words)
{
    num_words += 2;  /* we ate two for the tag and num_words */
    if (num_words < expected_words) {
        fprintf(stderr, "%s: Instruction %s should have at least %u words, has %u, corrupt file?\n", fname, opcode, (unsigned int) expected_words, (unsigned int) num_words);
        *bytecode += (num_words-2) * 4;
        *bclen -= (num_words-2) * 4;
        return 0;
    }
    return 1;
}

static void print_indent(const int indent)
{
    int i;
    for (i = 0; i < indent; i++) {
        printf("    ");
    }
}


static int dump_bytecode_instructions(const int indent, const char *fname, Uint8 **bytecode, size_t *bclen, Uint32 num_words);


static int dump_bytecode_instruction_nop(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    const char *comma = " ";
    Uint32 i;

    print_indent(indent);
    printf("%s", opcode);

    for (i = 0; i < num_words; i++) {
        printf("%s0x%X", comma, (unsigned int) readui32(bytecode, bclen));
        comma = ", ";
    }
    printf("\n");

    return 1;
}

static int dump_bytecode_instruction_noinout(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_okay(fname, opcode, bytecode, bclen, num_words, 2)) {
        print_indent(indent);
        printf("%s\n", opcode);
        return 1;
    }
    return 0;
}

static int dump_bytecode_instruction_noinput(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_okay(fname, opcode, bytecode, bclen, num_words, 3)) {
        const Uint32 output = readui32(bytecode, bclen);
        print_indent(indent);
        printf("%s %%%u\n", opcode, (unsigned int) output);
        return 1;
    }
    return 0;
}

static int dump_bytecode_instruction_unary(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_okay(fname, opcode, bytecode, bclen, num_words, 4)) {
        const Uint32 output = readui32(bytecode, bclen);
        const Uint32 input = readui32(bytecode, bclen);
        print_indent(indent);
        printf("%s %%%u, %%%u\n", opcode, (unsigned int) output, (unsigned int) input);
        return 1;
    }
    return 0;
}

static int dump_bytecode_instruction_binary(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_okay(fname, opcode, bytecode, bclen, num_words, 5)) {
        const Uint32 output = readui32(bytecode, bclen);
        const Uint32 input1 = readui32(bytecode, bclen);
        const Uint32 input2 = readui32(bytecode, bclen);
        print_indent(indent);
        printf("%s %%%u, %%%u, %%%u\n", opcode, (unsigned int) output, (unsigned int) input1, (unsigned int) input2);
        return 1;
    }
    return 0;
}

static int dump_bytecode_instruction_ternary(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_okay(fname, opcode, bytecode, bclen, num_words, 6)) {
        const Uint32 output = readui32(bytecode, bclen);
        const Uint32 input1 = readui32(bytecode, bclen);
        const Uint32 input2 = readui32(bytecode, bclen);
        const Uint32 input3 = readui32(bytecode, bclen);
        print_indent(indent);
        printf("%s %%%u, %%%u, %%%u, %%%u\n", opcode, (unsigned int) output, (unsigned int) input1, (unsigned int) input2, (unsigned int) input3);
        return 1;
    }
    return 0;
}

static int dump_bytecode_instruction_literalint(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_okay(fname, opcode, bytecode, bclen, num_words, 4)) {
        const Uint32 output = readui32(bytecode, bclen);
        const Uint32 input = readui32(bytecode, bclen);
        print_indent(indent);
        printf("%s %%%u, %u\n", opcode, (unsigned int) output, (unsigned int) input);
        return 1;
    }
    return 0;
}

static int dump_bytecode_instruction_literalfloat(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_okay(fname, opcode, bytecode, bclen, num_words, 4)) {
        const Uint32 output = readui32(bytecode, bclen);
        const Uint32 input = readui32(bytecode, bclen);
        Uint32_Float_Reinterpreter cvt;
        cvt.ui32 = input;
        print_indent(indent);
        printf("%s %%%u, %f\n", opcode, (unsigned int) output, cvt.f);
        return 1;
    }
    return 0;
}

static int dump_bytecode_instruction_literalint4(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_okay(fname, opcode, bytecode, bclen, num_words, 7)) {
        const Uint32 output = readui32(bytecode, bclen);
        int i;
        print_indent(indent);
        printf("%s %%%u", opcode, (unsigned int) output);
        for (i = 0; i < 4; i++) {
            const Uint32 input = readui32(bytecode, bclen);
            printf(", %u", (unsigned int) input);
        }
        printf("\n");
        return 1;
    }
    return 0;
}

static int dump_bytecode_instruction_literalfloat4(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_okay(fname, opcode, bytecode, bclen, num_words, 7)) {
        const Uint32 output = readui32(bytecode, bclen);
        int i;
        for (i = 0; i < 4; i++) {
            const Uint32 input = readui32(bytecode, bclen);
            Uint32_Float_Reinterpreter cvt;
            cvt.ui32 = input;
            printf(", %f", cvt.f);
        }
        printf("\n");
        return 1;
    }
    return 0;
}

static int dump_bytecode_instruction_if(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_atleast(fname, opcode, bytecode, bclen, num_words, 4)) {
        const Uint32 input = readui32(bytecode, bclen);
        const Uint32 true_words = readui32(bytecode, bclen);
        const Uint32 false_words = (num_words - 2) - true_words;
        Uint8 *elseblock = *bytecode + (true_words * 4);
        const size_t bclenatelse = *bclen - (true_words * 4);
        Uint8 *endblock = elseblock + (false_words * 4);
        const size_t bclenatend = bclenatelse - (false_words * 4);
        int retval = 1;

        if (true_words > (num_words - 2)) {
            fprintf(stderr, "%s: Instruction %s should have %u words, but code block is %u words, corrupt file?\n", fname, opcode, (unsigned int) num_words, (unsigned int) true_words);
            *bytecode += (num_words-2) * 4;
            *bclen -= (num_words-2) * 4;
            return 0;
        }

        print_indent(indent);
        printf("%s %%%u\n", opcode, (unsigned int) input);
        if (!dump_bytecode_instructions(indent + 1, fname, bytecode, bclen, true_words)) {
            retval = 0;
        }

        /* in case dump_bytecode_instructions didn't move this correctly. */
        *bytecode = elseblock;
        *bclen = bclenatelse;

        if (false_words) {
            print_indent(indent);
            printf("ELSE\n");
            if (!dump_bytecode_instructions(indent + 1, fname, bytecode, bclen, false_words)) {
                retval = 0;
            }
        }

        /* in case dump_bytecode_instructions didn't move this correctly. */
        *bytecode = endblock;
        *bclen = bclenatend;

        print_indent(indent);
        printf("ENDIF\n");
        return retval;
    }
    return 0;
}

static int dump_bytecode_instruction_call(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_atleast(fname, opcode, bytecode, bclen, num_words, 4)) {
        const Uint32 fnid = readui32(bytecode, bclen);
        const Uint32 output = readui32(bytecode, bclen);
        Uint32 i;

        print_indent(indent);
        printf("%s $%u, %%%u", opcode, (unsigned int) fnid, (unsigned int) output);

        num_words -= 2;  /* remaining words are the inputs. */
        for (i = 0; i < num_words; i++) {
            printf(", %%%u", (unsigned int) readui32(bytecode, bclen));
        }
        printf("\n");
        return 1;
    }
    return 0;
}

static int dump_bytecode_instruction_loop(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_atleast(fname, opcode, bytecode, bclen, num_words, 2)) {
        Uint8 *endblock = *bytecode + (num_words * 4);
        const size_t bclenatend = *bclen - (num_words * 4);
        int retval = 1;

        print_indent(indent);
        printf("%s\n", opcode);

        if (!dump_bytecode_instructions(indent + 1, fname, bytecode, bclen, num_words)) {
            retval = 0;
        }

        /* in case dump_bytecode_instructions didn't move this correctly. */
        *bytecode = endblock;
        *bclen = bclenatend;

        print_indent(indent);
        printf("ENDLOOP\n");
        return retval;
    }
    return 0;
}

static int dump_bytecode_instruction_return(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_okay(fname, opcode, bytecode, bclen, num_words, 3)) {
        const Uint32 output = readui32(bytecode, bclen);
        print_indent(indent);
        printf("%s %%%u\n", opcode, (unsigned int) output);
        return 1;
    }
    return 0;
}

static int dump_bytecode_instruction_swizzle(const int indent, const char *fname, const char *opcode, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    if (instruction_num_words_okay(fname, opcode, bytecode, bclen, num_words, 5)) {
        const Uint32 output = readui32(bytecode, bclen);
        const Uint32 input = readui32(bytecode, bclen);
        const Uint32 swizvals = readui32(bytecode, bclen);
        print_indent(indent);
        printf("%s %%%u, %%%u, 0x%X\n", opcode, (unsigned int) output, (unsigned int) input, (unsigned int) swizvals);
        return 1;
    }
    return 0;
}

static int dump_bytecode_instruction(const int indent, const char *fname, Uint8 **bytecode, size_t *bclen, Uint32 *total_num_words)
{
    const Uint32 tag = readui32(bytecode, bclen);
    const Uint32 num_words = readui32(bytecode, bclen) - 2;

    *total_num_words -= 2;

    if (num_words > *total_num_words) {
        fprintf(stderr, "%s: Instruction %u goes past code block, corrupt file?\n", fname, (unsigned int) tag);
        *bytecode += *total_num_words * 4;
        *bclen -= *total_num_words * 4;
        *total_num_words = 0;
        return 0;
    }

    *total_num_words -= num_words;

    switch ((SDL_SHADER_BytecodeTag) tag) {
        case SDL_SHADER_BCTAG_OP_NOP: return dump_bytecode_instruction_nop(indent, fname, "NOP", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_NEGATE: return dump_bytecode_instruction_unary(indent, fname, "NEGATE", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_COMPLEMENT: return dump_bytecode_instruction_unary(indent, fname, "COMPLEMENT", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_NOT: return dump_bytecode_instruction_unary(indent, fname, "NOT", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_MULTIPLY: return dump_bytecode_instruction_binary(indent, fname, "MULTIPLY", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_DIVIDE: return dump_bytecode_instruction_binary(indent, fname, "DIVIDE", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_MODULO: return dump_bytecode_instruction_binary(indent, fname, "MODULO", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_ADD: return dump_bytecode_instruction_binary(indent, fname, "ADD", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_SUBTRACT: return dump_bytecode_instruction_binary(indent, fname, "SUBTRACT", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_SHIFTLEFT: return dump_bytecode_instruction_binary(indent, fname, "SHIFTLEFT", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_SHIFTRIGHT: return dump_bytecode_instruction_binary(indent, fname, "SHIFTRIGHT", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_LESSTHAN: return dump_bytecode_instruction_binary(indent, fname, "LESSTHAN", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_GREATERTHAN: return dump_bytecode_instruction_binary(indent, fname, "GREATERTHAN", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_LESSTHANOREQUAL: return dump_bytecode_instruction_binary(indent, fname, "LESSTHANOREQUAL", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_GREATERTHANOREQUAL: return dump_bytecode_instruction_binary(indent, fname, "GREATERTHANOREQUAL", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_EQUAL: return dump_bytecode_instruction_binary(indent, fname, "EQUAL", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_NOTEQUAL: return dump_bytecode_instruction_binary(indent, fname, "NOTEQUAL", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_BINARYAND: return dump_bytecode_instruction_binary(indent, fname, "BINARYAND", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_BINARYOR: return dump_bytecode_instruction_binary(indent, fname, "BINARYOR", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_BINARYXOR: return dump_bytecode_instruction_binary(indent, fname, "BINARYXOR", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_LOGICALAND: return dump_bytecode_instruction_binary(indent, fname, "LOGICALAND", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_LOGICALOR: return dump_bytecode_instruction_binary(indent, fname, "LOGICALOR", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_LITERALINT: return dump_bytecode_instruction_literalint(indent, fname, "LITERALINT", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_LITERALFLOAT: return dump_bytecode_instruction_literalfloat(indent, fname, "LITERALFLOAT", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_LITERALINT4: return dump_bytecode_instruction_literalint4(indent, fname, "LITERALINT4", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_LITERALFLOAT4: return dump_bytecode_instruction_literalfloat4(indent, fname, "LITERALFLOAT4", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_IF: return dump_bytecode_instruction_if(indent, fname, "IF", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_CALL: return dump_bytecode_instruction_call(indent, fname, "CALL", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_DISCARD: return dump_bytecode_instruction_noinout(indent, fname, "DISCARD", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_BREAK: return dump_bytecode_instruction_noinout(indent, fname, "BREAK", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_CONTINUE: return dump_bytecode_instruction_noinout(indent, fname, "CONTINUE", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_LOOP: return dump_bytecode_instruction_loop(indent, fname, "LOOP", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_RETURN: return dump_bytecode_instruction_return(indent, fname, "RETURN", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_PHI: return dump_bytecode_instruction_binary(indent, fname, "PHI", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_SWIZZLE: return dump_bytecode_instruction_swizzle(indent, fname, "SWIZZLE", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_ALL: return dump_bytecode_instruction_unary(indent, fname, "ALL", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_ANY: return dump_bytecode_instruction_unary(indent, fname, "ANY", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_ROUND: return dump_bytecode_instruction_unary(indent, fname, "ROUND", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_ROUNDEVEN: return dump_bytecode_instruction_unary(indent, fname, "ROUNDEVEN", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_MOD: return dump_bytecode_instruction_binary(indent, fname, "MOD", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_TRUNC: return dump_bytecode_instruction_unary(indent, fname, "TRUNC", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_ABS: return dump_bytecode_instruction_unary(indent, fname, "ABS", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_SIGN: return dump_bytecode_instruction_unary(indent, fname, "SIGN", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_FLOOR: return dump_bytecode_instruction_unary(indent, fname, "FLOOR", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_CEIL: return dump_bytecode_instruction_unary(indent, fname, "CEIL", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_FRACT: return dump_bytecode_instruction_unary(indent, fname, "FRACT", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_RADIANS: return dump_bytecode_instruction_unary(indent, fname, "RADIANS", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_DEGREES: return dump_bytecode_instruction_unary(indent, fname, "DEGREES", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_SIN: return dump_bytecode_instruction_unary(indent, fname, "SIN", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_COS: return dump_bytecode_instruction_unary(indent, fname, "COS", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_TAN: return dump_bytecode_instruction_unary(indent, fname, "TAN", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_ASIN: return dump_bytecode_instruction_unary(indent, fname, "ASIN", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_ACOS: return dump_bytecode_instruction_unary(indent, fname, "ACOS", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_ATAN: return dump_bytecode_instruction_unary(indent, fname, "ATAN", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_SINH: return dump_bytecode_instruction_unary(indent, fname, "SINH", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_COSH: return dump_bytecode_instruction_unary(indent, fname, "COSH", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_TANH: return dump_bytecode_instruction_unary(indent, fname, "TANH", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_ASINH: return dump_bytecode_instruction_unary(indent, fname, "ASINH", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_ACOSH: return dump_bytecode_instruction_unary(indent, fname, "ACOSH", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_ATANH: return dump_bytecode_instruction_unary(indent, fname, "ATANH", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_ATAN2: return dump_bytecode_instruction_binary(indent, fname, "ATAN2", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_POW: return dump_bytecode_instruction_binary(indent, fname, "POW", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_EXP: return dump_bytecode_instruction_unary(indent, fname, "EXP", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_LOG: return dump_bytecode_instruction_unary(indent, fname, "LOG", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_EXP2: return dump_bytecode_instruction_unary(indent, fname, "EXP2", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_LOG2: return dump_bytecode_instruction_unary(indent, fname, "LOG2", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_SQRT: return dump_bytecode_instruction_unary(indent, fname, "SQRT", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_RSQRT: return dump_bytecode_instruction_unary(indent, fname, "RSQRT", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_MIN: return dump_bytecode_instruction_binary(indent, fname, "MIN", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_MAX: return dump_bytecode_instruction_binary(indent, fname, "MAX", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_CLAMP: return dump_bytecode_instruction_ternary(indent, fname, "CLAMP", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_MIX: return dump_bytecode_instruction_ternary(indent, fname, "MIX", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_STEP: return dump_bytecode_instruction_ternary(indent, fname, "STEP", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_SMOOTHSTEP: return dump_bytecode_instruction_ternary(indent, fname, "SMOOTHSTEP", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_MAD: return dump_bytecode_instruction_ternary(indent, fname, "MAD", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_FREXP: return dump_bytecode_instruction_binary(indent, fname, "FREXP", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_LDEXP: return dump_bytecode_instruction_binary(indent, fname, "LDEXP", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_LEN: return dump_bytecode_instruction_unary(indent, fname, "LEN", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_DISTANCE: return dump_bytecode_instruction_binary(indent, fname, "DISTANCE", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_DOT: return dump_bytecode_instruction_binary(indent, fname, "DOT", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_CROSS: return dump_bytecode_instruction_binary(indent, fname, "CROSS", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_NORMALIZE: return dump_bytecode_instruction_unary(indent, fname, "NORMALIZE", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_FACEFORWARD: return dump_bytecode_instruction_ternary(indent, fname, "FACEFORWARD", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_REFLECT: return dump_bytecode_instruction_binary(indent, fname, "REFLECT", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_REFRACT: return dump_bytecode_instruction_ternary(indent, fname, "REFRACT", bytecode, bclen, num_words);
        case SDL_SHADER_BCTAG_OP_TRANSPOSE: return dump_bytecode_instruction_unary(indent, fname, "TRANSPOSE", bytecode, bclen, num_words);
        //case SDL_SHADER_BCTAG_OP_SAMPLE: return dump_bytecode_instruction_sample(indent, fname, "SAMPLE", bytecode, bclen, num_words);
        default: break;
    }

    fprintf(stderr, "%s: Unknown instruction %u, skipping\n", fname, (unsigned int) tag);
    *bytecode += num_words * 4;
    *bclen -= num_words * 4;
    return 0;
}

static int dump_bytecode_instructions(const int indent, const char *fname, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    int retval = 1;
    while (num_words >= 2) {
        if (!dump_bytecode_instruction(indent, fname, bytecode, bclen, &num_words)) {
            retval = 0;
        }
    }

    if (num_words) {
        fprintf(stderr, "%s: extra bytes at end of code block, corrupt file?\n", fname);
        retval = 0;
        bytecode += num_words * 4;
        *bclen -= num_words * 4;
        num_words = 0;
    }

    return retval;
}

static const char *fntypestr(const Uint32 fntype)
{
    switch (fntype) {
        /* !!! FIXME: need an enum here */
        case 0x0: return "";
        case 0x1: return " @vertex";
        case 0x2: return " @fragment";
        default: break;
    }
    return "unknown";
}

static int dump_bytecode_function(const Uint32 fnid, const char *fname, Uint8 **bytecode, size_t *bclen, Uint32 num_words)
{
    const Uint32 fntype = readui32(bytecode, bclen);  // currently: 0x0 for generic function, 0x1 for vertex, 0x2 for fragment.
    const Uint32 namelen = readui32(bytecode, bclen);
    const char *name;
    int retval;

    num_words -= 2;
    if (namelen > num_words) {
        fprintf(stderr, "%s: Function with too-long name, corrupt file?\n", fname);
        *bytecode += num_words;
        *bclen -= num_words * 4;
        return 0;
    }

    num_words -= namelen;
    name = namelen ? *bytecode : NULL;
    *bytecode += namelen * 4;
    *bclen -= namelen * 4;

    /* !!! FIXME: make sure string has a null-terminator */
    printf("$%u = FUNCTION%s%s%s\n", (unsigned int) fnid, name ? " " : "", name ? name : "", fntypestr(fntype));

    // !!! FIXME:    Inputs inputs;  // details of arguments to the function (see below).
    // !!! FIXME:    Outputs outputs;  // details of outputs from the function (see below).

    retval = dump_bytecode_instructions(1, fname, bytecode, bclen, num_words);
    printf("ENDFUNCTION\n\n");
}

static int dump_bytecode_from_buffer(const char *fname, Uint8 *bytecode, size_t bclen)
{
    int retval = 1;
    Uint32 version;
    Uint32 crc32;
    Uint32 actual_crc32;
    Uint32 fnid = 0;

    if (bclen < 20) {
        fprintf(stderr, "%s: not a shader bytecode file (too short)\n", fname);
        return 0;
    } else if (memcmp(bytecode, SDL_SHADER_BYTECODE_MAGIC, 12) != 0) {
        fprintf(stderr, "%s: not a shader bytecode file (wrong magic)\n", fname);
        return 0;
    } else if ((version = readui32(&bytecode, &bclen)) > SDL_SHADER_BYTECODE_VERSION) {
        fprintf(stderr, "%s: shader bytecode format %u is not supported\n", fname, (unsigned int) version);
        return 0;
    }

    crc32 = readui32(&bytecode, &bclen);

    crc32_init(&actual_crc32);
    crc32_append(&actual_crc32, bytecode, bclen);
    crc32_finish(&actual_crc32);

    printf("%s: shader bytecode format %u, crc32 0x%X (checksum is %s)\n\n", fname, (unsigned int) version, (unsigned int) crc32, (crc32 == actual_crc32) ? "good" : "BAD");

    while (bclen >= 8) {
        const Uint32 tag = readui32(&bytecode, &bclen);
        const Uint32 num_words = readui32(&bytecode, &bclen);
        const size_t remaining_bytes = (((size_t) num_words) - 2) * 4;
        if (remaining_bytes > bclen) {
            fprintf(stderr, "%s: section with tag %u goes past eof, corrupt file?\n", fname, (unsigned int) tag);
            retval = 0;
            bclen = 0;
            break;
        } else if (tag == SDL_SHADER_BCTAG_FUNCTION) {
            if (!dump_bytecode_function(fnid, fname, &bytecode, &bclen, num_words)) { retval = 0; break; }
            fnid++;
        /*} else if (tag == SDL_SHADER_BCTAG_DEBUGTABLE) {   !!! FIXME
            if (!dump_bytecode_debug_table(fname, bytecode, bclen, num_words)) { retval = 0; break; }*/
        } else {
            fprintf(stderr, "%s: Unexpected tag %u (should have been function or debug table), corrupt file? Skipping section.\n", fname, (unsigned int) tag);
            retval = 0;
            bclen -= remaining_bytes;
            bytecode += remaining_bytes;
            break;
        }
    }

    if (bclen > 0) {
        fprintf(stderr, "%s: %u extra bytes at end of file, corrupt file?\n", fname, (unsigned int) bclen);
        retval = 0;
    }

    return retval;
}

static int dump_bytecode_from_stdio(const char *fname, FILE *io)
{
    Uint8 *bytecode = NULL;
    size_t allocated = 0;
    int retval = 0;

    while (1) {
        const size_t blocklen = 4096;
        const size_t new_allocated = allocated + blocklen;
        void *ptr = realloc(bytecode, new_allocated);
        size_t br;

        if (!ptr) {
            fprintf(stderr, "%s: Out of memory.\n", fname);
            free(bytecode);
            return 0;
        }

        br = fread(bytecode + allocated, 1, blocklen, io);
        allocated += br;
        if (br < blocklen) {
            break;
        }
    }

    if (ferror(io)) {
        fprintf(stderr, "%s: read error: %s\n", fname, strerror(errno));
    } else {
        retval = dump_bytecode_from_buffer(fname, bytecode, allocated);
    }

    free(bytecode);
    return retval;
}

static int dump_bytecode(const char *fname)
{
    int retval = 0;
    if (strcmp(fname, "-") == 0) {
        retval = dump_bytecode_from_stdio("stdin", stdin);
    } else {
        FILE *io = fopen(fname, "rb");
        if (!io) {
            fprintf(stderr, "Failed to open '%s': %s\n", fname, strerror(errno));
        } else {
            retval = dump_bytecode_from_stdio(fname, io);
            fclose(io);
        }
    }

    return retval;
}

int main(int argc, char **argv)
{
    int retval = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (!dump_bytecode(argv[i])) {
            retval = 1;
        }
    }

    return retval;
}

/* end of sdl-shader-bytecode-dumper.c ... */

