#include "anova_nano.h"

#ifdef USE_ESP32

#include <cinttypes>
#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_CLIMATE
#include "anova_nano_climate.h"
#endif
#ifdef USE_NUMBER
#include "anova_nano_number.h"
#endif
#ifdef USE_SELECT
#include "anova_nano_select.h"
#endif

namespace esphome::anova_nano {

using namespace protocol;

static const char *const TAG = "anova_nano";

// UUIDs from PyAnova
static const espbt::ESPBTUUID SERVICE_UUID = espbt::ESPBTUUID::from_raw("0e140000-0af1-4582-a242-773e63054c68");
static const espbt::ESPBTUUID CHAR_WRITE_UUID = espbt::ESPBTUUID::from_raw("0e140001-0af1-4582-a242-773e63054c68");
static const espbt::ESPBTUUID CHAR_READ_UUID = espbt::ESPBTUUID::from_raw("0e140002-0af1-4582-a242-773e63054c68");

// Bound queue growth if the device stops answering but the link stays up.
static constexpr size_t MAX_QUEUE = 16;

void AnovaNano::setup() { this->disable_loop(); }

void AnovaNano::dump_config() {
  ESP_LOGCONFIG(TAG, "Anova Nano:");
  LOG_UPDATE_INTERVAL(this);
#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Water temperature", this->water_temperature_sensor_);
  LOG_SENSOR("  ", "Heater temperature", this->heater_temperature_sensor_);
  LOG_SENSOR("  ", "Triac temperature", this->triac_temperature_sensor_);
  LOG_SENSOR("  ", "Internal temperature", this->internal_temperature_sensor_);
  LOG_SENSOR("  ", "Target temperature", this->target_temperature_sensor_);
  LOG_SENSOR("  ", "Motor speed", this->motor_speed_sensor_);
  LOG_SENSOR("  ", "Timer", this->timer_sensor_);
#endif
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Running", this->running_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Water low", this->water_low_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Water leak", this->water_leak_binary_sensor_);
#endif
#ifdef USE_TEXT_SENSOR
  LOG_TEXT_SENSOR("  ", "Firmware version", this->firmware_version_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Firmware commit", this->firmware_commit_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Device info", this->device_info_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Unit", this->unit_text_sensor_);
#endif
}

// ---- BLE -------------------------------------------------------------------

void AnovaNano::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                    esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      if (param->open.status == ESP_GATT_OK)
        ESP_LOGI(TAG, "[%s] Connected", this->parent_->address_str());
      break;

    case ESP_GATTC_DISCONNECT_EVT:
      ESP_LOGW(TAG, "[%s] Disconnected", this->parent_->address_str());
      this->handle_disconnect_();
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto *write_chr = this->parent_->get_characteristic(SERVICE_UUID, CHAR_WRITE_UUID);
      auto *read_chr = this->parent_->get_characteristic(SERVICE_UUID, CHAR_READ_UUID);
      if (write_chr == nullptr || read_chr == nullptr) {
        ESP_LOGE(TAG, "[%s] Anova Nano service/characteristics not found - is this an Anova Nano?",
                 this->parent_->address_str());
        break;
      }
      this->write_handle_ = write_chr->handle;
      this->notify_handle_ = read_chr->handle;
      // Responses arrive as notifications on the READ characteristic (PyAnova.start_notify(CHARACTERISTICS_READ)).
      // BLEClientBase writes the CCCD when the registration completes.
      auto status = this->parent_->register_for_notify(this->notify_handle_);
      if (status != ESP_OK)
        ESP_LOGW(TAG, "[%s] register_for_notify failed, status=%d", this->parent_->address_str(), status);
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (param->reg_for_notify.handle != this->notify_handle_)
        break;
      if (param->reg_for_notify.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "[%s] Notification registration failed, status=%d", this->parent_->address_str(),
                 param->reg_for_notify.status);
        break;
      }
      this->node_state = espbt::ClientState::ESTABLISHED;
      ESP_LOGI(TAG, "[%s] Ready", this->parent_->address_str());
      this->assembler_.reset();
      if (!this->info_requested_) {
        this->info_requested_ = true;
        this->enqueue_(GET_FIRMWARE_INFO);
        this->enqueue_(GET_DEVICE_INFO);
      }
      this->refresh();
      break;

    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle != this->notify_handle_)
        break;
      this->handle_notification_(param->notify.value, param->notify.value_len);
      break;

    case ESP_GATTC_WRITE_CHAR_EVT:
      if (param->write.handle == this->write_handle_ && param->write.status != ESP_GATT_OK)
        ESP_LOGW(TAG, "[%s] Write failed, status=%d", this->parent_->address_str(), param->write.status);
      break;

    default:
      break;
  }
}

