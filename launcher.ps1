param([switch]$DryRun, [string]$SettingsFile = '')
# A small settings window in front of run-windows.ps1 (launcher.cmd opens it), for Saros or another PS5 game. The
# settings and the console language names follow the KytyPS5 launcher (src/launcher, by the KytyPS5 developers); they
# are kept in launcher-settings.json next to this script (-SettingsFile <file>: in that file). -DryRun prints the
# commands without a window.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
# cmd.exe by its full path: Start-Process with a bare "cmd" opens whatever "cmd" it finds first, and a file of that
# name without an extension (in the working folder or on the PATH) makes Windows ask which app should open it.
$cmd = if ($env:ComSpec -and (Test-Path -LiteralPath $env:ComSpec)) { $env:ComSpec } else { Join-Path ([Environment]::SystemDirectory) 'cmd.exe' }
# Windows PowerShell by its path too: on a PC whose PATH lacks its folder, cmd.exe does not know "powershell".
$powershellExe = Join-Path ([Environment]::SystemDirectory) 'WindowsPowerShell\v1.0\powershell.exe'
$powershellCommand = if (Test-Path -LiteralPath $powershellExe) { "`"$powershellExe`"" } else { 'powershell' }
$settingsPath = if ($SettingsFile) { $SettingsFile } else { Join-Path $root 'launcher-settings.json' }
$exe = if (Test-Path "$root\kyty_emulator.exe") { "$root\kyty_emulator.exe" } else { "$root\_Build\windows\kyty_emulator.exe" }
$languages = 'Japanese', 'English (United States)', 'French (France)', 'Spanish (Spain)', 'German', 'Italian', 'Dutch',
	'Portuguese (Portugal)', 'Russian', 'Korean', 'Chinese (Traditional)', 'Chinese (Simplified)', 'Finnish', 'Swedish',
	'Danish', 'Norwegian', 'Polish', 'Portuguese (Brazil)', 'English (United Kingdom)', 'Turkish', 'Spanish (Latin America)',
	'Arabic', 'French (Canada)', 'Czech', 'Hungarian', 'Greek', 'Romanian', 'Thai', 'Vietnamese', 'Indonesian'
# The window sizes offered (the KytyPS5 launcher's and two smaller ones); another WIDTHxHEIGHT can be typed.
$resolutions = '1280x720', '1600x900', '1920x1080', '2560x1440', '3840x2160', '2560x1080', '3440x1440', '3840x1600'
# How frames reach the screen (run-windows.ps1 -PresentMode, the emulator's --present-mode; Mailbox is its default).
$presentModes = 'Mailbox', 'Fifo', 'Immediate'
$presentNames = 'Mailbox (no tearing)', 'Fifo (VSync)', 'Immediate (may tear)'
# The emulator's own log (run-windows.ps1 -Log, the emulator's --printf-direction).
$logModes = 'off', 'file', 'console'
$logNames = 'Off', 'File (in the logs folder)', 'Console (slow)'

$settings = [ordered]@{ game = ''; data = ''; resolution = '1920x1080'; fullscreen = $false; stretch = $false; language = 1
	gpu = -1; gpu_name = ''; present_mode = 'Mailbox'; hdr = $false; perf_overlay = $false; sound = $true
	skip_notice_screen = $false; redzone = $false; precompile_pipelines = $true; log = 'off'; extra_args = ''; environment = '' }
if (Test-Path -LiteralPath $settingsPath) {
	# (A file that cannot be read leaves the defaults.)
	try {
		$saved = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
		foreach ($property in $saved.PSObject.Properties) { if ($settings.Contains($property.Name)) { $settings[$property.Name] = $property.Value } }
	} catch {}
}
if (!$settings.game -and (Test-Path "$root\game-path.txt")) { $settings.game = "$(Get-Content "$root\game-path.txt" -TotalCount 1)".Trim() }
# (Values of another spelling or kind, as in a file edited by hand, become valid ones.)
$settings.present_mode = @($presentModes | Where-Object { $_ -eq [string]$settings.present_mode }) + $presentModes[0] | Select-Object -First 1
$settings.log = @($logModes | Where-Object { $_ -eq [string]$settings.log }) + $logModes[0] | Select-Object -First 1
$number = 0
$settings.language = if ([int]::TryParse([string]$settings.language, [ref]$number)) { [Math]::Max(0, [Math]::Min($languages.Count - 1, $number)) } else { 1 }
$settings.gpu = if ([int]::TryParse([string]$settings.gpu, [ref]$number)) { [Math]::Max(-1, $number) } else { -1 }

# A window size as WIDTHxHEIGHT (each 320 to 16384, the KytyPS5 launcher's limits); nothing for other text.
function Get-Resolution([string]$text) {
	if ($text -notmatch '^\s*(\d{3,5})\s*[xX]\s*(\d{3,5})\s*$') { return '' }
	$width = [int]$Matches[1]
	$height = [int]$Matches[2]
	if ($width -lt 320 -or $width -gt 16384 -or $height -lt 320 -or $height -gt 16384) { return '' }
	return "${width}x${height}"
}

# The graphics cards as Vulkan lists them (vulkan-1.dll, the loader that comes with every Vulkan driver): the
# emulator's --gpu takes a card's number in that list, as the KytyPS5 launcher's GPU box gives it. Nothing when the
# list cannot be read.
function Get-VulkanGpus {
	try {
		Add-Type -Namespace KytyLauncher -Name Vulkan -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)]
struct InstanceCreateInfo {
	public int sType;
	public IntPtr pNext;
	public int flags;
	public IntPtr pApplicationInfo;
	public uint enabledLayerCount;
	public IntPtr ppEnabledLayerNames;
	public uint enabledExtensionCount;
	public IntPtr ppEnabledExtensionNames;
}
[DllImport("vulkan-1.dll")]
static extern int vkCreateInstance(ref InstanceCreateInfo info, IntPtr allocator, out IntPtr instance);
[DllImport("vulkan-1.dll")]
static extern void vkDestroyInstance(IntPtr instance, IntPtr allocator);
[DllImport("vulkan-1.dll")]
static extern int vkEnumeratePhysicalDevices(IntPtr instance, ref uint count, IntPtr[] devices);
[DllImport("vulkan-1.dll")]
static extern void vkGetPhysicalDeviceProperties(IntPtr device, IntPtr properties);
// The physical devices' names, in vkEnumeratePhysicalDevices' order.
public static string[] Names() {
	var names = new System.Collections.Generic.List<string>();
	var info = new InstanceCreateInfo();
	info.sType = 1; // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
	IntPtr instance;
	if (vkCreateInstance(ref info, IntPtr.Zero, out instance) != 0) return names.ToArray();
	// VkPhysicalDeviceProperties: five 32-bit fields, then deviceName[256]; 824 bytes in all.
	IntPtr properties = Marshal.AllocHGlobal(1024);
	try {
		uint count = 0;
		vkEnumeratePhysicalDevices(instance, ref count, null);
		var devices = new IntPtr[count];
		if (count == 0 || vkEnumeratePhysicalDevices(instance, ref count, devices) < 0) return names.ToArray();
		for (uint i = 0; i < count; i++) {
			vkGetPhysicalDeviceProperties(devices[i], properties);
			names.Add(Marshal.PtrToStringAnsi(IntPtr.Add(properties, 20)));
		}
	} finally {
		Marshal.FreeHGlobal(properties);
		vkDestroyInstance(instance, IntPtr.Zero);
	}
	return names.ToArray();
}
'@
		return [KytyLauncher.Vulkan]::Names()
	} catch {
		return @()
	}
}
$gpuNames = @(Get-VulkanGpus)
# The card chosen (settings gpu, with its name) is found again by its name where the list's order has changed; one
# no longer listed leaves the choice to the emulator. Where the list cannot be read, its number is kept.
if ($settings.gpu -ge 0 -and $gpuNames.Count) {
	$at = $settings.gpu
	if ($at -ge $gpuNames.Count -or ($settings.gpu_name -and $gpuNames[$at] -ne $settings.gpu_name)) {
		$at = if ($settings.gpu_name) { [Array]::IndexOf($gpuNames, [string]$settings.gpu_name) } else { -1 }
	}
	$settings.gpu = $at
}
$gpuList = @(for ($i = 0; $i -lt $gpuNames.Count; $i++) { "${i}: $($gpuNames[$i])" })

# A game folder's sce_sys\param.json (its title and version); nothing for a .zar, which only the emulator reads.
function Read-GameParam([string]$game) {
	if (!$game -or !(Test-Path -LiteralPath "$game\sce_sys\param.json")) { return $null }
	try { return Get-Content -LiteralPath "$game\sce_sys\param.json" -Raw -Encoding UTF8 | ConvertFrom-Json } catch { return $null }
}

# The data folder (settings data): the emulator's working directory, where it keeps the saves (_SaveData), the shader
# and pipeline caches (_PipelineCache), downloads, temporary data and trophies. None chosen: this folder
# (run-windows.ps1 -DataDir). The logs go to its "logs" folder; in this folder, to a build tree's _Build\run-logs.
function Get-DataFolder {
	$data = [string]$settings.data
	if (!$data) { return $root }
	# (A relative one is this folder's.)
	try { if (![IO.Path]::IsPathRooted($data)) { return Join-Path $root $data } } catch {}
	return $data
}
function Get-LogFolder {
	$data = Get-DataFolder
	if ($data.TrimEnd('\') -ne $root.TrimEnd('\')) { return Join-Path $data 'logs' }
	if (Test-Path "$root\_Build") { return "$root\_Build\run-logs" }
	return "$root\logs"
}

# A value as one argument of a command line: in double quotes, a quote inside as \" and the backslashes before a
# quote doubled (the folder D:\data\ would otherwise end in an escaped quote).
function Format-Quoted([string]$text) {
	return '"' + ($text -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}
# The extra arguments (settings extra_args): emulator options as on its command line, given to run-windows.ps1
# -ExtraArgs in one piece. The command goes through cmd, so text with a character cmd would act on is left out;
# so is text that does not start with an option (--name) or leaves a double quote open.
$cmdCharacters = [char[]]@('&', '|', '<', '>', '^', '!', '%')
function Get-ExtraProblem {
	$text = ([string]$settings.extra_args).Trim()
	if (!$text) { return '' }
	if ($text.IndexOfAny($cmdCharacters) -ge 0) { return 'Extra arguments left out: the characters & | < > ^ ! % cannot be passed.' }
	if ($text -notmatch '^--[A-Za-z]') { return 'Extra arguments left out: emulator options start with -- (for example --hide-cursor).' }
	if (($text.Length - $text.Replace('"', '').Length) % 2) { return 'Extra arguments left out: a double quote is not closed.' }
	return ''
}
function Get-ExtraArguments {
	if (Get-ExtraProblem) { return '' }
	return ([string]$settings.extra_args).Trim()
}
# The environment switches (settings environment): KEY=VALUE pairs separated by spaces, commas or semicolons (a
# separator starts a new pair only before KEY=, so a value may hold them), one -Set argument for run-windows.ps1.
# A piece that is no KEY=VALUE, or holds a character cmd would act on, is left out.
function Get-EnvironmentPieces {
	$text = ([string]$settings.environment).Trim([char[]]" ,;`t")
	if (!$text) { return @() }
	return @($text -split '[\s,;]+(?=[A-Za-z_][A-Za-z0-9_]*=)')
}
function Test-EnvironmentPair([string]$pair) {
	return $pair -match '^[A-Za-z_][A-Za-z0-9_]*=' -and $pair.IndexOfAny($cmdCharacters) -lt 0 -and !$pair.Contains('"')
}
function Get-EnvironmentPairs { return @(Get-EnvironmentPieces | Where-Object { Test-EnvironmentPair $_ }) }
# What of the two text boxes is left out, for the window and -DryRun.
function Get-Problems {
	$problems = @()
	$extra = Get-ExtraProblem
	if ($extra) { $problems += $extra }
	$dropped = @(Get-EnvironmentPieces | Where-Object { !(Test-EnvironmentPair $_) })
	if ($dropped.Count) { $problems += "Environment switches left out (KEY=VALUE without & | < > ^ ! % `"): $($dropped -join ' ')" }
	return $problems
}

