#include "common/liveControl.h"

#include "common/config.h"
#include "common/debugCounters.h"
#include "common/gpuTiming.h"
#include "common/logging/log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#include <windows.h> // IWYU pragma: keep
// clang-format off
#include <dbghelp.h>
#include <psapi.h>
// clang-format on
#endif

namespace Common::LiveControl {

namespace {

// Heads of the lists the switches' and counters' constructors build (before main, one thread).
Switch*  g_switches = nullptr;
Counter* g_counters = nullptr;

HostHooks g_hooks;

struct NamedThread {
	std::string name;
	uint64_t    handle     = 0; // A thread HANDLE on Windows.
	uint32_t    tid        = 0;
	uint64_t    stack_low  = 0;
	uint64_t    stack_high = 0;
};

std::mutex               g_threads_mutex;
std::vector<NamedThread> g_threads;

std::string g_command_path;
std::string g_answer_path;

int64_t NowNs() noexcept {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
	           std::chrono::steady_clock::now().time_since_epoch())
	    .count();
}

void Answer(const std::string& line) {
	LOGF("%s\n", line.c_str());
	if (auto* out = std::fopen(g_answer_path.c_str(), "ab")) {
		std::fwrite(line.data(), 1, line.size(), out);
		std::fputc('\n', out);
		std::fclose(out);
	}
}

template <typename... Args>
std::string Format(const char* format, Args... args) {
	const int size = std::snprintf(nullptr, 0, format, args...);
	if (size <= 0) {
		return {};
	}
	std::string text(static_cast<size_t>(size), '\0');
	std::snprintf(text.data(), static_cast<size_t>(size) + 1, format, args...);
	return text;
}

std::vector<NamedThread> Threads() {
	std::scoped_lock lock(g_threads_mutex);
	return g_threads;
}

// CPU time a thread has used, in seconds; negative when unknown.
double ThreadCpuSeconds(const NamedThread& thread) noexcept {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	FILETIME creation {};
	FILETIME exit {};
	FILETIME kernel {};
	FILETIME user {};
	// NOLINTNEXTLINE(performance-no-int-to-ptr)
	if (GetThreadTimes(reinterpret_cast<HANDLE>(thread.handle), &creation, &exit, &kernel, &user) ==
	    0) {
		return -1.0;
	}
	const auto ticks = [](const FILETIME& time) {
		return (static_cast<uint64_t>(time.dwHighDateTime) << 32u) | time.dwLowDateTime;
	};
	return static_cast<double>(ticks(kernel) + ticks(user)) / 1e7;
#else
	(void)thread;
	return -1.0;
#endif
}

double ProcessCpuSeconds() noexcept {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	FILETIME creation {};
	FILETIME exit {};
	FILETIME kernel {};
	FILETIME user {};
	if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) == 0) {
		return -1.0;
	}
	const auto ticks = [](const FILETIME& time) {
		return (static_cast<uint64_t>(time.dwHighDateTime) << 32u) | time.dwLowDateTime;
	};
	return static_cast<double>(ticks(kernel) + ticks(user)) / 1e7;
#else
	return -1.0;
#endif
}

// ---------------------------------------------------------------------------------------------
// measure

using DebugCounter = DebugCounters::Counter;

constexpr std::array MEASURED_COUNTERS {
    std::pair {DebugCounter::Draws, "draws"},
    std::pair {DebugCounter::IndirectDraws, "indirect_draws"},
    std::pair {DebugCounter::Dispatches, "dispatches"},
    std::pair {DebugCounter::QueueSubmits, "submits"},
    std::pair {DebugCounter::GpuWaits, "gpu_waits"},
    std::pair {DebugCounter::GpuFaults, "write_faults"},
    std::pair {DebugCounter::ShadersCompiled, "shaders"},
    std::pair {DebugCounter::PipelinesCreated, "pipelines"},
};

struct Snapshot {
	int64_t                                          time_ns = 0;
	uint64_t                                         flips   = 0;
	std::array<uint64_t, MEASURED_COUNTERS.size()>   counters {};
	uint64_t                                         gpu_thread_busy_ns = 0;
	uint64_t                                         gpu_wait_ns        = 0;
	uint64_t                                         buffer_upload      = 0;
	uint64_t                                         texture_upload     = 0;
	uint64_t                                         buffer_download    = 0;
	GpuTiming::SyncCounts                            sync;
	GpuTiming::Totals                                gpu;
	double                                           process_cpu = 0.0;
	std::vector<std::pair<std::string, double>>      thread_cpu;
	std::vector<uint64_t>                            named; // The Counters, in list order.
};

