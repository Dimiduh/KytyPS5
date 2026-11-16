using System;
using System.IO;
using System.Text;
using System.Threading;
using System.Windows;

namespace KytyLauncher {

// Command line (a double-click passes none):
//   --settings <file>      that settings file in place of launcher-settings.json (launcher.ps1 -SettingsFile)
//   --play                 the game started with the saved settings, without this window (a shortcut, Steam's launch
//                          options); this process lasts until the game's console closes. No game there: the window.
// For tests (none of them shows a window, starts anything or writes the settings file):
//   --dry-run [<file>]     what launcher.ps1 -DryRun prints: the play and precompile commands the settings make and what
//                          was found on this PC, written to <file> (UTF-8); without one to the standard output, which
//                          has to be redirected (KytyLauncher.exe --dry-run | more)
//   --screenshot <folder>  each page rendered to <folder>\<n>-<page>.png, then exit
//   --size <WIDTHxHEIGHT>  (with --screenshot) the pictures' size in place of the window's (a page longer than it)
//   --lang en|zh           the window's language for this run (the saved choice otherwise)
//   --no-art               (with --screenshot) no game art in the banner
//   --demo-path <text>     (with --screenshot) the game path box shows <text>
public partial class App : Application {
	protected override void OnStartup(StartupEventArgs e) {
		base.OnStartup(e);
		string dryRun = null, screenshots = null, lang = null, settings = null, size = null;
		var args = e.Args;
		for (int i = 0; i < args.Length; i++) {
			var value = i + 1 < args.Length && !args[i + 1].StartsWith("--", StringComparison.Ordinal) ? args[i + 1] : null;
			if (args[i] == "--dry-run") dryRun = value ?? "";
			else if (args[i] == "--screenshot") screenshots = value;
			else if (args[i] == "--lang") lang = value;
			else if (args[i] == "--settings") settings = value;
			else if (args[i] == "--size") size = value;
			else if (args[i] == "--demo-path") KytyLauncher.MainWindow.ScreenshotPath = value;
			else continue;
			if (value != null) i++;
		}
		KytyLauncher.MainWindow.ScreenshotNoArt = Array.IndexOf(args, "--no-art") >= 0;
		if (dryRun != null || Array.IndexOf(args, "--screenshot") >= 0) {
			// (No dialog either: what went wrong goes where the result would have gone.)
			int code;
			try {
				var context = new LauncherContext(settings);
				context.Settings.ReadOnly = true;
				if (lang != null) Strings.Language = lang == "zh" ? UiLanguage.Chinese : UiLanguage.English;
				if (dryRun != null) {
					if (lang == null) Strings.Language = UiLanguage.English;
					Write(dryRun, DryRun(context));
				} else {
					if (screenshots == null) throw new ArgumentException("--screenshot takes a folder");
					new MainWindow(context).SaveScreenshots(screenshots, size);
				}
				code = 0;
			} catch (Exception error) {
				try {
					Write(dryRun ?? (screenshots == null ? "" : Path.Combine(screenshots, "error.txt")), "error:      " + error + Environment.NewLine);
				} catch (Exception) {
				}
				code = 1;
			}
			Shutdown(code);
			return;
		}
		var launcher = new LauncherContext(settings);
		if (lang != null) Strings.Language = lang == "zh" ? UiLanguage.Chinese : UiLanguage.English;
		if (Array.IndexOf(args, "--play") >= 0 && GameInfo.Read(launcher.Settings.Str("game")).Found) {
			try {
				var process = new Commands(launcher).StartPlay(true);
				// A shortcut's or Steam's "game" is this process: it lasts as long as the game's console.
				new Thread(() => {
					process.WaitForExit();
					var code = process.ExitCode;
					Dispatcher.BeginInvoke(new Action(() => Shutdown(code)));
				}) { IsBackground = true }.Start();
			} catch (Exception error) {
				MessageBox.Show(Strings.F("error.start", error.Message), Strings.T("error.title"), MessageBoxButton.OK, MessageBoxImage.Error);
				Shutdown(1);
			}
			return;
		}
		var window = new MainWindow(launcher);
		base.MainWindow = window;
		window.Show();
	}

	// What launcher.ps1 -DryRun prints, line for line: the commands the buttons would run, then what they rest on.
	static string DryRun(LauncherContext context) {
		var commands = new Commands(context);
		var settings = context.Settings;
		var text = new StringBuilder();
		text.AppendLine("play:       cmd " + commands.PlayArguments());
		text.AppendLine("precompile: cmd " + commands.PrecompileArguments(context.PrecompileResultFile()));
		text.AppendLine("settings:   " + context.SettingsPath);
		text.AppendLine("emulator:   " + context.EmulatorExe + (File.Exists(context.EmulatorExe) ? "" : " (missing: build it with build-windows.cmd)"));
		text.AppendLine("data:       " + context.DataFolder() + "; logs: " + context.LogsFolder());
		text.AppendLine("framegen:   " + (context.FrameGenAvailable
			? "NVIDIA's Streamline runtime in " + context.Streamline + "; chosen: " + (settings.Int("framegen") >= 2 ? settings.Int("framegen") + "x" : "off")
			: !context.RtxGpu ? "no NVIDIA RTX graphics card"
			: "no NVIDIA-signed Streamline runtime (settings: '" + settings.Str("streamline") + "')"));
		var gpus = new string[context.VulkanGpus.Length];
		for (int i = 0; i < gpus.Length; i++) gpus[i] = i + ": " + context.VulkanGpus[i];
		text.AppendLine("gpus:       " + (gpus.Length != 0 ? string.Join(", ", gpus) : "Vulkan lists none") + "; chosen: " +
		                (settings.Int("gpu") >= 0 ? settings.Int("gpu").ToString(System.Globalization.CultureInfo.InvariantCulture) : "automatic"));
		foreach (var problem in commands.Problems()) text.AppendLine("note:       " + problem);
		return text.ToString();
	}

	// To a file, or to the standard output without one (nothing shows when that is not redirected: a window program has
	// no console).
	static void Write(string file, string text) {
		var bytes = new UTF8Encoding(false).GetBytes(text);
		if (!string.IsNullOrEmpty(file)) {
			File.WriteAllBytes(file, bytes);
			return;
		}
		using (var output = Console.OpenStandardOutput()) {
			output.Write(bytes, 0, bytes.Length);
			output.Flush();
		}
	}
}

}
