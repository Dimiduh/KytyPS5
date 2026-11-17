# Windows launcher: runs the Windows build with the command line of a launch config.
#   .\run-windows.ps1                        the config's command line (run-windows.json), 1920x1080
#   .\run-windows.ps1 -Game <file.zar>       the game: its folder packed into a ZArchive (read without extracting it),
#   .\run-windows.ps1 -Game <folder>         or the folder with eboot.bin and sce_sys; remembered in game-path.txt, a
#                                            file dialog when none is known
#   .\run-windows.ps1 -DataDir <folder>      where the emulator keeps its data, its working directory (_SaveData,
#                                            _PipelineCache, _DownloadData, _TempData, _Trophies); default: this folder
#   .\run-windows.ps1 -Config <launch.json>  another launch config
#   .\run-windows.ps1 -Width 2560 -Height 1440 -Fullscreen
#   .\run-windows.ps1 -Stretch               the image stretched to the window, instead of kept at the game's aspect
#                                            ratio between black bars
#   .\run-windows.ps1 -Hdr                   HDR10 output when Windows HDR is on for the display (SDR otherwise)
#   .\run-windows.ps1 -PresentMode Fifo      how frames reach the screen: Fifo (VSync), Mailbox (no tearing, the newest
#                                            frame; the emulator's default) or Immediate (no waiting, may tear)
#   .\run-windows.ps1 -Language 11           the console language (0-29 as the PS5 numbers them: 1 English (US),
#                                            11 Chinese (Simplified); default: the config's)
#   .\run-windows.ps1 -Gpu 1                 the graphics card on a PC with more than one: its number in Vulkan's list
#                                            (the launcher shows the list by name; default: the emulator chooses)
#   .\run-windows.ps1 -PerfOverlay           the performance panel from the start (F2 switches it, Shift+F2 its details)
#   .\run-windows.ps1 -NoSound               no sound: SDL's dummy audio driver (SDL_AUDIO_DRIVER=dummy)
#   .\run-windows.ps1 -RedZone               guest red-zone protection also for the functions the emulator cannot copy
#   .\run-windows.ps1 -SkipNotice            the game is told to skip its startup logos and notices
#   .\run-windows.ps1 -PrecompilePipelines off  the recorded shaders and pipelines not compiled in the background
#                                            beside the game (on: compiled, the emulator's default; either is passed
#                                            to the emulator as given)
#   .\run-windows.ps1 -Precompile            compile every recorded shader and pipeline into the driver cache, then
#                                            exit: its "Precompile done:" line and the time it took at the end
#   .\run-windows.ps1 -ImportShaders <file>  with -Precompile: also build the compute shaders of this file, which
#                                            holds the shaders taken out of the game's own files, so that they are
#                                            ready before the game first uses them. A folder stands for its
#                                            <title id>.seeds, which the pass writes itself from an Unreal Engine 5
#                                            game's files when it is missing (shader-seeds\README.md). Default: the
#                                            shader-seeds folder next to this script, if there is one; none: no file
#   .\run-windows.ps1 -SkipPendingDraws on   a graphics pipeline the game needs and nothing has built yet is built
#                                            in the background, and the draws that use it are left out until it is
#                                            ready (at most 2 s) instead of the game waiting for it: no stall, the
#                                            object appears a moment late. off: the game waits (the emulator's
#                                            default)
#   .\run-windows.ps1 -PipelineList <file>   also compile the shaders and pipelines of a shared pipeline list (what
#                                            another data folder or player recorded, without shader code; the code
#                                            comes from what this game has created or imported): with -Precompile
#                                            in that pass, otherwise in the background beside the game. A folder
#                                            stands for its <title id>.pipelinelist. Default: the pipeline-lists
#                                            folder next to this script, if there is one; none: no list
#   .\run-windows.ps1 -ExportPipelineList <file>   write what this data folder has recorded as such a list, then exit
#   .\run-windows.ps1 -ShaderPairs <file>    a list of the vertex and pixel shaders of the game's files that are
#                                            drawn together (no shader code; shader-pairs\README.md): -Precompile
#                                            builds their graphics pipelines before any session has drawn with
#                                            them, and a session takes those. A folder stands for its
#                                            <title id>.pairs. Default: the shader-pairs folder next to this
#                                            script, if there is one; none: no list
#   .\run-windows.ps1 -FrameGen 1            DLSS frame generation: the number of generated frames per rendered
#                                            one (1 = 2x, up to 5 = 6x; NVIDIA RTX 40 or newer, 2 and up RTX 50;
#                                            the GPU's maximum is taken when more is asked for). Needs NVIDIA's
#                                            Streamline runtime (-StreamlineDir) and a build made with its SDK;
#                                            0: off, whatever the config says
#   .\run-windows.ps1 -RenderScale 50        Unreal Engine titles (Saros): the size the 3D is rendered at, in percent
#                                            of the game's own (Saros: 1600x900 to 2304x1296, picked as it goes); the
#                                            game's upscaler still makes the 4K picture. Less work for the graphics
#                                            card, a softer picture: at one spot of Saros 31 fps at 100, 33 at 75, 36 at
#                                            50 (RTX 4090). 100: as the game is, whatever the config says
#   .\run-windows.ps1 -UnrealCvar r.X=1,r.Y=x0.5   Unreal Engine titles: console variables of the running game, set
#                                            and kept set (--unreal-cvar: float and int variables the engine reads as
#                                            it runs; x0.5: half the game's own value). none: not the config's either
#   .\run-windows.ps1 -StreamlineDir <dir>   where NVIDIA's Streamline runtime is: the unpacked Streamline SDK (a
#                                            release zip from github.com/NVIDIA-RTX/Streamline/releases) or its
#                                            bin\x64. Default: a "streamline" folder next to the emulator, else the
#                                            SDK beside a local build (_Build\deps\streamline\sdk)
#   .\run-windows.ps1 -Log file              the emulator's own log into the logs folder (console: into the run log,
#                                            which -Follow shows, slow; off: none)
#   .\run-windows.ps1 -Set KEY=VALUE         an environment switch of the emulator (KYTY_*, SDL_*; KEY= removes one);
#                                            several: -Set A=1,B=2
#   .\run-windows.ps1 -ExtraArgs "--vblank-frequency 120 --hide-cursor"   more emulator options, as on its command
#                                            line (kyty_emulator.exe --help lists them); they come last, so they win
#   .\run-windows.ps1 -Affinity FFFFCF       only these CPUs (hex mask; default: the config's list, else all)
#   .\run-windows.ps1 -Follow                stay open showing the run log until the game exits, then its exit code
#   .\run-windows.ps1 -DryRun                print environment and command only: nothing is started or written
# A release package has its kyty_emulator.exe and launch.json next to this script: those are used instead of the
# build tree's (_Build\windows\kyty_emulator.exe, built by build-windows.cmd, and run-windows.json).
param(
	[string]$Config = $(@("$PSScriptRoot\launch.json", "$PSScriptRoot\run-windows.json") | Where-Object { Test-Path $_ } |
	                    Select-Object -First 1),
	[string]$Game = '',
	[string]$Exe = $(if (Test-Path "$PSScriptRoot\kyty_emulator.exe") { "$PSScriptRoot\kyty_emulator.exe" } else { "$PSScriptRoot\_Build\windows\kyty_emulator.exe" }),
	[string]$DataDir = '',
	[int]$Width = 0,
	[int]$Height = 0,
	[switch]$Fullscreen,
	[switch]$Stretch,
	[switch]$Hdr,
	[ValidateSet('', 'Fifo', 'Mailbox', 'Immediate')][string]$PresentMode = '',
	[int]$Language = -1,
	[int]$Gpu = -1,
	[switch]$PerfOverlay,
	[switch]$NoSound,
	[switch]$RedZone,
	[switch]$SkipNotice,
	[ValidateSet('', 'on', 'off')][string]$PrecompilePipelines = '',
	[switch]$Precompile,
	[string]$ImportShaders = '',
	[ValidateSet('', 'on', 'off')][string]$SkipPendingDraws = '',
	[string]$PipelineList = '',
	[string]$ExportPipelineList = '',
	[string]$ShaderPairs = '',
	[ValidateRange(-1, 5)][int]$FrameGen = -1,
	[ValidateRange(0, 200)][int]$RenderScale = 0,
	[string[]]$UnrealCvar = @(),
	[string]$StreamlineDir = '',
	[ValidateSet('', 'off', 'file', 'console')][string]$Log = '',
	[string[]]$Set = @(),
	[string]$ExtraArgs = '',
	[string]$Affinity = '',
	# -Precompile: the pass's last lines (its summary, then the time and exit code) into this file too, for the
	# launcher's window.
	[string]$ResultFile = '',
	[switch]$Follow,
	[switch]$DryRun
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Exe)) {
	if (!$DryRun) { throw "missing $Exe; build it with build-windows.cmd" }
	Write-Host "warning:  missing $Exe (build it with build-windows.cmd)"
}
if (!$Config -or !(Test-Path -LiteralPath $Config)) { throw 'missing launch config (run-windows.json next to this script, or -Config <file>)' }

