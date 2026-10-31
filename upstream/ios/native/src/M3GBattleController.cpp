#include "g1/M3GBattleController.hpp"

#include <cmath>
#include <cstring>
#include <utility>

namespace g1 {
namespace {
void require(bool condition,const char* message) { if(!condition)throw FormatError(message); }
std::int32_t signed_bits(std::uint32_t bits) noexcept { std::int32_t value;std::memcpy(&value,&bits,4);return value; }
std::int32_t add32(std::int32_t a,std::int32_t b) noexcept { return signed_bits(std::uint32_t(a)+std::uint32_t(b)); }
std::int32_t mul32(std::int32_t a,std::int32_t b) noexcept { return signed_bits(std::uint32_t(a)*std::uint32_t(b)); }
float addf(float a,float b) noexcept { volatile float value=a+b;return value; }
float mulf(float a,float b) noexcept { volatile float value=a*b;return value; }
float divf(float a,float b) noexcept { volatile float value=a/b;return value; }
std::int32_t integer(float value) {
    require(std::isfinite(value) && double(value)>=-2147483648.0 && double(value)<2147483648.0,
            "battle HUD arithmetic exceeds signed32");
    return static_cast<std::int32_t>(value);
}
void validate(const M3GBattleControllerConfig& config,const M3GBattleControllerState& state) {
    require(state.phase<=12,"unit-viewer or unknown battle phase is outside ordinary presentation");
    require(config.forms[0]<=1 && config.forms[1]<=1,"invalid battle form");
    require(config.logical_width>0 && config.logical_width<=8191 &&
            config.logical_height>0 && config.logical_height<=16383,"invalid native battle logical viewport");
    require(!config.embedded_camera[8],"scene-only battle camera8 cannot have a model binding");
}
class Step {
public:
    Step(const M3GBattleControllerConfig& config,const M3GBattleControllerState& state)
        :config_(config) { result.state=state; }
    M3GBattleControllerStep result;
    void event(M3GBattleEvent e) { result.events.push_back(e); }
    void animate(M3GBattleTarget target,std::int32_t time) {
        M3GBattleEvent e;e.kind=M3GBattleEventKind::animate;e.target=target;e.time=time;event(e);
    }
    void camera(std::uint8_t id) {
        M3GBattleEvent e;e.kind=M3GBattleEventKind::select_camera;e.id=id;event(e);
        result.state.camera_side_a=id==0 || id==1 || id==2 || id==6 || id==7 || id==9 || id==11;
        result.state.embedded_current=config_.embedded_camera.at(id);
    }
    void propagate(std::int32_t time,std::uint8_t adjustment) {
        M3GBattleEvent e;e.kind=M3GBattleEventKind::propagate_camera;e.time=time;e.adjust=adjustment;event(e);
    }
    void start(std::size_t side,std::uint8_t clip) {
        M3GBattleEvent e;e.kind=M3GBattleEventKind::start_clip;e.target=side ? M3GBattleTarget::model_b : M3GBattleTarget::model_a;
        e.clip=clip;e.clock=static_cast<std::int32_t>(side);event(e);
        const auto step=start_m3g_battle_clip(clip);result.state.clocks[side]=step.stored_clock_ms;
        animate(e.target,step.animation_time_ms);
    }
    void tick(std::size_t target,std::size_t clock,std::uint8_t clip,std::int32_t delta,bool restart=false) {
        M3GBattleEvent e;e.kind=M3GBattleEventKind::tick_clip;e.target=target ? M3GBattleTarget::model_b : M3GBattleTarget::model_a;
        e.clip=clip;e.clock=static_cast<std::int32_t>(clock);e.delta=delta;e.restart=restart;event(e);
        const auto step=advance_m3g_battle_clip(clip,result.state.clocks[clock],delta,restart);
        result.state.clocks[clock]=step.stored_clock_ms;
        if(step.restarted) {
            auto began=e;began.kind=M3GBattleEventKind::start_clip;began.delta=0;began.restart=false;event(began);
        }
        animate(e.target,step.animation_time_ms);
    }
    void place(std::size_t side,bool hit) {
        M3GBattleEvent e;e.kind=M3GBattleEventKind::place_effect;e.side_a=side==0;e.hit=hit;event(e);
    }
    void text(std::int32_t field,std::int32_t value) {
        M3GBattleEvent e;e.kind=M3GBattleEventKind::clear_text;e.field=field;event(e);
        e.kind=M3GBattleEventKind::set_integer_text;e.value=value;event(e);
    }
    void audio(std::int32_t id) {
        M3GBattleEvent e;e.kind=M3GBattleEventKind::audio;e.operation=5;e.argument=id;event(e);
    }
    void shooting(std::size_t side,bool camera_first=true) {
        result.state.phase=side ? 4 : 1;
        if(camera_first)camera(side ? 4 : 1);
        start(side,1);place(side,false);
    }
    void setup(std::size_t side) {
        result.state.phase=side ? 10 : 9;start(side,5);camera(side ? 10 : 9);
    }
    void closeup(std::size_t side) {
        result.state.phase=side ? 12 : 11;start(side,4);camera(side ? 12 : 11);
    }
private:
    const M3GBattleControllerConfig& config_;
};
} // namespace

M3GBattleControllerStep advance_m3g_battle_controller(const M3GBattleControllerConfig& config,
    const M3GBattleControllerState& input,std::int32_t raw_delta) {
    validate(config,input);require(raw_delta>=0,"negative native battle elapsed time");
    Step step(config,input);auto& state=step.result.state;
    if(state.complete)return step.result;
    const auto old=input.elapsed_ms;
    const auto delta=(input.phase==2 || input.phase==5) ? raw_delta/2 : raw_delta;
    const auto next=add32(old,delta);
    const auto camera_time=[&](std::size_t side,std::int32_t fallback) {
        return state.embedded_current ? state.clocks.at(side) : fallback;
    };
    switch(input.phase) {
    case 0: {
        step.propagate(0,0);
        const auto fixed=signed_bits(std::uint32_t(config.logical_width)<<17U);
        const auto offset=integer(mulf(static_cast<float>(fixed),addf(1.0F,divf(static_cast<float>(old),-1000.0F))));
        state.slide_fixed16=signed_bits(0U-std::uint32_t(offset));
        state.elapsed_ms=next;
        if(next>999) {
            state.elapsed_ms=0;state.slide_fixed16=0;
            if(!config.intro)step.shooting(config.a_first ? 0 : 1);
            else if(config.forms[0]==1 || (!config.a_first && !config.counter))step.setup(1);
            else if(config.a_first && !config.counter)step.setup(0);
            else state.phase=8;
        }
        break;
    }
    case 8:
        step.propagate(old,0);state.elapsed_ms=next;
        if(next>3599) { state.elapsed_ms=0;step.setup(config.a_first ? 0 : 1); }
        break;
    case 9:case 10: {
        const std::size_t side=input.phase==9 ? 0 : 1;state.camera_side_a=side==0;
        step.tick(side,side,5,delta);step.propagate(camera_time(side,old),config.adjustments[side]);
        state.elapsed_ms=next;
        if(next>1332) {
            state.elapsed_ms=0;
            if((side==0 && config.a_first) || (side==1 && !config.a_first))step.setup(1-side);
            else if(side==0)step.closeup(config.forms[1]==0 ? 1 : 0);
            else step.closeup(config.forms[0]==0 ? 0 : 1);
        }
        break;
    }
    case 11:case 12: {
        const std::size_t side=input.phase==11 ? 0 : 1;
        if(side==1)state.camera_side_a=false;
        step.propagate(camera_time(side,old),config.adjustments[side]);
        step.tick(0,side,4,delta);state.elapsed_ms=next; // Original phase12 deliberately targets A.
        if(next>998) {
            state.elapsed_ms=0;
            if(side==0 && config.a_first && config.forms[1]==0)step.closeup(1);
            else if(side==1 && !config.a_first && config.forms[0]==0)step.closeup(0);
            else step.shooting(config.a_first ? 0 : 1);
        }
        break;
    }
    case 1:case 4: {
        const std::size_t side=input.phase==1 ? 0 : 1,other=1-side;
        step.animate(side ? M3GBattleTarget::effect_2 : M3GBattleTarget::effect_0,add32(old,-466));
        if(old<=465 && next>466)step.audio(side ? 9 : 8);
        step.propagate(camera_time(side,mul32(old,3)),config.adjustments[side]);
        step.tick(side,side,1,delta,side==1);state.elapsed_ms=next;
        if(next>1332) {
            state.elapsed_ms=0;state.phase=side ? 5 : 2;
            step.text(0x10,config.immune[other] ? 0 : config.damage[other]);
            step.camera(side ? 0 : 3);step.place(other,true);step.start(other,0);
        }
        break;
    }
    case 2:case 5: {
        const std::size_t side=input.phase==2 ? 1 : 0;
        const auto effect_time=mul32(add32(old,-66),2);
        step.animate(side ? M3GBattleTarget::effect_1 : M3GBattleTarget::effect_3,effect_time<0 ? 0 : effect_time);
        if(old<=332 && next>333)step.audio(side ? 11 : 10);
        step.propagate(camera_time(side,old),config.adjustments[side]);
        step.tick(side,side,0,delta);
        if(state.display_health[side]!=config.final_health[side]) {
            state.display_health[side]=config.final_health[side];step.text(side ? 0x28 : 0x20,state.display_health[side]);
        }
        state.elapsed_ms=next;
        if(next>731) {
            state.elapsed_ms=0;
            if(config.dead[side]) {
                step.camera(side ? 5 : 2);state.phase=side ? 3 : 6;step.start(side,2);
            } else if(config.counter && ((side==1 && config.a_first) || (side==0 && !config.a_first)))
                step.shooting(side);
            else state.phase=7;
        }
        break;
    }
    case 3:case 6: {
        const std::size_t side=input.phase==3 ? 1 : 0;
        step.propagate(camera_time(side,old/2),config.adjustments[side]);
        step.animate(M3GBattleTarget::death_effect,old);step.tick(side,side,2,delta);state.elapsed_ms=next;
        if(next>2999) { state.phase=7;state.elapsed_ms=0; }
        break;
    }
    case 7: {
        const auto fixed=signed_bits(std::uint32_t(config.logical_width)<<17U);
        const auto offset=integer(mulf(static_cast<float>(fixed),divf(static_cast<float>(old),1000.0F)));
        state.slide_fixed16=signed_bits(0U-std::uint32_t(offset));state.elapsed_ms=next;
        if(next>999)state.complete=true;
        break;
    }
    default:throw FormatError("unsupported ordinary battle phase");
    }
    return std::move(step.result);
}

M3GBattleDrawPlan m3g_battle_draw_plan(const M3GBattleControllerConfig& config,
    const M3GBattleControllerState& state,bool refresh) {
    validate(config,state);M3GBattleDrawPlan draw;draw.slide_fixed16=state.slide_fixed16;
    if(state.phase==0 && !refresh)return draw;
    draw.render_3d=true;
    if(state.phase==0) { draw.root_enabled[0]=config.forms[0]!=1;draw.root_enabled[1]=true; }
    else if(state.phase==7) { draw.root_enabled[0]=state.camera_side_a;draw.root_enabled[1]=!state.camera_side_a; }
    else {
        const auto phase=state.phase;
        draw.root_enabled[0]=state.camera_side_a || phase==1 || phase==8 || (phase==4 && config.forms[0]!=1);
        draw.root_enabled[1]=!state.camera_side_a || phase==1 || phase==4 || phase==8;
        if((phase==1 || phase==4) && state.elapsed_ms>466)draw.root_enabled[state.camera_side_a ? 2 : 4]=true;
        if(phase==2 || phase==5)draw.root_enabled[state.camera_side_a ? 5 : 3]=true;
        draw.root_enabled[6]=phase==3 || phase==6;
    }
    draw.damage_text=state.phase==2 || state.phase==5;
    if(draw.damage_text) {
        const auto faction=config.factions[state.phase==2 ? 0 : 1];
        draw.damage_x=faction ? config.logical_width/4 : (config.logical_width*3)/4;
        draw.damage_y=config.logical_height/4;
    }
    return draw;
}

} // namespace g1
