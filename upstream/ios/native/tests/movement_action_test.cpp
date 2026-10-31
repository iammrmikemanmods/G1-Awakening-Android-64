#include "g1/MovementAction.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks{};
void require(bool value, const char* detail) {
    ++checks;
    if (!value) throw std::runtime_error(detail);
}
template<class F> void rejects(F&& function, const char* detail) {
    bool threw{};
    try { function(); } catch (const std::exception&) { threw = true; }
    require(threw, detail);
}
g1::MovementRange field() { return {3, 3, {4,3,2, 3,2,1, 2,1,0}, true}; }
g1::MovementUnitState unit() {
    g1::MovementUnitState result;
    result.timer_ms = 100;
    return result;
}
g1::MovementProfile profile() { return {0,0,4,0,0}; }
g1::MovementAction action(g1::MovementUnitState state = unit(), int form0_field2 = 0) {
    const auto range = field();
    return {state, profile(), range, g1::choose_movement_route(range, {0,0}, {2,2}),
            static_cast<std::int8_t>(form0_field2)};
}
void finish(g1::MovementAction& value) {
    for (int i = 0; value.moving() && i < 100; ++i) value.advance_tick(30);
    require(!value.moving(), "valid route finishes within its tick bound");
}
}

int main() {
    try {
        auto range = field();
        const auto route = g1::choose_movement_route(range, {0,0}, {2,2});
        require(route.status == g1::MovementRouteStatus::ready && route.steps.size() == 4,
                "four forward steps exclude origin");
        require(route.steps[0].x == 1 && route.steps[0].y == 0 &&
                route.steps[1].x == 2 && route.steps[1].y == 0 &&
                route.steps[2].x == 2 && route.steps[2].y == 1,
                "reverse backtracking prefers UP over LEFT on equal budgets");
        require(g1::choose_movement_route(range,{1,1},{1,1}).status ==
                    g1::MovementRouteStatus::already_at_destination,
                "same destination is distinguished from a route with travel steps");
        range.remaining[8] = -1;
        require(g1::choose_movement_route(range,{0,0},{2,2}).status ==
                    g1::MovementRouteStatus::unreachable, "unvisited target rejects");
        range.remaining[8] = 127;
        require(g1::choose_movement_route(range,{0,0},{2,2}).status ==
                    g1::MovementRouteStatus::unreachable, "reserved target rejects");
        range.remaining.assign(9,-1); range.remaining[8]=0;
        require(g1::choose_movement_route(range,{0,0},{2,2}).status ==
                    g1::MovementRouteStatus::disconnected_field, "isolated target rejects");
        range.remaining.assign(9,0);
        require(g1::choose_movement_route(range,{0,0},{2,2}).status ==
                    g1::MovementRouteStatus::cyclic_field, "zero-cost tie cycle rejects instead of rerouting");
        g1::MovementRange corridor{20,20,std::vector<std::int32_t>(400,-1),true};
        int order=0;
        for(int row=0;row<10;++row) {
            for(int step=0;step<20;++step) {
                const int x=(row%2)==0?step:19-step;
                corridor.remaining[std::size_t(row*2)*20+std::size_t(x)]=255-order++;
            }
            if(row!=9) {
                const int x=(row%2)==0?19:0;
                corridor.remaining[std::size_t(row*2+1)*20+std::size_t(x)]=255-order++;
            }
        }
        require(g1::choose_movement_route(corridor,{0,0},{0,18}).status ==
                    g1::MovementRouteStatus::original_storage_limit,
                "long valid-gradient corridor rejects original route storage overflow");
        range = field(); range.remaining.pop_back();
        rejects([&]{g1::choose_movement_route(range,{0,0},{2,2});}, "truncated field rejects");
        range = field();
        rejects([&]{g1::choose_movement_route(range,{0,0},{3,2});}, "outside coordinate rejects");
        auto moving = action();
        require(moving.pending_cost() == 4 && moving.state().spent_budget == 0,
                "move begin defers spending");
        require(moving.undo_ordinary() == g1::MovementUndoResult::still_moving,
                "mid-motion undo boundary is explicit");
        moving.advance_tick(0);
        require(moving.state().offset_x == 3 && moving.state().tile_x == 0 &&
                moving.state().timer_ms == 100 && moving.state().facing == 3,
                "zero-ms simulation tick still moves three pixels");
        for (int i=0;i<5;++i) moving.advance_tick(1);
        require(moving.state().tile_x == 1 && moving.state().offset_x == -14,
                "positive crossing normalizes at offset16");
        for (int i=0;i<4;++i) moving.advance_tick(1);
        require(moving.state().offset_x == -2 && moving.route_index() == 0,
                "route has not reached first waypoint after ten ticks");
        require(moving.advance_tick(1) == g1::MovementTickResult::waypoint_reached &&
                moving.state().offset_x == 0 && moving.route_index() == 1,
                "eleventh tick clamps exactly and advances one waypoint");
        require(moving.state().spent_budget == 0, "intermediate waypoint does not charge");
        finish(moving);
        require(moving.state().tile_x == 2 && moving.state().tile_y == 2 &&
                moving.state().offset_x == 0 && moving.state().offset_y == 0 &&
                moving.state().spent_budget == 4 && moving.state().facing == 1,
                "finish charges and retains last travel direction");
        const auto spent = moving.state().spent_budget;
        require(moving.advance_tick(0) == g1::MovementTickResult::stationary &&
                moving.state().spent_budget == spent, "completed action never charges twice");
        require(moving.state().timer_ms == 0, "separate countdown clamps at zero");
        require(moving.undo_ordinary() == g1::MovementUndoResult::restored &&
                moving.state().tile_x == 0 && moving.state().tile_y == 0 &&
                moving.state().spent_budget == 0 && moving.state().facing == 1,
                "ordinary undo restores anchor and clears spent movement");
        auto transformed = unit(); transformed.form=1;
        auto fast = action(transformed,2);
        require(fast.state().flag_38 == 1, "form-zero field2 controls begin auxiliary flag");
        for (int i=0;i<12;++i) fast.advance_tick(30);
        require(fast.state().tile_x == 2 && fast.state().tile_y == 0 && fast.route_index()==2,
                "nonzero form takes six ticks per horizontal tile");
        for (int i=0;i<4;++i) fast.advance_tick(30);
        require(fast.state().tile_y == 1 && fast.state().offset_y == 0 && fast.route_index()==3,
                "nonzero form takes four ticks per vertical tile");
        finish(fast);
        require(fast.state().flag_38 == 1, "completion preserves auxiliary rule");
        auto special = unit(); special.flag_3a=1;
        auto special_action = action(special); finish(special_action);
        require(special_action.undo_ordinary() == g1::MovementUndoResult::unsupported_special_state &&
                special_action.state().spent_budget == 4, "special undo rejects atomically");
        special = unit(); special.flag_2d=1;
        auto transform_undo = action(special); finish(transform_undo);
        require(transform_undo.undo_ordinary() == g1::MovementUndoResult::unsupported_special_state,
                "transform-related undo is outside ordinary branch");
        auto bad = unit(); bad.offset_x=1;
        rejects([&]{action(bad);}, "noncentered move begin rejects");
        auto valid = action();
        rejects([&]{valid.advance_tick(0x80000000U);}, "overflowing delta rejects");
        require(valid.state().offset_x == 0, "bad delta does not mutate position");
        auto turn = unit();
        turn.tile_x=2;turn.tile_y=1;turn.anchor_facing=3;turn.facing=2;
        turn.offset_x=-7;turn.offset_y=12;turn.spent_budget=5;
        turn.flag_2c=turn.flag_2d=turn.flag_2f=turn.flag_38=turn.flag_39=turn.flag_3a=1;
        g1::reset_movement_turn(turn);
        require(turn.anchor_x==2 && turn.anchor_y==1 && turn.spent_budget==0 &&
                turn.flag_2c==0 && turn.flag_2d==0 && turn.flag_2f==0 &&
                turn.flag_38==0 && turn.flag_39==0 && turn.flag_3a==0,
                "unit turn reset clears proven fields and records current tile");
        require(turn.offset_x==-7 && turn.offset_y==12 && turn.facing==2 &&
                turn.anchor_facing==3 && turn.timer_ms==100,
                "turn reset preserves offsets, both facing fields and countdown");
        // These expectations are compared with unchanged 49e90, 43e2e and
        // 4351c by analysis_tools/movement_action/empty_route_oracle.py.
        g1::MovementRange same_range{1,1,{3},false};
        g1::MovementProfile same_profile{0,0,3,0,0};
        auto same_state=unit(); same_state.facing=2; same_state.anchor_facing=2;
        const auto same_route=g1::choose_movement_route(same_range,{0,0},{0,0});
        g1::MovementAction same_move(same_state,same_profile,same_range,same_route,2);
        require(same_move.moving() && same_move.route().steps.empty() &&
                same_move.route_index()==0 && same_move.pending_cost()==0 &&
                same_move.state().spent_budget==0 && same_move.state().flag_38==1,
                "admitted same-tile begin sets moving and predicate flag without completing");
        require(same_move.undo_ordinary()==g1::MovementUndoResult::still_moving,
                "same-tile action retains begin-to-first-tick boundary");
        require(same_move.advance_tick(30)==g1::MovementTickResult::completed &&
                !same_move.moving() && same_move.state().tile_x==0 &&
                same_move.state().tile_y==0 && same_move.state().offset_x==0 &&
                same_move.state().offset_y==0 && same_move.state().facing==2 &&
                same_move.route_index()==0 && same_move.state().timer_ms==70 &&
                same_move.state().spent_budget==0,
                "first same-tile tick completes without displacement and updates countdown");
        require(same_move.advance_tick(30)==g1::MovementTickResult::stationary &&
                same_move.state().timer_ms==40 && same_move.state().spent_budget==0,
                "later same-tile ticks do not repeat completion or charge");
        same_profile.spent_budget=253; // Remaining byte budget is 6.
        same_state.spent_budget=253; same_state.timer_ms=-1; same_state.form=1;
        g1::MovementAction stale_same(same_state,same_profile,same_range,same_route,0);
        require(stale_same.pending_cost()==3 && stale_same.state().spent_budget==253,
                "same-tile begin preserves deferred arithmetic from supplied range");
        require(stale_same.advance_tick(0)==g1::MovementTickResult::completed &&
                stale_same.state().spent_budget==0 && stale_same.state().timer_ms==-1 &&
                stale_same.state().facing==2,
                "zero-ms same-tile tick charges with byte wrap and preserves disabled timer");
        same_profile.base_budget=127; same_profile.spent_budget=0;
        same_state.spent_budget=0; same_range.remaining[0]=127;
        g1::MovementAction sentinel_same(same_state,same_profile,same_range,same_route,0);
        require(sentinel_same.pending_cost()==0 &&
                sentinel_same.advance_tick(1)==g1::MovementTickResult::completed,
                "same-origin base case admits actual budget127 without neighbor scanning");
        auto forged=same_route; forged.status=g1::MovementRouteStatus::ready;
        rejects([&]{g1::MovementAction value(same_state,same_profile,same_range,forged,0);},
                "empty ready route cannot bypass same-tile status validation");
        forged=same_route; forged.steps.push_back({0,0});
        rejects([&]{g1::MovementAction value(same_state,same_profile,same_range,forged,0);},
                "same-tile route with fabricated travel step rejects");
        forged=same_route; forged.destination={1,0};
        rejects([&]{g1::MovementAction value(same_state,same_profile,same_range,forged,0);},
                "same-tile status cannot admit a different destination");
        same_range.remaining[0]=-1;
        rejects([&]{g1::MovementAction value(same_state,same_profile,same_range,same_route,0);},
                "same-tile action still rejects inconsistent negative destination budget");
        std::cout << "movement action contracts: " << checks << " passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