# Dialogs: sharp on scaled displays (the process is DPI aware before its first window).
function Initialize-Dialogs {
	Add-Type -AssemblyName System.Windows.Forms, System.Drawing
	Add-Type -Namespace Kyty -Name Dpi -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
	[Kyty.Dpi]::SetProcessDPIAware() | Out-Null
	[System.Windows.Forms.Application]::EnableVisualStyles()
}

# A game: its folder (eboot.bin and sce_sys), or a file: the folder packed into a ZArchive (a .zar file, read by the
# emulator without extracting it) or an ELF (--game <dir|elf|zar>).
function Test-Game([string]$game) {
	$game -and ((Test-Path -LiteralPath "$game\eboot.bin") -or (Test-Path -LiteralPath $game -PathType Leaf))
}
# A game folder's sce_sys\param.json (its title and version); nothing for a .zar, which only the emulator reads.
function Read-GameParam([string]$game) {
	if (!$game -or !(Test-Path -LiteralPath "$game\sce_sys\param.json")) { return $null }
	try { return Get-Content -LiteralPath "$game\sce_sys\param.json" -Raw -Encoding UTF8 | ConvertFrom-Json } catch { return $null }
}
# An argument as the emulator's command line takes it: in double quotes when it holds a space or a quote, a quote
# inside as \" and the backslashes before a quote doubled (C:\My Games\ would otherwise end in an escaped quote).
function Format-Argument([string]$text) {
	if ($text -and $text -notmatch '[\s"]') { return $text }
	return '"' + ($text -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}

# The game: -Game, else the last one given; a file dialog when that is no game (a portable package started without
# the launcher). The folder of a chosen eboot.bin is the game.
$gameFile = "$PSScriptRoot\game-path.txt"
$remember = [bool]$Game
if (!$Game -and (Test-Path -LiteralPath $gameFile)) { $Game = "$(Get-Content -LiteralPath $gameFile -Raw)".Trim() }
if (!$remember -and !$DryRun -and !(Test-Game $Game)) {
	Initialize-Dialogs
	$dialog = New-Object System.Windows.Forms.OpenFileDialog -Property @{
		Title = 'Choose the game: its .zar archive, or eboot.bin in its folder'
		Filter = 'PS5 game (*.zar; eboot.bin)|*.zar;eboot.bin|All files (*.*)|*.*' }
	if ($dialog.ShowDialog() -eq 'OK') {
		$Game = if ((Split-Path $dialog.FileName -Leaf) -eq 'eboot.bin') { Split-Path $dialog.FileName } else { $dialog.FileName }
		$remember = $true
	}
}
if (!(Test-Game $Game)) {
	$problem = if ($Game) { "no game at $Game (a .zar archive, or a folder with eboot.bin)" } else { 'no game given (-Game <file.zar or folder>)' }
	if (!$DryRun) { throw $problem }
	Write-Host "warning:  $problem"
}
if ($remember -and !$DryRun) { Set-Content -LiteralPath $gameFile $Game -Encoding UTF8 }
$param = Read-GameParam $Game
$about = if ($param) { "$($param.localizedParameters.($param.localizedParameters.defaultLanguage).titleName), $($param.titleId) $($param.contentVersion)" }
	elseif ($Game -and (Test-Path -LiteralPath $Game -PathType Leaf)) { '{0} ({1:N1} GB)' -f $Game, ((Get-Item -LiteralPath $Game).Length / 1GB) }
	else { $Game }

# The data folder: the emulator's working directory, where it keeps the saves (_SaveData), the shader and pipeline
# caches (_PipelineCache), downloads, temporary data and trophies. This folder unless -DataDir names another (a
# relative one is this shell's). The logs go to its "logs" folder; in this folder, to a build tree's _Build\run-logs.
if (!$DataDir) { $DataDir = $PSScriptRoot }
$DataDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DataDir)
if ($DataDir.Length -gt 3) { $DataDir = $DataDir.TrimEnd('\') }
$ownFolder = $DataDir -eq $PSScriptRoot.TrimEnd('\')
$logDir = if (!$ownFolder) { Join-Path $DataDir 'logs' } elseif (Test-Path "$PSScriptRoot\_Build") { "$PSScriptRoot\_Build\run-logs" } else { "$PSScriptRoot\logs" }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'

$launch = Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json

# Emulator options: everything after the binary in the config's command, minus --game.
$command = @($launch.command)
$start = [Array]::IndexOf($command, '--') + 2
$options = New-Object System.Collections.Generic.List[string]
for ($i = $start; $i -lt $command.Count; $i++) {
	if ($command[$i] -eq '--game') { $i++; continue }
	$options.Add($command[$i])
}
function Set-Option([string]$name, [string]$value) {
	$at = $options.IndexOf($name)
	if ($at -ge 0) { $options[$at + 1] = $value } else { $options.Add($name); $options.Add($value) }
}
function Add-Flag([string]$name) {
	if (!$options.Contains($name)) { $options.Add($name) }
}
if ($Width -gt 0) { Set-Option '--screen-width' "$Width" }
if ($Height -gt 0) { Set-Option '--screen-height' "$Height" }
# (Not for the precompile pass: its window shows nothing and would cover the screen, this console with it.)
if ($Precompile) { [void]$options.Remove('--fullscreen') } elseif ($Fullscreen) { Add-Flag '--fullscreen' }
if ($Stretch) { Add-Flag '--stretch' }
# -RenderScale: --unreal-render-scale (kernel/unrealCvars.h), or the config's without it; 100 takes the config's out.
if ($RenderScale -gt 0) {
	$at = $options.IndexOf('--unreal-render-scale')
	if ($at -ge 0) { $options.RemoveRange($at, 2) }
	if ($RenderScale -lt 10) { throw '-RenderScale takes 10 to 200 (percent)' }
	if ($RenderScale -ne 100) { Set-Option '--unreal-render-scale' "$RenderScale" }
}
# -UnrealCvar: one --unreal-cvar each, after the config's own (the later one of a name wins); none: no config's.
if ($UnrealCvar.Count -gt 0) {
	$pairs = @($UnrealCvar | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
	if ($pairs -contains 'none') {
		for ($at = $options.IndexOf('--unreal-cvar'); $at -ge 0; $at = $options.IndexOf('--unreal-cvar')) { $options.RemoveRange($at, 2) }
	}
	foreach ($pair in $pairs) {
		if ($pair -eq 'none') { continue }
		if ($pair -notmatch '^[A-Za-z][A-Za-z0-9_.]*=[x*]?-?[0-9.]+$') { throw "-UnrealCvar takes name=number or name=x<factor>, not '$pair'" }
		$options.Add('--unreal-cvar'); $options.Add($pair)
	}
}
if ($Hdr) { Add-Flag '--hdr' }
if ($PresentMode) { Set-Option '--present-mode' $PresentMode }
if ($Language -ge 0) { Set-Option '--console-language' "$Language" }
# -Gpu: Vulkan's physical device of that number (the emulator falls back to its own choice when there is none).
# The driver's pipeline cache is the card's: a precompile pass is for the card it ran with.
if ($Gpu -ge 0) { Set-Option '--gpu' "$Gpu" }
if ($PerfOverlay) { Add-Flag '--perf-overlay' }
# Windows writes exception frames directly below the guest's stack pointer, over its SysV red zone. The emulator
# always runs the functions that keep data there from a copy; --redzone also patches single instructions of the
# functions it could not copy (src/loader/guestInstructionPatcher.h).
if ($RedZone) { Add-Flag '--redzone' }
if ($SkipNotice) { Add-Flag '--skip-notice-screen' }
# -PrecompilePipelines on|off: whether the pipelines earlier sessions recorded (and those of compute shaders the game
# created but never dispatched) are built in the background while the game runs. On is the emulator's default; the
# choice is passed either way, in place of the config's: --precompile-pipelines, which every build takes, or
# --no-precompile-pipelines (an emulator from before 2026-11-15 does not know it and ends with its option list).
if ($PrecompilePipelines) {
	[void]$options.Remove('--precompile-pipelines')
	[void]$options.Remove('--no-precompile-pipelines')
	$options.Add($(if ($PrecompilePipelines -eq 'on') { '--precompile-pipelines' } else { '--no-precompile-pipelines' }))
}
# -Precompile: every recorded compute program and graphics pipeline, and the compute shaders the game created, built
# into the driver cache; the emulator saves it and exits (0: done, 1: the cache could not be saved, 2: stopped). The
# game does not start.
if ($Precompile) { Add-Flag '--precompile-only' }
# -ImportShaders: the shaders of the game's own files (--import-shaders, taken by the precompile pass only): a file,
# or a folder with <title id>.seeds (written there by the pass from the game's files when it is missing or the game
# has changed). Without it the shader-seeds folder next to this script, when there is one.
if ($Precompile) {
	if ($ImportShaders -eq 'none') {
		$at = $options.IndexOf('--import-shaders')
		if ($at -ge 0) { $options.RemoveRange($at, 2) }
	} elseif ($ImportShaders) {
		Set-Option '--import-shaders' ($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ImportShaders))
	} elseif ($options.IndexOf('--import-shaders') -lt 0 -and (Test-Path -LiteralPath "$PSScriptRoot\shader-seeds" -PathType Container)) {
		Set-Option '--import-shaders' "$PSScriptRoot\shader-seeds"
	}
}
# -SkipPendingDraws on|off: --skip-pending-draws, or the config's without it.
if ($SkipPendingDraws) {
	[void]$options.Remove('--skip-pending-draws')
	if ($SkipPendingDraws -eq 'on') { $options.Add('--skip-pending-draws') }
}
# -PipelineList: a shared list of recorded programs and pipelines (--import-pipeline-list). It names shaders by
# content and carries none: an entry is built once the emulator has its shaders' code, from what the game created in
# any session or from shader binaries imported with --import-shaders. -ExportPipelineList writes this data folder's
# recordings as such a list and exits (--export-pipeline-list).
# Without -PipelineList, the lists that ship next to this script (pipeline-lists\<title id>.pipelinelist) are offered:
# the emulator takes the list of the game it runs, when the folder has one. -PipelineList none: no list at all.
if ($PipelineList -eq 'none') {
	$at = $options.IndexOf('--import-pipeline-list')
	if ($at -ge 0) { $options.RemoveRange($at, 2) }
} elseif ($PipelineList) {
	Set-Option '--import-pipeline-list' ($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PipelineList))
} elseif ($options.IndexOf('--import-pipeline-list') -lt 0 -and (Test-Path -LiteralPath "$PSScriptRoot\pipeline-lists" -PathType Container)) {
	Set-Option '--import-pipeline-list' "$PSScriptRoot\pipeline-lists"
}
if ($ExportPipelineList) {
	Set-Option '--export-pipeline-list' ($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ExportPipelineList))
}
# -ShaderPairs: which vertex and pixel shaders of the game's files are drawn together (--shader-pairs), each pair
# with a recorded pipeline of the same kind. The precompile pass builds the pipelines of those pairs, so that a
# material no session has shown is ready too; a session needs the list to take them. Without -ShaderPairs, the lists
# next to this script (shader-pairs\<title id>.pairs) are offered. -ShaderPairs none: no list at all.
if ($ShaderPairs -eq 'none') {
	$at = $options.IndexOf('--shader-pairs')
	if ($at -ge 0) { $options.RemoveRange($at, 2) }
} elseif ($ShaderPairs) {
	Set-Option '--shader-pairs' ($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ShaderPairs))
} elseif ($options.IndexOf('--shader-pairs') -lt 0 -and (Test-Path -LiteralPath "$PSScriptRoot\shader-pairs" -PathType Container)) {
	Set-Option '--shader-pairs' "$PSScriptRoot\shader-pairs"
}
# -Log: the emulator's own log (--printf-direction). What the pipeline cache and the precompile report reaches the
# run log (the emulator's console output, logs\<time>.out.log) in every mode.
switch ($Log) {
	'off' { Set-Option '--printf-direction' 'Silent' }
	'file' { Set-Option '--printf-direction' 'File'; Set-Option '--printf-output-file' "$logDir\$stamp.log" }
	'console' { Set-Option '--printf-direction' 'Console' }
}

