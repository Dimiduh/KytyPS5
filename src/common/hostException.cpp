#include "common/hostException.h"

#include "common/logging/log.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#include <windows.h> // IWYU pragma: keep
#else
#include <csignal>
#include <initializer_list>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/ucontext.h>
#else
#include <ucontext.h> // IWYU pragma: keep
#endif
#endif

// IWYU pragma: no_include <errhandlingapi.h>
// IWYU pragma: no_include <excpt.h>
// IWYU pragma: no_include <minwinbase.h>
// IWYU pragma: no_include <minwindef.h>
// IWYU pragma: no_include <wtypes.h>

namespace Common::HostException {

static std::atomic<Handler> g_handler {nullptr};
static_assert(decltype(g_handler)::is_always_lock_free);

#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX

// macOS uses the same POSIX platform setting and needs a signal stack too.
class ThreadSignalStack {
public:
	ThreadSignalStack() {
		const auto page_size = static_cast<size_t>(::getpagesize());
		const auto stack_size =
		    (std::max<size_t>(64 * 1024, MINSIGSTKSZ) + page_size - 1) & ~(page_size - 1);
		if (::posix_memalign(&m_memory, page_size, stack_size) != 0) {
			return;
		}

		stack_t stack {};
		stack.ss_sp   = m_memory;
		stack.ss_size = stack_size;
		if (::sigaltstack(&stack, &m_previous) != 0) {
			std::free(m_memory);
			m_memory = nullptr;
		}
	}

	~ThreadSignalStack() {
		if (m_memory != nullptr && ::sigaltstack(&m_previous, nullptr) == 0) {
			std::free(m_memory);
		}
	}

	[[nodiscard]] bool IsInitialized() const { return m_memory != nullptr; }

