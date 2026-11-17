# DLSS upscaling in place of the game's own upscaler

NVIDIA DLSS Super Resolution makes the game's picture from its 3D, instead of the upscaler the
game brings. Today for Saros (PPSA07631), which is Unreal Engine 5 with Temporal Super
Resolution (TSR). Windows only.

Why: Saros renders its 3D at 1600x900 to 2304x1296 and makes a 3840x2160 picture of it. The
emulator's frame is bound by the graphics card's time, so a smaller 3D is the one setting that
buys frames ("3D resolution" in the launcher), and the game's upscaler does poorly with a small
one: thin and distant things break up into stairs and dots. DLSS makes a cleaner picture of the
same 3D, most of all of a small one.

It needs:

- an NVIDIA RTX graphics card;
- DLSS's model, `nvngx_dlss.dll`, which the emulator does not ship. It is in NVIDIA's Streamline
  SDK (a release zip from <https://github.com/NVIDIA-RTX/Streamline/releases>, unpacked), the
  same folder frame generation takes its files from.

## Use

The launcher's "Upscaling" row, or:

```
.\run-windows.ps1 -Dlss performance           # quality, balanced, performance or ultra
.\run-windows.ps1 -Dlss performance -StreamlineDir D:\streamline-sdk
.\run-windows.ps1 -Dlss off                   # the game's own upscaler
```

| Mode | 3D size | of the 4K picture |
|---|---|---|
| quality | 2560x1440 | 66.7% |
| balanced | 2227x1253 | 58% |
| performance | 1920x1080 | 50% |
| ultra (ultra performance) | 1280x720 | 33.3% |
| the game's own | 1600x900 to 2304x1296, picked as it goes | 41.7% to 60% |

A mode sets the 3D to one size; the game's dynamic resolution and "3D resolution" do not apply
then. DLSS and frame generation work together.

Underneath: `--upscaler DLSS` and `--upscaler-render-percent <percent of the output's size>`.
Without the second the game's own sizes are upscaled as they come, a new DLSS feature for every
size. `--dlss-folder <folder>` names the model's folder; without it the emulator looks where
frame generation looks for Streamline (`KYTY_STREAMLINE_DIR`, a `streamline` folder next to the
emulator, `_Build\deps\streamline\sdk`; a folder may be the SDK's own, its `bin\x64` is taken).
`--dlss-preset <A-O>` picks the model's preset.

The console and the log say what happened:

```
DLSS: upscaling in place of the game's own upscaler with the model in D:\streamline-sdk\bin\x64
game upscaler: the picture of CS 0x13c0d5cfcdb64669 made in the game's place: 1920x1080 of 1920x1080 to 3840x2160 (layer 3 of 4)
```

When DLSS cannot be loaded (another card, no model) a warning says why, the game's upscaler
makes the picture and the 3D keeps the game's own sizes.

## Measured

One spot of Saros, one boot per setting within the same hour (RTX 4090, occlusion queries on,
no frame generation, a test build):

| Setting | 3D size | fps |
|---|---|---|
| the game's own upscaler | 1600x900 (what the game picks there) | 41.7 |
| the game's own, "3D resolution" 50% | 800x450 | 49.2 |
| DLSS quality | 2561x1441 | 32.8 |
| DLSS balanced | 2228x1253 | 39.5 |
| DLSS performance | 1920x1080 | 41.3 |
| DLSS ultra | 1280x720 | 46.7 |

At the same 3D size DLSS costs about as much as the game's upscaler: at 1920x1080, switched in
one session, 42.2 fps with the game's and 41.3 with DLSS (40.0 before the game's last pass was
left out). So ultra is both faster and cleaner than the game as it is, and performance gives
the game's frame rate from a third more pixels.

What the pictures show (pixel-for-pixel captures of the 4K picture, switched in one session):
the game's upscaler leaves distant thin things (chains, railings, the edge of a pillar against
the sky) as stairs and dotted lines at 1600x900, and as blotches at 800x450; DLSS draws them as
lines at 1600x900 and 1280x720, softer but whole at 800x450. Near surfaces look alike. While
the camera turns DLSS shows no trails.

## How it works

The game's upscaler runs as a chain of compute passes at the end of the 3D. Its first pass has
the frame's depth and writes its motion vectors; frame generation already finds that pass and
takes both (`FrameGen::FindInputs`, `CaptureInputs`, [frame-generation.md](frame-generation.md)),
along with the frame's render size and the projection's jitter from the upscaler's constants.
With `--upscaler` the same capture runs without frame generation.

The last pass ("update history") makes the picture. It is found by its images, not by its
shader (`FrameGen::FindUpscalerPass`): it samples the frame's color at the render size (the
scene with its translucency, written by the pass before) and its history, an array at the output
size whose layers are the last frames' pictures, and writes the frame's picture into one layer
of that array. `RenderExecutor::UpscaleGameFrame` then has DLSS write that layer from the color,
the captured depth and motion vectors and the jitter. Everything after the upscaler (bloom, tone
mapping, the interface) reads the layer as the game's own, and so does the game's next frame.

The game's last pass is left out once DLSS has taken a frame of the same size: its dispatch is
not recorded. Besides the picture it writes a quarter-size copy of it, which the exposure reads
and which is made from DLSS's picture instead (`ScaleLinear`), and its history's weights, which
only that pass reads. The other passes of the game's upscaler run as they are; the color DLSS
takes comes from one of them.

What had to be found:

- The jitter is the engine's as it is (pixels, y down). Negated in x the picture blurs, negated
  in y edges come out as stairs.
- The motion vectors are the ones frame generation decodes: the way to the pixel's previous
  position in parts of the frame, so their scale is the render size.
- The engine stores the scene's color multiplied by a factor that follows the exposure
  (pre-exposure). It stands in the view constants with its inverse next to it, and DLSS is
  told, so its history stays right while the exposure moves.
- DLSS's modes take half the output's size and more, ultra performance one size only. The
  game's own 1600x900 is in none of them; a feature of exactly the asked size is made then.
- The NVIDIA driver refuses the device DLSS's extensions ask for unless address space below
  1 TB is free, as with Streamline: the same range is held from the first moment of `main`.

## Another game

`FindUpscalerPass` and `FindInputs` describe Unreal Engine 5's TSR as Saros runs it. Another
UE5 title with TSR has a chance of matching as it is. A title with another upscaler needs its
own description: which pass has depth and motion vectors, in which encoding, where the jitter
is, and which pass writes the picture.

## Not done

- FSR in place of the game's upscaler. The emulator's FSR path (PS5 Pro's PSSR) reads whole
  one-layer images; the picture here is one layer of an array.
- An exposure image is not passed to DLSS, which works the exposure out of the color; the
  factor the engine stores its color with (the pre-exposure, 0.06 to 8 within one boot) is.
- A smaller output than 3840x2160. The picture's size does not change the frame rate here.

## Comparing

Live switches (the live channel, or `KYTY_<NAME>` at start): `game_upscaler` 0 lets the game's
upscaler make the picture although DLSS is loaded; `game_upscaler_skip` 0 runs the game's last
pass as well; `present_crop` 1 to 9 shows a part of the picture pixel for pixel instead of all
of it scaled to the window (5 is the center).
