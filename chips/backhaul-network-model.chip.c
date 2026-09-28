#include "wokwi-api.h"

static pin_t traffic_in_pin;
static pin_t latency_pin;
static pin_t loss_pin;
static pin_t link_pin;
static pin_t upstream_pin;

static uint32_t base_latency_attr;
static uint32_t congestion_attr;
static uint32_t traffic_coupling_attr;
static uint32_t impairment_attr;
static uint32_t jitter_attr;
static uint32_t physical_failure_attr;
static uint32_t upstream_failure_attr;
static timer_t update_timer;
static uint32_t prng_state = 0x5a17c3e1u;

static float clampf_local(float value, float minimum, float maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

static float maxf_local(float a, float b) {
  return a > b ? a : b;
}

static float random_signed(void) {
  prng_state = prng_state * 1664525u + 1013904223u;
  uint32_t sample = (prng_state >> 8) & 0x00ffffffu;
  float unit = (float)sample / 16777215.0f;
  return unit * 2.0f - 1.0f;
}

static void update_network(void *user_data) {
  (void)user_data;

  float base_latency_ms = clampf_local(attr_read_float(base_latency_attr), 10.0f, 250.0f);
  float injected_congestion_pct = clampf_local(attr_read_float(congestion_attr), 0.0f, 100.0f);
  float traffic_coupling_pct = clampf_local(attr_read_float(traffic_coupling_attr), 0.0f, 100.0f);
  float impairment_pct = clampf_local(attr_read_float(impairment_attr), 0.0f, 100.0f);
  float jitter_ms = clampf_local(attr_read_float(jitter_attr), 0.0f, 200.0f);
  bool physical_failure = attr_read(physical_failure_attr) >= 1u;
  bool upstream_failure = attr_read(upstream_failure_attr) >= 1u;

  float traffic_voltage = clampf_local(pin_adc_read(traffic_in_pin), 0.0f, 5.0f);
  float traffic_pct = (traffic_voltage / 5.0f) * 100.0f;
  float traffic_congestion_pct = traffic_pct * (traffic_coupling_pct / 100.0f);
  float congestion_pct = clampf_local(
    maxf_local(injected_congestion_pct, traffic_congestion_pct),
    0.0f,
    100.0f
  );

  float latency_ms;
  float packet_loss_pct;
  bool link_up = !physical_failure;
  bool upstream_reachable = !physical_failure && !upstream_failure;

  if (!link_up || !upstream_reachable) {
    latency_ms = 1000.0f;
    packet_loss_pct = 100.0f;
  } else {
    float congestion_delay = 0.05f * congestion_pct * congestion_pct;
    float impairment_delay = 1.50f * impairment_pct;
    float jitter_component = random_signed() * jitter_ms;

    latency_ms = base_latency_ms + congestion_delay + impairment_delay + jitter_component;
    latency_ms = clampf_local(latency_ms, 10.0f, 1000.0f);

    float congestion_loss = congestion_pct > 60.0f
      ? (congestion_pct - 60.0f) * 1.20f
      : 0.0f;
    float impairment_loss = impairment_pct * 0.35f;
    float random_loss = random_signed() * 1.5f;

    packet_loss_pct = congestion_loss + impairment_loss + random_loss;
    packet_loss_pct = clampf_local(packet_loss_pct, 0.0f, 100.0f);
  }

  float latency_voltage = ((latency_ms - 10.0f) / 990.0f) * 5.0f;
  float loss_voltage = (packet_loss_pct / 100.0f) * 5.0f;

  pin_dac_write(latency_pin, clampf_local(latency_voltage, 0.0f, 5.0f));
  pin_dac_write(loss_pin, clampf_local(loss_voltage, 0.0f, 5.0f));
  pin_write(link_pin, link_up ? HIGH : LOW);
  pin_write(upstream_pin, upstream_reachable ? HIGH : LOW);
}

void chip_init(void) {
  traffic_in_pin = pin_init("TRAFFIC_IN", ANALOG);
  latency_pin = pin_init("LATENCY", ANALOG);
  loss_pin = pin_init("LOSS", ANALOG);
  link_pin = pin_init("LINK", OUTPUT_LOW);
  upstream_pin = pin_init("UPSTREAM", OUTPUT_LOW);

  base_latency_attr = attr_init_float("baseLatencyMs", 45.0f);
  congestion_attr = attr_init_float("congestionPct", 20.0f);
  traffic_coupling_attr = attr_init_float("trafficCouplingPct", 70.0f);
  impairment_attr = attr_init_float("lineImpairmentPct", 2.0f);
  jitter_attr = attr_init_float("jitterMs", 8.0f);
  physical_failure_attr = attr_init("physicalLinkFailure", 0u);
  upstream_failure_attr = attr_init("upstreamFailure", 0u);

  const timer_config_t config = {
    .user_data = 0,
    .callback = update_network,
    .reserved = {0}
  };
  update_timer = timer_init(&config);

  update_network(0);
  timer_start(update_timer, 50000u, true);
}
