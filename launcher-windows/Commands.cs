using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace KytyLauncher {

// The choices with a fixed list of values, as launcher.ps1 has them.
static class Options {
	// The window sizes offered (the KytyPS5 launcher's and two smaller ones); another WIDTHxHEIGHT can be typed.
	public static readonly string[] Resolutions = { "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160", "2560x1080", "3440x1440", "3840x1600" };
	// How frames reach the screen (run-windows.ps1 -PresentMode, the emulator's --present-mode; Mailbox is its default).
	public static readonly string[] PresentModes = { "Mailbox", "Fifo", "Immediate" };
	// The emulator's own log (run-windows.ps1 -Log, the emulator's --printf-direction).
	public static readonly string[] LogModes = { "off", "file", "console" };
	// The size the game renders its 3D at, in percent of its own (run-windows.ps1 -RenderScale).
	public static readonly int[] RenderScales = { 100, 85, 75, 67, 50 };
	// What makes the 4K picture (run-windows.ps1 -Dlss): the game's own upscaler (off), or DLSS in one of its modes, each
	// with the one size its 3D is rendered at.
	public static readonly string[] DlssModes = { "off", "quality", "balanced", "performance", "ultra" };
	public static readonly string[] DlssSizes = { "", "2560x1440", "2227x1253", "1920x1080", "1280x720" };

	// `value` in the list's spelling; the list's first for anything else.
	public static string Pick(string[] list, string value) {
		return list.FirstOrDefault(item => string.Equals(item, value, StringComparison.OrdinalIgnoreCase)) ?? list[0];
	}

	// A window size as WIDTHxHEIGHT (each 320 to 16384, the KytyPS5 launcher's limits); "" for other text.
	public static string Resolution(string text) {
		var match = Regex.Match(text ?? "", @"^\s*(\d{3,5})\s*[xX]\s*(\d{3,5})\s*$");
		int width, height;
		if (!match.Success || !int.TryParse(match.Groups[1].Value, NumberStyles.None, CultureInfo.InvariantCulture, out width) ||
		    !int.TryParse(match.Groups[2].Value, NumberStyles.None, CultureInfo.InvariantCulture, out height)) return "";
		if (width < 320 || width > 16384 || height < 320 || height > 16384) return "";
		return width.ToString(CultureInfo.InvariantCulture) + "x" + height.ToString(CultureInfo.InvariantCulture);
	}
}

// The command lines the buttons run: run-windows.ps1 in a console window of its own, as launcher.ps1 starts it
// (Get-RunArguments, Get-PlayCommand, Get-PrecompileCommand), so that both launchers stay one path.
sealed class Commands {
	readonly LauncherContext context;

	public Commands(LauncherContext context) {
		this.context = context;
	}

	Settings S { get { return context.Settings; } }

	// Windows PowerShell and cmd.exe by their paths: on a PC whose PATH lacks its folder cmd.exe does not know
	// "powershell", and a file named "cmd" in the working folder makes Windows ask which app should open it.
	static string PowerShell() {
		var exe = Path.Combine(Environment.SystemDirectory, "WindowsPowerShell\\v1.0\\powershell.exe");
		return File.Exists(exe) ? "\"" + exe + "\"" : "powershell";
	}

	public static string Cmd() {
		var comspec = Environment.GetEnvironmentVariable("ComSpec");
		return !string.IsNullOrEmpty(comspec) && File.Exists(comspec) ? comspec : Path.Combine(Environment.SystemDirectory, "cmd.exe");
	}

	// A value as one argument of a command line: in double quotes, a quote inside as \" and the backslashes before a
	// quote doubled (the folder D:\data\ would otherwise end in an escaped quote).
	public static string Quoted(string text) {
		var escaped = Regex.Replace(text ?? "", "(\\\\*)\"", "$1$1\\\"");
		return "\"" + Regex.Replace(escaped, "(\\\\+)$", "$1$1") + "\"";
	}

	// Characters cmd would act on: the command goes through cmd, so text holding one is left out.
	static readonly char[] CmdCharacters = { '&', '|', '<', '>', '^', '!', '%' };

	// The extra arguments (settings extra_args): emulator options as on its command line, given to run-windows.ps1
	// -ExtraArgs in one piece. Left out: text with a character cmd would act on, text that does not start with an option
	// (--name) or leaves a double quote open. The key of what is wrong with them, or null.
	public string ExtraProblem() {
		var text = S.Str("extra_args").Trim();
		if (text.Length == 0) return null;
		if (text.IndexOfAny(CmdCharacters) >= 0) return "problem.extraCharacters";
		if (!Regex.IsMatch(text, "^--[A-Za-z]")) return "problem.extraStart";
		if (text.Count(c => c == '"') % 2 != 0) return "problem.extraQuote";
		return null;
	}

	public string ExtraArguments() {
		return ExtraProblem() != null ? "" : S.Str("extra_args").Trim();
	}

	// The environment switches (settings environment): KEY=VALUE pairs separated by spaces, commas or semicolons (a
	// separator starts a new pair only before KEY=, so a value may hold them), one -Set argument for run-windows.ps1.
	// A piece that is no KEY=VALUE, or holds a character cmd would act on, is left out.
	public string[] EnvironmentPieces() {
		var text = S.Str("environment").Trim(' ', ',', ';', '\t');
		if (text.Length == 0) return new string[0];
		return Regex.Split(text, @"[\s,;]+(?=[A-Za-z_][A-Za-z0-9_]*=)");
	}

	static bool IsEnvironmentPair(string pair) {
		return Regex.IsMatch(pair, "^[A-Za-z_][A-Za-z0-9_]*=") && pair.IndexOfAny(CmdCharacters) < 0 && pair.IndexOf('"') < 0;
	}

	public string[] EnvironmentPairs() {
		return EnvironmentPieces().Where(IsEnvironmentPair).ToArray();
	}

	// What of the two text boxes is left out of the command, for the window and --dry-run.
	public List<string> Problems() {
		var problems = new List<string>();
		var extra = ExtraProblem();
		if (extra != null) problems.Add(Strings.T(extra));
		var dropped = EnvironmentPieces().Where(piece => !IsEnvironmentPair(piece)).ToArray();
		if (dropped.Length != 0) problems.Add(Strings.F("problem.environment", string.Join(" ", dropped)));
		return problems;
	}

	// Frame generation as chosen and possible here: the multiplier (2 to 6), or 1 for off.
	public int FrameGen { get { return context.FrameGenAvailable && S.Int("framegen") >= 2 ? S.Int("framegen") : 1; } }

	// The upscaler as chosen and possible here: DLSS's mode, or "off" for the game's own.
	public string Dlss { get { return context.DlssAvailable ? S.Str("dlss") : "off"; } }

	// run-windows.ps1's arguments for the settings. The choices with a value (window size, language, present mode, log,
	// background shader preparation) are always passed, so that another default of the emulator or the launch config
	// changes nothing here.
	List<string> RunArguments() {
		var size = Options.Resolution(S.Str("resolution")).Split('x');
		if (size.Length != 2) size = new[] { "1920", "1080" };
		var arguments = new List<string> { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", Quoted(context.Root + "\\run-windows.ps1"),
			"-Width", size[0], "-Height", size[1], "-Language", Number(S.Int("language")),
			"-PresentMode", S.Str("present_mode"), "-Log", S.Str("log") };
		// Background shader preparation beside the game: on or off, as chosen (the emulator's --precompile-pipelines or
		// --no-precompile-pipelines).
		arguments.AddRange(new[] { "-PrecompilePipelines", S.Bool("precompile_pipelines") ? "on" : "off" });
		// The shared pipeline lists next to run-windows.ps1 (pipeline-lists\<title id>.pipelinelist) are its default;
		// off: none.
		if (!S.Bool("pipeline_lists")) arguments.AddRange(new[] { "-PipelineList", "none" });
		// The shader pairs next to run-windows.ps1 are its default too; off: none (a much shorter first precompile).
		if (!S.Bool("shader_pairs")) arguments.AddRange(new[] { "-ShaderPairs", "none" });
		// A graphics pipeline nothing has built yet: built in the background with its draws left out meanwhile (on), or
		// the game waits for it (off).
		arguments.AddRange(new[] { "-SkipPendingDraws", S.Bool("skip_pending_draws") ? "on" : "off" });
		// The game's occlusion queries: counted on the graphics card, so the game leaves out what it finds hidden (on),
		// or every object answered as visible (off).
		arguments.AddRange(new[] { "-OcclusionQueries", S.Bool("occlusion_queries") ? "on" : "off" });
		// Frame generation: the generated frames per rendered one and where NVIDIA's runtime is; 0 turns off what the
		// launch config may set.
		if (FrameGen >= 2) arguments.AddRange(new[] { "-FrameGen", Number(FrameGen - 1), "-StreamlineDir", Quoted(context.Streamline) });
		else arguments.AddRange(new[] { "-FrameGen", "0" });
		// The size the game renders its 3D at, in percent of its own (Unreal Engine titles); 100 takes out what the
		// launch config may set. (With a DLSS mode run-windows.ps1 leaves it out itself.)
		arguments.AddRange(new[] { "-RenderScale", Number(S.Int("render_scale")) });
		if (S.Bool("fullscreen")) arguments.Add("-Fullscreen");
		// (Without it the game's picture keeps its aspect ratio, between black bars in a window of another shape.)
		if (S.Bool("stretch")) arguments.Add("-Stretch");
		// HDR10 output when Windows HDR is on for the display; SDR otherwise.
		if (S.Bool("hdr")) arguments.Add("-Hdr");
		// The graphics card by its number in Vulkan's list, for the game and the precompile alike (the driver's pipeline
		// cache is the card's).
		if (S.Int("gpu") >= 0) arguments.AddRange(new[] { "-Gpu", Number(S.Int("gpu")) });
		// The emulator's performance panel (F2 in the game switches it).
		if (S.Bool("perf_overlay")) arguments.Add("-PerfOverlay");
		if (!S.Bool("sound")) arguments.Add("-NoSound");
		if (S.Bool("skip_notice_screen")) arguments.Add("-SkipNotice");
		if (S.Bool("redzone")) arguments.Add("-RedZone");
		if (S.Bool("data")) arguments.AddRange(new[] { "-DataDir", Quoted(context.DataFolder()) });
		var pairs = EnvironmentPairs();
		if (pairs.Length != 0) arguments.AddRange(new[] { "-Set", Quoted(string.Join(",", pairs)) });
		var extra = ExtraArguments();
		if (extra.Length != 0) arguments.AddRange(new[] { "-ExtraArgs", Quoted(extra) });
		if (S.Bool("game")) arguments.AddRange(new[] { "-Game", Quoted(S.Str("game")) });
		return arguments;
	}

	// The console stays for the live log; after a crash it waits for a key. direct (--play, a start from a shortcut or
	// from Steam, maybe with only a gamepad at hand): no wait for a key.
	public string PlayCommand(bool direct = false) {
		var arguments = RunArguments();
		// The upscaler, for the game only (the precompile pass shows no picture): DLSS's mode, or off, which also turns
		// off what the launch config may set. DLSS's model is in NVIDIA's runtime folder, which frame generation has
		// passed already when it is on.
		arguments.AddRange(new[] { "-Dlss", Dlss });
		if (Dlss != "off" && FrameGen < 2) arguments.AddRange(new[] { "-StreamlineDir", Quoted(context.Streamline) });
		arguments.Add("-Follow");
		return PowerShell() + " " + string.Join(" ", arguments) + (direct ? "" : " || pause");
	}

	// The precompile pass with the same settings (run-windows.ps1 -Precompile: the emulator's --precompile-only); its
	// summary also goes to `resultFile`, which the window shows. On the efficiency cores only when chosen (Shaders page:
	// run-windows.ps1 -Affinity, a hexadecimal mask).
	public string PrecompileCommand(string resultFile) {
		var arguments = RunArguments();
		arguments.Add("-Precompile");
		if (S.Bool("ecores") && context.EfficiencyMask != 0)
			arguments.AddRange(new[] { "-Affinity", context.EfficiencyMask.ToString("X", CultureInfo.InvariantCulture) });
		if (!string.IsNullOrEmpty(resultFile)) arguments.AddRange(new[] { "-ResultFile", Quoted(resultFile) });
		return PowerShell() + " " + string.Join(" ", arguments) + " & pause";
	}

	// cmd.exe's own arguments for a command (/s: the outer quotes are taken off, the command stays as written).
	public string PlayArguments(bool direct = false) {
		return "/s /c \"" + PlayCommand(direct) + "\"";
	}

	public string PrecompileArguments(string resultFile) {
		return "/s /c \"" + PrecompileCommand(resultFile) + "\"";
	}

	public Process StartPlay(bool direct = false) {
		return Start(PlayArguments(direct), direct);
	}

	// (--play: the console minimized, so that the game's window is the one in front.)
	public Process Start(string cmdArguments, bool minimized = false) {
		return Process.Start(new ProcessStartInfo(Cmd(), cmdArguments) { UseShellExecute = true, WorkingDirectory = context.Root,
			WindowStyle = minimized ? ProcessWindowStyle.Minimized : ProcessWindowStyle.Normal });
	}

	static string Number(int value) {
		return value.ToString(CultureInfo.InvariantCulture);
	}
}

}
