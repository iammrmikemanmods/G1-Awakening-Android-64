#include "g1/UnitInteractions.hpp"
#include "g1/UpgradeAction.hpp"

#include <algorithm>
#include <stdexcept>

namespace g1 {
namespace {

int sbyte(std::uint8_t value) noexcept { return value<128 ? value : int(value)-256; }

void validate_definitions(const std::vector<UnitDefinition>& definitions,
                         const std::array<std::vector<std::uint8_t>,2>& slots) {
    if (definitions.size()!=23)
        throw std::invalid_argument("Unit interactions need all23 original definitions");
    for (std::size_t t=0;t<23;++t) {
        const auto& d=definitions[t];
        if (d.type!=t || d.forms.size()!=2 || d.forms[0].size()!=14 || d.forms[1].size()!=14)
            throw std::invalid_argument("Unit interaction definition shape differs from original");
    }
    for (const auto& owner:slots) {
        if (owner.size()>23) throw std::invalid_argument("Unit owner slots exceed original capacity");
        for (std::size_t i=0;i<owner.size();++i)
            if (owner[i]>=23 || (i && owner[i]<=owner[i-1]))
                throw std::invalid_argument("Unit owner slots must be ascending unique types");
    }
}

using TypeIndex=std::array<std::optional<std::size_t>,23>;
TypeIndex validate(const UnitInteractionView& v) {
    validate_definitions(v.definitions,v.owner_slot_types);
    if (v.units.size()>23 || v.units.size()!=v.movement_units.size())
        throw std::invalid_argument("Unit interaction roster shape is invalid");
    TypeIndex by_type{};
    for (std::size_t i=0;i<v.units.size();++i) {
        const auto& u=v.units[i];const auto& m=v.movement_units[i];
        if (u.type>=23 || by_type[u.type] || u.owner>1 || u.form>1 ||
            u.active!=m.active || u.form!=m.form || u.x!=m.tile_x || u.y!=m.tile_y)
            throw std::invalid_argument("Unit interaction state is invalid or unsynchronized");
        const auto& slots=v.owner_slot_types[u.owner];
        if (std::find(slots.begin(),slots.end(),u.type)==slots.end())
            throw std::invalid_argument("Unit identity is absent from its original owner slots");
        by_type[u.type]=i;
    }
    return by_type;
}

void actor_exists(const UnitInteractionView& v,std::size_t actor) {
    if (actor>=v.units.size()) throw std::invalid_argument("Unit interaction actor is absent");
}

bool either_one(const UnitDefinition& d,std::size_t field) noexcept {
    return d.forms[0][field]==1 || d.forms[1][field]==1;
}

std::optional<std::size_t> centered_occupant(const UnitInteractionView& v,
    const TypeIndex& index,WorldCoordinate at) {
    for (const auto& slots:v.owner_slot_types)
        for (const auto type:slots) if (index[type]) {
            const auto i=*index[type];const auto& u=v.units[i];const auto& m=v.movement_units[i];
            if (u.active && u.x==at.x && u.y==at.y && m.offset_x==0 && m.offset_y==0) return i;
        }
    return {};
}

void validate_board(const MovementBoard& board) {
    if (board.width==0 || board.height==0 || board.width>255 || board.height>255 ||
        board.cells.size()!=std::size_t(board.width)*board.height)
        throw std::invalid_argument("Production board shape is invalid");
    for (const auto& c:board.cells)
        if (c.active_owner < -1 || c.active_owner>1)
            throw std::invalid_argument("Production occupancy owner is invalid");
}

void validate_live_occupancy(const UnitInteractionView& v,const TypeIndex& index,
                           const MovementBoard& board) {
    validate_board(board);
    for (std::size_t y=0;y<board.height;++y)
        for (std::size_t x=0;x<board.width;++x) {
            const auto occupant=centered_occupant(v,index,
                {static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y)});
            const int expected=occupant ? v.units[*occupant].owner : -1;
            if (board.cells[y*board.width+x].active_owner!=expected)
                throw std::invalid_argument("Production board occupancy differs from live centered units");
        }
}

std::vector<std::uint8_t> candidates(const UnitInteractionView& v,const TypeIndex& index,
    const std::array<bool,23>& available,std::uint8_t owner,ProductionMode mode) {
    if (owner>1 || (mode!=ProductionMode::ordinary && mode!=ProductionMode::deploy))
        throw std::invalid_argument("Production owner or mode is invalid");
    std::vector<std::uint8_t> result;
    for (auto type:v.owner_slot_types[owner]) {
        if (!available[type] || (index[type] && v.units[*index[type]].active)) continue;
        if (either_one(v.definitions[type],8)==(mode==ProductionMode::deploy)) result.push_back(type);
    }
    return result;
}

ProductionEligibility eligibility(const UnitInteractionView& v,const TypeIndex& index,
    const MovementBoard& board,const std::array<bool,23>& available,
    std::size_t producer,ProductionMode mode,std::int32_t funds) {
    actor_exists(v,producer);
    const auto& u=v.units[producer];const auto& d=v.definitions[u.type];
    if (mode!=ProductionMode::ordinary && mode!=ProductionMode::deploy)
        throw std::invalid_argument("Production mode is invalid");
    if (!u.active) return ProductionEligibility::inactive;
    if (v.movement_units[producer].flag_2c!=0) return ProductionEligibility::action_used;
    if (mode==ProductionMode::ordinary) {
        const UpgradeWorldView upgrade{v.definitions,v.units,v.movement_units,v.owner_slot_types};
        if (upgrade_eligibility(upgrade,producer,funds)==UpgradeEligibility::allowed)
            return ProductionEligibility::upgrade_has_priority;
    }
    if (d.forms[u.form][mode==ProductionMode::ordinary ? 12U : 9U]!=1)
        return ProductionEligibility::wrong_form;
    if (candidates(v,index,available,u.owner,mode).empty()) return ProductionEligibility::no_candidates;
    if (!production_placement(board,{u.x,u.y},static_cast<std::int8_t>(sbyte(d.forms[u.form][6])))
        .range.has_unoccupied_destination) return ProductionEligibility::no_vacant_destination;
    return ProductionEligibility::allowed;
}

} // namespace

