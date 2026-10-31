#include "CleanTextureStore.hpp"
#include <iostream>
#include <filesystem>
int main(){@autoreleasepool {try {
    g1::RgbaImage source{2,2,g1::Bytes(16,111)};auto copy=source;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(source.pixels.data(),(CC_LONG)source.pixels.size(),digest);
    const char* digits="0123456789abcdef";std::string hex;for(auto c:digest){hex+=digits[c>>4];hex+=digits[c&15];}
    const auto root=std::filesystem::path(NSBundle.mainBundle.resourcePath.UTF8String)/"CleanTextures017";
    std::filesystem::create_directories(root);
    const auto file=root/("material_2x2_"+hex+".png");
    if(std::filesystem::exists(file))throw std::runtime_error("Refusing to overwrite clean pack probe fixture");
    struct Remove{std::filesystem::path path;~Remove(){std::error_code e;std::filesystem::remove(path,e);}}remove{file};
    const auto write=[&](const g1::Bytes& bytes){NSData* d=[NSData dataWithBytes:bytes.data() length:bytes.size()];
        if(![d writeToFile:[NSString stringWithUTF8String:file.c_str()] atomically:YES])throw std::runtime_error("Fixture write failed");};
    unsigned checks=0;
    if(g1::clean_texture(source,"material",2))throw std::runtime_error("Missing pack returned texture");++checks;
    auto target=g1::RgbaImage{4,4,g1::Bytes(64,88)};write(g1::encode_png(target));
    auto clean=g1::clean_texture(source,"material",2);
    if(!clean||clean->width!=4||clean->pixels!=target.pixels||source.pixels!=copy.pixels)throw std::runtime_error("Native clean pack lookup failed");++checks;
    write(g1::encode_png({3,3,g1::Bytes(36,99)}));if(g1::clean_texture(source,"material",2))throw std::runtime_error("Wrong texture dimensions accepted");++checks;
    write(g1::Bytes(40,23));if(g1::clean_texture(source,"material",2))throw std::runtime_error("Corrupt pack texture accepted");++checks;
    std::cout<<"{\"result\":\"passed\",\"checks\":"<<checks<<",\"original_assets_used\":false,\"physical_device\":false}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}}
