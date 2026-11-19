#pragma once
namespace esphome {
namespace setup_priority { static constexpr float AFTER_WIFI=200.0f; }
class Component {public: virtual ~Component()=default; virtual void setup(){} virtual void dump_config(){} virtual void loop(){} virtual float get_setup_priority()const{return 0;}};
}
#define ESP_LOGI(...) (void)0
#define ESP_LOGW(...) (void)0
#define ESP_LOGE(...) (void)0
#define ESP_LOGCONFIG(...) (void)0