# run-windows.ps1's arguments for the settings. The choices with a value (window size, language, present mode, log,
# background shader preparation) are always passed, so that another default of the emulator or the launch config
# changes nothing here.
function Get-RunArguments {
	$size = (Get-Resolution ([string]$settings.resolution)) -split 'x'
	if ($size.Count -ne 2) { $size = '1920', '1080' }
	$arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Format-Quoted "$root\run-windows.ps1"),
		'-Width', $size[0], '-Height', $size[1], '-Language', [int]$settings.language,
		'-PresentMode', $settings.present_mode, '-Log', $settings.log)
	# Background shader preparation beside the game: on or off, as chosen (the emulator's --precompile-pipelines or
	# --no-precompile-pipelines).
	$arguments += @('-PrecompilePipelines', $(if ($settings.precompile_pipelines) { 'on' } else { 'off' }))
	if ($settings.fullscreen) { $arguments += '-Fullscreen' }
	# (Without it the game's picture keeps its aspect ratio, between black bars in a window of another shape.)
	if ($settings.stretch) { $arguments += '-Stretch' }
	# HDR10 output when Windows HDR is on for the display; SDR otherwise.
	if ($settings.hdr) { $arguments += '-Hdr' }
	# The graphics card by its number in Vulkan's list, for the game and the precompile alike (the driver's pipeline
	# cache is the card's).
	if ([int]$settings.gpu -ge 0) { $arguments += @('-Gpu', [int]$settings.gpu) }
	# The emulator's performance panel (F2 in the game switches it).
	if ($settings.perf_overlay) { $arguments += '-PerfOverlay' }
	if (!$settings.sound) { $arguments += '-NoSound' }
	if ($settings.skip_notice_screen) { $arguments += '-SkipNotice' }
	if ($settings.redzone) { $arguments += '-RedZone' }
	if ($settings.data) { $arguments += @('-DataDir', (Format-Quoted (Get-DataFolder))) }
	$pairs = @(Get-EnvironmentPairs)
	if ($pairs.Count) { $arguments += @('-Set', (Format-Quoted ($pairs -join ','))) }
	$extra = Get-ExtraArguments
	if ($extra) { $arguments += @('-ExtraArgs', (Format-Quoted $extra)) }
	if ($settings.game) { $arguments += @('-Game', (Format-Quoted ([string]$settings.game))) }
	return $arguments
}
function Get-PlayCommand {
	# The console stays for the live log; after a crash it waits for a key.
	return "$powershellCommand " + ((@(Get-RunArguments) + '-Follow') -join ' ') + ' || pause'
}
# The precompile pass with the same settings (run-windows.ps1 -Precompile: the emulator's --precompile-only); its
# summary also goes to $resultFile, which the window shows.
function Get-PrecompileCommand([string]$resultFile) {
	$arguments = @(Get-RunArguments) + '-Precompile'
	if ($resultFile) { $arguments += @('-ResultFile', (Format-Quoted $resultFile)) }
	return "$powershellCommand " + ($arguments -join ' ') + ' & pause'
}
function Save-Settings { $settings | ConvertTo-Json | Set-Content -LiteralPath $settingsPath -Encoding UTF8 }
if ($DryRun) {
	"play:       cmd /s /c `"$(Get-PlayCommand)`""
	"precompile: cmd /s /c `"$(Get-PrecompileCommand (Join-Path (Get-LogFolder) 'precompile-last.txt'))`""
	"settings:   $settingsPath"
	"emulator:   $exe$(if (!(Test-Path -LiteralPath $exe)) { ' (missing: build it with build-windows.cmd)' })"
	"data:       $(Get-DataFolder); logs: $(Get-LogFolder)"
	"gpus:       $(if ($gpuList.Count) { $gpuList -join ', ' } else { 'Vulkan lists none' }); chosen: $(if ($settings.gpu -ge 0) { $settings.gpu } else { 'automatic' })"
	Get-Problems | ForEach-Object { "note:       $_" }
	return
}

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -Namespace KytyLauncher -Name Dpi -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
[KytyLauncher.Dpi]::SetProcessDPIAware() | Out-Null
[System.Windows.Forms.Application]::EnableVisualStyles()

