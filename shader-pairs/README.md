# Shader pairs

`<title id>.pairs` names vertex and pixel shaders of a game's files that are likely drawn
together, each pair with a recorded graphics pipeline to pattern its own pipeline on. The
precompile pass (`run-windows.ps1 -Precompile`) builds those pipelines into the driver cache, so
that a material no session has shown yet can find its pipeline there. The file holds hashes and
numbers only, no shader code; the code comes from `shader-seeds/<title id>.seeds`.

`run-windows.ps1` passes this folder to the emulator (`--shader-pairs`) when it exists;
`-ShaderPairs none` leaves it out (the launcher's "Also build likely shader pairs", off),
`-ShaderPairs <file>` names another list. The pairs are the last and longest part of a first
precompile: for Saros about 23,000 of its 37,000 pipelines.

## What a line builds

```
KytyShaderPairs2
<pattern> <VS hash> <VS words> <VS user data> <PS hash> <PS words> <PS user data>
```

- `pattern`: the record key of a graphics pipeline that is recorded in the data folder or is in
  the pipeline list (`pipeline-lists/<title id>.pipelinelist`): one vertex-side stage, no merged
  two-part program.
- the vertex-side shader and the pixel shader by program hash (hex), program size in words and
  user data count; `0000000000000000 0 0` when the pattern has no pixel stage.

The pipeline is the pattern's with the pair's code: the pattern gives the render state, the
vertex input and, of the two stage inputs, what belongs to the mesh and the pass (vertex buffer
formats, targets, blending); everything a draw's registers would hold of the two shaders' own
headers is taken from those headers (the pixel shader's inputs and interpolation, the wave sizes,
`PA_CL_VS_OUT_CNTL`). A pixel shader whose header belongs to another pass than the pattern's, or
that reads an input the vertex shader does not write, is skipped; a program that ships with
several headers gets a pipeline for each that fits. The stage modules are the ones the code gives
by itself (they read texture and buffer formats at run time), and a session takes the same
modules for these shaders, which is what makes its first draw find the pipeline.

A line whose pattern or shaders the emulator does not have costs nothing. A pair the game never
draws, or draws in another pass or with another mesh, costs its build time and its room in the
cache (about half a second of one thread and 55 KB each on an RTX 4090).

## What it covers, and what it does not

The game's files do not say which vertex shader goes with which pixel shader, nor in which pass:
the list is a guess from what was recorded. For Saros (`PPSA07631.pairs`, 23,207 lines made from
610 recorded pipelines): pipelines recorded later in the game than the patterns were prebuilt in
about a third to a half of the cases (measured on a data folder with nothing recorded: 50 of 161
with patterns from other parts of the game only; the offline estimate with all patterns is 54%).
The misses are pairs no pattern covers: a vertex-shader variant, a pass or a render state that no
recorded pipeline has, and two-part geometry shader programs.

What is not prebuilt is built in the background as before (`--skip-pending-draws`) or at the
draw. Every session's own recordings are prebuilt for the next start, and a pipeline list made
after more of the game was played gives the pairs more patterns.

## How PPSA07631.pairs was made

From the game's shader files and one data folder's recordings, offline: shaders are grouped by
the header values that reach a stage's input (vertex side: input semantics and vertex fetch
tables; pixel side: `CB_SHADER_MASK`, `DB_SHADER_CONTROL`, `SPI_SHADER_COL_FORMAT`,
`SPI_SHADER_Z_FORMAT`), and a recorded pipeline is the pattern of the pairs of its two groups
that sit in one shader map of the game and whose vertex shader is the pattern's own or has its
user data layout and output count relative to the pixel shader's inputs. Without that last rule
the list is ten times as long and reaches about a quarter more.
