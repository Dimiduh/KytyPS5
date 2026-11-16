@echo off
rem Builds KytyLauncher.exe into _Build\launcher with Visual Studio's MSBuild (any edition or the Build Tools with the
rem .NET desktop workload; .NET Framework 4.7.2 targeting pack). launcher.cmd starts the exe when it is there.
rem More MSBuild arguments are passed on. Without the targeting pack MSBuild stops with error MSB3644; the installed
rem .NET Framework's own assemblies can then stand in for it (warning MSB3270 comes with that):
rem   build.cmd /p:FrameworkPathOverride=C:\Windows\Microsoft.NET\Framework64\v4.0.30319
setlocal
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" (
	echo vswhere.exe not found: install Visual Studio or its Build Tools
	exit /b 1
)
for /f "usebackq delims=" %%i in (`"%vswhere%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "msbuild=%%i"
if not defined msbuild (
	echo MSBuild not found
	exit /b 1
)
"%msbuild%" "%~dp0KytyLauncher.csproj" /nologo /v:minimal /p:Configuration=Release %*
exit /b %errorlevel%