Snapshot Take() {
	Snapshot snapshot;
	snapshot.time_ns = NowNs();
	snapshot.flips   = Detail::g_flips.load(std::memory_order_acquire);
	for (size_t i = 0; i < MEASURED_COUNTERS.size(); i++) {
		snapshot.counters[i] = DebugCounters::Get(MEASURED_COUNTERS[i].first);
	}
	snapshot.gpu_thread_busy_ns = DebugCounters::Get(DebugCounter::GpuThreadBusyNs);
	snapshot.gpu_wait_ns        = DebugCounters::Get(DebugCounter::GpuWaitNs);
	snapshot.buffer_upload      = DebugCounters::Get(DebugCounter::BufferUploadBytes);
	snapshot.texture_upload     = DebugCounters::Get(DebugCounter::TextureUploadBytes);
	snapshot.buffer_download    = DebugCounters::Get(DebugCounter::BufferDownloadBytes);
	snapshot.sync               = GpuTiming::Sync().Snapshot();
	snapshot.gpu                = GpuTiming::RunningTotals();
	snapshot.process_cpu        = ProcessCpuSeconds();
	for (const auto* counter = g_counters; counter != nullptr; counter = counter->Next()) {
		snapshot.named.push_back(counter->Get());
	}
	for (const auto& thread: Threads()) {
		const double seconds = ThreadCpuSeconds(thread);
		const auto   known   = std::find_if(snapshot.thread_cpu.begin(), snapshot.thread_cpu.end(),
		                                    [&](const auto& entry) { return entry.first == thread.name; });
		if (known != snapshot.thread_cpu.end()) {
			known->second += std::max(seconds, 0.0);
		} else {
			snapshot.thread_cpu.emplace_back(thread.name, std::max(seconds, 0.0));
		}
	}
	return snapshot;
}

void Measure(uint64_t id, double seconds, const std::string& label) {
	// The counters are collected only while something reads them (the overlay, or this).
	const bool counters_were_on = DebugCounters::Enabled();
	DebugCounters::SetEnabled(true);
	const auto begin = Take();
	std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
	const auto end = Take();
	DebugCounters::SetEnabled(counters_were_on);

	const double elapsed = static_cast<double>(end.time_ns - begin.time_ns) / 1e9;
	const auto   flips   = end.flips - begin.flips;
	const double frames  = static_cast<double>(std::max<uint64_t>(flips, 1));
	const double fps     = static_cast<double>(flips) / elapsed;

	// Frame times of the window from the flip time ring (the newest FLIP_TIMES flips).
	std::vector<double> frame_ms;
	const auto          first = flips + 1 > Detail::FLIP_TIMES ? end.flips - Detail::FLIP_TIMES + 1
	                                                           : begin.flips + 1;
	for (auto flip = std::max<uint64_t>(first, 1); flip < end.flips; flip++) {
		const auto earlier =
		    Detail::g_flip_times_ns[(flip - 1) % Detail::FLIP_TIMES].load(std::memory_order_relaxed);
		const auto later =
		    Detail::g_flip_times_ns[flip % Detail::FLIP_TIMES].load(std::memory_order_relaxed);
		if (later > earlier) {
			frame_ms.push_back(static_cast<double>(later - earlier) / 1e6);
		}
	}
	std::sort(frame_ms.begin(), frame_ms.end());
	const auto quantile = [&](double q) {
		if (frame_ms.empty()) {
			return 0.0;
		}
		return frame_ms[std::min(frame_ms.size() - 1,
		                         static_cast<size_t>(q * static_cast<double>(frame_ms.size())))];
	};
	// The mean of the slowest 1% of frames, as a rate.
	double low = 0.0;
	if (!frame_ms.empty()) {
		const auto count = std::max<size_t>(1, frame_ms.size() / 100);
		double     total = 0.0;
		for (size_t i = frame_ms.size() - count; i < frame_ms.size(); i++) {
			total += frame_ms[i];
		}
		low = 1000.0 * static_cast<double>(count) / total;
	}

	const auto sync       = end.sync.Since(begin.sync);
	const auto per_frame  = [&](uint64_t later, uint64_t earlier) {
		return static_cast<double>(later - earlier) / frames;
	};
	const auto ms_per_frame = [&](uint64_t later, uint64_t earlier) {
		return static_cast<double>(later - earlier) / 1e6 / frames;
	};
	const double gpu_window_s = static_cast<double>(end.gpu.window_ns - begin.gpu.window_ns) / 1e9;
	const double gpu_busy_share =
	    gpu_window_s > 0.0
	        ? static_cast<double>(end.gpu.busy_ns - begin.gpu.busy_ns) / 1e9 / gpu_window_s
	        : -1.0;

	std::string line = Format("LIVE measure id=%" PRIu64 " label=%s seconds=%.2f flips=%" PRIu64
	                          " fps=%.2f low1=%.2f frame_ms p50=%.2f p95=%.2f p99=%.2f max=%.2f",
	                          id, label.c_str(), elapsed, flips, fps, low, quantile(0.50),
	                          quantile(0.95), quantile(0.99), frame_ms.empty() ? 0.0 : frame_ms.back());
	// The GPU thread per frame: wall time inside commands and submissions (waits included), its
	// waits on the GPU, and its time without guest work (starved) or with every stream suspended.
	line += Format(" | gpu_thread_ms busy=%.2f gpu_waits=%.2f starved=%.2f blocked=%.2f",
	               ms_per_frame(end.gpu_thread_busy_ns, begin.gpu_thread_busy_ns),
	               ms_per_frame(end.gpu_wait_ns, begin.gpu_wait_ns),
	               static_cast<double>(sync.gpu_thread_starved_ns) / 1e6 / frames,
	               static_cast<double>(sync.gpu_thread_blocked_ns) / 1e6 / frames);
	if (gpu_busy_share >= 0.0) {
		line += Format(" | gpu busy=%.1f%% busy_ms=%.2f", 100.0 * gpu_busy_share,
		               1000.0 * gpu_busy_share / std::max(fps, 0.001));
	} else {
		line += " | gpu busy=n/a";
	}
	line += " | per_frame";
	for (size_t i = 0; i < MEASURED_COUNTERS.size(); i++) {
		line += Format(" %s=%.1f", MEASURED_COUNTERS[i].second,
		               per_frame(end.counters[i], begin.counters[i]));
	}
	line += Format(" buffer_upload_kb=%.0f texture_upload_kb=%.0f buffer_download_kb=%.0f",
	               per_frame(end.buffer_upload, begin.buffer_upload) / 1024.0,
	               per_frame(end.texture_upload, begin.texture_upload) / 1024.0,
	               per_frame(end.buffer_download, begin.buffer_download) / 1024.0);
	if (g_counters != nullptr) {
		line += " | counters";
		size_t index = 0;
		for (const auto* counter = g_counters; counter != nullptr; counter = counter->Next()) {
			line += Format(" %s=%.1f", counter->Name(),
			               per_frame(end.named[index], begin.named[index]));
			index++;
		}
	}
	line += " | waits_ms";
	for (size_t reason = 0; reason < GpuTiming::SYNC_REASON_COUNT; reason++) {
		if (sync.waits[reason] != 0) {
			line += Format(" %s=%.2f(%.1f)",
			               GpuTiming::SyncReasonName(static_cast<GpuTiming::SyncReason>(reason)),
			               static_cast<double>(sync.wait_ns[reason]) / 1e6 / frames,
			               static_cast<double>(sync.waits[reason]) / frames);
		}
	}
	line += " | cpu_ms";
	if (begin.process_cpu >= 0.0 && end.process_cpu >= 0.0) {
		line += Format(" process=%.2f", 1000.0 * (end.process_cpu - begin.process_cpu) / frames);
	}
	for (const auto& [name, cpu]: end.thread_cpu) {
		const auto earlier = std::find_if(begin.thread_cpu.begin(), begin.thread_cpu.end(),
		                                  [&](const auto& entry) { return entry.first == name; });
		const double base  = earlier != begin.thread_cpu.end() ? earlier->second : 0.0;
		line += Format(" %s=%.2f", name.c_str(), 1000.0 * (cpu - base) / frames);
	}
	Answer(line);
}