# Environment switches: the config's, then this script's, then -Set's.
$environment = [ordered]@{}
foreach ($property in $launch.environment.PSObject.Properties) {
	$environment[$property.Name] = [string]$property.Value
}
# -NoSound: SDL opens its dummy audio driver instead of the default playback device (src/libs/audio.cpp), which
# plays nothing: the game's sound, the trophy sound, and a wired DualSense's haptics, which are sent as audio.
if ($NoSound) { $environment['SDL_AUDIO_DRIVER'] = 'dummy' }
# -FrameGen: DLSS frame generation through NVIDIA's Streamline runtime (KYTY_FRAMEGEN: generated frames per rendered
# one; src/graphics/host_gpu/renderer/upscaler/frameGeneration.h). The emulator loads only NVIDIA-signed files.
if ($FrameGen -gt 0) { $environment['KYTY_FRAMEGEN'] = [string]$FrameGen } elseif ($FrameGen -eq 0) { $environment.Remove('KYTY_FRAMEGEN') }
if ($StreamlineDir) { $environment['KYTY_STREAMLINE_DIR'] = $StreamlineDir }
# -Set KEY=VALUE overrides a switch of the config; KEY= removes it (from what this shell hands down too). Several:
# -Set A=1,B=2 (a comma starts a new pair only before KEY=, so values such as 1,2,3,6,7 stay whole).
$removed = @()
foreach ($pair in ($Set | ForEach-Object { $_ -split ',(?=[A-Za-z_][A-Za-z0-9_]*=)' })) {
	if (!$pair) { continue }
	if ($pair -notmatch '^[A-Za-z_][A-Za-z0-9_]*=') { throw "-Set takes KEY=VALUE, not '$pair'" }
	$key, $value = $pair -split '=', 2
	if ($value) { $environment[$key] = $value } else { $environment.Remove($key); $removed += $key }
}

