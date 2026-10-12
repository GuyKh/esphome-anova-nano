#include "anova_nano_protocol.h"

#include <cmath>

namespace esphome::anova_nano::protocol {

const char *message_type_name(uint8_t type) {
  switch (type) {
    case SET_TEMP_SETPOINT:
      return "SET_TEMP_SETPOINT";
    case GET_TEMP_SETPOINT:
      return "GET_TEMP_SETPOINT";
    case GET_SENSORS:
      return "GET_SENSORS";
    case SET_TEMP_UNITS:
      return "SET_TEMP_UNITS";
    case GET_TEMP_UNITS:
      return "GET_TEMP_UNITS";
    case START_COOKING:
      return "START_COOKING";
    case STOP_COOKING:
      return "STOP_COOKING";
    case SET_COOKING_TIMER:
      return "SET_COOKING_TIMER";
    case GET_COOKING_TIMER:
      return "GET_COOKING_TIMER";
    case GET_DEVICE_INFO:
      return "GET_DEVICE_INFO";
    case GET_FIRMWARE_INFO:
      return "GET_FIRMWARE_INFO";
    default:
      return "UNKNOWN";
  }
}

// ---- Framing -------------------------------------------------------------

std::vector<uint8_t> cobs_encode(const uint8_t *data, size_t len, bool append_delimiter) {
  // Line-for-line port of encode_command() to keep the 254-byte block edge case identical.
  std::vector<uint8_t> result;
  result.reserve(len + len / 254 + 3);
  result.push_back(0);
  size_t last_value = 0;
  size_t current_index = 1;

  auto reset_index = [&](bool is_end) {
    result[last_value] = static_cast<uint8_t>(current_index);
    last_value = result.size();
    if (is_end)
      result.push_back(0);
    current_index = 1;
  };

  for (size_t u = 0; u < len; u++) {
    if (data[u] == 0) {
      reset_index(true);
    } else {
      result.push_back(data[u]);
      current_index++;
      if (current_index == 255)
        reset_index(true);
    }
  }
  reset_index(false);
  if (append_delimiter)
    result.push_back(0);
  return result;
}

bool cobs_decode(const uint8_t *data, size_t len, std::vector<uint8_t> &out) {
  out.clear();
  size_t i = 0;
  while (i < len) {
    uint8_t code = data[i++];
    if (code == 0)
      return false;  // 0x00 is never valid inside a COBS frame
    for (uint8_t k = 1; k < code; k++) {
      if (i >= len)
        return false;  // truncated block
      out.push_back(data[i++]);
    }
    if (code < 0xFF && i < len)
      out.push_back(0);
  }
  return true;
}

std::vector<uint8_t> build_command(MessageType type, const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> raw;
  raw.reserve(2 + payload.size());
  raw.push_back(DOMAIN_CONFIG);
  raw.push_back(type);
  raw.insert(raw.end(), payload.begin(), payload.end());
  return cobs_encode(raw.data(), raw.size(), true);
}

std::vector<uint8_t> build_command(MessageType type, int32_t integer_value) {
  return build_command(type, encode_integer_value(integer_value));
}

size_t FrameAssembler::feed(const uint8_t *data, size_t len, std::vector<std::vector<uint8_t>> &frames) {
  size_t completed = 0;
  for (size_t i = 0; i < len; i++) {
    if (data[i] == 0) {
      if (!this->buffer_.empty()) {
        frames.push_back(this->buffer_);
        completed++;
      }
      this->buffer_.clear();
      continue;
    }
    if (this->buffer_.size() >= MAX_FRAME) {
      // Garbage or lost delimiter: drop and resync on the next 0x00.
      this->buffer_.clear();
    }
    this->buffer_.push_back(data[i]);
  }
  return completed;
}

bool parse_frame(const uint8_t *data, size_t len, Frame &frame) {
  std::vector<uint8_t> decoded;
  if (!cobs_decode(data, len, decoded) || decoded.size() < 2)
    return false;
  frame.domain = decoded[0];
  frame.message_type = decoded[1];
  frame.payload.assign(decoded.begin() + 2, decoded.end());
  return true;
}

// ---- Protobuf ------------------------------------------------------------

namespace {

enum WireType : uint8_t { VARINT = 0, FIXED64 = 1, LEN = 2, FIXED32 = 5 };

void write_varint(std::vector<uint8_t> &out, uint64_t v) {
  while (v >= 0x80) {
    out.push_back(static_cast<uint8_t>(v | 0x80));
    v >>= 7;
  }
  out.push_back(static_cast<uint8_t>(v));
}

class Reader {
 public:
  Reader(const uint8_t *data, size_t len) : p_(data), end_(data + len) {}
  bool done() const { return this->p_ >= this->end_; }

  bool varint(uint64_t &v) {
    v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
      if (this->p_ >= this->end_)
        return false;
      uint8_t b = *this->p_++;
      v |= static_cast<uint64_t>(b & 0x7F) << shift;
      if ((b & 0x80) == 0)
        return true;
    }
    return false;
  }

  bool tag(uint32_t &field, uint8_t &wire) {
    uint64_t t;
    if (!this->varint(t))
      return false;
    field = static_cast<uint32_t>(t >> 3);
    wire = static_cast<uint8_t>(t & 0x7);
    return field != 0;
  }

  bool bytes(const uint8_t *&data, size_t &len) {
    uint64_t n;
    if (!this->varint(n) || n > static_cast<uint64_t>(this->end_ - this->p_))
      return false;
    data = this->p_;
    len = static_cast<size_t>(n);
    this->p_ += len;
    return true;
  }

