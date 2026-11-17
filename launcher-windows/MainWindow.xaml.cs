using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace KytyLauncher {

// An item of a combo box: what it shows and the value it stands for.
sealed class Choice {
	public readonly string Label;
	public readonly object Value;

	public Choice(string label, object value) {
		Label = label;
		Value = value;
	}

	public override string ToString() {
		return Label;
	}
}

partial class MainWindow : Window {
	// (--screenshot pictures: no game art, another path shown.)
	public static bool ScreenshotNoArt;
	public static string ScreenshotPath;

	readonly LauncherContext context;
	readonly Commands commands;
	readonly List<FrameworkElement> pages = new List<FrameworkElement>();
	readonly DispatcherTimer gameTimer;
	readonly DispatcherTimer precompileTimer;
	GameInfo game = new GameInfo();
	bool building;     // the controls are being made: their events change nothing
	bool built;        // every page has its controls
	bool syncingSize;  // the window size's list and box are being set to each other
	int pageIndex;
	// The precompile pass started from this window: its console, the file its summary goes to, when it began and what
	// there is to say while it runs; then whether its window was closed before the pass ended.
	Process precompileProcess;
	string precompileResult;
	DateTime precompileBegin;
	string precompileStatus;
	bool precompileClosed;

	TextBox gamePath, dataPath, resolutionText, extraArgs, environment;
	ComboBox resolution, present, upscaling, renderScale, frameGen, language, gpu, log, uiLanguage;
	CheckBox fullscreen, stretch, hdr, overlay, sound, skipNotice, redzone, background, lists, pairs, noWait, ecores, reopenLauncher, occlusion;
	TextBlock resolutionNote, renderNote, frameGenNote, streamlineText, bannerTitle, bannerLine, shaderLine, problemText;
	Button precompilePageButton;
	Image banner;
	Border bannerFallback;
	StackPanel checkList;
	Grid currentSettings;

	Settings S { get { return context.Settings; } }

	static string T(string key) {
		return Strings.T(key);
	}

	static string F(string key, params object[] values) {
		return Strings.F(key, values);
	}

	public MainWindow(LauncherContext context) {
		InitializeComponent();
		this.context = context;
		commands = new Commands(context);
		gameTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(450) };
		gameTimer.Tick += (s, e) => {
			gameTimer.Stop();
			LoadGame(gamePath.Text);
		};
		precompileTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
		precompileTimer.Tick += (s, e) => PrecompileTick();
		Nav.SelectionChanged += (s, e) => ShowPage(Nav.SelectedIndex);
		PlayButton.Click += (s, e) => Play();
		PrecompileButton.Click += (s, e) => Precompile();
		// (Back in front: a precompile started elsewhere may have ended.)
		Activated += (s, e) => UpdateChecks();
		Closing += (s, e) => ReadForm();
		// (A small screen: the window fits its work area.)
		var area = SystemParameters.WorkArea;
		if (Height > area.Height - 16) Height = Math.Max(MinHeight, area.Height - 16);
		if (Width > area.Width - 16) Width = Math.Max(MinWidth, area.Width - 16);
		Build();
	}

	T2 Res<T2>(string key) where T2 : class {
		return FindResource(key) as T2;
	}

	// ------------------------------------------------------------------ building the window

	void Build() {
		building = true;
		built = false;
		AppSubtitle.Text = T("app.subtitle");
		NavHint.Text = T("nav.hint");
		PlayButton.Content = T("button.play");
		PrecompileButton.Content = T("button.precompile");
		pages.Clear();
		PageHost.Children.Clear();
		Nav.Items.Clear();
		AddPage("nav.play", BuildPlayPage());
		AddPage("nav.display", BuildDisplayPage());
		AddPage("nav.graphics", BuildGraphicsPage());
		AddPage("nav.game", BuildGamePage());
		AddPage("nav.shaders", BuildShadersPage());
		AddPage("nav.advanced", BuildAdvancedPage());
		building = false;
		built = true;
		Nav.SelectedIndex = Math.Max(0, Math.Min(pageIndex, pages.Count - 1));
		ShowPage(Nav.SelectedIndex);
		LoadGame(S.Str("game"));
		UpdateBusy();
	}

	void AddPage(string navKey, FrameworkElement page) {
		// (Text, not a TextBlock: an element item takes its color from the list, not from its item.)
		Nav.Items.Add(T(navKey));
		page.Visibility = Visibility.Collapsed;
		pages.Add(page);
		PageHost.Children.Add(page);
	}

	void ShowPage(int index) {
		if (index < 0 || building) return;
		pageIndex = index;
		for (int i = 0; i < pages.Count; i++) pages[i].Visibility = i == index ? Visibility.Visible : Visibility.Collapsed;
	}

	FrameworkElement Page(string titleKey, params UIElement[] content) {
		var stack = new StackPanel { Margin = new Thickness(44, 32, 44, 40), MaxWidth = 880, HorizontalAlignment = HorizontalAlignment.Left };
		if (titleKey != null) stack.Children.Add(new TextBlock { Text = T(titleKey), Style = Res<Style>("PageTitle") });
		foreach (var element in content) stack.Children.Add(element);
		return new ScrollViewer { Content = stack, VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
			HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, Focusable = false };
	}

	TextBlock Section(string key) {
		return new TextBlock { Text = T(key), Style = Res<Style>("SectionTitle") };
	}

	TextBlock Desc(string text) {
		return new TextBlock { Text = text, Style = Res<Style>("Desc") };
	}

	// A setting: its name on the left, the control and what it does on the right.
	FrameworkElement Row(string labelKey, FrameworkElement control, FrameworkElement desc = null) {
		var grid = new Grid { Margin = new Thickness(0, 7, 0, 9) };
		grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(232) });
		grid.ColumnDefinitions.Add(new ColumnDefinition());
		grid.Children.Add(new TextBlock { Text = labelKey == null ? "" : T(labelKey), Style = Res<Style>("RowLabel") });
		var right = new StackPanel();
		right.Children.Add(control);
		if (desc != null) right.Children.Add(desc);
		Grid.SetColumn(right, 1);
		grid.Children.Add(right);
		return grid;
	}

	CheckBox Switch(bool value, bool enabled = true) {
		var box = new CheckBox { IsChecked = value, IsEnabled = enabled, Style = Res<Style>("Switch") };
		box.Checked += (s, e) => Changed();
		box.Unchecked += (s, e) => Changed();
		return box;
	}

	ComboBox Combo(IEnumerable<Choice> items, int selected, double width = 300) {
		var box = new ComboBox { Width = width };
		foreach (var item in items) box.Items.Add(item);
		box.SelectedIndex = Math.Max(0, Math.Min(selected, box.Items.Count - 1));
		box.SelectionChanged += (s, e) => Changed();
		return box;
	}

	Button FlatButton(string key, RoutedEventHandler click) {
		var button = new Button { Content = T(key), Style = Res<Style>("FlatButton"), HorizontalAlignment = HorizontalAlignment.Left };
		button.Click += click;
		return button;
	}

	// A switch with what it turns on next to it, for a row without a name on the left.
	FrameworkElement Labelled(CheckBox box, string key) {
		return Horizontal(box, new TextBlock { Text = T(key), Margin = new Thickness(12, 4, 0, 0), TextWrapping = TextWrapping.Wrap, MaxWidth = 520 });
	}

	// A text box with a button at its right end.
	static FrameworkElement WithButton(TextBox box, Button button) {
		var panel = new DockPanel();
		button.Margin = new Thickness(8, 0, 0, 0);
		DockPanel.SetDock(button, Dock.Right);
		panel.Children.Add(button);
		panel.Children.Add(box);
		return panel;
	}

	static object Value(ComboBox box) {
		var choice = box.SelectedItem as Choice;
		return choice == null ? null : choice.Value;
	}

	static StackPanel Horizontal(params UIElement[] children) {
		var panel = new StackPanel { Orientation = Orientation.Horizontal };
		foreach (var child in children) panel.Children.Add(child);
		return panel;
	}

	static StackPanel Stack(params UIElement[] children) {
		var panel = new StackPanel();
		foreach (var child in children) panel.Children.Add(child);
		return panel;
	}

	// A path as typed or pasted: without the spaces and double quotes around it.
	static string CleanPath(string text) {
		return (text ?? "").Trim().Trim('"');
	}

	// ------------------------------------------------------------------ Play

	FrameworkElement BuildPlayPage() {
		// The game's own art from its dump (sce_sys\pic0.png), under a fade into the window; a plain gradient without one.
		var bannerGrid = new Grid { Height = 270, ClipToBounds = true };
		bannerFallback = new Border { Background = new LinearGradientBrush(Color.FromRgb(0x2A, 0x22, 0x18), Color.FromRgb(0x10, 0x10, 0x12), 35) };
		bannerGrid.Children.Add(bannerFallback);
		banner = new Image { Stretch = Stretch.UniformToFill, VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Center };
		RenderOptions.SetBitmapScalingMode(banner, BitmapScalingMode.HighQuality);
		bannerGrid.Children.Add(banner);
		var fade = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(0, 1) };
		fade.GradientStops.Add(new GradientStop(Color.FromArgb(0x20, 0x10, 0x10, 0x12), 0));
		fade.GradientStops.Add(new GradientStop(Color.FromArgb(0x60, 0x10, 0x10, 0x12), 0.55));
		fade.GradientStops.Add(new GradientStop(Color.FromArgb(0xFF, 0x10, 0x10, 0x12), 1));
		bannerGrid.Children.Add(new Border { Background = fade });
		var side = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
		side.GradientStops.Add(new GradientStop(Color.FromArgb(0xB0, 0x10, 0x10, 0x12), 0));
		side.GradientStops.Add(new GradientStop(Color.FromArgb(0x00, 0x10, 0x10, 0x12), 0.6));
		bannerGrid.Children.Add(new Border { Background = side });
		var titles = new StackPanel { VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(44, 0, 44, 22) };
		bannerTitle = new TextBlock { FontFamily = Res<FontFamily>("SerifFont"), FontSize = 34, Foreground = Brushes.White, TextWrapping = TextWrapping.Wrap };
		bannerLine = new TextBlock { FontSize = 13, Foreground = Res<Brush>("TextDimBrush"), Margin = new Thickness(2, 6, 0, 0) };
		titles.Children.Add(bannerTitle);
		titles.Children.Add(bannerLine);
		bannerGrid.Children.Add(titles);

		// Checks on the left; the game and the current settings on the right.
		var columns = new Grid { Margin = new Thickness(44, 22, 44, 32) };
		columns.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
		columns.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(24) });
		columns.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1.05, GridUnitType.Star) });

		checkList = new StackPanel();
		var checksCard = new Border { Style = Res<Style>("Card"), VerticalAlignment = VerticalAlignment.Top };
		checksCard.Child = Stack(new TextBlock { Text = T("play.checks"), Style = Res<Style>("SectionTitle"), Margin = new Thickness(0, 0, 0, 10) }, checkList);
		columns.Children.Add(checksCard);

		gamePath = new TextBox { Text = S.Str("game"), Margin = new Thickness(0, 0, 0, 8) };
		gamePath.TextChanged += (s, e) => {
			if (building) return;
			gameTimer.Stop();
			gameTimer.Start();
		};
		var browseZar = FlatButton("button.zar", (s, e) => BrowseZar());
		var browse = FlatButton("button.folder", (s, e) => BrowseGame());
		browse.Margin = new Thickness(8, 0, 0, 0);
		var gameCard = new Border { Style = Res<Style>("Card") };
		gameCard.Child = Stack(new TextBlock { Text = T("play.gamebox"), Style = Res<Style>("SectionTitle"), Margin = new Thickness(0, 0, 0, 10) },
			gamePath, Horizontal(browseZar, browse));
		currentSettings = new Grid();
		currentSettings.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
		currentSettings.ColumnDefinitions.Add(new ColumnDefinition());
		var settingsCard = new Border { Style = Res<Style>("Card"), Margin = new Thickness(0, 16, 0, 0) };
		settingsCard.Child = Stack(new TextBlock { Text = T("play.summary"), Style = Res<Style>("SectionTitle"), Margin = new Thickness(0, 0, 0, 8) },
			currentSettings);
		var right = Stack(gameCard, settingsCard);
		Grid.SetColumn(right, 2);
		columns.Children.Add(right);

		var stack = new StackPanel();
		stack.Children.Add(bannerGrid);
		stack.Children.Add(columns);
		return new ScrollViewer { Content = stack, VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
			HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, Focusable = false };
	}

	void BrowseGame() {
		var current = CleanPath(gamePath.Text);
		var chosen = FolderPicker.Pick(this, T("dialog.game"), Directory.Exists(current) ? current : null);
		if (chosen != null) gamePath.Text = chosen;
	}

	// The game folder packed into a ZArchive: read without extracting it.
	void BrowseZar() {
		var current = CleanPath(gamePath.Text);
		var dialog = new Microsoft.Win32.OpenFileDialog { Title = T("dialog.zar"), Filter = "ZArchive (*.zar)|*.zar|All files (*.*)|*.*" };
		try {
			if (File.Exists(current)) dialog.InitialDirectory = Path.GetDirectoryName(current);
		} catch (Exception) {
		}
		if (dialog.ShowDialog(this) == true) gamePath.Text = dialog.FileName;
	}

	// The game at `path`: its name, art and checks.
	void LoadGame(string path) {
		if (!building && CleanPath(path) != S.Str("game")) Changed();
		game = GameInfo.Read(path);
		var known = game.Found || game.Described;
		GameTitle.Text = known ? game.Title : "";
		bannerTitle.Text = known ? game.Title : T("play.choose");
		bannerLine.Text = game.Described ? game.TitleId + "  ·  " + game.Version
			: game.File ? F("game.fileLine", T(game.Archive ? "game.zarchive" : "game.plainfile"), game.Size / 1073741824.0)
			: game.Folder ? T("game.folder") : "";
		banner.Source = null;
		if (game.Picture != null && !ScreenshotNoArt) {
			try {
				var image = new BitmapImage();
				image.BeginInit();
				image.CacheOption = BitmapCacheOption.OnLoad; // (the file stays free)
				image.DecodePixelWidth = 1920;
				image.UriSource = new Uri(game.Picture);
				image.EndInit();
				image.Freeze();
				banner.Source = image;
			} catch (Exception) {
			}
		}
		UpdateChecks();
	}

	enum Mark { Good, Warn, Bad, Busy }

	void AddCheck(Mark mark, string text) {
		var symbol = mark == Mark.Good ? "✓" : mark == Mark.Warn ? "!" : mark == Mark.Bad ? "✕" : "…";
		var brush = mark == Mark.Good ? Res<Brush>("GoodBrush") : mark == Mark.Warn ? Res<Brush>("WarnBrush")
			: mark == Mark.Bad ? Res<Brush>("BadBrush") : Res<Brush>("TextDimBrush");
		var row = new DockPanel { Margin = new Thickness(0, 4, 0, 6) };
		var icon = new TextBlock { Text = symbol, Foreground = brush, FontWeight = FontWeights.Bold, Width = 22, FontSize = 14 };
		DockPanel.SetDock(icon, Dock.Left);
		row.Children.Add(icon);
		row.Children.Add(new TextBlock { Text = text, TextWrapping = TextWrapping.Wrap, Foreground = Res<Brush>("TextBrush"), LineHeight = 18 });
		checkList.Children.Add(row);
	}

	void UpdateChecks() {
		if (!built) return;
		checkList.Children.Clear();
		if (game.Described) AddCheck(game.Folder ? Mark.Good : Mark.Bad, game.Folder ? F("game.ok", game.Title, game.TitleId, game.Version) : F("game.noEboot", game.Title));
		else if (game.File) AddCheck(Mark.Good, F("game.file", T(game.Archive ? "game.zarchive" : "game.plainfile"), game.Size / 1073741824.0));
		else if (game.Folder) AddCheck(Mark.Good, T("game.folder"));
		else AddCheck(Mark.Bad, T(game.Path.Length == 0 ? "game.none" : "game.bad"));
		if (File.Exists(context.EmulatorExe)) AddCheck(Mark.Good, F("emulator.ok", context.EmulatorExe));
		else AddCheck(Mark.Bad, F("emulator.missing", context.EmulatorExe));
		// The card chosen from Vulkan's list, else the one with the most memory of those Windows lists.
		var chosen = S.Int("gpu");
		var name = chosen >= 0 && chosen < context.VulkanGpus.Length ? context.VulkanGpus[chosen] : null;
		var card = name == null ? context.Gpus.FirstOrDefault() : context.Gpus.FirstOrDefault(g => g.Name == name) ?? new GpuInfo { Name = name };
		if (card == null) AddCheck(Mark.Warn, T("gpu.none"));
		else if (card.Memory > 0) AddCheck(Mark.Good, F("gpu.ok", card.Name, Math.Round(card.Memory / 1073741824.0)));
		else AddCheck(Mark.Good, F("gpu.okNoMemory", card.Name));
		var ramGb = Math.Round(context.RamBytes / 1073741824.0);
		AddCheck(ramGb <= 20 ? Mark.Warn : Mark.Good, F(ramGb <= 20 ? "ram.low" : "ram.ok", ramGb));
		Mark shaderMark;
		var shaderText = ShaderStatus(out shaderMark);
		AddCheck(shaderMark, shaderText);
		if (context.FrameGenAvailable) AddCheck(Mark.Good, T("fg.ok"));
		else AddCheck(Mark.Busy, T(context.RtxGpu ? "fg.noStreamline" : "fg.noRtx"));
		foreach (var problem in commands.Problems()) AddCheck(Mark.Warn, problem);
		shaderLine.Text = shaderText;
	}

	// What there is to say about the precompile pass: running, closed early, or the last one's summary (its "Precompile
	// done:" line, then the time it took and its exit code: run-windows.ps1 -ResultFile) for the data folder chosen.
	string ShaderStatus(out Mark mark) {
		mark = Mark.Busy;
		if (precompileProcess != null) return precompileStatus ?? T("precompile.running");
		mark = Mark.Warn;
		try {
			var file = context.PrecompileResultFile();
			if (File.Exists(file)) {
				var lines = File.ReadAllLines(file).Where(line => line.Trim().Length != 0).ToArray();
				if (lines.Length != 0) {
					if (lines[0].StartsWith("Precompile done:", StringComparison.Ordinal) && lines.Any(line => line.Contains("exit code 0;"))) mark = Mark.Good;
					return F("precompile.last", string.Join("\n", lines));
				}
			}
		} catch (Exception) {
			// (Being written, or text that is no path: as if there were none.)
		}
		return T(precompileClosed ? "precompile.closed" : "precompile.none");
	}

	// ------------------------------------------------------------------ Display

	FrameworkElement BuildDisplayPage() {
		// The window: its size, picked or typed; the game's picture keeps its aspect ratio in it (black bars) unless
		// stretched.
		var size = Options.Resolution(S.Str("resolution"));
		if (size.Length == 0) size = "1920x1080";
		resolution = new ComboBox { Width = 200 };
		foreach (var item in Options.Resolutions) resolution.Items.Add(new Choice(item.Replace("x", " × "), item));
		resolution.Items.Add(new Choice(T("resolution.other"), null));
		// (As high as the list next to it.)
		resolutionText = new TextBox { Text = size, Width = 130, Height = 32, Padding = new Thickness(8, 0, 8, 0), Margin = new Thickness(10, 0, 0, 0),
			VerticalAlignment = VerticalAlignment.Center };
		resolutionNote = Desc(T("resolution.desc"));
		SelectSize(size);
		resolution.SelectionChanged += (s, e) => {
			if (building || syncingSize) return;
			var picked = Value(resolution) as string;
			if (picked == null) {
				resolutionText.Focus();
				resolutionText.SelectAll();
				return;
			}
			syncingSize = true;
			resolutionText.Text = picked;
			syncingSize = false;
			WindowSizeChanged();
		};
		resolutionText.TextChanged += (s, e) => {
			if (building || syncingSize) return;
			var typed = Options.Resolution(resolutionText.Text);
			if (typed.Length != 0) SelectSize(typed);
			WindowSizeChanged();
		};
		fullscreen = Switch(S.Bool("fullscreen"));
		stretch = Switch(S.Bool("stretch"));
		hdr = Switch(S.Bool("hdr"));
		var modes = new[] { new Choice(T("present.mailbox"), "Mailbox"), new Choice(T("present.fifo"), "Fifo"), new Choice(T("present.immediate"), "Immediate") };
		present = Combo(modes, Math.Max(0, Array.IndexOf(Options.PresentModes, S.Str("present_mode"))));
		return Page("display.title",
			Section("display.window"),
			Row("resolution", Horizontal(resolution, resolutionText), resolutionNote),
			Row("fullscreen", fullscreen, Desc(T("fullscreen.desc"))),
			Row("stretch", stretch, Desc(T("stretch.desc"))),
			Row("hdr", hdr, Desc(T("hdr.desc"))),
			Section("display.frames"),
			Row("present", present, Desc(T("present.desc"))));
	}

	// The list shows `size` when it is one of its sizes, else its last item (a typed size).
	void SelectSize(string size) {
		var was = syncingSize;
		syncingSize = true;
		var index = Array.IndexOf(Options.Resolutions, size);
		resolution.SelectedIndex = index >= 0 ? index : resolution.Items.Count - 1;
		syncingSize = was;
	}

	// (Text that is no size leaves the last one, and says so.)
	void WindowSizeChanged() {
		Changed();
		resolutionNote.Text = Options.Resolution(resolutionText.Text).Length != 0 ? T("resolution.desc") : F("resolution.bad", S.Str("resolution"));
	}

	// ------------------------------------------------------------------ Graphics

	FrameworkElement BuildGraphicsPage() {
		var scales = Options.RenderScales.Select(scale => new Choice(scale + "%", scale));
		renderScale = Combo(scales, Math.Max(0, Array.IndexOf(Options.RenderScales, S.Int("render_scale"))), 160);
		renderNote = Desc("");
		occlusion = Switch(S.Bool("occlusion_queries"));

		// The upscaler: the game's own, or DLSS in one of its modes; the game's own without an NVIDIA RTX card or DLSS's
		// model in NVIDIA's runtime (the saved choice is kept for when they are there).
		var modes = Options.DlssModes.Select(mode => new Choice(mode == "off" ? T("upscaling.off") : DlssName(mode), mode));
		upscaling = Combo(modes, context.DlssAvailable ? Array.IndexOf(Options.DlssModes, S.Str("dlss")) : 0);
		upscaling.IsEnabled = context.DlssAvailable;
		upscaling.SelectionChanged += (s, e) => UpdateRenderScale();
		var upscalingNote = Desc(T(context.DlssAvailable ? "upscaling.ok" : !context.RtxGpu ? "upscaling.noRtx"
			: context.Streamline == null ? "upscaling.noStreamline" : "upscaling.noModel"));
		UpdateRenderScale();

		// Frame generation: the multiplier, off without an NVIDIA RTX card or NVIDIA's runtime (the saved choice is kept
		// for when they are there).
		var gens = new List<Choice> { new Choice(T("framegen.off"), 1) };
		for (int x = 2; x <= 6; x++) gens.Add(new Choice(x + "x", x));
		frameGen = Combo(gens, context.FrameGenAvailable ? S.Int("framegen") - 1 : 0, 160);
		frameGen.IsEnabled = context.FrameGenAvailable;
		frameGenNote = Desc(T(context.FrameGenAvailable ? "framegen.ok" : !context.RtxGpu ? "framegen.noRtx" : "framegen.noStreamline"));
		var chooseStreamline = FlatButton("button.streamline", (s, e) => ChooseStreamline());
		streamlineText = new TextBlock { Foreground = Res<Brush>("TextDimBrush"), VerticalAlignment = VerticalAlignment.Center,
			Margin = new Thickness(12, 0, 0, 0), TextTrimming = TextTrimming.CharacterEllipsis, MaxWidth = 380 };
		streamlineText.Text = context.Streamline ?? (S.Str("streamline").Length != 0 ? S.Str("streamline") : T("streamline.none"));

		// The graphics card: left to the emulator, or one of Vulkan's list by its number. (A number chosen where the list
		// cannot be read stays as "GPU <n>".)
		var cards = new List<Choice> { new Choice(T("gpu.auto"), -1) };
		for (int i = 0; i < context.VulkanGpus.Length; i++) cards.Add(new Choice(i + ": " + context.VulkanGpus[i], i));
		var chosen = S.Int("gpu");
		if (context.VulkanGpus.Length == 0 && chosen >= 0) cards.Add(new Choice(F("gpu.number", chosen), chosen));
		gpu = Combo(cards, chosen < 0 ? 0 : context.VulkanGpus.Length != 0 ? chosen + 1 : 1, 360);
		return Page("graphics.title",
			Section("graphics.resolution"),
			Row("upscaling", upscaling, upscalingNote),
			Row("render", renderScale, renderNote),
			Section("graphics.occlusion"),
			Row("occlusion", occlusion, Desc(T("occlusion.desc"))),
			Section("graphics.fg"),
			Row("framegen", frameGen, frameGenNote),
			Row("streamline", Horizontal(chooseStreamline, streamlineText), Desc(T("streamline.desc"))),
			Section("graphics.gpu"),
			Row("gpu", gpu, Desc(T("gpu.desc"))));
	}

	// A DLSS mode as the list names it: its name and the size of its 3D.
	static string DlssName(string mode) {
		return F("upscaling.choice", T("dlss." + mode), Options.DlssSizes[Math.Max(0, Array.IndexOf(Options.DlssModes, mode))]);
	}

	// With a DLSS mode the 3D has that mode's one size: the percent of the game's own does not apply (it stays as chosen
	// for the game's own upscaler).
	void UpdateRenderScale() {
		var dlss = context.DlssAvailable && upscaling.SelectedIndex > 0;
		renderScale.IsEnabled = !dlss;
		renderNote.Text = T(dlss ? "render.dlss" : "render.desc");
	}

	void ChooseStreamline() {
		var dir = FolderPicker.Pick(this, T("dialog.streamline"), S.Str("streamline"));
		if (dir == null) return;
		// (Read first: the multiplier and the upscaler saved for when the runtime is there stay.)
		ReadForm();
		if (!context.UseStreamlineFolder(dir)) {
			MessageBox.Show(this, T("dialog.streamlineBad"), T("graphics.fg"), MessageBoxButton.OK, MessageBoxImage.Warning);
			return;
		}
		// (Without an NVIDIA RTX graphics card the folder is kept and the row stays off.)
		S.Save();
		Build();
	}

	// ------------------------------------------------------------------ Game

	FrameworkElement BuildGamePage() {
		var languages = Strings.ConsoleLanguages.Select((name, i) => new Choice(name, i));
		language = Combo(languages, S.Int("language"), 340);
		skipNotice = Switch(S.Bool("skip_notice_screen"));
		sound = Switch(S.Bool("sound"));
		dataPath = new TextBox { Text = S.Str("data") };
		dataPath.TextChanged += (s, e) => Changed();
		var browse = FlatButton("button.browse", (s, e) => BrowseData());
		return Page("game.title",
			Section("game.console"),
			Row("language", language, Desc(T("language.desc"))),
			Row("skipnotice", skipNotice, Desc(T("skipnotice.desc"))),
			Section("game.sound"),
			Row("sound", sound, Desc(T("sound.desc"))),
			Section("game.data"),
			Row("data", WithButton(dataPath, browse), Desc(F("data.desc", context.Root))));
	}

	void BrowseData() {
		var current = CleanPath(dataPath.Text);
		var chosen = FolderPicker.Pick(this, T("dialog.data"), Directory.Exists(current) ? current : context.Root);
		if (chosen != null) dataPath.Text = chosen;
	}

	// ------------------------------------------------------------------ Shaders

	FrameworkElement BuildShadersPage() {
		shaderLine = new TextBlock { Foreground = Res<Brush>("TextBrush"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 4, 0, 0), LineHeight = 18 };
		precompilePageButton = FlatButton("button.precompile", (s, e) => Precompile());
		ecores = Switch(S.Bool("ecores") && context.EfficiencyMask != 0, context.EfficiencyMask != 0);
		background = Switch(S.Bool("precompile_pipelines"));
		lists = Switch(S.Bool("pipeline_lists"));
		pairs = Switch(S.Bool("shader_pairs"));
		noWait = Switch(S.Bool("skip_pending_draws"));
		return Page("shaders.title",
			shaderLine,
			Section("shaders.precompile"),
			Row(null, precompilePageButton, Desc(T("precompile.desc"))),
			Row(null, Labelled(ecores, "ecores"), context.EfficiencyMask != 0 ? null : Desc(T("ecores.none"))),
			Section("shaders.playing"),
			Row("background", background, Desc(T("background.desc"))),
			Row("lists", lists, Desc(T("lists.desc"))),
			Row("pairs", pairs, Desc(T("pairs.desc"))),
			Row("nowait", noWait, Desc(T("nowait.desc"))));
	}

	// ------------------------------------------------------------------ Advanced

	FrameworkElement BuildAdvancedPage() {
		overlay = Switch(S.Bool("perf_overlay"));
		redzone = Switch(S.Bool("redzone"));
		var logs = new[] { new Choice(T("log.off"), "off"), new Choice(T("log.file"), "file"), new Choice(T("log.console"), "console") };
		log = Combo(logs, Math.Max(0, Array.IndexOf(Options.LogModes, S.Str("log"))));
		// More emulator options and environment switches, saved as typed; what of them is left out of the command shows
		// under the boxes.
		extraArgs = new TextBox { Text = S.Str("extra_args"), MinWidth = 420 };
		extraArgs.TextChanged += (s, e) => Changed();
		environment = new TextBox { Text = S.Str("environment"), MinWidth = 420 };
		environment.TextChanged += (s, e) => Changed();
		problemText = new TextBlock { Foreground = Res<Brush>("BadBrush"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 8, 0, 0),
			Visibility = Visibility.Collapsed };
		var saved = S.Str("ui_language");
		uiLanguage = Combo(new[] { new Choice(T("uilang.auto"), ""), new Choice("English", "en"), new Choice("中文（简体）", "zh") },
			saved == "en" ? 1 : saved == "zh" ? 2 : 0, 220);
		uiLanguage.SelectionChanged += (s, e) => {
			if (building) return;
			var value = Value(uiLanguage) as string ?? "";
			S.Set("ui_language", value);
			Strings.Language = value == "zh" ? UiLanguage.Chinese : value == "en" ? UiLanguage.English : Strings.SystemLanguage();
			ReadForm();
			Build();
		};
		reopenLauncher = Switch(S.Bool("reopen_launcher"));
		var logsFolder = FlatButton("button.logs", (s, e) => OpenLogs());
		var readme = FlatButton("button.readme", (s, e) => OpenReadme());
		var root = FlatButton("button.root", (s, e) => OpenFolder(context.Root));
		readme.Margin = root.Margin = new Thickness(10, 0, 0, 0);
		return Page("advanced.title",
			Section("advanced.stability"),
			Row("overlay", overlay, Desc(T("overlay.desc"))),
			Row("redzone", redzone, Desc(T("redzone.desc"))),
			Row("log", log, Desc(T("log.desc"))),
			Section("advanced.extra"),
			Row("extra", extraArgs, Desc(T("extra.desc"))),
			Row("environment", environment, Stack(Desc(T("environment.desc")), problemText)),
			Section("advanced.launcher"),
			Row("uilang", uiLanguage),
			Row("reopen", reopenLauncher, Desc(T("reopen.desc"))),
			Row(null, Horizontal(logsFolder, readme, root)),
			Desc(T("credit")));
	}

	// ------------------------------------------------------------------ state, summary, actions

	void Changed() {
		if (building) return;
		ReadForm();
		UpdateSummary();
		UpdateChecks();
	}

	// The controls into the settings, saved (launcher.ps1's Read-Form).
	void ReadForm() {
		if (!built) return;
		S.Set("game", CleanPath(gamePath.Text));
		S.Set("data", CleanPath(dataPath.Text));
		// (Text that is no size leaves the last one.)
		var size = Options.Resolution(resolutionText.Text);
		if (size.Length != 0) S.Set("resolution", size);
		S.Set("fullscreen", fullscreen.IsChecked == true);
		S.Set("stretch", stretch.IsChecked == true);
		S.Set("language", Math.Max(0, language.SelectedIndex));
		// (The card's name too: its number is found again by it when Vulkan's list changes.)
		if (context.VulkanGpus.Length != 0) {
			var card = Math.Max(0, gpu.SelectedIndex) - 1;
			S.Set("gpu", card);
			S.Set("gpu_name", card >= 0 ? context.VulkanGpus[card] : "");
		} else if (gpu.SelectedIndex == 0) {
			S.Set("gpu", -1);
			S.Set("gpu_name", "");
		}
		S.Set("present_mode", Options.PresentModes[Math.Max(0, present.SelectedIndex)]);
		S.Set("hdr", hdr.IsChecked == true);
		S.Set("perf_overlay", overlay.IsChecked == true);
		S.Set("sound", sound.IsChecked == true);
		S.Set("skip_notice_screen", skipNotice.IsChecked == true);
		S.Set("redzone", redzone.IsChecked == true);
		S.Set("precompile_pipelines", background.IsChecked == true);
		S.Set("pipeline_lists", lists.IsChecked == true);
		S.Set("shader_pairs", pairs.IsChecked == true);
		S.Set("skip_pending_draws", noWait.IsChecked == true);
		// (An unavailable row keeps the saved choice for when the runtime is there.)
		if (context.FrameGenAvailable) S.Set("framegen", Math.Max(0, frameGen.SelectedIndex) + 1);
		if (context.DlssAvailable) S.Set("dlss", Options.DlssModes[Math.Max(0, upscaling.SelectedIndex)]);
		S.Set("render_scale", Options.RenderScales[Math.Max(0, renderScale.SelectedIndex)]);
		S.Set("occlusion_queries", occlusion.IsChecked == true);
		S.Set("log", Options.LogModes[Math.Max(0, log.SelectedIndex)]);
		S.Set("extra_args", extraArgs.Text.Trim());
		S.Set("environment", environment.Text.Trim());
		// (Kept as it was on a processor of one kind of core.)
		if (context.EfficiencyMask != 0) S.Set("ecores", ecores.IsChecked == true);
		S.Set("reopen_launcher", reopenLauncher.IsChecked == true);
		S.Save();
	}

	void UpdateSummary() {
		if (!built) return;
		var problems = commands.Problems();
		problemText.Text = string.Join("\n", problems);
		problemText.Visibility = problems.Count != 0 ? Visibility.Visible : Visibility.Collapsed;

		var window = F(S.Bool("fullscreen") ? "summary.fullscreen" : "summary.window", S.Str("resolution").Replace("x", "×"));
		var frameGeneration = commands.FrameGen >= 2 ? "DLSS " + commands.FrameGen + "x" : T("framegen.off");
		// (With a DLSS mode, the mode stands where the 3D resolution's percent does: that one does not apply.)
		var dlss = commands.Dlss != "off";
		var shaders = new List<string>();
		if (S.Bool("precompile_pipelines")) shaders.Add(T("summary.background"));
		if (S.Bool("pipeline_lists")) shaders.Add(T("summary.lists"));
		if (S.Bool("shader_pairs")) shaders.Add(T("summary.pairs"));
		if (S.Bool("skip_pending_draws")) shaders.Add(T("summary.noWait"));
		if (S.Bool("ecores") && context.EfficiencyMask != 0) shaders.Add(T("summary.ecores"));
		var options = new List<string>();
		if (S.Bool("hdr")) options.Add(T("summary.hdr"));
		if (S.Bool("stretch")) options.Add(T("summary.stretch"));
		if (S.Bool("perf_overlay")) options.Add(T("summary.overlay"));
		if (!S.Bool("sound")) options.Add(T("summary.noSound"));
		if (!S.Bool("occlusion_queries")) options.Add(T("summary.noOcclusion"));
		if (S.Bool("skip_notice_screen")) options.Add(T("summary.skipNotice"));
		if (S.Bool("redzone")) options.Add(T("summary.redzone"));
		if (S.Str("log") != "off") options.Add(F("summary.log", S.Str("log")));
		if (S.Bool("data")) options.Add(T("summary.data"));
		if (commands.ExtraArguments().Length != 0) options.Add(T("summary.extra"));
		if (commands.EnvironmentPairs().Length != 0) options.Add(T("summary.environment"));

		// (While a precompile runs, the bar says so: neither button starts another emulator on the same caches.)
		if (precompileProcess != null) Summary.Text = precompileStatus ?? T("precompile.running");
		else {
			var parts = new List<string> { window, dlss ? T("dlss." + commands.Dlss) : F("summary.render", S.Int("render_scale")), S.Str("present_mode") };
			if (commands.FrameGen >= 2) parts.Add(frameGeneration);
			parts.AddRange(options);
			Summary.Text = string.Join("   ·   ", parts);
		}

		currentSettings.Children.Clear();
		currentSettings.RowDefinitions.Clear();
		var lines = new[] {
			new[] { T("nav.display"), window + "  ·  " + T("present." + S.Str("present_mode").ToLowerInvariant()) },
			new[] { T("summary.3d"), dlss ? DlssName(commands.Dlss) : F("summary.percent", S.Int("render_scale")) },
			new[] { T("graphics.fg"), frameGeneration },
			new[] { T("summary.shaders"), shaders.Count == 0 ? "—" : string.Join("  ·  ", shaders) },
			new[] { T("summary.options"), options.Count == 0 ? "—" : string.Join("  ·  ", options) },
		};
		for (int i = 0; i < lines.Length; i++) {
			currentSettings.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
			var label = new TextBlock { Text = lines[i][0], Foreground = Res<Brush>("TextDimBrush"), Margin = new Thickness(0, 4, 18, 4) };
			var value = new TextBlock { Text = lines[i][1], Foreground = Res<Brush>("TextBrush"), Margin = new Thickness(0, 4, 0, 4), TextWrapping = TextWrapping.Wrap };
			Grid.SetRow(label, i);
			Grid.SetRow(value, i);
			Grid.SetColumn(value, 1);
			currentSettings.Children.Add(label);
			currentSettings.Children.Add(value);
		}
	}

	// The buttons that start an emulator: off while a precompile pass runs.
	void UpdateBusy() {
		var idle = precompileProcess == null;
		PlayButton.IsEnabled = idle;
		PrecompileButton.IsEnabled = idle;
		if (precompilePageButton != null) precompilePageButton.IsEnabled = idle;
		UpdateSummary();
		UpdateChecks();
	}

	void Play() {
		if (precompileProcess != null) return;
		ReadForm();
		Process process;
		try {
			process = commands.StartPlay();
		} catch (Exception e) {
			MessageBox.Show(this, F("error.start", e.Message), T("error.title"), MessageBoxButton.OK, MessageBoxImage.Error);
			return;
		}
		if (process == null || !S.Bool("reopen_launcher")) {
			Close();
			return;
		}
		// (Advanced page, "Back to the launcher after the game": hidden while the game's console is open.)
		Hide();
		process.EnableRaisingEvents = true;
		process.Exited += (s, e) => Dispatcher.BeginInvoke(new Action(() => {
			Show();
			Activate();
		}));
		if (process.HasExited) {
			Show();
			Activate();
		}
	}

	// The precompile pass in a console of its own, which shows its progress and stays until a key is pressed; when the
	// pass ends, its summary line and the time it took (run-windows.ps1 -ResultFile) show in the window. Until then
	// neither button starts another emulator on the same caches.
	void Precompile() {
		if (precompileProcess != null) return;
		ReadForm();
		precompileResult = context.PrecompileResultFile();
		try {
			File.Delete(precompileResult);
		} catch (Exception) {
		}
		precompileBegin = DateTime.Now;
		precompileClosed = false;
		try {
			precompileProcess = commands.Start(commands.PrecompileArguments(precompileResult));
		} catch (Exception e) {
			MessageBox.Show(this, F("error.start", e.Message), T("error.title"), MessageBoxButton.OK, MessageBoxImage.Error);
			return;
		}
		if (precompileProcess == null) return;
		precompileStatus = T("precompile.running");
		UpdateBusy();
		precompileTimer.Start();
	}

	void PrecompileTick() {
		if (precompileProcess == null) {
			precompileTimer.Stop();
			return;
		}
		var written = false;
		try {
			if (File.Exists(precompileResult)) {
				// (Read again at the next tick when it is still being written.)
				if (File.ReadAllLines(precompileResult).Length == 0) return;
				written = true;
			}
		} catch (Exception) {
			return;
		}
		if (!written && !precompileProcess.HasExited) {
			precompileStatus = F("precompile.runningTime", (DateTime.Now - precompileBegin).ToString(@"h\:mm\:ss"));
			UpdateBusy();
			return;
		}
		precompileTimer.Stop();
		precompileProcess = null;
		precompileStatus = null;
		precompileClosed = !written;
		UpdateBusy();
	}

	void OpenLogs() {
		ReadForm();
		var folder = context.LogsFolder();
		try {
			Directory.CreateDirectory(folder);
		} catch (Exception) {
		}
		OpenFolder(folder);
	}

	static void OpenFolder(string folder) {
		try {
			Process.Start(new ProcessStartInfo("explorer.exe", "\"" + folder + "\"") { UseShellExecute = true });
		} catch (Exception) {
		}
	}

	void OpenReadme() {
		var file = context.Readme();
		if (file == null) return;
		try {
			Process.Start(new ProcessStartInfo("notepad.exe", "\"" + file + "\"") { UseShellExecute = true });
		} catch (Exception) {
		}
	}

	// ------------------------------------------------------------------ test pictures

	// Each page drawn into <folder>\<n>-<page>.png without showing the window (--screenshot); `size` (WIDTHxHEIGHT) in
	// place of the window's own, for a page longer than the window.
	public void SaveScreenshots(string folder, string size = null) {
		var other = Options.Resolution(size).Split('x');
		if (other.Length == 2) {
			Width = int.Parse(other[0]);
			Height = int.Parse(other[1]);
		}
		Directory.CreateDirectory(folder);
		if (ScreenshotPath != null) {
			building = true;
			gamePath.Text = ScreenshotPath;
			building = false;
		}
		var root = (FrameworkElement)Content;
		var width = (int)Width;
		var height = (int)Height;
		var names = new[] { "play", "display", "graphics", "game", "shaders", "advanced" };
		for (int i = 0; i < pages.Count; i++) {
			Nav.SelectedIndex = i;
			ShowPage(i);
			root.Measure(new Size(width, height));
			root.Arrange(new Rect(0, 0, width, height));
			root.UpdateLayout();
			var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32);
			var backdrop = new DrawingVisual();
			using (var dc = backdrop.RenderOpen()) dc.DrawRectangle(Background, null, new Rect(0, 0, width, height));
			bitmap.Render(backdrop);
			bitmap.Render(root);
			var encoder = new PngBitmapEncoder();
			encoder.Frames.Add(BitmapFrame.Create(bitmap));
			using (var stream = File.Create(Path.Combine(folder, (i + 1) + "-" + names[Math.Min(i, names.Length - 1)] + ".png"))) encoder.Save(stream);
		}
	}
}

}
