@echo off
rem Windows build: clang-cl + Ninja in the Visual Studio 2022 x64 environment, without the Qt launcher.
rem   build-windows.cmd            configure (first time) and build kyty_emulator
rem   build-windows.cmd <target>   build another target (for example kyty_tests or all)
rem Needs Git, Visual Studio 2022 or its Build Tools with the C++ workload (CMake and Ninja come with
rem it), LLVM clang-cl (19.1.7 tested; else the VS "C++ Clang tools" component) and the Vulkan SDK
rem (glslangValidator). Missing submodules are checked out first.
rem Release builds use ThinLTO. Unless KYTY_CMAKE_ARGS says otherwise they also use -O3, this CPU's
rem instruction set (-march=native: the exe may not start on another CPU) and profiling symbols.
rem   KYTY_BUILD_DIR    build directory (default _Build\windows)
rem   KYTY_CMAKE_ARGS   arguments for the first configure in place of the defaults above, e.g.
rem                     "-DKYTY_NATIVE_OPT=ON -DKYTY_TARGET_ARCH=x86-64-v3" (an exe other PCs with
rem                     AVX2 can run), -DKYTY_PGO_GENERATE=ON, -DKYTY_PGO_USE=<file.profdata>;
rem                     a single space (set "KYTY_CMAKE_ARGS= ") for the plain Release build
setlocal
set "ROOT=%~dp0"
set "BUILD=%ROOT%_Build\windows"
if defined KYTY_BUILD_DIR set "BUILD=%KYTY_BUILD_DIR%"
if not defined KYTY_CMAKE_ARGS set "KYTY_CMAKE_ARGS=-DKYTY_NATIVE_OPT=ON -DKYTY_TARGET_ARCH=native -DKYTY_PROFILING_SYMBOLS=ON"

if not exist "%ROOT%.git" (
	echo No Git checkout: clone the repository with git ^(a source archive has no submodules^).
	exit /b 1
)
where git >nul 2>nul || (
	echo git not found: install Git ^(winget install Git.Git^).
	exit /b 1
)
rem The submodules a clone without --recurse-submodules lacks (only those: one checked out at another
rem commit on purpose stays as it is).
set "MISSING="
for /f "tokens=2" %%s in ('git -C "%ROOT%." submodule status ^| findstr /b /c:"-"') do call set "MISSING=%%MISSING%% %%s"
if defined MISSING (
	echo Checking out submodules:%MISSING%
	git -C "%ROOT%." submodule update --init --recursive --%MISSING% || exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
	echo Visual Studio 2022 or Build Tools 2022 with the "Desktop development with C++" workload not found.
	exit /b 1
)
rem (vcvars64.bat runs vswhere by name.)
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
where cmake >nul 2>nul || (
	echo cmake not found: add the VS component "C++ CMake tools for Windows" or winget install Kitware.CMake.
	exit /b 1
)
where ninja >nul 2>nul || (
	echo ninja not found: add the VS component "C++ CMake tools for Windows" or winget install Ninja-build.Ninja.
	exit /b 1
)

rem clang-cl: a standalone LLVM first, the VS component otherwise.
set "LLVM_BIN="
if exist "%ProgramFiles%\LLVM\bin\clang-cl.exe" set "LLVM_BIN=%ProgramFiles%\LLVM\bin"
if not defined LLVM_BIN if exist "%VSDIR%\VC\Tools\Llvm\x64\bin\clang-cl.exe" set "LLVM_BIN=%VSDIR%\VC\Tools\Llvm\x64\bin"
if defined LLVM_BIN set "PATH=%LLVM_BIN%;%PATH%"
where clang-cl >nul 2>nul || (
	echo clang-cl not found: install LLVM ^(winget install LLVM.LLVM --version 19.1.7^).
	exit /b 1
)

rem glslangValidator: the Vulkan SDK (winget install KhronosGroup.VulkanSDK).
if not defined VULKAN_SDK for /d %%d in ("%SystemDrive%\VulkanSDK\*") do set "VULKAN_SDK=%%d"
if defined VULKAN_SDK set "PATH=%VULKAN_SDK%\Bin;%PATH%"
where glslangValidator >nul 2>nul || (
	echo glslangValidator not found: install the Vulkan SDK ^(winget install KhronosGroup.VulkanSDK^).
	exit /b 1
)

if not exist "%BUILD%\build.ninja" (
	cmake -S "%ROOT%." -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
		-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DKYTY_BUILD_LAUNCHER=OFF ^
		-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON %KYTY_CMAKE_ARGS% || exit /b 1
)

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=kyty_emulator"
cmake --build "%BUILD%" --target %TARGET% || exit /b 1
exit /b 0