std::array<UnitStaticLinks,23> derive_unit_static_links(
    const std::vector<UnitDefinition>& definitions,
    const std::array<std::vector<std::uint8_t>,2>& slots) {
    validate_definitions(definitions,slots);
    std::array<UnitStaticLinks,23> result{};
    for (const auto& owner:slots) for (const auto carrier:owner) {
        if (!either_one(definitions[carrier],9)) continue;
        unsigned count=0;
        for (const auto passenger:owner) if (either_one(definitions[passenger],8)) {
            ++count;
            result[passenger].field_10_type=carrier;
            if (count==1) result[carrier].field_10_type=passenger;
            else result[carrier].field_14_type=passenger;
        }
    }
    return result;
}

std::vector<std::uint8_t> production_candidates(const UnitInteractionView& v,
    const std::array<bool,23>& available,std::uint8_t owner,ProductionMode mode) {
    return candidates(v,validate(v),available,owner,mode);
}

ProductionPlacement production_placement(const MovementBoard& board,WorldCoordinate origin,
                                         std::int8_t field6) {
    validate_board(board);
    if (origin.x>=board.width || origin.y>=board.height)
        throw std::invalid_argument("Production origin is outside the map");
    ProductionPlacement result;
    result.range={board.width,board.height,
        std::vector<std::int32_t>(board.cells.size(),movement_unvisited),false};
    const auto origin_index=std::size_t(origin.y)*board.width+origin.x;
    if (board.cells[origin_index].terrain_cost==99 && field6<=0) return result;
    result.range.remaining[origin_index]=1;
    constexpr int dx[4]={0,0,-1,1};
    constexpr int dy[4]={-1,1,0,0};
    for (std::size_t direction=0;direction<4;++direction) {
        const int x=int(origin.x)+dx[direction],y=int(origin.y)+dy[direction];
        if (x<0 || y<0 || x>=board.width || y>=board.height) continue;
        const auto cell=std::size_t(y)*board.width+std::size_t(x);
        if (board.cells[cell].terrain_cost==99) continue;
        result.range.remaining[cell]=0;
        result.reachable[direction]=true;
        result.vacant[direction]=board.cells[cell].active_owner==-1;
        result.range.has_unoccupied_destination|=result.vacant[direction];
        if (!result.initial_cursor) result.initial_cursor=WorldCoordinate{
            static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y)};
    }
    // 0x49b5a first tests four CLAMPED neighbors, then scans the whole field
    // including origin. An ordinary centered producer occupies origin, but
    // retaining the exact predicate also handles other explicitly seeded state.
    const int left=origin.x>0 ? int(origin.x)-1 : 0;
    const int up=origin.y>0 ? int(origin.y)-1 : 0;
    const int right=origin.x+1<board.width ? int(origin.x)+1 : int(board.width)-1;
    const int down=origin.y+1<board.height ? int(origin.y)+1 : int(board.height)-1;
    if (result.range.at(origin.x,up)>=0 || result.range.at(left,origin.y)>=0 ||
        result.range.at(right,origin.y)>=0 || result.range.at(origin.x,down)>=0) {
        for (std::size_t i=0;i<board.cells.size();++i)
            if (result.range.remaining[i]>=0 && board.cells[i].active_owner<0) {
                result.range.has_unoccupied_destination=true;
                break;
            }
    }
    return result;
}