# CPU affinity: -Affinity, else the config's list, else every CPU; only CPUs this PC has.
$all = if ([Environment]::ProcessorCount -ge 64) { [int64]-1 } else { ([int64]1 -shl [Environment]::ProcessorCount) - 1 }
$mask = [int64]0
if ($Affinity) { $mask = [Convert]::ToInt64(($Affinity -replace '^0x'), 16) } else { foreach ($cpu in $launch.cpu_affinity) { $mask = $mask -bor ([int64]1 -shl [int]$cpu) } }
$mask = $mask -band $all
if ($mask -eq 0) { $mask = $all }
$cpus = 0
for ($bit = 0; $bit -lt 64; $bit++) { if ($mask -band ([int64]1 -shl $bit)) { $cpus++ } }

# (-ExtraArgs as typed, after everything else: the emulator takes the last value of an option given twice.)
$quoted = @('--game', (Format-Argument $Game)) + @($options | ForEach-Object { Format-Argument $_ })
if ($ExtraArgs.Trim()) { $quoted += $ExtraArgs.Trim() }
Write-Host "game:     $about"
Write-Host "config:   $Config$(if ($Precompile) { ' (precompile)' })"
Write-Host "data:     $DataDir; logs: $logDir"
Write-Host ("affinity: 0x{0:X} ({1} CPUs)" -f $mask, $cpus)
Write-Host "switches: $($environment.Count)"
Write-Host "command:  $Exe $($quoted -join ' ')"
if ($DryRun) {
	$environment.GetEnumerator() | ForEach-Object { "  $($_.Key)=$($_.Value)" }
	$removed | ForEach-Object { "  $_ (removed)" }
	return
}

