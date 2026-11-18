# Frame generation

DLSS Frame Generation through NVIDIA Streamline, after the Demon's Souls fork
(chenxiao07/KytyPS5, `src/local/frame-gen.cpp`): frames are made between the game's own, so the
screen shows two (or more) frames for every frame the game renders. Windows only.

It needs four things:

- an NVIDIA RTX 40 or newer graphics card. An RTX 40 makes one frame per rendered frame (2x);
  3x and up need an RTX 50. More than the card makes is cut down to what it makes.
- NVIDIA's Streamline runtime, which the emulator does not ship: a release zip of the Streamline
  SDK from <https://github.com/NVIDIA-RTX/Streamline/releases>, unpacked. Only files signed by
  NVIDIA are loaded.
- a build made with the SDK's headers: unpack the SDK to `_Build/deps/streamline/sdk` before
  configuring (CMake says `Frame generation: Streamline SDK found`). Without them the build has
  no frame generation and says so when it is asked for.
- a game the emulator knows the inputs of (below). Today: Saros (PPSA07631).

## Use

```
.\run-windows.ps1 -FrameGen 1                 # 1 generated frame per rendered frame (2x); up to 5
.\run-windows.ps1 -FrameGen 1 -StreamlineDir D:\streamline-sdk
```

or the launcher's "Frame generation" row. Underneath: `KYTY_FRAMEGEN=<generated frames>` and
`KYTY_STREAMLINE_DIR=<the SDK or its bin\x64>`; without the latter a `streamline` folder next to
the emulator, then `_Build\deps\streamline\sdk`. `KYTY_FRAMEGEN_VERBOSE=1` prints Streamline's
whole log.

The console says what happened: `Frame generation: DLSS-G ready`, then `DLSS-G on` once the
game's 3D view has run for ten frames, and `DLSS-G off` after thirty frames without it (menus,
movies and loading screens are shown as they are).

Other programs' Vulkan layers are left out while frame generation is on. The overlays of Steam,
Epic, GOG Galaxy and RTSS (MSI Afterburner) and OBS's game capture each put a layer of their own
into every game, and here they would run between Streamline and the driver, inside the calls
that make the instance and the device. One player's start ended there (a `--- Crash ---` note
that names `vulkan-1.dll`, on a PC with Epic's and RTSS's layers); whether a layer was the cause
is not known, and it does not happen on the PC this was made on. The emulator sets the Vulkan
loader's own switches for it (`VK_LOADER_LAYERS_DISABLE=~implicit~`, and
`VK_LOADER_LAYERS_ALLOW` for the graphics vendors' layers) unless the environment already names
one of them, and the log says `Vulkan layers left out`. So those overlays and OBS's game capture
do not show in such a session (OBS's window capture uses no layer).

- `KYTY_VULKAN_LAYERS=1` (the launcher's Environment field, or `-Set`) keeps the layers.
- `KYTY_VULKAN_LAYERS=0` leaves them out without frame generation too: something to try for any
  start that ends while the device is made.

## How it works

Streamline's `sl.interposer.dll` becomes the process' Vulkan loader (SDL is pointed at it), so
the instance, the device and the swapchain are made through it, and it shows the generated
frames inside `vkQueuePresentKHR`. The emulator gives it, for every presented frame, the game's
depth, its motion vectors and its camera (`upscaler/frameGeneration.cpp`):

1. `FindInputs`, at every compute dispatch, recognises the pass of the game that has them.
2. `CaptureInputs`, right after that dispatch, copies the depth and decodes the motion vectors
   into images of its own (the game may reuse its images' memory later in the frame).
3. `PrepareInputs`, at the guest flip, hands them to the presenter's frame.
4. `BeforePresent` tags them for Streamline around the frame's `vkQueuePresentKHR`.

Two things the emulator has to do for Streamline's sake:

- The NVIDIA driver makes the device Streamline asks for only while it finds address space below
  1 TB. The guest's address space takes everything up to 0xfc00000000 and the emulator's own
  mappings fill the rest, so the range up to 1 TB is held from the first moment of `main` and
  released right before `vkCreateDevice` (`HoldDriverAddressSpace`).
- Streamline's DLLs do not survive `ExitProcess`, so with it loaded the emulator ends without
  the DLLs' detach routines (`Common::QuickExit`).

## The game's side: Saros

Saros is Unreal Engine 5 with Temporal Super Resolution. It renders at up to 2304x1296 with
dynamic resolution and upscales to 3840x2160. TSR's first pass ("dilate velocity") is found by
its images:

| Image | Format | What |
|---|---|---|
| 0 | 16_16_16_16 UNORM | the scene's velocity |
| 1 | 32_FLOAT | the scene's depth (reversed) |
| 2 | 16_16_FLOAT | depths |
| 4 | 32_UINT | the frame's motion vectors, x in the low and y in the high 16 bits |

The motion vectors are in the engine's 16-bit encoding (the signed square root of the velocity,
scaled by 0.499 * 0.5 * sqrt(2) and biased by 32767/65535); the velocity is the pixel's screen
position (-1..1, y up) minus its previous one. `gpu_framegen_motion.comp` turns them into what
DLSS takes: the way to the previous position in parts of the frame.

The pass's two constant buffers hold the rest, as rows of four floats:

- buffer 0, the upscaler's constants: row 2 the frame's rectangle in the images (integers), row
  3 its size, row 16 the jitter in pixels and the camera-cut flag;
- buffer 1, the engine's view constants: rows 32-35 the projection without jitter, 72 the
  camera's position, 73-75 forward, up and right, 136-139 clip to previous clip, 146 the field
  of view, 148 the frame's size.

The camera rows are checked against the projection's shape before they are used; a build of the
game that lays them out differently still gets frame generation, without the camera.

## Another game

The Streamline side is the same for every game; `FindInputs` and what it reads are per game.
What to look for: the compute pass of the game's temporal anti-aliasing or upscaler that reads
depth and motion vectors, the unit and sign of those vectors, the size of the rendered frame
within its images, and the constants with the camera. The check that settles unit and sign: for
a still scene and a turning camera, the motion vector at a pixel must equal what the camera's
clip-to-previous-clip matrix gives for that pixel's depth.

## Not done

- The HUD is part of the picture DLSS interpolates, so it can wobble while the camera moves
  (the Demon's Souls fork has an optional HUD-less image for this).
- The game's frames are still shown on the emulator's 60 Hz flip schedule; below 60 fps they
  arrive unevenly, which generated frames cannot smooth out.
- AMD FSR frame generation, which the Demon's Souls fork also has. (DLSS super resolution in
  place of the game's upscaler: [dlss-upscaling.md](dlss-upscaling.md).)
