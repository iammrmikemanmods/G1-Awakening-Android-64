#include "g1/Combat.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::size_t checks{};
std::size_t support_roster_cases{};

void require(bool condition, const std::string& what) {
    ++checks;
    if (!condition) throw std::runtime_error(what);
}

template<class T>
T number(std::istream& input) {
    long long value{};
    if (!(input >> value) || value < static_cast<long long>(std::numeric_limits<T>::min()) ||
        value > static_cast<long long>(std::numeric_limits<T>::max()))
        throw std::runtime_error("combat fixture numeric field is invalid");
    return static_cast<T>(value);
}

void token(std::istream& input, const std::string& expected) {
    std::string actual;
    if (!(input >> actual) || actual != expected)
        throw std::runtime_error("combat fixture expected " + expected + ", got " + actual);
}

std::vector<g1::UnitDefinition> definitions() {
    std::vector<g1::UnitDefinition> result(23);
    for (std::uint8_t i = 0; i != result.size(); ++i) {
        result[i].type = i;
        result[i].forms = {g1::Bytes(14), g1::Bytes(14)};
    }
    return result;
}

bool same_random(const g1::GameRandom::State& a, const g1::GameRandom::State& b) {
    return a.words == b.words && a.index == b.index && a.draws == b.draws;
}

std::string unit_snapshot(const std::vector<g1::CombatUnitState>& units) {
    std::ostringstream text;
    for (const auto& u : units)
        text << +u.type << ',' << +u.owner << ',' << +u.faction << ',' << +u.form << ','
             << +u.x << ',' << +u.y << ',' << +u.level << ',' << +u.health << ','
             << u.active << ',' << u.damage_immune << ',' << +u.flag_38 << ','
             << u.stats.attack_low << ',' << u.stats.attack_high << ',' << u.stats.defense << ','
             << u.support_bonus << ',' << u.timer_30_ms << ',' << u.preset_34_ms << ';';
    return text.str();
}

struct Case {
    std::uint32_t seed{};
    bool flag{};
    std::size_t attacker{}, defender{};
    g1::TerrainGrid grid;
    g1::TerrainLookup lookup{};
    g1::TerrainRules terrain;
    std::vector<g1::UnitDefinition> types{definitions()};
    std::vector<g1::CombatUnitState> units;

    g1::CombatRulesView rules() const { return {grid, lookup, terrain, types, flag}; }
};

void compare_strike(std::istream& input, const g1::CombatStrike& strike) {
    token(input,"A");
    require(strike.rolled_damage == number<std::int32_t>(input), "original strike damage differs");
    require(strike.health_before == number<std::uint8_t>(input), "original pre-hit health differs");
    require(strike.health_after == number<std::uint8_t>(input), "original post-hit health differs");
    require(strike.immune == number<std::uint8_t>(input), "original immunity differs");
    require(strike.evaded == number<std::uint8_t>(input), "original evade branch differs");
    require(strike.random_words == number<std::uint32_t>(input), "original strike random draws differ");
}

