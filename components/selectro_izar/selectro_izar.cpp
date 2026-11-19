#include "selectro_izar.h"
#include "portal.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <inttypes.h>
#ifdef USE_ESP32
#include "esphome/core/application.h"
#include <nvs.h>
#include <esp_err.h>
#endif

namespace esphome { namespace selectro_izar {
static const char *TAG = "selectro_izar";
static constexpr uint32_t CONFIG_MAGIC=0x53495A47u; // 'SIZG'
static constexpr uint32_t CONFIG_VERSION=1u;
static constexpr uint32_t STALE_MS=30u*60u*1000u;
static constexpr uint32_t FLOW_STALE_MS=3u*60u*1000u;
// ESPHome 2026.10.2 Wi-Fi saved-STA preference key with AP-only YAML.
static constexpr uint32_t AP_FIRST_WIFI_PREF_KEY=88491487u;
static constexpr uint32_t RESET_RESPONSE_GRACE_MS=1800u;

uint32_t Gateway::checksum_(const Storage &s) {
  const auto *p=reinterpret_cast<const uint8_t*>(&s);
  uint32_t h=2166136261u;
  for(size_t i=0; i<offsetof(Storage,check); ++i) h=(h^p[i])*16777619u;
  return h;
}

void Gateway::setup() {
  this->pref_=global_preferences->make_preference<Storage>(0x53495A31u,true);
  Storage loaded{};
  if(this->pref_.load(&loaded) && loaded.magic==CONFIG_MAGIC &&
      loaded.version==CONFIG_VERSION && loaded.check==checksum_(loaded)) {
    this->cfg_=loaded;
    ESP_LOGI(TAG,"Saved meter assignments loaded");
  } else {
    ESP_LOGI(TAG,"No saved assignments. Starting with 10 free slots.");
    this->cfg_={}; this->cfg_.magic=CONFIG_MAGIC; this->cfg_.version=CONFIG_VERSION;
  }
  if(web_server_base::global_web_server_base)
    web_server_base::global_web_server_base->add_handler(this);
  else ESP_LOGE(TAG,"No web_server_base; Selectro portal unavailable");
}
// Erase only saved Wi-Fi credentials, preserving Selectro's assigned meter slots.
// Requires AP-first YAML with no statically configured home SSID.
bool Gateway::erase_saved_wifi_() {
#ifdef USE_ESP32
  if (!global_preferences || !global_preferences->sync()) {
    ESP_LOGE(TAG,"Wi-Fi reset: unable to sync preferences");
    return false;
  }
  if (!global_preferences->nvs_handle) {
    ESP_LOGE(TAG,"Wi-Fi reset: no NVS handle");
    return false;
  }
  char key[16]{};
  std::snprintf(key,sizeof(key),"%" PRIu32,AP_FIRST_WIFI_PREF_KEY);
  esp_err_t err=nvs_erase_key(global_preferences->nvs_handle,key);
  if (err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGE(TAG,"Wi-Fi reset: failed to delete Wi-Fi key (%d)",int(err));
    return false;
  }
  err=nvs_commit(global_preferences->nvs_handle);
  if (err!=ESP_OK) {
    ESP_LOGE(TAG,"Wi-Fi reset: NVS commit failed (%d)",int(err));
    return false;
  }
  ESP_LOGI(TAG,"Saved Wi-Fi erased; meter assignments preserved");
  return true;
#else
  return false;
#endif
}

void Gateway::loop() {
  const uint8_t action=pending_reset_.load(std::memory_order_acquire);
  if(!action) return;
  if(static_cast<int32_t>(millis()-reset_due_ms_.load(std::memory_order_relaxed))<0) return;
  pending_reset_.store(0,std::memory_order_release);
  if(action==1) {
    if(!erase_saved_wifi_()) return;
  } else if(action==2) {
    if(!global_preferences || !global_preferences->reset()) {
      ESP_LOGE(TAG,"Factory reset: preference reset failed");
      return;
    }
    ESP_LOGI(TAG,"Factory reset completed");
  } else return;
#ifdef USE_ESP32
  App.safe_reboot();
#endif
}

void Gateway::request_reset_(AsyncWebServerRequest *req,uint8_t action) {
  const char *expected=(action==1)?"RESET_WIFI":"ERASE_ALL";
  if(req->arg("confirm")!=expected) {
    req->send(400,"application/json","{\"ok\":false,\"error\":\"Invalid confirmation\"}");
    return;
  }
  if(pending_reset_.load(std::memory_order_acquire)) {
    req->send(409,"application/json","{\"ok\":false,\"error\":\"Reset already pending\"}");
    return;
  }
  reset_due_ms_.store(millis()+RESET_RESPONSE_GRACE_MS,std::memory_order_relaxed);
  pending_reset_.store(action,std::memory_order_release);
  req->send(202,"application/json","{\"ok\":true,\"status\":\"reset_scheduled\"}");
}

void Gateway::dump_config() {
  ESP_LOGCONFIG(TAG,"Selectro IZAR Gateway (10 slots, max 40 nearby meters)");
  ESP_LOGCONFIG(TAG,"Open /selectro for setup; API via ESPHome Native API");
  ESP_LOGCONFIG(TAG,"Only PRIOS LFSR; AES-128/OMS not implemented");
}
void Gateway::save_unsafe_() {
  this->cfg_.magic=CONFIG_MAGIC;this->cfg_.version=CONFIG_VERSION;
  this->cfg_.check=checksum_(this->cfg_);
  if (!this->pref_.save(&this->cfg_) || !global_preferences->sync())
    ESP_LOGE(TAG,"Failed to store configuration");
}
const Gateway::Found* Gateway::find_unsafe_(uint32_t id) const {
  if(!id)return nullptr;
  for(const auto &f:found_)if(f.id==id)return &f;
  return nullptr;
}

bool Gateway::handle_packet(const std::vector<uint8_t> &frame,int8_t rssi) {
  if(frame.size()<20 || frame[0]!=frame.size()-1 ||
    (frame[1]!=0x44 && frame[1]!=0x46)) return false;
  // Only Diehl-compatible PRIOS manufacturer profiles; others remain unsupported.
  const uint16_t m=uint16_t(frame[2])|(uint16_t(frame[3])<<8);
  if(m!=0x4C30 && m!=0x11A5 && m!=0x2324) return false; // SAP, DME, HYD
  const uint32_t id=(m==0x4C30)
      ? uint32_t(frame[4])|(uint32_t(frame[5])<<8)|(uint32_t(frame[6])<<16)|(uint32_t(frame[7])<<24)
      : uint32_t(frame[6])|(uint32_t(frame[7])<<8)|(uint32_t(frame[8])<<16)|(uint32_t(frame[9])<<24);
  char user_key[17]{};
  {
    std::lock_guard<std::mutex> lk(mutex_);
    for(const auto &s:cfg_.slots)if(s.id==id && s.key[0]){
      std::memcpy(user_key,s.key,sizeof(user_key));break;
    }
  }
  IzarReading r=decode_izar_prios(frame,user_key[0]?user_key:nullptr);
  const uint32_t now=millis();
  {
    std::lock_guard<std::mutex> lk(mutex_);
    rx_count_++;
    Found *slot=nullptr, *empty=nullptr, *oldest=&found_[0];
    for(auto &x:found_) {
      if(x.id==id) {slot=&x;break;}
      if(!x.id && !empty)empty=&x;
      if((uint32_t)(now-x.seen_ms)>(uint32_t)(now-oldest->seen_ms))oldest=&x;
    }
    if(!slot)slot=empty?empty:oldest;
    FlowEstimator flow;
    if (slot->id==id) flow=slot->flow;
    if (r.valid) flow.observe(r.total_l,now);
    *slot=Found{};
    slot->id=id;slot->rssi=rssi;slot->seen_ms=now;
    slot->flow=flow;
    if(!r.printed_number.empty())
      std::snprintf(slot->printed,sizeof(slot->printed),"%s",r.printed_number.c_str());
    slot->valid=r.valid;
    if(r.valid) {
      decoded_count_++;
      slot->total_l=r.total_l;slot->target_l=r.target_l;slot->has_target=r.has_target;
      slot->battery_y=r.battery_y;slot->flags=r.alarms;slot->general_alarm=r.general_alarm;
      std::snprintf(slot->decoder,sizeof(slot->decoder),"%s",r.decoder.c_str());
    } else std::snprintf(slot->error,sizeof(slot->error),"%s",r.error.c_str());
  }
  if(r.valid)
    ESP_LOGI(TAG,"RX %08" PRIX32 " %.3f m3 RSSI %d alarm=%s via %s",
      id,r.total_l/1000.0f,int(rssi),alarms_text(r).c_str(),r.decoder.c_str());
  else ESP_LOGW(TAG,"RX %08" PRIX32 " undecodable: %s",id,r.error.c_str());
  return true; // A valid Diehl manufacturer header; decoder can still report unsupported CI.
}

optional<float> Gateway::slot_total(unsigned i) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if(i>=MAX_SLOTS)return {};
  const auto *f=find_unsafe_(cfg_.slots[i].id);
  if(!f || !f->valid || (uint32_t)(millis()-f->seen_ms)>STALE_MS)return {};
  return f->total_l/1000.f;
}
optional<float> Gateway::slot_flow(unsigned i) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if(i>=MAX_SLOTS)return {};
  const auto *f=find_unsafe_(cfg_.slots[i].id);
  if(!f || !f->valid || !f->flow.ready() ||
     (uint32_t)(millis()-f->seen_ms)>FLOW_STALE_MS) return {};
  return f->flow.rate_lph();
}
optional<float> Gateway::slot_target(unsigned i) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if(i>=MAX_SLOTS)return {};
  const auto *f=find_unsafe_(cfg_.slots[i].id);
  if(!f || !f->valid || !f->has_target || (uint32_t)(millis()-f->seen_ms)>STALE_MS)return {};
  return f->target_l/1000.f;
}
optional<float> Gateway::slot_rssi(unsigned i) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if(i>=MAX_SLOTS)return {};
  const auto *f=find_unsafe_(cfg_.slots[i].id);
  if(!f || (uint32_t)(millis()-f->seen_ms)>STALE_MS)return {};
  return float(f->rssi);
}
optional<float> Gateway::slot_battery(unsigned i) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if(i>=MAX_SLOTS)return {};
  const auto *f=find_unsafe_(cfg_.slots[i].id);
  if(!f || !f->valid || (uint32_t)(millis()-f->seen_ms)>STALE_MS)return {};
  return f->battery_y;
}
optional<float> Gateway::slot_age_s(unsigned i) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if(i>=MAX_SLOTS)return {};
  const auto *f=find_unsafe_(cfg_.slots[i].id);
  if(!f)return {};
  return (uint32_t)(millis()-f->seen_ms)/1000.0f;
}
optional<float> Gateway::slot_alarm_mask(unsigned i) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if(i>=MAX_SLOTS)return {};
  const auto *f=find_unsafe_(cfg_.slots[i].id);
  if(!f || !f->valid || (uint32_t)(millis()-f->seen_ms)>STALE_MS)return {};
  return float(f->flags);
}
optional<bool> Gateway::slot_alarm(unsigned i,uint16_t mask) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if(i>=MAX_SLOTS)return {};
  const auto *f=find_unsafe_(cfg_.slots[i].id);
  if(!f || !f->valid || (uint32_t)(millis()-f->seen_ms)>STALE_MS)return {};
  return (f->flags & mask)!=0;
}
std::string Gateway::slot_printed(unsigned i) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if(i>=MAX_SLOTS || !cfg_.slots[i].id)return "UNASSIGNED";
  const auto *f=find_unsafe_(cfg_.slots[i].id);
  return f && f->printed[0] ? std::string(f->printed) : "UNKNOWN";
}
optional<bool> Gateway::slot_leak(unsigned i) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if(i>=MAX_SLOTS)return {};
  const auto *f=find_unsafe_(cfg_.slots[i].id);
  if(!f || !f->valid || (uint32_t)(millis()-f->seen_ms)>STALE_MS)return {};
  return bool(f->flags&LEAKAGE);
}
optional<bool> Gateway::slot_stale(unsigned i) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if(i>=MAX_SLOTS || !cfg_.slots[i].id)return {};
  const auto *f=find_unsafe_(cfg_.slots[i].id);
  if(!f || !f->valid)return true;
  return (uint32_t)(millis()-f->seen_ms)>STALE_MS;
}
std::string Gateway::slot_status(unsigned i) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if(i>=MAX_SLOTS || !cfg_.slots[i].id)return "UNASSIGNED";
  const auto *f=find_unsafe_(cfg_.slots[i].id);
  if(!f)return "WAITING_FOR_TELEGRAM";
  if((uint32_t)(millis()-f->seen_ms)>STALE_MS)return "STALE";
  if(!f->valid)return "UNDECODABLE";
  IzarReading r;r.valid=true;r.alarms=f->flags;r.general_alarm=f->general_alarm;
  return alarms_text(r);
}
std::string Gateway::slot_alarm_details(unsigned i) const {
  std::lock_guard<std::mutex> lk(mutex_);
  if(i>=MAX_SLOTS || !cfg_.slots[i].id)return "UNASSIGNED";
  const auto *f=find_unsafe_(cfg_.slots[i].id);
  if(!f)return "WAITING_FOR_TELEGRAM";
  if((uint32_t)(millis()-f->seen_ms)>STALE_MS)return "STALE";
  if(!f->valid)return "UNDECODABLE";
  IzarReading r; r.valid=true; r.alarms=f->flags; r.general_alarm=f->general_alarm;
  return alarms_text(r);
}
std::string Gateway::slot_id(unsigned i) const {
  std::lock_guard<std::mutex> lk(mutex_);
  return (i<MAX_SLOTS && cfg_.slots[i].id)?id_to_string_(cfg_.slots[i].id):"UNASSIGNED";
}

