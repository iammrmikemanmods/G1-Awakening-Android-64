#pragma once

#include "g1/GameSession.hpp"
#include "g1/SupportGeometry.hpp"
#include "g1/SupportOverlayAssets.hpp"
#include "g1/SupportPresentation.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <tuple>

namespace g1::tools {

// Development-only check performed before the pilot's existing synthetic
// battle acknowledgement. The real session is const throughout this check.
// It prepares source-asset geometry, not Metal commands or device frames.
class SupportPilotValidation {
public:
    explicit SupportPilotValidation(const Archive& archive)
        : assets_(load_support_overlay_assets(archive)) {}

    void verify(const GameSession& session) {
        try {verify_battle(session);}
        catch(...) {failed_=true;throw;}
    }

    void write_report(const std::string& path) const {
        std::ofstream out(path);
        if(!out)throw FormatError("cannot write support pilot validation report");
        out<<"{\n\"scope\":\"headless source-asset support preparation before synthetic battle acknowledgement\",\n"
            <<"\"gpu_rendered\":false,\n\"physical_device_test\":false,\n"
            <<"\"presentation_feedback\":\"synthetic; unchanged command pilot\",\n"
            <<"\"projection\":\"synthetic orthographic map bounds; UIKit tactical camera not tested\",\n"
            <<"\"viewport\":[1792,828],\n\"simulation_step_ms\":30,\n"
            <<"\"asset_loads\":1,\n\"image_resource\":"<<assets_.image_resource_id
            <<",\n\"descriptor_resource\":256,\n\"animation_index\":2,\n"
            <<"\"battles_checked\":"<<battles_<<",\n\"helper_count_histogram\":[";
        for(std::size_t i=0;i<helper_counts_.size();++i) {if(i)out<<',';out<<helper_counts_[i];}
        out<<"],\n\"simulated_visible_cues\":"<<cues_<<",\n\"support_simulation_ticks\":"<<ticks_
            <<",\n\"effect_frames_prepared\":"<<frames_<<",\n\"empty_blink_frames\":"<<empty_frames_
            <<",\n\"completed_effect_hidden_ticks\":"<<hidden_ticks_<<",\n\"vertices_prepared\":"<<vertices_
            <<",\n\"lethal_battles\":"<<lethal_battles_<<",\n\"live_state_comparisons\":"<<state_comparisons_
            <<",\n\"checks\":"<<checks_<<",\n\"passed\":"<<(failed_ ? "false" : "true")<<"\n}\n";
        if(!out)throw FormatError("support pilot validation report write failed");
    }

private:
    SupportOverlayAssets assets_;
    std::array<std::size_t,7> helper_counts_{};
    std::size_t battles_{},cues_{},ticks_{},frames_{},empty_frames_{},hidden_ticks_{},vertices_{};
    std::size_t lethal_battles_{},state_comparisons_{},checks_{};
    bool failed_{};

    void check(bool condition,const char* message) {
        ++checks_;
        if(!condition)throw FormatError(message);
    }

    static bool same_movement(const MovementUnitState& a,const MovementUnitState& b) {
        return std::tie(a.active,a.form,a.tile_x,a.tile_y,a.offset_x,a.offset_y,a.facing,a.spent_budget,
                        a.timer_ms,a.anchor_x,a.anchor_y,a.anchor_facing,a.flag_2c,a.flag_2d,a.flag_2f,
                        a.flag_38,a.flag_39,a.flag_3a)==
               std::tie(b.active,b.form,b.tile_x,b.tile_y,b.offset_x,b.offset_y,b.facing,b.spent_budget,
                        b.timer_ms,b.anchor_x,b.anchor_y,b.anchor_facing,b.flag_2c,b.flag_2d,b.flag_2f,
                        b.flag_38,b.flag_39,b.flag_3a);
    }

    static bool same_combat(const CombatUnitState& a,const CombatUnitState& b) {
        return std::tie(a.type,a.owner,a.faction,a.form,a.x,a.y,a.level,a.health,a.active,a.damage_immune,
                        a.flag_38,a.stats.attack_low,a.stats.attack_high,a.stats.defense,
                        a.support_bonus,a.timer_30_ms,a.preset_34_ms)==
               std::tie(b.type,b.owner,b.faction,b.form,b.x,b.y,b.level,b.health,b.active,b.damage_immune,
                        b.flag_38,b.stats.attack_low,b.stats.attack_high,b.stats.defense,
                        b.support_bonus,b.timer_30_ms,b.preset_34_ms);
    }