Case battle_case(std::istream& input) {
    Case c;
    c.seed=number<std::uint32_t>(input);
    c.flag=number<std::uint8_t>(input) != 0;
    c.grid.width=number<std::uint16_t>(input);
    c.grid.height=number<std::uint16_t>(input);
    const auto count=number<std::uint8_t>(input);
    c.attacker=number<std::uint8_t>(input);
    c.defender=number<std::uint8_t>(input);
    token(input,"G");
    for (std::size_t i=0;i<static_cast<std::size_t>(c.grid.width)*c.grid.height;++i)
        c.grid.tiles.push_back(number<std::uint8_t>(input));
    token(input,"L");
    for (auto& v:c.lookup) v=number<std::uint8_t>(input);
    token(input,"T");
    c.terrain.resize(number<std::uint8_t>(input));
    for (auto& row:c.terrain)
        for (auto& v:row.fields) v=number<std::uint8_t>(input);
    for (std::size_t i=0;i<count;++i) {
        token(input,"U");
        g1::CombatUnitState u;
        u.type=number<std::uint8_t>(input);
        u.owner=number<std::uint8_t>(input);
        u.faction=number<std::uint8_t>(input);
        u.form=number<std::uint8_t>(input);
        u.x=number<std::uint8_t>(input);
        u.y=number<std::uint8_t>(input);
        u.level=number<std::uint8_t>(input);
        u.health=number<std::uint8_t>(input);
        u.active=number<std::uint8_t>(input) != 0;
        u.damage_immune=number<std::uint8_t>(input) != 0;
        u.flag_38=number<std::uint8_t>(input);
        u.support_bonus=number<std::int32_t>(input);
        u.timer_30_ms=number<std::int32_t>(input);
        u.preset_34_ms=number<std::int32_t>(input);
        auto& type=c.types.at(u.type);
        type.scalar_fields[0]=u.faction;
        type.scalar_fields[3]=number<std::uint8_t>(input);
        type.scalar_fields[4]=number<std::uint8_t>(input);
        for (auto& form:type.forms)
            for (auto& value:form) value=number<std::uint8_t>(input);
        u.stats=g1::combat_stats(type,u.form,u.level);
        c.units.push_back(u);
    }
    token(input,"E");
    require(g1::combat_can_target(c.rules(),c.units,c.attacker,c.defender)==number<std::uint8_t>(input),
            "original attacker target predicate differs");
    require(g1::combat_can_target(c.rules(),c.units,c.defender,c.attacker)==number<std::uint8_t>(input),
            "original defender target predicate differs");
    const auto a=g1::combat_bonuses(c.rules(),c.units,c.attacker,c.defender);
    const auto d=g1::combat_bonuses(c.rules(),c.units,c.defender,c.attacker);
    for (const auto value:{a.attack,a.defense,d.attack,d.defense})
        require(value==number<std::int32_t>(input),"original combat world modifier differs");
    const auto counter=number<std::uint8_t>(input);
    const auto draws=number<std::uint32_t>(input);
    const auto index=number<std::uint32_t>(input);
    std::vector<std::uint8_t> support(number<std::uint8_t>(input));
    for (auto& v:support) v=number<std::uint8_t>(input);
    const auto strikes=number<std::uint8_t>(input);
    const auto deaths=number<std::uint8_t>(input);
    auto changed=c.units;
    g1::GameRandom random(c.seed);
    const auto result=g1::resolve_combat_exchange(c.rules(),changed,c.attacker,c.defender,random);
    require(result.counter_eligible_before_attack==counter,"original pre-hit counter eligibility differs");
    require(result.random_words==draws,"original exchange random draw count differs");
    require(random.state().index==index,"original exchange random state index differs");
    require(result.support_types==support,"original combined-support ordered participants differ");
    require(result.support_units.size()==support.size(),"support helper indices were lost");
    for(std::size_t i=0;i<support.size();++i) {
        const auto found=std::find_if(c.units.begin(),c.units.end(),[&](const auto& unit) {
            return unit.type==support[i];
        });
        require(found!=c.units.end() && result.support_units[i]==std::size_t(found-c.units.begin()),
                "support helper index differs from original ordered participant");
    }
    if(!support.empty()) {
        // Native roster order is independent of the original ascending-type
        // helper order. Reversing it must preserve all damage and RNG outputs
        // while mapping helpers to their new exact indices.
        auto reversed=c.units;
        std::reverse(reversed.begin(),reversed.end());
        g1::GameRandom replay_random(c.seed);
        const auto replay=g1::resolve_combat_exchange(c.rules(),reversed,
            c.units.size()-1-c.attacker,c.units.size()-1-c.defender,replay_random);
        require(replay.support_types==support,"roster order changed original helper type ordering");
        require(replay.support_units.size()==result.support_units.size(),"roster permutation lost helper identities");
        for(std::size_t i=0;i<replay.support_units.size();++i)
            require(replay.support_units[i]==c.units.size()-1-result.support_units[i],
                    "support helper identity did not follow its permuted roster index");
        std::reverse(reversed.begin(),reversed.end());
        require(unit_snapshot(reversed)==unit_snapshot(changed),"helper identity capture changed combat state");
        require(same_random(replay_random.state(),random.state()),"helper identity capture changed combat RNG");
        ++support_roster_cases;
    }
    require((result.counterattack ? 2:1)==strikes,"original strike ordering/count differs");
    require(result.destroyed.size()==deaths,"original destruction count differs");
    compare_strike(input,result.attack);
    if (result.counterattack) compare_strike(input,*result.counterattack);
    for (const auto& death:result.destroyed) {
        token(input,"D");
        require(death.x==number<std::uint8_t>(input),"original destruction x differs");
        require(death.y==number<std::uint8_t>(input),"original destruction y differs");
        require(death.type==number<std::uint8_t>(input),"original destruction type differs");
        require(number<std::uint8_t>(input)==0,"unexpected original death-log field3");
    }
    for (const auto& u:changed) {
        token(input,"V");
        for (const auto value:{static_cast<int>(u.stats.attack_low),static_cast<int>(u.stats.attack_high),
                              static_cast<int>(u.stats.defense),static_cast<int>(u.health),
                              static_cast<int>(u.active),u.support_bonus,u.timer_30_ms,u.preset_34_ms})
            require(value==number<std::int32_t>(input),"original final unit state differs");
    }
    token(input,"N");
    for (int i=0;i<8;++i)
        require(random.next_word()==number<std::uint32_t>(input),"original following random stream differs");
    return c;
}