std::string Gateway::id_to_string_(uint32_t id) {
  char buf[9];std::snprintf(buf,sizeof(buf),"%08" PRIX32,id);return buf;
}
bool Gateway::parse_id_(const std::string &s,uint32_t *id) {
  if(s.size()!=8)return false;
  for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;
  *id=uint32_t(std::strtoul(s.c_str(),nullptr,16));return *id!=0;
}
std::string Gateway::escape_json_(const char *s) {
  std::string out;
  while(s && *s) {
    const unsigned char c=*s++;
    if(c=='"'||c=='\\') {out+='\\';out+=char(c);}
    else if(c<32)out+=' ';
    else out+=char(c);
  }
  return out;
}
std::string Gateway::state_json_() const {
  std::lock_guard<std::mutex> lk(mutex_);
  const uint32_t now=millis();
  std::string j="{\"rx_count\":"+std::to_string(rx_count_)+",\"decoded_count\":"+
    std::to_string(decoded_count_)+",\"found\":[";
  bool first=true;
  char b[160];
  for(const auto &f:found_) {
    if(!f.id)continue;
    if(!first) j+=",";
    first=false;
    const auto key=id_to_string_(f.id);
    std::snprintf(b,sizeof(b),"{\"id\":\"%s\",\"rssi\":%d,\"age_s\":%" PRIu32 ",\"valid\":%s,\"total_m3\":",
      key.c_str(),int(f.rssi),(uint32_t)(now-f.seen_ms)/1000u,f.valid?"true":"false");
    j+=b;
    if(f.valid){std::snprintf(b,sizeof(b),"%.3f",f.total_l/1000.0);j+=b;}else j+="null";
    j+=",\"flow_l_h\":";
    if(f.valid && f.flow.ready() && (uint32_t)(now-f.seen_ms)<=FLOW_STALE_MS) {
      std::snprintf(b,sizeof(b),"%.1f",double(f.flow.rate_lph()));j+=b;
    } else j+="null";
    j+=",\"leak_alarm\":";
    if(f.valid)j+=(f.flags&LEAKAGE)?"true":"false";else j+="null";
    j+=",\"printed\":\""+escape_json_(f.printed)+"\",\"target_m3\":";

    if(f.valid && f.has_target){std::snprintf(b,sizeof(b),"%.3f",f.target_l/1000.0);j+=b;}else j+="null";
    IzarReading r;r.valid=f.valid;r.alarms=f.flags;r.general_alarm=f.general_alarm;
    j+=",\"status\":\""+escape_json_(alarms_text(r).c_str())+"\",\"decoder\":\""+
      escape_json_(f.decoder)+"\",\"error\":\""+escape_json_(f.error)+"\"}";
  }
  j+="],\"slots\":[";
  for(unsigned i=0;i<MAX_SLOTS;i++) {
    if(i)j+=",";
    const auto &s=cfg_.slots[i];
    j+="{\"id\":\""+(s.id?id_to_string_(s.id):"")+"\",\"name\":\""+escape_json_(s.name)+
       "\",\"custom_key\":"+(s.key[0]?"true":"false")+",\"total_m3\":";
    const auto *f=find_unsafe_(s.id);
    if(f && f->valid && (uint32_t)(now-f->seen_ms)<STALE_MS) {
      std::snprintf(b,sizeof(b),"%.3f",f->total_l/1000.0);j+=b;
    }else j+="null";
    j+=",\"flow_l_h\":";
    if(f && f->valid && f->flow.ready() && (uint32_t)(now-f->seen_ms)<=FLOW_STALE_MS) {
      std::snprintf(b,sizeof(b),"%.1f",double(f->flow.rate_lph()));j+=b;
    } else j+="null";
    j+=",\"stale\":";
    if(!s.id) j+="null";
    else j+=(!f || !f->valid || (uint32_t)(now-f->seen_ms)>STALE_MS)?"true":"false";
    j+=",\"printed\":\""+escape_json_(f?f->printed:"")+"\"";
    j+=",\"alarm_details\":\"";
    if(!s.id) j+="UNASSIGNED";
    else if(!f) j+="WAITING_FOR_TELEGRAM";
    else if((uint32_t)(now-f->seen_ms)>STALE_MS) j+="STALE";
    else if(!f->valid) j+="UNDECODABLE";
    else {
      IzarReading r; r.valid=true; r.alarms=f->flags; r.general_alarm=f->general_alarm;
      j+=escape_json_(alarms_text(r).c_str());
    }
    j+="\"";
    j+=",\"leak_alarm\":";
    if(f && f->valid && (uint32_t)(now-f->seen_ms)<=STALE_MS)
      j+=(f->flags&LEAKAGE)?"true":"false";
    else j+="null";
    j+="}";
  }
  j+="]}";
  return j;
}

