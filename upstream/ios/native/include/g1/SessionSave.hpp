#pragma once

#include "g1/GameSession.hpp"
#include "g1/SaveStore.hpp"

namespace g1 {

// Native mid-mission checkpoint. This is a versioned value representation,
// never an original ARM memory image. Only completed action boundaries at an
// active opcode12 objective are eligible. A busy action keeps the previous
// durable checkpoint; restoring does not fabricate its completion.
struct SessionCheckpoint {
    std::uint64_t archive_bytes{};
    std::uint32_t archive_checksum{};
    WorldBootstrap world;
    CampaignContext campaign;
    MissionGameplayCheckpoint mission;
    std::vector<MovementUnitState> movement;
    std::vector<CombatUnitState> combat;
    std::vector<std::uint8_t> animation_pending;
    CaptureNetwork captures;
    TurnState turn;
    std::array<std::int32_t,2> upgrade_slots{{-1,-1}};
    std::optional<std::size_t> selected;
    GameRandom::State random;
    bool config_damage_immunity{};
    std::int32_t death_counter_278{},death_counter_27c{};
    std::vector<std::array<std::int32_t,4>> death_log;
    // Tables/layout are immutable validation context, not serialized payload.
    // The save stores their checksum and the complete mutable pool state.
    std::optional<TerrainRuntime> terrain;
};

constexpr std::size_t max_session_save_size=2U*1024U*1024U;

// Codecs store mutable state plus resource IDs. Immutable grid, terrain rules,
// definitions and script commands are decoded afresh from the matching archive.
// Length/version/checksum/shape validation precedes installation. No clock,
// random draw, filesystem write or objective advancement occurs in decoding.
Bytes encode_session_checkpoint(const SessionCheckpoint&);
SessionCheckpoint decode_session_checkpoint(const Archive&,const Bytes&,
    const TerrainTables* terrain_tables=nullptr);

} // namespace g1