New-Item -ItemType Directory -Force $DataDir | Out-Null
New-Item -ItemType Directory -Force $logDir | Out-Null
$out = "$logDir\$stamp.out.log"
# The switches for the emulator only: this shell's environment is as before once it has started.
$before = @{}
foreach ($key in @($environment.Keys) + $removed) { $before[$key] = [Environment]::GetEnvironmentVariable($key) }
try {
	foreach ($entry in $environment.GetEnumerator()) { Set-Item "env:$($entry.Key)" $entry.Value }
	foreach ($key in $removed) { Remove-Item "env:$key" -ErrorAction SilentlyContinue }
	$process = Start-Process -FilePath $Exe -ArgumentList $quoted -WorkingDirectory $DataDir -PassThru `
		-RedirectStandardOutput $out -RedirectStandardError "$logDir\$stamp.err.log"
} finally {
	foreach ($key in $before.Keys) { [Environment]::SetEnvironmentVariable($key, $before[$key]) }
}
if ($mask -ne $all) { $process.ProcessorAffinity = [IntPtr]$mask }
$null = $process.Handle # keeps the exit code readable after the process ends
Write-Host "pid $($process.Id); logs: $logDir\$stamp.*.log"
if (!$Precompile -and !$Follow) { return }

# The run log in this window as it is written, until the emulator exits. Of a precompile pass only its own lines
# (the pipeline cache's and the precompile's progress); its last "Precompile done:" line is the summary ("stopped"
# when its window was closed; a pass that starts over with an empty driver cache logs more than one).
$begin = Get-Date
$reader = $null
$pending = ''
$summary = ''
while ($true) {
	$exited = $process.HasExited
	if (!$reader -and (Test-Path -LiteralPath $out)) {
		$reader = New-Object System.IO.StreamReader([System.IO.FileStream]::new($out, 'Open', 'Read', 'ReadWrite'))
	}
	$text = if ($reader) { $reader.ReadToEnd() } else { '' }
	# (A last line without its line end is shown too.)
	if ($exited -and ($pending + $text) -and !$text.EndsWith("`n")) { $text += "`n" }
	if ($text) {
		$lines = ($pending + $text) -split "`n"
		$pending = $lines[-1]
		for ($i = 0; $i -lt $lines.Count - 1; $i++) {
			$line = $lines[$i].TrimEnd("`r")
			if ($line -match '^Precompile (done|stopped):') { $summary = $line }
			if (!$Precompile -or $line -match 'Precompile|pipeline cache|Title ID') { Write-Host $line }
		}
	}
	if ($exited) { break }
	Start-Sleep -Milliseconds 250
}
if ($reader) { $reader.Dispose() }
Write-Host ''
if (!$Precompile) {
	Write-Host ("The game exited with code {0} (0x{0:X8}); logs: {1}\{2}.*.log" -f $process.ExitCode, $logDir, $stamp)
	if ($process.ExitCode -ne 0) {
		Get-Content -LiteralPath "$logDir\$stamp.err.log" -Tail 20 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
	}
	exit $process.ExitCode
}

# Precompile: the summary and the time the pass took.
$elapsed = (Get-Date) - $begin
$time = if ($elapsed.TotalSeconds -ge 60) { '{0} min {1} s' -f [math]::Floor($elapsed.TotalMinutes), $elapsed.Seconds } else { '{0:N0} s' -f $elapsed.TotalSeconds }
if (!$summary) { $summary = "The precompile logged no 'Precompile done:' line (nothing recorded for this game yet, or it did not start)." }
$result = "Finished in $time with exit code $($process.ExitCode); logs: $logDir\$stamp.*.log"
Write-Host $summary
Write-Host $result
if ($process.ExitCode -ne 0) {
	Get-Content -LiteralPath "$logDir\$stamp.err.log" -Tail 20 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
}
if ($ResultFile) {
	New-Item -ItemType Directory -Force (Split-Path $ResultFile) | Out-Null
	Set-Content -LiteralPath $ResultFile -Value @($summary, $result) -Encoding UTF8
}
exit $process.ExitCode
