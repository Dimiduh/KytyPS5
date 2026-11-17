# Changes in this fork

This branch (`saros-port`) starts from the `saros` branch of
[CarRamRod-963/KytyPS5](https://github.com/CarRamRod-963/KytyPS5) (its `saros-v0.1.7` since
2026-11-17, `saros-v0.1.5` before) and adds the work below, most of it for Saros (PPSA07631) on
Windows. Several pieces are ported from, or follow ideas of, the
Demon's Souls fork [chenxiao07/KytyPS5](https://github.com/chenxiao07/KytyPS5); those are marked
"from the DS fork".

Numbers were measured on Saros with an RTX 4090 and an i9-13900K, each as an off/on comparison
inside one running session unless it says otherwise. They belong to the spot and settings named
with them; other places and machines will differ.

## For players

### A launcher for Windows

- `launcher.cmd` opens **KytyLauncher**, a settings window with pages for play, display, graphics,
  game, shaders and advanced options (from the DS fork). It needs no install; build it with
  `launcher-windows\build.cmd`. Without the app, `launcher.cmd` falls back to the script
  `launcher.ps1`, which offers the same settings.
- `run-windows.ps1` starts the emulator from a settings file or from options, and
  `build-windows.cmd` builds it. Both are what the launcher runs underneath.
- Settings are kept in `launcher-settings.json` next to the launcher (not tracked by git).

### Frame generation (DLSS Frame Generation)

- NVIDIA DLSS Frame Generation through NVIDIA Streamline (from the DS fork), with Saros's depth
  and motion vectors: the screen shows two or more frames for every frame the game renders.
- Needs an RTX 40 or newer card and NVIDIA's Streamline SDK, which is not shipped here.
- Launcher row "Frame generation", or `run-windows.ps1 -FrameGen <frames>`.
- Menus, movies and loading screens are shown as they are.
- Details: [docs/frame-generation.md](docs/frame-generation.md).

### 3D resolution and the game's own Unreal settings

- Unreal Engine titles keep their console variables; the emulator can now set them in the running
  game without patching it: `--unreal-cvar <name>=<number>` or `=x<factor>`.
- "3D resolution" in the launcher (`--unreal-render-scale <percent>`, `run-windows.ps1 -RenderScale`)
  scales the size the game renders its 3D at; the game's upscaler still makes the full picture.
- Saros at one heavy spot: 31 fps as it is, 33 at 75%, 36 at 50%.
- Details: [docs/unreal-settings.md](docs/unreal-settings.md).

### Shaders compiled before play instead of during it

- **Precompile pass** (`run-windows.ps1 -Precompile`, or the launcher's button): builds everything
  known for the game into the driver's cache and exits.
- **The whole game's compute shaders**, taken from the game's own files (`--import-shaders`,
  `shader-seeds/`), not only the ones an earlier session met. The precompile pass reads them out
  of an Unreal Engine 5 game by itself the first time it runs (a few seconds for Saros: 37,545
  shaders from six shader libraries), and again after a game update. The test build of
  2026-11-16 could not do this yet: without a seeds file made by hand, its pass built only what
  sessions had recorded and the Saros lists.
- **Pipeline lists** (`pipeline-lists/`): recordings shared without any game code; a list for Saros
  is included.
- **Shader pairs** (`shader-pairs/`): vertex and pixel shaders likely to be drawn together are
  built ahead; a list for Saros is included.
- **At every start**, the recorded programs are translated on helper threads and recorded
  pipelines are built in the background before they are needed.
- **"Never wait for a new graphics shader"** (`--skip-pending-draws`): a graphics shader met for
  the first time is built in the background and its draw is left out until it is ready, instead
  of the game stopping.
- The driver's own shader disk cache is kept in the game's data folder.
- The pass's summary now also says how much earlier passes already built, so a short re-check no
  longer looks like a lost cache.
- **The pass builds on every CPU but two.** It used half of the threads and never more than
  eight. Nearly all of its time is the graphics driver's shader compiler (94 to 98% of a build
  thread with an NVIDIA card), so the gain is what the extra threads are worth: on an i9-13900K
  the same first 3,000 builds take 118 s instead of 208 s, and the first phase (the recorded,
  listed and compute shaders, 13,800 builds) 15 minutes instead of about 27. On a PC with
  little memory it takes fewer threads; `-PrecompileThreads <n>` names a count.
- **The progress line says how far the pass is**: the total, the share done and about how long
  is left at the pace of the last two minutes.
- **A pass can be stopped and continued.** Closing the emulator's window ends it; what it built
  is saved then, and every five minutes while it runs, and the next pass goes on from there.
  Before, the save after a stop could fail and lose everything the pass had built, a pass saved
  nothing until its end, and stopping twice during the same long compile shut that shader out
  of later passes.