  bool skip(uint8_t wire) {
    uint64_t v;
    const uint8_t *d;
    size_t n;
    switch (wire) {
      case VARINT:
        return this->varint(v);
      case LEN:
        return this->bytes(d, n);
      case FIXED64:
        return this->advance(8);
      case FIXED32:
        return this->advance(4);
      default:
        return false;
    }
  }

 private:
  bool advance(size_t n) {
    if (static_cast<size_t>(this->end_ - this->p_) < n)
      return false;
    this->p_ += n;
    return true;
  }
  const uint8_t *p_;
  const uint8_t *end_;
};

// proto3 int32/enum: varint holding a sign-extended 64-bit value; truncate to 32 bits.
inline int32_t as_int32(uint64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v)); }

}  // namespace

std::vector<uint8_t> encode_integer_value(int32_t value) {
  std::vector<uint8_t> out;
  if (value == 0)
    return out;  // proto3: default value is not serialized
  out.push_back((1 << 3) | VARINT);
  write_varint(out, static_cast<uint64_t>(static_cast<int64_t>(value)));
  return out;
}

bool decode_integer_value(const uint8_t *data, size_t len, int32_t &value) {
  value = 0;
  Reader r(data, len);
  while (!r.done()) {
    uint32_t field;
    uint8_t wire;
    if (!r.tag(field, wire))
      return false;
    if (field == 1 && wire == VARINT) {
      uint64_t v;
      if (!r.varint(v))
        return false;
      value = as_int32(v);
    } else if (!r.skip(wire)) {
      return false;
    }
  }
  return true;
}

static bool decode_sensor_value(const uint8_t *data, size_t len, SensorValue &sv) {
  sv = SensorValue{};
  Reader r(data, len);
  while (!r.done()) {
    uint32_t field;
    uint8_t wire;
    if (!r.tag(field, wire))
      return false;
    if (wire == VARINT && field >= 1 && field <= 3) {
      uint64_t v;
      if (!r.varint(v))
        return false;
      if (field == 1)
        sv.value = as_int32(v);
      else if (field == 2)
        sv.units = as_int32(v);
      else
        sv.sensor_type = as_int32(v);
    } else if (!r.skip(wire)) {
      return false;
    }
  }
  return true;
}

bool decode_sensor_value_list(const uint8_t *data, size_t len, std::vector<SensorValue> &values) {
  values.clear();
  Reader r(data, len);
  while (!r.done()) {
    uint32_t field;
    uint8_t wire;
    if (!r.tag(field, wire))
      return false;
    if (field == 1 && wire == LEN) {
      const uint8_t *sub;
      size_t sub_len;
      if (!r.bytes(sub, sub_len))
        return false;
      SensorValue sv;
      if (!decode_sensor_value(sub, sub_len, sv))
        return false;
      values.push_back(sv);
    } else if (!r.skip(wire)) {
      return false;
    }
  }
  return true;
}

bool decode_firmware_info(const uint8_t *data, size_t len, FirmwareInfo &info) {
  info = FirmwareInfo{};
  Reader r(data, len);
  while (!r.done()) {
    uint32_t field;
    uint8_t wire;
    if (!r.tag(field, wire))
      return false;
    if ((field == 1 || field == 2) && wire == LEN) {
      const uint8_t *s;
      size_t n;
      if (!r.bytes(s, n))
        return false;
      (field == 1 ? info.commit_id : info.tag_id).assign(reinterpret_cast<const char *>(s), n);
    } else if (field == 3 && wire == VARINT) {
      uint64_t v;
      if (!r.varint(v))
        return false;
      info.date_code = static_cast<uint32_t>(v);
    } else if (!r.skip(wire)) {
      return false;
    }
  }
  return true;
}

// ---- Semantics -------------------------------------------------------------

bool unit_and_factor(int32_t units, char &unit, int &factor) {
  switch (units) {
    case DEGREES_C:
      unit = 'C';
      factor = 1;
      return true;
    case DEGREES_POINT_1C:
      unit = 'C';
      factor = 10;
      return true;
    case DEGREES_POINT_01C:
      unit = 'C';
      factor = 100;
      return true;
    case DEGREES_F:
      unit = 'F';
      factor = 1;
      return true;
    case DEGREES_POINT_1F:
      unit = 'F';
      factor = 10;
      return true;
    case DEGREES_POINT_01F:
      unit = 'F';
      factor = 100;
      return true;
    default:
      return false;
  }
}

static bool scaled(const SensorValue &sv, float &value, char &unit) {
  int factor;
  if (!unit_and_factor(sv.units, unit, factor))
    return false;
  value = static_cast<float>(static_cast<double>(sv.value) / factor);
  return true;
}

bool to_sensor_values(const std::vector<SensorValue> &list, SensorValues &out) {
  // Positional, exactly like PyAnova.get_sensor_values():
  // 0 water, 1 heater, 2 triac, 3 unused, 4 internal, 5 water_low, 6 water_leak, 7 motor_speed
  if (list.size() < 8)
    return false;
  if (!scaled(list[0], out.water_temp, out.water_temp_unit) ||
      !scaled(list[1], out.heater_temp, out.heater_temp_unit) ||
      !scaled(list[2], out.triac_temp, out.triac_temp_unit) ||
      !scaled(list[4], out.internal_temp, out.internal_temp_unit))
    return false;
  out.water_low = list[5].value != 0;
  out.water_leak = list[6].value != 0;
  out.motor_speed = list[7].value;
  return true;
}

int32_t setpoint_to_raw(double temperature) { return static_cast<int32_t>(std::nearbyint(temperature * 10.0)); }

}  // namespace esphome::anova_nano::protocol
