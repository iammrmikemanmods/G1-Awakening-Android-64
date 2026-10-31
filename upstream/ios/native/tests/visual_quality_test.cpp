#include "g1/TileUpscale.hpp"
#include "g1/VisualBorder.hpp"
#include "g1/ContactShadow.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool x,const char*m){if(!x)throw std::runtime_error(m);}
int main(){try{
 g1::RgbaImage atlas;atlas.width=4;atlas.height=2;atlas.pixels.resize(4*2*4);
 for(unsigned y=0;y<2;++y)for(unsigned x=0;x<4;++x){auto k=(y*4+x)*4;atlas.pixels[k]=x<2?255:0;atlas.pixels[k+1]=0;atlas.pixels[k+2]=x<2?0:255;atlas.pixels[k+3]=x<2?255:0;}
 const auto out=g1::scale4x_tiles(atlas,2,2);check(out.width==16&&out.height==8,"scale");
 for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x){auto k=(y*16+x)*4;check(out.pixels[k]==(x<8?255:0)&&out.pixels[k+2]==(x<8?0:255)&&out.pixels[k+3]==(x<8?255:0),"atlas cross-cell bleed/alpha corruption");}
 g1::TerrainGrid grid;grid.width=grid.height=2;grid.tiles={1,1,2,2};const auto frozen=grid.tiles;
 g1::visual_border::Plan p;p.original_width=p.original_height=2;p.style=0;p.original_grid_resource=268;p.decorative={{-1,0,1},{2,1,2}};
 g1::visual_border::validate(p,grid,0,268);check(grid.tiles==frozen,"map mutated");
 check(!p.approved,"not silently approved");
 auto illegal=p;illegal.decorative.push_back({0,0,1});bool rejected=false;try{g1::visual_border::validate(illegal,grid,0,268);}catch(...){rejected=true;}check(rejected,"playable overlap accepted");
 illegal=p;illegal.decorative.push_back({-2,1,3});rejected=false;try{g1::visual_border::validate(illegal,grid,0,268);}catch(...){rejected=true;}check(rejected,"foreign theme tile accepted");
 auto beams=g1::visual_border::boundary(15,14);check(beams.size()==4,"original border not closed");
 auto shadow=g1::soft_contact_shadow();check(shadow.pixels[(32*64+32)*4+3]>shadow.pixels[3],"contact shadow gradient");
 std::cout<<"Tile-local reconstruction, transparent edges, immutable map footprint, review gate and soft shadow gradient passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
