#include "selectro_izar.h"
#include "portal.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <inttypes.h>

namespace esphome { namespace selectro_izar {
static const char *TAG = "selectro_izar";
static constexpr uint32_t CONFIG_MAGIC=0x53495A47u; // 'SIZG'
static constexpr uint32_t CONFIG_VERSION=1u;
static constexpr uint32_t STALE_MS=30u*60u*1000u;

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
    *slot=Found{};
    slot->id=id;slot->rssi=rssi;slot->seen_ms=now;
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
  return url=="/selectro" || url=="/selectro/" || url=="/selectro/api/state" || url=="/selectro/api/slot";
}
void Gateway::handleRequest(AsyncWebServerRequest *req) {
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  const std::string path=req->url_to(buffer).str();
  if(req->method()==HTTP_GET && (path=="/selectro" || path=="/selectro/")) {
    auto *response=req->beginResponse(200,"text/html; charset=utf-8",std::string(SELECTRO_PORTAL_HTML));
    response->addHeader("Cache-Control","no-store");req->send(response);return;
  }
  if(req->method()==HTTP_GET && path=="/selectro/api/state") {
    auto *response=req->beginResponse(200,"application/json; charset=utf-8",state_json_());
    response->addHeader("Cache-Control","no-store");req->send(response);return;
  }
  if(req->method()==HTTP_POST && path=="/selectro/api/slot") {bind_(req);return;}
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
