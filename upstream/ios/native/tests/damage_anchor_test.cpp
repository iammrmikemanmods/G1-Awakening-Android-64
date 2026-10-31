#include "g1/DamageAnchor.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool x){if(!x)throw std::runtime_error("Damage projection contract");}
int main(){try{g1::M3GRenderAsset asset;g1::M3GSurface surface{};
 g1::M3GVertex a{},b{};a.position={{-.2F,0,.5F,1}};b.position={{.2F,.4F,.5F,1}};surface.vertices={a,b};asset.geometry.surfaces.push_back(surface);
 g1::M3GPreparedFrame f;f.assets={&asset};g1::M3GPreparedDraw d{};d.metal_matrix={{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};f.draws={d};
 auto p=g1::projected_actor_top(f);check(p.has_value()&&std::abs((*p)[0]-.5F)<.001F&&std::abs((*p)[1]-.3F)<.001F);
 f.draws[0].metal_matrix[12]=.5F;p=g1::projected_actor_top(f);check(p&&(*p)[0]>.7F);
 f.draws[0].metal_matrix[15]=-1;check(!g1::projected_actor_top(f));
 f.draws.clear();check(!g1::projected_actor_top(f));
 std::cout<<"Real draw projection, camera translation and behind-camera rejection passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
