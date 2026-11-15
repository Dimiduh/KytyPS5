#ifndef KYTY_COMMON_LIVE_CONTROL_H_
#define KYTY_COMMON_LIVE_CONTROL_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

// A measurement channel for one boot and many same-process experiments. With KYTY_LIVE_FILE set
// to a command file, a side thread reads the file every 100 ms and runs its commands once per new
// "id <n>" first line, one command per following line, in order:
//   measure <seconds> [label]      frames, frame times and per-frame work of the render threads
//   prof <seconds> [thread] [top]  wall-time samples of a registered thread (default gpu), by function
//   prof <seconds> <thread> <top> <text>   the callers of the functions whose name contains <text>
//   set <switch> <value>           a live switch (Switch below); `switches` lists them
//   pad <button> [ms]              press and release a controller button (cross, circle, up, ...)
//   stick <lx> <ly> <rx> <ry> <ms> hold the sticks (0..255, 128 centred), then centre them
//   sleep <ms>                     wait before the next command
//   threads                        the registered threads and their CPU time
//   quit                           close the emulator window
// Answers go to the log and, one line each, to <command file>.out. Without KYTY_LIVE_FILE nothing
// runs: the flip hook is one relaxed load, a switch read is one relaxed load.
namespace Common::LiveControl {

// A named run-time switch of a code path, for A/B measurements in one process. It starts from
// the environment variable KYTY_<NAME> (the name in upper case) when that is set, else from the
// default, and `set <name> <value>` changes it while the game runs. Define switches at namespace
// scope: they register themselves before main.
class Switch {
public:
	Switch(const char* name, uint32_t default_value) noexcept;
	Switch(const Switch&)            = delete;
	Switch& operator=(const Switch&) = delete;

	[[nodiscard]] uint32_t Get() const noexcept { return m_value.load(std::memory_order_relaxed); }
	[[nodiscard]] bool     On() const noexcept { return Get() != 0; }
	void Set(uint32_t value) noexcept { m_value.store(value, std::memory_order_relaxed); }

	[[nodiscard]] const char*   Name() const noexcept { return m_name; }
	[[nodiscard]] const Switch* Next() const noexcept { return m_next; }

private:
	const char*           m_name;
	std::atomic<uint32_t> m_value;
	Switch*               m_next;
};

// The host actions the commands need, set by the emulator before Start.
struct HostHooks {
	void (*pad_button)(uint32_t button, bool down)             = nullptr;
	void (*pad_sticks)(int left_x, int left_y, int right_x, int right_y) = nullptr;
	void (*quit)()                                              = nullptr;
};
void SetHostHooks(const HostHooks& hooks) noexcept;

// Starts the command thread when KYTY_LIVE_FILE names a command file.
void Start();

// Names the calling thread for `prof`, `threads` and the per-thread CPU time of `measure`
// ("gpu" is the thread that translates guest commands).
void RegisterThread(const char* name) noexcept;

namespace Detail {
inline constexpr size_t      FLIP_TIMES = 8192;
inline std::atomic<bool>     g_active {false};
inline std::atomic<uint64_t> g_flips {0};
inline std::atomic<int64_t>  g_flip_times_ns[FLIP_TIMES] {};
} // namespace Detail

[[nodiscard]] inline bool Active() noexcept {
	return Detail::g_active.load(std::memory_order_relaxed);
}

// A guest flip on the main bus (one caller thread).
inline void NoteFlip() noexcept {
	if (!Active()) {
		return;
	}
	const auto flip = Detail::g_flips.load(std::memory_order_relaxed);
	Detail::g_flip_times_ns[flip % Detail::FLIP_TIMES].store(
	    std::chrono::duration_cast<std::chrono::nanoseconds>(
	        std::chrono::steady_clock::now().time_since_epoch())
	        .count(),
	    std::memory_order_relaxed);
	Detail::g_flips.store(flip + 1, std::memory_order_release);
}

} // namespace Common::LiveControl

#endif /* KYTY_COMMON_LIVE_CONTROL_H_ */
