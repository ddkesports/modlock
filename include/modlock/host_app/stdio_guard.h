#pragma once

#include "modlock/export.h"

namespace modlock::host_app {

// Identifies one of the three standard streams the guard repairs.
enum class StdioStream { kInput, kOutput, kError };

// Reports whether the stream's underlying standard handle is usable. The
// default check validates the real process handles; tests inject fakes so
// no console is required.
using StdioHandleCheck = bool (*)(StdioStream);

// DefaultStdioHandleCheck asks Windows for STD_INPUT_HANDLE,
// STD_OUTPUT_HANDLE, and STD_ERROR_HANDLE and rejects both null and
// INVALID_HANDLE_VALUE results. Redirected spawns deliver null handles,
// which leave the CRT descriptors closed and turn later iostream use into
// invalid-parameter fast-fails. Off Windows standard descriptors always
// exist, so it always reports valid and the guard stays a no-op.
MODLOCK_API bool DefaultStdioHandleCheck(StdioStream stream);

// InstallStdioGuard reopens every standard stream whose handle fails
// `check` from the null device, so reads return end-of-stream and writes
// are discarded instead of terminating the host. It also installs a no-op
// CRT invalid-parameter handler as defense-in-depth against any closed
// descriptor the reopen cannot see. Call it once at the top of main(),
// before any iostream operation.
MODLOCK_API void InstallStdioGuard(StdioHandleCheck check = DefaultStdioHandleCheck);

}  // namespace modlock::host_app
