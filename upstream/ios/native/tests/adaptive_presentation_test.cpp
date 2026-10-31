#include "g1/AdaptivePresentation.hpp"
#include "g1/PresentationQuality.hpp"
#include <iostream>
#include <vector>
#include <limits>
#include <stdexcept>
static void check(bool x,const char* m){if(!x)throw std::runtime_error(m);}
int main(){try{
 using namespace g1::adaptive;
 struct Case {double w,h; Insets s;};
 const std::vector<Case> cases={{568,320,{}},{667,375,{}},{736,414,{}},
 {896,414,{0,44,21,44}},{812,375,{0,44,21,44}},{852,393,{0,59,21,59}},
 {932,430,{0,59,21,59}},{1024,768,{24,0,20,0}},{1194,834,{24,0,20,0}},
 {1366,1024,{24,0,20,0}},{640,480,{}},{414,896,{44,0,34,0}},{320,480,{24,0,20,0}}};
 for(const auto& c:cases){auto l=layout(c.w,c.h,c.s);check(l.scale>0,"scale");
  check(contains(l.safe,l.source),"central original safe");
  for(int y=50;y<200;y+=49)for(auto a:{Anchor::start,Anchor::end}){
   auto b=l.place({a==Anchor::start?0.0:428.0,double(y),52,49},a);
   check(contains(l.safe,b),"action cut off");check(std::abs(b.width/b.height-52.0/49)<1e-9,"stretched icon");}
  check(contains(l.safe,l.bottom(62)),"bottom hud cut off");
  auto left=l.place({0,0,60,49},Anchor::start,Anchor::start);
  check(contains(l.safe,left),"notch overlaps top button");
 }
 auto xr=layout(896,414,{0,44,21,44});
 check(xr.bottom(62).width==808,"XR uses whole safe width");
 auto z=layout(0,0);check(z.scale==0,"zero launch bounds");
 bool rejected=false;try{layout(std::numeric_limits<double>::quiet_NaN(),414);}catch(...){rejected=true;}
 check(rejected,"invalid screen rejected");
 auto modern=g1::quality::plan(1792,828,g1::quality::Mode::modern);
 check(modern.supersampling&&modern.width==2688&&modern.height==1242,"XR supersampling");
 auto classic=g1::quality::plan(1792,828,g1::quality::Mode::classic);
 check(!classic.supersampling&&classic.enhanced_ui,"Classic UI enhancement shared");
 g1::quality::Capability hot;hot.thermal_serious=true;
 check(!g1::quality::plan(1792,828,g1::quality::Mode::modern,hot).supersampling,"thermal fallback");
 auto tablet=g1::quality::plan(2732,2048,g1::quality::Mode::modern);
 check(tablet.estimated_surface_bytes<=96ULL*1024*1024,"surface budget");
 std::cout<<"13 viewport cases; safe areas, isotropic icons, shared UI, SSAA and thermal budgets passed\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