- **"Also build likely shader pairs"** in the launcher (on by default; `-ShaderPairs none`):
  off, a first pass leaves out the shader pairs, about 23,000 of the 37,000 pipelines of Saros.
- Details: [docs/shader-precompile.md](docs/shader-precompile.md).

### Crashes and memory

- **The Saros fork's fixes up to `saros-v0.1.7` are merged**: the emulator follows the driver's
  current video memory budget and frees memory at once when it is over the limit, a lost
  graphics device on the lift ride into Shattered Rise and in similar spots is fixed (an older
  copy of a picture was written over newer game data), and so is a freeze when video memory
  was nearly full. A failed wait for the GPU now names the Vulkan result and the memory heaps
  in the log, and the log warns while video memory is over its budget.
- **The shader preparation gives way where memory is short.** Translating the compute shaders
  of the whole game ahead of their first use holds 6.6 GB of RAM in a Saros session (23.1 GB
  against 16.5 GB at one spot): it is done on a PC with more than 40 GB of RAM, and left to
  the first use (about 13 ms each) on one with less; `KYTY_PREPARE_CREATED=1` or `=0` decides
  it by hand. The pipelines a session keeps ready for their first use take about 0.7 GB of
  video memory, and the background builds at the start of a session leave 0.9 to 2.6 GB
  behind in the driver: when video memory comes near its limit the kept pipelines are
  released and the builds not started yet are left undone.
- **The table that tells shaders where a buffer lives takes the video memory it needs** (after
  the DS fork). It has an entry for every 16 KiB of the game's address space and was made whole:
  768 MiB of video memory on every card, nearly all of it zeros. It now starts at 64 MiB and
  grows to the highest buffer the game registers, 336 MiB in Saros: video memory in use at a
  test spot went from 8.1 to 7.7 GB. `KYTY_BDA_TABLE_GIB=0` keeps it whole.
- **A card short of video memory does not build pipelines beside the game.** Those builds keep
  about 1.6 GB of video memory at a heavy spot (the pipelines held ready, and what the driver
  keeps of each pipeline it made), and giving them up later does not give the memory back. With
  a video memory budget under 13 GiB (cards of 12 GB and less) a session leaves them out: what a
  precompile pass built comes from the driver cache at its first use, anything else is compiled
  then. `KYTY_SESSION_PRECOMPILE=1` builds them on any card, `=0` on none.
- **A buffer is made in video memory even when the budget is used up**, and in system memory
  only when the card has none left. Before, the allocator put it in system memory without a
  word once the budget was reached, where the GPU reads it over the bus at every draw until the
  game is restarted (seen on the DS fork). The log warns when it happens all the same.
- The allocator takes video memory in blocks of 64 MiB instead of 256 MiB on cards with less
  than 13 GiB (from the DS fork, where it gave 2 to 6% on a card of 8 GB; not measured here).
- **A session no longer writes the shader cache again when nothing was added to it.** After a
  precompile pass every session wrote the whole file twice (2 GB for Saros, and as much memory
  for the copy while it did).
- **The log names the PC's memory**: RAM, free RAM and what can still be committed, at the
  start and again with a fatal error. Running out of memory shows as many things (a lost
  graphics device, a crash inside the game); a report can now be checked for it.
- With 12 GB of video memory or 16 GB of RAM Saros is still at the edge; a lower 3D resolution
  and no frame generation leave more room.

### Smaller things

- `KYTY_HOST_GAMEPADS=0` makes the emulator ignore the PC's gamepads.
- A fatal error ends the process at once instead of hanging in library shutdown.
- The emulator opts out of Windows power throttling.

## Performance

At a heavy spot of Saros (about 1,560 draws, 1,120 indirect draws and 830 dispatches a frame, no
frame generation) the branch went from 29.7 fps to about 35 fps at the game's own 3D resolution
during this work, and runs at about 39 to 40 fps at 50% 3D resolution. At a lighter spot the
unmodified `saros` branch gave 37 fps and the first batch of changes below 47.9.

### The thread that feeds the GPU

- **Vulkan commands are recorded on a thread of their own**: 38.0 to 40.9 fps.
- **Buffers are synchronized only where shaders read them through the page table**: 40.4 to
  47.9 fps at the lighter spot, the largest single step. Guest write faults fell from about
  3,700 to 840 a frame.
- **Draws that bind the same small constant buffer share one copy of it** (idea from the DS
  fork): copies 10,900 to 2,600 a frame, 2.6 ms less CPU a frame, about +2.3 fps at 50%.
- **Saros's main texture table stays resident on the GPU**: the table has about 200,000 entries
  and 2,100 textures, and was rebuilt at every draw that reads it. About +1 fps at 100% and
  +3.4 fps at 50%.
- **The look-ahead that prepares resource tables on helper threads plans again where the command
  stream leaves what it scanned** (command lists the game writes on the GPU): table walks left to
  the GPU thread fell from 226 to 53 a frame, 34.1 to 35.1 fps.