void rejection_checks(const Case& good) {
    const auto reject=[](Case c, std::size_t attacker, std::size_t defender) {
        g1::GameRandom random(7);
        const auto before_units=unit_snapshot(c.units);
        const auto before_random=random.state();
        bool rejected=false;
        try { (void)g1::resolve_combat_exchange(c.rules(),c.units,attacker,defender,random); }
        catch (const std::invalid_argument&) { rejected=true; }
        require(rejected,"malformed/unsupported combat input was accepted");
        require(unit_snapshot(c.units)==before_units && same_random(random.state(),before_random),
                "rejected combat input changed unit/random state");
    };
    auto c=good; c.grid.tiles.pop_back(); reject(c,0,1);
    c=good; c.grid.width=0; reject(c,0,1);
    c=good; c.lookup[0]=255; reject(c,0,1);
    c=good; c.grid.tiles[0]=255; reject(c,0,1);
    c=good; c.types[0].forms[0].pop_back(); reject(c,0,1);
    c=good; c.units[1].type=c.units[0].type; reject(c,0,1);
    c=good; c.units[0].owner=2; reject(c,0,1);
    c=good; c.units[0].form=2; reject(c,0,1);
    c=good; c.units[0].x=255; reject(c,0,1);
    c=good; c.units[1].active=false; reject(c,0,1);
    c=good; c.units[1].owner=c.units[0].owner; reject(c,0,1);
    reject(good,0,0);
    reject(good,0,good.units.size());
    c=good;
    c.units.resize(2);
    c.units[0].x=3; c.units[0].y=3;
    c.units[1].x=4; c.units[1].y=3;
    for (std::uint8_t type=1;type<=7;++type) {
        g1::CombatUnitState helper;
        helper.type=type; helper.owner=0; helper.x=4; helper.y=2;
        for (auto& form:c.types[type].forms) { form[1]=1; form[2]=1; form[3]=50; }
        c.units.push_back(helper);
    }
    reject(c,0,1);
    g1::GameRandom random(47);
    for (int i=0;i<37;++i) (void)random.next_word();
    const auto saved=random.state();
    std::array<std::uint32_t,64> expected{};
    for (auto& v:expected) v=random.next_word();
    random.restore(saved);
    for (const auto v:expected) require(random.next_word()==v,"native RNG restored stream differs");
    const auto before=random.state();
    auto malformed=before; malformed.index=625;
    bool rejected=false;
    try { random.restore(malformed); } catch (const std::invalid_argument&) { rejected=true; }
    require(rejected && same_random(before,random.state()),"invalid RNG restore changed state");
}

} // namespace

