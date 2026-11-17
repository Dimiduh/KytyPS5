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

A list covers what was played before it was exported, nothing else. To add what a data folder
has recorded to the game's list here:

```
.\run-windows.ps1 -DataDir <data folder> -ExportPipelineList pipeline-lists\<title id>.pipelinelist
```

The export writes the list the emulator was given for the game (this folder's, by default)
with the data folder's recordings added after it, and says how many were new. With
`-PipelineList none` it writes the data folder's recordings alone.

`-PipelineList <file or folder>` uses another list, `-PipelineList none` none.

| File | Game | Recorded |
|---|---|---|
| `PPSA07631.pipelinelist` | Saros | 2026-11-17, the first hours of the game and three starts of a new game: 961 graphics pipelines (the patterns of `shader-pairs/PPSA07631.pairs` are among them) and 13,970 compute programs |
