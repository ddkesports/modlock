#include "modlock/wasm_plugin.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

#include "modlock/engine_host.h"
#include "modlock/gameinterop/engine_server.h"
#include "modlock/gameinterop/native_user_messages.h"
#include "wasm/instance.h"

namespace modlock {
namespace {

// WasmPlugin connects one sandboxed mod to the engine. Frames arrive through
// Tick and player commands through an OnCommand subscription; both run on the
// engine thread, as do the mod's host calls.
class WasmPlugin final : public Plugin {
 public:
  WasmPlugin(std::string name, const PluginContext& context)
      : name_(std::move(name)), engine_(context.engine), check_only_(context.check_only) {
    for (int i = 1; i < context.argc; ++i) args_.emplace_back(context.argv[i]);
  }

  // Attach takes the loaded mod; LoadWasmPlugin calls it once.
  void Attach(std::unique_ptr<wasm::Instance> instance) { instance_ = std::move(instance); }

  // Call answers one request from the mod.
  wasm::HostResponse Call(const wasm::HostRequest& request);

  uint32_t InterfaceVersion() const override { return PluginInterfaceVersion; }
  const char* Name() const override { return name_.c_str(); }
  bool Start() override;
  void Tick() override;
  void Stop() override;

 private:
  std::optional<wasm::EventResult> Deliver(const wasm::Event& event);
  bool Command(int32_t slot, std::string_view line);
  std::expected<void, std::string> ServerCommand(const std::string& command);
  std::expected<const gameinterop::NativeUserMessages*, std::string> Messages();

  std::string name_;
  // engine_ outlives the plugin; the host owns it.
  EngineHost* engine_;
  bool check_only_;
  std::vector<std::string> args_;
  std::unique_ptr<wasm::Instance> instance_;
  // frames_ is true when the mod asked for a frame event every server frame.
  bool frames_ = false;
  std::optional<gameinterop::EngineServer> server_;
  std::optional<gameinterop::NativeUserMessages> messages_;
  Subscription commands_;
};

bool WasmPlugin::Start() {
  // Start the mod and learn which events it consumes.
  wasm::Event event;
  auto* start = event.mutable_start();
  for (const auto& arg : args_) start->add_args(arg);
  start->set_check_only(check_only_);
  auto result = Deliver(event);
  if (!result) return true;
  frames_ = result->start().frames();
  if (check_only_) return true;

  // Offer the mod every player command.
  auto commands = engine_->OnCommand(
      [this](int32_t slot, std::string_view line) { return Command(slot, line); });
  if (!commands) {
    std::cout << name_ << ": player commands are unavailable: " << commands.error() << '\n';
    return true;
  }
  commands_ = std::move(*commands);
  return true;
}

void WasmPlugin::Tick() {
  if (!frames_ || !instance_) return;
  wasm::Event event;
  auto* frame = event.mutable_frame();
  if (!check_only_) {
    if (!server_) {
      if (auto server = gameinterop::EngineServer::Resolve()) server_ = *server;
    }
    if (server_) {
      if (auto clock = server_->ReadClock()) {
        frame->set_tick(static_cast<uint64_t>(clock->tick));
        frame->set_time_seconds(clock->current_time);
      }
    }
  }
  Deliver(event);
}

void WasmPlugin::Stop() {
  commands_.Reset();
  instance_.reset();
}

std::optional<wasm::EventResult> WasmPlugin::Deliver(const wasm::Event& event) {
  if (!instance_) return std::nullopt;
  auto result = instance_->Deliver(event);
  if (result) return std::move(*result);

  // A failed mod stops alone; a refused nested event leaves it running.
  if (instance_->Failure()) {
    std::cout << name_ << ": stopped: " << result.error() << '\n';
    commands_.Reset();
    instance_.reset();
  }
  return std::nullopt;
}

bool WasmPlugin::Command(int32_t slot, std::string_view line) {
  wasm::Event event;
  auto* command = event.mutable_command();
  command->set_slot(slot);
  command->set_line(std::string(line));
  auto result = Deliver(event);
  return result && result->command().claimed();
}

wasm::HostResponse WasmPlugin::Call(const wasm::HostRequest& request) {
  std::expected<void, std::string> done;
  switch (request.body_case()) {
    case wasm::HostRequest::kLog:
      std::cout << name_ << ": " << request.log().message() << '\n';
      break;
    case wasm::HostRequest::kServerCommand:
      done = ServerCommand(request.server_command().command());
      break;
    case wasm::HostRequest::kChat:
      if (auto messages = Messages()) {
        done = (*messages)->Chat(request.chat().slot(), request.chat().text());
      } else {
        done = std::unexpected(messages.error());
      }
      break;
    case wasm::HostRequest::kCenterText:
      if (auto messages = Messages()) {
        done = (*messages)->CenterText(request.center_text().slot(), request.center_text().text());
      } else {
        done = std::unexpected(messages.error());
      }
      break;
    case wasm::HostRequest::BODY_NOT_SET:
      done = std::unexpected("this host does not support the request");
      break;
  }

  // Report a failed call to the mod and in the server log.
  wasm::HostResponse response;
  if (!done) {
    std::cout << name_ << ": " << done.error() << '\n';
    response.set_error(done.error());
  }
  return response;
}

std::expected<void, std::string> WasmPlugin::ServerCommand(const std::string& command) {
  if (check_only_) return std::unexpected("no game is running");
  if (!server_) {
    auto server = gameinterop::EngineServer::Resolve();
    if (!server) return std::unexpected(server.error());
    server_ = *server;
  }
  if (!server_->ServerCommand((command + '\n').c_str())) {
    return std::unexpected("the server console is unavailable");
  }
  return {};
}

std::expected<const gameinterop::NativeUserMessages*, std::string> WasmPlugin::Messages() {
  if (check_only_) return std::unexpected("no game is running");
  if (!messages_) {
    auto messages = gameinterop::NativeUserMessages::TryCreate();
    if (!messages) return std::unexpected(messages.error());
    messages_ = *messages;
  }
  return &*messages_;
}

}  // namespace

std::expected<std::unique_ptr<Plugin>, std::string> LoadWasmPlugin(
    const std::filesystem::path& path, const PluginContext& context) {
  // Read the module.
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::unexpected("cannot read " + path.string());
  std::vector<uint8_t> module((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

  // Load it into a sandbox that answers through the plugin.
  auto plugin = std::make_unique<WasmPlugin>(path.stem().string(), context);
  auto instance = wasm::Instance::Load(
      module, wasm::Limits{},
      [raw = plugin.get()](const wasm::HostRequest& request) { return raw->Call(request); });
  if (!instance) return std::unexpected(path.filename().string() + ": " + instance.error());
  plugin->Attach(std::move(*instance));
  return plugin;
}

}  // namespace modlock
