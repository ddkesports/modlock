// Contract tests for the WebAssembly mod sandbox: the Go example mod answers
// events through the protobuf boundary, and a trap, a runaway loop, an
// oversized memory or a bad boundary call stops only that mod.
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "wasm/instance.h"

namespace {

using modlock::wasm::Event;
using modlock::wasm::HostRequest;
using modlock::wasm::HostResponse;
using modlock::wasm::Instance;
using modlock::wasm::Limits;

// ReadFile returns the bytes of a built module.
std::vector<uint8_t> ReadFile(const char* path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Wat compiles WebAssembly text to a module.
std::vector<uint8_t> Wat(std::string_view text) {
  auto module = wasmtime::wat2wasm(text);
  EXPECT_TRUE(module) << module.err().message();
  return module.ok();
}

// Recorder answers host calls and keeps every request.
struct Recorder {
  std::vector<HostRequest> requests;

  Instance::HostCall Call() {
    return [this](const HostRequest& request) {
      requests.push_back(request);
      return HostResponse{};
    };
  }
};

Event Command(int32_t slot, std::string line) {
  Event event;
  event.mutable_command()->set_slot(slot);
  event.mutable_command()->set_line(std::move(line));
  return event;
}

TEST(WasmInstance, GoModAnswersEvents) {
  Recorder recorder;
  auto instance = Instance::Load(ReadFile(HELLO_GO_WASM), Limits{}, recorder.Call());
  ASSERT_TRUE(instance) << instance.error();

  // Start asks for frames because the mod registered a frame handler.
  Event start;
  start.mutable_start();
  auto started = (*instance)->Deliver(start);
  ASSERT_TRUE(started) << started.error();
  EXPECT_TRUE(started->start().frames());

  // The hello command is claimed and greets its player.
  auto hello = (*instance)->Deliver(Command(3, "hello  there"));
  ASSERT_TRUE(hello) << hello.error();
  EXPECT_TRUE(hello->command().claimed());
  ASSERT_EQ(recorder.requests.size(), 1);
  EXPECT_EQ(recorder.requests[0].chat().slot(), 3);
  EXPECT_EQ(recorder.requests[0].chat().text(), "Hello from Go!");

  // Other commands pass through to the game.
  auto other = (*instance)->Deliver(Command(3, "say hi"));
  ASSERT_TRUE(other) << other.error();
  EXPECT_FALSE(other->command().claimed());

  // The first frame is logged once.
  Event frame;
  frame.mutable_frame()->set_tick(7);
  ASSERT_TRUE((*instance)->Deliver(frame));
  ASSERT_TRUE((*instance)->Deliver(frame));
  ASSERT_EQ(recorder.requests.size(), 2);
  EXPECT_EQ(recorder.requests[1].log().message(), "first frame at tick 7");
}

TEST(WasmInstance, TrapStopsTheMod) {
  Recorder recorder;
  auto instance = Instance::Load(Wat(R"((module
    (memory (export "memory") 1)
    (func (export "modlock_event") (param i32) (result i64) unreachable)))"),
                                 Limits{}, recorder.Call());
  ASSERT_TRUE(instance) << instance.error();

  Event frame;
  frame.mutable_frame();
  EXPECT_FALSE((*instance)->Deliver(frame));
  ASSERT_TRUE((*instance)->Failure());
  EXPECT_FALSE((*instance)->Deliver(frame));
}

TEST(WasmInstance, BudgetStopsARunawayLoop) {
  Recorder recorder;
  Limits limits;
  limits.event_budget = std::chrono::milliseconds{50};
  auto instance = Instance::Load(Wat(R"((module
    (memory (export "memory") 1)
    (func (export "modlock_event") (param i32) (result i64) (loop br 0) i64.const 0)))"),
                                 limits, recorder.Call());
  ASSERT_TRUE(instance) << instance.error();

  Event frame;
  frame.mutable_frame();
  const auto began = std::chrono::steady_clock::now();
  EXPECT_FALSE((*instance)->Deliver(frame));
  EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::seconds{2});
  EXPECT_TRUE((*instance)->Failure());
}

TEST(WasmInstance, MemoryLimitRefusesALargeMod) {
  Recorder recorder;
  Limits limits;
  limits.memory_bytes = 1 << 20;
  auto instance = Instance::Load(Wat(R"((module
    (memory (export "memory") 64)
    (func (export "modlock_event") (param i32) (result i64) i64.const 0)))"),
                                 limits, recorder.Call());
  EXPECT_FALSE(instance);
}

TEST(WasmInstance, RequestOutsideMemoryStopsTheMod) {
  Recorder recorder;
  auto instance = Instance::Load(Wat(R"((module
    (import "modlock" "host_call" (func $call (param i32 i32) (result i32)))
    (memory (export "memory") 1)
    (func (export "modlock_event") (param i32) (result i64)
      (drop (call $call (i32.const 65530) (i32.const 100)))
      i64.const 0)))"),
                                 Limits{}, recorder.Call());
  ASSERT_TRUE(instance) << instance.error();

  Event frame;
  frame.mutable_frame();
  auto delivered = (*instance)->Deliver(frame);
  ASSERT_FALSE(delivered);
  EXPECT_NE(delivered.error().find("outside memory"), std::string::npos) << delivered.error();
  EXPECT_TRUE(recorder.requests.empty());
}

}  // namespace