void AnovaNano::handle_disconnect_() {
  this->queue_.clear();
  this->in_flight_ = false;
  this->assembler_.reset();
  this->write_handle_ = 0;
  this->notify_handle_ = 0;
  this->has_sensor_values_ = false;
  this->has_target_ = false;
  this->has_timer_ = false;
  this->has_unit_ = false;  // re-read on reconnect; never convert with a stale unit
  this->info_requested_ = false;
  this->publish_unavailable_();
  this->disable_loop();
}

// ---- Queue -----------------------------------------------------------------

void AnovaNano::enqueue_(MessageType type) {
  // Reads are idempotent: skip if the same read is already queued *after* the last write,
  // so a refresh requested after a command still observes that command's effect.
  for (auto it = this->queue_.rbegin(); it != this->queue_.rend(); ++it) {
    if (it->is_write)
      break;
    if (it->type == type)
      return;
  }
  if (this->queue_.size() >= MAX_QUEUE) {
    ESP_LOGW(TAG, "Queue full, dropping %s", message_type_name(type));
    return;
  }
  this->queue_.push_back({type, build_command(type), false});
  this->enable_loop();
}

void AnovaNano::enqueue_write_(MessageType type, int32_t value) {
  // Latest write wins (e.g. dragging the setpoint slider), but keep its place in line.
  for (auto &r : this->queue_) {
    if (r.type == type && r.is_write) {
      r.frame = build_command(type, value);
      return;
    }
  }
  if (this->queue_.size() >= MAX_QUEUE) {
    ESP_LOGW(TAG, "Queue full, dropping %s", message_type_name(type));
    return;
  }
  this->queue_.push_back({type, build_command(type, value), true});
  this->enable_loop();
}

void AnovaNano::loop() {
  if (this->in_flight_) {
    if (millis() - this->in_flight_since_ < RESPONSE_TIMEOUT_MS)
      return;
    ESP_LOGW(TAG, "No response to %s within %" PRIu32 " ms", message_type_name(this->in_flight_type_),
             RESPONSE_TIMEOUT_MS);
    this->in_flight_ = false;
    this->assembler_.reset();
  }
  if (!this->is_ready() || this->queue_.empty()) {
    this->disable_loop();
    return;
  }
  this->send_next_();
}

