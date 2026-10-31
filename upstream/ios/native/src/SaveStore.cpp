#include "g1/SaveStore.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <limits>

namespace g1 {
namespace {
constexpr std::array<std::uint8_t,8> magic{{'G','1','N','P','R','O','F',0}};
constexpr std::size_t profile_v1_size=8+4+4+2+8+25*12+4;
constexpr std::size_t profile_v2_size=profile_v1_size+1;

void word(Bytes& out,std::uint32_t value) {
    for (unsigned i=0;i<4;++i) out.push_back(static_cast<std::uint8_t>(value>>(i*8)));
}
std::uint32_t read_word(const Bytes& input,std::size_t& cursor) {
    if (cursor>input.size() || input.size()-cursor<4) throw FormatError("native profile is truncated");
    std::uint32_t value=0;
    for (unsigned i=0;i<4;++i) value|=std::uint32_t(input[cursor++])<<(i*8);
    return value;
}
std::int32_t signed_word(std::uint32_t value) noexcept {
    return value<=0x7fffffffU ? static_cast<std::int32_t>(value) :
        static_cast<std::int32_t>(static_cast<std::int64_t>(value)-0x100000000LL);
}
void validate(const CampaignProfile& profile) {
    if (profile.records[0].size()!=17 || profile.records[1].size()!=8)
        throw FormatError("native campaign profile has invalid record counts");
    // The original may store the table length as its completed unlock value.
    for (std::size_t group=0;group<2;++group)
        if (profile.highest_unlocked[group]<0 ||
            profile.highest_unlocked[group]>static_cast<std::int32_t>(profile.records[group].size()))
            throw FormatError("native campaign profile unlock index is invalid");
}
std::string failure(const char* action,int code) {
    return std::string(action)+": "+std::strerror(code);
}
} // namespace

Bytes encode_campaign_profile(const CampaignProfile& profile) {
    validate(profile);
    Bytes result(magic.begin(),magic.end());
    word(result,2);word(result,static_cast<std::uint32_t>(profile_v2_size));
    for (auto value:profile.completed) result.push_back(value ? 1 : 0);
    for (auto value:profile.highest_unlocked) word(result,static_cast<std::uint32_t>(value));
    for (const auto& records:profile.records)
        for (const auto& record:records)
            for (auto value:{record.value_274,record.value_278,record.value_27c})
                word(result,static_cast<std::uint32_t>(value));
    result.push_back(profile.support_tutorial_pending ? 1 : 0);
    word(result,adler32_bytes(result));
    return result;
}

CampaignProfile decode_campaign_profile(const Bytes& input) {
    if ((input.size()!=profile_v1_size && input.size()!=profile_v2_size) ||
        !std::equal(magic.begin(),magic.end(),input.begin()))
        throw FormatError("native profile size or signature is invalid");
    std::size_t cursor=8;
    const auto version=read_word(input,cursor);
    const auto declared_size=read_word(input,cursor);
    if (!((version==1 && input.size()==profile_v1_size) ||
          (version==2 && input.size()==profile_v2_size)) || declared_size!=input.size())
        throw FormatError("native profile version or declared size is unsupported");
    std::size_t checksum_cursor=input.size()-4;
    const auto checksum=read_word(input,checksum_cursor);
    if (adler32_bytes(Bytes(input.begin(),input.end()-4))!=checksum)
        throw FormatError("native profile checksum failed");
    CampaignProfile result;
    for (auto& value:result.completed) {
        if (input[cursor]>1) throw FormatError("native profile boolean is invalid");
        value=input[cursor++]!=0;
    }
    for (auto& value:result.highest_unlocked) value=signed_word(read_word(input,cursor));
    for (auto& records:result.records)
        for (auto& record:records) {
            record.value_274=signed_word(read_word(input,cursor));
            record.value_278=signed_word(read_word(input,cursor));
            record.value_27c=signed_word(read_word(input,cursor));
        }
    // Build006 profiles have no tutorial field. Preserve every record and
    // unlock while giving them the original enabled-on-reset default.
    if (version==2) {
        if (input[cursor]>1) throw FormatError("native profile tutorial boolean is invalid");
        result.support_tutorial_pending=input[cursor++]!=0;
    }
    if (cursor!=input.size()-4) throw FormatError("native profile field size is invalid");
    validate(result);
    return result;
}

SaveWriteResult write_native_save_atomic(const std::string& destination,const Bytes& data,
                                        bool replace_existing) {
    if (destination.empty() || destination.find('\0')!=std::string::npos ||
        data.empty() || data.size()>Archive::max_resource_size)
        return {false,"native save path or payload is invalid"};
    const auto slash=destination.find_last_of('/');
    const auto parent=slash==std::string::npos ? std::string(".") :
        (slash==0 ? std::string("/") : destination.substr(0,slash));
    if (slash!=std::string::npos && slash+1==destination.size())
        return {false,"native save destination is a directory"};
    std::string pattern=destination+".writing-XXXXXX";
    std::vector<char> temporary(pattern.begin(),pattern.end());temporary.push_back(0);
    int descriptor=mkstemp(temporary.data());
    if (descriptor<0) return {false,failure("create temporary save",errno)};
    bool renamed=false;
    const auto fail=[&](const char* action,int error) {
        if (descriptor>=0) {close(descriptor);descriptor=-1;}
        if (!renamed) unlink(temporary.data());
        return SaveWriteResult{false,failure(action,error)};
    };
    std::size_t offset=0;
    while (offset<data.size()) {
        const auto count=write(descriptor,data.data()+offset,data.size()-offset);
        if (count<0 && errno==EINTR) continue;
        if (count<=0) return fail("write temporary save",count<0 ? errno : EIO);
        offset+=static_cast<std::size_t>(count);
    }
    if (fsync(descriptor)<0) return fail("flush temporary save",errno);
    if (close(descriptor)<0) {descriptor=-1;return fail("close temporary save",errno);}
    descriptor=-1;
    if (replace_existing) {
        if (rename(temporary.data(),destination.c_str())<0) return fail("replace native save",errno);
    } else {
        // link publishes a complete already-flushed sibling without replacing
        // a prior backup. The original name remains until publication succeeds.
        if (link(temporary.data(),destination.c_str())<0) return fail("preserve native save backup",errno);
        if (unlink(temporary.data())<0) return fail("retire backup temporary file",errno);
    }
    renamed=true;
    descriptor=open(parent.c_str(),O_RDONLY|O_DIRECTORY);
    if (descriptor<0) return fail("open save directory",errno);
    if (fsync(descriptor)<0) return fail("flush save directory",errno);
    if (close(descriptor)<0) {descriptor=-1;return fail("close save directory",errno);}
    descriptor=-1;
    return {true,{}};
}
} // namespace g1
