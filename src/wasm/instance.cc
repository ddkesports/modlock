#include "wasm/instance.h"

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <utility>
#include <variant>

namespace modlock::wasm {
namespace {

// kEpochTick is how often the ticker advances the engine epoch; budgets are
// rounded up to whole ticks.
constexpr std::chrono::milliseconds kEpochTick{10};

// Bytes returns the mod memory range [data, data + size), or nullopt when the
// range leaves memory.
std::optional<std::span<uint8_t>> Bytes(wasmtime::Span<uint8_t> memory, uint32_t data,
                                        uint32_t size) {
  if (uint64_t{data} + size > memory.size()) return std::nullopt;
  return std::span<uint8_t>(memory.data() + data, size);
}

// CallerBytes resolves a range of the calling mod's exported memory.
std::optional<std::span<uint8_t>> CallerBytes(wasmtime::Caller& caller, uint32_t data,
                                              uint32_t size) {
  auto exported = caller.get_export("memory");
  if (!exported) return std::nullopt;
  auto* memory = std::get_if<wasmtime::Memory>(&*exported);
  if (!memory) return std::nullopt;
  return Bytes(memory->data(caller.context()), data, size);
}

}  // namespace

Instance::Instance(wasmtime::Engine engine, const Limits& limits, HostCall host_call)
    : engine_(std::move(engine)),
      limits_(limits),
      host_call_(std::move(host_call)),
      store_(engine_) {
  store_.limiter(limits_.memory_bytes, -1, -1, -1, -1);

  // Advance the epoch so a running call meets its deadline.
  ticker_ = std::jthread([engine = engine_](std::stop_token stop) {
    std::mutex mu;
    std::condition_variable_any wake;
    std::unique_lock lock(mu);
    auto stopped = [&stop] { return stop.stop_requested(); };
    while (!wake.wait_for(lock, stop, kEpochTick, stopped)) engine.increment_epoch();
  });
}

Instance::~Instance() = default;

std::expected<std::unique_ptr<Instance>, std::string> Instance::Load(
    std::span<const uint8_t> module, const Limits& limits, HostCall host_call) {
  wasmtime::Config config;
  config.epoch_interruption(true);
  std::unique_ptr<Instance> instance(
      new Instance(wasmtime::Engine(std::move(config)), limits, std::move(host_call)));
  if (auto instantiated = instance->Instantiate(module); !instantiated) {
    return std::unexpected(instantiated.error());
  }
  return instance;
}

std::expected<void, std::string> Instance::Instantiate(std::span<const uint8_t> module) {
  // Compile the module; Wasmtime only reads the bytes.
  auto compiled = wasmtime::Module::compile(
      engine_, wasmtime::Span<uint8_t>(const_cast<uint8_t*>(module.data()), module.size()));
  if (!compiled) return std::unexpected("cannot compile the mod: " + compiled.err().message());

  // Give the mod standard output and error and nothing else from WASI.
  wasmtime::WasiConfig wasi;
  wasi.inherit_stdout();
  wasi.inherit_stderr();
  if (auto set = store_.context().set_wasi(std::move(wasi)); !set) {
    return std::unexpected("cannot configure WASI: " + set.err().message());
  }

  // Link WASI and the modlock imports, then instantiate.
  wasmtime::Linker linker(engine_);
  if (auto defined = linker.define_wasi(); !defined) {
    return std::unexpected("cannot define WASI: " + defined.err().message());
  }
  if (auto defined = DefineImports(linker); !defined) return defined;
  SetBudget(limits_.start_budget);
  auto instance = linker.instantiate(store_.context(), compiled.ok_ref());
  if (!instance) return std::unexpected("cannot instantiate the mod: " + instance.err().message());

  // Find the memory and the event entry point the boundary needs.
  auto context = store_.context();
  auto memory = instance.ok_ref().get(context, "memory");
  if (memory) {
    if (auto* exported = std::get_if<wasmtime::Memory>(&*memory)) memory_ = *exported;
  }
  auto event = instance.ok_ref().get(context, "modlock_event");
  if (event) {
    if (auto* exported = std::get_if<wasmtime::Func>(&*event)) event_ = *exported;
  }
  if (!memory_ || !event_) {
    return std::unexpected("the mod must export memory and modlock_event");
  }

  // Run the reactor's initialization, which registers the mod's handlers.
  auto initialize = instance.ok_ref().get(context, "_initialize");
  auto* function = initialize ? std::get_if<wasmtime::Func>(&*initialize) : nullptr;
  if (!function) return {};
  busy_ = true;
  auto initialized = function->call(context, {});
  busy_ = false;
  if (!initialized) return std::unexpected(Fail(initialized.err().message()));
  return {};
}

std::expected<void, std::string> Instance::DefineImports(wasmtime::Linker& linker) {
  auto call = linker.func_wrap("modlock", "host_call",
                               [this](wasmtime::Caller caller, uint32_t data, uint32_t size) {
                                 return HostCallImport(caller, data, size);
                               });
  if (!call) return std::unexpected("cannot define host_call: " + call.err().message());
  auto read = linker.func_wrap("modlock", "host_read",
                               [this](wasmtime::Caller caller, uint32_t data, uint32_t size) {
                                 return HostReadImport(caller, data, size);
                               });
  if (!read) return std::unexpected("cannot define host_read: " + read.err().message());
  return {};
}

std::expected<EventResult, std::string> Instance::Deliver(const Event& event) {
  if (failure_) return std::unexpected(*failure_);
  if (busy_) return std::unexpected("the mod is already handling an event");

  // Call the mod, which copies the event out with host_read.
  auto typed = event_->typed<uint32_t, uint64_t>(store_.context());
  if (!typed) return std::unexpected(Fail("modlock_event must take i32 and return i64"));
  event.SerializeToString(&pending_);
  SetBudget(limits_.event_budget);
  busy_ = true;
  auto called = typed.ok_ref().call(store_.context(), static_cast<uint32_t>(pending_.size()));
  busy_ = false;
  pending_.clear();
  if (!called) return std::unexpected(Fail(called.err().message()));

  // Decode the answer the mod left in its memory.
  const uint64_t packed = called.ok();
  EventResult result;
  if (packed == 0) return result;
  auto bytes = Bytes(memory_->data(store_.context()), static_cast<uint32_t>(packed >> 32),
                     static_cast<uint32_t>(packed));
  if (!bytes) return std::unexpected(Fail("modlock_event returned a result outside memory"));
  if (!result.ParseFromArray(bytes->data(), static_cast<int>(bytes->size()))) {
    return std::unexpected(Fail("modlock_event returned an invalid EventResult"));
  }
  return result;
}

wasmtime::Result<uint32_t, wasmtime::Trap> Instance::HostCallImport(wasmtime::Caller caller,
                                                                    uint32_t data, uint32_t size) {
  // Decode the request from mod memory.
  auto bytes = CallerBytes(caller, data, size);
  if (!bytes) return wasmtime::Trap("modlock.host_call: the request is outside memory");
  HostRequest request;
  if (!request.ParseFromArray(bytes->data(), static_cast<int>(bytes->size()))) {
    return wasmtime::Trap("modlock.host_call: invalid HostRequest");
  }

  // Answer it and hold the encoded response for host_read.
  host_call_(request).SerializeToString(&pending_);
  return static_cast<uint32_t>(pending_.size());
}

wasmtime::Result<std::monostate, wasmtime::Trap> Instance::HostReadImport(wasmtime::Caller caller,
                                                                          uint32_t data,
                                                                          uint32_t size) {
  if (size != pending_.size()) {
    return wasmtime::Trap("modlock.host_read: the size differs from the pending message");
  }
  auto bytes = CallerBytes(caller, data, size);
  if (!bytes) return wasmtime::Trap("modlock.host_read: the buffer is outside memory");
  std::memcpy(bytes->data(), pending_.data(), size);
  pending_.clear();
  return std::monostate{};
}

void Instance::SetBudget(std::chrono::milliseconds budget) {
  const uint64_t ticks = (budget + kEpochTick - std::chrono::milliseconds{1}) / kEpochTick;
  store_.context().set_epoch_deadline(ticks);
}

std::string Instance::Fail(std::string reason) {
  failure_ = reason;
  return reason;
}

}  // namespace modlock::wasm
