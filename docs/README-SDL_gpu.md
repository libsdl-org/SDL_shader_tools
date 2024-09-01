
# SDL GPU SUPPORT: THE BASIC IDEA

Since a lot of people ask and this is scattered across a lot of different
places, here's a quick collection of stuff to explain what the new SDL GPU
work is about.

## tl;dr

The idea is that it's a new 3D API, that works with command queues (like
next-gen APIs like Direct3D 12, Vulkan, and Metal), and leaves all the heavy
lifting to shaders. Shaders are written in whatever language you like--SDL3
only cares about the compiled shader binaries--but a new programming language
and cross-platform bytecode is also being developed.

The ideal scenario is this: you ship one shader binary that works
everywhere, and the C code you write to draw things works everywhere.
You can precook shaders, or compile shaders at runtime. SDL3 already has
the API for your C code, and the SDL_shader_tools repository is focusing
on building the shader language and binary format.

## What's supported?

The GPU API has been merged into SDL3's revision control!
Behind the scenes, it currently supports:

- Metal
- Direct3D 11
- Direct3D 12
- Vulkan

Future plans are to also support:

- WebGPU
- Various console targets (in a separate fork, under NDA).

We will _not_ support:

- A software renderer.
- OpenGL, OpenGL ES, WebGL.
- Direct3D older than D3D11.
- Wild old APIs like Sony PSP/Vita homebrew, etc.

## Is this going into SDL2?

No, this will be part of SDL3, included in the first official release. It will not be backported to SDL2.

## What does the shader language look like?

Let me preface this by saying: you do _not_ have to use SDL's shader language. The SDL3 GPU 
API only cares about compiled shader binaries, and it accepts several different formats. So
if you want to force Vulkan behind the scenes and only provide SPIR-V binaries, or use
Direct3D 12 and provide DXIL, or provide several binary formats and use several different
backends, you can do that and not worry about this _at all_. This is the initial solution in
SDL3, and also happens to be a convenient way to migrate existing rendering code to SDL3.

The SDL shader language is meant to allow one shader language that generates one binary
format that will work on _any_ SDL3 GPU target, that can be conveniently used to either 
precook shader binaries with simple command line tools or used as a lightweight shader
compiler at runtime. It is still under construction.

You can examine [the Shader Language Quickstart document](README-shader-language-quickstart.md), and see some
example code at the start of the [the shader syntax megathread discussion](https://github.com/libsdl-org/SDL_shader_tools/issues/3).

Mostly, it looks a lot like GLSL/HLSL with some footguns removed and some
(mostly optional!) better syntax alternatives.


## What does this look like on the C side?

The best comparison I can give is to look at SDL's [testvulkan.c](https://github.com/libsdl-org/SDL/blob/main/test/testvulkan.c),
which is about 1170 lines of C code and just clears the screen, and then
show you the [same thing with the new API](https://github.com/libsdl-org/SDL/blob/main/test/testgpu_simple_clear.c),
in under 150 lines.

[A spinning cube in GLES2](https://github.com/icculus/SDL/blob/gpu-api/test/testgles2.c) becomes
[this in the new API](https://github.com/libsdl-org/SDL/blob/main/test/testgpu_spinning_cube.c),
but now can work with many different targets and platforms, and gives you an idea of what, you
know, a _draw call_ looks like.


## Does this replace the existing SDL 2D rendering API?

Nope! The existing API remains, for when you want simple rendering of 2D
graphics. However, there is a now a 2D backend that that uses the new SDL
GPU API directly and exposes the structures you need so you can
jump between them as necessary, which might be nice if you mostly want to
do 2D but throw some post-processing effects on top, or slot in some 3D
models, etc.

The 2D API may lose some backends, if the GPU API backend makes them
redundant, but several of them will remain for targets the GPU API doesn't
support.


## Why a new shader language?

(To restate: _you do not need to use the new shader language if you don't want to!_
But here are some reasons you might want to use it.)

HLSL, GLSL, and Metal Shader Language are all fairly complex languages, so to
support them we would either need to build something that handles all their 
intricacies, or pull in a large amount of source code we didn't write and don't
have a strong understanding of...including, perhaps, a dependency on something
massive like LLVM.

By writing the compiler ourselves for a simple language, we could guarantee it'll
be small, run fast, offer thread safety (so you can distribute compiles across CPU
cores), accept a custom allocator, and be easily embedded in offline tools and also
in games that want to compile shaders on-the-fly.

Tools are readily available that will translate shader source from one language to
another, so our gamble is that in the worst case, it's just one more target and
developers can write in HLSL/GLSL as they would anyhow.

But also...I think this is worth saying out loud: almost every popular shading
language made a conscious choice to be as close to C code as possible, and we can
make some changes to that syntax to make a _better_ language. We don't have to
reinvent the wheel here, just make it a bit more round.


## Why a new bytecode format?

Part of the problem with existing solutions is that one must ship different cooked shaders
for different targets, even if just targeting two possible APIs on the same operating system!
This causes you to spend time compiling shaders more than once when building your
project, and causes bloat in the final installation...not to mention having to manage these
logistics in your build system and at runtime. Having one format that works everywhere that
SDL_gpu does is a better option.

The other question is usually: why didn't we take something off the shelf, like SPIR-V or
DXIL? These formats are significantly (and in my personal opinion, unnecessarily) complex,
and supporting them adds an enormous amount of complexity and risk to the project.


## But I can still just use Direct3D, right?

Yes! If you want to use any version of Direct3D, Vulkan, Metal, OpenGL, or whatever,
you can do so, as always: SDL will provide you window handles and get out of your
way so you can set up any rendering API to use directly. You are in no way forced to
use the GPU API to use SDL3!


## Can I help fund this?

YES. Initial work on this was funded by an Epic Megagrant, and while I'm
deeply grateful for that grant, it wasn't _gigantic_ piles of money, nor
was it ongoing funding.

So if you want to throw in a few bucks, feel free to do so at
[my Patreon](https://patreon.com/icculus) or
[my GitHub Sponsors page](https://github.com/sponsors/icculus).

If you want to do something bigger, like a corporate sponsorship,
[email me](mailto:icculus@icculus.org).


## Link dump

Longer-form writing about all of this:

- [Current development branch for the API](https://github.com/icculus/SDL/tree/gpu-api)
- [SDL_gpu.h, for API documentation](https://github.com/icculus/SDL/blob/gpu-api/include/SDL3/SDL_gpu.h)
- [SDL_shader_tools](https://github.com/libsdl-org/SDL_shader_tools), where the shader compiler is being built.
- [Shader Language Quickstart document](README-shader-language-quickstart.md)
- [Shader Bytecode Format document](README-bytecode-format.md)
- [The initial announcement.](https://www.patreon.com/posts/new-project-top-58563886)
- [What this looks like on the C side.](https://www.patreon.com/posts/sdl-gpu-update-65960741)
- [Metal implementation and shader plans.](https://www.patreon.com/posts/sdl-gpu-apple-66552682)
- [Shader Preprocessor work.](https://www.patreon.com/posts/sdl-gpu-and-67437415)
- [Shader syntax megathread.](https://github.com/icculus/SDL/issues/3)


## Questions? Comments? Complaints?

File GPU API bugs and feedback at the [SDL issue tracker](https://github.com/libsdl-org/SDL/issues/new).

For shader language and bytecode stuff, use the [SDL_shader_tools issue tracker](https://github.com/libsdl-org/SDL_shader_tools/issues/new).
