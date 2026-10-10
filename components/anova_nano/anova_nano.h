#pragma once

#include "esphome.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/ble_client/ble_client.h"

namespace esphome {
namespace anova_nano {

class AnovaNanoComponent : public PollingComponent, public ble_client::BLEClientNode, public climate::Climate {
 public:
  AnovaNanoComponent();

  // Configuration
  void set_uuid(const std::string &uuid) { uuid_ = uuid; }
  void set_scan_timeout(uint32_t scan_timeout) { scan_timeout_ = scan_timeout; }
  void set_poll_interval(uint32_t poll_interval) { this->set_update_interval(poll_interval * 1000); }

  // BLE Client Node overrides
  void on_connect() override;
  void on_disconnect() override;
  void on_service_found(const ble_client::BLEService &service) override;
  void on_characteristic_found(const ble_client::BLEService &service, const ble_client::BLECharacteristic &characteristic) override;

  // PollingComponent override
  void update() override;

  // Climate overrides
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 private:
  // BLE UUIDs from pyanova-nano
  static constexpr const char *SERVICE_UUID = "0e140000-0af1-4582-a242-773e63054c68";
  static constexpr const char *CHARACTERISTICS_WRITE = "0e140001-0af1-4582-a242-773e63054c68";
  static constexpr const char *CHARACTERISTICS_READ = "0e140002-0af1-4582-a242-773e63054c68";
  static constexpr const char *CHARACTERISTICS_ASYNC = "0e140003-0af1-4582-a242-773e63054c68";

  // State tracking
  std::string uuid_;
  uint32_t scan_timeout_{5};
  bool connected_{false};
  
  // Sensor values
  float current_temperature_{NAN};
  float target_temperature_{NAN};
  bool is_heating_{false};
  
  // BLE handles
  ble_client::BLEClient *ble_client_{nullptr};
  ble_client::BLECharacteristic *read_characteristic_{nullptr};
  ble_client::BLECharacteristic *write_characteristic_{nullptr};
  ble_client::BLECharacteristic *async_characteristic_{nullptr};
  
  // Pending command handling for synchronous requests
  enum class PendingCommand {
    NONE,
    GET_SENSOR_VALUES,
    READ_TARGET_TEMP,
    START_COOKING,
    STOP_COOKING
  };
  PendingCommand pending_command_{PendingCommand::NONE};
  bool response_received_{false};
  std::vector<uint8_t> response_buffer_;
  
  // Helper methods
  bool is_anova_device(const ble_client::BLEDevice &device);
  void handle_notification(uint16_t handle, const std::string &data);
  void parse_sensor_data(const std::vector<uint8_t> &data);
  
  // BLE command helpers (based on pyanova-nano)
  std::vector<uint8_t> create_command_array(uint8_t message_type, const std::vector<uint8_t> &value = {});
  std::vector<uint8_t> encode_command(const std::vector<uint8_t> &buffer, bool append_zero = true);
  void send_command(uint8_t message_type, const std::vector<uint8_t> &value = {});
  
  // Specific command methods
  void request_sensor_values();
  void read_target_temperature();
  void start_cooking();
  void stop_cooking();
  
  // Constants from pyanova-nano proto
  static constexpr uint8_t ANOVA_DOMAIN_ID_CONFIG = 0x03; // From pyanova-nano.proto.messages_pb2.DomainType.ANOVA_DOMAIN_ID_CONFIG
  static constexpr uint8_t CONFIG_DOMAIN_MESSAGE_TYPE_GET_SENSORS = 0x01;
  static constexpr uint8_t CONFIG_DOMAIN_MESSAGE_TYPE_GET_TEMP_SETPOINT = 0x02;
  static constexpr uint8_t CONFIG_DOMAIN_MESSAGE_TYPE_START_COOKING = 0x03;
  static constexpr uint8_t CONFIG_DOMAIN_MESSAGE_TYPE_STOP_COOKING = 0x04;
};

}  // namespace anova_nano
}  // namespace esphome