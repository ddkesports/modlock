#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <wasmtime.hh>

#include "proto/modlock/wasm.pb.h"

namespace modlock::wasm {

// Limits bounds what one mod may consume. A mod that exceeds a limit traps,
// and only that mod stops.
struct Limits {
  // memory_bytes caps each linear memory the mod creates or grows.
  int64_t memory_bytes = int64_t{256} << 20;
  // start_budget bounds module initialization, which runs package setup.
  std::chrono::milliseconds start_budget{5000};
  // event_budget bounds one event, including its host calls.
  std::chrono::milliseconds event_budget{100};
};

// Instance runs one WebAssembly mod in its own Wasmtime store. The mod sees
// WASI preview 1 with standard output and error but no files, environment,
// arguments or network, and the two modlock imports: host_call, which hands
// the host a HostRequest, and host_read, which copies the host's pending Event
// or HostResponse into mod memory. Deliver calls the mod's modlock_event
// export. A trap, an exhausted budget or a boundary violation fails the
// instance permanently; later deliveries return that failure.
//
// All methods except construction run on one thread at a time, normally the
// engine thread. The epoch ticker is the only other thread.
class Instance {
 public:
  // HostCall answers one request from the mod. It runs inside Deliver or
  // Load on the calling thread.
  using HostCall = std::function<HostResponse(const HostRequest&)>;

  ~Instance();
  Instance(const Instance&) = delete;
  Instance& operator=(const Instance&) = delete;

  // Load compiles and instantiates module, then runs its _initialize export
  // within limits.start_budget. host_call answers the mod's requests for the
  // instance lifetime, starting during initialization.
  [[nodiscard]] static std::expected<std::unique_ptr<Instance>, std::string> Load(
      std::span<const uint8_t> module, const Limits& limits, HostCall host_call);

  // Deliver hands event to the mod and returns its answer. A nested delivery
  // from inside a host call is refused without failing the instance.
  [[nodiscard]] std::expected<EventResult, std::string> Deliver(const Event& event);

  // Failure returns the reason the instance stopped, or nullopt while it runs.
  [[nodiscard]] const std::optional<std::string>& Failure() const { return failure_; }

 private:
  Instance(wasmtime::Engine engine, const Limits& limits, HostCall host_call);

  std::expected<void, std::string> Instantiate(std::span<const uint8_t> module);
  std::expected<void, std::string> DefineImports(wasmtime::Linker& linker);
  wasmtime::Result<uint32_t, wasmtime::Trap> HostCallImport(wasmtime::Caller caller, uint32_t data,
                                                            uint32_t size);
  wasmtime::Result<std::monostate, wasmtime::Trap> HostReadImport(wasmtime::Caller caller,
                                                                  uint32_t data, uint32_t size);
  void SetBudget(std::chrono::milliseconds budget);
  std::string Fail(std::string reason);

  // engine_ compiles the module and carries the epoch the ticker advances.
  wasmtime::Engine engine_;
  Limits limits_;
  HostCall host_call_;
  // store_ owns the instance, its memory and its WASI state.
  wasmtime::Store store_;
  std::optional<wasmtime::Memory> memory_;
  std::optional<wasmtime::Func> event_;
  // pending_ holds the encoded message host_read copies next.
  std::string pending_;
  // busy_ is true while a call into the mod is running.
  bool busy_ = false;
  std::optional<std::string> failure_;
  // ticker_ advances the engine epoch; it is last so it stops first.
  std::jthread ticker_;
};

}  // namespace modlock::wasm
