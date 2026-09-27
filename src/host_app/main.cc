#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "modlock/build.h"
#include "modlock/engine_host.h"
#include "modlock/host.h"
#include "modlock/host_app/stdio_guard.h"
#include "modlock/plugin_library.h"

namespace {

void Help() {
  std::cout << "Modlock hosts native Deadlock plugins.\n\n"
               "Usage: modlock-host --plugin PATH [options] [-- plugin arguments]\n\n"
               "  --plugin PATH       Load a plugin library; may be repeated\n"
               "  --game-dir PATH     Deadlock installation (or DEADLOCK_DIR)\n"
               "  --hostport PORT     Server UDP port (default 27067)\n"
               "  --map NAME          Startup map (default dl_midtown)\n"
               "  --connect ADDRESS   Run a game client that joins ADDRESS instead of a server\n"
               "  --engine-args ARGS  Append engine command-line arguments\n"
               "  --check-plugin      Check library compatibility and lifecycle without a game\n"
               "  --version           Print the framework source revision\n"
               "  --help              Show this help\n";
}

std::optional<uint16_t> Port(std::string_view value) {
  unsigned port = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), port);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || port > 65535) {
    return std::nullopt;
  }
  return static_cast<uint16_t>(port);
}

}  // namespace

int main(int argc, char** argv) {
  modlock::host_app::InstallStdioGuard();
  std::cout << std::unitbuf;
  std::vector<std::filesystem::path> libraries;
  std::filesystem::path game_dir;
  modlock::net::LaunchConfig launch;
  bool check_only = false;
  int plugin_argc = 0;
  const char* const* plugin_argv = nullptr;
  if (const char* environment = std::getenv("DEADLOCK_DIR")) game_dir = environment;
  if (const char* environment = std::getenv("MODLOCK_HOST_PORT")) {
    auto port = Port(environment);
    if (!port) {
      std::cerr << "MODLOCK_HOST_PORT must be an integer from 0 to 65535.\n";
      return 2;
    }
    launch.host_port = *port;
  }
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") {
      Help();
      return 0;
    }
    if (argument == "--version") {
      std::cout << "Modlock " << MODLOCK_SOURCE_REVISION << '\n';
      return 0;
    }
    if (argument == "--check-plugin") {
      check_only = true;
      continue;
    }
    if (argument == "--") {
      plugin_argc = argc - index;
      plugin_argv = argv + index;
      break;
    }
    if (argument != "--plugin" && argument != "--game-dir" && argument != "--hostport" &&
        argument != "--map" && argument != "--connect" && argument != "--engine-args") {
      std::cerr << "Unknown option: " << argument << ". Use --help for usage.\n";
      return 2;
    }
    if (++index == argc) {
      std::cerr << argument << " needs a value.\n";
      return 2;
    }
    const std::string_view value(argv[index]);
    if (argument == "--plugin") libraries.emplace_back(value);
    if (argument == "--engine-args") launch.engine_arguments = value;
    if (argument == "--connect") {
      if (value.empty() ||
          value.find_first_not_of(
              "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-") !=
              std::string_view::npos) {
        std::cerr << "The connect address must be a host name or address with an optional port.\n";
        return 2;
      }
      launch.connect = value;
    }
    if (argument == "--game-dir") game_dir = value;
    if (argument == "--map") {
      if (value.empty() ||
          value.find_first_not_of(
              "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/-") !=
              std::string_view::npos) {
        std::cerr << "The map must be a Source 2 map name.\n";
        return 2;
      }
      launch.map = value;
    }
    if (argument == "--hostport") {
      auto port = Port(value);
      if (!port) {
        std::cerr << "The host port must be an integer from 0 to 65535.\n";
        return 2;
      }
      launch.host_port = *port;
    }
  }
  if (libraries.empty()) {
    std::cerr << "Select a plugin with --plugin PATH. Use --help for usage.\n";
    return 2;
  }
  modlock::EngineHost engine;
  if (!check_only) {
    if (game_dir.empty()) {
      std::cerr << "Set DEADLOCK_DIR or pass --game-dir with your Deadlock installation.\n";
      return 2;
    }
    if (auto opened = engine.Open(std::filesystem::absolute(game_dir), launch); !opened) {
      std::cerr << opened.error() << '\n';
      return 1;
    }
  }

  // The host destroys plugin instances and libraries before engine resources.
  modlock::host::PluginHost plugins;
  const modlock::PluginContext context{.engine = &engine,
                                       .argc = plugin_argc,
                                       .argv = plugin_argv,
                                       .check_only = check_only,
                                       .launch = &launch};
  for (const auto& library : libraries) {
    auto plugin = modlock::LoadPluginLibrary(std::filesystem::absolute(library), context);
    if (!plugin) {
      std::cerr << plugin.error() << '\n';
      return 1;
    }
    if (auto error = plugins.Register(std::move(*plugin))) {
      std::cerr << *error << '\n';
      return 1;
    }
  }
  std::string error;
  if (!plugins.StartAll(error)) {
    std::cerr << error << '\n';
    return 1;
  }
  if (check_only) {
    plugins.TickAll();
    plugins.StopAll();
    std::cout << "Plugin compatibility and lifecycle check passed. No game was started.\n";
    return 0;
  }
  auto frames = engine.OnFrame([&plugins] { plugins.TickAll(); });
  if (!frames) {
    std::cerr << frames.error() << '\n';
    return 1;
  }
  auto result = engine.Run(launch);
  frames->Reset();
  plugins.StopAll();
  if (!result) {
    std::cerr << result.error() << '\n';
    return 1;
  }
  return plugins.ExitCode(*result);
}
