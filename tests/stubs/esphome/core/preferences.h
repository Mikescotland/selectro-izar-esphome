#pragma once
#include <cstddef>
#include <cstdint>
namespace esphome {
struct ESPPreferenceObject{ template<typename T> bool load(T *){return false;} template<typename T>bool save(T*){return true;} };
struct ESPPreferences {template<typename T>ESPPreferenceObject make_preference(uint32_t,bool){return {};};bool sync(){return true;}};
extern ESPPreferences *global_preferences;
}
