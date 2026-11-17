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
   On a card with a video memory budget under 13 GiB (12 GB and less) a session leaves the
   pipeline builds out, because they keep about 1.6 GB of video memory: run the precompile pass
   before play, and a pipeline then comes from the driver cache at its first use.
   `KYTY_SESSION_PRECOMPILE=1` builds them beside the game all the same.
2. **A pipeline list** (`pipeline-lists/<title>.pipelinelist`): somebody else's recordings,
   without any game code. See `pipeline-lists/README.md`.
3. **Every compute shader of the game** (`shader-seeds/<title>.seeds`, see
   `shader-seeds/README.md`): the precompile pass reads the shaders out of the game's own files
   (Unreal Engine 5 games; it writes the seeds file the first time, in a few seconds) and builds
   a module from each shader's code alone.
   Such a module reads at run time what the code does not say (buffer formats and strides,
   texture channel orders, level counts), so one module serves whatever the game binds. The same
   modules are translated again in the background at every start (about 45 s on 8 threads,
   `--prepare-threads`), and a first use takes one without translating anything.
4. **Graphics pipelines of shader pairs** (`shader-pairs/<title>.pairs`, see
   `shader-pairs/README.md`): vertex and pixel shaders of the game's files that are likely drawn
   together, each pair built into the pipeline of a recorded one of the same kind. This is a
   guess, because the game's files do not say which shaders are drawn together or how: it
   provides a third to a half of the pipelines of a part of the game no session has seen.
5. **New graphics shaders off the GPU thread** (`--skip-pending-draws`, the launcher's "Never
   wait for a new graphics shader"): the translation of a stage nothing has translated and the
   build of its pipeline both happen on helper threads, and the draws that need them are left
   out until they are there: an object appears a few frames late instead of the game stopping.
   Draws that must not be left out wait as before: they clear depth, render to a target the
   CPU reads, or a vertex shader of theirs stores to memory. A pixel shader that fills a buffer
   or marks bits in a storage image does not keep its draw in the frame (a fifth of Saros's
   pipelines have one, and each new one used to stop the game for 0.1 to 0.9 s); what it would
   have written is missing for those frames, like the draw's picture.

The pass has to run once after installing, after an emulator update, after a new list of
shaders and after a graphics driver update. The launcher does that by itself before the game
starts ("Before playing" on its Shaders page, on by default; `run-windows.ps1 -PrecompileFirst
on`): a finished pass leaves a stamp in the data folder (`_PipelineCache\precompile-stamp.txt`:
the emulator, the game, the shader lists and the graphics driver it was for), and a start that
finds another stamp, or none, runs the pass in its window first and then starts the game. A
data folder is one such unit: a new one, or a cache copied in from elsewhere, has no finished
pass. Closing the pass's window starts the game at once, with the rest built as it runs; the
next start goes on with the pass. By hand:

```
.\run-windows.ps1 -Precompile -Game <game>
```

A first pass compiles everything (for Saros about 14,000 recorded, listed and compute pipelines
and 23,000 graphics pipelines of shader pairs, a cache of about 2 GB); a later one only what
changed.

## How long the pass takes

Nearly all of the pass is the graphics driver's shader compiler: 94 to 98% of a build thread
with an NVIDIA card. So the time depends on the processor, and on how many builds run at a
time:

- The pass builds one pipeline for each logical CPU the emulator may use but two, on threads
  below normal priority, so the PC stays usable. `-PrecompileThreads <n>`
  (`--precompile-threads`) names another count. On the PC above (8 fast and 16 small cores)
  the same first 3,000 builds take 208 s on 8 threads and 118 s on 30, and the first phase
  (everything but the shader pairs, 13,800 builds) 15 minutes on 30 threads against about 27
  on 8. A processor without small cores gains less: its second threads are worth about a
  third of a core each.
- Memory bounds the count: the driver's compiler takes up to two thirds of a gigabyte for
  each build it has at a time (22 GB at the peak with 30 builds), so a PC with 16 GB of RAM
  builds 14 at a time at most.
- The shader pairs come last and are about half of a first pass. The launcher's "Also build
  likely shader pairs" (`-ShaderPairs none`) leaves them out: their pipelines are then built
  in the background the first time they are drawn.
- The pass can be stopped at any time by closing the emulator's window ("Kyty: precompiling
  pipelines"). It saves what it has built then, and every five minutes while it runs, and the
  next pass goes on from there.
- Every 30 seconds it says where it is: `Precompile: 21378 builds left of 36943 (42% done;
  about 12 min left at this pace)`. The kinds of builds differ in what they cost, so the
  estimate moves.

## What is still compiled during play

- A compute shader that picks its textures from a table the GPU selects from, the first time any
  session meets it: its code does not say what the table holds. That one compile builds the
  module for the largest table the emulator lists (32 entries) and, in the background, the one
  for larger tables, and the precompile pass does the same for every such shader that is
  recorded; a table that grows with the scene needs nothing more. (Saros has eleven of these,
  all recorded in the first area; before, each compiled again at 4, 8, 16 and 32 entries.)
- A compute shader that reads an integer texture through a filtering sampler, the first time.
- Graphics pipelines no session and no list has recorded and no shader pair has guessed (4):
  half or more of those of a new part of the game. They are built in the background (5). Only
  recordings are complete: a place that has been shown once, here or on the PC a pipeline list
  comes from, is ready the next time.

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

"compiled" is what cost time during play, and two more lines say how much of it the game
waited for: `Pipelines created at their first use this session held the GPU thread for ...`, and
for graphics the same by the rule that kept the draws in the frame. A line `Vulkan pipeline cache: N builds missed
pipelines the cache was given before` means the driver dropped entries (the section above): run
the pass again.
