#include "g1/GameSession.hpp"
#include "g1/SupportDisplayGate.hpp"
#include "support_presentation_fixture.hpp"

#include <iostream>

namespace {
std::size_t checks{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

g1::WorldBootstrap fixture() {
    g1::WorldBootstrap w;
    w.mission_resource_id = 315;
    w.stop = g1::BootstrapStop::presentation_required;
    w.grid = {5,5,g1::Bytes(25,0)};
    w.terrain_rules = {{{{1,0,0}}}};
    for (std::uint8_t type=0;type<23;++type) {
        g1::UnitDefinition definition;
        definition.type=type;
        definition.raw_text_indices[0]=124;
        definition.forms.push_back({2,1,1,60,70,50,0,0,0,0,1,0,0,0});
        definition.forms.push_back({4,1,1,40,50,60,0,0,0,0,0,0,0,0});
        w.unit_definitions.push_back(std::move(definition));
    }
    w.units.push_back({11,0,0,0,1,2,0,true,0});
    w.units.push_back({19,1,1,0,4,4,0,true,0});
    w.initialization_effects={
        {g1::WorldInitializationEffectKind::debit_spawn_funds,0,0,0,{},1},
        {g1::WorldInitializationEffectKind::debit_spawn_funds,0,1,0,{},1}};
    w.sites.push_back({{3,2},-1,{{{2,2}}}});
    const std::vector<std::vector<std::uint16_t>> script{
        {3,11,280},{12,1,245,0,0,0,0},{3,11,288},{8,202}};
    for (std::size_t i=0;i<script.size();++i)
        w.commands.push_back({i,0,script[i].size()*2,script[i][0],
            std::vector<std::uint16_t>(script[i].begin()+1,script[i].end())});
    return w;
}

g1::MissionTickInput advance() {
    // Explicit synthetic renderer fixture, not an actual font/layout claim.
    return {16,g1::DialogueLayout{1,16,14,3,40,true}};
}
}

int main() {
    try {
        auto startup=fixture();
        startup.initialization_effects={
            {g1::WorldInitializationEffectKind::begin_current_player_turn,2,0,0,{}},
            {g1::WorldInitializationEffectKind::debit_spawn_funds,3,0,17,{},1},
            {g1::WorldInitializationEffectKind::debit_spawn_funds,4,1,8,{},1}};
        startup.units[1].spawn={9,true,true};
        g1::GameSession initialized(std::move(startup),1234);
        check(initialized.turn().funds==std::array<std::int32_t,2>{{0,0}} &&
              initialized.turn().human_turn_counter_274==1 &&
              initialized.captures().counters_68==std::array<std::int32_t,2>{{1,1}},
              "ordered initial turn credit and spawn debits changed economy");
        check(initialized.combat_units()[1].health==9 && initialized.combat_units()[1].damage_immune,
              "special scripted placement HP/immunity was lost");
        check(initialized.combat_units()[0].preset_34_ms==0 && initialized.combat_units()[0].timer_30_ms==-1,
              "fresh original unit countdown and preset defaults were conflated");
        auto repeated=fixture();
        repeated.initialization_effects.push_back(
            {g1::WorldInitializationEffectKind::debit_spawn_funds,1,0,0,{},1});
        g1::GameSession repeated_session(std::move(repeated),1234);
        check(repeated_session.campaign_context().active_unit_counts==std::array<std::uint8_t,2>{{2,1}},
              "repeated activation count was incorrectly reconstructed from unique roster");
        g1::GameSession session(fixture(),1234);
        check(session.captures().counters_68 == std::array<std::int32_t,2>{{0,0}},
              "cold-start income was invented");
        check(!session.select(0),"script dialogue allowed gameplay selection");
        check(session.move_selected_to({2,2}) == g1::MoveRequestResult::script_controls_input,
              "script dialogue allowed movement");
        (void)session.tick(30,advance());
        check(session.mission().command_index == 0 && session.mission().gate == g1::MissionGate::advance_pending,
              "dialogue consumed dispatch immediately");
        (void)session.tick(30);
        check(session.mission().gate == g1::MissionGate::captured_site_item,
              "goal did not enter gameplay");
        check(!session.select(1),"opponent unit accepted as current-player selection");
        check(session.select(0),"own active unit not selectable");
        check(session.selected_movement_range().has_value(),"movement field missing");
        check(session.move_selected_to({4,4}) == g1::MoveRequestResult::occupied_destination,
              "ordinary movement admitted occupied destination");
        check(session.move_selected_to({9,9}) == g1::MoveRequestResult::outside_map,
              "outside movement admitted");
        check(session.move_selected_to({2,2}) == g1::MoveRequestResult::started,"valid move failed");
        check(!session.selected_can_capture(),"capture admitted during travel");
        check(session.move_selected_to({2,3}) == g1::MoveRequestResult::movement_in_progress,
              "second move admitted while travelling");
        for (int step=0;step<10;++step) (void)session.tick(30);
        check(session.moving(),"one-tile robot movement finished before original11steps");
        check(session.units()[0].spent_budget == 0,"movement charged before arrival");
        const auto arrived = session.tick(30);
        check(arrived.movement_finished && !session.moving(),"movement failed to complete");
        check(session.world().units[0].x == 2 && session.world().units[0].y == 2,
              "world did not receive real move state");
        check(session.units()[0].spent_budget == 1 && session.units()[0].facing == 1,
              "arrival cost/facing not applied");
        check(session.selected_can_undo(),"completed eligible movement did not expose Undo");
        const auto undone_move=session.undo_selected();
        check(undone_move && undone_move->restored_anchor && undone_move->retain_selection &&
              session.units()[0].tile_x==1 && session.units()[0].tile_y==2 &&
              session.units()[0].spent_budget==0 && session.combat_units()[0].x==1 &&
              session.sites()[0].item_states[0][0]==-1 && !session.selected_can_undo(),
              "real movement Undo did not restore only its original action state");
        check(session.move_selected_to({2,2})==g1::MoveRequestResult::started,
              "undone movement could not start again");
        for(int i=0;i<11;++i)(void)session.tick(30);
        check(session.sites()[0].item_states[0][0] == -1 &&
              session.mission().gate == g1::MissionGate::captured_site_item,
              "moving onto objective fabricated capture");
        check(session.move_selected_to({2,3}) == g1::MoveRequestResult::already_moved,
              "remaining budget bypassed one-move admission");
        check(session.selected_can_capture(),"real capture unavailable at objective");
        check(session.capture_selected().status == g1::CaptureStatus::captured,"capture command failed");
        check(session.sites()[0].item_states[0][0] == 0 && session.units()[0].flag_2c == 1,
              "capture did not mutate authentic owner/action state");
        check(session.mission().gate == g1::MissionGate::captured_site_item,
              "capture command directly advanced script");
        (void)session.tick(30);
        check(session.mission().gate == g1::MissionGate::advance_pending &&
              session.units()[0].flag_2c == 0 && session.units()[0].spent_budget == 0,
              "goal observer/action reset not connected");
        (void)session.tick(30);
        check(session.mission().gate == g1::MissionGate::dialogue,"post-capture dialogue missing");
        (void)session.tick(30,advance());
        const auto completion=session.tick(30);
        check(completion.completion.has_value() &&
              session.mission().gate == g1::MissionGate::completion_frame &&
              session.mission().stop == g1::MissionStop::none,
              "actual world state did not enter recovered completion presentation");
        check(!session.end_turn_available(),"completion presentation allowed gameplay input");

        auto battle_world=fixture();
        battle_world.units[1].x=2;battle_world.units[1].y=2;
        battle_world.units[1].spawn.health=1;
        battle_world.unit_definitions[11].forms[0][3]=100;
        battle_world.unit_definitions[11].forms[0][4]=100;
        battle_world.unit_definitions[19].forms[0][5]=0;
        // Helper roster indices deliberately differ from original type order.
        // The target dies immediately in gameplay, while support presentation
        // still needs the unchanged pre-combat tactical view of all units.
        battle_world.units.push_back({5,0,0,0,2,1,0,true,0});
        battle_world.units.push_back({0,0,0,0,3,2,0,true,0});
        battle_world.initialization_effects.push_back(
            {g1::WorldInitializationEffectKind::debit_spawn_funds,2,0,0,{},1});
        battle_world.initialization_effects.push_back(
            {g1::WorldInitializationEffectKind::debit_spawn_funds,3,0,0,{},1});
        battle_world.commands={{0,0,6,3,{11,280}},{1,0,8,12,{2,245,19}},
                               {2,0,2,1,{}},{3,0,4,8,{202}}};
        g1::GameSession combat_session(std::move(battle_world),4321);
        (void)combat_session.tick(30,advance());
        (void)combat_session.tick(30);
        check(combat_session.select(0),"combat actor selection failed");
        check(combat_session.selected_attack_targets()==std::vector<std::size_t>{1},
              "actual opponent target not exposed");
        check(!combat_session.attack_selected(0),"self attack admitted");
        const auto before_battle_units=combat_session.units();
        check(combat_session.attack_selected(1),"actual enemy attack did not start");
        check(combat_session.battle().has_value() && combat_session.battle()->exchange.destroyed.size()==1 &&
              !combat_session.world().units[1].active && !combat_session.units()[1].active &&
              combat_session.combat_units()[1].health==0,
              "combat death did not synchronize native world and occupancy");
        const auto& pending_battle=*combat_session.battle();
        check(pending_battle.units_before.size()==before_battle_units.size() &&
              pending_battle.units_before[1].active && pending_battle.defender_before.active &&
              pending_battle.defender_before.health==1 && pending_battle.units_before[0].flag_2c==0,
              "pending battle snapshot lost living target or pre-action attacker");
        for(std::size_t i=0;i<before_battle_units.size();++i) {
            const auto& before=before_battle_units[i];
            const auto& saved=pending_battle.units_before[i];
            check(saved.active==before.active && saved.form==before.form &&
                  saved.tile_x==before.tile_x && saved.tile_y==before.tile_y &&
                  saved.offset_x==before.offset_x && saved.offset_y==before.offset_y &&
                  saved.facing==before.facing && saved.spent_budget==before.spent_budget &&
                  saved.timer_ms==before.timer_ms && saved.anchor_x==before.anchor_x &&
                  saved.anchor_y==before.anchor_y && saved.anchor_facing==before.anchor_facing &&
                  saved.flag_2c==before.flag_2c && saved.flag_2d==before.flag_2d &&
                  saved.flag_2f==before.flag_2f && saved.flag_38==before.flag_38 &&
                  saved.flag_39==before.flag_39 && saved.flag_3a==before.flag_3a,
                  "render-only pre-combat movement snapshot changed a unit field");
        }
        check(pending_battle.exchange.support_types==std::vector<std::uint8_t>{0,5} &&
              pending_battle.exchange.support_units==std::vector<std::size_t>{3,2},
              "pending battle did not retain exact helpers in original type order");
        for(std::size_t i=0;i<pending_battle.exchange.support_units.size();++i) {
            const auto index=pending_battle.exchange.support_units[i];
            check(combat_session.world().units[index].type==pending_battle.exchange.support_types[i] &&
                  pending_battle.units_before[index].active,
                  "support identity does not reference its real pre-combat unit");
        }
        check(combat_session.mission_record().value_278==1 &&
              combat_session.mission_record().value_27c==0 && combat_session.units()[0].flag_2c==1,
              "combat action/counters were not applied");
        check(!combat_session.select(0) && !combat_session.end_turn_available() &&
              combat_session.selected_attack_targets().empty(),"battle presentation did not block input");
        // Exercise the actual portable support controller with the pending
        // lethal exchange. Synthetic frame receipts exercise the production
        // host gate, not UIKit/Metal presentation. No battle acknowledgement
        // is permitted merely because these 134 support ticks have finished.
        std::vector<g1::AtlasStep> support_steps;
        for(const auto& pair:g1_support_fixture::effect_steps)
            support_steps.push_back({static_cast<std::uint8_t>(pair[0]),pair[1]});
        g1::SupportPresentation support(pending_battle.token,pending_battle.exchange.support_units,
                                       std::move(support_steps));
        g1::SupportDisplayGate support_gate;
        const auto resolved_random=combat_session.random().state();
        const auto resolved_units=combat_session.combat_units();
        const auto pending_token=pending_battle.token;
        std::size_t support_ticks=0;
        while(!support.status().complete) {
            const auto before=support.status();
            if(!support_gate.can_advance(before)) {
                check(!support_gate.observe(before,support_gate.receipt(before,false)),
                      "support fixture released a helper without banner presentation");
                check(support_gate.observe(before,support_gate.receipt(before,true)),
                      "support fixture rejected its current first frame");
            }
            if(support_ticks==10) {
                const auto queued=support_gate.receipt(before,true);
                support_gate.invalidate();
                check(!support_gate.can_advance(before) && !support_gate.observe(before,queued) &&
                      support.status().elapsed_ms==before.elapsed_ms,
                      "support pause consumed time or accepted an old frame");
                check(support_gate.observe(before,support_gate.receipt(before,true)),
                      "resumed support fixture did not accept a fresh frame");
            }
            for(unsigned render=0;render<3;++render)
                check(support.status().revision==before.revision &&
                      support.status().elapsed_ms==before.elapsed_ms,
                      "render-only support queries advanced simulation");
            check(support_gate.can_advance(before),"support fixture advanced before presentation");
            const auto action=support.advance(g1::SimulationClock::step_ms);
            ++support_ticks;
            check(support_ticks<=134,"real two-helper exchange did not hand off after its original cues");
            if(action==g1::SupportPresentationAction::show_next_helper)support_gate.invalidate();
            const auto& now_random=combat_session.random().state();
            check(now_random.words==resolved_random.words && now_random.index==resolved_random.index &&
                  now_random.draws==resolved_random.draws,
                  "support presentation changed the shared gameplay random stream");
            check(combat_session.battle() && combat_session.battle()->token==pending_token &&
                  !combat_session.checkpoint_available() &&
                  combat_session.mission().gate==g1::MissionGate::objective,
                  "support presentation acknowledged a battle, enabled save or advanced its objective");
            check(combat_session.units()[0].flag_2c==1 && !combat_session.units()[1].active &&
                  combat_session.battle()->units_before[1].active,
                  "support presentation changed resolved action/death or its pre-combat snapshot");
            for(std::size_t i=0;i<resolved_units.size();++i) {
                const auto& actual=combat_session.combat_units()[i];
                const auto& resolved=resolved_units[i];
                check(actual.health==resolved.health && actual.active==resolved.active &&
                      actual.support_bonus==resolved.support_bonus,
                      "support presentation recalculated resolved combat state");
            }
        }
        check(support_ticks==134 && !support_gate.can_advance(support.status()) && combat_session.battle(),
              "support completion incorrectly completed the real pending battle");
        (void)combat_session.tick(30);
        check(combat_session.mission().gate==g1::MissionGate::objective,
              "battle presentation was skipped before observing its result");
        const auto token=combat_session.battle()->token;
        bool rejected=false;
        try {combat_session.report_battle_finished(token+1);} catch(const g1::FormatError&){rejected=true;}
        check(rejected && combat_session.battle().has_value(),"stale battle callback unlocked session");
        combat_session.report_battle_finished(token);
        (void)combat_session.tick(30);
        check(combat_session.mission().gate==g1::MissionGate::advance_pending &&
              combat_session.campaign_context().active_unit_counts[1]==0,
              "real combat objective did not resume script after presentation");

        auto repair_world=fixture();
        repair_world.units[0].spawn.health=91;
        repair_world.unit_definitions[11].forms[0][7]=1;
        repair_world.initialization_effects.insert(repair_world.initialization_effects.begin(),
            {g1::WorldInitializationEffectKind::begin_current_player_turn,0,0,0,{},0});
        g1::GameSession repair(std::move(repair_world),1);
        (void)repair.tick(30,advance());(void)repair.tick(30);
        check(repair.select(0) && repair.selected_heal_targets()==std::vector<std::size_t>{0},
              "healing did not expose the actual self target");
        const auto healed=repair.heal_selected(0);
        check(healed && healed->outcome==g1::HealOutcome::healed &&
              repair.combat_units()[0].health==100 && repair.turn().funds[0]==0 &&
              repair.units()[0].flag_2c==1,"native repair did not apply HP/funds/action together");

        g1::GameSession upgrading(fixture(),2);
        (void)upgrading.tick(30,advance());(void)upgrading.tick(30);
        check(upgrading.select(0) && upgrading.selected_upgrade_eligibility()==g1::UpgradeEligibility::allowed,
              "authorized upgrade was unavailable");
        const auto upgraded=upgrading.upgrade_selected();
        check(upgraded && upgrading.combat_units()[0].level==1 &&
              upgrading.combat_units()[0].stats.attack_low==72 && upgrading.units()[0].flag_2c==1,
              "native upgrade did not apply original level/stats/action");

        auto ai_world=fixture();
        ai_world.units[1].x=2;ai_world.units[1].y=2;
        ai_world.sites[0].position={4,4}; // Only the current item is in capture movement range.
        for(auto& form:ai_world.unit_definitions[19].forms)form[3]=form[4]=0;
        g1::GameSession opponent(std::move(ai_world),3);
        (void)opponent.tick(30,advance());(void)opponent.tick(30);
        check(opponent.end_turn().has_value() && opponent.current_player()==1,
              "human end-turn did not enter the real opponent turn");
        bool captured=false,returned=false;
        for(int step=0;step<500 && !returned;++step) {
            // Explicit synthetic host acknowledgements. They are not evidence
            // of an Apple drawable, physical camera, sound or playthrough.
            if(opponent.focus_request())opponent.report_focus_presented(opponent.focus_request()->token);
            for(std::size_t i=0;i<opponent.units().size();++i)
                if(opponent.unit_transform_waiting(i)) {
                    opponent.clear_transform_animation_request(i);
                    opponent.report_transform_finished(i,opponent.unit_transform_generation(i));
                }
            (void)opponent.tick(30);
            captured=captured || opponent.sites()[0].item_states[0][0]==1;
            if(opponent.battle())opponent.report_battle_finished(opponent.battle()->token);
            returned=opponent.current_player()==0;
        }
        check(captured && returned,"native enemy capture did not finish its turn and return control");

        auto production_world=fixture();
        production_world.units[1].type=20;
        production_world.unit_definitions[20].scalar_fields[0]=1;
        production_world.unit_definitions[19].scalar_fields[0]=1;
        production_world.unit_definitions[19].scalar_fields[1]=1;
        production_world.available_types[19]=true;
        for(auto& form:production_world.unit_definitions[20].forms)form[12]=1;
        g1::GameSession production(std::move(production_world),4);
        (void)production.tick(30,advance());(void)production.tick(30);
        check(production.end_turn().has_value(),"production fixture could not enter enemy turn");
        for(int step=0;step<20 && production.units().size()==2;++step) {
            if(production.focus_request())production.report_focus_presented(production.focus_request()->token);
            (void)production.tick(30);
        }
        check(production.world().units.size()==3 && production.world().units[2].type==19 &&
              production.world().units[2].x==4 && production.world().units[2].y==3 &&
              production.units()[2].flag_2c==1 && production.units()[1].flag_2c==0 &&
              production.turn().funds[1]==0 && production.campaign_context().active_unit_counts[1]==2,
              "native AI production did not preserve destination/funds/new-unit action/count rules");

        auto human_build_world=fixture();
        for(auto& form:human_build_world.unit_definitions[11].forms)form[12]=1;
        human_build_world.available_types[8]=true;
        human_build_world.unit_definitions[8].scalar_fields[1]=5;
        g1::GameSession human_build(std::move(human_build_world),6);
        (void)human_build.tick(30,advance());(void)human_build.tick(30);
        check(human_build.select(0) &&
              human_build.selected_production_candidates(g1::ProductionMode::ordinary)==std::vector<std::uint8_t>{8},
              "native human builder did not expose the original inactive available type");
        check(!human_build.produce_selected(g1::ProductionMode::ordinary,8,{1,1},
              g1::ProductionConfirmation::funds_checked_button) && human_build.units().size()==2,
              "funds-checked production mutated the roster without enough funds");
        check(!human_build.produce_selected(g1::ProductionMode::ordinary,8,{4,4},
              g1::ProductionConfirmation::direct_type_list) && human_build.units().size()==2,
              "type-list production admitted a nonadjacent occupied cell");
        const auto built=human_build.produce_selected(g1::ProductionMode::ordinary,8,{1,1},
            g1::ProductionConfirmation::direct_type_list);
        check(built && built->audio_operation==5 && built->audio_argument==16 &&
              human_build.units().size()==3 && human_build.world().units[2].type==8 &&
              human_build.world().units[2].x==1 && human_build.world().units[2].y==1 &&
              human_build.units()[0].flag_2c==1 && human_build.units()[2].flag_2c==1 &&
              human_build.turn().funds[0]==0 && human_build.campaign_context().active_unit_counts[0]==2 &&
              human_build.unit_spawn_generation(2)==1 && !human_build.selected(),
              "human type-list production lost its single debit, both actions, or stable new renderer identity");
        check(human_build.select(0) &&
              human_build.selected_production_eligibility(g1::ProductionMode::ordinary)==g1::ProductionEligibility::action_used,
              "human producer was allowed to build twice in one turn");

        auto carrier_world=fixture();
        carrier_world.units[0].type=6;carrier_world.units[0].spawn.health=91;
        carrier_world.units.push_back({5,0,0,0,2,2,0,true,0});
        carrier_world.initialization_effects.push_back(
            {g1::WorldInitializationEffectKind::debit_spawn_funds,2,0,0,{},1});
        carrier_world.available_types[6]=true;
        for(auto& form:carrier_world.unit_definitions[6].forms)form[8]=1;
        for(auto& form:carrier_world.unit_definitions[5].forms)form[9]=1;
        g1::GameSession carrier(std::move(carrier_world),7);
        (void)carrier.tick(30,advance());(void)carrier.tick(30);
        check(carrier.select(0) && carrier.move_selected_to({2,2})==g1::MoveRequestResult::started,
              "eligible tape could not start the actual route to its friendly carrier");
        for(int i=0;i<11;++i)(void)carrier.tick(30);
        check(!carrier.moving() && !carrier.units()[0].active && !carrier.combat_units()[0].active &&
              !carrier.world().units[0].active && carrier.combat_units()[0].health==91 &&
              carrier.units()[0].spent_budget==1 && carrier.campaign_context().active_unit_counts[0]==1 &&
              carrier.units()[2].active && carrier.units()[2].flag_2c==0 && !carrier.selected(),
              "carrier arrival lost original deactivation, preserved HP, count, or carrier action rules");
        check(carrier.world().movement_board().cells[12].active_owner==0 && carrier.select(2) &&
              carrier.selected_production_candidates(g1::ProductionMode::deploy)==std::vector<std::uint8_t>{6},
              "boarded tape did not become an actual inactive deploy candidate");
        const auto deployed=carrier.produce_selected(g1::ProductionMode::deploy,6,{1,2},
            g1::ProductionConfirmation::direct_type_list);
        check(deployed && carrier.units().size()==3 && carrier.units()[0].active &&
              carrier.combat_units()[0].health==100 && carrier.units()[0].tile_x==1 &&
              carrier.units()[0].tile_y==2 && carrier.units()[0].flag_2c==1 &&
              carrier.units()[2].flag_2c==1 && carrier.campaign_context().active_unit_counts[0]==2 &&
              carrier.unit_spawn_generation(0)==2,
              "native deploy invented cargo HP or failed to reactivate the same type and renderer entry");

        auto undo_world=fixture();
        undo_world.units.push_back({0,0,0,0,0,0,0,true,0});
        undo_world.initialization_effects.push_back(
            {g1::WorldInitializationEffectKind::debit_spawn_funds,2,0,0,{},1});
        undo_world.unit_definitions[11].scalar_fields[2]=1;
        g1::GameSession undoing(std::move(undo_world),8);
        (void)undoing.tick(30,advance());(void)undoing.tick(30);
        check(undoing.select(0) && undoing.transform_selected().has_value() &&
              !undoing.selected_can_undo(),"transformation did not retain its actual presentation gate");
        undoing.clear_transform_animation_request(0);
        undoing.report_transform_finished(0,undoing.unit_transform_generation(0));
        check(undoing.selected_can_undo(),"completed recorded transform was not undoable");
        const auto undone_transform=undoing.undo_selected();
        check(undone_transform && undone_transform->reversed_transform && undone_transform->retain_selection &&
              undoing.units()[0].form==0 && undoing.units()[0].flag_2d==0 &&
              undoing.combat_units()[0].stats.attack_low==60 &&
              undoing.transform_animation_pending(0)==0 && !undoing.unit_transform_waiting(0),
              "transform Undo invented another recorded animation or failed to restore original form/stats");
        check(undoing.move_selected_to({2,2})==g1::MoveRequestResult::started,
              "selection-lock fixture could not execute movement");
        for(int i=0;i<11;++i)(void)undoing.tick(30);
        check(undoing.selected_can_undo() && undoing.select(2) && undoing.select(0) &&
              undoing.units()[0].flag_3a==1 && !undoing.selected_can_undo(),
              "switching to another friendly unit failed to lock committed movement Undo");

        auto assault_world=fixture();
        assault_world.units[1].x=4;assault_world.units[1].y=2;
        assault_world.sites.clear();
        for(auto& form:assault_world.unit_definitions[19].forms)form[10]=0;
        assault_world.unit_definitions[19].scalar_fields[1]=99;
        assault_world.commands={{0,0,6,3,{11,280}},{1,0,8,12,{2,245,19}},
                                {2,0,2,1,{}},{3,0,4,8,{202}}};
        g1::GameSession assault(std::move(assault_world),5);
        (void)assault.tick(30,advance());(void)assault.tick(30);
        check(assault.end_turn().has_value(),"assault fixture could not enter AI turn");
        bool travelled=false,fought=false,back_to_human=false;
        for(int step=0;step<500 && !back_to_human;++step) {
            if(assault.focus_request())assault.report_focus_presented(assault.focus_request()->token);
            for(std::size_t i=0;i<assault.units().size();++i)
                if(assault.unit_transform_waiting(i)) {
                    assault.clear_transform_animation_request(i);
                    assault.report_transform_finished(i,assault.unit_transform_generation(i));
                }
            const auto update=assault.tick(30);
            travelled=travelled || update.movement_finished;
            if(assault.battle()) {
                check(assault.battle()->attacker_before.type==19 &&
                      assault.battle()->defender_before.type==11 &&
                      assault.ai_action() && assault.ai_action()->elapsed_ms>=100,
                      "AI battle lost the actual actor/target or original delay gate");
                fought=true;assault.report_battle_finished(assault.battle()->token);
            }
            back_to_human=assault.current_player()==0;
        }
        check(travelled && fought && back_to_human && assault.combat_units()[0].health<100,
              "native AI move/attack/presentation/turn sequence did not complete");

        for (const int refresh : {30,60,120,144}) {
            g1::SimulationClock clock;
            std::uint32_t previous{}, ticks{};
            for (int frame=1;frame<=refresh;++frame) {
                const auto now=std::uint32_t(frame*1000/refresh);
                ticks+=clock.add_elapsed(now-previous);
                previous=now;
            }
            check(ticks == 33,"display refresh changed simulation update count");
            check(clock.interpolation() > 0.3333F && clock.interpolation() < 0.3334F,
                  "display partition lost fractional simulation time");
            clock.reset();
            check(clock.add_elapsed(0)==0 && clock.interpolation()==0,"resume clock retained catch-up time");
            check(clock.add_elapsed(2000)==33,"original1000ms delta bound not respected");
        }
        std::cout << checks << " native session/clock integration checks passed; synthetic mission, not a playthrough\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
