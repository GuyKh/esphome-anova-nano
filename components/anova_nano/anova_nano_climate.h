#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32) && defined(USE_CLIMATE)

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/climate/climate.h"
#include "anova_nano.h"

namespace esphome::anova_nano {

/// Climate view of the cooker, always in °C (the hub converts to/from the device unit).
///  - mode HEAT/OFF      <- running (PyAnova.get_status: motor_speed != 0); -> START_COOKING / STOP_COOKING
///  - target temperature <- GET_TEMP_SETPOINT;                              -> SET_TEMP_SETPOINT
///  - current temperature <- water temperature from GET_SENSORS
class AnovaNanoClimate : public climate::Climate, public Component, public Parented<AnovaNano> {
 public:
  void setup() override;
  void dump_config() override;
  climate::ClimateTraits traits() override;

  void on_hub_update();

 protected:
  void control(const climate::ClimateCall &call) override;
};

}  // namespace esphome::anova_nano

#endif
