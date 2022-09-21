
# SDL GPU SUPPORT: THE BASIC IDEA

Since a lot of people ask and this is scattered across a lot of different
places, here's a quick collection of stuff to explain what the new SDL GPU
work is about.

## tl;dr

The idea is that it's a new 3D API, that works with command queues (like
next-gen APIs like Direct3D 12, Vulkan, and Metal), and leaves all the heavy
lifting to shaders. Shaders are written in a new programming language, and
compiles to a cross-platform bytecode. You ship one shader binary that works
everywhere, and the C code you write to draw things works everywhere.
You can precook shaders, or compile shaders at runtime.

## What's supported?

This is still work in progress, but the initial plans are to write backends
to support:

- Metal
- Direct3D 12
- Vulkan

Future plans are to also support:

- WebGPU
- Various console targets (in a separate fork, under NDA).

We _may_ try to get this to limp along on:

- Direct3D 11.
- OpenGL 4.5 Core Profile, GLES3, WebGL 2.

We will _not_ support:

- A software renderer.
- Older OpenGL and Direct3D.
- Wild old APIs like Sony PSP/Vita homebrew, etc.


## What does the shader language look like?

And you can examine [the Shader Language Quickstart document](README-shader-language-quickstart.md), and see some
example code at the start of the [the shader syntax megathread discussion](https://github.com/icculus/SDL_shader_tools/issues/3),

Mostly, it looks a lot like GLSL/HLSL with some footguns removed and some
(mostly optional!) better syntax alternatives.


## What does this look like on the C side?

The best comparison I can give is to look at SDL's [testvulkan.c](https://github.com/icculus/SDL/blob/gpu-api/test/testvulkan.c),
which is about 1100 lines of C code and just clears the screen, and then
show you the [same thing with the new API](https://github.com/icculus/SDL/blob/gpu-api/test/testgpu_simple_clear.c),
in under 150 lines.


## Does this replace the existing SDL 2D rendering API?

Nope! The existing API remains, for when you want simple rendering of 2D
graphics. However, we will be writing a backend for that API that uses the
new SDL GPU API directly and exposing the structures you need so you can
jump between them as necessary, which might be nice if you mostly want to
do 2D but throw some post-processing effects on top, or slot in some 3D
models, etc.

The 2D API might lose some backends, if the GPU API backend makes them
redundant, but several of them will remain for targets the GPU API doesn't
support.


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
- [SDL_gpu.h, for API documentation](https://github.com/icculus/SDL/blob/gpu-api/include/SDL_gpu.h)
- [SDL_shader_tools](https://github.com/icculus/SDL_shader_tools), where the shader compiler is being built.
- [Shader Language Quickstart document](README-shader-language-quickstart.md)
- [Shader Bytecode Format document](README-bytecode-format.md)
- [The initial announcement.](https://www.patreon.com/posts/new-project-top-58563886)
- [What this looks like on the C side.](https://www.patreon.com/posts/sdl-gpu-update-65960741)
- [Metal implementation and shader plans.](https://www.patreon.com/posts/sdl-gpu-apple-66552682)
- [Shader Preprocessor work.](https://www.patreon.com/posts/sdl-gpu-and-67437415)
- [Shader syntax megathread.](https://github.com/icculus/SDL/issues/3)


## Questions? Comments? Complaints?

File them at the [current issue tracker](https://github.com/icculus/SDL/issues/new)

This will eventually merge with mainline SDL, but for now, this is where the
magic happens.