// ---------------------------------------------------------------------------------------------
// prof

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS

constexpr size_t SAMPLE_FRAMES = 48;

struct Sample {
	std::array<uint64_t, SAMPLE_FRAMES> frames {};
	uint32_t                            count = 0;
};

// One sample of a suspended thread: its pc and the return addresses of its callers, unwound with
// the functions' unwind data while the thread stands still (code without unwind data, generated
// or guest code, is taken as a leaf: its return address is the word at rsp). The walk stays
// inside the thread's stack. The unwind tables are read under the system's own shared lock,
// which a thread holds exclusively only while a module or a function table is added or removed.
// The walk of SampleThread, apart so that a fault in it is caught: a thread can be stopped at an
// instant its function's unwind data does not describe (a frame register that is not set up
// yet), and the system's unwinder then reads through whatever the registers hold. The frames
// found until then stand. (Such a fault ended the emulator: it reaches no handler of ours.)
void WalkSuspendedThread(CONTEXT& context, uint64_t stack_low, uint64_t stack_high,
                         Sample& sample) noexcept {
	__try {
		while (context.Rip != 0 && sample.count < SAMPLE_FRAMES) {
			sample.frames[sample.count++] = context.Rip;
			if (context.Rsp < stack_low || context.Rsp + 8 > stack_high || (context.Rsp & 7u) != 0) {
				break;
			}
			DWORD64 image_base = 0;
			auto*   function   = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
			if (function != nullptr) {
				void*   handler_data = nullptr;
				DWORD64 establisher  = 0;
				RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context,
				                 &handler_data, &establisher, nullptr);
			} else {
				// NOLINTNEXTLINE(performance-no-int-to-ptr)
				context.Rip = *reinterpret_cast<const uint64_t*>(context.Rsp);
				context.Rsp += 8;
			}
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

bool SampleThread(const NamedThread& thread, Sample& sample) noexcept {
	// NOLINTNEXTLINE(performance-no-int-to-ptr)
	auto* handle = reinterpret_cast<HANDLE>(thread.handle);
	if (SuspendThread(handle) == static_cast<DWORD>(-1)) {
		return false;
	}
	CONTEXT context {};
	context.ContextFlags = CONTEXT_FULL;
	const bool ok        = GetThreadContext(handle, &context) != 0;
	if (ok) {
		sample.count = 0;
		WalkSuspendedThread(context, thread.stack_low, thread.stack_high, sample);
	}
	ResumeThread(handle);
	return ok;
}

// The function an address is in, "<module>" when the module has no symbol for it, "<code>" for
// memory outside every module (guest and generated code).
class Symbols {
public:
	Symbols() {
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
		m_ready = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != 0;
	}

	const std::string& Name(uint64_t address) {
		if (const auto known = m_names.find(address); known != m_names.end()) {
			return known->second;
		}
		std::string name;
		if (m_ready) {
			alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512] {};
			auto*                     info = reinterpret_cast<SYMBOL_INFO*>(buffer);
			info->SizeOfStruct             = sizeof(SYMBOL_INFO);
			info->MaxNameLen               = 511;
			DWORD64 displacement           = 0;
			// A symbol far before the address is the nearest export of a module without
			// symbols (a driver): the module's name says more.
			if (SymFromAddr(GetCurrentProcess(), address, &displacement, info) != 0 &&
			    displacement < 0x4000) {
				name.assign(info->Name, info->NameLen);
			}
		}
		if (name.empty()) {
			HMODULE module = nullptr;
			char    path[MAX_PATH] {};
			// NOLINTNEXTLINE(performance-no-int-to-ptr)
			if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			                       reinterpret_cast<LPCSTR>(address), &module) != 0 &&
			    GetModuleBaseNameA(GetCurrentProcess(), module, path, MAX_PATH) != 0) {
				name = std::string("<") + path + ">";
			} else {
				name = "<code>";
			}
		}
		return m_names.emplace(address, std::move(name)).first->second;
	}