ProductionEligibility production_eligibility(const UnitInteractionView& v,
    const MovementBoard& board,const std::array<bool,23>& available,
    std::size_t producer,ProductionMode mode,std::int32_t funds) {
    const auto index=validate(v);validate_live_occupancy(v,index,board);
    return eligibility(v,index,board,available,producer,mode,funds);
}

bool production_affordable(const UnitDefinition& definition,std::int32_t funds) {
    return funds>=unit_spawn_cost(definition);
}

ProductionResult produce_unit(const UnitInteractionView& v,const MovementBoard& board,
    const std::array<bool,23>& available,std::size_t producer,ProductionMode mode,
    std::uint8_t type,WorldCoordinate destination,std::int32_t funds,ProductionConfirmation confirmation) {
    const auto index=validate(v);validate_live_occupancy(v,index,board);
    if (confirmation!=ProductionConfirmation::funds_checked_button &&
        confirmation!=ProductionConfirmation::direct_type_list)
        throw std::invalid_argument("Production confirmation path is invalid");
    if (eligibility(v,index,board,available,producer,mode,funds)!=ProductionEligibility::allowed)
        throw std::invalid_argument("Human production command is not eligible");
    if (type>=23 || (confirmation==ProductionConfirmation::funds_checked_button &&
                    !production_affordable(v.definitions[type],funds)))
        throw std::invalid_argument("Production type or funds are invalid");
    const auto& p=v.units[producer];
    const auto allowed=candidates(v,index,available,p.owner,mode);
    if (std::find(allowed.begin(),allowed.end(),type)==allowed.end())
        throw std::invalid_argument("Production type is not a current candidate");
    const auto placement=production_placement(board,{p.x,p.y},
        static_cast<std::int8_t>(sbyte(v.definitions[p.type].forms[p.form][6])));
    if (destination.x>=board.width || destination.y>=board.height ||
        (destination.x==p.x && destination.y==p.y) ||
        placement.range.at(destination.x,destination.y)<0)
        throw std::invalid_argument("Production destination is not an adjacent reachable cell");
    const auto& cell=board.cells[std::size_t(destination.y)*board.width+destination.x];
    if (cell.active_owner!=-1) throw std::invalid_argument("Production destination is occupied");
    const auto& d=v.definitions[type];
    WorldUnit identity;identity.type=type;identity.owner=p.owner;identity.faction=d.faction();
    MovementUnitState previous_motion;previous_motion.active=false;
    CombatUnitState previous_combat;previous_combat.active=false;
    previous_combat.preset_34_ms=0; // Actual base constructor0x430c2.
    if (index[type]) {
        previous_motion=v.movement_units[*index[type]];
        previous_combat=v.units[*index[type]];
    }
    ProductionResult result;
    result.activation=activate_unit(d,identity,destination,cell.terrain_cost,cell.terrain_field2,
                                    previous_motion,previous_combat);
    result.activation.movement.flag_2c=1;
    result.producer_movement=v.movement_units[producer];
    result.producer_movement.flag_2c=1;
    result.funds_after=apply_spawn_funds_debit(funds,result.activation.signed_cost);
    result.return_cursor={p.x,p.y};
    return result;
}

