#include "g1/TacticalMotion.hpp"
#include <cassert>
#include <limits>
using namespace g1;
template<class F>bool rejected(F f){try{f();return false;}catch(const std::exception&){return true;}}
int main(){
 for(int axis=0;axis<3;++axis)for(float angle:{-1.0F,0.0F,.3F,1.0F}) {
  const std::array<float,3> pivot{{2,-3,5}};const auto m=rigid_hinge(pivot,angle,axis);
  for(int i=0;i<3;++i){float actual=m[12+i];for(int j=0;j<3;++j)actual+=m[j*4+i]*pivot[j];assert(std::abs(actual-pivot[i])<1e-5);
   for(int j=0;j<3;++j){float dot=0;for(int r=0;r<3;++r)dot+=m[i*4+r]*m[j*4+r];assert(std::abs(dot-(i==j?1:0))<1e-6);}}
  const float det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[4]*(m[1]*m[10]-m[2]*m[9])+m[8]*(m[1]*m[6]-m[2]*m[5]);assert(std::abs(det-1)<1e-6);
 }
 const std::vector<AtlasStep> steps{{0,80},{1,80},{2,80},{3,80}};
 SpriteAnimation f(steps,1,false),r(steps,1,true);float forward=0,reverse=1;
 for(int tick=0;tick<40;++tick){
  const auto a=transformation_fraction(steps,f.state()),b=transformation_fraction(steps,r.state());
  assert(a>=forward && b<=reverse);assert(a>=0&&a<=1&&b>=0&&b<=1);forward=a;reverse=b;
  if(!f.state().completed)f.advance_ms(10);if(!r.state().completed)r.advance_ms(10);
 }
 assert(forward==1&&reverse==0);
 auto saved=f.state();(void)transformation_fraction(steps,saved);assert(saved.step_index==f.state().step_index&&saved.elapsed_ms==f.state().elapsed_ms);
 for(int step=0;step<=100;++step){const auto p=mechanical_phase(step/100.0F);assert(p.highlight>=0&&p.highlight<=.35F);
  unsigned car=0;for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){bool a=mechanical_coverage(false,p.exchange,x,y),b=mechanical_coverage(true,p.exchange,x,y);assert(a!=b);car+=b;}
  if(step==0){assert(car==0&&p.robot_fold==0);}if(step==100){assert(car==16&&p.vehicle_release==0);}
 }
 assert(rejected([]{mechanical_phase(-.1F);}));assert(rejected([]{mechanical_phase(std::numeric_limits<float>::quiet_NaN());}));
 assert(rejected([]{rigid_hinge({{0,0,0}},0,3);}));assert(rejected([]{tactical_stride(0,0,4);}));
 assert(rejected([&]{auto s=saved;s.step_index=99;transformation_fraction(steps,s);}));
 for(int facing=0;facing<4;++facing){assert(std::abs(std::sin(tactical_stride(.16,.16,facing)*6.283185307F))<1e-5);}
}
