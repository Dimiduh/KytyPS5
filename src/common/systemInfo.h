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
// How much more memory the system can commit now (RAM and page file together, less what every
// program has committed), in MiB; 0 when the platform does not say. On a PC with much RAM this
// can be the smaller number by far: a web browser with many tabs commits tens of GB.
[[nodiscard]] uint64_t CommitAvailableMiB();
// A sentence for the user when the system can commit less than `needed_mib` more; empty when
// there is room or the platform does not say.
[[nodiscard]] std::string LowCommitText(uint64_t needed_mib);
// The PC's memory now, for the log: RAM and how much of it is free, how much more memory the
// system can still commit (RAM and page file), and what this process has committed. Empty
// where the platform does not say.
[[nodiscard]] std::string HostMemoryText();

} // namespace Common

#endif /* KYTY_COMMON_SYSTEM_INFO_H_ */
