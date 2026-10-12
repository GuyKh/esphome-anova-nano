#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32

#include <deque>
#include <vector>

#include <esp_gattc_api.h>

#include "esphome/core/component.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#include "anova_nano_protocol.h"

namespace esphome::anova_nano {

namespace espbt = esphome::esp32_ble_tracker;

class AnovaNanoClimate;
class AnovaNanoTimerNumber;
class AnovaNanoUnitSelect;

/// Owns the BLE link and the request/response queue; entities register with it.
///
/// Mirrors pyanova-nano's PyAnova client:
///  - writes to 0e140001 without response, receives responses as notifications on 0e140002
///  - exactly one command in flight (PyAnova._command_lock), 10 s response timeout
///  - every command expects a response (all COMMANDS_MAP entries have a handler)
class AnovaNano : public PollingComponent, public ble_client::BLEClientNode {
 public:
  static constexpr uint32_t RESPONSE_TIMEOUT_MS = 10000;  // PyAnova._READ_DATA_TIMEOUT_SEC

  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  // ---- Commands (ports of PyAnova methods) ----
  void start();                                 // PyAnova.start
  void stop();                                  // PyAnova.stop
  void set_target_temperature_c(float celsius);  // PyAnova.set_target_temperature (converted to device unit)
  void set_timer(int32_t minutes);              // PyAnova.set_timer
  void set_unit(char unit);                     // PyAnova.set_unit ('C' / 'F')
  void refresh();                               // GET_TEMP_UNITS, GET_SENSORS, GET_TEMP_SETPOINT, GET_COOKING_TIMER

  // ---- State ----
  bool is_ready() const { return this->node_state == espbt::ClientState::ESTABLISHED; }
  bool has_sensor_values() const { return this->has_sensor_values_; }
  const protocol::SensorValues &sensor_values() const { return this->sensor_values_; }
  bool has_unit() const { return this->has_unit_; }
  char unit() const { return this->unit_; }
  bool has_target() const { return this->has_target_; }
  float target_temperature_c() const;  // NAN if unknown
  float water_temperature_c() const;   // NAN if unknown

  // ---- Entity registration (from codegen) ----
#ifdef USE_SENSOR
  void set_water_temperature_sensor(sensor::Sensor *s) { this->water_temperature_sensor_ = s; }
  void set_heater_temperature_sensor(sensor::Sensor *s) { this->heater_temperature_sensor_ = s; }
  void set_triac_temperature_sensor(sensor::Sensor *s) { this->triac_temperature_sensor_ = s; }
  void set_internal_temperature_sensor(sensor::Sensor *s) { this->internal_temperature_sensor_ = s; }
  void set_target_temperature_sensor(sensor::Sensor *s) { this->target_temperature_sensor_ = s; }
  void set_motor_speed_sensor(sensor::Sensor *s) { this->motor_speed_sensor_ = s; }
  void set_timer_sensor(sensor::Sensor *s) { this->timer_sensor_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  void set_running_binary_sensor(binary_sensor::BinarySensor *s) { this->running_binary_sensor_ = s; }
  void set_water_low_binary_sensor(binary_sensor::BinarySensor *s) { this->water_low_binary_sensor_ = s; }
  void set_water_leak_binary_sensor(binary_sensor::BinarySensor *s) { this->water_leak_binary_sensor_ = s; }
#endif
#ifdef USE_TEXT_SENSOR
  void set_firmware_version_text_sensor(text_sensor::TextSensor *s) { this->firmware_version_text_sensor_ = s; }
  void set_firmware_commit_text_sensor(text_sensor::TextSensor *s) { this->firmware_commit_text_sensor_ = s; }
  void set_device_info_text_sensor(text_sensor::TextSensor *s) { this->device_info_text_sensor_ = s; }
  void set_unit_text_sensor(text_sensor::TextSensor *s) { this->unit_text_sensor_ = s; }
#endif
  void register_climate(AnovaNanoClimate *c) { this->climate_ = c; }
  void register_timer_number(AnovaNanoTimerNumber *n) { this->timer_number_ = n; }
  void register_unit_select(AnovaNanoUnitSelect *s) { this->unit_select_ = s; }

 protected:
  struct Request {
    protocol::MessageType type;
    std::vector<uint8_t> frame;
    bool is_write;
  };

  void enqueue_(protocol::MessageType type);
  void enqueue_write_(protocol::MessageType type, int32_t value);
  void send_next_();
  void handle_notification_(const uint8_t *data, uint16_t len);
  void handle_frame_(const protocol::Frame &frame);
  void handle_disconnect_();

  void on_sensor_values_(const protocol::SensorValues &sv);
  void on_target_raw_(int32_t raw);
  void on_unit_raw_(int32_t raw);
  void on_timer_(int32_t minutes);
  void on_firmware_(const protocol::FirmwareInfo &fw);
  void on_device_info_(int32_t value);
  void publish_unavailable_();
  void notify_entities_();

  // BLE
  uint16_t write_handle_{0};
  uint16_t notify_handle_{0};

  // Queue (one in flight)
  std::deque<Request> queue_;
  bool in_flight_{false};
  protocol::MessageType in_flight_type_{protocol::GET_SENSORS};
  uint32_t in_flight_since_{0};
  protocol::FrameAssembler assembler_;
  bool info_requested_{false};

  // Device state
  protocol::SensorValues sensor_values_{};
  bool has_sensor_values_{false};
  int32_t target_raw_{0};
  bool has_target_{false};
  char unit_{'C'};
  bool has_unit_{false};
  int32_t timer_minutes_{0};
  bool has_timer_{false};

  // Entities
#ifdef USE_SENSOR
  sensor::Sensor *water_temperature_sensor_{nullptr};
  sensor::Sensor *heater_temperature_sensor_{nullptr};
  sensor::Sensor *triac_temperature_sensor_{nullptr};
  sensor::Sensor *internal_temperature_sensor_{nullptr};
  sensor::Sensor *target_temperature_sensor_{nullptr};
  sensor::Sensor *motor_speed_sensor_{nullptr};
  sensor::Sensor *timer_sensor_{nullptr};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *running_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *water_low_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *water_leak_binary_sensor_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *firmware_version_text_sensor_{nullptr};
  text_sensor::TextSensor *firmware_commit_text_sensor_{nullptr};
  text_sensor::TextSensor *device_info_text_sensor_{nullptr};
  text_sensor::TextSensor *unit_text_sensor_{nullptr};
#endif
  AnovaNanoClimate *climate_{nullptr};
  AnovaNanoTimerNumber *timer_number_{nullptr};
  AnovaNanoUnitSelect *unit_select_{nullptr};
};

}  // namespace esphome::anova_nano

#endif  // USE_ESP32
