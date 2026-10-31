#include "g1/SaveStore.hpp"
#include "campaign_profile_v1_fixture.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <unistd.h> // Darwin declares mkdtemp here; Linux declares it in stdlib.

namespace {
std::size_t checks=0;
void check(bool value,const char* message) {
    ++checks;if(!value)throw std::runtime_error(message);
}
template<class Function> void rejects(Function f) {
    ++checks;try{f();}catch(const g1::FormatError&){return;}
    throw std::runtime_error("corrupt native profile accepted");
}
void put32(g1::Bytes& bytes,std::size_t at,std::uint32_t value) {
    for(std::size_t i=0;i<4;++i)bytes.at(at+i)=static_cast<std::uint8_t>(value>>(8U*i));
}
void rechecksum(g1::Bytes& bytes) {
    put32(bytes,bytes.size()-4,g1::adler32_bytes(g1::Bytes(bytes.begin(),bytes.end()-4)));
}
void check_legacy_progress(const g1::CampaignProfile& profile) {
    check(profile.completed[0] && !profile.completed[1] &&
          profile.highest_unlocked==std::array<std::int32_t,2>{{17,0}},
          "Build006 migration changed campaign completion/unlocks");
    check(profile.records[0].size()==17 && profile.records[1].size()==8,
          "Build006 migration changed campaign record counts");
    for(std::size_t i=0;i<g1_test::build006_story_records.size();++i) {
        const auto& actual=profile.records[0][i];const auto& expected=g1_test::build006_story_records[i];
        check(actual.value_274==expected[0] && actual.value_278==expected[1] && actual.value_27c==expected[2],
              "Build006 migration changed a recorded Story result");
    }
    for(const auto& actual:profile.records[1])
        check(actual.value_274==-1 && actual.value_278==-1 && actual.value_27c==-1,
              "Build006 migration invented a Showdown result");
}
}

