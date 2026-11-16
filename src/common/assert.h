#ifndef KYTY_COMMON_ASSERT_H_
#define KYTY_COMMON_ASSERT_H_

#include "common/common.h"
#include "common/logging/log.h"

#include <string>
#include <string_view>

namespace Common {

// Thrown instead of ending the process by EXIT, EXIT_IF and EXIT_NOT_IMPLEMENTED on a thread
// that is inside a RecoverableFatalScope.
struct RecoverableFatal {
	std::string message;
};

// Makes the fatal checks of speculative work on this thread recoverable: a speculative shader
// translation may meet code the recompiler cannot handle, which no guest ever runs. Only for
// self-contained work whose partial state the unwinding discards; nests.
class RecoverableFatalScope {
public:
	RecoverableFatalScope();
	~RecoverableFatalScope();
	RecoverableFatalScope(const RecoverableFatalScope&)            = delete;
	RecoverableFatalScope& operator=(const RecoverableFatalScope&) = delete;
};

// Ends the process after a fatal error, at once: no exit handlers, and on Windows no DLL detach
// routines either. The caller has written and flushed what it wants kept.
[[noreturn]] void FatalExit(int status) noexcept;

// std::quick_exit for this process: once ExitWithoutDetach has been called, the process ends
// after the quick-exit handlers as FatalExit ends it. For DLLs that do not survive ExitProcess
// (NVIDIA Streamline's shut their runtime down in their detach routines, after the threads they
// need are gone: an access violation at every exit).
// RegisterQuickExit is main's first statement: quick-exit handlers run in reverse order, and the
// one it registers has to run last.
void              RegisterQuickExit();
void              ExitWithoutDetach() noexcept;
[[noreturn]] void QuickExit(int status);

[[noreturn]] void DbgExitHandler(char const* file, int line, std::string_view text);
[[noreturn]] void DbgExitIfHandler(char const* expr, char const* file, int line);
[[noreturn]] void DbgNotImplementedHandler(char const* expr, char const* file, int line);

} // namespace Common

#ifndef KYTY_FINAL
#define EXIT_IF(x) ((x) ? Common::DbgExitIfHandler(#x, __FILE__, __LINE__) : (void)0)
#else
#define EXIT_IF(x) ((void)(false && (x)))
#endif
#define EXIT(...) Common::DbgExitHandler(__FILE__, __LINE__, ::fmt::sprintf(__VA_ARGS__))
#define EXIT_NOT_IMPLEMENTED(x)                                                                    \
	((x) ? Common::DbgNotImplementedHandler(#x, __FILE__, __LINE__) : (void)0)

#endif /* KYTY_COMMON_ASSERT_H_ */
