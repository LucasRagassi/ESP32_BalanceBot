#pragma once
#include "balance/wifi_observer.hpp"
#include "test_wifi.hpp"

namespace wifi_observer_test {
balance::ApDriverEvidence healthyDriver() {
    balance::ApDriverEvidence d;
    d.modeOk = d.configOk = d.channelOk = d.clientsOk = d.interfaceUp = d.ipAssigned = true;
    d.ssidNonempty = true; d.ssidHidden = false;
    d.ssidMatchesExpected = true;
    d.mode = 3; d.channel = 1;
    return d;
}
}
void test_ap_ip_and_boot_success_cannot_mask_stop_or_driver_failures() {
    using namespace balance;
    ApObservation ap;
    auto driver = wifi_observer_test::healthyDriver();
    TEST_ASSERT_FALSE(apDriverReady(ap, driver)); // IP and mode alone are insufficient.
    observeAp(ap, ApEvent::Start, 100);
    TEST_ASSERT_TRUE(apDriverReady(ap, driver));
    observeAp(ap, ApEvent::Stop, 60000);
    TEST_ASSERT_FALSE(apDriverReady(ap, driver)); // IP still assigned, no implicit reset/recovery.
    TEST_ASSERT_EQUAL_UINT32(1, ap.stops);
    TEST_ASSERT_EQUAL_UINT32(60000, ap.atMs);
    TEST_ASSERT_EQUAL_STRING("AP_STOP", apEventName(ap.last));
    observeAp(ap, ApEvent::Start, 61000);
    TEST_ASSERT_EQUAL_UINT32(2, ap.starts);
    TEST_ASSERT_TRUE(apDriverReady(ap, driver));
    driver.mode = 1; TEST_ASSERT_FALSE(apDriverReady(ap, driver));
    driver = wifi_observer_test::healthyDriver(); driver.configOk = false;
    TEST_ASSERT_FALSE(apDriverReady(ap, driver));
    driver = wifi_observer_test::healthyDriver(); driver.ssidHidden = true;
    TEST_ASSERT_FALSE(apDriverReady(ap, driver));
    driver = wifi_observer_test::healthyDriver(); driver.ssidMatchesExpected = false;
    TEST_ASSERT_FALSE(apDriverReady(ap, driver));
    driver = wifi_observer_test::healthyDriver(); driver.channel = 0;
    TEST_ASSERT_FALSE(apDriverReady(ap, driver));
    driver = wifi_observer_test::healthyDriver(); driver.interfaceUp = false;
    TEST_ASSERT_FALSE(apDriverReady(ap, driver));
    driver = wifi_observer_test::healthyDriver(); driver.clientsOk = false;
    TEST_ASSERT_FALSE(apDriverReady(ap, driver));
}
void test_unconfigured_sta_seven_days_has_no_radio_mutation_or_automatic_scan() {
    using namespace balance;
    wifi_test::MemorySlots storage;
    WifiCredentialJournal journal(storage);
    wifi_test::Radio radio;
    WifiManager manager(radio, journal);
    ExplicitScan scan;
    ApObservation ap;
    const auto driver = wifi_observer_test::healthyDriver();
    observeAp(ap, ApEvent::Start, 0);
    manager.begin(0);
    for (uint32_t now = 0; now <= 7U * 24U * 60U * 60U * 1000U; now += 1000) {
        manager.tick(now, false, false);
        TEST_ASSERT_EQUAL_INT(int(WifiPhase::Unconfigured), int(manager.phase()));
        TEST_ASSERT_TRUE(apDriverReady(ap, driver));
    }
    TEST_ASSERT_EQUAL_UINT(0, radio.connections);
    TEST_ASSERT_EQUAL_UINT(0, radio.disconnections);
    TEST_ASSERT_EQUAL_UINT32(0, scan.starts());
    TEST_ASSERT_EQUAL_UINT32(0, ap.stops);
    TEST_ASSERT_EQUAL_UINT(0, storage.writes);
}
void test_scan_requires_explicit_start_and_guards_overlap_cooldown_and_wrap() {
    using namespace balance;
    ExplicitScan scan;
    const uint32_t first = UINT32_MAX - 100;
    TEST_ASSERT_FALSE(scan.allowed(first, false, false)); // AP not ready.
    TEST_ASSERT_FALSE(scan.allowed(first, true, true)); // STA busy.
    TEST_ASSERT_TRUE(scan.allowed(first, false, true));
    TEST_ASSERT_EQUAL_UINT32(0, scan.starts()); // Querying eligibility does not start a scan.
    scan.started(first, -1);
    TEST_ASSERT_TRUE(scan.running());
    TEST_ASSERT_FALSE(scan.allowed(first + 20000, false, true));
    scan.finished(first + 20001, 5);
    TEST_ASSERT_EQUAL_STRING("complete", scanStateName(scan.state()));
    TEST_ASSERT_EQUAL_INT(5, scan.result());
    TEST_ASSERT_TRUE(scan.allowed(first + 20002, false, true));
    scan.started(first + 20002, -1); scan.finished(first + 21000, 0);
    TEST_ASSERT_FALSE(scan.allowed(first + 34999, false, true));
    TEST_ASSERT_TRUE(scan.allowed(first + 35002, false, true));
    scan.started(first + 35002, -2);
    TEST_ASSERT_FALSE(scan.running());
    TEST_ASSERT_EQUAL_STRING("failed", scanStateName(scan.state()));
    TEST_ASSERT_EQUAL_UINT32(3, scan.starts());
}
void test_failed_trial_without_saved_sta_returns_to_idle_without_ap_reset() {
    using namespace balance;
    wifi_test::MemorySlots storage; WifiCredentialJournal journal(storage); wifi_test::Radio radio;
    WifiManager manager(radio, journal); manager.begin(0);
    TEST_ASSERT_TRUE(manager.connectAndSave(wifi_test::credentials("MissingRouter"), 100));
    manager.tick(25100, false, false);
    TEST_ASSERT_EQUAL_INT(int(WifiPhase::Unconfigured), int(manager.phase()));
    const auto connects = radio.connections, disconnects = radio.disconnections;
    for (uint32_t now = 25101; now < 172800000; now += 1000) manager.tick(now, false, false);
    TEST_ASSERT_EQUAL_UINT(connects, radio.connections);
    TEST_ASSERT_EQUAL_UINT(disconnects, radio.disconnections);
    TEST_ASSERT_EQUAL_UINT(0, storage.writes);
}