- **No barrier between storage writes that draws repeat** (from the DS fork): 30.2 to 31.1 fps.
- **A read that no pending label can touch skips the label list's lock**: about 1 ms less CPU a
  frame.
- **Address ranges are kept in blocks of sorted intervals instead of a tree** (from the DS fork):
  the sets of GPU-written, pending and mapped ranges took 3.3% of the GPU thread at the heavy
  spot and take 1.1%; 24.3 to 23.6–24.0 ms of that thread a frame at 50%. The frame rate moved
  within what two runs differ by (40.4–40.7 before, 40.1–41.6 after).
- The GPU thread runs above the game's own threads.

### Readbacks (the game reading what the GPU wrote)

- **A game thread finishes its own readback** instead of waiting for the GPU thread: 29.6 to
  31.3 fps, and the slowest 5% of frames from 49.6 to 33.8 ms.
- **Only the pages a store or a table walk asks for are read back**: 38.8 to 40.3 fps.
- **Readbacks over 64 KiB go through the download buffer**: 40.7 to 41.6 fps, worst frame 66 to
  34 ms.
- In-place readbacks copy with streaming loads.

### Textures

- **A rendered image is no longer uploaded again from stale memory when a small unrelated buffer
  is written at its address** (Unreal reuses one block of memory for several resources in a
  frame): image uploads 320 to 130 MiB a frame, about +1 fps at 100%.

### Shader translation

- **Compute programs whose 64 lanes do not depend on each other run one lane per invocation**
  (from the DS fork): 357 of Saros's 563 such programs qualify; GPU time -1.7%.
- **One module per compute shader serves whatever the game binds**: buffer formats, strides,
  texture channel orders and level counts are read at run time, which is what lets the whole
  game's compute shaders be built before play.

### Switches for comparing

Most of the changes above can be turned off while the game runs, through the live channel below:
`vulkan_recording`, `bda_lazy`, `stream_share`, `heap_by_content`, `lookahead_replan`,
`guest_image_writes`, `image_buffer_writes`, `guest_copy`, `readback_narrow`. Each also reads an
environment variable `KYTY_<NAME>` at start.

## Tools for measuring

- **Live channel** (`KYTY_LIVE_FILE=<file>`): commands written to a file while the game runs.
  `measure <seconds>` reports fps, frame times, GPU time and per-frame counters; `prof` samples a
  thread and can look inside one function; `set <switch> <value>` flips a switch; `mem` searches,
  reads and writes the game's memory; `pad` and `stick` send controller input.
- **A performance log of a whole play session** (`KYTY_PERF_LOG=<seconds>`, for example
  `KYTY_PERF_LOG=10` in the launcher's Environment field): every such window of play writes one
  `Perf <time of day> ...` line with the numbers of `measure` into the log, so a session played
  through any part of the game says afterwards where it was slow and whether the CPU or the GPU
  was the limit there. Windows without a frame (loading) are left out.
- **A card with less video memory, for tests on a bigger one** (`KYTY_SIMULATE_VRAM_MB=<n>`, from
  the DS fork): the rest of the card's memory is taken at start and kept, so the driver and
  Windows treat the game as on a card of n MiB.
- **An upload worker** for buffer synchronization exists behind `async_upload` and is off: it did
  not pay on Saros.
- `build-windows.cmd` can build with profiling symbols and with a locally trained PGO profile.

## Not done, and known limits

- Table mode of the DS fork is not ported: as written it takes only draws without bound vertex
  buffers, which is 3% of Saros's draws. Its ideas that carry over are in the list above.
- The DS fork's merged page protections are not ported: they join protections of adjacent
  pages that follow each other, of which Demon's Souls has about 110 a frame and Saros one.
- Cards of 12 GB and less are not confirmed. With the rest of a 24 GB card taken away
  (`KYTY_SIMULATE_VRAM_MB=12288`, on a desktop whose other programs held 5 GB of it) Saros ran
  at 20 fps at 100% 3D resolution and at 52 fps at 50%, where the whole card gives 53 and 60:
  Windows kept 1.3 to 2 GB of the game's video memory in system memory.
- DLSS Super Resolution is not included; the game's own upscaler makes the picture.
- A shader that reads the resident texture table is another compiled variant than before. It is
  built at first use in each part of the game and cached from then on.
- CPU writes into memory that a rendered image also uses still cause whole-image uploads (about
  75 MiB a frame at 100% on Saros).
- Frame generation, the launcher app and the run scripts are Windows only, and so is reading the
  shaders out of a game's files: `ooz_decode`, the program that unpacks Unreal's shader
  libraries for it, is built for Windows only. It is a separate program under GPL-3.0-or-later
  (the ooz decompressor by Powzix), started by the precompile pass and never linked with the
  emulator (`tools/ooz-decode/README.md`).
