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
// The PC's memory now, for the log: RAM and how much of it is free, how much more memory the
// system can still commit (RAM and page file), and what this process has committed. Empty
// where the platform does not say.
[[nodiscard]] std::string HostMemoryText();

} // namespace Common

#endif /* KYTY_COMMON_SYSTEM_INFO_H_ */
