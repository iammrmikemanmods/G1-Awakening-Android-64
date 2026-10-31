#include "g1/BitmapFont.hpp"
#include <iostream>
#include <stdexcept>

// Owner-data verification only. Does not export font files or modify the archive.
int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"Usage: g1_font_bank_probe <owner-supplied iphone.big>\n";return 2;}
    try {
        const g1::Archive archive(g1::read_file(argv[1]));
        std::cout<<"{\"banks\":[";
        for(unsigned slot=0;slot<12;++slot){
            const auto f=g1::load_bitmap_font_assets(archive,slot);
            if(f.descriptor.glyphs().empty())throw std::runtime_error("Source font bank has no glyphs");
            if(slot)std::cout<<',';
            std::cout<<"{\"slot\":"<<slot<<",\"image\":"<<f.image_resource_id
                <<",\"glyphs\":"<<f.descriptor.glyphs().size()<<",\"width\":"<<f.image.width
                <<",\"height\":"<<f.image.height<<'}';
        }
        std::cout<<"],\"original_data\":true,\"passed\":true}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
