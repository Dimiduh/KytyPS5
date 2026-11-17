using System.Collections.Generic;
using System.Globalization;

namespace KytyLauncher {

enum UiLanguage { English, Chinese }

// The window's text in English and Simplified Chinese (the launcher's own language, not the console language the game
// is told). A key missing in Chinese shows the English text.
static class Strings {
	public static UiLanguage Language = UiLanguage.English;

	public static UiLanguage SystemLanguage() {
		return CultureInfo.CurrentUICulture.Name.StartsWith("zh", System.StringComparison.OrdinalIgnoreCase) ? UiLanguage.Chinese : UiLanguage.English;
	}

	public static string T(string key) {
		string text;
		if (Language == UiLanguage.Chinese && Chinese.TryGetValue(key, out text)) return text;
		return English.TryGetValue(key, out text) ? text : key;
	}

	public static string F(string key, params object[] values) {
		return string.Format(CultureInfo.CurrentCulture, T(key), values);
	}

	// The PS5's console languages in its order (run-windows.ps1 -Language takes the index), as the KytyPS5 launcher names them.
	public static readonly string[] ConsoleLanguagesEnglish = {
		"Japanese", "English (United States)", "French (France)", "Spanish (Spain)", "German", "Italian", "Dutch",
		"Portuguese (Portugal)", "Russian", "Korean", "Chinese (Traditional)", "Chinese (Simplified)", "Finnish", "Swedish",
		"Danish", "Norwegian", "Polish", "Portuguese (Brazil)", "English (United Kingdom)", "Turkish", "Spanish (Latin America)",
		"Arabic", "French (Canada)", "Czech", "Hungarian", "Greek", "Romanian", "Thai", "Vietnamese", "Indonesian" };
	static readonly string[] ConsoleLanguagesChinese = {
		"日语", "英语（美国）", "法语（法国）", "西班牙语（西班牙）", "德语", "意大利语", "荷兰语",
		"葡萄牙语（葡萄牙）", "俄语", "韩语", "中文（繁体）", "中文（简体）", "芬兰语", "瑞典语",
		"丹麦语", "挪威语", "波兰语", "葡萄牙语（巴西）", "英语（英国）", "土耳其语", "西班牙语（拉丁美洲）",
		"阿拉伯语", "法语（加拿大）", "捷克语", "匈牙利语", "希腊语", "罗马尼亚语", "泰语", "越南语", "印尼语" };
	public static string[] ConsoleLanguages { get { return Language == UiLanguage.Chinese ? ConsoleLanguagesChinese : ConsoleLanguagesEnglish; } }

