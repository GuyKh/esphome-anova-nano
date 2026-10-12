#include "anova_nano_select.h"

#if defined(USE_ESP32) && defined(USE_SELECT)

#include "esphome/core/log.h"

namespace esphome::anova_nano {

static const char *const TAG = "anova_nano.select";

void AnovaNanoUnitSelect::dump_config() { LOG_SELECT("", "Anova Nano Unit", this); }

void AnovaNanoUnitSelect::on_hub_update(char unit) {
  const size_t index = unit == 'F' ? 1 : 0;
  auto active = this->active_index();
  if (!active.has_value() || *active != index)
    this->publish_state(index);
}

void AnovaNanoUnitSelect::control(size_t index) {
  this->parent_->set_unit(index == 1 ? 'F' : 'C');
  this->publish_state(index);
}

}  // namespace esphome::anova_nano

#endif
