#include "g1/SupportPresentation.hpp"
#include "support_presentation_fixture.hpp"

#include <iostream>
#include <sstream>

int main() {
    try {
        std::size_t comparisons=0,sequences=0;
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
                if(ticks>335)throw std::runtime_error("support sequence did not terminate");
            }
            const auto expected=count<5 ? count : 5U;
            if(shown!=expected || ticks!=expected*67 || handoffs!=(count ? 1U : 0U))
                throw std::runtime_error("30ms support sequence lost a cue or repeated the battle handoff");
            const auto elapsed=state.elapsed_ms;
            if(g1::advance_support_presentation(state,2000)!=g1::SupportPresentationAction::waiting ||
               state.elapsed_ms!=elapsed)throw std::runtime_error("completed support sequence emitted another handoff");
            ++sequences;
        }
        bool rejected=false;
        try{(void)g1::begin_support_presentation(7);}catch(const g1::FormatError&){rejected=true;}
        if(!rejected)throw std::runtime_error("unsupported seventh helper accepted");
        std::cout<<"{\"original_updates\":"<<g1_support_fixture::rows.size()<<",\"state_comparisons\":"<<comparisons
            <<",\"native_complete_sequences\":"<<sequences<<",\"passed\":true}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