	static readonly Dictionary<string, string> English = new Dictionary<string, string> {
		{"app.subtitle", "PS5 emulator for Windows"},
		{"nav.play", "Play"}, {"nav.display", "Display"}, {"nav.graphics", "Graphics"}, {"nav.game", "Game"},
		{"nav.shaders", "Shaders"}, {"nav.advanced", "Advanced"},
		{"nav.hint", "In the game: F2 switches the performance panel, F11 fullscreen."},

		{"play.choose", "Choose your game"},
		{"play.checks", "Checks"}, {"play.gamebox", "Game"}, {"play.summary", "Current settings"},
		{"button.folder", "Folder…"}, {"button.zar", ".zar…"}, {"button.browse", "Browse…"},
		{"button.play", "PLAY"}, {"button.precompile", "Precompile shaders"},
		{"button.logs", "Logs folder"}, {"button.readme", "README"}, {"button.root", "Emulator folder"},
		{"button.streamline", "Streamline SDK folder…"},
		{"game.none", "No game chosen: choose its .zar archive, or the folder with eboot.bin and sce_sys."},
		{"game.bad", "Not found. Choose the game: its .zar archive, or the folder with eboot.bin and sce_sys."},
		{"game.noEboot", "{0}: no eboot.bin in that folder."},
		{"game.ok", "{0} · {1} · version {2}"},
		{"game.zarchive", "ZArchive"}, {"game.plainfile", "File"},
		{"game.file", "{0}, {1:N1} GB: the emulator reads it without extracting it"},
		{"game.fileLine", "{0}  ·  {1:N1} GB"},
		{"game.folder", "Game folder (eboot.bin)"},
		{"emulator.ok", "Emulator: {0}"},
		{"emulator.missing", "kyty_emulator.exe is missing ({0}): build it with build-windows.cmd."},
		{"gpu.ok", "{0} · {1} GB video memory"}, {"gpu.okNoMemory", "{0}"}, {"gpu.none", "Graphics card not identified"},
		{"ram.ok", "{0} GB of RAM"},
		{"ram.low", "{0} GB of RAM: close other programs while playing"},
		{"fg.ok", "Frame generation ready (NVIDIA's Streamline found)"},
		{"fg.noRtx", "Frame generation needs an NVIDIA RTX 40 or newer graphics card"},
		{"fg.noStreamline", "Frame generation: choose NVIDIA's Streamline SDK folder (Graphics page)"},
		{"precompile.last", "Last precompile: {0}"},
		{"precompile.none", "No finished precompile for this data folder yet: 'Precompile shaders' builds the recorded shaders before playing."},
		{"precompile.running", "Precompiling the recorded shaders: its console window shows the progress."},
		{"precompile.runningTime", "Precompiling the recorded shaders ({0}): its console window shows the progress."},
		{"precompile.closed", "The precompile window was closed before the pass ended."},
		{"problem.extraCharacters", "Extra arguments left out: the characters & | < > ^ ! % cannot be passed."},
		{"problem.extraStart", "Extra arguments left out: emulator options start with -- (for example --hide-cursor)."},
		{"problem.extraQuote", "Extra arguments left out: a double quote is not closed."},
		{"problem.environment", "Environment switches left out (KEY=VALUE without & | < > ^ ! % \"): {0}"},

		{"summary.window", "{0} window"}, {"summary.fullscreen", "{0} fullscreen"}, {"summary.render", "3D {0}%"},
		{"summary.3d", "3D resolution"}, {"summary.percent", "{0}% of the game's own"},
		{"summary.hdr", "HDR"}, {"summary.stretch", "stretched"}, {"summary.overlay", "performance panel"},
		{"summary.noSound", "no sound"}, {"summary.skipNotice", "startup notices skipped"},
		{"summary.redzone", "extended red-zone protection"}, {"summary.log", "log: {0}"},
		{"summary.extra", "extra arguments"}, {"summary.environment", "environment switches"}, {"summary.data", "own data folder"},
		{"summary.options", "Options"}, {"summary.shaders", "Shaders"},
		{"summary.background", "prepared in the background"}, {"summary.lists", "shared list"}, {"summary.pairs", "shader pairs"}, {"summary.noWait", "no waiting for new ones"},
		{"summary.ecores", "precompile on the efficiency cores"},

		{"display.title", "Display"}, {"display.window", "Window"}, {"display.frames", "Frames"},
		{"resolution", "Window size"},
		{"resolution.desc", "The window as WIDTHxHEIGHT: pick a size or type one (320 to 16384 each). The game's picture keeps its aspect ratio in it, between black bars, unless it is stretched."},
		{"resolution.other", "Other (typed)"},
		{"resolution.bad", "Not a size: WIDTHxHEIGHT, 320 to 16384 each. {0} stays."},
		{"fullscreen", "Fullscreen"},
		{"fullscreen.desc", "Borderless fullscreen on the desktop. F11 or Alt+Enter in the game switches it."},
		{"stretch", "Stretch to the window (no black bars)"},
		{"stretch.desc", "Off: the game's picture keeps its aspect ratio, with black bars in a window or on a screen of another shape (21:9, 16:10). On: it fills the window."},
		{"hdr", "HDR"}, {"hdr.desc", "Turn on \"Use HDR\" in Windows display settings first; SDR otherwise."},
		{"present", "Present mode"},
		{"present.mailbox", "Mailbox (no tearing)"}, {"present.fifo", "Fifo (VSync)"}, {"present.immediate", "Immediate (may tear)"},
		{"present.desc", "Mailbox: no tearing, the newest finished frame is shown (the emulator's default). Fifo: every frame waits for the display's refresh (VSync). Immediate: frames are shown at once; the picture may tear. Where the graphics card lacks the mode chosen, the emulator takes Fifo."},

		{"graphics.title", "Graphics"}, {"graphics.resolution", "Resolution"}, {"graphics.fg", "Frame generation"},
		{"graphics.gpu", "Graphics card"},
		{"render", "3D resolution"},
		{"render.desc", "Unreal Engine games (Saros): lower is faster and softer. The size the game renders its 3D at, in percent of its own (Saros: 1600x900 to 2304x1296). The game's upscaler still makes the 4K picture: less work for the graphics card, a softer picture. At one spot of Saros (RTX 4090): 31 fps at 100%, 33 at 75%, 36 at 50%."},
		{"framegen", "Frame generation"}, {"framegen.off", "Off"},
		{"framegen.ok", "DLSS; NVIDIA RTX 40 or newer (3x and up: RTX 50). Frames made between the game's own: shown frames = the game's frames x the multiplier. It works in the game's 3D view, where the emulator finds the game's depth and motion vectors (Saros); menus, movies and loading screens are shown as they are. A card that makes fewer frames than asked for makes as many as it can (RTX 40: 2x)."},
		{"framegen.noRtx", "DLSS: needs an NVIDIA RTX 40 or newer graphics card."},
		{"framegen.noStreamline", "Needs NVIDIA's Streamline SDK: download it, then choose its folder."},
		{"streamline", "Streamline SDK"},
		{"streamline.desc", "The NVIDIA Streamline SDK (a release zip from https://github.com/NVIDIA-RTX/Streamline/releases), unpacked: its folder or its bin\\x64. Only NVIDIA-signed files are loaded; nothing of NVIDIA's comes with the emulator."},
		{"streamline.none", "not chosen"},
		{"gpu", "GPU"}, {"gpu.auto", "Automatic"}, {"gpu.number", "GPU {0}"},
		{"gpu.desc", "For the game and the precompile: shader caches are made for one card. Automatic leaves the choice to the emulator."},

		{"game.title", "Game"}, {"game.console", "Console"}, {"game.sound", "Sound"}, {"game.data", "Saves and caches"},
		{"language", "Console language"}, {"language.desc", "The language the game is told; it decides which text and voices it has."},
		{"skipnotice", "Skip startup logos and notices"},
		{"skipnotice.desc", "Where the game asks the console whether to."},
		{"sound", "Sound"},
		{"sound.desc", "On Windows' default playback device; the emulator has no choice of device. Off: the emulator starts with SDL's dummy audio driver (SDL_AUDIO_DRIVER=dummy), which plays nothing: the game's sound, the trophy sound, and a wired DualSense's haptics, which are sent as audio."},
		{"data", "Data folder"},
		{"data.desc", "Where the emulator keeps its data: the saves (_SaveData), the shader and pipeline caches (_PipelineCache), downloads, temporary data, trophies and the logs. Empty: the emulator's folder ({0}). Each folder has its own saves and shader caches: a precompile is for the folder it ran with."},

		{"shaders.title", "Shaders"}, {"shaders.precompile", "Precompile"}, {"shaders.playing", "While playing"},
		{"precompile.desc", "Builds every shader and pipeline that earlier sessions of the game recorded into the graphics driver's cache, with the current settings, then exits: the game finds them there instead of compiling them while playing. For an Unreal Engine 5 game it also reads the game's own shader files (a few seconds, the first time) and builds all of its compute shaders; that first pass takes long. Worth running after a new emulator build or a graphics driver update. Its summary and the time it took show here when it ends."},
		{"ecores", "Precompile on the efficiency cores only (slower, the PC stays responsive)"},
		{"ecores.none", "This processor has one kind of core."},
		{"background", "Prepare shaders in the background"},
		{"background.desc", "While the game runs. On: the shaders and pipelines earlier sessions recorded are built in the background, at low priority, from the start of the game, so that fewer are compiled the moment they are first drawn (a stutter). Off: they are compiled when first needed. 'Precompile shaders' builds them all before playing, either way."},
		{"lists", "Also prepare the shared pipeline list"},
		{"pairs", "Also build likely shader pairs"},
		{"pairs.desc", "Graphics shaders of the game's files that are likely drawn together, built into pipelines by 'Precompile shaders', so that a part of the game nobody has shown yet finds many of its pipelines ready (for Saros a third to a half of them). On: for Saros about 23,000 more pipelines, which takes about half of the first precompile. Off: the first precompile is about half as long, and those pipelines are built in the background the first time they are drawn. A precompile can be closed at any time: what it built is kept, and the next one goes on from there."},
		{"lists.desc", "The shaders of the shared pipeline list for the game, if one comes with the emulator. A pipeline list (pipeline-lists\\<title id>.pipelinelist) holds what other sessions of the game needed, without any shader code. On: its shaders and pipelines are built too, in the background and by 'Precompile shaders', as soon as the emulator has their code (from the game, as it creates its shaders). Off: only what this data folder recorded itself."},
		{"nowait", "Never wait for a new graphics shader"},
		{"nowait.desc", "Draw without it until it is built. A graphics shader combination the game uses for the first time, and that no earlier session or list prepared, has to be compiled. On: that happens in the background and what it draws appears a moment late (at most 2 s), so the game does not stall. Off: the game waits for every one, which shows as a hitch in new places. Compute shaders always have to be waited for; 'Precompile shaders' prepares those from the game's files."},

		{"advanced.title", "Advanced"}, {"advanced.stability", "Panel, stability and log"}, {"advanced.extra", "Extra options"},
		{"advanced.launcher", "Launcher"},
		{"overlay", "Performance panel"},
		{"overlay.desc", "Frame rate, frame times, CPU and GPU, from the start of the game. F2 in the game switches it, Shift+F2 its details."},
		{"redzone", "Extended red-zone protection"},
		{"redzone.desc", "Experimental; try it when the game crashes. Windows writes exception frames just below the game's stack pointer, over the 'red zone' its code may keep data in. The emulator always runs the functions that do from a copy that is safe. With this (--redzone) it also patches single instructions of the functions it could not copy."},
		{"log", "Log"}, {"log.off", "Off"}, {"log.file", "File (in the logs folder)"}, {"log.console", "Console (slow)"},
		{"log.desc", "The emulator's own log; writing it can slow the game."},
		{"extra", "Extra arguments"},
		{"extra.desc", "More options of kyty_emulator.exe, as on its command line (kyty_emulator.exe --help lists them all). They come after the launcher's own, so they win. For example: --vblank-frequency 120 --hide-cursor --no-splash, or --rebuild-pipeline-cache (with 'Precompile shaders': the driver cache made anew)."},
		{"environment", "Environment"},
		{"environment.desc", "Environment switches of the emulator, KEY=VALUE, separated by spaces or commas. For example: KYTY_RENDER_CPUS=auto KYTY_BDA_LAZY=0 KYTY_LIVE_FILE=C:\\bench\\live.txt. A live switch of the emulator is KYTY_ and its name in capitals."},
		{"uilang", "Launcher language"}, {"uilang.auto", "Automatic"},
		{"reopen", "Back to the launcher after the game"},
		{"reopen.desc", "The launcher waits in the background while the game runs and comes back when it ends, for another start with other settings. Off: it closes when the game starts."},
		{"credit", "Settings follow the KytyPS5 launcher (src/launcher) by the KytyPS5 developers."},

		{"dialog.game", "The game folder (eboot.bin, sce_sys)"},
		{"dialog.zar", "The game folder packed into a ZArchive"},
		{"dialog.data", "The folder for the saves, shader caches and logs"},
		{"dialog.streamline", "The unpacked NVIDIA Streamline SDK, or its bin\\x64 folder"},
		{"dialog.streamlineBad", "No sl.interposer.dll signed by NVIDIA in that folder or its bin\\x64.\nDownload the Streamline SDK from https://github.com/NVIDIA-RTX/Streamline/releases and choose the unpacked folder."},
		{"error.title", "KytyPS5 launcher"},
		{"error.start", "Could not start:\n{0}"},
	};