$form = New-Object System.Windows.Forms.Form -Property @{
	Text = 'KytyPS5 launcher (Saros build)'; FormBorderStyle = 'FixedDialog'; MaximizeBox = $false; StartPosition = 'CenterScreen'
	AutoSize = $true; AutoSizeMode = 'GrowAndShrink'; Font = New-Object System.Drawing.Font('Segoe UI', 9); Padding = '12,12,12,8'
}
$grid = New-Object System.Windows.Forms.TableLayoutPanel -Property @{ ColumnCount = 2; AutoSize = $true; Dock = 'Fill' }
$form.Controls.Add($grid)
$tips = New-Object System.Windows.Forms.ToolTip -Property @{ AutoPopDelay = 30000 }
# A row: a label, then the controls side by side.
function Add-Row($label, [object[]]$controls) {
	$grid.Controls.Add((New-Object System.Windows.Forms.Label -Property @{ Text = $label; AutoSize = $true; Anchor = 'Left'; Margin = '0,6,8,6' }))
	$row = New-Object System.Windows.Forms.FlowLayoutPanel -Property @{ AutoSize = $true; WrapContents = $false; Margin = '0,0,0,0' }
	$row.Controls.AddRange($controls)
	$grid.Controls.Add($row)
}
function New-Note([string]$text) {
	return New-Object System.Windows.Forms.Label -Property @{ Text = $text; AutoSize = $true; ForeColor = 'Gray'; Margin = '6,6,0,0' }
}

