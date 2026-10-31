#include "g1/SupportPresentation.hpp"
#include "support_presentation_fixture.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace {

std::size_t checks{};

void require(bool condition,const char* message) {
    ++checks;
    if(!condition)throw std::runtime_error(message);
}

std::vector<g1::AtlasStep> effect_steps() {
    std::vector<g1::AtlasStep> steps;
    for(const auto& pair:g1_support_fixture::effect_steps)
        steps.push_back({static_cast<std::uint8_t>(pair[0]),pair[1]});
    return steps;
}

void compare_effect(const g1::SupportPresentation& presentation,std::size_t tick) {
    const auto state=presentation.animation_state();
    require(state.has_value(),"nonempty support cue has no independent 2D player");
    const auto& expected=g1_support_fixture::effect_states.at(tick);
    const auto status=presentation.status();
    require(state->step_index==static_cast<std::size_t>(expected[0]),"original support effect step differs");
    require(status.effect_frame==expected[1],"original support effect frame differs");
    require(state->elapsed_ms==expected[2],"original support effect elapsed differs");
    require(state->repeat_count==expected[3],"original support effect repeat differs");
    require(state->reverse==bool(expected[4]),"original support effect direction differs");
    require(state->completed==bool(expected[5]),"original support effect completion differs");
    require(status.effect_complete==state->completed,"support status lost effect completion");
}

} // namespace

int main() {
    try {
        std::size_t comparisons=0,sequences=0,controller_sequences=0;
        for(const auto& row:g1_support_fixture::rows) {
            g1::SupportPresentationState state{row.count,row.index,row.elapsed,false};
            const auto action=g1::advance_support_presentation(state,row.delta);
            comparisons+=4;
            if(state.slot!=row.next_index || state.elapsed_ms!=row.next_elapsed ||
               static_cast<std::uint8_t>(action)!=row.action || state.complete!=(row.action==2)) {
                std::ostringstream message;message<<"original support state differs count="<<unsigned(row.count)
                    <<" slot="<<unsigned(row.index)<<" elapsed="<<row.elapsed<<" delta="<<row.delta
                    <<" expected=["<<unsigned(row.next_index)<<','<<row.next_elapsed<<','<<unsigned(row.action)
                    <<"] actual=["<<unsigned(state.slot)<<','<<state.elapsed_ms<<','<<unsigned(action)<<']';
                throw std::runtime_error(message.str());
            }
        }
        for(std::uint8_t count=0;count<=6;++count) {
            auto state=g1::begin_support_presentation(count);
            std::uint32_t ticks=0,shown=count ? 1U : 0U,handoffs=0;
            while(!state.complete) {
                const auto action=g1::advance_support_presentation(state,30);
                ++ticks;
                if(action==g1::SupportPresentationAction::show_next_helper)++shown;
                if(action==g1::SupportPresentationAction::start_main_battle)++handoffs;
                require(ticks<=335,"support sequence did not terminate");
            }
            const auto expected=count<5 ? count : 5U;
            require(shown==expected && ticks==expected*67 && handoffs==(count ? 1U : 0U),
                    "30ms support sequence lost a cue or repeated the battle handoff");
            const auto elapsed=state.elapsed_ms;
            require(g1::advance_support_presentation(state,2000)==g1::SupportPresentationAction::waiting &&
                    state.elapsed_ms==elapsed,"completed support sequence emitted another handoff");
            ++sequences;
        }
        // Intentionally unordered native roster indices. Their original helper
        // order is supplied by combat and must not be sorted or deduplicated by
        // the visual controller. The sixth helper contributes to combat but is
        // never displayed by the recovered five-cue presentation loop.
        const std::vector<std::size_t> identities{19,2,21,6,0,17};
        for(std::uint8_t count=0;count<=6;++count) {
            std::vector<std::size_t> helpers(identities.begin(),identities.begin()+count);
            g1::SupportPresentation presentation(76,helpers,effect_steps());
            auto status=presentation.status();
            require(status.token==76 && status.revision==0 && status.helper_count==count,
                    "support controller initial identity differs");
            require(status.complete==(count==0),"empty support bypass differs");
            if(count==0) {
                require(!status.helper_unit && !presentation.animation_state() && status.effect_complete,
                        "empty support started a player or assigned a helper");
            }
            const auto visible=std::min<std::uint8_t>(count,5);
            for(std::uint8_t slot=0;slot<visible;++slot) {
                status=presentation.status();
                require(!status.complete && status.slot==slot && status.helper_unit==helpers[slot] &&
                        status.elapsed_ms==0,"support cue identity or initial clock differs");
                compare_effect(presentation,0);
                for(std::size_t tick=1;tick<=67;++tick) {
                    const auto old_revision=presentation.status().revision;
                    // Reading status is render-only: repeated 60Hz presentation
                    // queries cannot advance the fixed simulation clock.
                    require(presentation.status().revision==old_revision,"status query mutated the support clock");
                    const auto action=presentation.advance(30);
                    status=presentation.status();
                    require(status.revision==old_revision+1,"support update did not advance revision");
                    if(tick<67) {
                        require(action==g1::SupportPresentationAction::waiting && status.slot==slot &&
                                status.helper_unit==helpers[slot] && !status.complete,
                                "support cue handed off before original 2010ms boundary");
                        require(status.elapsed_ms==static_cast<std::int32_t>(tick*30),
                                "support phase elapsed differs");
                        compare_effect(presentation,tick);
                    } else if(slot+1<visible) {
                        require(action==g1::SupportPresentationAction::show_next_helper && !status.complete &&
                                status.slot==slot+1 && status.elapsed_ms==0,
                                "support cue did not reset exactly once for next helper");
                    } else {
                        require(action==g1::SupportPresentationAction::start_main_battle && status.complete &&
                                !status.helper_unit && status.elapsed_ms==2010,
                                "support controller final handoff differs");
                        compare_effect(presentation,tick);
                    }
                }
            }
            status=presentation.status();
            require(presentation.advance(30)==g1::SupportPresentationAction::waiting &&
                    presentation.status().revision==status.revision,
                    "completed support controller advanced or repeated handoff");
            ++controller_sequences;
        }
        bool rejected=false;
        try{(void)g1::begin_support_presentation(7);}catch(const g1::FormatError&){rejected=true;}
        require(rejected,"unsupported seventh helper accepted");
        rejected=false;
        try{g1::SupportPresentation invalid(1,std::vector<std::size_t>(7),effect_steps());}
        catch(const g1::FormatError&){rejected=true;}
        require(rejected,"controller accepted unsupported seventh helper");
        g1::SupportPresentation bounded(1,{2},effect_steps());
        rejected=false;
        try{bounded.advance(0x80000000U);}catch(const std::out_of_range&){rejected=true;}
        require(rejected && bounded.status().revision==0 && bounded.status().elapsed_ms==0,
                "invalid unsigned host clock changed support state");
        compare_effect(bounded,0);
        std::cout<<"{\"original_updates\":"<<g1_support_fixture::rows.size()<<",\"state_comparisons\":"<<comparisons
            <<",\"native_complete_sequences\":"<<sequences<<",\"controller_complete_sequences\":"<<controller_sequences
            <<",\"controller_checks\":"<<checks<<",\"passed\":true}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
