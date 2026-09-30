#include "wokwi-api.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  pin_t dc_in;
  pin_t battery_in;
  pin_t rectifier_in;
  pin_t fault_enable;
  pin_t dc_out;
  pin_t battery_out;
  pin_t rectifier_out;
  pin_t fault_active;
  timer_t timer;
  float dc_progress;
  float battery_progress;
} chip_state_t;

static float clampf_local(float value, float minimum, float maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

static void refresh(void *user_data) {
  chip_state_t *s = (chip_state_t *)user_data;

  // Wokwi custom-chip analog I/O uses a 0..5V virtual reference.
  // Keep the full range so healthy mode is a true pass-through.
  const float dc_in_v = clampf_local(pin_adc_read(s->dc_in), 0.0f, 5.0f);
  const float battery_in_v = clampf_local(pin_adc_read(s->battery_in), 0.0f, 5.0f);
  const bool source_ok = pin_read(s->rectifier_in) == HIGH;
  const bool fault_enabled = pin_read(s->fault_enable) == HIGH;

  if (fault_enabled) {
    // 50ms timer: DC sag reaches full effect in ~8 seconds.
    s->dc_progress = clampf_local(s->dc_progress + 0.00625f, 0.0f, 1.0f);
    // Battery discharge develops more slowly, reaching full effect in ~25 seconds.
    s->battery_progress = clampf_local(s->battery_progress + 0.0020f, 0.0f, 1.0f);
  } else {
    // Recovery is faster once the rectifier is restored.
    s->dc_progress = clampf_local(s->dc_progress - 0.0125f, 0.0f, 1.0f);
    s->battery_progress = clampf_local(s->battery_progress - 0.0040f, 0.0f, 1.0f);
  }

  // Rectifier failure causes the DC bus to sag toward 78% of source voltage.
  const float dc_factor = 1.0f - (0.22f * s->dc_progress);
  // Battery voltage gradually sags toward 92% of source voltage while carrying the site.
  const float battery_factor = 1.0f - (0.08f * s->battery_progress);

  const float dc_out_v = clampf_local(dc_in_v * dc_factor, 0.0f, 5.0f);
  const float battery_out_v = clampf_local(battery_in_v * battery_factor, 0.0f, 5.0f);

  pin_dac_write(s->dc_out, dc_out_v);
  pin_dac_write(s->battery_out, battery_out_v);
  pin_write(s->rectifier_out, (source_ok && !fault_enabled) ? HIGH : LOW);
  pin_write(s->fault_active, fault_enabled ? HIGH : LOW);
}

void chip_init(void) {
  static chip_state_t state;
  chip_state_t *s = &state;

  s->dc_in = pin_init("DC_IN", ANALOG);
  s->battery_in = pin_init("BATTERY_IN", ANALOG);
  s->rectifier_in = pin_init("RECTIFIER_IN", INPUT_PULLDOWN);
  s->fault_enable = pin_init("FAULT_ENABLE", INPUT_PULLDOWN);

  s->dc_out = pin_init("DC_OUT", ANALOG);
  s->battery_out = pin_init("BATTERY_OUT", ANALOG);
  s->rectifier_out = pin_init("RECTIFIER_OUT", OUTPUT_LOW);
  s->fault_active = pin_init("FAULT_ACTIVE", OUTPUT_LOW);

  s->dc_progress = 0.0f;
  s->battery_progress = 0.0f;

  const timer_config_t cfg = {
    .user_data = s,
    .callback = refresh,
    .reserved = {0},
  };

  s->timer = timer_init(&cfg);
  refresh(s);
  timer_start(s->timer, 50000u, true);
}