FriendlyDestinationAdmission friendly_destination_admission(const UnitInteractionView& v,
    std::size_t actor,WorldCoordinate destination,bool moving,bool route) {
    const auto index=validate(v);actor_exists(v,actor);
    const auto& a=v.units[actor];
    if (!a.active) throw std::invalid_argument("Move admission actor is inactive");
    if (moving) return FriendlyDestinationAdmission::already_moving;
    const auto occupied=centered_occupant(v,index,destination);
    if (occupied) {
        if (*occupied==actor) return FriendlyDestinationAdmission::select_actor;
        const auto& target=v.units[*occupied];
        if (target.owner!=a.owner) return FriendlyDestinationAdmission::enemy_occupied;
        if (v.definitions[target.type].forms[target.form][9]==0)
            return FriendlyDestinationAdmission::occupant_cannot_carry;
        if (v.definitions[a.type].forms[a.form][8]==0)
            return FriendlyDestinationAdmission::actor_cannot_board;
    }
    return route ? FriendlyDestinationAdmission::move : FriendlyDestinationAdmission::missing_route;
}

TransportArrivalResult apply_transport_arrival(const UnitInteractionView& v,
    std::size_t actor,std::uint8_t count,std::int32_t slot) {
    const auto index=validate(v);actor_exists(v,actor);
    const auto& a=v.units[actor];
    if (!a.active) throw std::invalid_argument("Transport arrival actor is inactive");
    const auto& slots=v.owner_slot_types[a.owner];
    if (slot < -1 || (slot>=0 && static_cast<std::size_t>(slot)>=slots.size()))
        throw std::invalid_argument("Transport arrival upgrade slot is invalid");
    TransportArrivalResult result{a,v.movement_units[actor],count,slot,{},false,false};
    if (v.definitions[a.type].forms[a.form][8]==0) return result;
    for (const auto type:slots) if (index[type] && *index[type]!=actor) {
        const auto& carrier=v.units[*index[type]];
        if (carrier.active && v.definitions[type].forms[carrier.form][9]!=0 &&
            carrier.x==a.x && carrier.y==a.y) result.matching_carrier_types.push_back(type);
    }
    if (result.matching_carrier_types.empty()) return result;
    const UpgradeWorldView upgrade{v.definitions,v.units,v.movement_units,v.owner_slot_types};
    result.upgrade_slot_after=upgrade_slot_after_removal(upgrade,a.owner,a.type,slot);
    result.combat.active=false;
    result.movement.active=false;
    result.active_count_after=static_cast<std::uint8_t>(
        unsigned(count)-static_cast<unsigned>(result.matching_carrier_types.size()));
    result.clear_status_cache=true;
    result.deactivate_renderer=true;
    return result;
}

} // namespace g1
