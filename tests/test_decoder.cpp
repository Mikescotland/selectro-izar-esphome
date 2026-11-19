#include "../components/selectro_izar/izar_decoder.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace esphome::selectro_izar;
static std::vector<uint8_t> unhex(const char*s) {
 std::vector<uint8_t>v;for(size_t i=0;i<strlen(s);i+=2){unsigned b;sscanf(s+i,"%2x",&b);v.push_back(uint8_t(b));}return v;
}
void check(const char*hex, float total,float target,bool leak=false,bool prev=false) {
  auto a=decode_izar_prios(unhex(hex));
  if (!a.valid) {printf("FAIL %s\n",a.error.c_str());assert(a.valid);}
  assert(std::fabs(float(a.total_l)/1000.f-total)<0.002f);
  assert(a.has_target && std::fabs(float(a.target_l)/1000.f-target)<0.002f);
  assert(bool(a.alarms&LEAKAGE)==leak);
  assert(bool(a.alarms&PREV_LEAKAGE)==prev);
  if (a.manufacturer == 0x4c30) assert(!a.printed_number.empty());
  printf("PASS %08X %.3f / %.3f flags=%s variant=%s\n",a.id,a.total_l/1000.,a.target_l/1000.,alarms_text(a).c_str(),a.decoder.c_str());
}
int main(){
 check("1944304C9B769A215C0AA2011800137EEB0118F4C56683190660",3222,120);
 check("1944304C9B769A215C0AA2019800137E75BB62B99C681DD7DEA8",3222.126,120,true);
 check("1944304C72242421D401A2013D4013DD8B46A4999C1293E582CC",3.488,3.486);
 check("2944A511780729662366A20118001378D3B3DB8CEDD77731F25832AAF3DA8CADF9774EA673172E8C61F2",16.760,11.840);
 check("19442423860775035048A251520015BEB6B2E1ED623A18FC74A5",521.602,519.147,false,true);
 assert(valid_prios_key("39BC8A10E66D83F8"));
 assert(!valid_prios_key(""));assert(!valid_prios_key("00000000000000000000000000000000"));
 auto bad=unhex("1944304C9B769A215C0AA2011800137EEB0118F4C56683190660");bad[16]^=1;
 assert(!decode_izar_prios(bad).valid || decode_izar_prios(bad).total_l!=3222000);
 puts("ALL TESTS PASSED");
}