int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::runtime_error("usage: g1_combat_tests ORIGINAL_SYNTHETIC_FIXTURE.txt");
        std::ifstream input(argv[1]);
        if (!input) throw std::runtime_error("cannot read combat original-function fixture");
        token(input,"G1_COMBAT_ORACLE_V1");
        std::string kind;
        std::size_t rng_cases{},stat_cases{},health_cases{},direct_cases{},battle_cases{};
        std::optional<Case> sample;
        while (input>>kind && kind!="END") {
            if (kind=="R") {
                g1::GameRandom random(number<std::uint32_t>(input));
                const auto count=number<std::uint32_t>(input);
                for (std::uint32_t i=0;i<count;++i)
                    require(random.next_word()==number<std::uint32_t>(input),"original RNG word differs");
                require(random.state().index==number<std::uint32_t>(input),"original RNG final index differs");
                for (const auto word:random.state().words)
                    require(word==number<std::uint32_t>(input),"original RNG complete state differs");
                ++rng_cases;
            } else if (kind=="S") {
                auto type=definitions()[0];
                for (std::size_t i=3;i<6;++i)
                    type.forms[0][i]=static_cast<std::uint8_t>(number<std::int8_t>(input));
                const auto stats=g1::combat_stats(type,0,number<std::uint8_t>(input));
                for (const auto value:{stats.attack_low,stats.attack_high,stats.defense})
                    require(value==number<std::int16_t>(input),"original scaled stat differs");
                ++stat_cases;
            } else if (kind=="H") {
                g1::CombatUnitState unit;
                unit.health=number<std::uint8_t>(input);
                unit.damage_immune=number<std::uint8_t>(input)!=0;
                unit.timer_30_ms=number<std::int32_t>(input);
                unit.preset_34_ms=number<std::int32_t>(input);
                const auto mode=number<std::uint8_t>(input);
                const auto value=number<std::int32_t>(input);
                if (mode==0) g1::set_combat_health(unit,value);
                else if (mode==1) g1::apply_combat_damage(unit,value);
                else throw std::runtime_error("unexpected health fixture mode");
                require(unit.health==number<std::uint8_t>(input),"original health write differs");
                require(unit.timer_30_ms==number<std::int32_t>(input),"original health timer differs");
                require(unit.preset_34_ms==number<std::int32_t>(input),"original health timer preset differs");
                ++health_cases;
            } else if (kind=="X") {
                g1::CombatUnitState attacker,defender;
                attacker.stats.attack_low=number<std::int16_t>(input);
                attacker.stats.attack_high=number<std::int16_t>(input);
                attacker.support_bonus=number<std::int32_t>(input);
                attacker.health=number<std::uint8_t>(input);
                defender.stats.defense=number<std::int16_t>(input);
                defender.type=number<std::uint8_t>(input);
                defender.health=number<std::uint8_t>(input);
                defender.damage_immune=number<std::uint8_t>(input)!=0;
                defender.timer_30_ms=number<std::int32_t>(input);
                defender.preset_34_ms=number<std::int32_t>(input);
                g1::GameRandom random(0);
                g1::GameRandom::State state;
                state.index=0;
                for (std::size_t i=0;i<4;++i) state.words[i]=number<std::uint32_t>(input);
                random.restore(state);
                const auto result=g1::resolve_combat_strike(attacker,defender,{0,0},random);
                require(result.rolled_damage==number<std::int32_t>(input),"original edge strike damage differs");
                require(result.health_before==number<std::uint8_t>(input),"original edge pre-hit health differs");
                require(result.health_after==number<std::uint8_t>(input),"original edge post-hit health differs");
                require(result.immune==number<std::uint8_t>(input),"original edge immunity differs");
                require(result.evaded==number<std::uint8_t>(input),"original edge evade differs");
                require(result.random_words==number<std::uint32_t>(input),"original edge random draw count differs");
                require(defender.timer_30_ms==number<std::int32_t>(input),"original edge timer differs");
                require(defender.preset_34_ms==number<std::int32_t>(input),"original edge timer preset differs");
                require(random.state().index==number<std::uint32_t>(input),"original edge RNG index differs");
                ++direct_cases;
            } else if (kind=="B") {
                auto c=battle_case(input);
                if (!sample) sample=std::move(c);
                ++battle_cases;
            } else throw std::runtime_error("unexpected combat fixture section "+kind);
        }
        require(kind=="END" && !(input>>kind),"combat fixture has truncation/trailing tokens");
        require(rng_cases==6 && stat_cases==36 && health_cases==64 && direct_cases==16 && battle_cases>0,
                "combat fixture coverage sections missing");
        rejection_checks(*sample);
        std::cout << "{\"rng_cases\":" << rng_cases << ",\"stat_cases\":" << stat_cases
                  << ",\"health_cases\":" << health_cases << ",\"battle_cases\":" << battle_cases
                  << ",\"direct_strike_cases\":" << direct_cases
                  << ",\"support_roster_cases\":" << support_roster_cases
                  << ",\"checks\":" << checks << ",\"all_passed\":true}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "combat validation failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
