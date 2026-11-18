#ifndef EMULATOR_SRC_COMMON_DRIVERADDRESSSPACE_H_
#define EMULATOR_SRC_COMMON_DRIVERADDRESSSPACE_H_

#include <cstdint>

// Where the guest's address space ends and the room left for the graphics driver below 1 TB
// begins. The NVIDIA driver makes the device DLSS and frame generation ask for only while it
// finds address space there: on an RTX 4090 the call reserves one block of 8.5 GB at an address
// that is a multiple of 4 GB, and about 0.35 GB more. The guest's reservation ends at
// 0xfc00000000, which leaves 16 GB; with DLSS or frame generation asked for it ends lower, so
// that a driver or a card that wants more finds it (a player's RTX 5070 was refused the device
// with the 16 GB). The game maps nothing that high: its highest addresses are near 0xd000000000.
// The value is set once, from the process' first moment, before the guest's memory is set up
// (FrameGen::HoldDriverAddressSpace), and read by the kernel's memory code.
namespace Common::DriverAddressSpace {

constexpr uint64_t End         = 0x10000000000ull; // 1 TB
constexpr uint64_t DefaultBase = 0xfc00000000ull;  // 16 GB below it
constexpr uint64_t DefaultRoomGiB = 64;

inline uint64_t& BaseValue() noexcept {
	static uint64_t base = DefaultBase;
	return base;
}

[[nodiscard]] inline uint64_t Base() noexcept {
	return BaseValue();
}

// Room of `gib` GB (a multiple of 4, from 16 to 128) below 1 TB.
inline void SetRoomGiB(uint64_t gib) noexcept {
	gib         = gib < 16 ? 16 : gib > 128 ? 128 : gib & ~uint64_t {3};
	BaseValue() = End - (gib << 30u);
}

} // namespace Common::DriverAddressSpace

#endif // EMULATOR_SRC_COMMON_DRIVERADDRESSSPACE_H_
