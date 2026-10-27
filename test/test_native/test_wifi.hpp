#pragma once
#include "balance/wifi_manager.hpp"
#include <cstring>
#include <initializer_list>

using namespace balance;
namespace wifi_test {
WifiCredentials credentials(const char* ssid = "Router24", const char* password = "secret-test") {
    WifiCredentials value;
    strncpy(value.ssid, ssid, sizeof(value.ssid) - 1);
    strncpy(value.password, password, sizeof(value.password) - 1);
    return value;
}
class MemorySlots final : public WifiSlotStorage {
public:
    WifiRecord records[2];
    bool present[2]{};
    uint8_t active = 255;
    bool failWrite = false, failSelect = false, failRead = false, failSelectorRead = false;
    bool corruptWrite = false, failReadInactive = false;
    unsigned writes = 0;
    bool readSlot(uint8_t slot, WifiRecord& value) override {
        if (failRead || (failReadInactive && slot != active) || slot > 1 || !present[slot]) return false;
        value = records[slot]; return true;
    }
    bool writeSlot(uint8_t slot, const WifiRecord& value) override {
        ++writes;
        if (slot > 1) return false;
        records[slot] = value; present[slot] = true;
        if (failWrite) { records[slot].bytes[70] ^= 1; return false; }
        if (corruptWrite) records[slot].bytes[70] ^= 1;
        return true;
    }
    bool readActive(uint8_t& slot) override {
        if (failSelectorRead) return false;
        slot = active; return true;
    }
    bool writeActive(uint8_t slot) override {
        if (failSelect) return false;
        active = slot; return true;
    }
};
class Radio final : public WifiRadio {
public:
    WifiCredentials last;
    unsigned connections = 0, disconnections = 0;
    void connect(const WifiCredentials& credentials, uint32_t) override { last = credentials; ++connections; }
    void disconnect() override { ++disconnections; }
};
}

