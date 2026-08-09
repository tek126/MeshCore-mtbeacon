#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

R1NeoBoard board;

DISPLAY_CLASS display;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#ifdef PIN_USER_BTN
// v1.17.0 UITask references user_btn when PIN_USER_BTN + DISPLAY_CLASS are set,
// but upstream never defined it for this board (declared extern in target.h only).
MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea    = MicroNMEALocationProvider(Serial1, &rtc_clock);
  EnvironmentSensorManager  sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

bool radio_init() {
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
