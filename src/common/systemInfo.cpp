#include "common/systemInfo.h"

#include "common/assert.h"
#include "common/common.h"
#include "cpuinfo.h"

#include <cstdlib>

#include <fmt/format.h>

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#include <windows.h> // IWYU pragma: keep
// (After windows.h.)
#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace Common {

SystemInfo GetSystemInfo() {
	static const bool initialized = cpuinfo_initialize();
	EXIT_IF(!initialized);

	const auto* package = cpuinfo_get_package(0);
	EXIT_IF(package == nullptr);

	return {package->name};
}

std::string HostMemoryText() {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	MEMORYSTATUSEX status {};
	status.dwLength = sizeof(status);
	if (GlobalMemoryStatusEx(&status) == 0) {
		return {};
	}
	constexpr double        GiB = 1024.0 * 1024.0 * 1024.0;
	PROCESS_MEMORY_COUNTERS_EX counters {};
	counters.cb = sizeof(counters);
	const bool own = GetProcessMemoryInfo(GetCurrentProcess(),
	                                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
	                                      sizeof(counters)) != 0;
	return fmt::format("{:.1f} GB of RAM, {:.1f} GB free; {:.1f} GB can still be committed (RAM and page "
	                   "file); this process holds {:.1f} GB",
	                   static_cast<double>(status.ullTotalPhys) / GiB,
	                   static_cast<double>(status.ullAvailPhys) / GiB,
	                   static_cast<double>(status.ullAvailPageFile) / GiB,
	                   own ? static_cast<double>(counters.PrivateUsage) / GiB : 0.0);
#else
	const auto total = PhysicalMemoryMiB();
	return total != 0 ? fmt::format("{:.1f} GB of RAM", static_cast<double>(total) / 1024.0) : std::string {};
#endif
}

uint64_t PhysicalMemoryMiB() {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	MEMORYSTATUSEX status {};
	status.dwLength = sizeof(status);
	return GlobalMemoryStatusEx(&status) != 0 ? status.ullTotalPhys >> 20u : 0;
#else
	const auto pages = sysconf(_SC_PHYS_PAGES);
	const auto size  = sysconf(_SC_PAGE_SIZE);
	return pages > 0 && size > 0 ? (static_cast<uint64_t>(pages) * static_cast<uint64_t>(size)) >> 20u : 0;
#endif
}

uint64_t CommitAvailableMiB() {
	// (KYTY_COMMIT_AVAILABLE_MIB stands in for the reading: what a full PC does is tested on
	// one that is not.)
	if (const char* given = std::getenv("KYTY_COMMIT_AVAILABLE_MIB"); given != nullptr && given[0] != '\0') {
		return std::strtoull(given, nullptr, 10);
	}
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	MEMORYSTATUSEX status {};
	status.dwLength = sizeof(status);
	return GlobalMemoryStatusEx(&status) != 0 ? status.ullAvailPageFile >> 20u : 0;
#else
	return 0;
#endif
}

std::string LowCommitText(uint64_t needed_mib) {
	const auto available = CommitAvailableMiB();
	if (available == 0 || available >= needed_mib) {
		return {};
	}
	return fmt::format("Windows can give out only {:.1f} GB more memory now (RAM and page file together, less "
	                   "what the other programs hold). Close other programs (web browsers hold the most) or "
	                   "enlarge the page file",
	                   static_cast<double>(available) / 1024.0);
}

} // namespace Common
