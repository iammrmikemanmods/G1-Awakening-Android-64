#include "g1/TacticalModel.hpp"
#include "g1/MaterialEnhancement.hpp"
#include <cassert>
#include <limits>
int main(){
 using namespace g1;
 const TacticalBounds b{{{-2,-1,-4}},{{2,1,6}}};
 for(std::uint8_t facing=0;facing<4;++facing){
  const auto m=tactical_placement(b,facing,3,0.32F,4);
  std::array<float,3> lengths{};
  for(int c=0;c<3;++c)for(int r=0;r<3;++r)lengths[c]+=m[c*4+r]*m[c*4+r];
  assert(std::abs(lengths[0]-lengths[1])<1e-7 && std::abs(lengths[1]-lengths[2])<1e-7);
  // Source floor -4 Z lands at requested 0.32 Y exactly; X/Y pivot centers.
  assert(std::abs(m[9]*-4+m[13]-0.32F)<1e-6);
  const float determinant=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
  assert(determinant>0); // No handedness flip or mirrored faces.
 }
 assert(tactical_model_eligible(true,true,false,false,false));
 assert(!tactical_model_eligible(false,true,false,false,false));
 assert(!tactical_model_eligible(true,true,true,false,false));
 assert(!tactical_model_eligible(true,true,false,true,false));
 assert(!tactical_model_eligible(true,true,false,false,true));
 bool rejected=false;try{tactical_placement(b,4,0,0,0);}catch(const FormatError&){rejected=true;}assert(rejected);
 rejected=false;try{tactical_placement(b,1,std::numeric_limits<float>::quiet_NaN(),0,0);}catch(const FormatError&){rejected=true;}assert(rejected);
 RgbaImage source{2,1,{255,0,0,0,0,100,200,255}};const auto copy=source;
 const auto image=enhance_material(source,MaterialEnhancement::effect);
 assert(image.width==4 && image.height==2 && source.pixels==copy.pixels);
 // Invisible red source colour must never bleed into visible blue material.
 for(std::size_t i=0;i<image.pixels.size();i+=4){assert(image.pixels[i]==0);if(image.pixels[i+3]){assert(image.pixels[i+1]==100);assert(image.pixels[i+2]==200);}}
 assert(enhance_material(source,MaterialEnhancement::original).pixels==source.pixels);
 RgbaImage flat{2,2,Bytes(16,128)};for(auto kind:{MaterialEnhancement::character,MaterialEnhancement::environment,MaterialEnhancement::effect}){
  const auto result=enhance_material(flat,kind);for(auto v:result.pixels)assert(v==128);
 }
}