private:
	bool                                      m_ready = false;
	std::unordered_map<uint64_t, std::string> m_names;
};

// A function's name short enough for a report line.
std::string Short(const std::string& name) {
	return name.size() <= 110 ? name : name.substr(0, 107) + "...";
}

void Profile(uint64_t id, double seconds, const std::string& thread_name, size_t top,
             const std::string& focus) {
	const auto threads = Threads();
	const auto thread  = std::find_if(threads.begin(), threads.end(),
	                                  [&](const auto& entry) { return entry.name == thread_name; });
	if (thread == threads.end() || thread->handle == 0) {
		Answer(Format("LIVE error id=%" PRIu64 " prof: no thread named %s", id, thread_name.c_str()));
		return;
	}
	constexpr int64_t   PERIOD_NS = 500'000; // 2 kHz of wall time.
	std::vector<Sample> samples;
	samples.reserve(static_cast<size_t>(seconds * 2000.0) + 16);
	const auto end  = NowNs() + static_cast<int64_t>(seconds * 1e9);
	auto       next = NowNs();
	while (next < end) {
		Sample sample;
		if (SampleThread(*thread, sample)) {
			samples.push_back(sample);
		}
		next += PERIOD_NS;
		// The timer resolution is too coarse for the period: sleep while far, spin when near.
		for (auto now = NowNs(); now < next; now = NowNs()) {
			if (next - now > 2'000'000) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			} else {
				YieldProcessor();
			}
		}
	}
	if (samples.empty()) {
		Answer(Format("LIVE error id=%" PRIu64 " prof: no samples of %s", id, thread_name.c_str()));
		return;
	}

	static Symbols                            symbols;
	std::unordered_map<std::string, uint64_t> self;
	std::unordered_map<std::string, uint64_t> inclusive;
	// "under:<text>": what runs inside the outermost function whose name contains the text.
	static constexpr std::string_view Under = "under:";
	if (focus.starts_with(Under)) {
		const auto within  = focus.substr(Under.size());
		uint64_t   matched = 0;
		for (const auto& sample: samples) {
			uint32_t outer = UINT32_MAX;
			for (uint32_t i = 0; i < sample.count; i++) {
				if (symbols.Name(sample.frames[i]).find(within) != std::string::npos) {
					outer = i;
				}
			}
			if (outer == UINT32_MAX) {
				continue;
			}
			matched++;
			self[symbols.Name(sample.frames[0])]++;
			std::unordered_set<std::string_view> seen;
			for (uint32_t i = 0; i < outer; i++) {
				const auto& name = symbols.Name(sample.frames[i]);
				if (seen.insert(name).second) {
					inclusive[name]++;
				}
			}
		}
		Answer(Format("LIVE prof id=%" PRIu64 " thread=%s samples=%zu under %s: %.2f%% of them", id,
		              thread_name.c_str(), samples.size(), within.c_str(),
		              100.0 * static_cast<double>(matched) / static_cast<double>(samples.size())));
		const auto list = [&](const char* kind, const std::unordered_map<std::string, uint64_t>& counts) {
			std::vector<std::pair<std::string, uint64_t>> sorted(counts.begin(), counts.end());
			std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
				return a.second != b.second ? a.second > b.second : a.first < b.first;
			});
			for (size_t i = 0; i < std::min(top, sorted.size()); i++) {
				Answer(Format("LIVE prof id=%" PRIu64 " %s %5.2f%% %s", id, kind,
				              100.0 * static_cast<double>(sorted[i].second) /
				                  static_cast<double>(samples.size()),
				              Short(sorted[i].first).c_str()));
			}
		};
		list("self", self);
		list("incl", inclusive);
		Answer(Format("LIVE prof id=%" PRIu64 " end", id));
		return;
	}
	// With a focus: the callers of the innermost function whose name contains it, three deep.
	std::unordered_map<std::string, uint64_t> paths;
	for (const auto& sample: samples) {
		self[symbols.Name(sample.frames[0])]++;
		std::unordered_set<std::string_view> seen;
		bool                                 focused = focus.empty();
		for (uint32_t i = 0; i < sample.count; i++) {
			const auto& name = symbols.Name(sample.frames[i]);
			if (seen.insert(name).second) {
				inclusive[name]++;
			}
			if (!focused && name.find(focus) != std::string::npos) {
				focused = true;
				std::string        path;
				const std::string* previous = &name;
				for (uint32_t caller = i + 1, depth = 0; caller < sample.count && depth < 3;
				     caller++) {
					const auto& above = symbols.Name(sample.frames[caller]);
					if (above == *previous) {
						continue;
					}
					path += (path.empty() ? "" : " < ") + Short(above);
					previous = &above;
					depth++;
				}
				paths[path]++;
			}
		}
	}
	const auto report = [&](const char* kind, const std::unordered_map<std::string, uint64_t>& counts,
	                        bool whole = false) {
		std::vector<std::pair<std::string, uint64_t>> sorted(counts.begin(), counts.end());
		std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
			return a.second != b.second ? a.second > b.second : a.first < b.first;
		});
		for (size_t i = 0; i < std::min(top, sorted.size()); i++) {
			Answer(Format("LIVE prof id=%" PRIu64 " %s %5.2f%% %s", id, kind,
			              100.0 * static_cast<double>(sorted[i].second) /
			                  static_cast<double>(samples.size()),
			              (whole ? sorted[i].first : Short(sorted[i].first)).c_str()));
		}
	};
	Answer(Format("LIVE prof id=%" PRIu64 " thread=%s samples=%zu seconds=%.1f", id,
	              thread_name.c_str(), samples.size(), seconds));
	if (focus.empty()) {
		report("self", self);
		report("incl", inclusive);
	} else {
		report("path", paths, true);
	}
	Answer(Format("LIVE prof id=%" PRIu64 " end", id));
}