$game = New-Object System.Windows.Forms.TextBox -Property @{ Text = $settings.game; Width = 560 }
$browseZar = New-Object System.Windows.Forms.Button -Property @{ Text = '.zar...'; AutoSize = $true }
$browse = New-Object System.Windows.Forms.Button -Property @{ Text = 'Folder...'; AutoSize = $true }
Add-Row 'Game' @($game, $browseZar, $browse)
$title = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Gray' }
Add-Row '' @($title)
# A game: the game folder packed into a ZArchive (a .zar file, as the Saros copy is), or the folder itself (eboot.bin
# and sce_sys).
$gameHint = 'Choose the game: its .zar archive, or the folder with eboot.bin and sce_sys.'
function Update-Title {
	$path = $game.Text.Trim().Trim('"')
	$title.Text = $gameHint
	try {
		if (!$path) { return }
		$json = Read-GameParam $path
		if ($json) {
			$title.Text = '{0} ({1} {2})' -f $json.localizedParameters.($json.localizedParameters.defaultLanguage).titleName,
				$json.titleId, $json.contentVersion
		} elseif (Test-Path -LiteralPath $path -PathType Leaf) {
			$title.Text = '{0}, {1:N1} GB: the emulator reads it without extracting it' -f $(if ($path -match '\.zar$') { 'ZArchive' } else { 'File' }),
				((Get-Item -LiteralPath $path).Length / 1GB)
		} elseif (Test-Path -LiteralPath "$path\eboot.bin") {
			$title.Text = 'Game folder (eboot.bin)'
		} else {
			$title.Text = 'Not found. ' + $gameHint
		}
	} catch {}
}
# The game folder packed into a ZArchive: read without extracting it.
$browseZar.Add_Click({
	$dialog = New-Object System.Windows.Forms.OpenFileDialog -Property @{ Title = 'The game folder packed into a ZArchive'; Filter = 'ZArchive (*.zar)|*.zar|All files (*.*)|*.*' }
	$current = $game.Text.Trim().Trim('"')
	if ($current -and (Test-Path -LiteralPath $current -PathType Leaf)) { $dialog.InitialDirectory = Split-Path $current }
	if ($dialog.ShowDialog($form) -eq 'OK') { $game.Text = $dialog.FileName }
})
$browse.Add_Click({
	$dialog = New-Object System.Windows.Forms.FolderBrowserDialog -Property @{ Description = 'The game folder (eboot.bin, sce_sys)' }
	$current = $game.Text.Trim().Trim('"')
	if ($current -and (Test-Path -LiteralPath $current -PathType Container)) { $dialog.SelectedPath = $current }
	if ($dialog.ShowDialog($form) -eq 'OK') { $game.Text = $dialog.SelectedPath }
})
$game.Add_TextChanged({ Update-Title })
Update-Title

