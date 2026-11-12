/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
// Select the caller's debug boundary independently of the test build type.
#ifdef NDEBUG
#  undef NDEBUG
#endif
#ifdef AE_NO_DEBUG_LOG
#  undef AE_NO_DEBUG_LOG
#endif
#if defined(AE_OBJECTS_TEST_disabled)
#  define AE_NO_DEBUG_LOG 1
#else
#  define AE_NO_DEBUG_LOG 0
#endif
#if defined(AE_OBJECTS_TEST_custom)
inline int custom_calls = 0;
#  define AE_LOG_MACRO(FORMAT_STR, ...) (++custom_calls)
#endif

#include <unity.h>
#include "aether-objects/log.h"
#include "aether-objects/telemetry.h"
#include "aether-tele/raw/backend.h"
#include "aether-tele/raw/decode.h"

#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace ae::test_object_telemetry {
class Backend final : public tele::raw::IBackend {
 public:
  tele::raw::MemoryBackend memory;
  std::vector<std::vector<std::uint8_t>> attempts;
  std::vector<std::uint8_t> console;
  bool fail_next{};
  bool wrong_space{};
  bool Append(std::string_view space,
              std::span<std::uint8_t const> bytes) override {
    if (space != "objects") wrong_space = true;
    attempts.emplace_back(bytes.begin(), bytes.end());
    if (fail_next) {
      fail_next = false;
      return false;
    }
    return memory.Append(space, bytes);
  }
  bool RestoreMetrics(std::string_view space, std::uint32_t writer,
                      std::span<tele::raw::MetricTotal> output,
                      std::size_t& count) override {
    return memory.RestoreMetrics(space, writer, output, count);
  }
  void Console(std::string_view space,
               std::span<std::uint8_t const> bytes) override {
    if (space != "objects") wrong_space = true;
    console.insert(console.end(), bytes.begin(), bytes.end());
  }
};
inline constexpr tele::Tag kTestPoint{60000, objects_tele::kModule,
                                      "TestPoint"};
void Emit([[maybe_unused]] int value) {
  AE_LOG_MACRO_TAGGED(kTestPoint, "object {}", value);
}

void test_TaggedLoggingRuntimeAndDrain() {
  auto backend = std::make_shared<Backend>();
  auto& sink = objects_tele::ObjectTeleSink::Instance();
  TEST_ASSERT_TRUE(sink.SetBackend(backend));
  TEST_ASSERT_TRUE(sink.BindWriter(27));
  sink.SetRuntime({true, true, true, true, tele::raw::kAllFields});
  Emit(42);
  TEST_ASSERT_TRUE(sink.DrainCurrent());
#if defined(AE_OBJECTS_TEST_debug)
  auto bytes = backend->memory.Snapshot("objects");
  auto decoded = tele::raw::Decode(bytes);
  TEST_ASSERT_TRUE(decoded.status == tele::raw::DecodeStatus::kOk);
  unsigned logs = 0;
  for (auto const& record : decoded.records) {
    if (record.kind != tele::raw::Kind::kLog) continue;
    ++logs;
    TEST_ASSERT_EQUAL_UINT(60000, record.index);
    TEST_ASSERT_EQUAL_UINT(27, record.stream);
    std::string payload(record.payload.begin(), record.payload.end());
    TEST_ASSERT_EQUAL_STRING("object 42", payload.c_str());
    TEST_ASSERT_TRUE(record.duration_code != 255);
  }
  TEST_ASSERT_EQUAL_UINT(1, logs);
  auto total = tele::raw::RestoreMetric(decoded.records, 60000);
  TEST_ASSERT_TRUE(total.count == 1 && total.samples == 1);
  TEST_ASSERT_FALSE(backend->console.empty());
  std::ostringstream map;
  TEST_ASSERT_TRUE(sink.ExportMap(map, "objects-test"));
  TEST_ASSERT_TRUE(map.str().find("object {}") != std::string::npos);
  TEST_ASSERT_TRUE(map.str().find("60000") != std::string::npos);
  TEST_ASSERT_TRUE(map.str().find("main.cpp") != std::string::npos);
  sink.SetRuntime({true, false, false, false, 0});
  Emit(43);
  TEST_ASSERT_TRUE(sink.DrainCurrent());
  bytes = backend->memory.Snapshot("objects");
  decoded = tele::raw::Decode(bytes);
  TEST_ASSERT_TRUE(decoded.status == tele::raw::DecodeStatus::kOk);
  TEST_ASSERT_TRUE(decoded.records.back().fields == 0);
  TEST_ASSERT_TRUE(decoded.records.back().payload.empty());
  total = tele::raw::RestoreMetric(decoded.records, 60000);
  TEST_ASSERT_TRUE(total.count == 1 && total.samples == 1);
  sink.SetRuntime({true, false, true, true, tele::raw::kAllFields});
  backend->fail_next = true;
  Emit(44);
  auto const attempt = backend->attempts.size();
  TEST_ASSERT_FALSE(sink.DrainCurrent());
  TEST_ASSERT_TRUE(sink.DrainCurrent());
  TEST_ASSERT_TRUE(backend->attempts[attempt] ==
                   backend->attempts[attempt + 1]);
  bytes = backend->memory.Snapshot("objects");
  decoded = tele::raw::Decode(bytes);
  total = tele::raw::RestoreMetric(decoded.records, 60000);
  TEST_ASSERT_TRUE(total.count == 2 && total.samples == 2);
#else
  TEST_ASSERT_TRUE(backend->memory.Snapshot("objects").empty());
  TEST_ASSERT_TRUE(backend->console.empty());
#  if defined(AE_OBJECTS_TEST_custom)
  TEST_ASSERT_EQUAL_INT(1, custom_calls);
#  endif
#endif
  TEST_ASSERT_FALSE(backend->wrong_space);
  TEST_ASSERT_TRUE(sink.DrainAll());
}

void test_DisabledArgumentsAreNotEvaluated() {
#if !defined(AE_OBJECTS_TEST_debug)
  int evaluated = 0;
  AE_LOG_MACRO_TAGGED(objects_tele::kRamNotFound, "disabled {}", ++evaluated);
  TEST_ASSERT_EQUAL_INT(0, evaluated);
#endif
}
}  // namespace ae::test_object_telemetry

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_object_telemetry::test_TaggedLoggingRuntimeAndDrain);
  RUN_TEST(ae::test_object_telemetry::test_DisabledArgumentsAreNotEvaluated);
  return UNITY_END();
}