void AnovaNano::send_next_() {
  Request req = std::move(this->queue_.front());
  this->queue_.pop_front();
  ESP_LOGD(TAG, "-> %s %s", message_type_name(req.type), format_hex_pretty(req.frame).c_str());
  this->assembler_.reset();  // like PyAnova.send_command: fresh receive buffer per command
  // PyAnova: write_gatt_char(CHARACTERISTICS_WRITE, ..., response=False)
  auto status = esp_ble_gattc_write_char(this->parent_->get_gattc_if(), this->parent_->get_conn_id(),
                                         this->write_handle_, req.frame.size(), req.frame.data(),
                                         ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (status != ESP_OK) {
    ESP_LOGW(TAG, "[%s] write_char(%s) failed, status=%d", this->parent_->address_str(), message_type_name(req.type),
             status);
    return;  // loop() moves on to the next request
  }
  this->in_flight_ = true;
  this->in_flight_type_ = req.type;
  this->in_flight_since_ = millis();
}

// ---- Receive ---------------------------------------------------------------

void AnovaNano::handle_notification_(const uint8_t *data, uint16_t len) {
  ESP_LOGV(TAG, "<- chunk %s", format_hex_pretty(data, len).c_str());
  std::vector<std::vector<uint8_t>> frames;
  this->assembler_.feed(data, len, frames);
  for (const auto &raw : frames) {
    Frame frame;
    if (!parse_frame(raw.data(), raw.size(), frame)) {
      ESP_LOGW(TAG, "Malformed frame: %s", format_hex_pretty(raw).c_str());
      continue;
    }
    if (frame.domain != DOMAIN_CONFIG) {
      ESP_LOGW(TAG, "Ignoring frame for domain %u", frame.domain);
      continue;
    }
    this->handle_frame_(frame);
  }
}

void AnovaNano::handle_frame_(const Frame &frame) {
  if (!this->in_flight_) {
    // PyAnova logs and drops data that arrives after the buffer was completed.
    ESP_LOGD(TAG, "Unexpected frame (type %u): %s", frame.message_type, format_hex_pretty(frame.payload).c_str());
    return;
  }
  const MessageType type = this->in_flight_type_;
  this->in_flight_ = false;
  ESP_LOGD(TAG, "<- %s (type %u) %s", message_type_name(type), frame.message_type,
           format_hex_pretty(frame.payload).c_str());

  const uint8_t *p = frame.payload.data();
  const size_t n = frame.payload.size();
  int32_t value;

  // Handlers as in pyanova_nano.commands.COMMANDS_MAP
  switch (type) {
    case GET_SENSORS:
    case START_COOKING:
    case STOP_COOKING: {  // SensorValueList
      std::vector<SensorValue> list;
      SensorValues sv;
      if (decode_sensor_value_list(p, n, list) && to_sensor_values(list, sv)) {
        this->on_sensor_values_(sv);
      } else if (type == GET_SENSORS) {
        ESP_LOGW(TAG, "Could not parse sensor values (%u entries)", static_cast<unsigned>(list.size()));
      }
      break;
    }
    case GET_TEMP_SETPOINT:
      if (decode_integer_value(p, n, value))
        this->on_target_raw_(value);
      break;
    case GET_TEMP_UNITS:
      if (decode_integer_value(p, n, value))
        this->on_unit_raw_(value);
      break;
    case GET_COOKING_TIMER:
      if (decode_integer_value(p, n, value))
        this->on_timer_(value);
      break;
    case GET_FIRMWARE_INFO: {
      FirmwareInfo fw;
      if (decode_firmware_info(p, n, fw))
        this->on_firmware_(fw);
      break;
    }
    case GET_DEVICE_INFO:
      if (decode_integer_value(p, n, value))
        this->on_device_info_(value);
      break;
    case SET_TEMP_SETPOINT:
    case SET_TEMP_UNITS:
    case SET_COOKING_TIMER:  // IntegerValue acknowledgement
      if (decode_integer_value(p, n, value))
        ESP_LOGD(TAG, "%s acknowledged: %" PRId32, message_type_name(type), value);
      break;
  }
}

// ---- Commands ---------------------------------------------------------------

void AnovaNano::refresh() {
  if (!this->is_ready())
    return;
  // Unit first: the setpoint is reported in the device's current unit.
  this->enqueue_(GET_TEMP_UNITS);
  this->enqueue_(GET_SENSORS);
  this->enqueue_(GET_TEMP_SETPOINT);
  this->enqueue_(GET_COOKING_TIMER);
}

void AnovaNano::update() { this->refresh(); }

void AnovaNano::start() {
  if (!this->is_ready()) {
    ESP_LOGW(TAG, "start: not connected");
    return;
  }
  this->enqueue_write_(START_COOKING, 0);  // no payload; response is a SensorValueList
  this->refresh();
}

void AnovaNano::stop() {
  if (!this->is_ready()) {
    ESP_LOGW(TAG, "stop: not connected");
    return;
  }
  this->enqueue_write_(STOP_COOKING, 0);
  this->refresh();
}

void AnovaNano::set_target_temperature_c(float celsius) {
  if (!this->is_ready()) {
    ESP_LOGW(TAG, "set_target_temperature: not connected");
    return;
  }
  if (!this->has_unit_) {
    // Never guess: writing a Celsius value to a Fahrenheit device would be badly wrong.
    ESP_LOGW(TAG, "set_target_temperature: device unit unknown yet, ignoring");
    this->refresh();
    return;
  }
  const double device_value = this->unit_ == 'F' ? c_to_f(celsius) : celsius;
  const int32_t raw = setpoint_to_raw(device_value);
  ESP_LOGI(TAG, "Setting target %.2f°C -> %.1f°%c (raw %" PRId32 ")", celsius, device_value, this->unit_, raw);
  this->enqueue_write_(SET_TEMP_SETPOINT, raw);
  this->refresh();
}

void AnovaNano::set_timer(int32_t minutes) {
  if (!this->is_ready()) {
    ESP_LOGW(TAG, "set_timer: not connected");
    return;
  }
  ESP_LOGI(TAG, "Setting timer to %" PRId32 " min", minutes);
  this->enqueue_write_(SET_COOKING_TIMER, minutes);
  this->refresh();
}

void AnovaNano::set_unit(char unit) {
  if (!this->is_ready()) {
    ESP_LOGW(TAG, "set_unit: not connected");
    return;
  }
  ESP_LOGI(TAG, "Setting unit to %c", unit);
  this->enqueue_write_(SET_TEMP_UNITS, unit_to_raw(unit));
  this->refresh();
}

// ---- State -----------------------------------------------------------------

float AnovaNano::target_temperature_c() const {
  if (!this->has_target_ || !this->has_unit_)
    return NAN;
  return to_celsius(setpoint_from_raw(this->target_raw_), this->unit_);
}

float AnovaNano::water_temperature_c() const {
  if (!this->has_sensor_values_)
    return NAN;
  return to_celsius(this->sensor_values_.water_temp, this->sensor_values_.water_temp_unit);
}

void AnovaNano::on_sensor_values_(const SensorValues &sv) {
  this->sensor_values_ = sv;
  this->has_sensor_values_ = true;
  ESP_LOGD(TAG, "water=%.2f°%c heater=%.2f°%c triac=%.2f°%c internal=%.2f°%c low=%d leak=%d motor=%" PRId32,
           sv.water_temp, sv.water_temp_unit, sv.heater_temp, sv.heater_temp_unit, sv.triac_temp, sv.triac_temp_unit,
           sv.internal_temp, sv.internal_temp_unit, sv.water_low, sv.water_leak, sv.motor_speed);
#ifdef USE_SENSOR
  if (this->water_temperature_sensor_ != nullptr)
    this->water_temperature_sensor_->publish_state(to_celsius(sv.water_temp, sv.water_temp_unit));
  if (this->heater_temperature_sensor_ != nullptr)
    this->heater_temperature_sensor_->publish_state(to_celsius(sv.heater_temp, sv.heater_temp_unit));
  if (this->triac_temperature_sensor_ != nullptr)
    this->triac_temperature_sensor_->publish_state(to_celsius(sv.triac_temp, sv.triac_temp_unit));
  if (this->internal_temperature_sensor_ != nullptr)
    this->internal_temperature_sensor_->publish_state(to_celsius(sv.internal_temp, sv.internal_temp_unit));
  if (this->motor_speed_sensor_ != nullptr)
    this->motor_speed_sensor_->publish_state(sv.motor_speed);
#endif
#ifdef USE_BINARY_SENSOR
  if (this->running_binary_sensor_ != nullptr)
    this->running_binary_sensor_->publish_state(sv.running());
  if (this->water_low_binary_sensor_ != nullptr)
    this->water_low_binary_sensor_->publish_state(sv.water_low);
  if (this->water_leak_binary_sensor_ != nullptr)
    this->water_leak_binary_sensor_->publish_state(sv.water_leak);
#endif
  this->notify_entities_();
}

void AnovaNano::on_target_raw_(int32_t raw) {
  this->target_raw_ = raw;
  this->has_target_ = true;
#ifdef USE_SENSOR
  if (this->target_temperature_sensor_ != nullptr)
    this->target_temperature_sensor_->publish_state(this->target_temperature_c());
#endif
  this->notify_entities_();
}

void AnovaNano::on_unit_raw_(int32_t raw) {
  char unit;
  int factor;
  if (!unit_and_factor(raw, unit, factor)) {
    ESP_LOGW(TAG, "Unknown unit type %" PRId32, raw);
    return;
  }
  const bool changed = !this->has_unit_ || unit != this->unit_;
  this->unit_ = unit;
  this->has_unit_ = true;
  if (changed) {
    ESP_LOGI(TAG, "Device unit: %c", unit);
#ifdef USE_TEXT_SENSOR
    if (this->unit_text_sensor_ != nullptr)
      this->unit_text_sensor_->publish_state(unit == 'F' ? "F" : "C");
#endif
#ifdef USE_SENSOR
    // Re-publish the setpoint: its meaning depends on the unit.
    if (this->has_target_ && this->target_temperature_sensor_ != nullptr)
      this->target_temperature_sensor_->publish_state(this->target_temperature_c());
#endif
  }
  this->notify_entities_();
}

void AnovaNano::on_timer_(int32_t minutes) {
  this->timer_minutes_ = minutes;
  this->has_timer_ = true;
#ifdef USE_SENSOR
  if (this->timer_sensor_ != nullptr)
    this->timer_sensor_->publish_state(minutes);
#endif
#ifdef USE_NUMBER
  if (this->timer_number_ != nullptr)
    this->timer_number_->on_hub_update(minutes);
#endif
}

void AnovaNano::on_firmware_(const FirmwareInfo &fw) {
  ESP_LOGI(TAG, "Firmware: tag=%s commit=%s date=%" PRIu32, fw.tag_id.c_str(), fw.commit_id.c_str(), fw.date_code);
#ifdef USE_TEXT_SENSOR
  if (this->firmware_version_text_sensor_ != nullptr)
    this->firmware_version_text_sensor_->publish_state(fw.tag_id);
  if (this->firmware_commit_text_sensor_ != nullptr)
    this->firmware_commit_text_sensor_->publish_state(fw.commit_id);
#endif
}

void AnovaNano::on_device_info_(int32_t value) {
  // pyanova-nano decodes GET_DEVICE_INFO as an IntegerValue without documenting its meaning.
  ESP_LOGI(TAG, "Device info: %" PRId32, value);
#ifdef USE_TEXT_SENSOR
  if (this->device_info_text_sensor_ != nullptr)
    this->device_info_text_sensor_->publish_state(to_string(value));
#endif
}

void AnovaNano::notify_entities_() {
#ifdef USE_CLIMATE
  if (this->climate_ != nullptr)
    this->climate_->on_hub_update();
#endif
#ifdef USE_SELECT
  if (this->unit_select_ != nullptr && this->has_unit_)
    this->unit_select_->on_hub_update(this->unit_);
#endif
}

void AnovaNano::publish_unavailable_() {
#ifdef USE_SENSOR
  for (auto *s : {this->water_temperature_sensor_, this->heater_temperature_sensor_, this->triac_temperature_sensor_,
                  this->internal_temperature_sensor_, this->target_temperature_sensor_, this->motor_speed_sensor_,
                  this->timer_sensor_}) {
    if (s != nullptr)
      s->publish_state(NAN);
  }
#endif
#ifdef USE_CLIMATE
  if (this->climate_ != nullptr)
    this->climate_->on_hub_update();
#endif
}

}  // namespace esphome::anova_nano

#endif  // USE_ESP32
