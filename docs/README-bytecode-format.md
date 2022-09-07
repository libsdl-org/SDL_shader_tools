# SDLSL bytecode format

## First off

This is a work in progress (even if all the `FIXME write me` parts are gone,
none of this is locked down yet).


## The basic idea

- It's fairly high-level to accomodate various target needs.
- It's not important that it obfuscate original shader sources well.
- It deals with variables in SSA form, to accomodate SPIR-V, etc.
- Has optional debug table (can be included in binary or built separately).
- All fields are in little-endian byte order, and aligned/padded to 32-bits.
- Includes magic id, version number, checksum.
- Everything after the initial header is a set of 32-bit words, that contains
  a 32-bit word of its own word count.


## Structure of bytecode file

The first data in the file MUST be a Header...

    struct Header {
        Uint8 magic[12];  // always "SDLSHADERBC\0"
        Uint32 version;   // format version of this file, initial version is 1.
        Uint32 crc32;     // CRC-32 of whole file, starting after this Uint32.
    };

Next comes any Functions. Functions MUST be the first thing after the Header,
one after another, and Functions MUST NOT appear anywhere else in the file.

Note that Functions do not always have name strings associated with them; if
they aren't exported (by having a `@vertex` or `@fragment` attribute) and
there is no debug table included with the bytecode, the function's name is
unavailable in this file.

    struct Function {
        Uint32 tag;  // always 0x00000001
        Uint32 num_words;  // number of 32-bit words this struct uses.
        Uint32 fntype;  // currently: 0x0 for generic function, 0x1 for vertex, 0x2 for fragment.
        String name;  // name of function. name.num_words==0 (empty string) if not exported (see below).
        Inputs inputs;  // details of arguments to the function (see below).
        Outputs outputs;  // details of outputs from the function (see below).
        Uint32 code[];  // instructions that make up this function (see below).
    };

Functions (and other things) use this format for contained string data:

    struct String {
        Uint32 num_words;  // number of 32-bit words this struct uses.
        Uint32 data[]; // num_words*4 of UTF-8 string data, NULL-terminated. Padded with zeroes to end on 32-bit boundary.
    };

Function inputs are detailed like this:

    struct Inputs {
        WRITE ME
    };

Function outputs are detailed like this:

    struct Outputs {
        WRITE ME
    };

Function code is the remainder of the words in the Function struct. It is
a series of Instructions (see below).


## Instructions

Each instruction is layed out like:

    struct Instruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // number of 32-bit words this struct uses.
        Uint32 operands[];  // instructions are variable size
    };



Here are the current list of instructions. Note that there are _not_ separate
opcodes for float vs int vs vector; the system manages this based on what is
provided as inputs (!!! FIXME: this might be a bad idea.)


### Miscellenous instructions

- NOP [...]: A no-op, ignored. Can have as many operands as it likes; their
  contents are ignored. This can be used to leave space in a bytecode
  file for something else to be filled in later.


### Basic math and logic instructions

All of these instructions look like this...

    struct BinaryOperationInstruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // always 5.
        Uint32 output;  // SSA id of where to store the results (zero to not store).
        Uint32 input1;  // SSA id of first operand.
        Uint32 input2;  // SSA id of second operand.
    };

...or this...

    struct UnaryOperationInstruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // always 4.
        Uint32 output;  // SSA id of where to store the results (zero to not store).
        Uint32 input;  // SSA id of operand.
    };

Each non-zero `output` must be a unique value that becomes a new SSA id, which
can be used in future calculations.

- NEGATE %output, %input: `%output = - %input;`
- COMPLEMENT %output, %input: `%output = ~ %input;`
- NOT %output, %input: `%output = ! %input;`
- MULTIPLY %output, %input1, %input2: `%output = %input1 * %input2;`
- DIVIDE %output, %input1, %input2: `%output = %input1 / %input2;`
- MODULO %output, %input1, %input2: `%output = %input1 % %input2;`
- ADD %output, %input1, %input2: `%output = %input1 + %input2;`
- SUBTRACT %output, %input1, %input2: `%output = %input1 - %input2;`
- LSHIFT %output, %input1, %input2: `%output = %input1 << %input2;`
- RSHIFT %output, %input1, %input2: `%output = %input1 >> %input2;`
- LESSTHAN %output, %input1, %input2: `%output = %input1 < %input2;`
- GREATERTHAN %output, %input1, %input2: `%output = %input1 > %input2;`
- LESSTHANOREQUAL %output, %input1, %input2: `%output = %input1 <= %input2;`
- GREATERTHANOREQUAL %output, %input1, %input2: `%output = %input1 >= %input2;`
- EQUAL %output, %input1, %input2: `%output = %input1 == %input2;`
- NOTEQUAL %output, %input1, %input2: `%output = %input1 != %input2;`
- BINARYAND %output, %input1, %input2: `%output = %input1 & %input2;`
- BINARYOR %output, %input1, %input2: `%output = %input1 | %input2;`
- BINARYXOR %output, %input1, %input2: `%output = %input1 ^ %input2;`
- LOGICALAND %output, %input1, %input2: `%output = %input1 && %input2;`
- LOGICALOR %output, %input1, %input2: `%output = %input1 || %input2;`


### Conversion

These are used to convert between data types (int to float, etc).

*** !!! FIXME: write me ***

