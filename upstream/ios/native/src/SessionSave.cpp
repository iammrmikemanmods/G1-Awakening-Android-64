#include "g1/SessionSave.hpp"

#include <algorithm>
#include <limits>

namespace g1 {
namespace {
constexpr std::array<std::uint8_t,8> magic{{'G','1','N','S','E','S','S',0}};
void require(bool value,const char* message) { if(!value)throw FormatError(message); }
std::int32_t signed32(std::uint32_t value) noexcept {
    return value<=0x7fffffffU ? static_cast<std::int32_t>(value) :
        static_cast<std::int32_t>(static_cast<std::int64_t>(value)-0x100000000LL);
}
std::int16_t signed16(std::uint16_t value) noexcept {
    return static_cast<std::int16_t>(value<0x8000U ? int(value) : int(value)-65536);
}
std::int8_t signed8(std::uint8_t value) noexcept {
    return static_cast<std::int8_t>(value<128U ? int(value) : int(value)-256);
}
struct Writer {
    Bytes bytes;
    void byte(std::uint8_t value) { bytes.push_back(value); }
    void boolean(bool value) { byte(value?1:0); }
    void half(std::uint16_t value) { byte(static_cast<std::uint8_t>(value));byte(static_cast<std::uint8_t>(value>>8U)); }
    void word(std::uint32_t value) { for(unsigned i=0;i<4;++i)byte(static_cast<std::uint8_t>(value>>(8U*i))); }
    void integer(std::int32_t value) { word(static_cast<std::uint32_t>(value)); }
    void wide(std::uint64_t value) { word(static_cast<std::uint32_t>(value));word(static_cast<std::uint32_t>(value>>32U)); }
    void count8(std::size_t value) { require(value<=255,"native checkpoint count exceeds byte storage");byte(static_cast<std::uint8_t>(value)); }
    void count16(std::size_t value) { require(value<=65535,"native checkpoint count exceeds short storage");half(static_cast<std::uint16_t>(value)); }
};
struct Reader {
    const Bytes& bytes;std::size_t cursor{},end{};
    std::uint8_t byte() { require(cursor<end,"native checkpoint is truncated");return bytes[cursor++]; }
    bool boolean() { const auto value=byte();require(value<2,"native checkpoint boolean is invalid");return value!=0; }
    std::uint16_t half() { const auto lo=byte();const auto hi=byte();return static_cast<std::uint16_t>(lo|(std::uint16_t(hi)<<8U)); }
    std::uint32_t word() { std::uint32_t value{};for(unsigned i=0;i<4;++i)value|=std::uint32_t(byte())<<(8U*i);return value; }
    std::int32_t integer() { return signed32(word()); }
    std::uint64_t wide() { const auto lo=word();const auto hi=word();return std::uint64_t(lo)|(std::uint64_t(hi)<<32U); }
};
Bytes resource(const Archive& archive,std::uint32_t id) {
    const auto found=archive.find(id,0xf4e02223U);
    require(found.size()==1,"native checkpoint resource is missing or ambiguous");
    return archive.read(found.front());
}
void load_immutable_world(const Archive& archive,SessionCheckpoint& saved) {
    require(saved.archive_bytes==archive.byte_size() && saved.archive_checksum==archive.content_checksum(),
            "native checkpoint belongs to different game data");
    auto& world=saved.world;
    require(world.mission_resource_id==mission_resource_for_campaign(saved.campaign.group,saved.campaign.mission_index),
            "native checkpoint campaign resource does not match its index");
    require(world.style<=2 && world.lookup_resource_id==mission_lookup_resource(world.style),
            "native checkpoint terrain style or lookup is invalid");
    bool known_grid=false;
    for(std::size_t i=0;i<22;++i)known_grid=known_grid || world.grid_resource_id==mission_grid_resource(i);
    require(known_grid,"native checkpoint grid resource is outside the original map table");
    world.grid=decode_terrain_grid(resource(archive,world.grid_resource_id));
    world.effective_lookup=apply_mission_style(decode_terrain_lookup(resource(archive,world.lookup_resource_id)),world.style);
    world.terrain_rules=decode_terrain_rules(resource(archive,306));
    world.unit_definitions.clear();
    for(std::uint8_t type=0;type<23;++type) {
        const auto id=unit_definition_resource(type);
        world.unit_definitions.push_back(decode_unit_definition(resource(archive,id),type,id));
    }
    world.commands=MissionStream(resource(archive,world.mission_resource_id)).commands();
    world.player_factions=saved.campaign.player_factions;
    world.next_command_index=saved.mission.command_index;
    world.stop=BootstrapStop::execution_required;
    world.stop_detail="restored native gameplay checkpoint";
    world.initialization_effects.clear();
    if(saved.terrain)saved.terrain=restore_terrain_runtime(archive,world,
        saved.terrain->tables,saved.terrain->state,saved.terrain->sprite_state);
}
void validate(const SessionCheckpoint& saved) {
    const auto& w=saved.world;const auto& c=saved.campaign;
    require(saved.archive_bytes>0 && saved.archive_bytes<=Archive::max_archive_size,"native checkpoint archive size is invalid");
    require(w.mission_resource_id==mission_resource_for_campaign(c.group,c.mission_index),"native checkpoint campaign mismatch");
    require(w.player_factions==c.player_factions && c.player_factions[0]<2 && c.player_factions[1]<2 &&
            c.player_factions[0]!=c.player_factions[1],"native checkpoint player factions are invalid");
    require(w.unit_definitions.size()==23 && w.grid.width>0 && w.grid.height>0 &&
            w.grid.width<=255 && w.grid.height<=255 && w.grid.tiles.size()==std::size_t(w.grid.width)*w.grid.height,
            "native checkpoint immutable world data is incomplete");
    require(saved.mission.command_index<w.commands.size() && w.commands[saved.mission.command_index].opcode==12 &&
            saved.mission.original_phase>=0 && saved.mission.original_phase<=99,"native checkpoint is not an objective boundary");
    (void)decode_mission_objective(w.commands[saved.mission.command_index]);
    require(saved.turn.current_player<2 && saved.turn.controllers==c.player_controllers &&
            saved.turn.ai_unit_cursor>=0 && saved.turn.ai_unit_cursor<=23,"native checkpoint turn state is invalid");
    require(saved.random.index<=624,"native checkpoint random cursor is invalid");
    require(w.units.size()<=23 && saved.movement.size()==w.units.size() && saved.combat.size()==w.units.size() &&
            saved.animation_pending.size()==w.units.size(),"native checkpoint unit vectors are inconsistent");
    std::array<bool,23> seen{};bool type12=false;
    for(std::size_t i=0;i<w.units.size();++i) {
        const auto& u=w.units[i];const auto& m=saved.movement[i];const auto& b=saved.combat[i];
        require(u.type<23 && !seen[u.type] && u.owner<2 && u.faction==c.player_factions[u.owner],"native checkpoint unit identity is invalid");
        seen[u.type]=true;
        require(u.form<w.unit_definitions[u.type].forms.size() && m.form==u.form && b.form==u.form &&
                u.type==b.type && u.owner==b.owner && u.faction==b.faction &&
                u.active==m.active && u.active==b.active && u.x==m.tile_x && u.x==b.x &&
                u.y==m.tile_y && u.y==b.y && u.spent_movement==m.spent_budget &&
                b.flag_38==m.flag_38 && b.timer_30_ms==m.timer_ms,"native checkpoint unit mirrors disagree");
        require(m.facing<4 && m.anchor_facing<4 && (!u.active ||
                (u.x<w.grid.width && u.y<w.grid.height && m.offset_x==0 && m.offset_y==0 && saved.animation_pending[i]==0)),
                "native checkpoint contains an unfinished or invalid active unit");
        type12=type12 || (u.type==12 && u.active);
    }
    require(type12==c.type_12_active,"native checkpoint special-unit activity is inconsistent");
    for(const auto slot:saved.upgrade_slots)require(slot>=-1 && slot<23,"native checkpoint upgrade cursor is invalid");
    if(saved.selected) {
        const auto i=*saved.selected;
        require(i<w.units.size() && w.units[i].active && w.units[i].owner==saved.turn.current_player &&
                saved.turn.controllers[saved.turn.current_player]!=1,"native checkpoint selection is invalid");
    }
    require(w.sites.size()<=255 && saved.captures.sites.size()==w.sites.size() &&
            saved.captures.item_links.size()==w.sites.size(),"native checkpoint site vectors disagree");
    const auto owner=[](std::int8_t x){return x>=-1 && x<=1;};
    for(std::size_t i=0;i<w.sites.size();++i) {
        const auto& site=w.sites[i];const auto& state=saved.captures.sites[i];const auto& links=saved.captures.item_links[i];
        require(site.position.x<w.grid.width && site.position.y<w.grid.height && owner(site.state) && owner(state.state) &&
                site.coordinate_groups.size()<=255 && state.item_states.size()==site.coordinate_groups.size() &&
                links.size()==site.coordinate_groups.size(),"native checkpoint site shape is invalid");
        for(std::size_t g=0;g<site.coordinate_groups.size();++g) {
            const auto& positions=site.coordinate_groups[g];
            require(positions.size()<=255 && state.item_states[g].size()==positions.size() && links[g].size()==positions.size(),
                    "native checkpoint site item vectors disagree");
            for(std::size_t j=0;j<positions.size();++j)
                require(positions[j].x<w.grid.width && positions[j].y<w.grid.height && owner(state.item_states[g][j]) &&
                        links[g][j]<=1,"native checkpoint site item state is invalid");
        }
    }
    require(w.site_points.size()<=65535 && w.cost99_regions.size()<=65535 && saved.death_log.size()<=65535,
            "native checkpoint extended count is invalid");
    for(const auto& point:w.site_points)
        require(point.site_index<w.sites.size() && point.point.x>=0 && point.point.y>=0 &&
                point.point.x<w.grid.width && point.point.y<w.grid.height,"native checkpoint site point is invalid");
    if(saved.terrain)validate_terrain_runtime(*saved.terrain,w.style);
}
void write_movement(Writer& out,const MovementUnitState& m) {
    out.boolean(m.active);out.byte(m.form);out.byte(m.tile_x);out.byte(m.tile_y);
    out.integer(m.offset_x);out.integer(m.offset_y);out.byte(m.facing);out.byte(m.spent_budget);out.integer(m.timer_ms);
    for(auto value:{m.anchor_x,m.anchor_y,m.anchor_facing,m.flag_2c,m.flag_2d,m.flag_2f,m.flag_38,m.flag_39,m.flag_3a})out.byte(value);
}
MovementUnitState read_movement(Reader& in) {
    MovementUnitState m;m.active=in.boolean();m.form=in.byte();m.tile_x=in.byte();m.tile_y=in.byte();
    m.offset_x=in.integer();m.offset_y=in.integer();m.facing=in.byte();m.spent_budget=in.byte();m.timer_ms=in.integer();
    m.anchor_x=in.byte();m.anchor_y=in.byte();m.anchor_facing=in.byte();m.flag_2c=in.byte();m.flag_2d=in.byte();
    m.flag_2f=in.byte();m.flag_38=in.byte();m.flag_39=in.byte();m.flag_3a=in.byte();return m;
}
void write_combat(Writer& out,const CombatUnitState& b) {
    for(auto value:{b.type,b.owner,b.faction,b.form,b.x,b.y,b.level,b.health})out.byte(value);
    out.boolean(b.active);out.boolean(b.damage_immune);out.byte(b.flag_38);
    out.half(static_cast<std::uint16_t>(b.stats.attack_low));out.half(static_cast<std::uint16_t>(b.stats.attack_high));
    out.half(static_cast<std::uint16_t>(b.stats.defense));out.integer(b.support_bonus);out.integer(b.timer_30_ms);out.integer(b.preset_34_ms);
}
CombatUnitState read_combat(Reader& in) {
    CombatUnitState b;b.type=in.byte();b.owner=in.byte();b.faction=in.byte();b.form=in.byte();b.x=in.byte();b.y=in.byte();
    b.level=in.byte();b.health=in.byte();b.active=in.boolean();b.damage_immune=in.boolean();b.flag_38=in.byte();
    b.stats={signed16(in.half()),signed16(in.half()),signed16(in.half())};
    b.support_bonus=in.integer();b.timer_30_ms=in.integer();b.preset_34_ms=in.integer();return b;
}
} // namespace

Bytes encode_session_checkpoint(const SessionCheckpoint& saved) {
    validate(saved);Writer out;out.bytes.assign(magic.begin(),magic.end());out.word(3);out.word(0);
    out.wide(saved.archive_bytes);out.word(saved.archive_checksum);
    const auto& c=saved.campaign;const auto& w=saved.world;
    out.byte(c.group);out.integer(c.mission_index);
    for(auto value:c.player_factions)out.byte(value);
    for(auto value:c.player_controllers)out.byte(static_cast<std::uint8_t>(value));
    for(auto value:c.active_unit_counts)out.byte(value);
    out.boolean(c.type_12_active);
    out.word(static_cast<std::uint32_t>(saved.mission.command_index));out.integer(saved.mission.original_phase);
    out.boolean(saved.mission.phase_flag_225);out.boolean(saved.mission.alternate_dialogue_variant);
    out.integer(saved.mission.transition_value);out.integer(saved.mission.remaining_delay_ms);
    out.word(w.mission_resource_id);out.word(w.grid_resource_id);out.word(w.lookup_resource_id);out.byte(w.style);
    out.boolean(w.title_text_index.has_value());if(w.title_text_index)out.half(*w.title_text_index);
    out.half(w.last_phase_opcode);out.boolean(w.flag_225);out.boolean(w.flag_227);
    out.boolean(w.camera.has_value());if(w.camera){out.half(static_cast<std::uint16_t>(w.camera->x));out.half(static_cast<std::uint16_t>(w.camera->y));out.boolean(w.camera->immediate);}
    for(auto value:w.available_types)out.boolean(value);
    out.count8(w.units.size());
    for(std::size_t i=0;i<w.units.size();++i) {
        const auto& u=w.units[i];for(auto value:{u.type,u.faction,u.owner,u.form,u.x,u.y,u.spent_movement})out.byte(value);
        out.boolean(u.active);out.word(static_cast<std::uint32_t>(u.placement_command_index));
        out.byte(u.spawn.health);out.boolean(u.spawn.flag_18);out.boolean(u.spawn.damage_immune);
        write_movement(out,saved.movement[i]);write_combat(out,saved.combat[i]);out.byte(saved.animation_pending[i]);
    }
    out.count8(w.sites.size());
    for(std::size_t i=0;i<w.sites.size();++i) {
        const auto& s=w.sites[i];const auto& state=saved.captures.sites[i];
        out.byte(s.position.x);out.byte(s.position.y);out.byte(static_cast<std::uint8_t>(s.state));out.byte(static_cast<std::uint8_t>(state.state));
        out.count8(s.coordinate_groups.size());
        for(std::size_t g=0;g<s.coordinate_groups.size();++g) {
            out.count8(s.coordinate_groups[g].size());
            for(std::size_t j=0;j<s.coordinate_groups[g].size();++j) {
                const auto p=s.coordinate_groups[g][j];out.byte(p.x);out.byte(p.y);
                out.byte(static_cast<std::uint8_t>(state.item_states[g][j]));out.byte(saved.captures.item_links[i][g][j]);
            }
        }
    }
    for(auto value:saved.captures.counters_68)out.integer(value);
    out.count16(w.site_points.size());for(const auto& p:w.site_points){out.integer(p.point.x);out.integer(p.point.y);out.integer(p.point.state);out.count8(p.site_index);}
    out.count16(w.cost99_regions.size());for(const auto& r:w.cost99_regions){out.integer(r.x);out.integer(r.y);out.integer(r.width);out.integer(r.height);}
    for(const auto& marker:{w.marker_254,w.marker_25c})for(auto value:marker)out.half(static_cast<std::uint16_t>(value));
    out.byte(saved.turn.current_player);for(auto value:saved.turn.funds)out.integer(value);
    out.integer(saved.turn.human_turn_counter_274);out.integer(saved.turn.ai_unit_cursor);out.boolean(saved.turn.income_notice_pending);
    for(auto value:saved.upgrade_slots)out.integer(value);
    out.boolean(saved.selected.has_value());if(saved.selected)out.count8(*saved.selected);
    out.boolean(saved.config_damage_immunity);out.integer(saved.death_counter_278);out.integer(saved.death_counter_27c);
    out.count16(saved.death_log.size());for(const auto& row:saved.death_log)for(auto value:row)out.integer(value);
    out.word(saved.random.index);out.wide(saved.random.draws);for(auto value:saved.random.words)out.word(value);
    out.boolean(saved.terrain.has_value());
    if(saved.terrain) {
        out.word(terrain_tables_checksum(saved.terrain->tables));out.boolean(saved.terrain->state.has_value());
        if(saved.terrain->state) {
            const auto& state=*saved.terrain->state;
            out.word(state.animation.accumulated_ms);out.byte(state.animation.phase);
            out.count16(state.candidate_records.size());for(const auto value:state.candidate_records)out.byte(value);
            out.count16(state.slots.size());for(const auto& slot:state.slots){out.byte(slot.phase);out.integer(slot.candidate_byte_offset);}
        }
        out.boolean(saved.terrain->sprite_state.has_value());
        if(saved.terrain->sprite_state) {
            const auto& state=*saved.terrain->sprite_state;
            out.word(state.accumulated_ms);out.byte(state.phase);
            out.count16(state.candidate_records.size());for(const auto value:state.candidate_records)out.byte(value);
            out.count16(state.slot_phases.size());for(const auto value:state.slot_phases)out.byte(value);
            out.count16(state.sprites.size());
            for(const auto& sprite:state.sprites) {
                out.byte(sprite.animation_index);out.word(static_cast<std::uint32_t>(sprite.playback.step_index));
                out.integer(sprite.playback.elapsed_ms);out.integer(sprite.playback.repeat_count);
                out.boolean(sprite.playback.reverse);out.boolean(sprite.playback.completed);
                out.boolean(sprite.visible);out.byte(static_cast<std::uint8_t>(sprite.parent));
                out.word(sprite.attachment_order);out.boolean(sprite.original_position.has_value());
                if(sprite.original_position)for(const auto coordinate:*sprite.original_position)out.integer(coordinate);
            }
        }
    }
    require(out.bytes.size()+4<=max_session_save_size,"native checkpoint exceeds its size limit");
    const auto size=static_cast<std::uint32_t>(out.bytes.size()+4);
    for(unsigned i=0;i<4;++i)out.bytes[12+i]=static_cast<std::uint8_t>(size>>(8U*i));
    out.word(adler32_bytes(out.bytes));return out.bytes;
}

SessionCheckpoint decode_session_checkpoint(const Archive& archive,const Bytes& bytes,const TerrainTables* terrain_tables) {
    require(bytes.size()>=32 && bytes.size()<=max_session_save_size && std::equal(magic.begin(),magic.end(),bytes.begin()),
            "native checkpoint signature or size is invalid");
    Reader tail{bytes,bytes.size()-4,bytes.size()};const auto checksum=tail.word();
    require(adler32_bytes(Bytes(bytes.begin(),bytes.end()-4))==checksum,"native checkpoint checksum failed");
    Reader in{bytes,8,bytes.size()-4};
    require(in.word()==3,"native checkpoint version is unsupported");
    require(in.word()==bytes.size(),"native checkpoint declared size is invalid");
    SessionCheckpoint saved;saved.archive_bytes=in.wide();saved.archive_checksum=in.word();
    require(saved.archive_bytes==archive.byte_size() && saved.archive_checksum==archive.content_checksum(),
            "native checkpoint belongs to different game data");
    auto& c=saved.campaign;auto& w=saved.world;c.group=in.byte();c.mission_index=in.integer();
    for(auto& value:c.player_factions)value=in.byte();
    for(auto& value:c.player_controllers)value=signed8(in.byte());
    for(auto& value:c.active_unit_counts)value=in.byte();
    c.type_12_active=in.boolean();
    saved.mission.command_index=in.word();saved.mission.original_phase=in.integer();
    saved.mission.phase_flag_225=in.boolean();saved.mission.alternate_dialogue_variant=in.boolean();
    saved.mission.transition_value=in.integer();saved.mission.remaining_delay_ms=in.integer();
    w.mission_resource_id=in.word();w.grid_resource_id=in.word();w.lookup_resource_id=in.word();w.style=in.byte();
    if(in.boolean())w.title_text_index=in.half();
    w.last_phase_opcode=in.half();w.flag_225=in.boolean();w.flag_227=in.boolean();
    if(in.boolean())w.camera=WorldCameraRequest{signed16(in.half()),signed16(in.half()),in.boolean()};
    for(auto& value:w.available_types)value=in.boolean();
    const auto units=in.byte();require(units<=23,"native checkpoint roster is too large");
    for(std::size_t i=0;i<units;++i) {
        WorldUnit u;u.type=in.byte();u.faction=in.byte();u.owner=in.byte();u.form=in.byte();u.x=in.byte();u.y=in.byte();
        u.spent_movement=in.byte();u.active=in.boolean();u.placement_command_index=in.word();
        u.spawn.health=in.byte();u.spawn.flag_18=in.boolean();u.spawn.damage_immune=in.boolean();w.units.push_back(u);
        saved.movement.push_back(read_movement(in));saved.combat.push_back(read_combat(in));saved.animation_pending.push_back(in.byte());
    }
    const auto sites=in.byte();
    for(std::size_t i=0;i<sites;++i) {
        WorldSite site;site.position={in.byte(),in.byte()};site.state=signed8(in.byte());
        MissionSiteState state;state.state=signed8(in.byte());std::vector<std::vector<std::uint8_t>> links;
        const auto groups=in.byte();
        for(std::size_t g=0;g<groups;++g) {
            std::vector<WorldCoordinate> positions;std::vector<std::int8_t> owners;std::vector<std::uint8_t> row;
            const auto count=in.byte();
            for(std::size_t j=0;j<count;++j){positions.push_back({in.byte(),in.byte()});owners.push_back(signed8(in.byte()));row.push_back(in.byte());}
            site.coordinate_groups.push_back(std::move(positions));state.item_states.push_back(std::move(owners));links.push_back(std::move(row));
        }
        w.sites.push_back(std::move(site));saved.captures.sites.push_back(std::move(state));saved.captures.item_links.push_back(std::move(links));
    }
    for(auto& value:saved.captures.counters_68)value=in.integer();
    const auto points=in.half();for(std::size_t i=0;i<points;++i){WorldSitePoint p;p.point={in.integer(),in.integer(),in.integer()};p.site_index=in.byte();w.site_points.push_back(p);}
    const auto regions=in.half();for(std::size_t i=0;i<regions;++i)w.cost99_regions.push_back({in.integer(),in.integer(),in.integer(),in.integer()});
    for(auto& value:w.marker_254)value=signed16(in.half());
    for(auto& value:w.marker_25c)value=signed16(in.half());
    saved.turn.current_player=in.byte();saved.turn.controllers=c.player_controllers;for(auto& value:saved.turn.funds)value=in.integer();
    saved.turn.human_turn_counter_274=in.integer();saved.turn.ai_unit_cursor=in.integer();saved.turn.income_notice_pending=in.boolean();
    for(auto& value:saved.upgrade_slots)value=in.integer();
    if(in.boolean())saved.selected=in.byte();
    saved.config_damage_immunity=in.boolean();saved.death_counter_278=in.integer();saved.death_counter_27c=in.integer();
    const auto deaths=in.half();for(std::size_t i=0;i<deaths;++i){std::array<std::int32_t,4> row{};for(auto& value:row)value=in.integer();saved.death_log.push_back(row);}
    saved.random.index=in.word();saved.random.draws=in.wide();for(auto& value:saved.random.words)value=in.word();
    if(in.boolean()) {
        const auto checksum=in.word();
        require(terrain_tables && checksum==terrain_tables_checksum(*terrain_tables),
            "native checkpoint requires its matching terrain-table data");
        TerrainRuntime runtime;runtime.tables=*terrain_tables;
        if(in.boolean()) {
            TerrainDecorationState state;state.animation.accumulated_ms=in.word();state.animation.phase=in.byte();
            const auto records=in.half();require(records<=2667,"native checkpoint terrain pool is too large");
            state.candidate_records.reserve(records);for(std::size_t i=0;i<records;++i)state.candidate_records.push_back(in.byte());
            const auto slots=in.half();require(slots<127,"native checkpoint terrain slot count is invalid");
            for(std::size_t i=0;i<slots;++i)state.slots.push_back({in.byte(),in.integer()});
            runtime.state=std::move(state);
        }
        if(in.boolean()) {
            TerrainSpriteDecorationState state;state.accumulated_ms=in.word();state.phase=in.byte();
            const auto records=in.half();require(records<=2667,"native checkpoint terrain sprite pool is too large");
            state.candidate_records.reserve(records);for(std::size_t i=0;i<records;++i)state.candidate_records.push_back(in.byte());
            const auto slots=in.half();require(slots<127,"native checkpoint terrain sprite slot count is invalid");
            for(std::size_t i=0;i<slots;++i)state.slot_phases.push_back(in.byte());
            const auto sprites=in.half();require(sprites<=2048,"native checkpoint terrain sprite count is invalid");
            state.sprites.reserve(sprites);
            for(std::size_t i=0;i<sprites;++i) {
                TerrainSpriteDecoration sprite;sprite.animation_index=in.byte();sprite.playback.step_index=in.word();
                sprite.playback.elapsed_ms=in.integer();sprite.playback.repeat_count=in.integer();
                sprite.playback.reverse=in.boolean();sprite.playback.completed=in.boolean();
                sprite.visible=in.boolean();sprite.parent=static_cast<TerrainSpriteParent>(in.byte());
                sprite.attachment_order=in.word();
                if(in.boolean())sprite.original_position=std::array<std::int32_t,3>{{in.integer(),in.integer(),in.integer()}};
                state.sprites.push_back(std::move(sprite));
            }
            runtime.sprite_state=std::move(state);
        }
        saved.terrain=std::move(runtime);
    }
    require(in.cursor==in.end,"native checkpoint has trailing payload");
    load_immutable_world(archive,saved);validate(saved);return saved;
}

bool GameSession::checkpoint_available() const noexcept {
    const auto& mission=mission_.state();
    if(!archive_ || moving_ || battle_ || ai_ || focus_ || world_presentation_ || original_transform_gate_ ||
       mission.interaction_mode!=0 || mission.stop!=MissionStop::none || mission.opcode!=12 ||
       (mission.gate!=MissionGate::objective && mission.gate!=MissionGate::captured_site_item))return false;
    for(std::size_t i=0;i<units_.size();++i)
        if(units_[i].active && (units_[i].offset_x!=0 || units_[i].offset_y!=0 ||
           animation_pending_[i]!=0 || transform_waiting_[i]))return false;
    return true;
}

SessionCheckpoint GameSession::make_checkpoint() const {
    if(!checkpoint_available())throw FormatError("native session has no completed checkpoint boundary");
    SessionCheckpoint saved;saved.archive_bytes=archive_->byte_size();saved.archive_checksum=archive_->content_checksum();
    saved.world=world_;saved.campaign=campaign_context();saved.mission=mission_.gameplay_checkpoint();
    saved.world.initialization_effects.clear();saved.world.next_command_index=saved.mission.command_index;
    saved.world.stop=BootstrapStop::execution_required;saved.world.stop_detail="native gameplay checkpoint";
    saved.movement=units_;saved.combat=combat_;saved.animation_pending=animation_pending_;saved.captures=captures_;
    saved.turn=turn_;saved.upgrade_slots=upgrade_slots_;saved.selected=selected_;saved.random=random_.state();
    saved.config_damage_immunity=config_damage_immunity_;saved.death_counter_278=death_counter_278_;
    saved.death_counter_27c=death_counter_27c_;saved.death_log=death_log_;saved.terrain=terrain_;validate(saved);return saved;
}

std::unique_ptr<GameSession> GameSession::restore_checkpoint(const Archive& archive,const SessionCheckpoint& input) {
    auto saved=input;load_immutable_world(archive,saved);validate(saved);
    GameRandom random(0);random.restore(saved.random);
    auto result=std::unique_ptr<GameSession>(new GameSession(saved.world,std::move(random),saved.campaign,{}));
    result->archive_=&archive;result->mission_.restore_gameplay_checkpoint(saved.mission);
    result->units_=saved.movement;result->combat_=saved.combat;result->animation_pending_=saved.animation_pending;
    result->captures_=saved.captures;result->turn_=saved.turn;result->upgrade_slots_=saved.upgrade_slots;
    result->selected_=saved.selected;result->active_counts_=saved.campaign.active_unit_counts;
    result->config_damage_immunity_=saved.config_damage_immunity;result->death_counter_278_=saved.death_counter_278;
    result->death_counter_27c_=saved.death_counter_27c;result->death_log_=saved.death_log;
    result->terrain_=std::move(saved.terrain);
    // Rebuild visible state from the saved world. These are presentation-only
    // operations: no map reset, spawn, capture, income or random draw is replayed.
    SessionWorldPresentation presentation;presentation.token=result->next_world_token_++;
    WorldMapInstall map;map.grid_resource_id=saved.world.grid_resource_id;map.lookup_resource_id=saved.world.lookup_resource_id;
    map.style=saved.world.style;map.grid=saved.world.grid;map.effective_lookup=saved.world.effective_lookup;
    map.style_changed=true;map.music_request=MissionAudioRequest{5,map.style==0?1:map.style==2?2:3};map.stop_music_if_config_a4_disabled=true;
    presentation.operations.push_back({saved.mission.command_index,9,{},std::move(map)});
    if(!saved.world.sites.empty())presentation.operations.push_back({saved.mission.command_index,7,{},WorldSitesCommand{saved.world.sites}});
    if(!saved.world.site_points.empty())presentation.operations.push_back({saved.mission.command_index,17,{},WorldPointsCommand{saved.world.site_points}});
    if(saved.world.title_text_index) {
        const auto index=*saved.world.title_text_index;
        presentation.operations.push_back({saved.mission.command_index,16,{},WorldPhaseCommand{{},index,script_text_id(signed16(index)),2}});
    }
    for(std::size_t slot=0;slot<2;++slot) {
        const auto marker=slot==0?saved.world.marker_254:saved.world.marker_25c;
        presentation.operations.push_back({saved.mission.command_index,static_cast<std::uint16_t>(slot==0?19:20),{},
            WorldMarkerCommand{slot==0?WorldMarkerSlot::field_254:WorldMarkerSlot::field_25c,marker[0],marker[1],static_cast<std::uint8_t>(slot==0?2:3),slot==0}});
    }
    result->world_presentation_=std::move(presentation);
    std::optional<std::size_t> focus=saved.selected;
    if(!focus)for(std::size_t i=0;i<saved.world.units.size();++i)
        if(saved.world.units[i].active && saved.world.units[i].owner==saved.turn.current_player){focus=i;break;}
    if(focus) {
        result->request_focus({saved.movement[*focus].tile_x,saved.movement[*focus].tile_y},SessionFocusReason::turn_start,focus);
        result->focus_->immediate=true;
    }
    return result;
}

} // namespace g1
