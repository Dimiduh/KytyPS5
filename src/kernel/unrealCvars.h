#ifndef EMULATOR_SRC_KERNEL_UNREALCVARS_H_
#define EMULATOR_SRC_KERNEL_UNREALCVARS_H_

#include <string>
#include <vector>

// --unreal-cvar <name>=<number> (repeatable; run-windows.ps1 -UnrealCvar), --unreal-render-scale
// <percent> (run-windows.ps1 -RenderScale): console variables of an Unreal Engine title, set in
// the running game.
//
// A shipped Unreal title keeps its console variables (r.ScreenPercentage and some thousands
// more) and reads most of them every frame, but a console has no way to change them: no command
// line, no console, the configuration files inside the package. They decide what the title
// spends its frame on - above all at what size it renders - and that is tuned for the console's
// GPU, not for an emulator whose frame is bound elsewhere. Saros renders its 3D at 1600x900 to
// 2304x1296 and makes a 3840x2160 picture of it (r.ScreenPercentage 50 with dynamic resolution
// 41.67-60; no 1080p output mode: it ignores the display's size). Measured at one spot (same
// session, RTX 4090): 31 fps as it is; 33 with the 3D at three quarters of that size and 36 at
// half (r.DynamicRes.Min/MaxScreenPercentage 20.8-30), the 4K picture still made by its
// upscaler; a 1080p picture stretched to the output instead
// (r.SecondaryScreenPercentage.GameViewport=50) with the 3D as it was: 32, no gain. The 3D's
// size is GPU time, and GPU time is what the GPU thread waits for at its stops.
//
// <name>=x<factor> sets the title's own value times the factor, and follows the title when it
// sets another. --unreal-render-scale <percent> is that for the three variables that size the
// 3D: r.ScreenPercentage and the dynamic resolution's bounds.
//
// The variables are found in the guest's memory, nothing of the title is patched: the console
// manager maps names (FString) to objects (IConsoleObject*) in one array of elements; the
// element of r.ScreenPercentage (which every version has, a float) is found by its name's
// UTF-16 text and a pointer to that text, and gives the float class's vtable and where a value
// is kept (the two equal floats in its object: the game thread's and the render thread's
// copies). The elements around it are the other variables. A variable of the float class or of
// the int class (the class of r.VSync) takes the number in both copies; one of another class (a
// reference to a global, a string, a bool) is refused. A helper thread looks for the variables
// once the title runs and from then on puts a value back when the title changed it (its
// settings menu, a level's device profile).
//
// What this cannot do: run a variable's change callback, or the scalability groups (sg.*),
// which act through one; a variable the engine reads once at start keeps what it read.
namespace Libs::LibKernel::UnrealCvars {

// Starts the helper thread for the assignments ("name=number"); nothing without any.
// "upscaler?name=number" is set only once WithUpscaler was called: the size the emulator's
// upscaler wants the 3D at (--upscaler-render-percent) must not reach a title whose own upscaler
// still makes the picture, because that upscaler could not be replaced on this PC.
void Start(const std::vector<std::string>& assignments);
void Stop();
// Any thread: the emulator's upscaler makes the title's picture in place of the title's own.
void WithUpscaler();

} // namespace Libs::LibKernel::UnrealCvars

#endif // EMULATOR_SRC_KERNEL_UNREALCVARS_H_