void test_wifi_credentials_limits_and_subnet_overlap() {
    using namespace wifi_test;
    TEST_ASSERT_TRUE(validWifiCredentials(credentials()));
    TEST_ASSERT_TRUE(validWifiCredentials(credentials("Hidden24", "")));
    TEST_ASSERT_FALSE(validWifiCredentials(credentials("", "password")));
    TEST_ASSERT_FALSE(validWifiCredentials(credentials("Router", "short")));
    auto value = credentials(); memset(value.password, 'f', 64); value.password[64] = 0;
    TEST_ASSERT_TRUE(validWifiCredentials(value));
    value.password[0] = 'z'; TEST_ASSERT_FALSE(validWifiCredentials(value));
    memset(value.ssid, 'x', sizeof(value.ssid)); TEST_ASSERT_FALSE(validWifiCredentials(value));
    TEST_ASSERT_TRUE(subnetOverlapsAp(0xc0a8040a, 0xffffff00));
    TEST_ASSERT_TRUE(subnetOverlapsAp(0xc0a80002, 0xffff0000));
    TEST_ASSERT_TRUE(subnetOverlapsAp(0xc0a80502, 0xfffffe00));
    TEST_ASSERT_FALSE(subnetOverlapsAp(0xc0a80102, 0xffffff00));
    TEST_ASSERT_FALSE(subnetOverlapsAp(0x0a000002, 0xff000000));
    TEST_ASSERT_TRUE(subnetOverlapsAp(0xc0a80102, 0));
    TEST_ASSERT_TRUE(subnetOverlapsAp(0xc0a80102, 0xff00ff00));
    TEST_ASSERT_TRUE(is24GhzChannel(1)); TEST_ASSERT_TRUE(is24GhzChannel(14));
    TEST_ASSERT_FALSE(is24GhzChannel(0)); TEST_ASSERT_FALSE(is24GhzChannel(36));
    clearWifiCredentials(value);
    TEST_ASSERT_EQUAL_CHAR(0, value.password[0]); TEST_ASSERT_EQUAL_CHAR(0, value.ssid[0]);
}
void test_wifi_journal_rollback_and_unselected_candidate_after_restart() {
    using namespace wifi_test;
    MemorySlots storage; WifiCredentialJournal journal(storage); WifiCredentials loaded;
    TEST_ASSERT_FALSE(journal.load(loaded));
    TEST_ASSERT_TRUE(journal.save(credentials("Previous24")));
    const uint8_t previous = storage.active;
    storage.failSelect = true;
    TEST_ASSERT_FALSE(journal.save(credentials("Candidate24")));
    TEST_ASSERT_EQUAL_UINT8(previous, storage.active);
    WifiCredentialJournal reboot(storage);
    TEST_ASSERT_TRUE(reboot.load(loaded)); TEST_ASSERT_EQUAL_STRING("Previous24", loaded.ssid);
    storage.failSelect = false;
    TEST_ASSERT_TRUE(journal.save(credentials("Candidate24")));
    TEST_ASSERT_NOT_EQUAL(previous, storage.active);
    TEST_ASSERT_TRUE(reboot.load(loaded)); TEST_ASSERT_EQUAL_STRING("Candidate24", loaded.ssid);
}
void test_wifi_journal_partial_write_and_read_errors_preserve_active() {
    using namespace wifi_test;
    MemorySlots storage; WifiCredentialJournal journal(storage); WifiCredentials loaded;
    TEST_ASSERT_TRUE(journal.save(credentials("Previous24")));
    const uint8_t previous = storage.active;
    storage.failWrite = true;
    TEST_ASSERT_FALSE(journal.save(credentials("Broken24")));
    storage.failWrite = false;
    TEST_ASSERT_TRUE(journal.load(loaded)); TEST_ASSERT_EQUAL_STRING("Previous24", loaded.ssid);
    storage.failSelectorRead = true;
    const unsigned writes = storage.writes;
    TEST_ASSERT_FALSE(journal.save(credentials("DoNotWrite")));
    TEST_ASSERT_EQUAL_UINT(writes, storage.writes);
    storage.failSelectorRead = false; storage.failRead = true;
    TEST_ASSERT_FALSE(journal.save(credentials("DoNotWrite")));
    TEST_ASSERT_EQUAL_UINT(writes, storage.writes);
    storage.failRead = false;
    TEST_ASSERT_EQUAL_UINT8(previous, storage.active);
}
void test_wifi_journal_crc_version_and_unconfirmed_first_slot() {
    using namespace wifi_test;
    MemorySlots storage; WifiCredentialJournal journal(storage); WifiCredentials loaded;
    storage.failSelect = true;
    TEST_ASSERT_FALSE(journal.save(credentials("Unconfirmed")));
    TEST_ASSERT_FALSE(journal.load(loaded));
    storage.failSelect = false;
    TEST_ASSERT_TRUE(journal.save(credentials("Confirmed")));
    storage.records[storage.active].bytes[70] ^= 1;
    TEST_ASSERT_FALSE(journal.load(loaded));
    TEST_ASSERT_FALSE(journal.save(credentials("NoImplicitRepair")));
    storage.records[storage.active].bytes[70] ^= 1;
    storage.records[storage.active].bytes[3] = '2';
    TEST_ASSERT_FALSE(journal.load(loaded));
    TEST_ASSERT_EQUAL_CHAR(0, loaded.password[0]);
}
namespace wifi_test {
WifiLease lease(WifiManager& manager) { return {manager.epoch(), 0xc0a8641aU, 0xffffff00U, 0xc0a86401U}; }
bool online(WifiManager& manager, uint32_t now) { manager.associated(manager.epoch()); return manager.gotIp(now, lease(manager)); }
}
void test_wifi_explicit_dhcp_autosave_is_idempotent_and_persistent() {
    using namespace wifi_test;
    MemorySlots storage; WifiCredentialJournal journal(storage); Radio radio; WifiManager manager(radio, journal);
    manager.begin(0);
    TEST_ASSERT_FALSE(online(manager, 101)); TEST_ASSERT_EQUAL_UINT(0, storage.writes);
    TEST_ASSERT_TRUE(manager.connectAndSave(credentials(), 102));
    TEST_ASSERT_FALSE(manager.connectAndSave(credentials("Concurrent"), 103));
    manager.tick(104, true, false); // Link flag alone cannot save.
    TEST_ASSERT_EQUAL_UINT(0, storage.writes);
    TEST_ASSERT_FALSE(manager.gotIp(105, lease(manager))); // No association proof.
    TEST_ASSERT_TRUE(online(manager, 106));
    TEST_ASSERT_EQUAL_UINT(1, storage.writes); TEST_ASSERT_TRUE(manager.hasSaved());
    TEST_ASSERT_EQUAL_STRING("saved", manager.result()); TEST_ASSERT_EQUAL_UINT32(0xc0a8641aU, manager.resultIp());
    TEST_ASSERT_FALSE(online(manager, 107)); manager.cancel(108); // duplicate/cancel after commit are no-ops
    manager.tick(90000, true, false); TEST_ASSERT_EQUAL_UINT(1, storage.writes);
    TEST_ASSERT_EQUAL_INT(int(WifiPhase::Online), int(manager.phase()));
    manager.tick(90001, false, false); // Future disconnect keeps persisted credentials/result.
    TEST_ASSERT_EQUAL_STRING("saved", manager.result()); TEST_ASSERT_TRUE(manager.hasSaved());
    Radio rebootRadio; WifiManager reboot(rebootRadio, journal); reboot.begin(100000);
    TEST_ASSERT_EQUAL_STRING("Router24", rebootRadio.last.ssid);
    TEST_ASSERT_TRUE(online(reboot, 100001)); TEST_ASSERT_EQUAL_UINT(1, storage.writes);
}
void test_wifi_timeout_dhcp_auth_cancel_and_late_epoch_restore_previous() {
    using namespace wifi_test;
    MemorySlots storage; WifiCredentialJournal journal(storage); Radio radio;
    TEST_ASSERT_TRUE(journal.save(credentials("Previous")));
    WifiManager manager(radio, journal); manager.begin(0); TEST_ASSERT_TRUE(online(manager, 1));
    TEST_ASSERT_TRUE(manager.connectAndSave(credentials("Missing"), 10));
    const auto stale = lease(manager);
    manager.tick(25010, false, false);
    TEST_ASSERT_EQUAL_INT(int(WifiError::Timeout), int(manager.error()));
    TEST_ASSERT_EQUAL_STRING("failed", manager.result());
    manager.tick(26010, false, false); TEST_ASSERT_EQUAL_STRING("Previous", radio.last.ssid);
    TEST_ASSERT_TRUE(online(manager, 26011));
    TEST_ASSERT_TRUE(manager.connectAndSave(credentials("NoDHCP"), 27000));
    manager.associated(manager.epoch()); manager.tick(52000, false, false);
    TEST_ASSERT_EQUAL_INT(int(WifiError::Dhcp), int(manager.resultError()));
    TEST_ASSERT_TRUE(manager.connectAndSave(credentials("LateDHCP"), 53000));
    TEST_ASSERT_FALSE(online(manager, 78000)); // No commit after the transaction deadline.
    TEST_ASSERT_EQUAL_UINT(1, storage.writes);
    TEST_ASSERT_TRUE(manager.connectAndSave(credentials("New"), 79000));
    manager.associated(manager.epoch());
    TEST_ASSERT_FALSE(manager.gotIp(79001, stale)); // Late GOT_IP for previous transaction.
    manager.failed(79002, stale.epoch, WifiError::Authentication);
    TEST_ASSERT_TRUE(manager.trialActive());
    manager.failed(79003, manager.epoch(), WifiError::Authentication);
    TEST_ASSERT_EQUAL_INT(int(WifiError::Authentication), int(manager.resultError()));
    TEST_ASSERT_TRUE(manager.connectAndSave(credentials("Cancelled"), 80000));
    const auto cancelled = lease(manager); manager.cancel(80001);
    manager.associated(cancelled.epoch); TEST_ASSERT_FALSE(manager.gotIp(80002, cancelled));
    TEST_ASSERT_EQUAL_STRING("cancelled", manager.result());
    WifiCredentials loaded; TEST_ASSERT_TRUE(journal.load(loaded)); TEST_ASSERT_EQUAL_STRING("Previous", loaded.ssid);
    TEST_ASSERT_EQUAL_UINT(1, storage.writes);
}
void test_wifi_autosave_storage_failures_roll_back_and_reboot_old_record() {
    using namespace wifi_test;
    for (int failure = 0; failure < 5; ++failure) {
        MemorySlots storage; WifiCredentialJournal journal(storage); Radio radio;
        TEST_ASSERT_TRUE(journal.save(credentials("Previous")));
        WifiManager manager(radio, journal); manager.begin(0); TEST_ASSERT_TRUE(online(manager, 1));
        TEST_ASSERT_TRUE(manager.connectAndSave(credentials("Candidate"), 10));
        storage.failSelect = failure == 0; storage.failWrite = failure == 1; storage.failRead = failure == 2;
        storage.corruptWrite = failure == 3; storage.failReadInactive = failure == 4;
        TEST_ASSERT_FALSE(online(manager, 11));
        TEST_ASSERT_EQUAL_INT(int(WifiError::Storage), int(manager.resultError()));
        storage.failSelect = storage.failWrite = storage.failRead = storage.corruptWrite = storage.failReadInactive = false;
        Radio rebootRadio; WifiManager reboot(rebootRadio, journal); reboot.begin(100);
        TEST_ASSERT_EQUAL_STRING("Previous", rebootRadio.last.ssid);
        manager.tick(1011, false, false); TEST_ASSERT_EQUAL_STRING("Previous", radio.last.ssid);
        TEST_ASSERT_TRUE(online(manager, 1012));
        TEST_ASSERT_EQUAL_STRING("failed", manager.result()); // reconnect must not hide transaction failure
    }
}
void test_wifi_lease_validation_rejects_invalid_dhcp_and_conflicting_subnets() {
    using namespace wifi_test;
    const WifiLease valid{1, 0xc0a8641aU, 0xffffff00U, 0xc0a86401U};
    TEST_ASSERT_EQUAL_INT(int(WifiError::None), int(validateWifiLease(valid)));
    for (auto invalid : {WifiLease{1,0,valid.mask,valid.gateway}, WifiLease{1,valid.ip,0,valid.gateway},
            WifiLease{1,valid.ip,0xff00ff00U,valid.gateway}, WifiLease{1,valid.ip,valid.mask,0},
            WifiLease{1,valid.ip,valid.mask,0xc0a80101U}, WifiLease{1,0xc0a864ffU,valid.mask,valid.gateway},
            WifiLease{1,0xa9fe0102U,valid.mask,0xa9fe0101U}}) {
        TEST_ASSERT_EQUAL_INT(int(WifiError::Dhcp), int(validateWifiLease(invalid)));
    }
    MemorySlots storage; WifiCredentialJournal journal(storage); Radio radio;
    TEST_ASSERT_TRUE(journal.save(credentials("Previous")));
    WifiManager manager(radio, journal); manager.begin(0); TEST_ASSERT_TRUE(online(manager, 1));
    TEST_ASSERT_TRUE(manager.connectAndSave(credentials("Conflict"), 10)); manager.associated(manager.epoch());
    TEST_ASSERT_FALSE(manager.gotIp(11, {manager.epoch(), 0xc0a8040aU, 0xffffff00U, 0xc0a804feU}));
    TEST_ASSERT_EQUAL_INT(int(WifiError::Conflict), int(manager.resultError()));
    manager.tick(1011, false, false); TEST_ASSERT_EQUAL_STRING("Previous", radio.last.ssid);
    WifiCredentials loaded; TEST_ASSERT_TRUE(journal.load(loaded)); TEST_ASSERT_EQUAL_STRING("Previous", loaded.ssid);
}
void test_wifi_reconnect_backoff_is_bounded_and_wrap_safe() {
    using namespace wifi_test;
    MemorySlots storage; WifiCredentialJournal journal(storage); Radio radio;
    TEST_ASSERT_TRUE(journal.save(credentials()));
    WifiManager manager(radio, journal);
    uint32_t now = UINT32_MAX - 100;
    manager.begin(now);
    for (const uint32_t expected : {1000U, 2000U, 4000U, 8000U, 16000U, 30000U, 30000U}) {
        now += 25000; manager.tick(now, false, false);
        TEST_ASSERT_EQUAL_UINT32(expected, manager.retryInMs(now));
        const auto attempts = radio.connections;
        manager.tick(now + expected - 1, false, false); TEST_ASSERT_EQUAL_UINT(attempts, radio.connections);
        now += expected; manager.tick(now, false, false); TEST_ASSERT_EQUAL_UINT(attempts + 1, radio.connections);
    }
    TEST_ASSERT_TRUE(wifi_test::online(manager, ++now));
    manager.tick(++now, false, false);
    TEST_ASSERT_EQUAL_UINT32(1000, manager.retryInMs(now));
}