$data = New-Object System.Windows.Forms.TextBox -Property @{ Text = $settings.data; Width = 560 }
$browseData = New-Object System.Windows.Forms.Button -Property @{ Text = 'Browse...'; AutoSize = $true }
$tips.SetToolTip($data, "Where the emulator keeps its data: the saves (_SaveData), the shader and pipeline caches (_PipelineCache),`n" +
	"downloads, temporary data, trophies and the logs. Empty: the launcher's own folder.`n" +
	"Each folder has its own saves and shader caches: a precompile is for the folder it ran with.")
$browseData.Add_Click({
	$dialog = New-Object System.Windows.Forms.FolderBrowserDialog -Property @{ Description = 'The folder for the saves, shader caches and logs' }
	$current = $data.Text.Trim().Trim('"')
	$dialog.SelectedPath = if ($current -and (Test-Path -LiteralPath $current -PathType Container)) { $current } else { $root }
	if ($dialog.ShowDialog($form) -eq 'OK') { $data.Text = $dialog.SelectedPath }
})
Add-Row 'Data folder' @($data, $browseData)
Add-Row '' @((New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Gray'
	Text = "Where the saves, shader caches and logs are kept. Empty: the launcher's own folder." }))

# The window: its size, typed or picked; the game's picture keeps its aspect ratio in it (black bars) unless stretched.
$resolution = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDown'; Width = 120 }
$resolution.Items.AddRange($resolutions)
$resolution.Text = if (Get-Resolution ([string]$settings.resolution)) { Get-Resolution ([string]$settings.resolution) } else { '1920x1080' }
$tips.SetToolTip($resolution, 'The window as WIDTHxHEIGHT: pick a size or type one (320 to 16384 each).')
$fullscreen = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'Fullscreen'; AutoSize = $true; Checked = [bool]$settings.fullscreen; Margin = '12,4,3,3' }
$tips.SetToolTip($fullscreen, 'Borderless fullscreen on the desktop. F11 or Alt+Enter in the game switches it.')
$stretch = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'Stretch to the window (no black bars)'; AutoSize = $true; Checked = [bool]$settings.stretch; Margin = '12,4,3,3' }
$tips.SetToolTip($stretch, "Off: the game's picture keeps its aspect ratio, with black bars in a window or on a screen of another shape`n(21:9, 16:10). On: it fills the window.")
Add-Row 'Window size' @($resolution, $fullscreen, $stretch)
$language = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 320 }
$language.Items.AddRange($languages)
$language.SelectedIndex = [Math]::Max(0, [Math]::Min($languages.Count - 1, [int]$settings.language))
Add-Row 'Console language' @($language)
# The graphics card: left to the emulator, or one of Vulkan's list by its number. (A number chosen where the list
# cannot be read stays as "GPU <n>".)
$gpu = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 320 }
[void]$gpu.Items.Add('Automatic')
foreach ($item in $gpuList) { [void]$gpu.Items.Add($item) }
if (!$gpuNames.Count -and $settings.gpu -ge 0) { [void]$gpu.Items.Add("GPU $($settings.gpu)") }
$gpu.SelectedIndex = if ($settings.gpu -lt 0) { 0 } elseif ($gpuNames.Count) { $settings.gpu + 1 } else { 1 }
Add-Row 'GPU' @($gpu, (New-Note 'for the game and the precompile: shader caches are made for one card'))
$present = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 320 }
$present.Items.AddRange($presentNames)
$present.SelectedIndex = [Math]::Max(0, [Array]::IndexOf($presentModes, [string]$settings.present_mode))
$tips.SetToolTip($present, "Mailbox: no tearing, the newest finished frame is shown (the emulator's default).`n" +
	"Fifo: every frame waits for the display's refresh (VSync).`n" +
	"Immediate: frames are shown at once; the picture may tear.`n" +
	"Where the graphics card lacks the mode chosen, the emulator takes Fifo.")
