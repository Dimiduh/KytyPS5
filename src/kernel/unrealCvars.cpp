#include "kernel/unrealCvars.h"

#include "common/logging/log.h"
#include "kernel/memory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Libs::LibKernel::UnrealCvars {

namespace {

struct Wanted {
	std::string name;
	std::string lower;
	double      number = 0;
	// "x<factor>": the number is the title's own value times the factor, and follows it when
	// the title sets another.
	bool   relative = false;
	double factor   = 1.0;
	// Once found: where the two copies of the value are, of which class, and what goes there.
	uint64_t                value_address = 0;
	bool                    is_float      = false;
	std::array<uint8_t, 8>  bytes {};
	bool                    refused  = false;
	uint64_t                restored = 0;
};

// An element of the console manager's map: TPair<FString, IConsoleObject*> and the set's links.
struct Element {
	uint64_t text;
	int32_t  count; // with the terminator
	int32_t  capacity;
	uint64_t object;
	uint64_t links;
};
static_assert(sizeof(Element) == 32);

struct Anchor {
	uint64_t element      = 0;
	uint64_t float_class  = 0; // the vtable of the float variables
	uint32_t value_offset = 0; // of the value's two copies in an object
};

constexpr size_t ObjectBytes = 128;

std::vector<Wanted>     g_wanted;
std::thread             g_thread;
std::mutex              g_mutex;
std::condition_variable g_wake;
bool                    g_stop = false;

void Say(const std::string& text) {
	Log::WriteToConsoleAndLog(text);
}

std::string Format(const char* format, ...) {
	va_list arguments;
	va_start(arguments, format);
	char text[512];
	std::vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	return text;
}

std::string Lower(std::string_view text) {
	std::string lower(text);
	std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return lower;
}

template <typename T>
bool Read(uint64_t address, T& value) {
	return Memory::TryReadBacking(address, &value, sizeof(T));
}

// The element's name in lower case; empty when the bytes are not an element.
std::string NameOf(const Element& element) {
	constexpr int32_t Longest = 256;
	if (element.text == 0 || element.object == 0 || element.count < 2 || element.count > Longest ||
	    element.capacity < element.count) {
		return {};
	}
	std::array<char16_t, Longest> text {};
	if (!Memory::TryReadBacking(element.text, text.data(), static_cast<uint64_t>(element.count) * sizeof(char16_t)) ||
	    text[static_cast<size_t>(element.count) - 1] != 0) {
		return {};
	}
	std::string name;
	for (int32_t i = 0; i + 1 < element.count; i++) {
		const auto c = text[static_cast<size_t>(i)];
		if (c < 0x21 || c > 0x7e) {
			return {};
		}
		name.push_back(static_cast<char>(std::tolower(static_cast<int>(c))));
	}
	return name;
}

// The map element of r.ScreenPercentage, with what its object tells about every variable.
std::optional<Anchor> FindAnchor() {
	static constexpr std::string_view Name = "r.ScreenPercentage";
	std::vector<uint8_t>              text;
	for (const char c: Name) {
		text.push_back(static_cast<uint8_t>(c));
		text.push_back(0);
	}
	text.push_back(0);
	text.push_back(0);
	for (const auto found: Memory::FindInBacking(text, sizeof(char16_t), 16)) {
		std::array<uint8_t, sizeof(found)> pointer {};
		std::memcpy(pointer.data(), &found, sizeof(found));
		for (const auto at: Memory::FindInBacking(pointer, sizeof(found), 16)) {
			Element element {};
			if (!Read(at, element) || element.text != found ||
			    element.count != static_cast<int32_t>(Name.size()) + 1) {
				continue;
			}
			std::array<uint8_t, ObjectBytes> object {};
			uint64_t                         vtable = 0;
			if (!Read(element.object, object)) {
				continue;
			}
			std::memcpy(&vtable, object.data(), sizeof(vtable));
			// The value: two equal floats, a percentage.
			for (uint32_t offset = 0x20; vtable != 0 && offset + 8 <= ObjectBytes; offset += 8) {
				std::array<float, 2> copies {};
				std::memcpy(copies.data(), object.data() + offset, sizeof(copies));
				if (copies[0] == copies[1] && copies[0] >= 5.0f && copies[0] <= 400.0f) {
					return Anchor {at, vtable, offset};
				}
			}
		}
	}
	return std::nullopt;
}

// Calls visit(name, object) for the elements of the array the anchor's element is in.
template <typename Visit>
void ForEachVariable(const Anchor& anchor, Visit&& visit) {
	// Elements in a row that are none before the array is taken to have ended (it has holes
	// where variables were removed), and how far to look at most.
	constexpr uint64_t Gap    = 8192;
	constexpr uint64_t Extent = (64u << 20u) / sizeof(Element);
	for (const bool up: {false, true}) {
		uint64_t gap = 0;
		for (uint64_t i = up ? 0 : 1; i < Extent && gap < Gap; i++) {
			const auto at = up ? anchor.element + i * sizeof(Element) : anchor.element - i * sizeof(Element);
			Element    element {};
			const auto name = Read(at, element) ? NameOf(element) : std::string {};
			if (name.empty()) {
				gap++;
				continue;
			}
			gap = 0;
			visit(name, element.object);
		}
	}
}

// Finds the wanted variables; true once the console manager was found at all.
bool Locate() {
	const auto anchor = FindAnchor();
	if (!anchor) {
		return false;
	}
	// The int class: that of a variable that is an int in every version.
	static constexpr std::array<std::string_view, 3> Ints = {"r.vsync", "r.dynamicres.operationmode",
	                                                         "r.shadow.virtual.enable"};
	uint64_t int_class = 0;
	uint64_t count     = 0;
	struct Found {
		Wanted*  wanted;
		uint64_t object;
	};
	std::vector<Found> found;
	const auto         vtable_of = [](uint64_t object) {
        uint64_t vtable = 0;
        return Read(object, vtable) ? vtable : 0;
	};
	ForEachVariable(*anchor, [&](const std::string& name, uint64_t object) {
		count++;
		if (int_class == 0 && std::ranges::find(Ints, name) != Ints.end()) {
			std::array<int32_t, 2> copies {};
			const auto             vtable = vtable_of(object);
			if (vtable != 0 && vtable != anchor->float_class && Read(object + anchor->value_offset, copies) &&
			    copies[0] == copies[1]) {
				int_class = vtable;
			}
		}
		for (auto& wanted: g_wanted) {
			if (wanted.lower == name && wanted.value_address == 0 && !wanted.refused) {
				found.push_back({&wanted, object});
			}
		}
	});
	Say(Format("Unreal console variables: %llu found in the title's memory\n", static_cast<unsigned long long>(count)));
	for (const auto& [wanted, object]: found) {
		const auto vtable = vtable_of(object);
		if (vtable == anchor->float_class) {
			wanted->is_float = true;
		} else if (vtable != 0 && vtable == int_class &&
		           (wanted->relative || std::nearbyint(wanted->number) == wanted->number)) {
			wanted->is_float = false;
		} else {
			wanted->refused = true;
			Say(Format("Unreal console variables: %s is not a float or int variable (or not given a whole number): "
			           "left as it is\n",
			           wanted->name.c_str()));
			continue;
		}
		wanted->value_address = object + anchor->value_offset;
	}
	for (const auto& wanted: g_wanted) {
		if (wanted.value_address == 0 && !wanted.refused) {
			Say(Format("Unreal console variables: %s is not among them (yet)\n", wanted.name.c_str()));
		}
	}
	return true;
}

// Puts the wanted values where they are not; the first time, with what was there.
void Apply(bool first) {
	for (auto& wanted: g_wanted) {
		if (wanted.value_address == 0) {
			continue;
		}
		std::array<uint8_t, 8> now {};
		if (!Read(wanted.value_address, now) || (wanted.restored != 0 && now == wanted.bytes)) {
			continue;
		}
		// What the title has there now is its own value (ours is what `bytes` holds).
		if (wanted.relative) {
			float   as_float = 0;
			int32_t as_int   = 0;
			std::memcpy(&as_float, now.data(), sizeof(as_float));
			std::memcpy(&as_int, now.data(), sizeof(as_int));
			wanted.number = (wanted.is_float ? static_cast<double>(as_float) : static_cast<double>(as_int)) * wanted.factor;
		}
		if (wanted.is_float) {
			const std::array<float, 2> copies {static_cast<float>(wanted.number), static_cast<float>(wanted.number)};
			std::memcpy(wanted.bytes.data(), copies.data(), sizeof(copies));
		} else {
			const auto                   whole = static_cast<int32_t>(std::nearbyint(wanted.number));
			const std::array<int32_t, 2> copies {whole, whole};
			std::memcpy(wanted.bytes.data(), copies.data(), sizeof(copies));
		}
		if (now == wanted.bytes ||
		    !Memory::StoreGuestBytes(wanted.value_address, wanted.bytes.data(), wanted.bytes.size())) {
			continue;
		}
		wanted.restored++;
		// (The title sets some of them again and again; the first few times are worth a line.)
		if (first || wanted.restored <= 4) {
			std::array<float, 2>   as_float {};
			std::array<int32_t, 2> as_int {};
			std::memcpy(as_float.data(), now.data(), sizeof(as_float));
			std::memcpy(as_int.data(), now.data(), sizeof(as_int));
			Say(Format("Unreal console variables: %s = %g (the title had %g as a float, %d as an int)\n",
			           wanted.name.c_str(), wanted.number, static_cast<double>(as_float[0]), as_int[0]));
		}
	}
}

void Run() {
	using namespace std::chrono_literals;
	const auto wait = [](std::chrono::milliseconds time) {
		std::unique_lock lock(g_mutex);
		return !g_wake.wait_for(lock, time, [] { return g_stop; });
	};
	// The console manager exists once the engine has started: looked for until then, and again
	// while a variable is missing (modules register theirs when they load), ever less often.
	bool located  = false;
	auto pause    = 5000ms;
	auto next_try = std::chrono::steady_clock::now() + pause;
	for (;;) {
		if (!wait(250ms)) {
			return;
		}
		const bool missing = std::ranges::any_of(
		    g_wanted, [](const Wanted& wanted) { return wanted.value_address == 0 && !wanted.refused; });
		if (missing && std::chrono::steady_clock::now() >= next_try) {
			uint64_t start = 0;
			uint64_t end   = 0;
			if (Memory::NextMappedRange(0, &start, &end)) {
				const bool before = located;
				located           = Locate() || located;
				Apply(!before);
				if (located) {
					pause = std::min(pause * 2, std::chrono::milliseconds(120000));
				}
			}
			next_try = std::chrono::steady_clock::now() + pause;
		}
		Apply(false);
	}
}

} // namespace