#else

void Profile(uint64_t id, double /*seconds*/, const std::string& /*thread_name*/, size_t /*top*/,
             const std::string& /*focus*/) {
	Answer(Format("LIVE error id=%" PRIu64 " prof: only on Windows", id));
}

#endif

// ---------------------------------------------------------------------------------------------
// pad

constexpr std::array PAD_BUTTONS {
    std::pair {"l3", 0x00000002u},       std::pair {"r3", 0x00000004u},
    std::pair {"options", 0x00000008u},  std::pair {"up", 0x00000010u},
    std::pair {"right", 0x00000020u},    std::pair {"down", 0x00000040u},
    std::pair {"left", 0x00000080u},     std::pair {"l2", 0x00000100u},
    std::pair {"r2", 0x00000200u},       std::pair {"l1", 0x00000400u},
    std::pair {"r1", 0x00000800u},       std::pair {"triangle", 0x00001000u},
    std::pair {"circle", 0x00002000u},   std::pair {"cross", 0x00004000u},
    std::pair {"square", 0x00008000u},   std::pair {"touchpad", 0x00100000u},
};

void Pad(uint64_t id, const std::string& button, int ms) {
	const auto known = std::find_if(PAD_BUTTONS.begin(), PAD_BUTTONS.end(),
	                                [&](const auto& entry) { return button == entry.first; });
	if (known == PAD_BUTTONS.end() || g_hooks.pad_button == nullptr) {
		Answer(Format("LIVE error id=%" PRIu64 " pad: unknown button %s", id, button.c_str()));
		return;
	}
	g_hooks.pad_button(known->second, true);
	std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(ms, 20, 60000)));
	g_hooks.pad_button(known->second, false);
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// ---------------------------------------------------------------------------------------------
// The command file

