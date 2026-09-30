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
  uint64_t last_update_ns;
  float dc_progress;
  float battery_progress;
  bool battery_reference_valid;
  float battery_reference_eng_v;
} chip_state_t;

static float clampf_local(float value, float minimum, float maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

static void refresh(void *user_data) {
  chip_state_t *s = (chip_state_t *)user_data;

  const uint64_t now_ns = get_sim_nanos();
  float dt_s = 0.05f;

  if (s->last_update_ns != 0 && now_ns > s->last_update_ns) {
    dt_s = (float)(now_ns - s->last_update_ns) / 1000000000.0f;
    dt_s = clampf_local(dt_s, 0.0f, 0.25f);
  }
  s->last_update_ns = now_ns;

  const float dc_in_v = clampf_local(pin_adc_read(s->dc_in), 0.0f, 5.0f);
  const float battery_in_v = clampf_local(pin_adc_read(s->battery_in), 0.0f, 5.0f);

  const bool source_ok = pin_read(s->rectifier_in) == HIGH;
  const bool fault_enabled = pin_read(s->fault_enable) == HIGH;

  if (fault_enabled) {
    s->dc_progress = clampf_local(s->dc_progress + (dt_s / 8.0f), 0.0f, 1.0f);
    s->battery_progress = clampf_local(s->battery_progress + (dt_s / 25.0f), 0.0f, 1.0f);
  } else {
    s->dc_progress = clampf_local(s->dc_progress - (dt_s / 4.0f), 0.0f, 1.0f);
    s->battery_progress = clampf_local(s->battery_progress - (dt_s / 12.5f), 0.0f, 1.0f);
  }

  const float dc_factor = 1.0f - (0.22f * s->dc_progress);
  const float dc_out_v = clampf_local(dc_in_v * dc_factor, 0.0f, 5.0f);

  const float battery_min_eng_v = 10.5f;
  const float battery_max_eng_v = 12.7f;
  const float battery_span_eng_v = battery_max_eng_v - battery_min_eng_v;

  const float live_battery_source_eng_v =
      battery_min_eng_v + ((battery_in_v / 3.3f) * battery_span_eng_v);

  /*
   * Track the live source only while healthy. During a rectifier fault, hold
   * the last healthy battery reference constant. This prevents any analog
   * back-coupling or output feedback in the simulator from recursively
   * re-applying the 8% sag to an already-sagged battery value.
   */
  if (!fault_enabled || !s->battery_reference_valid) {
    s->battery_reference_eng_v =
        clampf_local(live_battery_source_eng_v,
                     battery_min_eng_v,
                     battery_max_eng_v);
    s->battery_reference_valid = true;
  }

  const float battery_factor = 1.0f - (0.08f * s->battery_progress);

  const float battery_target_eng_v =
      clampf_local(s->battery_reference_eng_v * battery_factor,
                   battery_min_eng_v,
                   battery_max_eng_v);

  const float battery_out_v =
      clampf_local(((battery_target_eng_v - battery_min_eng_v) /
                    battery_span_eng_v) * 3.3f,
                   0.0f,
                   5.0f);

  pin_dac_write(s->dc_out, dc_out_v);
  pin_dac_write(s->battery_out, battery_out_v);

  pin_write(s->rectifier_out, (source_ok && !fault_enabled) ? HIGH : LOW);
  pin_write(s->fault_active, fault_enabled ? HIGH : LOW);

  timer_start(s->timer, 50000u, false);
}

void chip_init(void) {
  static chip_state_t state;
  static timer_config_t cfg;
  chip_state_t *s = &state;

  s->dc_in = pin_init("DC_IN", ANALOG);
  s->battery_in = pin_init("BATTERY_IN", ANALOG);
  s->rectifier_in = pin_init("RECTIFIER_IN", INPUT_PULLDOWN);
  s->fault_enable = pin_init("FAULT_ENABLE", INPUT_PULLDOWN);
  s->dc_out = pin_init("DC_OUT", ANALOG);
  s->battery_out = pin_init("BATTERY_OUT", ANALOG);
  s->rectifier_out = pin_init("RECTIFIER_OUT", OUTPUT_LOW);
  s->fault_active = pin_init("FAULT_ACTIVE", OUTPUT_LOW);

  s->last_update_ns = 0;
  s->dc_progress = 0.0f;
  s->battery_progress = 0.0f;
  s->battery_reference_valid = false;
  s->battery_reference_eng_v = 0.0f;

  cfg.user_data = s;
  cfg.callback = refresh;
  for (int i = 0; i < 8; ++i) cfg.reserved[i] = 0;

  s->timer = timer_init(&cfg);
  refresh(s);
}
