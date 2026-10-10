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
}

void AnovaNanoComponent::on_disconnect() {
  connected_ = false;
  ESP_LOGW(TAG, "Disconnected from Anova Nano device");
  
  // Reset sensor values when disconnected
  current_temperature_ = NAN;
  target_temperature_ = NAN;
  is_heating_ = false;
  
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
      // In a real implementation, we would send a START command here
      // For now, we just update our internal state
      is_heating_ = true;
      ESP_LOGI(TAG, "Setting Anova Nano to HEAT mode");
    } else if (mode == climate::CLIMATE_MODE_OFF) {
      // In a real implementation, we would send a STOP command here
      is_heating_ = false;
      ESP_LOGI(TAG, "Setting Anova Nano to OFF mode");
    }
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
    parse_sensor_data(data);
  }
}

void AnovaNanoComponent::parse_sensor_data(const std::string &data) {
  // This would need to be implemented based on the actual BLE protocol
  // For now, we'll leave it as a placeholder that would be filled in
  // by reverse-engineering the actual notification format
  //
  // Based on pyanova-nano, we know we need to:
  // 1. Parse the protobuf-like structure
  // 2. Extract temperature values
  // 3. Update current_temperature_, target_temperature_, is_heating_
  // 
  // This would require implementing the equivalent of:
  // - convert_buffer() from pyanova-nano.commands
  // - The protobuf message parsing
  // 
  // For a minimal viable product, we could start by trying to read
  // temperature values using the standard read commands instead of
  // relying solely on notifications
  
  ESP_LOGV(TAG, "Parsing sensor data of length %zu", data.length());
}

}  // namespace anova_nano
}  // namespace esphome