void Start(const std::vector<std::string>& assignments) {
	for (const auto& assignment: assignments) {
		const auto equals   = assignment.find('=');
		const bool relative = equals != std::string::npos && equals + 1 < assignment.size() &&
		                      (assignment[equals + 1] == 'x' || assignment[equals + 1] == '*');
		const auto* text    = assignment.c_str() + (equals == std::string::npos ? 0 : equals + (relative ? 2 : 1));
		char*      end      = nullptr;
		const auto number   = equals == std::string::npos ? 0.0 : std::strtod(text, &end);
		if (equals == std::string::npos || equals == 0 || end == text || *end != '\0' || !std::isfinite(number)) {
			Say(Format("Unreal console variables: \"%s\" is not <name>=<number> or <name>=x<factor>: ignored\n",
			           assignment.c_str()));
			continue;
		}
		Wanted wanted;
		wanted.name     = assignment.substr(0, equals);
		wanted.lower    = Lower(wanted.name);
		wanted.relative = relative;
		(relative ? wanted.factor : wanted.number) = number;
		// The later one of a name wins.
		std::erase_if(g_wanted, [&](const Wanted& other) { return other.lower == wanted.lower; });
		g_wanted.push_back(std::move(wanted));
	}
	if (g_wanted.empty() || g_thread.joinable()) {
		return;
	}
	g_stop   = false;
	g_thread = std::thread(Run);
}

void Stop() {
	{
		std::lock_guard lock(g_mutex);
		g_stop = true;
	}
	g_wake.notify_all();
	if (g_thread.joinable()) {
		g_thread.join();
	}
}

} // namespace Libs::LibKernel::UnrealCvars
