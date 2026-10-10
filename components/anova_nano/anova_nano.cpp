#include "anova_nano.h"

namespace esphome {
namespace anova_nano {

static const char *const TAG = "anova_nano.component";

AnovaNanoComponent::AnovaNanoComponent() {
  this->set_update_interval(30 * 1000);  // Default 30 seconds
}

void AnovaNanoComponent::on_connect() {
  connected_ = true;
  ESP_LOGI(TAG, "Connected to Anova Nano device");
  
  // Enable notifications on the async characteristic
  if (async_characteristic_ != nullptr) {
    async_characteristic_->start_notify(
      [this](const ble_client::BLECharacteristic *characteristic, const std::string &data) {
        this->handle_notification(characteristic->handle, data);
      }
    );
  }
  
  // Start periodic polling for sensor values
  this->request_sensor_values();
}

void AnovaNanoComponent::on_disconnect() {
  connected_ = false;
  ESP_LOGW(TAG, "Disconnected from Anova Nano device");
  
  // Reset sensor values when disconnected
  current_temperature_ = NAN;
  target_temperature_ = NAN;
  is_heating_ = false;
  
  // Reset pending command state
  pending_command_ = PendingCommand::NONE;
  response_received_ = false;
  response_buffer_.clear();
  
  // This will trigger state updates
  this->update();
}

void AnovaNanoComponent::on_service_found(const ble_client::BLEService &service) {
  if (service.uuid.equals(String(SERVICE_UUID))) {
    ESP_LOGD(TAG, "Found Anova Nano service");
  }
}

void AnovaNanoComponent::on_characteristic_found(const ble_client::BLEService &service, const ble_client::BLECharacteristic &characteristic) {
  if (!service.uuid.equals(String(SERVICE_UUID))) {
    return;
  }

  String char_uuid = characteristic.uuid.to_string();
  
  if (char_uuid.equals(String(CHARACTERISTICS_READ))) {
    read_characteristic_ = &characteristic;
    ESP_LOGD(TAG, "Found read characteristic");
  } else if (char_uuid.equals(String(CHARACTERISTICS_WRITE))) {
    write_characteristic_ = &characteristic;
    ESP_LOGD(TAG, "Found write characteristic");
  } else if (char_uuid.equals(String(CHARACTERISTICS_ASYNC))) {
    async_characteristic_ = &characteristic;
    ESP_LOGD(TAG, "Found async/notification characteristic");
  }
}

void AnovaNanoComponent::update() {
  // Only publish state if we have valid readings
  if (!std::isnan(current_temperature_)) {
    // Publish current temperature
    this->publish_state(current_temperature_);
  }
  
  // Always publish climate state (even if NAN, to show unavailable)
  climate::ClimateCall call = { .set_mode = true };
  if (is_heating_) {
    call.set_mode_ = climate::CLIMATE_MODE_HEAT;
  } else {
    call.set_mode_ = climate::CLIMATE_MODE_OFF;
  }
  this->control(call);
}

climate::ClimateTraits AnovaNanoComponent::traits() {
  climate::ClimateTraits traits;
  traits.set_supports_current_temperature(true);
  traits.set_supports_two_point_target_temperature(false);
  traits.set_visual_min_temperature(20.0);  // Anova Nano typical range
  traits.set_visual_max_temperature(95.0);
  traits.set_visual_temperature_step(0.5);
  
  // Supported modes
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
  
  return traits;
}

void AnovaNanoComponent::control(const climate::ClimateCall &call) {
  if (!connected_) {
    ESP_LOGW(TAG, "Cannot control - not connected to device");
    return;
  }
  
  if (call.get_mode() != nullptr) {
    climate::climate_mode_t mode = *call.get_mode();
    if (mode == climate::CLIMATE_MODE_HEAT) {
      // Send START command to device
      this->start_cooking();
    } else if (mode == climate::CLIMATE_MODE_OFF) {
      // Send STOP command to device
      this->stop_cooking();
    }
  }
  
  if (call.get_target_temperature() != nullptr) {
    float target_temp = *call.get_target_temperature();
    this->set_target_temperature(target_temp);
  }
  
  // Request an immediate update after control change
  this->request_call_now();
}

bool AnovaNanoComponent::is_anova_device(const ble_client::BLEDevice &device) {
  // Check if device advertises our service UUID
  for (const auto &service_uuid : service_uuids_) {
    if (service_uuid.equals(String(SERVICE_UUID))) {
      return true;
    }
  }
  
  // Also check manufacturer data or device name if available
  std::string name = device.get_name();
  if (!name.empty() && (name.find("Anova") != std::string::npos || 
                        name.find("nano") != std::string::npos)) {
    return true;
  }
  
  return false;
}

void AnovaNanoComponent::handle_notification(uint16_t handle, const std::string &data) {
  if (handle == async_characteristic_->handle) {
    // Handle asynchronous notifications from the device
    ESP_LOGV(TAG, "Received async notification: %s", util::encode_hex(data).c_str());
    // Convert string to vector<uint8_t> for parsing
    std::vector<uint8_t> data_vec(data.begin(), data.end());
    this->parse_sensor_data(data_vec);
  }
}

// BLE command helpers based on pyanova-nano
std::vector<uint8_t> AnovaNanoComponent::create_command_array(uint8_t message_type, const std::vector<uint8_t> &value) {
  // Based on pyanova-nano.commands.create_command_array
  std::vector<uint8_t> command = {
    ANOVA_DOMAIN_ID_CONFIG,  // Domain ID
    message_type             // Message type
  };
  
  // Add value if provided
  if (!value.empty()) {
    command.insert(command.end(), value.begin(), value.end());
  }
  
  return encode_command(command, true);
}

std::vector<uint8_t> AnovaNanoComponent::encode_command(const std::vector<uint8_t> &buffer, bool append_zero) {
  // Based on pyanova-nano.commands.encode_command
  std::vector<uint8_t> result;
  result.push_back(0);  // Start with 0
  size_t last_value = 0;
  size_t current_index = 1;
  
  auto reset_index = [&](bool is_end) {
    result[last_value] = static_cast<uint8_t>(current_index);
    last_value = result.size();
    if (is_end) {
      result.push_back(0);
    }
    current_index = 1;
  };
  
  for (size_t u = 0; u < buffer.size(); ++u) {
    if (buffer[u] == 0) {
      reset_index(true);
    } else {
      result.push_back(buffer[u]);
      current_index++;
      if (current_index == 255) {
        reset_index(true);
      }
    }
  }
  reset_index(false);
  
  if (append_zero) {
    result.push_back(0);
  }
  
  return result;
}

void AnovaNanoComponent::send_command(uint8_t message_type, const std::vector<uint8_t> &value) {
  if (!connected_ || write_characteristic_ == nullptr) {
    ESP_LOGW(TAG, "Cannot send command - not connected or no write characteristic");
    return;
  }
  
  // Set pending command state
  switch (message_type) {
    case CONFIG_DOMAIN_MESSAGE_TYPE_GET_SENSORS:
      pending_command_ = PendingCommand::GET_SENSOR_VALUES;
      break;
    case CONFIG_DOMAIN_MESSAGE_TYPE_GET_TEMP_SETPOINT:
      pending_command_ = PendingCommand::READ_TARGET_TEMP;
      break;
    case CONFIG_DOMAIN_MESSAGE_TYPE_START_COOKING:
      pending_command_ = PendingCommand::START_COOKING;
      break;
    case CONFIG_DOMAIN_MESSAGE_TYPE_STOP_COOKING:
      pending_command_ = PendingCommand::STOP_COOKING;
      break;
    default:
      pending_command_ = PendingCommand::NONE;
      break;
  }
  
  response_received_ = false;
  response_buffer_.clear();
  
  // Create and send the command
  std::vector<uint8_t> command_array = this->create_command_array(message_type, value);
  ESP_LOGD(TAG, "Sending command array: %s", util::encode_hex(command_array).c_str());
  
  write_characteristic_->write_bytes(command_array);
}

void AnovaNanoComponent::request_sensor_values() {
  this->send_command(CONFIG_DOMAIN_MESSAGE_TYPE_GET_SENSORS);
}

void AnovaNanoComponent::read_target_temperature() {
  this->send_command(CONFIG_DOMAIN_MESSAGE_TYPE_GET_TEMP_SETPOINT);
}

void AnovaNanoComponent::start_cooking() {
  this->send_command(CONFIG_DOMAIN_MESSAGE_TYPE_START_COOKING);
}

void AnovaNanoComponent::stop_cooking() {
  this->send_command(CONFIG_DOMAIN_MESSAGE_TYPE_STOP_COOKING);
}

void AnovaNanoComponent::parse_sensor_data(const std::vector<uint8_t> &data) {
  // Based on pyanova-nano.commands.convert_buffer and proto parsing
  // This is a simplified version - in practice we'd need to properly parse the protobuf
  
  if (data.empty()) {
    ESP_LOGW(TAG, "Received empty sensor data");
    return;
  }
  
  // Convert buffer using the same logic as pyanova-nano
  std::vector<int> results;
  if (data.size() < 2) {
    ESP_LOGW(TAG, "Sensor data too short");
    return;
  }
  
  size_t i = 1;  // Skip first byte (length)
  while (i < (data.size() - 1)) {
    uint8_t block_length = data[i];
    i++;
    for (size_t k = 1; k < block_length; k++) {
      if (i < data.size()) {
        results.push_back(data[i]);
        i++;
      } else {
        results.push_back(-1);  // Indicates missing data
      }
    }
    if (block_length < 255 && i < data.size()) {
      results.push_back(0);  // Add separator if needed
      i++;
    }
  }
  
  // Skip first two results (header bytes)
  if (results.size() < 4) {
    ESP_LOGW(TAG, "Not enough data points after conversion");
    return;
  }
  
  // Based on pyanova-nano types.SensorValues order:
  // water_temp, water_temp_units, heater_temp, heater_temp_units, 
  // triac_temp, triac_temp_units, internal_temp, internal_temp_units,
  // water_low, water_leak, motor_speed
  
  try {
    // Extract water temperature (index 0)
    if (results[0] != -1) {
      float water_temp = static_cast<float>(results[0]) / 100.0;  // Assuming 0.01C precision
      current_temperature_ = water_temp;
      ESP_LOGD(TAG, "Parsed water temperature: %.2f°C", water_temp);
    }
    
    // Extract heater temperature (index 2) 
    if (results[2] != -1) {
      float heater_temp = static_cast<float>(results[2]) / 100.0;
      // We could store this if needed, but for climate we mainly care about water temp
    }
    
    // Extract motor speed (last value) to determine if heating
    if (!results.empty() && results.back() != -1) {
      bool new_heating_state = (results.back() != 0);
      if (new_heating_state != is_heating_) {
        is_heating_ = new_heating_state;
        ESP_LOGD(TAG, "Heating state changed to: %s", is_heating_ ? "ON" : "OFF");
      }
    }
    
    // Trigger update to publish new state
    this->update();
  } catch (const std::exception &ex) {
    ESP_LOGW(TAG, "Error parsing sensor data: %s", ex.what());
  }
}

}  // namespace anova_nano
}  // namespace esphome