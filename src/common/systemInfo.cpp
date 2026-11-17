#include "common/systemInfo.h"

#include "common/assert.h"
#include "common/common.h"
#include "cpuinfo.h"

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

} // namespace Common
