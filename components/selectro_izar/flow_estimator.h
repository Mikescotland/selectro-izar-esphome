#pragma once
// Flow estimate from cumulative volume, not a direct meter measurement.
#include <cstdint>
namespace esphome { namespace selectro_izar {
class FlowEstimator {
 public:
  void observe(uint32_t total_l,uint32_t now_ms) {
    if(!initialized_ || total_l<last_l_ || (uint32_t)(now_ms-last_ms_)>600000u){
      initialized_=true;ready_=false;baseline_l_=total_l;last_l_=total_l;baseline_ms_=now_ms;last_ms_=now_ms;return;
    }
    if(now_ms==last_ms_)return;
    last_ms_=now_ms;last_l_=total_l;
    const uint32_t elapsed=(uint32_t)(now_ms-baseline_ms_);
    if(elapsed<60000u)return;
    rate_lph_=float(total_l-baseline_l_)*(3600000.f/float(elapsed));
    ready_=true;baseline_l_=total_l;baseline_ms_=now_ms;
  }
  bool ready() const{return ready_;}
  float rate_lph() const{return rate_lph_;}
 private:
  uint32_t baseline_l_{0},last_l_{0},baseline_ms_{0},last_ms_{0};
  float rate_lph_{0.f};bool initialized_{false},ready_{false};
};
} }
