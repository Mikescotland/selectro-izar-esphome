#include "izar_decoder.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace esphome { namespace selectro_izar {
namespace {
// The check byte 0x4B and XOR'ed 8-byte keys match the legacy driver.
static const char *DEFAULT_KEYS[] = {"39BC8A10E66D83F8", "51728910E66D83F8"};
static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c-'0';
  if (c >= 'a' && c <= 'f') return c-'a'+10;
  if (c >= 'A' && c <= 'F') return c-'A'+10;
  return -1;
}
static bool seed_from_key(const char *h, uint32_t *seed) {
  if (!h || std::strlen(h)!=16) return false;
  uint8_t b[8]{};
  for (size_t i=0;i<8;i++) {
    const int hi=hex_nibble(h[i*2]), lo=hex_nibble(h[i*2+1]);
    if(hi<0 || lo<0) return false;
    b[i]=uint8_t(hi*16+lo);
  }
  const auto be32=[](const uint8_t *p)->uint32_t{
    return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|uint32_t(p[3]);
  };
  *seed=be32(b)^be32(b+4);
  return true;
}
static uint32_t be32(const uint8_t *p) {
  return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|uint32_t(p[3]);
}
static uint32_t le32(const uint8_t *p) {
  return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
static bool lfsr_decode(const std::vector<uint8_t> &a, const char *h, std::vector<uint8_t> *dst) {
  uint32_t key{};
  if(!seed_from_key(h,&key) || a.size()<20) return false;
  key ^= be32(a.data()+2);
  key ^= be32(a.data()+6);
  key ^= be32(a.data()+10);
  dst->clear(); dst->reserve(a.size()-15);
  for(size_t i=15; i<a.size();i++) {
    for(unsigned j=0;j<8;j++) {
      const uint32_t bit=((key>>1)^(key>>2)^(key>>11)^(key>>31))&1u;
      key=(key<<1)|bit;
    }
    uint8_t x=uint8_t(a[i]^(key&0xff));
    if (i==15 && x!=0x4B) return false;
    dst->push_back(x);
  }
  return dst->size()>=5;
}
}

bool valid_prios_key(const std::string &s) {
  uint32_t seed{}; return seed_from_key(s.c_str(),&seed);
}

