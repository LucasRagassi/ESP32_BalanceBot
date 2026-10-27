#pragma once
#ifdef ARDUINO_ARCH_ESP32
#include <WebServer.h>
#include "balance/diagnostics.hpp"
void balanceNetworkPrepare(void (*invalidateMotion)());
void balanceNetworkSetup(WebServer& server);
void balanceNetworkLoop();
void balanceNetworkHealth(balance::HealthDiagnostics& health);
// No accessor returning the secret to HTTP: explicit local serial output only.
void balanceNetworkPrintDriverStatus(); // Read-only local diagnostic, never dumps wifi_config passwords.
#endif
