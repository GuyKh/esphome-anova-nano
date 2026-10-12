#pragma once

// Anova Nano BLE protocol, ported from pyanova-nano (https://github.com/filmkorn/pyanova-nano).
//
// Wire format (both directions):
//   COBS( [domain=0, message_type, protobuf payload...] ) + 0x00 delimiter
//
// This file has no ESPHome dependencies so it can be unit-tested on the host
// (see tests/test_protocol.cpp, whose expected values are generated from the
// reference implementation by tests/gen_golden.py).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome::anova_nano::protocol {

// messages.proto: DomainType
static constexpr uint8_t DOMAIN_CONFIG = 0;  // ANOVA_DOMAIN_ID_CONFIG

// messages.proto: ConfigDomainMessageType (only the ones pyanova-nano uses)
enum MessageType : uint8_t {
  SET_TEMP_SETPOINT = 3,
  GET_TEMP_SETPOINT = 4,
  GET_SENSORS = 5,
  SET_TEMP_UNITS = 6,
  GET_TEMP_UNITS = 7,
  START_COOKING = 10,
  STOP_COOKING = 11,
  SET_COOKING_TIMER = 16,
  GET_COOKING_TIMER = 18,
  GET_DEVICE_INFO = 25,
  GET_FIRMWARE_INFO = 26,
};

// messages.proto: UnitType
enum UnitType : int32_t {
  DEGREES_POINT_1C = 0,
  DEGREES_POINT_1F = 1,
  MOTOR_SPEED = 2,
  BOOLEAN = 3,
  DEGREES_POINT_01C = 4,
  DEGREES_POINT_01F = 5,
  DEGREES_C = 6,
  DEGREES_F = 7,
};

const char *message_type_name(uint8_t type);

// ---- Framing -------------------------------------------------------------

/// Port of pyanova_nano.commands.encode_command (COBS).
std::vector<uint8_t> cobs_encode(const uint8_t *data, size_t len, bool append_delimiter = true);

/// COBS-decode one frame (delimiter already stripped). Equivalent to
/// pyanova_nano.commands.convert_buffer for complete frames, minus the
/// 2-byte header strip (see parse_frame).
bool cobs_decode(const uint8_t *data, size_t len, std::vector<uint8_t> &out);

/// Port of pyanova_nano.commands.create_command_array.
/// Payload is a serialized IntegerValue for write commands, empty for reads.
std::vector<uint8_t> build_command(MessageType type, const std::vector<uint8_t> &payload = {});
std::vector<uint8_t> build_command(MessageType type, int32_t integer_value);

/// Reassembles BLE notifications into frames.
///
/// pyanova-nano accumulates notification chunks until convert_buffer() yields
/// no gaps. Because COBS output never contains 0x00, the trailing 0x00 is an
/// unambiguous end-of-frame marker; splitting on it gives the same frames for
/// well-formed input and does not mis-detect completion when a chunk boundary
/// falls right after a block code byte.
class FrameAssembler {
 public:
  static constexpr size_t MAX_FRAME = 1024;

  /// Feed a notification. Returns the number of completed frames appended to `frames`.
  size_t feed(const uint8_t *data, size_t len, std::vector<std::vector<uint8_t>> &frames);
  void reset() { this->buffer_.clear(); }
  size_t pending() const { return this->buffer_.size(); }

 protected:
  std::vector<uint8_t> buffer_;
};

struct Frame {
  uint8_t domain{0};
  uint8_t message_type{0};
  std::vector<uint8_t> payload;
};

/// Decode a complete frame (without delimiter) and split off the 2-byte header,
/// as convert_buffer's `results[2:]` does.
bool parse_frame(const uint8_t *data, size_t len, Frame &frame);

// ---- Protobuf (hand-rolled: only the messages pyanova-nano uses) ----------
//
// proto3 omits fields equal to their default (0), so every field defaults to 0
// when absent, e.g. a SensorValue {value: 0, units: DEGREES_POINT_1C, sensorType: WaterTemp}
// is serialized as an empty sub-message.

std::vector<uint8_t> encode_integer_value(int32_t value);
bool decode_integer_value(const uint8_t *data, size_t len, int32_t &value);

struct SensorValue {
  int32_t value{0};
  int32_t units{0};
  int32_t sensor_type{0};
};
bool decode_sensor_value_list(const uint8_t *data, size_t len, std::vector<SensorValue> &values);

struct FirmwareInfo {
  std::string commit_id;
  std::string tag_id;
  uint32_t date_code{0};
};
bool decode_firmware_info(const uint8_t *data, size_t len, FirmwareInfo &info);

// ---- Semantics (ports of PyAnova methods) ----------------------------------

/// Port of PyAnova._get_unit_and_factor. Returns false for unknown units
/// (the reference raises ValueError).
bool unit_and_factor(int32_t units, char &unit, int &factor);

/// Port of PyAnova.get_sensor_values: positional, 8 entries, index 3 unused.
struct SensorValues {
  float water_temp{0};
  char water_temp_unit{'C'};
  float heater_temp{0};
  char heater_temp_unit{'C'};
  float triac_temp{0};
  char triac_temp_unit{'C'};
  float internal_temp{0};
  char internal_temp_unit{'C'};
  bool water_low{false};
  bool water_leak{false};
  int32_t motor_speed{0};

  /// PyAnova.get_status(): "running" iff motor_speed != 0.
  bool running() const { return this->motor_speed != 0; }
};
bool to_sensor_values(const std::vector<SensorValue> &list, SensorValues &out);

/// PyAnova.get_target_temperature: raw / 10, in the device's current unit.
inline float setpoint_from_raw(int32_t raw) { return static_cast<float>(raw) / 10.0f; }

/// PyAnova.set_target_temperature: int(round(t * 10)). Python's round() is
/// round-half-to-even, so this uses nearbyint (default FE_TONEAREST).
int32_t setpoint_to_raw(double temperature);

/// PyAnova.set_unit: "c" -> DEGREES_C, anything else -> DEGREES_F.
inline int32_t unit_to_raw(char unit) { return (unit == 'c' || unit == 'C') ? DEGREES_C : DEGREES_F; }

inline float f_to_c(float f) { return (f - 32.0f) * 5.0f / 9.0f; }
inline float c_to_f(float c) { return c * 9.0f / 5.0f + 32.0f; }
inline float to_celsius(float value, char unit) { return unit == 'F' ? f_to_c(value) : value; }

}  // namespace esphome::anova_nano::protocol