	static readonly Dictionary<string, string> Chinese = new Dictionary<string, string> {
		{"app.subtitle", "Windows 版 PS5 模拟器"},
		{"nav.play", "开始游戏"}, {"nav.display", "画面"}, {"nav.graphics", "画质"}, {"nav.game", "游戏"},
		{"nav.shaders", "着色器"}, {"nav.advanced", "高级"},
		{"nav.hint", "游戏中：F2 切换性能面板，F11 切换全屏。"},

		{"play.choose", "选择游戏"},
		{"play.checks", "检查"}, {"play.gamebox", "游戏"}, {"play.summary", "当前设置"},
		{"button.folder", "文件夹…"}, {"button.zar", ".zar…"}, {"button.browse", "浏览…"},
		{"button.play", "开始游戏"}, {"button.precompile", "预编译着色器"},
		{"button.logs", "日志文件夹"}, {"button.readme", "说明文档"}, {"button.root", "模拟器文件夹"},
		{"button.streamline", "Streamline SDK 文件夹…"},
		{"game.none", "未选择游戏：请选择游戏的 .zar 压缩包，或包含 eboot.bin 和 sce_sys 的文件夹。"},
		{"game.bad", "未找到。请选择游戏：其 .zar 压缩包，或包含 eboot.bin 和 sce_sys 的文件夹。"},
		{"game.noEboot", "{0}：该文件夹中没有 eboot.bin。"},
		{"game.ok", "{0} · {1} · 版本 {2}"},
		{"game.zarchive", "ZArchive"}, {"game.plainfile", "文件"},
		{"game.file", "{0}，{1:N1} GB：模拟器无需解压即可读取"},
		{"game.fileLine", "{0}  ·  {1:N1} GB"},
		{"game.folder", "游戏文件夹（eboot.bin）"},
		{"emulator.ok", "模拟器：{0}"},
		{"emulator.missing", "缺少 kyty_emulator.exe（{0}）：请用 build-windows.cmd 构建。"},
		{"gpu.ok", "{0} · 显存 {1} GB"}, {"gpu.okNoMemory", "{0}"}, {"gpu.none", "未识别到显卡"},
		{"ram.ok", "内存 {0} GB"},
		{"ram.low", "内存 {0} GB：游戏时请关闭其他程序"},
		{"fg.ok", "帧生成可用（已找到 NVIDIA Streamline）"},
		{"fg.noRtx", "帧生成需要 NVIDIA RTX 40 或更新的显卡"},
		{"fg.noStreamline", "帧生成：请在「画质」页选择 NVIDIA Streamline SDK 文件夹"},
		{"precompile.last", "上次预编译：{0}"},
		{"precompile.none", "此数据文件夹还没有完成过预编译：「预编译着色器」会在游戏前编译好已记录的着色器。"},
		{"precompile.running", "正在预编译已记录的着色器：进度显示在它的控制台窗口中。"},
		{"precompile.runningTime", "正在预编译已记录的着色器（{0}）：进度显示在它的控制台窗口中。"},
		{"precompile.closed", "预编译窗口在完成前被关闭。"},
		{"problem.extraCharacters", "附加参数未生效：不能包含字符 & | < > ^ ! %。"},
		{"problem.extraStart", "附加参数未生效：模拟器选项以 -- 开头（例如 --hide-cursor）。"},
		{"problem.extraQuote", "附加参数未生效：有未闭合的双引号。"},
		{"problem.environment", "以下环境开关未生效（应为 KEY=VALUE，且不含 & | < > ^ ! % \"）：{0}"},

		{"summary.window", "窗口 {0}"}, {"summary.fullscreen", "全屏 {0}"}, {"summary.render", "3D {0}%"},
		{"summary.3d", "3D 分辨率"}, {"summary.percent", "游戏原本的 {0}%"},
		{"summary.hdr", "HDR"}, {"summary.stretch", "拉伸"}, {"summary.overlay", "性能面板"},
		{"summary.noSound", "无声音"}, {"summary.skipNotice", "跳过启动提示"},
		{"summary.redzone", "扩展红区保护"}, {"summary.log", "日志：{0}"},
		{"summary.extra", "附加参数"}, {"summary.environment", "环境开关"}, {"summary.data", "单独的数据文件夹"},
		{"summary.options", "选项"}, {"summary.shaders", "着色器"},
		{"summary.background", "后台准备"}, {"summary.lists", "共享列表"}, {"summary.pairs", "着色器配对"}, {"summary.noWait", "不等待新着色器"},
		{"summary.ecores", "在能效核上预编译"},

		{"display.title", "画面"}, {"display.window", "窗口"}, {"display.frames", "帧"},
		{"resolution", "窗口大小"},
		{"resolution.desc", "窗口大小，格式为 宽x高：从列表中选择或直接输入（各为 320 到 16384）。游戏画面在窗口中保持比例（两侧为黑边），除非选择拉伸。"},
		{"resolution.other", "其他（手动输入）"},
		{"resolution.bad", "不是有效的大小：应为 宽x高，各为 320 到 16384。仍使用 {0}。"},
		{"fullscreen", "全屏"},
		{"fullscreen.desc", "桌面上的无边框全屏。游戏中按 F11 或 Alt+Enter 切换。"},
		{"stretch", "拉伸到窗口（无黑边）"},
		{"stretch.desc", "关闭：游戏画面保持比例，窗口或屏幕比例不同时（21:9、16:10）出现黑边。打开：画面填满窗口。"},
		{"hdr", "HDR"}, {"hdr.desc", "需先在 Windows 显示设置中打开「使用 HDR」，否则为 SDR。"},
		{"present", "呈现模式"},
		{"present.mailbox", "Mailbox（无撕裂）"}, {"present.fifo", "Fifo（垂直同步）"}, {"present.immediate", "Immediate（可能撕裂）"},
		{"present.desc", "Mailbox：无撕裂，显示最新完成的一帧（模拟器的默认值）。Fifo：每一帧都等待显示器刷新（垂直同步）。Immediate：帧立即显示，画面可能撕裂。显卡不支持所选模式时，模拟器使用 Fifo。"},

		{"graphics.title", "画质"}, {"graphics.resolution", "分辨率"}, {"graphics.fg", "帧生成"},
		{"graphics.gpu", "显卡"},
		{"render", "3D 分辨率"},
		{"render.desc", "适用于虚幻引擎游戏（Saros）：越低越快，画面越软。游戏渲染 3D 画面的大小，以其原本大小的百分比表示（Saros：1600x900 到 2304x1296）。4K 画面仍由游戏自带的超分生成：显卡负担更低，画面更软。Saros 中某一处（RTX 4090）：100% 时 31 帧，75% 时 33 帧，50% 时 36 帧。"},
		{"framegen", "帧生成"}, {"framegen.off", "关闭"},
		{"framegen.ok", "DLSS；需 NVIDIA RTX 40 或更新（3 倍及以上需 RTX 50）。在游戏自己的帧之间生成帧：显示帧数 = 游戏帧数 × 倍数。它在游戏的 3D 画面中生效，即模拟器能找到游戏深度与运动矢量的地方（Saros）；菜单、过场动画和加载画面原样显示。显卡生成不了所选的帧数时，按它能生成的最多帧数生成（RTX 40：2 倍）。"},
		{"framegen.noRtx", "DLSS：需要 NVIDIA RTX 40 或更新的显卡。"},
		{"framegen.noStreamline", "需要 NVIDIA Streamline SDK：下载后选择其文件夹。"},
		{"streamline", "Streamline SDK"},
		{"streamline.desc", "NVIDIA Streamline SDK（https://github.com/NVIDIA-RTX/Streamline/releases 的发布包），解压后选择其文件夹或 bin\\x64。只加载 NVIDIA 签名的文件；模拟器不附带任何 NVIDIA 文件。"},
		{"streamline.none", "未选择"},
		{"gpu", "显卡"}, {"gpu.auto", "自动"}, {"gpu.number", "显卡 {0}"},
		{"gpu.desc", "游戏和预编译都使用所选显卡：着色器缓存是针对一块显卡生成的。自动：由模拟器选择。"},

		{"game.title", "游戏"}, {"game.console", "主机"}, {"game.sound", "声音"}, {"game.data", "存档与缓存"},
		{"language", "主机语言"}, {"language.desc", "告诉游戏的系统语言；游戏支持哪些文字与语音由游戏决定。"},
		{"skipnotice", "跳过启动标志与提示"},
		{"skipnotice.desc", "仅在游戏向主机询问是否跳过时生效。"},
		{"sound", "声音"},
		{"sound.desc", "使用 Windows 的默认播放设备；模拟器不能选择设备。关闭：模拟器以 SDL 的空音频驱动启动（SDL_AUDIO_DRIVER=dummy），不播放任何声音：包括游戏声音、奖杯提示音，以及有线 DualSense 的触觉反馈（它以音频方式发送）。"},
		{"data", "数据文件夹"},
		{"data.desc", "模拟器保存数据的位置：存档（_SaveData）、着色器与管线缓存（_PipelineCache）、下载内容、临时数据、奖杯和日志。留空：模拟器所在的文件夹（{0}）。每个文件夹有各自的存档和着色器缓存：预编译只对运行它时所用的文件夹有效。"},

		{"shaders.title", "着色器"}, {"shaders.precompile", "预编译"}, {"shaders.playing", "游戏运行时"},
		{"precompile.desc", "按当前设置，把之前游玩时记录的所有着色器和管线编译进显卡驱动的缓存，然后退出：游戏运行时直接使用，不必边玩边编译。对于虚幻引擎 5 游戏，还会读取游戏自身的着色器文件（首次需要几秒），并编译其全部计算着色器；首次运行耗时较长。更新模拟器或显卡驱动后值得再运行一次。完成后这里会显示它的摘要和用时。"},
		{"ecores", "仅在能效核上预编译（较慢，但电脑仍保持流畅）"},
		{"ecores.none", "此处理器只有一种核心。"},
		{"background", "在后台准备着色器"},
		{"background.desc", "游戏运行时生效。打开：从游戏启动起，在后台以低优先级编译之前记录的着色器和管线，减少首次绘制时才编译造成的卡顿。关闭：首次用到时才编译。无论开关，「预编译着色器」都会在游戏前把它们全部编译好。"},
		{"lists", "同时准备共享管线列表"},
		{"pairs", "同时编译可能的着色器配对"},
		{"pairs.desc", "游戏文件中很可能一起绘制的图形着色器：「预编译着色器」会把它们提前编译成管线，这样游戏中还没有人到过的地方也有不少管线已经就绪（对《Saros》约为三分之一到一半）。打开：《Saros》会多编译约 23,000 条管线，约占首次预编译时间的一半。关闭：首次预编译的时间大约减半，这些管线会在第一次绘制时于后台编译。预编译可以随时关闭：已编译的部分会保留，下次从中断处继续。"},
		{"lists.desc", "即随模拟器提供的、本游戏的共享管线列表中的着色器（如果有）。管线列表（pipeline-lists\\<title id>.pipelinelist）记录了其他人游玩时用到的内容，不含任何着色器代码。打开：模拟器一旦得到相应代码（游戏创建着色器时），就会在后台以及「预编译着色器」中一并编译列表中的着色器和管线。关闭：只编译此数据文件夹自己记录的内容。"},
		{"nowait", "不等待新的图形着色器"},
		{"nowait.desc", "着色器编译好之前先不绘制相应内容。游戏第一次用到、且之前的游玩和列表都没有准备过的图形着色器组合需要编译。打开：在后台编译，相应内容稍晚出现（最多 2 秒），游戏不会停顿。关闭：游戏等待每一个着色器，在新场景中表现为卡顿。计算着色器始终需要等待；「预编译着色器」会根据游戏文件提前准备它们。"},

		{"advanced.title", "高级"}, {"advanced.stability", "性能面板、稳定性与日志"}, {"advanced.extra", "附加选项"},
		{"advanced.launcher", "启动器"},
		{"overlay", "性能面板"},
		{"overlay.desc", "游戏启动时即显示帧率、帧时间、处理器与显卡占用。游戏中按 F2 切换，Shift+F2 切换详细信息。"},
		{"redzone", "扩展红区保护"},
		{"redzone.desc", "实验性；游戏崩溃时可以试试。Windows 会把异常帧写到游戏栈指针的正下方，覆盖游戏代码可能存放数据的「红区」。模拟器始终让用到红区的函数在安全的副本中运行。打开后（--redzone），对无法复制的函数还会修补其中的单条指令。"},
		{"log", "日志"}, {"log.off", "关闭"}, {"log.file", "文件（在日志文件夹中）"}, {"log.console", "控制台（慢）"},
		{"log.desc", "模拟器自己的日志；写日志可能拖慢游戏。"},
		{"extra", "附加参数"},
		{"extra.desc", "kyty_emulator.exe 的其他选项，写法与其命令行相同（kyty_emulator.exe --help 会列出全部）。它们排在启动器自己的参数之后，因此优先生效。例如：--vblank-frequency 120 --hide-cursor --no-splash，或 --rebuild-pipeline-cache（配合「预编译着色器」：重新生成驱动缓存）。"},
		{"environment", "环境开关"},
		{"environment.desc", "模拟器的环境开关，格式为 KEY=VALUE，用空格或逗号分隔。例如：KYTY_RENDER_CPUS=auto KYTY_BDA_LAZY=0 KYTY_LIVE_FILE=C:\\bench\\live.txt。模拟器的实时开关名为 KYTY_ 加上其大写名称。"},
		{"uilang", "启动器语言"}, {"uilang.auto", "自动"},
		{"reopen", "游戏结束后回到启动器"},
		{"reopen.desc", "游戏运行时启动器在后台等待，游戏结束后重新出现，方便换一组设置再启动。关闭：游戏启动后启动器随即关闭。"},
		{"credit", "设置项沿用 KytyPS5 开发者的 KytyPS5 启动器（src/launcher）。"},

		{"dialog.game", "游戏文件夹（eboot.bin、sce_sys）"},
		{"dialog.zar", "打包为 ZArchive 的游戏文件夹"},
		{"dialog.data", "存档、着色器缓存和日志所在的文件夹"},
		{"dialog.streamline", "解压后的 NVIDIA Streamline SDK 文件夹，或其 bin\\x64 文件夹"},
		{"dialog.streamlineBad", "该文件夹及其 bin\\x64 中没有 NVIDIA 签名的 sl.interposer.dll。\n请从 https://github.com/NVIDIA-RTX/Streamline/releases 下载 Streamline SDK，并选择解压后的文件夹。"},
		{"error.title", "KytyPS5 启动器"},
		{"error.start", "无法启动：\n{0}"},
	};
}

}
