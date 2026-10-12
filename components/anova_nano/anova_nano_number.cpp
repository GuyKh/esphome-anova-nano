#include "anova_nano_number.h"

#if defined(USE_ESP32) && defined(USE_NUMBER)

#include <cmath>

#include "esphome/core/log.h"

namespace esphome::anova_nano {

static const char *const TAG = "anova_nano.number";

void AnovaNanoTimerNumber::dump_config() { LOG_NUMBER("", "Anova Nano Timer", this); }

void AnovaNanoTimerNumber::control(float value) {
  this->parent_->set_timer(static_cast<int32_t>(std::lround(value)));
  this->publish_state(value);  // optimistic; corrected by the follow-up refresh
}

}  // namespace esphome::anova_nano

#endif