Add-Row 'Present mode' @($present)
$hdr = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'HDR (turn on "Use HDR" in Windows display settings first)'; AutoSize = $true; Checked = [bool]$settings.hdr }
Add-Row '' @($hdr)
$overlay = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'Performance panel (frame rate, frame times, CPU and GPU; F2 in the game switches it, Shift+F2 its details)'; AutoSize = $true; Checked = [bool]$settings.perf_overlay }
Add-Row '' @($overlay)
$sound = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'Sound'; AutoSize = $true; Checked = [bool]$settings.sound }
$tips.SetToolTip($sound, "Off: the emulator starts with SDL's dummy audio driver (SDL_AUDIO_DRIVER=dummy), which plays nothing:`n" +
	"the game's sound, the trophy sound, and a wired DualSense's haptics, which are sent as audio.`n" +
	"The emulator plays on Windows' default playback device; it has no choice of device.")
Add-Row '' @($sound, (New-Note 'on Windows'' default playback device'))
$skipNotice = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'Skip startup logos and notices (where the game asks the console whether to)'; AutoSize = $true; Checked = [bool]$settings.skip_notice_screen }
Add-Row '' @($skipNotice)
$redzone = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'Extended red-zone protection (experimental; try it when the game crashes)'; AutoSize = $true; Checked = [bool]$settings.redzone }
$tips.SetToolTip($redzone, "Windows writes exception frames just below the game's stack pointer, over the 'red zone' its code may keep data in.`n" +
	"The emulator always runs the functions that do from a copy that is safe. With this (--redzone) it also patches`n" +
	"single instructions of the functions it could not copy.")
