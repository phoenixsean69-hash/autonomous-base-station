#include "wokwi-api.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  pin_t packet_in;
  pin_t load_out;
  pin_t activity;
  timer_t service_timer;
  uint32_t service_ticks;
  uint32_t pulse_count;
  uint32_t last_rate_hz;
} chip_state_t;

static void packet_edge(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  chip_state_t *s = (chip_state_t *)user_data;
  pin_write(s->activity, value ? HIGH : LOW);
  if (value == HIGH) {
    s->pulse_count++;
  }
}

static void service(void *user_data) {
  chip_state_t *s = (chip_state_t *)user_data;
  s->service_ticks++;

  /* 20 x 50 ms = 1 second measurement window. */
  if ((s->service_ticks % 20u) != 0u) {
    return;
  }

  s->last_rate_hz = s->pulse_count;
  s->pulse_count = 0u;

  float load_pct = (float)s->last_rate_hz;
  if (load_pct > 100.0f) {
    load_pct = 100.0f;
  }

  pin_dac_write(
      s->load_out,
      (load_pct / 100.0f) * 5.0f
  );
}

void chip_init(void) {
  static chip_state_t state;
  chip_state_t *s = &state;

  s->packet_in = pin_init("PACKET_IN", INPUT_PULLDOWN);
  s->load_out = pin_init("LOAD_OUT", ANALOG);
  s->activity = pin_init("ACTIVITY", OUTPUT_LOW);

  const pin_watch_config_t packet_watch = {
      .user_data = s,
      .edge = BOTH,
      .pin_change = packet_edge,
  };
  pin_watch(s->packet_in, &packet_watch);

  const timer_config_t service_cfg = {
      .user_data = s,
      .callback = service,
      .reserved = {0},
  };
  s->service_timer = timer_init(&service_cfg);
  timer_start(s->service_timer, 50000u, true);
}
