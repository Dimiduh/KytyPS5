# Shader seeds

A `<title id>.seeds` file holds the shaders of one game, taken out of the game's own files. With
one here, the precompile pass (`run-windows.ps1 -Precompile`, the launcher's "Precompile
shaders") builds every compute shader of the game before the game has ever used it, instead of
only the ones earlier sessions met:

```
.\run-windows.ps1 -Precompile
```

## Where the file comes from

The precompile pass writes it. For a game made with Unreal Engine 5 (Saros is one) it reads the
shader libraries out of the game's containers (`*.utoc` and `*.ucas`, in the game's folder or
inside its archive) the first time, which takes a few seconds, and says so:

```
Shader seeds: reading the shader libraries of the game's files
Shader seeds: 37545 shaders from 6 shader libraries of the game's files (210.1 MiB) written to ...\shader-seeds\PPSA07631.seeds in 1.3 s
Shader import: 37545 shaders (11637 compute programs) from ...\shader-seeds\PPSA07631.seeds
```

Next to the file it writes `<title id>.seeds.source`, the names and sizes of the containers the
file was made from. While those stay the same, later passes use the file as it is; after a game
update they write it again. A seeds file with no `.source` note next to it is one somebody made
another way, and is never replaced.

What it needs:

- `ooz_decode.exe` next to `kyty_emulator.exe`. Unreal packs its shader libraries with Oodle, and
  this small program unpacks them. It is a program of its own with its own license (GPL-3.0 or
  later), built with the emulator and included in the Windows release; see
  `tools/ooz-decode/README.md`. Windows only for now.
- A game whose containers are neither encrypted nor packed as a whole (a PS5 dump of an Unreal
  Engine 5 game normally is neither).

When it cannot be done, the pass says why in a line starting with `Shader seeds:` and goes on
without the game's own shaders, as it did before. A game that is not made with Unreal Engine 5
has no such libraries, and nothing is written for it.

`-ImportShaders <file or folder>` takes the file from somewhere else (a file given by name is
used as it is, never written), `-ImportShaders none` leaves it out. The emulator's option is
`--import-shaders` (`ShaderImportSeeds` in `src/graphics/shader/shader.cpp` reads the format,
`src/graphics/shader/gameShaderSeeds.cpp` writes it).

A seeds file is the game's code: it stays on the PC of whoever owns the game. Git ignores
`*.seeds` in this folder; do not share one.

## What it does and does not cover

- Compute shaders: all of them are built, and translated again in the background at every start
  so that a first use takes its module without translating anything. The prebuilt module reads
  what it cannot know from the code at run time (buffer formats and strides, texture channel
  orders, level counts), so it serves a first use whatever the game binds, with these exceptions,
  which are compiled when they are first used: shaders that pick their textures from a table (the
  table's size is built into the module) and integer textures read through a filtering sampler.
- Graphics pipelines: not covered by the file alone. A pipeline is a pair of shaders with render
  state that only the running game decides, and the game's files do not list them. They come
  from what sessions record, from `pipeline-lists`, and from `shader-pairs`, which names pairs
  whose code this file provides; `-SkipPendingDraws on` keeps a new one from stalling the game
  while it is built.
