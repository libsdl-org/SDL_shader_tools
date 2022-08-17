I haven't even proofread this yet, but in case this is something you care
about, here's how the new shader language is different from C, or GLSL, etc,
if you just want to get a quick feel for what I'm aiming for.

Also, if you have a background in C or GLSL, then rather than read a formal
document about this language, it's more efficient to just tell you about the
parts that are different, so you can go "huh" or "eww" or whatever.

This is all up in the air, and some of this is already planned to change in
some ways based on feedback.

The feedback megathread is
[over here](https://github.com/icculus/SDL_shader_tools/issues/3).

Here's the current smashed-down quickstart document.

---

# SDL Shader Language (SDLSL) Quickstart

In lieu of more formal documentation, here's a quickstart guide to the
shader language syntax.

If you've written shaders in GLSL, HLSL, or MSL, then a lot of this is going
to look and feel familiar. Much of this language is "C like" with a few
differences that we'll highlight here.


## First things first.

This is all in flux right now, so check back for changes.

The compiler API takes a "profile" identifier when parsing code; once this
locks down to a 1.0 syntax, the tools will always support it, even if we go
in a different direction for 2.0, etc, as the 1.0 profile will continue to
remain as an option.


## "function" and "var" keywords

Let's get the biggest not-C thing out of the way first: functions are prefaced
with a `function` keyword, and variables are prefixed with `var`. This is
probably repulsive to you if you love C++ or GLSL, but maybe not a big deal
if you love Rust or WebGPU.

```c
function float myfunction(float x)
{
    var float y = x * 4.0;
    return y;
}
```


## Everything is case sensitive.

Like C, you can't say "MyFunction" to call "myfunction"


## There is a preprocessor.

This isn't dramatic; it works as closely to a standard C preprocessor as
possible; `#define`, `#include`, `#if` and `#ifdef`, etc.

Include files are intended to use '/' as a path separator on all platforms.
If other things happen to work, it's an accident and might become failures
at a later time. Always use '/'. This is good policy in C, too.

In the included command line tool, including an absolute path
(`#include "/dev/urandom"`), or something with a ".." path, will fail. This
is actually an option that can be set if calling the library's API, so if
for some reason you want this, you can enable it. But you probably shouldn't.


## Whitespace is (mostly) up to you.

Same C rules: whitespace doesn't matter, except for a few pieces of the
preprocessor being line-oriented in the same way as C. Nothing should be
surprising here to C developers. We don't care where you put your braces,
or if you use tabs or spaces, or how many, or if you use any at all.


## Both kinds of comments are allowed.

Both `/* inline comments */` and `// comments to the end of the line` work.


## Statements end with a semicolon, statement blocks are wrapped in braces.

This is just like C. Nothing surprising.


## Flow control statements _must_ use braces.

This is _not_ like C. You can't do this...

```c
if (x == 1)
    do_something();   // this won't compile, it _must_ be wrapped in braces!
```

...which means you--thankfully--can't do this...

```c
if (x == 1);   // uhoh, in C, that accidental semicolon means do_something always runs!
    do_something();
```

...so you have to wrap this in braces...

```c
if (x == 1) {
    do_something();
}
```

This is just about removing a footgun from C semantics.


## Flow control statements don't need parentheses.

If you love them, you can use them, but they aren't required:

```c
if x == 1 {   // this compiles! We can parse this because we require braces!
    do_something();
}

if (x == 1) {}  // this compiles, too, so you can ignore all this if you want.
```

## Most C operators match.

The things you'd expect, like `+` and `/` and stuff are all there.

There isn't a comma operator (beyond separating function call arguments, etc),
as this generally just causes subtle bugs.


## Vectors are `floatX`, not `vecX`.

This works like HLSL, not GLSL; vectors and matrices use the datatype
followed by the dimensions.

- `int3`
- `float4x4`

There is no `ivec4` or whatnot; it's `int4` instead.

Vectors can be dereferenced like an array or with the usual shader swizzles:

```c
var float4 f;
f.x = 1.0;  // you can set individual components.
// A literal index out of bounds will be caught by the compiler.
// A variable index out of bounds is undefined behaviour, so don't do that.
f[2] = 5.0;   // this sets f.z to 5.0.
```

## There is a "bool" datatype, and a "half" datatype.

The half datatype is a 16-bit float, and there is no native C equivalent. On
platforms where it isn't available, we'll use the closest precision (probably
32-bit float) internally. There is not (currently) a "double" type. On some
platforms, half might be more efficient, but be careful about how little
space 16-bits gives you to work with. If in doubt, use "float" instead.

`bool` is just `true` or `false`. Integers do not treat non-zero as `true`
since there's an actual bool datatype.


## Float literals don't end with 'f'.

If you want to say `5.0`, then say it. There isn't a `5.0f`, this is a
syntax error. Since there's no implicit casting, the compiler can figure out
that a float literal is meant to be a half or float or double or whatever
without the 'f' qualifier.


## There are no pointers.

This is not shocking in a shader language, but there aren't pointers _at all_,
let alone pointer manipulation.

There are arrays, and you can pass variables by reference to functions.


## Assignments are not expressions.

You can not assign in an expression, which means you can't accidentally do
this:

```c
if (myvar = 1) {  // this probably wasn't what you meant to do!
    do_something();
}
```

That code, in C, would assign 1 to `myvar`, and then always run the code in
the `if` block, since the 1 would become true. A double-disaster because you
used `=` where you meant `==`.

Here, this fails to compile, catching the problem immediately, because
assignment is not a valid _expression_. Instead, it's a statement.

This also applies to compound assignment operators (`myvar += 1;`, etc) and
increment operators (`myvar++;`). Since increment operators are statements,
while you can do `++i` or `i++`, they work the same, since there are no
other operations that it might run before or after them in the same statement.
And you can't do `x = y++;` since increment operators are not expressions.


## Expressions are not statements.

Statements are verbs, expressions are nouns. We have split out the things you
might want to do as a verb and allow only those things for statements:
assignment, loops, ifs, function calls where the return value is void or
ignored.

A bare expression is not allowed and the compiler will report an error. For
most uses of C, you won't notice this, but it _will_ catch accidents like
this:

```c
x + 1;  // did you mean to assign this to something...?
```

## Integers are not bools.

The language will not implicitly dither an int down to a bool. If you need
to test an integer is non-zero, you should do `if (x != 0)` instead of
`if (x)`.


## Construct, don't cast.

This is no casting operator, but there is a _constructor_ operation.

If you need to convert to a different type, call a datatype like it's a
function:

```c
var int x = 5;
var float y = float(x);
```

One constructs a vector like you would expect:
```
var float4 x = float4(1.0, 2.0, 3.0, 4.0);
```

And things like arrays and structs use the same format (with the fields or
elements listed in the proper order).


## Types do not implicitly cast.

There is never a time where the compiler will implicitly convert or promote
a datatype. This is illegal, for example:

```c
var half a = 5.0;
var float b = 5.0;
var float c = a + b;  // this is illegal, types must match.
```

A C compiler might just promote `a` to a float, since this is harmless (in
this case), but we prefer the simplicity and clarity of knowing everything
matches: matches datatypes, and matches your intentions. If you have to do
this, wrap `a` in a constructor:

```c
var float c = float(a) + b;
```


## Literals can stand in.

There is one exception to the implicit-casting rules: literals. As syntactic
sugar, the compiler will allow you to put an integer literal in lots of places
where you could not use a variable of `int` type.

```c
var int x = 5;
var float y = x;  // this won't work, we won't implicitly cast to float.
var float z = 5;  // legal! the compiler treats this as shorthand for 5.0
var float4 w = 5; // legal! shorthand for float4(5.0, 5.0, 5.0, 5.0)
```

Generally this is for making code clearer when you need to operate across
a vector with the same value:

```c
result = myfloat4 / 5;  // same as `myfloat4 / float4(5.0, 5.0, 5.0, 5.0)`
```

These don't _have_ to be integer literals; you may also use float literals
for many of these situations, but these will not work with things that
need integer values:

```c
var int x = 5.0;  // fails, can't use a float here, even if it's a whole number.
var float4 y = 5.0;  // legal!
return y / 3.3;  // legal! Returns a float4.
```


## Uninitialized variables default to zero.

If you don't initialize a variable, we'll pick a reasonable default for it;
zero for most things, false for bools, etc. There are no such thing as
uninitialized variables. When possible, we'll throw away the default value
if we see an assignment between declaration and first use, but that's our
problem, not yours.


## Variable initialization can't reference itself.

Why does C even allow this?

```c
int x = x + 1;
```

In C, `x` is in scope as soon as the parser sees `int x`, but this allows you
to reference a variable that is _definitely_ not initialized during its own
initializer! Doing so is _always_ a bug, so we just don't allow it. In that
statement, `x` will not come into scope until after the initializer is
complete...so this will fail to compile, letting you catch the problem
upfront.


## Variables must be unique in all scopes.

C lets you do this:

```c
int x = 1;
if (x > 0) {
    int x = 2;
    printf("%d\n", x);
}
printf("%d\n", x);
```

Which will print:

```
2
1
```

We don't allow the same variable name in children scopes. This will fail to
compile, so you can't accidentally access the wrong variable by human error or
a refactor gone bad. Pick a different name.


## For-loops are mostly C-like, sort of.

For-loops look like you'd expect from C...

```c
for (i = 0; i < 5; i++) {
    whatever(i);
}
```

But the first and last section are _statements_, not expressions, so while
you can put the things you generally would put in a C for loop here, you can't
put _anything_ in here. Remember that assignment, increment operators, and
bare function calls are statements, which means almost anything you would
write in C will work here, too.

The middle section _is_ an expression, and it must result in a bool datatype,
so `i` might not work where `i != 0` will.

You can declare a variable in the first section:

```c
for (var int i = 0; i < 5; i++) {}
```

## Variable declaration can be C-like, or not.

If you like C, variable/param/field declarations are like you'd expect
(disregarding the `var` and `function` keywords) ...

```c
struct mystruct {
    int stuff[5];
};

function int myfunc(mystruct x) {
    var int y = 5;
    return x.stuff[2] + y;
}
```

But if you have more modern sensibilities (or extremely oldschool
sensibilities, Pascal fans), you can do this with `varname : vartype` syntax
instead:

```c
struct mystruct {
    stuff : int[5];
};

function myfunc(x : mystruct) : int {
    var y : int = 5;
    return x.stuff[2] + y;
}
```

You can mix and match, too, but I wouldn't personally recommend it:

```c
struct mystruct {
    stuff : int[5];
    int other_stuff[5];
};

function int myfunc(int y, x : mystruct) {}
```


## Multiple assignments as syntactic sugar.

In C, you can assign several things in a single statement because assignment
is an expression and expressions can be statements.

Here, the compiler has syntactic sugar to let you assign the same value to
several items in a single statement, which matches C syntax:

```c
x = y = z = w = whatever();  // called once, value assigned to each var.
```

This is just syntactic sugar so you don't have to do this over several lines:

```c
w = whatever();
x = w;
y = w;
z = w;
```

This only works with basic assignment. Compound assignment (`x += 5;`) does
not allow multiple assignments in a single statement.


## Structs are only declared at global scope

C lets you declare a struct anywhere, including inline when declaring a
variable. We don't allow that. Structs must be declared in a global scope.


## Functions and structs do not need to be predeclared

Since we sprinkled around enough restrictions on the C language that we can
parse our shader language without knowing what has been predeclared, we can
remove the need to predeclare things at all, and figure out what is what from
the parse tree afterwards.

As such, structs and functions are not predeclared at all, and can be defined
in any order in the shader, and used before they are defined.


## There is no hardcoded "main" function.

You can name the main entry function whatever you want. You can have multiple
main functions in the same shader source file that do different things
depending on which one is used when loading the shader.

Since vertex and fragment programs can live in same source file, this becomes
useful, just to have a "mainVS" and "mainFS" function.


## There is (currently) no typedef.

This may change. But since most shaders are smallish, and the base datatypes
are pretty well defined, the hope is there isn't a need for typedefs.


## There are (currently) no globals.

This may change. Right now everything is passed as a function argument or
return value: shader inputs and outputs are not globals, they're data passed
into and out of the main function. If a subroutine needs data from a caller,
be prepared to pass it as an argument.


## @attributes

Several things need a little metadata to glue things to the right place for
the GPU or tell the compiler important information. These attributes are
prepended to the item and specified with an '@' character (like an ATtribute,
get it?).

For example, your fragment shader entry point will have one:

```c
function @fragment float4 main_fragment(void)
{
    return float4(1,1,1,1);  // always pure white.
}
```

See that `@fragment`? That tells the compiler "this is for a fragment shader"
which will let you specify it as the main function, and also permits things
like the `discard` statement to be used in this function.

Most attribute things, though, tell us how to get data in and out of the
shader:

```c
struct VertexInput
{
    float4 position @attribute(0);
    float4 color @attribute(1);
};
```

This says "position will be determined by the first (0) attribute in the
GPU pipeline's vertex description, and color will be the second (1) attribute."


```c
struct VertexOutput
{
    float4 ScreenPosition;
    float4 CameraVector_FogA;
    float4 Position @position;
};
```

"When this data is passed on to the next pipeline stage, the "Position"
field is what should be used for the vertex's coordinates.

Later, when setting up the vertex shader's main function:

```c
function @vertex VertexOutput vertex_main(VertexInput Input @inputs)
{
    // ...
}
```

The `@inputs` and `@vertex` attributes do some magic.