- CONVERT: Move an value to a different type.


### Literals

These are used to generate SSA ids for literal values.

- LITERALINT: Assign an int literal constant value to an SSA id.

    struct IntLiteralInstruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // always 4.
        Uint32 output;  // SSA id of where to store the results (zero to not store).
        Uint32 value;  // literal value to assign.
    };

- LITERALFLOAT: Assign a float literal constant value to an SSA id.

    struct IntLiteralInstruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // always 4.
        Uint32 output;  // SSA id of where to store the results (zero to not store).
        Uint32 value;  // literal value to assign (this is an IEEE float, stored in 32 bits of space).
    };

- LITERALINT4: Assign an int4 literal constant value to an SSA id.

    struct Int4LiteralInstruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // always 7.
        Uint32 output;  // SSA id of where to store the results (zero to not store).
        Uint32 value[4];  // literal values to assign (in order: x, y, z, w...or r, g, b, a).
    };

- LITERALFLOAT4: Assign a float4 literal constant value to an SSA id.

    struct Float4LiteralInstruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // always 7.
        Uint32 output;  // SSA id of where to store the results (zero to not store).
        Uint32 value[4];  // literal values to assign (in order: x, y, z, w...or r, g, b, a) (IEEE floats, stored in 32 bits of space each)
    };


*** !!! FIXME: matrices? ***


### Memory i/o

*** !!! FIXME: decide what these should look like. ***

- PEEK
- POKE


### Control flow

Note that these operate as high-level constructs, because we are avoiding
jump instructions and code addresses (so we can convert back to things that
_need_ high-level constructs, like GLSL or Shader Model 3 bytecode, etc).

- IF: conditional branching.

    struct IfInstruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // number of 32-bit words this struct uses.
        Uint32 input;  // condition to test
        Uint32 num_code_words;  // number of 32-bit words in `code`
        Uint32 code_if_true[];  // num_code_words of 32-bit words; block of instructions if input is true. Can be zero words long.
        Uint32 code_if_false[];  // num_words-num_code_words of 32-bit words; block of instructions if input is false (the "else" block). Can be zero words long.
    };

- CALL: Call a function.

    struct CallInstruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // number of 32-bit words this struct uses.
        Uint32 fn;  // index of function to call.
        Uint32 output;  // SSA id of where to store the results (zero to not store).
        Uint32 inputs[];  // num_words-4 words of SSA ids to use as function arguments.
    };

- DISCARD: Only valid in fragment shaders.

    struct DiscardInstruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // always 2.
    };

- BREAK: Leave innermost LOOP or SWITCH.

    struct BreakInstruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // always 2.
    };

- CONTINUE: Go to end of innermost LOOP, to loop again.

- LOOP: A structured loop. `do`, `while` and `for` all use this. This simply
  repeats the code block until an exit instruction occurs (a BREAK, RETURN,
  DISCARD, etc).

    struct LoopInstruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // number of 32-bit words this struct uses.
        Uint32 code[];  // num_words-2 words of instructions to run once at start of loop.
    };

- SWITCH: Jump to a code block based on a constant value. This might dither
  down to a collection of if-statements on some targets!

    struct SwitchInstruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // number of 32-bit words this struct uses.
        Uint32 input;   // SSA id of value to test against.
        Uint32 num_cases;  // number of 32-bit words in `cases`
        Uint32 cases[];  // num_cases words of SSA ids to compare to `input`.
        Uint32 offsets[];  // num_cases words of offsets into `code` for each case.
        Uint32 code[];  // block of instructions,
    };

- RETURN: Return from a previous CALL instruction. RETURNing from the entry
  function ends the shader. Setting `retval` to zero if the function was
  intended to return a value is undefined behavior.

    struct ReturnInstruction {
        Uint32 opcode;   // each instruction type has a unique value.
        Uint32 num_words;  // always 3.
        Uint32 retval;   // SSA id of value to return, zero for void.
    };


### The Phi function

- PHI: This uses BinaryOperationInstruction, with two SSA inputs that produce
  a new SSA output, and show up when a branch could cause a variable to end
  up with two different possibilities.


### Intrinsic functions

Built-in things that we can dither down to reasonably look like instructions,
that are likely accelerated on various targets, either as an instruction
itself or a standard library thing. This list is likely to grow.

Most of these use BinaryOperationInstruction or UnaryOperationInstruction.

*** !!! FIXME: write me ***

- ALL
- ANY
- ROUND
- ROUNDEVEN
- MOD   (?)  from GLSL
- TRUNC
- ABS
- SIGN
- FLOOR
- CEIL
- FRACT
- RADIANS  (?)
- DEGREES  (?)
- SIN
- COS
- TAN
- ASIN
- ACOS
- ATAN
- SINH
- COSH
- TANH
- ASINH
- ACOSH
- ATANH
- ATAN2
- POW
- EXP
- LOG
- EXP2
- LOG2
- SQRT
- RSQRT
- MIN
- MAX
- CLAMP
- MIX
- STEP
- SMOOTHSTEP  (?)
- MAD
- FREXP
- LDEXP
- LEN
- DISTANCE  (?)
- DOT
- CROSS
- NORMALIZE
- FACEFORWARD
- REFLECT
- REFRACT
- TRANSPOSE
- SAMPLE


### Debug table

*** !!! FIXME write me ***

