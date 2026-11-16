# Shader seeds

A `<title id>.seeds` file holds the shaders of one game, taken out of the game's own files. With
one here, the precompile pass (`run-windows.ps1 -Precompile`, the launcher's "Precompile
shaders") builds every compute shader of the game before the game has ever used it, instead of
only the ones earlier sessions met:

```
.\run-windows.ps1 -Precompile
```

`-ImportShaders <file or folder>` takes the file from somewhere else, `-ImportShaders none`
leaves it out. The emulator's option is `--import-shaders` (`ShaderImportSeeds`,
`src/graphics/shader/shader.cpp`, reads the format).

A seeds file is the game's code: it stays on the PC of whoever owns the game. Git ignores
`*.seeds` in this folder; do not share one.

What it does and does not cover:

- Compute shaders: all of them are built, and translated again in the background at every start
  so that a first use takes its module without translating anything. The prebuilt module reads
  what it cannot know from the code at run time (buffer formats and strides, texture channel
  orders, level counts), so it serves a first use whatever the game binds, with these exceptions,
  which are compiled when they are first used: shaders that pick their textures from a table (the
  table's size is built into the module) and integer textures read through a filtering sampler.
- Graphics pipelines: not covered. A pipeline is a pair of shaders with render state that only
  the running game decides, and the game's files do not list them. They come from what sessions
  record (and from `pipeline-lists`); `-SkipPendingDraws on` keeps a new one from stalling the
  game while it is built.

No tool in this repository writes the file yet. For Saros (Unreal Engine 5) it is written by
reading the game's shader libraries out of its archive; the script used so far lives outside
the repository.
