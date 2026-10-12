// Host-side tests for the Anova Nano protocol port.
// Every expected value comes from tests/golden_vectors.h, generated from pyanova-nano.
//
//   make -C tests   (or see tests/Makefile)
#include <cmath>
#include <cstdio>
#include <string>

#include "anova_nano_protocol.h"
#include "golden_vectors.h"

using namespace esphome::anova_nano::protocol;

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                       \
  do {                                                         \
    checks++;                                                  \
    if (!(cond)) {                                             \
      failures++;                                              \
      std::printf("FAIL %s:%d: %s | ", __FILE__, __LINE__, #cond); \
      std::printf(__VA_ARGS__);                                \
      std::printf("\n");                                       \
    }                                                          \
  } while (0)

static bool near(double a, double b) { return std::fabs(a - b) < 1e-4; }

static std::string hex(const std::vector<uint8_t> &v) {
  std::string s;
  char b[4];
  for (auto x : v) {
    std::snprintf(b, sizeof b, "%02x ", x);
    s += b;
  }
  return s;
}

// Decode a golden response frame, delivering it in `chunk`-byte notifications.
static bool receive(const std::vector<uint8_t> &wire, size_t chunk, Frame &frame) {
  FrameAssembler fa;
  std::vector<std::vector<uint8_t>> frames;
  for (size_t i = 0; i < wire.size(); i += chunk) {
    size_t n = std::min(chunk, wire.size() - i);
    fa.feed(wire.data() + i, n, frames);
  }
  if (frames.size() != 1 || fa.pending() != 0)
    return false;
  return parse_frame(frames[0].data(), frames[0].size(), frame);
}

static void test_requests() {
  for (const auto &c : golden::REQUESTS) {
    auto type = static_cast<MessageType>(c.msg_type);
    auto got = c.has_value ? build_command(type, c.value) : build_command(type);
    CHECK(got == c.frame, "%s: got %s want %s", c.name, hex(got).c_str(), hex(c.frame).c_str());
  }
}

static void test_setpoints() {
  for (const auto &c : golden::SETPOINTS) {
    int32_t raw = setpoint_to_raw(c.temperature);
    CHECK(raw == c.raw, "t=%.4f raw %d want %d", c.temperature, raw, c.raw);
    auto got = build_command(SET_TEMP_SETPOINT, raw);
    CHECK(got == c.frame, "t=%.4f frame %s want %s", c.temperature, hex(got).c_str(), hex(c.frame).c_str());
  }
  for (const auto &c : golden::UNIT_WRITES) {
    auto got = build_command(SET_TEMP_UNITS, unit_to_raw(c.unit));
    CHECK(got == c.frame, "unit %c frame %s", c.unit, hex(got).c_str());
  }
}

static void test_sensors() {
  for (size_t chunk : {size_t(1), size_t(7), size_t(20), size_t(512)}) {
    for (const auto &c : golden::SENSORS) {
      Frame f;
      CHECK(receive(c.frame, chunk, f), "%s chunk=%zu: frame did not decode", c.name, chunk);
      CHECK(f.domain == DOMAIN_CONFIG && f.message_type == GET_SENSORS, "%s: header %d/%d", c.name, f.domain,
            f.message_type);
      std::vector<SensorValue> list;
      CHECK(decode_sensor_value_list(f.payload.data(), f.payload.size(), list), "%s: protobuf", c.name);
      SensorValues sv;
      CHECK(to_sensor_values(list, sv), "%s: to_sensor_values", c.name);
      CHECK(near(sv.water_temp, c.water_temp) && sv.water_temp_unit == c.water_unit, "%s water %.3f%c", c.name,
            sv.water_temp, sv.water_temp_unit);
      CHECK(near(sv.heater_temp, c.heater_temp) && sv.heater_temp_unit == c.heater_unit, "%s heater %.3f", c.name,
            sv.heater_temp);
      CHECK(near(sv.triac_temp, c.triac_temp) && sv.triac_temp_unit == c.triac_unit, "%s triac %.3f", c.name,
            sv.triac_temp);
      CHECK(near(sv.internal_temp, c.internal_temp) && sv.internal_temp_unit == c.internal_unit, "%s internal %.3f",
            c.name, sv.internal_temp);
      CHECK(sv.water_low == c.water_low && sv.water_leak == c.water_leak, "%s alarms", c.name);
      CHECK(sv.motor_speed == c.motor_speed && sv.running() == c.running, "%s motor %d", c.name, sv.motor_speed);
    }
  }
}

static void test_integers() {
  for (const auto &c : golden::INTEGERS) {
    Frame f;
    CHECK(receive(c.frame, 20, f), "%s: frame", c.name);
    CHECK(f.message_type == c.msg_type, "%s: type %d", c.name, f.message_type);
    int32_t v = -1;
    CHECK(decode_integer_value(f.payload.data(), f.payload.size(), v) && v == c.raw, "%s: raw %d", c.name, v);
    if (c.msg_type == GET_TEMP_SETPOINT)
      CHECK(near(setpoint_from_raw(v), c.scaled), "%s: scaled %.2f", c.name, setpoint_from_raw(v));
    if (c.msg_type == GET_TEMP_UNITS) {
      char u;
      int factor;
      CHECK(unit_and_factor(v, u, factor) && u == c.unit, "%s: unit", c.name);
    }
  }
}

static void test_firmware() {
  for (const auto &c : golden::FIRMWARE) {
    Frame f;
    CHECK(receive(c.frame, 20, f), "%s: frame", c.name);
    FirmwareInfo fw;
    CHECK(decode_firmware_info(f.payload.data(), f.payload.size(), fw), "%s: protobuf", c.name);
    CHECK(fw.commit_id == c.commit_id && fw.tag_id == c.tag_id && fw.date_code == c.date_code, "%s: fields",
          c.name);
  }
}

static void test_robustness() {
  // Back-to-back frames in a single notification, then a split one.
  const auto &a = golden::SENSORS[0].frame;
  const auto &b = golden::INTEGERS[0].frame;
  std::vector<uint8_t> wire(a);
  wire.insert(wire.end(), b.begin(), b.end());
  FrameAssembler fa;
  std::vector<std::vector<uint8_t>> frames;
  fa.feed(wire.data(), wire.size(), frames);
  CHECK(frames.size() == 2, "two frames, got %zu", frames.size());

  // Truncated/garbage frames must be rejected, not crash.
  Frame f;
  const uint8_t truncated[] = {5, 1, 2};
  CHECK(!parse_frame(truncated, sizeof truncated, f), "truncated accepted");
  std::vector<SensorValue> list;
  const uint8_t bad_len[] = {0x0a, 0x7f, 0x08};
  CHECK(!decode_sensor_value_list(bad_len, sizeof bad_len, list), "bad length accepted");
  const uint8_t bad_varint[] = {0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  int32_t v;
  CHECK(!decode_integer_value(bad_varint, sizeof bad_varint, v), "overlong varint accepted");

  // Short sensor list (reference would raise StopIteration) must be rejected.
  SensorValues sv;
  std::vector<SensorValue> seven(7);
  CHECK(!to_sensor_values(seven, sv), "7-entry list accepted");

  // Unknown unit (reference raises ValueError).
  std::vector<SensorValue> eight(8);
  eight[0].units = 2;  // MOTOR_SPEED is not a temperature unit
  CHECK(!to_sensor_values(eight, sv), "unknown unit accepted");

  // Overflowing assembler resyncs.
  std::vector<uint8_t> junk(FrameAssembler::MAX_FRAME + 10, 0x42);
  frames.clear();
  fa.reset();
  fa.feed(junk.data(), junk.size(), frames);
  fa.feed(golden::INTEGERS[0].frame.data(), golden::INTEGERS[0].frame.size(), frames);
  CHECK(frames.size() == 1, "resync after overflow");
}

int main() {
  test_requests();
  test_setpoints();
  test_sensors();
  test_integers();
  test_firmware();
  test_robustness();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