Add-Row '' @($redzone)
$precompilePipelines = New-Object System.Windows.Forms.CheckBox -Property @{ Text = 'Prepare shaders in the background while the game runs'; AutoSize = $true; Checked = [bool]$settings.precompile_pipelines }
$tips.SetToolTip($precompilePipelines, "On: the shaders and pipelines earlier sessions recorded are built in the background, at low priority, from the`n" +
	"start of the game, so that fewer are compiled the moment they are first drawn (a stutter).`n" +
	"Off: they are compiled when first needed. 'Precompile shaders' below builds them all before playing, either way.")
Add-Row '' @($precompilePipelines)
$log = New-Object System.Windows.Forms.ComboBox -Property @{ DropDownStyle = 'DropDownList'; Width = 320 }
$log.Items.AddRange($logNames)
$log.SelectedIndex = [Math]::Max(0, [Array]::IndexOf($logModes, [string]$settings.log))
Add-Row 'Log' @($log, (New-Note 'the emulator''s own log; writing it can slow the game'))
$extraArgs = New-Object System.Windows.Forms.TextBox -Property @{ Text = $settings.extra_args; Width = 640 }
$tips.SetToolTip($extraArgs, "More options of kyty_emulator.exe, as on its command line (kyty_emulator.exe --help lists them all).`n" +
	"They come after the launcher's own, so they win. For example:`n" +
	"--vblank-frequency 120 --hide-cursor --no-splash`n" +
	"--rebuild-pipeline-cache   (with 'Precompile shaders': the driver cache made anew)")
Add-Row 'Extra arguments' @($extraArgs)
$switches = New-Object System.Windows.Forms.TextBox -Property @{ Text = $settings.environment; Width = 640 }
$tips.SetToolTip($switches, "Environment switches of the emulator, KEY=VALUE, separated by spaces or commas. For example:`n" +
	"KYTY_RENDER_CPUS=auto KYTY_BDA_LAZY=0 KYTY_LIVE_FILE=C:\bench\live.txt`n" +
	"A live switch of the emulator is KYTY_ and its name in capitals.")
Add-Row 'Environment' @($switches)
# What of the two boxes is left out of the command (nothing: the label takes no room).
$problem = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Firebrick'; Visible = $false; Margin = '0,4,0,0' }
$grid.Controls.Add($problem)
$grid.SetColumnSpan($problem, 2)
function Update-Problem {
	$settings.extra_args = $extraArgs.Text.Trim()
	$settings.environment = $switches.Text.Trim()
	$problems = @(Get-Problems)
	$problem.Text = $problems -join "`n"
	$problem.Visible = [bool]$problems.Count
}
$extraArgs.Add_TextChanged({ Update-Problem })
$switches.Add_TextChanged({ Update-Problem })
Update-Problem

$buttons = New-Object System.Windows.Forms.FlowLayoutPanel -Property @{ AutoSize = $true; Margin = '0,10,0,0' }
$play = New-Object System.Windows.Forms.Button -Property @{ Text = 'Play'; AutoSize = $true; Font = New-Object System.Drawing.Font('Segoe UI', 9, [System.Drawing.FontStyle]::Bold) }
$precompile = New-Object System.Windows.Forms.Button -Property @{ Text = 'Precompile shaders'; AutoSize = $true }
$tips.SetToolTip($precompile, "Builds every shader and pipeline that earlier sessions of the game recorded into the graphics driver's cache,`n" +
	"with the settings above, then exits: the game finds them there instead of compiling them while playing.`n" +
	"Worth running after a new emulator build or a graphics driver update. Its summary and the time it took show`n" +
	"here when it ends.")
$logs = New-Object System.Windows.Forms.Button -Property @{ Text = 'Logs folder'; AutoSize = $true }
$readme = New-Object System.Windows.Forms.Button -Property @{ Text = 'README'; AutoSize = $true }
$buttons.Controls.AddRange(@($play, $precompile, $logs, $readme))
$grid.Controls.Add($buttons)
$grid.SetColumnSpan($buttons, 2)
# What the launcher has to say: a missing emulator, the precompile pass's state and its summary.
$status = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; Margin = '0,6,0,0'; MaximumSize = New-Object System.Drawing.Size(900, 0) }
$status.Text = if (Test-Path -LiteralPath $exe) { '' } else { "kyty_emulator.exe is missing ($exe): build it with build-windows.cmd." }
$grid.Controls.Add($status)
$grid.SetColumnSpan($status, 2)
$credit = New-Object System.Windows.Forms.Label -Property @{ AutoSize = $true; ForeColor = 'Gray'; Margin = '0,8,0,0'
	Text = 'Settings follow the KytyPS5 launcher (src/launcher) by the KytyPS5 developers.' }
