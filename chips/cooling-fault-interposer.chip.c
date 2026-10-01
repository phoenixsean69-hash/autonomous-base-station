#include "wokwi-api.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  pin_t fan_in;
  pin_t fault_enable;
  pin_t fan_out;
  pin_t fault_active;
  timer_t timer;
} chip_state_t;

static void refresh(void *user_data) {
  chip_state_t *s = (chip_state_t *)user_data;
  const bool source_healthy = pin_read(s->fan_in) == HIGH;
  const bool inject_failure = pin_read(s->fault_enable) == HIGH;

  pin_write(s->fault_active, inject_failure ? HIGH : LOW);
  pin_write(s->fan_out, (source_healthy && !inject_failure) ? HIGH : LOW);

  // Use an explicit self-rearming one-shot timer. This has proven reliable
  // with the Wokwi custom-chip ABI used by this project and ensures switch
  // changes are sampled continuously after startup.
  timer_start(s->timer, 50000u, false);
}

void chip_init(void) {
  static chip_state_t state;
  static timer_config_t cfg;
  chip_state_t *s = &state;

  s->fan_in = pin_init("FAN_IN", INPUT_PULLDOWN);
  s->fault_enable = pin_init("FAULT_ENABLE", INPUT_PULLDOWN);
  s->fan_out = pin_init("FAN_OUT", OUTPUT_LOW);
  s->fault_active = pin_init("FAULT_ACTIVE", OUTPUT_LOW);

  cfg.user_data = s;
  cfg.callback = refresh;
  for (int i = 0; i < 8; ++i) cfg.reserved[i] = 0;

  s->timer = timer_init(&cfg);
  refresh(s);
}
