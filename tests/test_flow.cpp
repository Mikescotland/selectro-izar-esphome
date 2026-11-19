#include "../components/selectro_izar/flow_estimator.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using esphome::selectro_izar::FlowEstimator;
static bool near(float a,float b){return std::fabs(a-b)<0.15f;}
int main(){
  FlowEstimator f;
  f.observe(100000,1000); assert(!f.ready());
  f.observe(100001,30000); assert(!f.ready());
  f.observe(100003,61000); assert(f.ready()&&near(f.rate_lph(),180.0f));
  f.observe(100003,90000); assert(near(f.rate_lph(),180.0f));
  f.observe(100003,121000); assert(f.ready()&&near(f.rate_lph(),0.0f));
  f.observe(42,181000); assert(!f.ready());
  f.observe(44,241000); assert(f.ready()&&near(f.rate_lph(),120.0f));
  f.observe(47,900000); assert(!f.ready());
  f.observe(48,960000); assert(f.ready()&&near(f.rate_lph(),60.0f));
  puts("FLOW ESTIMATOR TESTS PASSED");
}
