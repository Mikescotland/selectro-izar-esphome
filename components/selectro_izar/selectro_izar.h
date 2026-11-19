#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "izar_decoder.h"
#include "flow_estimator.h"

namespace esphome { namespace selectro_izar {

static constexpr unsigned MAX_SLOTS = 10;
static constexpr unsigned MAX_FOUND = 40;

// Slots have fixed Home Assistant entity IDs, while their configured meter IDs
// are stored in ESPHome flash preferences and can be changed from the web UI.
class Gateway final : public Component, public AsyncWebHandler {
 public:
  void setup() override;
  void dump_config() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  bool handle_packet(const std::vector<uint8_t> &frame, int8_t rssi);
  void set_admin_credentials(const std::string &user,const std::string &pass) { admin_username_=user;admin_password_=pass; }

  optional<float> slot_total(unsigned i) const;
  optional<float> slot_flow(unsigned i) const;
  optional<float> slot_target(unsigned i) const;
  optional<float> slot_rssi(unsigned i) const;
  optional<float> slot_battery(unsigned i) const;
  optional<float> slot_age_s(unsigned i) const;
  optional<float> slot_alarm_mask(unsigned i) const;
  optional<bool> slot_alarm(unsigned i, uint16_t mask) const;
  std::string slot_printed(unsigned i) const;
  optional<bool> slot_leak(unsigned i) const;
  optional<bool> slot_stale(unsigned i) const;
  std::string slot_status(unsigned i) const;
  std::string slot_alarm_details(unsigned i) const;
  std::string slot_id(unsigned i) const;

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  bool isRequestHandlerTrivial() const override { return false; }
 private:
  struct Slot {
    uint32_t id{0};
    char name[24]{};
    char key[17]{}; // optional 8-byte LFSR key (16 hex chars); NEVER returned through API
  };
  struct Storage {
    uint32_t magic{0};
    uint32_t version{0};
    Slot slots[MAX_SLOTS]{};
    uint32_t check{0};
  };
  struct Found {
    uint32_t id{0};
    int8_t rssi{0};
    uint32_t seen_ms{0};
    bool valid{false};
    uint32_t total_l{0};
    uint32_t target_l{0};
    bool has_target{false};
    float battery_y{0};
    uint16_t flags{0};
    FlowEstimator flow{};
    bool general_alarm{false};
    char decoder[19]{};
    char error[57]{};
    char printed[18]{};
  };
  mutable std::mutex mutex_;
  Storage cfg_{};
  std::array<Found, MAX_FOUND> found_{};
  ESPPreferenceObject pref_{};
  uint32_t rx_count_{0};
  uint32_t decoded_count_{0};
  std::atomic<uint8_t> pending_reset_{0}; // 1: Wi-Fi only, 2: all NVS
  std::atomic<uint32_t> reset_due_ms_{0};
  std::string admin_username_{};
  std::string admin_password_{};
  const Found *find_unsafe_(uint32_t id) const;
  static uint32_t checksum_(const Storage &s);
  void save_unsafe_();
  std::string state_json_() const;
  static std::string escape_json_(const char *s);
  static std::string id_to_string_(uint32_t id);
  static bool parse_id_(const std::string &s, uint32_t *id);
  void bind_(AsyncWebServerRequest *req);
  void request_reset_(AsyncWebServerRequest *req,uint8_t action);
  bool erase_saved_wifi_();
};

} } // namespace