IzarReading decode_izar_prios(const std::vector<uint8_t>& a,const char *user_key) {
  IzarReading r;
  if (a.size()<20 || a.size()>255 || a[0]!=a.size()-1 || (a[1]!=0x44 && a[1]!=0x46)) {
    r.error="Invalid wM-Bus frame header/length";return r;
  }
  r.manufacturer=uint16_t(a[2])|(uint16_t(a[3])<<8);
  r.version=a[8];r.device_type=a[9];r.ci=a[10];
  if(r.ci<0xA0 || r.ci>0xA7) {r.error="Unsupported CI (non PRIOS)";return r;}
  // WMBus radio component has already verified CRC before this call.
  // SAP PRIOS keeps the 4-byte radio id at original A-field offsets 4..7.
  // Other manufacturers can use a swapped A field (version, type, ID...).
  if(r.manufacturer==0x4c30) r.id=le32(a.data()+4);
  else r.id=le32(a.data()+6);
  std::vector<uint8_t> dec;
  bool decoded=false;
  if(user_key && user_key[0]) {
    decoded=lfsr_decode(a,user_key,&dec);
    if (!decoded) {r.error="Provided PRIOS key did not decode";return r;}
  } else {
    for(auto *k:DEFAULT_KEYS) if(lfsr_decode(a,k,&dec)) {decoded=true;break;}
  }
  if(!decoded) {r.error="PRIOS LFSR check failed / unknown key or variant";return r;}
  r.total_l=le32(&dec[1]);
  if(dec.size()>=9) {r.target_l=le32(&dec[5]);r.has_target=true;}
  if(dec.size()>=11) {
    unsigned yy=((dec[10]&0xf0)>>1)+((dec[9]&0xe0)>>5);
    r.year=(yy>80?1900:2000)+int(yy);
    r.month=dec[10]&0x0f;r.day=dec[9]&0x1f;
    if (r.month<1 || r.month>12 || r.day<1 || r.day>31) {
      r.year=0; r.month=0; r.day=0; // don't invent invalid billing date
    }
  }
  if(r.manufacturer==0x4C30) {
    // Reverse-engineered SAP PRIOS address encoding as used by legacy izar.
    const uint32_t packed=(uint32_t(a[7]&0x03)<<24)|(uint32_t(a[6])<<16)|
                          (uint32_t(a[5])<<8)|a[4];
    char digits[10]; std::snprintf(digits,sizeof(digits),"%08u",unsigned(packed));
    const int yy=(digits[0]-'0')*10+(digits[1]-'0');
    const char supplier=char('@'+((a[9]&0x0f)<<1)+(a[8]>>7));
    const char type=char('@'+((a[8]&0x7c)>>2));
    const char diameter=char('@'+((a[8]&0x03)<<3)+(a[7]>>5));
    if(yy<=99 && supplier>='A' && supplier<='Z' && type>='A' && type<='Z' &&
       diameter>='A' && diameter<='Z') {
      char printed[18]; std::snprintf(printed,sizeof(printed),"%c%02d%c%c%s",
                                      supplier,yy,type,diameter,digits+2);
      r.printed_number=printed;
    }
  }
  r.battery_y=float(a[12]&0x1f)/2.0f;
  r.transmit_s=uint32_t(1)<<(uint32_t(a[11]&0x0f)+2u);
  r.general_alarm=(a[11]&0x80)!=0;
  if(a[12]&0x80)r.alarms|=LEAKAGE;
  if(a[12]&0x40)r.alarms|=PREV_LEAKAGE;
  if(a[12]&0x20)r.alarms|=METER_BLOCKED;
  if(a[13]&0x80)r.alarms|=BACK_FLOW;
  if(a[13]&0x40)r.alarms|=UNDERFLOW;
  if(a[13]&0x20)r.alarms|=OVERFLOW;
  if(a[13]&0x10)r.alarms|=SUBMARINE;
  if(a[13]&0x08)r.alarms|=SENSOR_FRAUD;
  if(a[13]&0x04)r.alarms|=PREV_SENSOR_FRAUD;
  if(a[13]&0x02)r.alarms|=MECHANICAL_FRAUD;
  if(a[13]&0x01)r.alarms|=PREV_MECHANICAL_FRAUD;
  // Version distinctions in izarv2.xmq largely concern address transformation,
  // extraction and alarm schema; its XMQ runtime is NOT included here.
  r.decoder=(r.manufacturer==0x4c30)
      ? "legacy-prios" : "extended-prios";
  r.valid=true;return r;
}

std::string alarms_text(const IzarReading &r) {
  if(!r.valid)return "decode_error";
  const struct {uint16_t mask; const char *name;} f[] = {
    {LEAKAGE,"LEAKAGE"}, {PREV_LEAKAGE,"PREV_LEAKAGE"},
    {METER_BLOCKED,"METER_BLOCKED"}, {BACK_FLOW,"BACK_FLOW"},
    {UNDERFLOW,"UNDERFLOW"}, {OVERFLOW,"OVERFLOW"},
    {SUBMARINE,"SUBMARINE"}, {SENSOR_FRAUD,"SENSOR_FRAUD"},
    {PREV_SENSOR_FRAUD,"PREV_SENSOR_FRAUD"},
    {MECHANICAL_FRAUD,"MECHANICAL_FRAUD"},
    {PREV_MECHANICAL_FRAUD,"PREV_MECHANICAL_FRAUD"}
  };
  std::string result;
  if(r.general_alarm)result="GENERAL_ALARM";
  for(auto &v:f)if(r.alarms&v.mask){if(!result.empty())result+=' ';result+=v.name;}
  if(result.empty())result="OK";
  return result;
}
} }
