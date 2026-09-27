#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace modlock::host_app {

// ShareVConsolePort lets several engine processes run on one machine. The
// engine's VConsole listener (vconcomm.dll) binds TCP 29000 and exits the
// process when that port is taken, and it has no option to choose another
// port. ShareVConsolePort maps vconcomm_dll for the process lifetime and
// patches its bind import: a bind the system refuses because the address is in
// use retries on an ephemeral port, so a second process keeps a working
// VConsole instead of exiting. Call it before the engine starts.
[[nodiscard]] std::expected<void, std::string> ShareVConsolePort(
    const std::filesystem::path& vconcomm_dll);

}  // namespace modlock::host_app
