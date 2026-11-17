#include "common/systemInfo.h"

#include "common/assert.h"
#include "common/common.h"
#include "cpuinfo.h"

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#include <windows.h> // IWYU pragma: keep
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
