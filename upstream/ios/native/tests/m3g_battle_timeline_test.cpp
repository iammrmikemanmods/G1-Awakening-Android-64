#include "g1/M3GBattleTimeline.hpp"
#include "m3g_battle_timeline_fixture.hpp"

#include <cstring>
#include <iostream>
#include <limits>

namespace {
std::size_t comparisons=0,contracts=0;
void compare(bool value,const char* name) { ++comparisons;if(!value)throw std::runtime_error(name); }
void contract(bool value,const char* name) { ++contracts;if(!value)throw std::runtime_error(name); }
std::uint32_t bits(float value) { std::uint32_t result;std::memcpy(&result,&value,4);return result; }
}

int main() {
    try {
        for(const auto& row:g1_timeline_fixture::ranges) {
            const auto range=g1::m3g_battle_clip_range(row.clip);
            compare(range.start_frame==row.first,"start frame");
            compare(range.end_frame==row.last,"end frame");
            compare(range.start_ms==row.start,"start milliseconds");
            compare(range.end_call_ms==row.end,"end call milliseconds");
            compare(bits(range.end_threshold_ms)==row.end_bits,"float32 end threshold");
        }
        for(const auto& row:g1_timeline_fixture::cases) {
            const auto step=row.start ? g1::start_m3g_battle_clip(row.clip) :
                g1::advance_m3g_battle_clip(row.clip,row.before,row.delta,row.restart);
            compare(step.stored_clock_ms==row.after,row.name);
            compare(step.animation_time_ms==row.requested,row.name);
            if(!row.start)compare(step.reached_end==row.completed,row.name);
            contract(step.restarted==(!row.start && row.completed && row.restart),"restart classification");
        }
        for(const auto index:{std::uint8_t{6},std::uint8_t{255}}) {
            bool failed=false;
            try { (void)g1::start_m3g_battle_clip(index); }catch(const g1::FormatError&) { failed=true; }
            contract(failed,"invalid clip accepted");
        }
        // The instruction-level ADD is defined modulo 2^32; these two tests
        // check the native language boundary separately from the 54-case oracle.
        const auto wrapped=g1::advance_m3g_battle_clip(0,std::numeric_limits<std::int32_t>::max(),1,false);
        contract(wrapped.stored_clock_ms==std::numeric_limits<std::int32_t>::min() && !wrapped.reached_end,"32-bit ADD overflow changed");
        const auto reverse=g1::advance_m3g_battle_clip(0,0,-1,false);
        contract(reverse.animation_time_ms==0 && reverse.stored_clock_ms==-1,"signed delta ordering changed");
        std::cout<<"{\"original_cases\":"<<g1_timeline_fixture::cases.size()
                 <<",\"reference_comparisons\":"<<comparisons<<",\"native_contracts\":"<<contracts
                 <<",\"reference_sha256\":\""<<g1_timeline_fixture::source_sha256<<"\",\"passed\":true}\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
