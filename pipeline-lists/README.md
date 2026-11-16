# Pipeline lists

A `<title id>.pipelinelist` holds the compute programs and graphics pipelines that sessions of
one game have needed: which shaders, with which render state. It names each shader by a hash of
its code and contains no shader code, so it can be shared.

`run-windows.ps1` (and the launcher, which starts the game through it) hands this folder to the
emulator (`--import-pipeline-list`). The emulator takes the list of the game it runs, if the
folder has one, and builds every entry in the background as soon as it has the entry's shaders:
the ones the game has created in any session, or the ones imported from the game's files
(`--import-shaders`). Each entry is built once; the driver's pipeline cache keeps it, and a first
use then finds its pipeline ready instead of compiling it.

A list covers what was played before it was exported, nothing else. To write the list of what a
data folder has recorded:

```
.\run-windows.ps1 -ExportPipelineList pipeline-lists\<title id>.pipelinelist
```

`-PipelineList <file or folder>` uses another list, `-PipelineList none` none.

| File | Game | Recorded |
|---|---|---|
| `PPSA07631.pipelinelist` | Saros | 2026-11-16, the start of the game: 610 graphics pipelines (the patterns of `shader-pairs/PPSA07631.pairs` are among them) and 12,655 compute programs |