std::vector<std::string> Words(const std::string& line) {
	std::vector<std::string> words;
	std::istringstream       stream(line);
	for (std::string word; stream >> word;) {
		words.push_back(std::move(word));
	}
	return words;
}

// Guest bytes [address, address + size) into `data`; pages that are not backed read as zeros.
// False when none of it is backed.
bool ReadGuest(uint64_t address, uint8_t* data, uint64_t size) {
	if (g_hooks.guest_read(address, data, size)) {
		return true;
	}
	constexpr uint64_t Page = 0x4000;
	bool               any  = false;
	for (uint64_t at = address; at < address + size;) {
		const auto next = std::min(address + size, (at / Page + 1) * Page);
		if (g_hooks.guest_read(at, data + (at - address), next - at)) {
			any = true;
		} else {
			std::memset(data + (at - address), 0, static_cast<size_t>(next - at));
		}
		at = next;
	}
	return any;
}

// The addresses in [from, to) of the guest's mappings where `pattern` stands, at multiples of
// `align`, at most `limit` of them; `scanned` gets the bytes looked at.
std::vector<uint64_t> FindGuest(const std::vector<uint8_t>& pattern, uint64_t align, size_t limit,
                                uint64_t from, uint64_t to, uint64_t& scanned) {
	return g_hooks.guest_find(pattern.data(), pattern.size(), align, limit, from, to, &scanned);
}

std::vector<uint8_t> HexBytes(const std::string& text) {
	std::vector<uint8_t> bytes;
	for (size_t i = 0; i + 1 < text.size(); i += 2) {
		bytes.push_back(static_cast<uint8_t>(std::strtoul(text.substr(i, 2).c_str(), nullptr, 16)));
	}
	return bytes;
}