    void verify_battle(const GameSession& session) {
        check(session.battle().has_value(),"support pilot requires an actual pending battle");
        const auto battle=*session.battle();
        const auto before_units=session.units();
        const auto before_combat=session.combat_units();
        const auto before_random=session.random().state();
        check(battle.units_before.size()==before_units.size(),"support pilot lost pre-combat roster snapshot");
        check(battle.attacker<battle.units_before.size() && battle.defender<battle.units_before.size(),
              "support pilot participant index is outside its snapshot");
        check(battle.units_before[battle.attacker].active && battle.units_before[battle.defender].active &&
              battle.attacker_before.active && battle.defender_before.active &&
              battle.attacker_before.health>0 && battle.defender_before.health>0,
              "support pilot participant vanished before its resolved battle presentation");
        const auto count=battle.exchange.support_units.size();
        check(count<=support_storage_slots && count==battle.exchange.support_types.size(),
              "support pilot helper identity counts differ");
        for(std::size_t i=0;i<count;++i) {
            const auto index=battle.exchange.support_units[i];
            check(index<battle.units_before.size() && index<session.world().units.size(),
                  "support pilot helper index is outside the original roster");
            check(battle.units_before[index].active &&
                  session.world().units[index].type==battle.exchange.support_types[i] &&
                  before_combat[index].type==battle.exchange.support_types[i],
                  "support pilot helper identity differs from its actual pre-combat unit");
        }
        const auto unchanged=[&] {
            check(session.battle() && session.battle()->token==battle.token,
                  "support preparation acknowledged or replaced the pending battle");
            const auto& random=session.random().state();
            check(random.words==before_random.words && random.index==before_random.index &&
                  random.draws==before_random.draws,"support preparation changed the full gameplay RNG state");
            check(session.units().size()==before_units.size() && session.combat_units().size()==before_combat.size(),
                  "support preparation changed live roster size");
            for(std::size_t i=0;i<before_units.size();++i) {
                check(same_movement(session.units()[i],before_units[i]),
                      "support preparation changed live movement or action state");
                check(same_combat(session.combat_units()[i],before_combat[i]),
                      "support preparation changed live health, statistics or combat state");
            }
            ++state_comparisons_;
        };
        unchanged();

        // An explicit test camera covering map bounds. The platform camera's
        // focus/easing and its drawable callbacks remain separate device tests.
        std::array<float,16> projection{};
        projection[0]=2.0F/(float(session.world().grid.width)*0.32F);
        projection[9]=-2.0F/(float(session.world().grid.height)*0.32F);
        projection[12]=-1;projection[13]=1;projection[15]=1;
        SupportPresentation presentation(battle.token,battle.exchange.support_units,
            assets_.descriptor.animations().at(support_effect_animation));
        std::size_t shown=count ? 1U : 0U;
        std::size_t battle_ticks=0,cue_ticks=0,handoffs=0;
        while(!presentation.status().complete) {
            const auto state=presentation.status();
            check(state.token==battle.token && state.slot<count && state.slot<support_presented_slots &&
                  state.helper_unit==battle.exchange.support_units[state.slot],
                  "support pilot displayed the wrong battle helper");
            const auto& helper=battle.units_before[*state.helper_unit];
            if(!state.effect_complete) {
                const auto geometry=support_overlay_vertices(assets_.descriptor,state.effect_frame,
                    assets_.image.width,assets_.image.height,projection,helper.tile_x,helper.tile_y,1792,828);
                check(geometry.size()%6==0,"support geometry is not a whole number of source-image parts");
                for(const auto& vertex:geometry) {
                    for(const auto value:vertex.position)
                        check(std::isfinite(value),"support pilot prepared a nonfinite position");
                    for(const auto value:vertex.uv)
                        check(std::isfinite(value) && value>=0 && value<=1,"support pilot prepared an invalid source UV");
                }
                ++frames_;vertices_+=geometry.size();
                if(geometry.empty())++empty_frames_;
            } else {
                check(cue_ticks>=56,"actual support source effect completed before1680ms");
                ++hidden_ticks_;
            }
            const auto action=presentation.advance(30);
            ++battle_ticks;++cue_ticks;
            unchanged();
            if(action==SupportPresentationAction::waiting) {
                check(cue_ticks<67,"support cue exceeded original2010ms before handoff");
                check(presentation.status().effect_complete==(cue_ticks>=56),
                      "actual support source effect completion differs from original1680ms");
            } else {
                check(cue_ticks==67,"support cue did not use exactly67 original30ms ticks");
                if(action==SupportPresentationAction::show_next_helper) {++shown;cue_ticks=0;}
                else ++handoffs;
            }
        }
        const auto expected=std::min<std::size_t>(count,support_presented_slots);
        check(shown==expected && battle_ticks==expected*67 && handoffs==(count ? 1U : 0U),
              "support pilot lost a cue, truncated combat helpers or repeated a handoff");
        const auto final=presentation.status();
        check(presentation.advance(30)==SupportPresentationAction::waiting &&
              presentation.status().revision==final.revision,
              "support pilot's completed sequence advanced again");
        unchanged();
        ++battles_;++helper_counts_[count];cues_+=shown;ticks_+=battle_ticks;
        if(!battle.exchange.destroyed.empty())++lethal_battles_;
    }
};

} // namespace g1::tools
