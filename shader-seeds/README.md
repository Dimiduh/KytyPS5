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

- Compute shaders: all of them are built. At a first use the prebuilt module serves about four
  in five; the rest depend on what the game binds at that moment and are compiled then.
- Graphics pipelines: not covered. A pipeline is a pair of shaders with render state that only
  the running game decides, and the game's files do not list them. They come from what sessions
  record (and from `pipeline-lists`); `-SkipPendingDraws on` keeps a new one from stalling the
  game while it is built.

No tool in this repository writes the file yet. For Saros (Unreal Engine 5) it is written by
reading the game's shader libraries out of its archive; the script used so far lives outside
the repository.
