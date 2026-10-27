#ifdef ARDUINO_ARCH_ESP32
#include <Arduino.h>

void balanceBotSetup();
void balanceBotLoop();

void setup() {
    balanceBotSetup();
}

void loop() {
    balanceBotLoop();
}
#endif
