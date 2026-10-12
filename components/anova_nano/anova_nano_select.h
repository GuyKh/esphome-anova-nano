#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32) && defined(USE_SELECT)

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/select/select.h"
#include "anova_nano.h"

namespace esphome::anova_nano {

/// Display unit on the cooker: GET_TEMP_UNITS / SET_TEMP_UNITS (PyAnova.get_unit / set_unit).
/// Options are fixed by codegen to ["C", "F"].
class AnovaNanoUnitSelect : public select::Select, public Component, public Parented<AnovaNano> {
 public:
  void setup() override { this->parent_->register_unit_select(this); }
  void dump_config() override;
  void on_hub_update(char unit);

 protected:
  void control(size_t index) override;
};

}  // namespace esphome::anova_nano

#endif