	KYTY_CLASS_NO_COPY(ThreadSignalStack)

private:
	void*   m_memory = nullptr;
	stack_t m_previous {};
};

bool InitializeThreadSignalStack() {
	// Keep fault handling off guest stacks, which GPU tracking can make read-only.
	thread_local ThreadSignalStack signal_stack;
	return signal_stack.IsInitialized();
}

int GuestSignalDispatchSignal() {
#if defined(__APPLE__)
	// macOS has no realtime signals, and the host uses SIGUSR1 for nothing else.
	return SIGUSR1;
#else
	return SIGRTMIN + 3;
#endif
}

#endif

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS

static std::atomic<Reporter> g_reporter {nullptr};
static_assert(decltype(g_reporter)::is_always_lock_free);

// Fills `info` from an access violation, a guard page violation or an illegal instruction;
// returns false for any other exception.
static bool ReadException(PEXCEPTION_POINTERS exception, ExceptionInfo* out) {
	const auto*   exception_record = exception->ExceptionRecord;
	ExceptionInfo info {};
	// A guard page another thread touched: Windows handles a thread's own stack guard page in the
	// kernel, so one that reaches here belongs to some other host thread's stack (the CPU walkers
	// of guest descriptor tables read pointers the guest never meant for them, which can land
	// there). The system has already turned the page into a normal one; it is treated as the
	// access violation it is for anything else, and the guard is put back if the access is handled
	// without touching it.
	if (exception_record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
	    exception_record->ExceptionCode == STATUS_GUARD_PAGE_VIOLATION) {
		info.type = ExceptionType::AccessViolation;
		switch (exception_record->ExceptionInformation[0]) {
			case 0: info.access_violation_type = AccessViolationType::Read; break;
			case 1: info.access_violation_type = AccessViolationType::Write; break;
			case 8: info.access_violation_type = AccessViolationType::Execute; break;
			default: info.access_violation_type = AccessViolationType::Unknown; break;
		}
		info.access_violation_vaddr = exception_record->ExceptionInformation[1];
	} else if (exception_record->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION) {
		info.type = ExceptionType::IllegalInstruction;
	} else {
		return false;
	}
	info.exception_address = reinterpret_cast<uint64_t>(exception_record->ExceptionAddress);
	info.native_code       = exception_record->ExceptionCode;
	info.native_context    = exception->ContextRecord;

	const auto* registers = exception->ContextRecord;
	info.rax              = registers->Rax;
	info.rbx              = registers->Rbx;
	info.rcx              = registers->Rcx;
	info.rdx              = registers->Rdx;
	info.rsi              = registers->Rsi;
	info.rdi              = registers->Rdi;
	info.rbp              = registers->Rbp;
	info.rsp              = registers->Rsp;
	info.r8               = registers->R8;
	info.r9               = registers->R9;
	info.r10              = registers->R10;
	info.r11              = registers->R11;
	info.r12              = registers->R12;
	info.r13              = registers->R13;
	info.r14              = registers->R14;
	info.r15              = registers->R15;
	*out                  = info;
	return true;
}

static LONG WINAPI ExceptionFilter(PEXCEPTION_POINTERS exception) noexcept {
	if (exception->ExceptionRecord->ExceptionCode == 0x406D1388) {
		// 0x406D1388 only names the thread for an attached debugger.
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	ExceptionInfo info {};
	if (!ReadException(exception, &info)) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	const bool guard_page = info.native_code == STATUS_GUARD_PAGE_VIOLATION;

	const auto handler = g_handler.load(std::memory_order_acquire);
	if (handler != nullptr && handler(info)) {
		if (guard_page) {
			MEMORY_BASIC_INFORMATION page {};
			const auto* address = reinterpret_cast<const void*>(info.access_violation_vaddr);
			if (VirtualQuery(address, &page, sizeof(page)) == sizeof(page) && page.State == MEM_COMMIT &&
			    (page.Protect & PAGE_GUARD) == 0) {
				DWORD old = 0;
				VirtualProtect(page.BaseAddress, 1, page.Protect | PAGE_GUARD, &old);
			}
		}
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

// The module an address lies in (its file name) and the offset into it.
static void NameAddress(uint64_t address, char* name, size_t size, uint64_t* offset) noexcept {
	std::snprintf(name, size, "no module");
	*offset        = 0;
	HMODULE module = nullptr;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       reinterpret_cast<LPCWSTR>(address), &module) != 0 &&
	    module != nullptr) {
		char path[MAX_PATH] = "";
		if (GetModuleFileNameA(module, path, MAX_PATH) != 0) {
			const char* file = std::strrchr(path, '\\');
			std::snprintf(name, size, "%s", file != nullptr ? file + 1 : path);
		}
		*offset = address - reinterpret_cast<uint64_t>(module);
	}
}

static bool Readable(uint64_t address, size_t size) noexcept {
	MEMORY_BASIC_INFORMATION page {};
	if (VirtualQuery(reinterpret_cast<const void*>(address), &page, sizeof(page)) != sizeof(page)) {
		return false;
	}
	constexpr DWORD NoRead = PAGE_NOACCESS | PAGE_GUARD;
	return page.State == MEM_COMMIT && (page.Protect & NoRead) == 0 && page.Protect != 0 &&
	       address + size <=
	           reinterpret_cast<uint64_t>(page.BaseAddress) + static_cast<uint64_t>(page.RegionSize);
}

// The registers, the bytes of the instruction that failed and the callers by module: a crash
// in the Vulkan loader says nothing about whose call it was (the driver's, an overlay layer's,
// NVIDIA's runtime's), the frames above it do. `text` is filled as far as the walk gets, so a
// second fault in here (the caller guards it) loses only the rest.
struct CrashText {
	char*  data = nullptr;
	size_t size = 0;
	size_t used = 0;
};

#if defined(__clang__) || defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
static void Append(CrashText* text, const char* format, ...) noexcept {
	if (text->used + 1 >= text->size) {
		return;
	}
	va_list values;
	va_start(values, format);
	const int written = std::vsnprintf(text->data + text->used, text->size - text->used, format, values);
	va_end(values);
	if (written > 0) {
		text->used = std::min(text->used + static_cast<size_t>(written), text->size - 1);
	}
}

static void DescribeCrash(const CONTEXT* at, CrashText* text) noexcept {
	const auto wide = [](DWORD64 value) { return static_cast<unsigned long long>(value); };
	Append(text, "rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n", wide(at->Rax), wide(at->Rbx),
	       wide(at->Rcx), wide(at->Rdx));
	Append(text, "rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx\n", wide(at->Rsi), wide(at->Rdi),
	       wide(at->Rbp), wide(at->Rsp));
	Append(text, "r8 =%016llx r9 =%016llx r10=%016llx r11=%016llx\n", wide(at->R8), wide(at->R9),
	       wide(at->R10), wide(at->R11));
	Append(text, "r12=%016llx r13=%016llx r14=%016llx r15=%016llx\n", wide(at->R12), wide(at->R13),
	       wide(at->R14), wide(at->R15));
	if (Readable(at->Rip, 16)) {
		Append(text, "code:");
		for (int index = 0; index < 16; ++index) {
			Append(text, " %02x", reinterpret_cast<const uint8_t*>(at->Rip)[index]);
		}
		Append(text, "\n");
	}
	CONTEXT context = *at;
	for (int frame = 0; frame < 40 && context.Rip != 0; ++frame) {
		char     name[MAX_PATH];
		uint64_t offset = 0;
		NameAddress(context.Rip, name, sizeof(name), &offset);
		Append(text, "#%-2d 0x%llx (%s+0x%llx)\n", frame, wide(context.Rip), name, wide(offset));
		if (!Readable(context.Rsp, 8)) {
			break;
		}
		DWORD64 image = 0;
		if (auto* function = RtlLookupFunctionEntry(context.Rip, &image, nullptr); function != nullptr) {
			void*   handler_data = nullptr;
			DWORD64 establisher  = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &handler_data,
			                 &establisher, nullptr);
		} else if (frame == 0) {
			// A leaf function: the return address is on top of the stack. (Further up, code
			// without unwind data is the guest's or generated: the walk ends there.)
			context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
			context.Rsp += 8;
		} else {
			break;
		}
	}
}

// The modules in the process that are not Windows' own (its driver store counts as not its
// own: the graphics driver and NVIDIA's NGX live there), by file name: overlays and hooks of
// other programs show here.
static void ListForeignModules(CrashText* text) noexcept {
	using Enumerate = BOOL(WINAPI*)(HANDLE, HMODULE*, DWORD, LPDWORD);
	const auto enumerate = reinterpret_cast<Enumerate>(reinterpret_cast<void*>(
	    GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32EnumProcessModules")));
	static HMODULE modules[1024];
	DWORD          bytes = 0;
	if (enumerate == nullptr || enumerate(GetCurrentProcess(), modules, sizeof(modules), &bytes) == 0) {
		return;
	}
	char       windows[MAX_PATH] = "";
	const auto windows_length    = GetWindowsDirectoryA(windows, MAX_PATH);
	for (DWORD index = 0; index < bytes / sizeof(HMODULE) && index < 1024; ++index) {
		char path[MAX_PATH] = "";
		if (GetModuleFileNameA(modules[index], path, MAX_PATH) == 0) {
			continue;
		}
		const bool in_windows = windows_length != 0 && _strnicmp(path, windows, windows_length) == 0;
		if (in_windows && std::strstr(path, "DriverStore") == nullptr) {
			continue;
		}
		const char* file = std::strrchr(path, '\\');
		Append(text, "%s%s", text->used == 0 ? "" : ", ", file != nullptr ? file + 1 : path);
	}
}

// (Its own function: a function with a __try holds no object with a destructor.)
static void DescribeCrashGuarded(const CONTEXT* at, CrashText* text, CrashText* modules) noexcept {
	__try {
		DescribeCrash(at, text);
		ListForeignModules(modules);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

// What ended the process and in which module, to the console and the log (flushed): a crash
// inside a graphics driver, an overlay or NVIDIA's runtime while the emulator starts otherwise
// leaves a log that stops without a reason. Once.
static void WriteCrashNote(PEXCEPTION_POINTERS exception) noexcept {
	static std::atomic<bool> written {false};
	if (written.exchange(true)) {
		return;
	}
	const auto* record  = exception->ExceptionRecord;
	const auto  address = reinterpret_cast<uint64_t>(record->ExceptionAddress);
	char        name[MAX_PATH];
	uint64_t    offset = 0;
	NameAddress(address, name, sizeof(name), &offset);
	char access[96] = "";
	if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
		std::snprintf(access, sizeof(access), ", %s 0x%llx",
		              record->ExceptionInformation[0] == 1   ? "writing"
		              : record->ExceptionInformation[0] == 8 ? "executing"
		                                                     : "reading",
		              static_cast<unsigned long long>(record->ExceptionInformation[1]));
	}
	char text[512];
	std::snprintf(text, sizeof(text),
	              "--- Crash ---\nUnhandled exception 0x%08lx at 0x%llx (%s+0x%llx)%s on thread %lu\n",
	              record->ExceptionCode, static_cast<unsigned long long>(address), name,
	              static_cast<unsigned long long>(offset), access, GetCurrentThreadId());
	Log::WriteToConsoleAndLog(text);
	// (After the line above is on disk: static, a crash may come with little stack left.)
	static char detail[8192];
	static char modules[8192];
	CrashText   detail_text {.data = detail, .size = sizeof(detail)};
	CrashText   modules_text {.data = modules, .size = sizeof(modules)};
	DescribeCrashGuarded(exception->ContextRecord, &detail_text, &modules_text);
	Log::WriteToConsoleAndLog(detail);
	if (modules[0] != '\0') {
		Log::WriteToConsoleAndLog("Modules not Windows' own: ");
		Log::WriteToConsoleAndLog(modules);
		Log::WriteToConsoleAndLog("\n");
	}
}

static LONG WINAPI CrashNoteFilter(PEXCEPTION_POINTERS exception) noexcept {
	WriteCrashNote(exception);
	return EXCEPTION_CONTINUE_SEARCH;
}

void InstallCrashNote() {
	SetUnhandledExceptionFilter(CrashNoteFilter);
}

void CrashTest() {
	if (std::getenv("KYTY_CRASH_TEST") == nullptr) {
		return;
	}
	// (Windows calls back from its own module: a foreign frame between the fault and its caller,
	// as with a driver's or a layer's call.)
	EnumSystemLocalesEx(
	    [](LPWSTR, DWORD, LPARAM) -> BOOL {
		    const volatile auto* invalid = reinterpret_cast<const volatile int*>(0xdead0000beef0000ull);
		    return *invalid;
	    },
	    LOCALE_ALL, 0, nullptr);
}

// The top-level filter: runs once the structured exception handler search found no handler.
static LONG WINAPI UnhandledExceptionFilterReport(PEXCEPTION_POINTERS exception) noexcept {
	WriteCrashNote(exception);
	ExceptionInfo info {};
	if (const auto report = g_reporter.load(std::memory_order_acquire);
	    report != nullptr && ReadException(exception, &info)) {
		report(info);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

bool InstallUnhandledReporter(Reporter report) {
	g_reporter.store(report, std::memory_order_release);
	SetUnhandledExceptionFilter(UnhandledExceptionFilterReport);
	return true;
}

bool LeaveToModuleHandlers(const ExceptionInfo& info, bool guest_thread) {
	if (guest_thread || g_reporter.load(std::memory_order_acquire) == nullptr) {
		return false;
	}
	// Guest code and code the host generates at run time belong to no module; the executable's
	// own faults are its handler's to report.
	void* module = nullptr;
	return RtlPcToFileHeader(reinterpret_cast<void*>(info.exception_address), &module) != nullptr &&
	       module != static_cast<void*>(GetModuleHandleW(nullptr));
}

#else

// x86-64 page-fault error code: bit 4 = instruction fetch, bit 1 = write, otherwise a read.
static AccessViolationType DecodeAccess(const ucontext_t* context) {
#if defined(__APPLE__)
	const auto error = static_cast<uint64_t>(context->uc_mcontext->__es.__err);
#else
	const auto error = static_cast<uint64_t>(context->uc_mcontext.gregs[REG_ERR]);
#endif
	if ((error & 0x10u) != 0) {
		return AccessViolationType::Execute;
	}
	if ((error & 0x2u) != 0) {
		return AccessViolationType::Write;
	}
	return AccessViolationType::Read;
}

// A resolved fault returns and retries the instruction; an unresolved one restores the default
// action so the retry terminates the process.
static void SignalHandler(int signal_number, siginfo_t* signal_info, void* native_context) {
	auto* context = static_cast<ucontext_t*>(native_context);

	ExceptionInfo info {};
#if defined(__APPLE__)
	const auto& registers  = context->uc_mcontext->__ss;
	info.exception_address = registers.__rip;
	info.rax               = registers.__rax;
	info.rbx               = registers.__rbx;
	info.rcx               = registers.__rcx;
	info.rdx               = registers.__rdx;
	info.rsi               = registers.__rsi;
	info.rdi               = registers.__rdi;
	info.rbp               = registers.__rbp;
	info.rsp               = registers.__rsp;
	info.r8                = registers.__r8;
	info.r9                = registers.__r9;
	info.r10               = registers.__r10;
	info.r11               = registers.__r11;
	info.r12               = registers.__r12;
	info.r13               = registers.__r13;
	info.r14               = registers.__r14;
	info.r15               = registers.__r15;
#else
	const auto* registers  = context->uc_mcontext.gregs;
	info.exception_address = static_cast<uint64_t>(registers[REG_RIP]);
	info.rax               = static_cast<uint64_t>(registers[REG_RAX]);
	info.rbx               = static_cast<uint64_t>(registers[REG_RBX]);
	info.rcx               = static_cast<uint64_t>(registers[REG_RCX]);
	info.rdx               = static_cast<uint64_t>(registers[REG_RDX]);
	info.rsi               = static_cast<uint64_t>(registers[REG_RSI]);
	info.rdi               = static_cast<uint64_t>(registers[REG_RDI]);
	info.rbp               = static_cast<uint64_t>(registers[REG_RBP]);
	info.rsp               = static_cast<uint64_t>(registers[REG_RSP]);
	info.r8                = static_cast<uint64_t>(registers[REG_R8]);
	info.r9                = static_cast<uint64_t>(registers[REG_R9]);
	info.r10               = static_cast<uint64_t>(registers[REG_R10]);
	info.r11               = static_cast<uint64_t>(registers[REG_R11]);
	info.r12               = static_cast<uint64_t>(registers[REG_R12]);
	info.r13               = static_cast<uint64_t>(registers[REG_R13]);
	info.r14               = static_cast<uint64_t>(registers[REG_R14]);
	info.r15               = static_cast<uint64_t>(registers[REG_R15]);
#endif
	info.native_code    = static_cast<uint32_t>(signal_number);
	info.native_context = context;
	if (signal_number == SIGILL) {
		info.type = ExceptionType::IllegalInstruction;
	} else {
		info.type                   = ExceptionType::AccessViolation;
		info.access_violation_type  = DecodeAccess(context);
		info.access_violation_vaddr = reinterpret_cast<uint64_t>(signal_info->si_addr);
	}

	const auto handler = g_handler.load(std::memory_order_acquire);
	if (handler != nullptr && handler(info)) {
		return;
	}

	struct sigaction restore {};
	restore.sa_handler = SIG_DFL;
	sigemptyset(&restore.sa_mask);
	::sigaction(signal_number, &restore, nullptr);
}

#endif

static bool InstallPlatformHandler() {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	if (AddVectoredExceptionHandler(0, ExceptionFilter) == nullptr) {
		printf("AddVectoredExceptionHandler() failed\n");
		return false;
	}
#else
	struct sigaction action {};
	action.sa_sigaction = SignalHandler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
	sigaddset(&action.sa_mask, GuestSignalDispatchSignal());

	// macOS raises SIGBUS for some protection faults; SIGILL routes instructions the host
	// cannot execute to the x64 emulator.
	for (const int signal_number: {SIGSEGV, SIGBUS, SIGILL}) {
		if (::sigaction(signal_number, &action, nullptr) != 0) {
			printf("sigaction(%d) failed\n", signal_number);
			return false;
		}
	}
#endif
	return true;
}

#if KYTY_PLATFORM != KYTY_PLATFORM_WINDOWS
bool InstallUnhandledReporter(Reporter /*report*/) {
	return false;
}

void InstallCrashNote() {}

void CrashTest() {}

bool LeaveToModuleHandlers(const ExceptionInfo& /*info*/, bool /*guest_thread*/) {
	return false;
}
#endif

// The first handler stays installed; later calls only report whether it is theirs.
bool InstallHandler(Handler handler) {
	static const bool installed = [handler] {
		g_handler.store(handler, std::memory_order_release);
		return InstallPlatformHandler();
	}();
	return installed && g_handler.load(std::memory_order_acquire) == handler;
}

} // namespace Common::HostException
