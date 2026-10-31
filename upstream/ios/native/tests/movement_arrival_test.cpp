#include "g1/MovementArrival.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks{};
void check(bool value,const char* reason) {
    ++checks;
    if(!value)throw std::runtime_error(reason);
}
}

int main() {
    try {
        g1::ArrivalUnit actor{11,0,5,5,3,true};
        std::vector<g1::ArrivalUnit> units{{19,1,6,5,1,true},{2,1,4,5,1,true},actor};
        auto arrival=g1::choose_movement_arrival_facing(actor,units,1,1,0);
        check(arrival.writes_facing && arrival.facing==2 && arrival.nearest_type==2,
              "exact ties use original ascending-type slot order");
        check(arrival.target_types==std::optional<std::vector<std::uint8_t>>({2,19}),
              "candidate list retains original slot ordering");
        units={{19,1,5,4,1,true}};
        arrival=g1::choose_movement_arrival_facing(actor,units,1,1,0);
        check(arrival.facing==1 && arrival.nearest_type==19,
              "vertical-only nearby target uses original down-facing fallback");
        units={{2,1,5,3,1,true},{19,1,7,5,1,true}};
        arrival=g1::choose_movement_arrival_facing(actor,units,1,2,0);
        check(arrival.facing==3 && arrival.nearest_type==19,
              "equal total distance prefers smaller vertical distance");
        arrival=g1::choose_movement_arrival_facing(actor,units,1,1,0);
        check(arrival.facing==1 && arrival.target_types && arrival.target_types->empty(),
              "out-of-range targets leave a refreshed empty cache and down facing");
        arrival=g1::choose_movement_arrival_facing(actor,units,1,2,1);
        check(!arrival.writes_facing && arrival.facing==3 && !arrival.target_types,
              "field13 exactly1 preserves facing and target cache");
        arrival=g1::choose_movement_arrival_facing(actor,units,1,2,-1);
        check(arrival.writes_facing,"negative field13 does not take equality1 branch");
        units={{0,0,4,5,1,true},{1,1,4,5,1,false},actor};
        arrival=g1::choose_movement_arrival_facing(actor,units,0,10,0);
        check(arrival.target_types->empty(),"friendly, inactive and actor records are excluded");
        units={{19,1,5,5,1,true}};
        arrival=g1::choose_movement_arrival_facing(actor,units,0,0,0);
        check(arrival.target_types->size()==1 && arrival.facing==1,
              "inclusive zero-distance interval matches original collector");
        arrival=g1::choose_movement_arrival_facing(actor,units,3,1,0);
        check(arrival.target_types->empty(),"reversed distance interval finds no targets");
        bool threw{};
        try { g1::choose_movement_arrival_facing(actor,{units[0],units[0]},1,2,0); }
        catch(const std::invalid_argument&){threw=true;}
        check(threw,"duplicate original unit type rejects");
        std::cout<<"movement arrival contracts: "<<checks<<" passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