bool Gateway::canHandle(AsyncWebServerRequest *request) const {
  const auto method=request->method();
  if(method!=HTTP_GET && method!=HTTP_POST)return false;
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  const auto url=request->url_to(buffer);
  return url=="/selectro" || url=="/selectro/" || url=="/selectro/api/state" || url=="/selectro/api/slot" || url=="/selectro/api/reset_wifi" || url=="/selectro/api/factory_reset";
}
void Gateway::handleRequest(AsyncWebServerRequest *req) {
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  const std::string path=req->url_to(buffer).str();
  // Custom AsyncWebHandler routes do not inherit the standard web_server authentication.
  const bool destructive=(path=="/selectro/api/reset_wifi" || path=="/selectro/api/factory_reset");
  if(!admin_username_.empty() && !admin_password_.empty()) {
#ifdef USE_WEBSERVER_AUTH
    if(!req->authenticate(admin_username_.c_str(),admin_password_.c_str())) {
      req->requestAuthentication();return;
    }
#else
    req->send(503,"application/json","{\"ok\":false,\"error\":\"ESPHome web authentication unavailable\"}");
    return;
#endif
  } else if(destructive) {
    req->send(403,"application/json","{\"ok\":false,\"error\":\"Configure selectro_izar admin credentials to enable reset\"}");
    return;
  }

  if(req->method()==HTTP_GET && (path=="/selectro" || path=="/selectro/")) {
    auto *response=req->beginResponse(200,"text/html; charset=utf-8",std::string(SELECTRO_PORTAL_HTML));
    response->addHeader("Cache-Control","no-store");req->send(response);return;
  }
  if(req->method()==HTTP_GET && path=="/selectro/api/state") {
    auto *response=req->beginResponse(200,"application/json; charset=utf-8",state_json_());
    response->addHeader("Cache-Control","no-store");req->send(response);return;
  }
  if(req->method()==HTTP_POST && path=="/selectro/api/slot") {bind_(req);return;}
  if(req->method()==HTTP_POST && path=="/selectro/api/reset_wifi") {request_reset_(req,1);return;}
  if(req->method()==HTTP_POST && path=="/selectro/api/factory_reset") {request_reset_(req,2);return;}
  req->send(405,"application/json","{\"ok\":false,\"error\":\"Method not allowed\"}");
}
void Gateway::bind_(AsyncWebServerRequest *req) {
  const std::string raw=req->arg("slot");
  if(raw.empty() || raw.size()>2 || raw.find_first_not_of("0123456789")!=std::string::npos) {
    req->send(400,"application/json","{\"ok\":false,\"error\":\"Invalid slot\"}");return;
  }
  const unsigned slot=unsigned(std::strtoul(raw.c_str(),nullptr,10));
  if(slot>=MAX_SLOTS){req->send(400,"application/json","{\"ok\":false,\"error\":\"Slot out of range\"}");return;}
  const std::string action=req->arg("action");
  if(action=="remove") {
    {std::lock_guard<std::mutex> lk(mutex_);cfg_.slots[slot]={};save_unsafe_();}
    req->send(200,"application/json","{\"ok\":true}");return;
  }
  if(action!="bind"){req->send(400,"application/json","{\"ok\":false,\"error\":\"Unknown action\"}");return;}
  uint32_t id=0;
  if(!parse_id_(req->arg("id"),&id)){
    const std::string printed=req->arg("id");
    if(printed.size()<8 || printed.size()>16) {
      req->send(400,"application/json","{\"ok\":false,\"error\":\"Enter 8-digit radio ID or a detected printed serial\"}");return;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    for(const auto &f:found_) {
      if(f.id && printed==std::string(f.printed)) {id=f.id;break;}
    }
    if(!id) {req->send(404,"application/json","{\"ok\":false,\"error\":\"Printed number not detected yet\"}");return;}
  }
  std::string key=req->arg("key");
  if(!key.empty() && !valid_prios_key(key)){
    req->send(400,"application/json","{\"ok\":false,\"error\":\"PRIOS key must be exactly 16 hex chars; AES-128 unsupported\"}");return;
  }
  const std::string label=req->arg("name");
  if(label.size()>23){req->send(400,"application/json","{\"ok\":false,\"error\":\"Label too long\"}");return;}
  {
    std::lock_guard<std::mutex> lk(mutex_);
    for(unsigned i=0;i<MAX_SLOTS;i++)if(i!=slot && cfg_.slots[i].id==id) {
      req->send(409,"application/json","{\"ok\":false,\"error\":\"Meter already assigned to another slot\"}");return;
    }
    Slot &s=cfg_.slots[slot];
    if(s.id!=id) std::memset(s.key,0,sizeof(s.key)); // don't inherit another meter's key
    s.id=id;
    std::memset(s.name,0,sizeof(s.name));std::memcpy(s.name,label.data(),label.size());
    if(!key.empty()) {
      std::memset(s.key,0,sizeof(s.key));std::memcpy(s.key,key.data(),key.size());
    }
    if(req->arg("clear_key")=="1")std::memset(s.key,0,sizeof(s.key));
    save_unsafe_();
  }
  req->send(200,"application/json","{\"ok\":true}");
}
} }
