#include "common/assert.h"

#include "common/subsystems.h"
#include "kytyGitVersion.h"

#include <cstdio>
#include <cstdlib>
#include <fmt/format.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Common {

void FatalExit(int status) noexcept {
#if defined(_WIN32)
	// Not ExitProcess (std::_Exit): it kills the other threads wherever they are and then runs
	// the DLLs' detach routines on this one, where one may wait forever on what a killed thread
	// held. The crashed emulator then stays a one-thread process that nothing can end, holding
	// its memory and its files.
	TerminateProcess(GetCurrentProcess(), static_cast<UINT>(status));
#endif
	std::_Exit(status);
}

static thread_local uint32_t g_recoverable_fatal_depth = 0;

RecoverableFatalScope::RecoverableFatalScope() {
	g_recoverable_fatal_depth++;
}

RecoverableFatalScope::~RecoverableFatalScope() {
	g_recoverable_fatal_depth--;
}

[[noreturn]] static void DbgReport(const char* title, std::string_view text, const char* file,
                                   int line) {
	if (g_recoverable_fatal_depth != 0) {
		throw RecoverableFatal {fmt::format("{} in {}:{}", text, file, line)};
	}
	Log::WriteToConsoleAndLog(fmt::format("--- Build ---\n{}\n{}\n{} in {}:{}\n", KYTY_BUILD_LABEL,
	                                      title, text, file, line));
	Subsystems::EmergencyShutdownActive();
	std::fflush(nullptr);
	FatalExit(321);
}

void DbgExitIfHandler(const char* expr, const char* file, int line) {
	DbgReport("--- Fatal Error ---", fmt::format("Error: condition ({}) is true", expr), file,
	          line);
}

void DbgNotImplementedHandler(const char* expr, const char* file, int line) {
	DbgReport("--- Fatal Error ---", fmt::format("Not implemented ({})", expr), file, line);
}

void DbgExitHandler(const char* file, int line, std::string_view text) {
	DbgReport("--- Error ---", text, file, line);
}

} // namespace Common
