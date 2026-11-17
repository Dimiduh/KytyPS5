# Changes in this fork

This branch (`saros-port`) starts from the `saros` branch of
[CarRamRod-963/KytyPS5](https://github.com/CarRamRod-963/KytyPS5) and adds the work below, most of
it for Saros (PPSA07631) on Windows. Several pieces are ported from, or follow ideas of, the
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
  `shader-seeds/`), not only the ones an earlier session met.
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
- Details: [docs/shader-precompile.md](docs/shader-precompile.md).

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
- **An upload worker** for buffer synchronization exists behind `async_upload` and is off: it did
  not pay on Saros.
- `build-windows.cmd` can build with profiling symbols and with a locally trained PGO profile.

## Not done, and known limits

- Table mode of the DS fork is not ported: as written it takes only draws without bound vertex
  buffers, which is 3% of Saros's draws. Its ideas that carry over are in the list above.
- DLSS Super Resolution is not included; the game's own upscaler makes the picture.
- A shader that reads the resident texture table is another compiled variant than before. It is
  built at first use in each part of the game and cached from then on.
- CPU writes into memory that a rendered image also uses still cause whole-image uploads (about
  75 MiB a frame at 100% on Saros).
- Frame generation, the launcher app and the run scripts are Windows only.