int main() {
    std::filesystem::path directory;
    try {
        g1::CampaignProfile profile;
        check(profile.support_tutorial_pending,"fresh native profile suppresses the support tutorial");
        profile.completed[0]=true;profile.highest_unlocked={{17,6}};
        profile.records[0][16]={18,31,2};
        profile.records[1][5]={4,7,0};
        profile.records[1][7]={-1,std::numeric_limits<std::int32_t>::max(),
            std::numeric_limits<std::int32_t>::min()};
        const auto encoded=g1::encode_campaign_profile(profile);
        const auto decoded=g1::decode_campaign_profile(encoded);
        check(g1::encode_campaign_profile(decoded)==encoded,"profile format lost values");
        check(encoded.size()==331 && encoded[8]==2 && encoded[12]==0x4b && encoded[13]==1 &&
              encoded[326]==1 && decoded.support_tutorial_pending,
              "current native profile did not encode v2 and its pending tutorial bit");
        auto consumed=profile;consumed.support_tutorial_pending=false;
        const auto consumed_bytes=g1::encode_campaign_profile(consumed);
        check(consumed_bytes.size()==331 && consumed_bytes[326]==0 &&
              !g1::decode_campaign_profile(consumed_bytes).support_tutorial_pending &&
              std::equal(encoded.begin(),encoded.begin()+326,consumed_bytes.begin()),
              "consuming tutorial changed unrelated profile bytes or failed to persist");

        const auto legacy=g1_test::build006_story_profile_v1();
        check(legacy.size()==330 && g1::adler32_bytes(g1::Bytes(legacy.begin(),legacy.end()-4))==0x0e6b6360U,
              "frozen Build006 v1 fixture has changed");
        const auto migrated=g1::decode_campaign_profile(legacy);
        check_legacy_progress(migrated);
        check(migrated.support_tutorial_pending,"v1 migration did not default the tutorial to pending");
        const auto migrated_bytes=g1::encode_campaign_profile(migrated);
        check(migrated_bytes.size()==331 && migrated_bytes[8]==2 && migrated_bytes[326]==1 &&
              std::equal(legacy.begin()+16,legacy.end()-4,migrated_bytes.begin()+16),
              "v1 migration moved or changed the existing gameplay payload");
        check_legacy_progress(g1::decode_campaign_profile(migrated_bytes));
        auto corrupt=encoded;corrupt[30]^=1;
        rejects([&]{g1::decode_campaign_profile(corrupt);});
        corrupt=encoded;corrupt.pop_back();
        rejects([&]{g1::decode_campaign_profile(corrupt);});
        corrupt=encoded;corrupt[8]=3;rechecksum(corrupt);
        rejects([&]{g1::decode_campaign_profile(corrupt);});
        for(std::uint8_t value:{std::uint8_t(2),std::uint8_t(255)}) {
            corrupt=encoded;corrupt[326]=value;rechecksum(corrupt);
            rejects([&]{g1::decode_campaign_profile(corrupt);});
        }
        corrupt=encoded;corrupt[16]=2;rechecksum(corrupt);
        rejects([&]{g1::decode_campaign_profile(corrupt);});
        corrupt=encoded;put32(corrupt,12,330);rechecksum(corrupt);
        rejects([&]{g1::decode_campaign_profile(corrupt);});
        corrupt=encoded;put32(corrupt,8,1);rechecksum(corrupt);
        rejects([&]{g1::decode_campaign_profile(corrupt);});
        corrupt=legacy;put32(corrupt,8,2);rechecksum(corrupt);
        rejects([&]{g1::decode_campaign_profile(corrupt);});
        corrupt=legacy;put32(corrupt,8,0);rechecksum(corrupt);
        rejects([&]{g1::decode_campaign_profile(corrupt);});
        corrupt=legacy;put32(corrupt,12,331);rechecksum(corrupt);
        rejects([&]{g1::decode_campaign_profile(corrupt);});
        corrupt=encoded;corrupt.push_back(0);
        rejects([&]{g1::decode_campaign_profile(corrupt);});
        corrupt=legacy;corrupt.push_back(0);
        rejects([&]{g1::decode_campaign_profile(corrupt);});
        for(std::size_t length=0;length<encoded.size();++length) {
            corrupt=g1::Bytes(encoded.begin(),encoded.begin()+static_cast<std::ptrdiff_t>(length));
            rejects([&]{g1::decode_campaign_profile(corrupt);});
        }
        for(std::size_t length=0;length<legacy.size();++length) {
            corrupt=g1::Bytes(legacy.begin(),legacy.begin()+static_cast<std::ptrdiff_t>(length));
            rejects([&]{g1::decode_campaign_profile(corrupt);});
        }
        auto invalid=profile;invalid.records[0].pop_back();
        rejects([&]{g1::encode_campaign_profile(invalid);});
        invalid=profile;invalid.highest_unlocked[1]=9;
        rejects([&]{g1::encode_campaign_profile(invalid);});

        auto pattern=(std::filesystem::current_path()/"g1-save-test-XXXXXX").string();
        std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
        const char* created=mkdtemp(name.data());
        if(!created)throw std::runtime_error("could not create isolated save test directory");
        directory=created;
        const auto destination=(directory/"campaign.g1native").string();
        const auto legacy_destination=(directory/"build006-profile.g1native").string();
        check(g1::write_native_save_atomic(legacy_destination,legacy).succeeded,
              "could not persist frozen v1 profile for closed-file migration");
        auto disk_migrated=g1::decode_campaign_profile(g1::read_file(legacy_destination,4096));
        check_legacy_progress(disk_migrated);
        check(disk_migrated.support_tutorial_pending && g1::read_file(legacy_destination,4096)==legacy,
              "v1 load rewrote source bytes or lost pending default");
        disk_migrated.support_tutorial_pending=false;
        const auto disk_upgrade=g1::encode_campaign_profile(disk_migrated);
        check(g1::write_native_save_atomic(legacy_destination,disk_upgrade).succeeded,
              "v1 to v2 profile replacement failed");
        const auto disk_reopened=g1::decode_campaign_profile(g1::read_file(legacy_destination,4096));
        check_legacy_progress(disk_reopened);
        check(!disk_reopened.support_tutorial_pending && g1::read_file(legacy_destination,4096)==disk_upgrade,
              "closed v2 replacement lost consumed tutorial state");
        auto result=g1::write_native_save_atomic(destination,encoded);
        check(result.succeeded && result.error.empty(),"first flushed atomic save failed");
        const auto reloaded=g1::decode_campaign_profile(g1::read_file(destination,4096));
        check(g1::encode_campaign_profile(reloaded)==encoded,"closed file reload lost profile state");
        profile.highest_unlocked[1]=7;profile.records[1][6]={9,4,1};
        const auto updated=g1::encode_campaign_profile(profile);
        result=g1::write_native_save_atomic(destination,updated);
        check(result.succeeded,"replacement flushed atomic save failed");
        check(g1::read_file(destination,4096)==updated,"replacement file retained old save data");
        const auto exclusive=(directory/"exclusive-v1-backup").string();
        result=g1::write_native_save_atomic(exclusive,legacy,false);
        check(result.succeeded && g1::read_file(exclusive,4096)==legacy,
              "exclusive backup publication did not create a complete closed file");
        result=g1::write_native_save_atomic(exclusive,updated,false);
        check(!result.succeeded && !result.error.empty() && g1::read_file(exclusive,4096)==legacy,
              "exclusive backup publication overwrote existing rollback bytes");
        const auto symlink=directory/"symlink-v1-backup";
        std::filesystem::create_symlink(exclusive,symlink);
        result=g1::write_native_save_atomic(symlink.string(),updated,false);
        check(!result.succeeded && !result.error.empty() && std::filesystem::is_symlink(symlink) &&
              g1::read_file(exclusive,4096)==legacy,
              "exclusive backup publication replaced a symlink or changed its target");
        result=g1::write_native_save_atomic((directory/"missing"/"campaign.g1native").string(),encoded);
        check(!result.succeeded && !result.error.empty(),"unwritable save was reported successful");
        check(g1::read_file(destination,4096)==updated,"failed write damaged previous save");
        const auto blocked=directory/"directory-target";std::filesystem::create_directory(blocked);
        result=g1::write_native_save_atomic(blocked.string(),encoded);
        check(!result.succeeded && std::filesystem::is_directory(blocked),"failed rename changed target");
        std::size_t leftovers=0;
        for(const auto& entry:std::filesystem::directory_iterator(directory))
            if(entry.path().filename().string().find(".writing-")!=std::string::npos)++leftovers;
        check(leftovers==0,"failed save left a temporary file");
        std::filesystem::remove_all(directory);directory.clear();
        std::cout<<checks<<" native profile/atomic file checks passed; iPhone cold reload remains untested\n";
    } catch(const std::exception& error) {
        if(!directory.empty())std::filesystem::remove_all(directory);
        std::cerr<<error.what()<<'\n';return 1;
    }
}
