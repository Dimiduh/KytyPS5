#include "common/assert.h"

#include "common/subsystems.h"
#include "common/systemInfo.h"
#include "kytyGitVersion.h"

#include <atomic>
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

// With less than this left to commit when a fatal error comes, the error is called a likely
// result of it.
constexpr uint64_t OutOfMemoryMiB = 2048;

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

static std::atomic<bool> g_exit_without_detach {false};
static std::atomic<int>  g_quick_exit_status {0};

void RegisterQuickExit() {
	(void)at_quick_exit([] {
		if (g_exit_without_detach.load(std::memory_order_relaxed)) {
			std::fflush(nullptr);
			FatalExit(g_quick_exit_status.load(std::memory_order_relaxed));
		}
	});
}

void ExitWithoutDetach() noexcept {
	g_exit_without_detach.store(true, std::memory_order_relaxed);
}

void QuickExit(int status) {
	g_quick_exit_status.store(status, std::memory_order_relaxed);
	std::quick_exit(status);
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
	// Out of memory shows as many things (a lost graphics device, a null pointer in the guest).
	if (const auto memory = HostMemoryText(); !memory.empty()) {
		Log::WriteToConsoleAndLog(fmt::format("--- Memory ---\n{}\n", memory));
	}
	// (Seen here: a lost graphics device three minutes into a session, with 0.6 GB left to
	// commit while a web browser held 36 GB.)
	if (const auto low = LowCommitText(OutOfMemoryMiB); !low.empty()) {
		Log::WriteToConsoleAndLog(fmt::format("Windows was out of memory when this happened: the likely reason. {}.\n", low));
	}
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
