# THIS IS A WORK IN PROGRESS

This is not yet ready for _anything_, let alone production use.

It's not clear if this will continue to live in a separate project or
become part of SDL itself. At the very least, it will definitely move to
a different location on GitHub.

This will--eventually, I hope--become tools used to generate shader
bytecode for SDL's GPU API.

This is not meant to become a world-class compiler; the primary goals are:

- Fast and cheap
- Reasonable to ship at runtime or embed in another project.

Which is to say that a non-goal is:

- Heavy optimization of shaders at compile time.

There is much work to be done here, and so much to build, so please be
patient as things fall into place and decisions are made.

# Current status: parsing!

We have a first draft of the syntax we're aiming for, and a parser that
can handle it.

Build the project with CMake, and then you should have a program named
"sdl-shader-compiler" ...run it like this to see it spit out the
Abstract Syntax Tree (AST) of a shader (which at the moment just looks
much like the shader itself with mild reformatting, because it will output
source code from the AST that it generated)...

```bash
./sdl-shader-compiler -T -I some_dir -DSOME_DEFINE=SOME_VALUE some_source.shader
```

If you just want to see it preprocess stuff, like a C preprocessor does:

```bash
./sdl-shader-compiler -P -I some_dir -DSOME_DEFINE=SOME_VALUE some_source.shader
```

# Questions?

If you have questions, please open an issue on the GitHub repository, or
feel free to hit me up [on Twitter](https://twitter.com/icculus) or just
[email me](mailto:icculus@icculus.org).

# Pay me.

Want to help fund this work?
- [Become my patron on Patreon](https://patreon.com/icculus), or
- [Sponsor me on GitHub](https://github.com/sponsors/icculus)

Thanks!

--ryan.

