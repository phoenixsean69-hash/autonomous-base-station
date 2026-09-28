#include "wokwi-api.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  pin_t traffic_in;
  pin_t probe_in;
  pin_t ack_out;
  pin_t latency_out;
  pin_t loss_out;
  pin_t link_out;
  pin_t upstream_out;
  pin_t drop_out;
  pin_t congest;
  pin_t impair;
  pin_t link_fail;
  pin_t upstream_fail;
  timer_t service_timer;
  timer_t ack_timer;
  timer_t drop_timer;
  uint32_t service_ticks;
  uint32_t traffic_pulses;
  uint32_t traffic_rate_hz;
  uint32_t probe_sequence;
  bool ack_high;
  float current_latency_ms;
  float current_loss_pct;
} chip_state_t;

static float clampf_local(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void traffic_edge(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  if (value == HIGH) ((chip_state_t *)user_data)->traffic_pulses++;
}

static void drop_off(void *user_data) {
  chip_state_t *s = (chip_state_t *)user_data;
  pin_write(s->drop_out, LOW);
}

static void ack_event(void *user_data) {
  chip_state_t *s = (chip_state_t *)user_data;
  if (!s->ack_high) {
    s->ack_high = true;
    pin_write(s->ack_out, HIGH);
    timer_start(s->ack_timer, 4000u, false);
  } else {
    s->ack_high = false;
    pin_write(s->ack_out, LOW);
  }
}

static bool should_drop(chip_state_t *s, bool congest, bool impair) {
  if (impair && congest) return (s->probe_sequence % 2u) == 0u;
  if (impair) return (s->probe_sequence % 3u) == 0u;
  if (congest && s->traffic_rate_hz >= 60u) return (s->probe_sequence % 5u) == 0u;
  return false;
}

static void probe_edge(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  chip_state_t *s = (chip_state_t *)user_data;
  if (value != HIGH || s->ack_high) return;

  s->probe_sequence++;
  bool link_fail = pin_read(s->link_fail) == HIGH;
  bool upstream_fail = pin_read(s->upstream_fail) == HIGH;
  bool congest = pin_read(s->congest) == HIGH;
  bool impair = pin_read(s->impair) == HIGH;

  if (link_fail || upstream_fail || should_drop(s, congest, impair)) {
    pin_write(s->drop_out, HIGH);
    timer_start(s->drop_timer, 70000u, false);
    return;
  }

  timer_start(s->ack_timer, (uint32_t)s->current_latency_ms * 1000u, false);
}

static void service(void *user_data) {
  chip_state_t *s = (chip_state_t *)user_data;
  s->service_ticks++;

  bool link_fail = pin_read(s->link_fail) == HIGH;
  bool upstream_fail = pin_read(s->upstream_fail) == HIGH;
  bool congest = pin_read(s->congest) == HIGH;
  bool impair = pin_read(s->impair) == HIGH;
  bool link_ok = !link_fail;
  bool upstream_ok = link_ok && !upstream_fail;

  pin_write(s->link_out, link_ok ? HIGH : LOW);
  pin_write(s->upstream_out, upstream_ok ? HIGH : LOW);

  if ((s->service_ticks % 20u) == 0u) {
    s->traffic_rate_hz = s->traffic_pulses;
    s->traffic_pulses = 0u;
  }

  if (!link_ok || !upstream_ok) {
    s->current_latency_ms = 1000.0f;
    s->current_loss_pct = 100.0f;
  } else {
    static const int8_t jitter[8] = {-12, -5, 4, 11, 18, 7, -3, 9};
    float latency = 45.0f + (float)s->traffic_rate_hz * 1.15f;
    if (congest) latency += 180.0f;
    if (impair) latency += 95.0f;
    if (s->traffic_rate_hz >= 90u) latency += 120.0f;
    latency += (float)jitter[s->service_ticks & 7u];
    s->current_latency_ms = clampf_local(latency, 10.0f, 1000.0f);

    float loss = 0.5f;
    if (congest && s->traffic_rate_hz >= 60u) loss += 8.0f;
    if (s->traffic_rate_hz >= 90u) loss += 8.0f;
    if (impair) loss += 28.0f;
    s->current_loss_pct = clampf_local(loss, 0.0f, 100.0f);
  }

  float latency_voltage = ((s->current_latency_ms - 10.0f) / 990.0f) * 5.0f;
  float loss_voltage = (s->current_loss_pct / 100.0f) * 5.0f;
  pin_dac_write(s->latency_out, clampf_local(latency_voltage, 0.0f, 5.0f));
  pin_dac_write(s->loss_out, clampf_local(loss_voltage, 0.0f, 5.0f));
}

void chip_init(void) {
  static chip_state_t state;
  chip_state_t *s = &state;
  s->traffic_in = pin_init("TRAFFIC_IN", INPUT_PULLDOWN);
  s->probe_in = pin_init("PROBE_IN", INPUT_PULLDOWN);
  s->ack_out = pin_init("ACK_OUT", OUTPUT_LOW);
  s->latency_out = pin_init("LATENCY_OUT", ANALOG);
  s->loss_out = pin_init("LOSS_OUT", ANALOG);
  s->link_out = pin_init("LINK_OUT", OUTPUT_HIGH);
  s->upstream_out = pin_init("UPSTREAM_OUT", OUTPUT_HIGH);
  s->drop_out = pin_init("DROP_OUT", OUTPUT_LOW);
  s->congest = pin_init("CONGEST", INPUT_PULLDOWN);
  s->impair = pin_init("IMPAIR", INPUT_PULLDOWN);
  s->link_fail = pin_init("LINK_FAIL", INPUT_PULLDOWN);
  s->upstream_fail = pin_init("UPSTREAM_FAIL", INPUT_PULLDOWN);
  s->current_latency_ms = 45.0f;
  s->current_loss_pct = 0.5f;

  const pin_watch_config_t traffic_watch = {.user_data=s, .edge=RISING, .pin_change=traffic_edge};
  const pin_watch_config_t probe_watch = {.user_data=s, .edge=RISING, .pin_change=probe_edge};
  pin_watch(s->traffic_in, &traffic_watch);
  pin_watch(s->probe_in, &probe_watch);

  const timer_config_t service_cfg = {.user_data=s, .callback=service, .reserved={0}};
  const timer_config_t ack_cfg = {.user_data=s, .callback=ack_event, .reserved={0}};
  const timer_config_t drop_cfg = {.user_data=s, .callback=drop_off, .reserved={0}};
  s->service_timer = timer_init(&service_cfg);
  s->ack_timer = timer_init(&ack_cfg);
  s->drop_timer = timer_init(&drop_cfg);
  service(s);
  timer_start(s->service_timer, 50000u, true);
}
