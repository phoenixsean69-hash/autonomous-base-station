#include "wokwi-api.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  pin_t medium;
  pin_t high;
  pin_t overload;
  pin_t burst;
  pin_t packets;
  pin_t load_out;
  pin_t activity;
  timer_t timer;
  uint32_t tick_ms;
  uint32_t next_pulse_ms;
  uint32_t pulse_end_ms;
  bool pulse_high;
} chip_state_t;

static uint32_t selected_rate_hz(chip_state_t *s) {
  uint32_t hz = 8u; /* LOW is the default profile */
  if (pin_read(s->medium)) hz = 35u;
  if (pin_read(s->high)) hz = 70u;
  if (pin_read(s->overload)) hz = 100u;
  if (pin_read(s->burst)) hz = 130u;
  return hz;
}

static void tick(void *user_data) {
  chip_state_t *s = (chip_state_t *)user_data;
  s->tick_ms += 1u;
  uint32_t hz = selected_rate_hz(s);

  float load_pct = ((float)hz / 100.0f) * 100.0f;
  if (load_pct > 100.0f) load_pct = 100.0f;
  pin_dac_write(s->load_out, (load_pct / 100.0f) * 5.0f);

  if (s->pulse_high && s->tick_ms >= s->pulse_end_ms) {
    s->pulse_high = false;
    pin_write(s->packets, LOW);
    pin_write(s->activity, LOW);
  }

  if (s->tick_ms >= s->next_pulse_ms) {
    uint32_t period_ms = 1000u / (hz ? hz : 1u);
    if (period_ms < 4u) period_ms = 4u;

    s->pulse_high = true;
    pin_write(s->packets, HIGH);
    pin_write(s->activity, HIGH);
    s->pulse_end_ms = s->tick_ms + 2u;
    s->next_pulse_ms = s->tick_ms + period_ms;
  }
}

void chip_init(void) {
  static chip_state_t state;
  chip_state_t *s = &state;
  s->medium = pin_init("MEDIUM", INPUT_PULLDOWN);
  s->high = pin_init("HIGH", INPUT_PULLDOWN);
  s->overload = pin_init("OVERLOAD", INPUT_PULLDOWN);
  s->burst = pin_init("BURST", INPUT_PULLDOWN);
  s->packets = pin_init("PACKETS", OUTPUT_LOW);
  s->load_out = pin_init("LOAD_OUT", ANALOG);
  s->activity = pin_init("ACTIVITY", OUTPUT_LOW);
  s->next_pulse_ms = 20u;

  const timer_config_t config = {.user_data=s, .callback=tick, .reserved={0}};
  s->timer = timer_init(&config);
  timer_start(s->timer, 1000u, true);
}
