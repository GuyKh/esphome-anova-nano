#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32) && defined(USE_NUMBER)

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/number/number.h"
#include "anova_nano.h"

namespace esphome::anova_nano {

/// Cooking timer in minutes: GET_COOKING_TIMER / SET_COOKING_TIMER (PyAnova.get_timer / set_timer).
class AnovaNanoTimerNumber : public number::Number, public Component, public Parented<AnovaNano> {
 public:
  void setup() override { this->parent_->register_timer_number(this); }
  void dump_config() override;
  void on_hub_update(int32_t minutes) { this->publish_state(static_cast<float>(minutes)); }

 protected:
  void control(float value) override;
};

}  // namespace esphome::anova_nano

#endif
