#include "anova_nano_climate.h"

#if defined(USE_ESP32) && defined(USE_CLIMATE)

#include <cmath>

#include "esphome/core/log.h"

namespace esphome::anova_nano {

static const char *const TAG = "anova_nano.climate";

void AnovaNanoClimate::setup() {
  this->parent_->register_climate(this);
  this->mode = climate::CLIMATE_MODE_OFF;
  this->action = climate::CLIMATE_ACTION_OFF;
  this->current_temperature = NAN;
  this->target_temperature = NAN;
}

void AnovaNanoClimate::dump_config() { LOG_CLIMATE("", "Anova Nano Climate", this); }

climate::ClimateTraits AnovaNanoClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
  // Anova Nano: 32-197 °F / 0-92 °C. Setpoint resolution is 0.1 in the device unit.
  traits.set_visual_min_temperature(0.0f);
  traits.set_visual_max_temperature(92.0f);
  traits.set_visual_target_temperature_step(0.1f);
  traits.set_visual_current_temperature_step(0.1f);
  return traits;
}

void AnovaNanoClimate::control(const climate::ClimateCall &call) {
  // Setpoint before start, so a combined call starts at the new temperature.
  auto target = call.get_target_temperature();
  if (target.has_value()) {
    this->parent_->set_target_temperature_c(*target);
    this->target_temperature = *target;  // optimistic; corrected by the follow-up refresh
  }
  auto mode = call.get_mode();
  if (mode.has_value()) {
    switch (*mode) {
      case climate::CLIMATE_MODE_HEAT:
        this->parent_->start();
        break;
      case climate::CLIMATE_MODE_OFF:
        this->parent_->stop();
        break;
      default:
        ESP_LOGW(TAG, "Unsupported mode %d", static_cast<int>(*mode));
        return;
    }
    this->mode = *mode;
  }
  this->publish_state();
}

void AnovaNanoClimate::on_hub_update() {
  this->current_temperature = this->parent_->water_temperature_c();
  this->target_temperature = this->parent_->target_temperature_c();
  if (this->parent_->has_sensor_values()) {
    const bool running = this->parent_->sensor_values().running();
    this->mode = running ? climate::CLIMATE_MODE_HEAT : climate::CLIMATE_MODE_OFF;
    this->action = running ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_OFF;
  } else {
    this->action = climate::CLIMATE_ACTION_OFF;
  }
  this->publish_state();
}

}  // namespace esphome::anova_nano

#endif
