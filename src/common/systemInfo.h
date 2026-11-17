#ifndef KYTY_COMMON_SYSTEM_INFO_H_
#define KYTY_COMMON_SYSTEM_INFO_H_

#include <cstdint>
#include <string>

namespace Common {

struct SystemInfo {
	std::string ProcessorName;
};

[[nodiscard]] SystemInfo GetSystemInfo();
// The PC's RAM in MiB; 0 when the platform does not say.
[[nodiscard]] uint64_t PhysicalMemoryMiB();

} // namespace Common

#endif /* KYTY_COMMON_SYSTEM_INFO_H_ */