// mem maps                                   the guest's mappings: how many, their bytes
// mem find <hex bytes> [max] [from] [to]     where the bytes stand
// mem find16 <text> [max] [from] [to]        the text as a zero-terminated UTF-16 string
// mem findq <hex value> [max] [from] [to]    the value as an aligned 64-bit word (a pointer)
// mem read <hex address> <count>             up to 1024 bytes, as hex
// mem write <hex address> <hex bytes>        through the backing store
// mem dump <hex address> <hex size> <file>   the range into a file (unbacked pages as zeros)
// Addresses and values are hexadecimal. Reads and writes go through the backing store: they see
// what the guest's CPU stored, not what the GPU has written and nobody read back.
void Mem(uint64_t id, const std::vector<std::string>& words) {
	if (g_hooks.guest_read == nullptr || g_hooks.guest_next_mapping == nullptr ||
	    g_hooks.guest_write == nullptr || g_hooks.guest_find == nullptr || words.size() < 2) {
		Answer(Format("LIVE error id=%" PRIu64 " mem: not available", id));
		return;
	}
	const auto hex = [&](size_t index, uint64_t fallback) {
		return words.size() > index ? std::strtoull(words[index].c_str(), nullptr, 16) : fallback;
	};
	const auto& what = words[1];
	if (what == "maps") {
		uint64_t count = 0;
		uint64_t bytes = 0;
		uint64_t start = 0;
		uint64_t end   = 0;
		for (uint64_t at = 0; g_hooks.guest_next_mapping(at, &start, &end) && end > at; at = end) {
			count++;
			bytes += end - start;
		}
		Answer(Format("LIVE mem id=%" PRIu64 " maps: %" PRIu64 " mappings, %" PRIu64 " MiB", id, count,
		              bytes >> 20u));
	} else if ((what == "find" || what == "find16" || what == "findq") && words.size() >= 3) {
		std::vector<uint8_t> pattern;
		uint64_t             align = 1;
		if (what == "find") {
			pattern = HexBytes(words[2]);
		} else if (what == "find16") {
			for (const char c: words[2]) {
				pattern.push_back(static_cast<uint8_t>(c));
				pattern.push_back(0);
			}
			pattern.push_back(0);
			pattern.push_back(0);
			align = 2;
		} else {
			const auto value = hex(2, 0);
			pattern.resize(sizeof(value));
			std::memcpy(pattern.data(), &value, sizeof(value));
			align = 8;
		}
		if (pattern.empty()) {
			Answer(Format("LIVE error id=%" PRIu64 " mem: nothing to find", id));
			return;
		}
		const auto limit   = static_cast<size_t>(std::clamp<uint64_t>(
            words.size() > 3 ? std::strtoull(words[3].c_str(), nullptr, 10) : 16u, 1u, 4096u));
		uint64_t   scanned = 0;
		const auto begin   = std::chrono::steady_clock::now();
		const auto found   = FindGuest(pattern, align, limit, hex(4, 0), hex(5, UINT64_MAX), scanned);
		std::string line   = Format(
            "LIVE mem id=%" PRIu64 " %s: %zu found in %" PRIu64 " MiB, %.1f s:", id, what.c_str(),
            found.size(), scanned >> 20u,
            std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
		for (const auto address: found) {
			line += Format(" %" PRIx64, address);
		}
		Answer(line);
	} else if (what == "read" && words.size() >= 4) {
		const auto address = hex(2, 0);
		const auto count   = std::clamp<uint64_t>(std::strtoull(words[3].c_str(), nullptr, 10), 1, 1024);
		std::vector<uint8_t> bytes(static_cast<size_t>(count));
		if (!ReadGuest(address, bytes.data(), count)) {
			Answer(Format("LIVE error id=%" PRIu64 " mem: %" PRIx64 " is not mapped", id, address));
			return;
		}
		std::string line = Format("LIVE mem id=%" PRIu64 " read %" PRIx64 ":", id, address);
		for (size_t i = 0; i < bytes.size(); i++) {
			line += Format(i % 8 == 0 ? " %02x" : "%02x", bytes[i]);
		}
		Answer(line);
	} else if (what == "dump" && words.size() >= 5) {
		const auto address = hex(2, 0);
		const auto size    = std::min<uint64_t>(hex(3, 0), uint64_t {1} << 30u);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		const bool           any = size != 0 && ReadGuest(address, bytes.data(), size);
		std::ofstream        file(words[4], std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		Answer(Format("LIVE mem id=%" PRIu64 " dump %" PRIx64 ": %zu bytes to %s%s", id, address, bytes.size(),
		              words[4].c_str(), any ? "" : " (nothing of it is mapped)"));
	} else if (what == "write" && words.size() >= 4) {
		const auto address = hex(2, 0);
		const auto bytes   = HexBytes(words[3]);
		const bool done    = !bytes.empty() && g_hooks.guest_write(address, bytes.data(), bytes.size());
		Answer(Format("LIVE mem id=%" PRIu64 " write %" PRIx64 ": %zu bytes %s", id, address, bytes.size(),
		              done ? "written" : "NOT written (not mapped)"));
	} else {
		Answer(Format("LIVE error id=%" PRIu64 " mem: maps | find | find16 | findq | read | write | dump", id));
	}
}

void Execute(uint64_t id, const std::string& line) {
	const auto words = Words(line);
	if (words.empty() || words[0].starts_with('#')) {
		return;
	}
	const auto& command = words[0];
	const auto  number  = [&](size_t index, double fallback) {
		return words.size() > index ? std::strtod(words[index].c_str(), nullptr) : fallback;
	};
	if (command == "measure" && words.size() >= 2) {
		Measure(id, std::clamp(number(1, 10.0), 0.1, 3600.0), words.size() > 2 ? words[2] : "-");
	} else if (command == "prof" && words.size() >= 2) {
		Profile(id, std::clamp(number(1, 10.0), 0.1, 600.0), words.size() > 2 ? words[2] : "gpu",
		        static_cast<size_t>(std::clamp(number(3, 40.0), 1.0, 500.0)),
		        words.size() > 4 ? words[4] : std::string {});
	} else if (command == "set" && words.size() == 3) {
		for (auto* entry = g_switches; entry != nullptr; entry = const_cast<Switch*>(entry->Next())) {
			if (words[1] == entry->Name()) {
				const auto previous = entry->Get();
				entry->Set(static_cast<uint32_t>(std::strtoul(words[2].c_str(), nullptr, 0)));
				Answer(Format("LIVE set id=%" PRIu64 " %s=%u (was %u)", id, entry->Name(),
				              entry->Get(), previous));
				return;
			}
		}
		Answer(Format("LIVE error id=%" PRIu64 " set: no switch named %s", id, words[1].c_str()));
	} else if (command == "switches") {
		std::string list = Format("LIVE switches id=%" PRIu64, id);
		for (const auto* entry = g_switches; entry != nullptr; entry = entry->Next()) {
			list += Format(" %s=%u", entry->Name(), entry->Get());
		}
		Answer(list);
	} else if (command == "mem") {
		Mem(id, words);
	} else if (command == "pad" && words.size() >= 2) {
		Pad(id, words[1], static_cast<int>(number(2, 120.0)));
	} else if (command == "stick" && words.size() == 6 && g_hooks.pad_sticks != nullptr) {
		const auto axis = [&](size_t index) {
			return std::clamp(static_cast<int>(number(index, 128.0)), 0, 255);
		};
		g_hooks.pad_sticks(axis(1), axis(2), axis(3), axis(4));
		std::this_thread::sleep_for(
		    std::chrono::milliseconds(std::clamp(static_cast<int>(number(5, 0.0)), 0, 600000)));
		g_hooks.pad_sticks(128, 128, 128, 128);
	} else if (command == "sleep" && words.size() == 2) {
		std::this_thread::sleep_for(
		    std::chrono::milliseconds(std::clamp(static_cast<int>(number(1, 0.0)), 0, 600000)));
	} else if (command == "wait" && g_hooks.pending_builds != nullptr) {
		const auto start    = std::chrono::steady_clock::now();
		const auto limit    = std::chrono::duration<double>(std::clamp(number(1, 300.0), 0.0, 3600.0));
		uint64_t   pending  = g_hooks.pending_builds();
		uint64_t   at_start = pending;
		// Idle for a second: builds queue as the guest creates its shaders.
		auto idle_since = start;
		for (;;) {
			const auto now = std::chrono::steady_clock::now();
			if (pending != 0) {
				idle_since = now;
			}
			if (now - idle_since >= std::chrono::seconds {1} || now - start >= limit) {
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			pending = g_hooks.pending_builds();
		}
		Answer(Format("LIVE wait id=%" PRIu64 " pending=%" PRIu64 " (was %" PRIu64 ") waited=%.1fs",
		              id, pending, at_start,
		              std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
		                  .count()));
	} else if (command == "threads") {
		std::string list = Format("LIVE threads id=%" PRIu64, id);
		for (const auto& thread: Threads()) {
			list += Format(" %s:%u=%.2fs", thread.name.c_str(), thread.tid, ThreadCpuSeconds(thread));
		}
		Answer(list);
	} else if (command == "quit" && g_hooks.quit != nullptr) {
		Answer(Format("LIVE quit id=%" PRIu64, id));
		g_hooks.quit();
	} else {
		Answer(Format("LIVE error id=%" PRIu64 " line=%s", id, line.c_str()));
	}
}

void Run() {
	uint64_t last_id = 0;
	for (;;) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		std::ifstream file(g_command_path);
		std::string   line;
		if (!file || !std::getline(file, line)) {
			continue;
		}
		const auto head = Words(line);
		if (head.size() != 2 || head[0] != "id") {
			continue;
		}
		const auto id = std::strtoull(head[1].c_str(), nullptr, 10);
		if (id == last_id) {
			continue;
		}
		last_id = id;
		std::vector<std::string> commands;
		while (std::getline(file, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			commands.push_back(line);
		}
		file.close();
		for (const auto& command: commands) {
			Execute(id, command);
		}
		Answer(Format("LIVE done id=%" PRIu64, id));
	}
}

} // namespace

Switch::Switch(const char* name, uint32_t default_value) noexcept
    : m_name(name), m_value(default_value), m_next(g_switches) {
	g_switches = this;
	char variable[96] {};
	std::snprintf(variable, sizeof(variable), "KYTY_%s", name);
	for (char* c = variable; *c != '\0'; c++) {
		*c = static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
	}
	if (const char* text = std::getenv(variable); text != nullptr && *text != '\0') {
		m_value.store(static_cast<uint32_t>(std::strtoul(text, nullptr, 0)),
		              std::memory_order_relaxed);
	}
}

Counter::Counter(const char* name) noexcept: m_name(name), m_next(g_counters) {
	g_counters = this;
}

void SetHostHooks(const HostHooks& hooks) noexcept {
	g_hooks = hooks;
}

void RegisterThread(const char* name) noexcept {
	NamedThread thread;
	thread.name          = name;
	Detail::t_gpu_thread = thread.name == "gpu";
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	HANDLE handle = nullptr;
	if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &handle, 0,
	                    FALSE, DUPLICATE_SAME_ACCESS) != 0) {
		thread.handle = reinterpret_cast<uint64_t>(handle);
	}
	thread.tid = GetCurrentThreadId();
	ULONG_PTR low  = 0;
	ULONG_PTR high = 0;
	GetCurrentThreadStackLimits(&low, &high);
	thread.stack_low  = low;
	thread.stack_high = high;
#endif
	std::scoped_lock lock(g_threads_mutex);
	g_threads.push_back(std::move(thread));
}

void Start() {
	const char* path = std::getenv("KYTY_LIVE_FILE");
	if (path == nullptr || *path == '\0') {
		return;
	}
	g_command_path = path;
	g_answer_path  = g_command_path + ".out";
	Detail::g_active.store(true, std::memory_order_relaxed);
	std::string list;
	for (const auto* entry = g_switches; entry != nullptr; entry = entry->Next()) {
		list += Format(" %s=%u", entry->Name(), entry->Get());
	}
	Answer("LIVE start file=" + g_command_path + " switches:" + list);
	std::thread(Run).detach();
}

} // namespace Common::LiveControl