$grid.Controls.Add($credit)
$grid.SetColumnSpan($credit, 2)
$form.AcceptButton = $play

function Read-Form {
	$settings.game       = $game.Text.Trim().Trim('"')
	$settings.data       = $data.Text.Trim().Trim('"')
	# (Text that is no size leaves the last one.)
	if (Get-Resolution $resolution.Text) { $settings.resolution = Get-Resolution $resolution.Text }
	$settings.fullscreen = $fullscreen.Checked
	$settings.stretch    = $stretch.Checked
	$settings.language   = $language.SelectedIndex
	# (The card's name too: its number is found again by it when Vulkan's list changes.)
	if ($gpuNames.Count) {
		$settings.gpu      = $gpu.SelectedIndex - 1
		$settings.gpu_name = if ($gpu.SelectedIndex -gt 0) { $gpuNames[$gpu.SelectedIndex - 1] } else { '' }
	} elseif ($gpu.SelectedIndex -eq 0) {
		$settings.gpu      = -1
		$settings.gpu_name = ''
	}
	$settings.present_mode         = $presentModes[[Math]::Max(0, $present.SelectedIndex)]
	$settings.hdr                  = $hdr.Checked
	$settings.perf_overlay         = $overlay.Checked
	$settings.sound                = $sound.Checked
	$settings.skip_notice_screen   = $skipNotice.Checked
	$settings.redzone              = $redzone.Checked
	$settings.precompile_pipelines = $precompilePipelines.Checked
	$settings.log                  = $logModes[[Math]::Max(0, $log.SelectedIndex)]
	$settings.extra_args           = $extraArgs.Text.Trim()
	$settings.environment          = $switches.Text.Trim()
	Save-Settings
}
$play.Add_Click({
	Read-Form
	Start-Process $cmd -ArgumentList "/s /c `"$(Get-PlayCommand)`"" -WorkingDirectory $root
	$form.Close()
})
# The precompile pass in a console of its own, which shows its progress and stays until a key is pressed; when the
# pass ends, its summary line and the time it took (run-windows.ps1 -ResultFile) show in the window. Until then
# neither button starts another emulator on the same caches.
$precompileTimer = New-Object System.Windows.Forms.Timer -Property @{ Interval = 1000 }
$precompile.Add_Click({
	Read-Form
	$script:precompileResult = Join-Path (Get-LogFolder) 'precompile-last.txt'
	Remove-Item -LiteralPath $script:precompileResult -ErrorAction SilentlyContinue
	$script:precompileBegin = Get-Date
	$script:precompileProcess = Start-Process $cmd -ArgumentList "/s /c `"$(Get-PrecompileCommand $script:precompileResult)`"" -WorkingDirectory $root -PassThru
	$play.Enabled = $false
	$precompile.Enabled = $false
	$status.Text = 'Precompiling the recorded shaders: its console window shows the progress.'
	$precompileTimer.Start()
})
$precompileTimer.Add_Tick({
	$lines = @()
	if (Test-Path -LiteralPath $script:precompileResult) {
		# (Read again at the next tick when it is still being written.)
		try { $lines = @(Get-Content -LiteralPath $script:precompileResult) } catch { return }
		if (!$lines.Count) { return }
	} elseif (!$script:precompileProcess.HasExited) {
		$status.Text = 'Precompiling the recorded shaders ({0:h\:mm\:ss}): its console window shows the progress.' -f ((Get-Date) - $script:precompileBegin)
		return
	}
	$precompileTimer.Stop()
	$play.Enabled = $true
	$precompile.Enabled = $true
	$status.Text = if ($lines.Count) { $lines -join "`n" } else { 'The precompile window was closed before the pass ended.' }
})
$logs.Add_Click({
	Read-Form
	$folder = Get-LogFolder
	New-Item -ItemType Directory -Force $folder | Out-Null
	Start-Process explorer.exe "`"$folder`""
})
$readme.Add_Click({
	if (Test-Path "$root\README.md") { Start-Process notepad.exe "`"$root\README.md`"" }
})
$form.Add_FormClosing({ Read-Form })
[void]$form.ShowDialog()
