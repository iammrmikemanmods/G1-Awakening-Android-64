#include "g1/UnitSpawn.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::size_t checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

g1::UnitDefinition definition(std::uint8_t type, std::uint8_t field6, std::uint8_t cost) {
    g1::UnitDefinition result;
    result.type = type;
    result.scalar_fields = {{static_cast<std::uint8_t>(type % 2U), cost, 0, 0, 0}};
    result.forms = {{3,1,1,250,20,128,0,0,0,0,1,0,0,0},
                    {6,1,1,1,2,3,field6,0,0,0,1,0,0,1}};
    return result;
}

std::vector<std::int32_t> values(const g1::UnitActivation& value, std::int32_t funds) {
    const auto& m = value.movement;
    const auto& c = value.combat;
    const auto& w = value.world;
    return {m.active,m.form,m.tile_x,m.tile_y,m.offset_x,m.offset_y,m.facing,m.spent_budget,
        m.timer_ms,m.anchor_x,m.anchor_y,m.anchor_facing,m.flag_2c,m.flag_2d,m.flag_2f,
        m.flag_38,m.flag_39,m.flag_3a,
        c.active,c.form,c.health,c.level,c.damage_immune,c.stats.attack_low,c.stats.attack_high,
        c.stats.defense,c.support_bonus,c.preset_34_ms,c.timer_30_ms,c.flag_38,
        w.form,w.spawn.health,w.spawn.flag_18,w.spawn.damage_immune,value.signed_cost,
        g1::apply_spawn_funds_debit(funds,value.signed_cost),value.clear_pending_route ? -1 : 12};
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: unit_spawn_test original_fixture");
        std::ifstream input(argv[1]);
        std::string magic;
        std::size_t rows{}, input_count{}, output_count{};
        if (!(input >> magic >> rows >> input_count >> output_count) || magic != "G1UNITSPAWN1" ||
            rows != 192U || input_count != 12U || output_count != 37U)
            throw std::runtime_error("invalid original unit-activation fixture header");
        for (std::size_t row=0; row<rows; ++row) {
            std::array<std::int32_t,12> in{};
            std::array<std::int32_t,37> expected{};
            for (auto& value : in) if (!(input >> value)) throw std::runtime_error("truncated input row");
            for (auto& value : expected) if (!(input >> value)) throw std::runtime_error("truncated output row");
            const auto type = static_cast<std::uint8_t>(in[0]);
            const auto d = definition(type,static_cast<std::uint8_t>(in[1]),static_cast<std::uint8_t>(in[11]));
            g1::WorldUnit identity;
            identity.type=type;
            identity.owner=identity.faction=static_cast<std::uint8_t>(type % 2U);
            identity.placement_command_index=99;
            g1::MovementUnitState m;
            m.active=row % 2U != 0U;
            m.form=1; m.tile_x=3; m.tile_y=3; m.offset_x=0x1234; m.offset_y=-7;
            m.facing=3; m.spent_budget=99; m.timer_ms=12345;
            m.anchor_x=2; m.anchor_y=2; m.anchor_facing=0;
            m.flag_2c=1; m.flag_2d=2; m.flag_2f=3; m.flag_38=4; m.flag_39=5; m.flag_3a=6;
            g1::CombatUnitState c;
            c.type=type; c.owner=c.faction=identity.owner; c.form=1; c.x=3; c.y=3;
            c.level=7; c.health=13; c.active=m.active; c.damage_immune=true; c.flag_38=4;
            c.stats={99,98,97}; c.support_bonus=in[7]; c.preset_34_ms=in[6]; c.timer_30_ms=12345;
            const auto actual = g1::activate_unit(d,identity,
                {static_cast<std::uint8_t>(in[8]),static_cast<std::uint8_t>(in[9])},
                static_cast<std::uint8_t>(in[2]),static_cast<std::uint8_t>(in[3]),m,c,
                {in[4]!=0,in[5]!=0});
            const auto output = values(actual,in[10]);
            check(output.size()==expected.size(),"native activation field count differs");
            for (std::size_t n=0;n<expected.size();++n) {
                if (output[n]!=expected[n])
                    throw std::runtime_error("original activation mismatch at row " +
                        std::to_string(row) + " field " + std::to_string(n));
                ++checks;
            }
            check(actual.world.type==type && actual.world.faction==identity.faction &&
                  actual.world.owner==identity.owner && actual.world.placement_command_index==99U &&
                  actual.active_count_increment==1U,
                  "activation changed resolved identity or command token");
            check(m.offset_x==0x1234 && m.flag_3a==6 && c.health==13U && c.level==7U,
                  "activation mutated its input objects");
        }
        input >> std::ws;
        check(input.eof(),"trailing original activation fixture data");
        for (int bad=0;bad<4;++bad) {
            auto d=definition(11,1,17);
            g1::WorldUnit identity; identity.type=11;
            if (bad==0) d.forms.pop_back();
            if (bad==1) d.forms[1].pop_back();
            if (bad==2) identity.type=22;
            if (bad==3) identity.owner=2;
            bool rejected=false;
            try { (void)g1::activate_unit(d,identity,{0,0},1,0,{},{}); }
            catch (const g1::FormatError&) { rejected=true; }
            check(rejected,"malformed activation input was accepted");
        }
        std::cout << "Passed " << checks << " unit activation comparisons/guards across "
                  << rows << " original-function cases.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
