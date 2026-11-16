# Unreal Engine titles: the game's own settings

A shipped Unreal Engine title still has the engine's console variables (`r.ScreenPercentage` and
several thousand more) and reads most of them every frame. On a console nothing can change them:
there is no command line and no console, and the configuration files are inside the package. The
emulator can set them in the running game. Nothing of the game is patched.

## Options

| Emulator option | `run-windows.ps1` | Launcher | What it does |
|---|---|---|---|
| `--unreal-render-scale <percent>` | `-RenderScale <percent>` | "3D resolution" | The size the 3D is rendered at, in percent of the game's own. The game's upscaler still makes the output's picture. |
| `--unreal-cvar <name>=<number>` | `-UnrealCvar <name>=<number>,...` | "Extra arguments" | Sets a console variable and keeps it set. Repeatable. |
| `--unreal-cvar <name>=x<factor>` | `-UnrealCvar <name>=x<factor>` | | The game's own value times the factor; follows the game when it sets another value. |

`--unreal-render-scale` is `x<percent / 100>` for `r.ScreenPercentage`,
`r.DynamicRes.MinScreenPercentage` and `r.DynamicRes.MaxScreenPercentage`.

Only float and int variables that the engine reads as it runs can be set. Variables of another
kind (a reference to a global, a string, a bool) are refused with a line in the log. A variable's
change callback does not run, so the scalability groups (`sg.*`) do nothing, and a variable the
engine reads once at start keeps what it read.

The emulator's console says what it did:

```
Unreal console variables: 7042 found in the title's memory
Unreal console variables: r.DynamicRes.MinScreenPercentage = 20.833 (the title had 41.666 as a float, ...)
```

## Saros

Saros renders its 3D at 1600x900 to 2304x1296 (`r.ScreenPercentage` 50, dynamic resolution
41.67 to 60 percent of the 3840x2160 output) and its upscaler (TSR) makes the 4K picture. It has
no 1080p output mode: it asks for the display's size and ignores the answer.

Measured in one session at one spot (1,570 draws, 1,120 indirect draws and 830 dispatches a frame;
RTX 4090, i9-13900K, no frame generation, a test build):

| Output | 3D size | fps | GPU time a frame |
|---|---|---|---|
| 4K | the game's own | 31 | 21.5 ms |
| 4K | 75% | 33.3 | 19.2 ms |
| 4K | 50% | 35.9 | 16.3 ms |
| 1440p (`r.SecondaryScreenPercentage.GameViewport=66.667`) | the game's own | 28.9 | 22.7 ms |
| 1080p (`...GameViewport=50`) | the game's own | 32.2 | 20.2 ms |
| 1080p | 50% | 35.8 | 16.0 ms |

The size of the output's picture does not matter; the 3D's size does. The frame is bound by the
emulator's GPU thread, which does about 25 ms of work a frame and stops several times a frame
until the graphics card has caught up. A smaller 3D picture shortens those stops; it does not
shorten the 25 ms.

Other variables tried there: `r.Shadow.Virtual.Enable=0` is slower (the engine falls back to a
path with more draws and dispatches); `r.Lumen.Reflections.Allow=0` and
`r.Lumen.DiffuseIndirect.Allow=0` change nothing when set in the running game.

## How the variables are found

The console manager maps names to objects in one array of 32-byte elements: an `FString` (text
pointer, count, capacity), the `IConsoleObject*`, and the set's links. The emulator looks for the
UTF-16 text `r.ScreenPercentage` in the game's memory and for a pointer to that text; the element
it finds gives the vtable of the float variables and where an object keeps its value (two equal
floats: the game thread's and the render thread's copies). The elements around it are the other
variables. The int class is that of `r.VSync`. A helper thread does this once the game runs (two
passes over the game's memory, about 20 s) and from then on puts a value back when the game
changed it. Source: `src/kernel/unrealCvars.{h,cpp}`.

## Looking for more

The measurement channel (`KYTY_LIVE_FILE`) has `mem` commands to search, read and write the
game's memory while it runs: `mem find16 <text>`, `mem findq <hex value>`, `mem read <hex address>
<count>`, `mem write <hex address> <hex bytes>`, `mem dump <hex address> <hex size> <file>`,
`mem maps`. With them a variable can be changed and measured in the same session (`measure`)
before it becomes an option.
