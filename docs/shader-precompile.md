# Shaders before play

A PS5 game ships its shaders as machine code for the console's GPU. The emulator translates each
one and the graphics driver compiles the result into a pipeline; both take time, and when they
happen in the middle of play the game stops for that long. This page says what is done ahead of
play instead, what is not, and how to tell which happened.

## What a session costs when nothing is prepared

Measured on Saros (RTX 4090, i9-13900K), on the thread that feeds the GPU:

| First use of | Translation | Driver compile |
|---|---|---|
| a compute shader | about 13 ms | about 115 ms on average, up to 1.3 s |
| a graphics shader stage | about 5 ms | (with its pipeline) |
| a graphics pipeline | | about 60 ms |

Three minutes in a part of the game no session had seen needed 110 compute shaders, about 190
graphics stages and 85 graphics pipelines.

## What is done ahead

1. **Recorded programs and pipelines.** Every session writes down what it compiled
   (`_PipelineCache/<title>.v5.programs`, `.v7.pipelines`). The next start translates the
   recorded programs on helper threads and builds the recorded pipelines into the driver cache
   before they are needed. A place you have been costs nothing the second time.
2. **A pipeline list** (`pipeline-lists/<title>.pipelinelist`): somebody else's recordings,
   without any game code. See `pipeline-lists/README.md`.
3. **Every compute shader of the game** (`shader-seeds/<title>.seeds`, see
   `shader-seeds/README.md`): the precompile pass builds a module from each shader's code alone.
   Such a module reads at run time what the code does not say (buffer formats and strides,
   texture channel orders, level counts), so one module serves whatever the game binds. The same
   modules are translated again in the background at every start (about 45 s on 8 threads,
   `--prepare-threads`), and a first use takes one without translating anything.
4. **New graphics shaders off the GPU thread** (`--skip-pending-draws`, the launcher's "Never
   wait for a new graphics shader"): the translation of a stage nothing has translated and the
   build of its pipeline both happen on helper threads, and the draws that need them are left
   out until they are there: an object appears a few frames late instead of the game stopping.
   Draws that must not be left out (they clear depth, render to a target the CPU reads, or
   their shaders store to memory) wait as before.

Run the pass once after installing, after an emulator update that changes shaders, and after a
graphics driver update:

```
.\run-windows.ps1 -Precompile -Game <game>
```

A first pass compiles everything (about 14,000 pipelines for Saros, 25 to 30 minutes on the PC
above, a 1 GB cache); a later one only what changed.

## What is still compiled during play

- Compute shaders that pick their textures from a table the GPU selects from: the table's size is
  built into the module, so the first use compiles, and so does a use with a larger table.
  Integer textures read through a filtering sampler, and bindless texture heaps.
- Graphics pipelines no session and no list has recorded. They are built in the background (4);
  nothing prepares them before play.

## NVIDIA's shader disk cache

NVIDIA's driver compresses what it compiles with a dictionary that it trains once a process has
made a few hundred pipelines and keeps in its shader disk cache, and it can read the entries of
a pipeline cache file only with the dictionary its disk cache holds at that time. The driver's
default disk cache is shared by every program on the PC, so that dictionary changes behind the
emulator's back; a title's whole pipeline cache then turns into entries the driver keeps but
does not provide, and a session compiles every pipeline at its first use although the pass had
built them all.

The emulator therefore gives the driver a disk cache of the data folder's own,
`_PipelineCache/nvidia` (`__GL_SHADER_DISK_CACHE_PATH`; an environment that already names one
keeps it). Do not delete that folder without deleting `<title>.bin`, `<title>.v1.built` and
`<title>.v1.finished` next to it and running the pass again. A pipeline cache written by a build
older than this one has to be rebuilt the same way.

## Telling what happened

The log of a session (`logs/<time>.out.log` next to the data) ends with lines such as:

```
Compute pipelines this session: 1076 from the driver cache, 37 compiled
Graphics pipelines this session: 499 from the driver cache, 81 compiled; 495 of 580 recorded were predicted (by earlier sessions or the list)
Compute programs this session: of the first uses of shaders built from their code alone, 79 were that module, 336 took it (it serves them), 0 took their own from the pipeline list instead, 34 needed their own
Programs prepared ahead: 11164 of 11199 recorded or built from their code translated (0 failed); 391 taken at their first use (0 waited for), 35 first used before their turn
Stage programs translated off the GPU thread while their draws were left out: 222 programs, 250 modules (0 done on the GPU thread after all)
```

"compiled" is what cost time during play. A line `Vulkan pipeline cache: N builds missed
pipelines the cache was given before` means the driver dropped entries (the section above): run
the pass again.
