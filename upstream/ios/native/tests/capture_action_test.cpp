#include "g1/CaptureAction.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks{};
void check(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}
}

int main() {
    try {
        std::vector<g1::WorldSite> coordinates{{{5,10},-1,{{{5,7},{10,7}}}}};
        auto network = g1::initial_capture_network(coordinates);
        check(network.sites[0].state == -1 && network.sites[0].item_states[0][0] == -1,
              "opcode7 ownership remains separate and initially neutral");
        network.counters_68 = {{17,29}};
        g1::CaptureActor actor{0,{5,7},true,0,0};
        auto result = g1::perform_capture(coordinates,network,actor,{{0,1}},{{0,1}});
        check(result.status == g1::CaptureStatus::form_cannot_capture && result.message_id == 1145,
              "vehicle form requests exact localized explanation");
        check(network.sites[0].item_states[0][0] == -1 && actor.flag_2c == 0,
              "rejected form preserves state");
        actor.form_field10 = 1;
        result = g1::perform_capture(coordinates,network,actor,{{0,1}},{{0,1}});
        check(result.status == g1::CaptureStatus::captured && result.target &&
              result.target->kind == g1::CaptureTargetKind::item && result.target->item == 0,
              "first objective item is the genuine target");
        check(network.sites[0].item_states[0][0] == 0 && network.sites[0].state == -1,
              "item capture does not fabricate site ownership");
        check(network.item_links[0][0] == std::vector<std::uint8_t>({1,0}) &&
              network.counters_68 == std::array<std::int32_t,2>{{17,29}},
              "capture updates links but preserves counters");
        check(actor.flag_2c == 1 && result.audio_5_12 && result.clear_selection_flags &&
              result.close_command_menu && !result.site_visual_faction,
              "item action completion and presentation requests");
        check(g1::perform_capture(coordinates,network,actor,{{0,1}},{{0,1}}).status ==
              g1::CaptureStatus::action_used, "capture cannot repeat a used action");
        actor.flag_2c = 0;
        check(g1::perform_capture(coordinates,network,actor,{{0,1}},{{0,1}}).status ==
              g1::CaptureStatus::unavailable, "already-owned sole target is unavailable");
        actor.position = {5,10};
        result = g1::perform_capture(coordinates,network,actor,{{1,0}},{{1,0}});
        check(result.target && result.target->kind == g1::CaptureTargetKind::site &&
              result.site_visual_faction == 1 && !result.clear_selection_flags && !result.audio_5_12,
              "site capture returns mapped faction and conditional audio");
        check(network.sites[0].state == 0 && network.sites[0].item_states[0][0] == 0,
              "site capture preserves separate item owner");
        g1::refresh_capture_network(network,{{0,1}},true,true);
        check(network.counters_68 == std::array<std::int32_t,2>{{5,1}},
              "one site and one same-faction item each add two to baseline one");
        network.sites[0].state = -1;
        network.sites[0].item_states = {{0,1,0,-1,1}};
        network.item_links[0] = {{7,7,7,7,7}};
        g1::refresh_capture_network(network,{{0,1}},true,true);
        check(network.counters_68 == std::array<std::int32_t,2>{{4,3}} &&
              network.item_links[0][0] == std::vector<std::uint8_t>({1,0,0,0,0}),
              "ordered faction conflict persists after first conflicting item");
        g1::refresh_capture_network(network,{{0,0}},true,true);
        check(network.counters_68 == std::array<std::int32_t,2>{{9,1}} &&
              network.item_links[0][0] == std::vector<std::uint8_t>({1,1,1,0,1}),
              "same faction across owners credits the first reference owner");
        network.item_links[0][0] = {9,8,7,6,5};
        g1::refresh_capture_network(network,{{-1,1}},true,false);
        check(network.counters_68 == std::array<std::int32_t,2>{{1,5}} &&
              network.item_links[0][0] == std::vector<std::uint8_t>({9,8,7,6,5}),
              "mapped-neutral owners are skipped and disabled link refresh preserves bytes");
        coordinates.push_back({{5,10},-1,{}});
        auto target = g1::choose_capture_target(coordinates,{5,10});
        check(target && target->site == 1 && target->kind == g1::CaptureTargetKind::site,
              "last duplicate site is the original fallback");
        coordinates[1].coordinate_groups = {{{5,10}}};
        target = g1::choose_capture_target(coordinates,{5,10});
        check(target && target->site == 1 && target->kind == g1::CaptureTargetKind::item,
              "an item match takes precedence over a preceding site match");
        coordinates[0].coordinate_groups = {{{5,10}}};
        target = g1::choose_capture_target(coordinates,{5,10});
        check(target && target->site == 0 && target->kind == g1::CaptureTargetKind::item,
              "first duplicate item wins");
        coordinates = {{{200,1},-1,{{{200,1}}}}};
        check(!g1::choose_capture_target(coordinates,{200,1}),
              "signed coordinate bytes do not match unsigned unit cells above127");
        auto invalid = network;
        invalid.sites[0].item_states[0][2] = 2;
        bool threw{};
        try { g1::refresh_capture_network(invalid,{{0,1}},true,true); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw && invalid.counters_68 == network.counters_68 &&
              invalid.item_links == network.item_links, "invalid owner rejects before writes");
        std::cout << "capture action contracts: " << checks << " passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
