#include "g1/SpriteAnimation.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
std::size_t checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const std::invalid_argument&) { rejected = true; }
      catch (const std::out_of_range&) { rejected = true; }
    check(rejected, message);
}
} // namespace

int main() {
    try {
        // Synthetic unequal durations distinguish the recovered single-duration
        // forward tick from a conventional animation sequencer.
        g1::SpriteAnimation a({{7, 100}, {8, 30}, {9, 200}});
        check(a.current_frame() == 7U && a.state().elapsed_ms == 0, "forward start");
        check(!a.advance_ms(99) && a.current_frame() == 7U, "before exact duration");
        check(a.advance_ms(1) && a.current_frame() == 8U && a.state().elapsed_ms == 0, "exact duration boundary");
        a.reset();
        check(a.advance_ms(250) && a.current_frame() == 9U && a.state().elapsed_ms == 50,
              "original entry-duration forward tick");
        a.reset();
        check(!a.advance_ms(300) && a.current_frame() == 7U && a.state().elapsed_ms == 0,
              "whole cycle leaves same final geometry");
        a.reset(1);
        check(a.advance_ms(300) && a.current_frame() == 9U && a.state().completed &&
              a.state().repeat_count == 1, "forward completion");
        check(!a.advance_ms(1000) && a.state().completed, "completed forward calls remain defined");
        a.reset(2);
        a.advance_ms(300);
        check(a.state().repeat_count == 1 && !a.state().completed && a.current_frame() == 7U,
              "finite forward repeat decrement");
        a.reset(0);
        a.advance_ms(300);
        check(!a.state().completed && a.current_frame() == 7U, "zero repeat loops forward");
        a.reset(-1, true);
        check(a.current_frame() == 9U && a.state().elapsed_ms == 200, "reverse start");
        check(!a.advance_ms(200) && a.current_frame() == 9U && a.state().elapsed_ms == 0,
              "reverse strict-negative boundary");
        check(a.advance_ms(1) && a.current_frame() == 8U && a.state().elapsed_ms == 29,
              "reverse reads entered duration");
        a.reset(-1, true);
        a.advance_ms(331);
        check(a.current_frame() == 9U && a.state().elapsed_ms == 199 && !a.state().completed,
              "reverse infinite wrap");
        a.reset(0, true);
        a.advance_ms(331);
        check(a.current_frame() == 7U && a.state().elapsed_ms == 99 && a.state().completed,
              "zero repeat completes reverse");
        a.reset(2, true);
        a.advance_ms(661);
        check(a.current_frame() == 7U && a.state().repeat_count == 1 && a.state().completed &&
              a.state().elapsed_ms == 99, "reverse finite repeat and completion");
        g1::SpriteAnimation single({{42, 1}}, -1, true);
        single.advance_ms(2'000'000'000U);
        check(single.current_frame() == 42U && single.state().elapsed_ms == 0 && !single.state().completed,
              "bounded computation for a large infinite reverse tick");
        rejects([] { g1::SpriteAnimation invalid({}); }, "empty sequence accepted");
        rejects([] { g1::SpriteAnimation invalid({{0, 0}}); }, "zero duration accepted");
        rejects([] { g1::SpriteAnimation invalid({{0, 32768}}); }, "negative original signed duration accepted");
        rejects([] { g1::SpriteAnimation invalid(std::vector<g1::AtlasStep>(256, {0, 1})); },
                "sequence beyond original count byte accepted");
        const auto before = a.state();
        rejects([&] { a.advance_ms(std::numeric_limits<std::uint32_t>::max()); }, "oversized delta accepted");
        check(a.state().elapsed_ms == before.elapsed_ms && a.state().step_index == before.step_index &&
              a.state().completed == before.completed, "rejected delta changed state");
        a.reset();
        a.advance_ms(99);
        rejects([&] { a.advance_ms(std::uint32_t(std::numeric_limits<std::int32_t>::max())); },
                "signed elapsed overflow accepted");
        check(a.current_frame() == 7U && a.state().elapsed_ms == 99, "overflow rejection changed state");
        a.reset(2, true);
        a.advance_ms(11);
        const auto selected_before = a.state();
        a.select_step(0);
        check(a.state().step_index == 0 && a.state().elapsed_ms == selected_before.elapsed_ms &&
              a.state().repeat_count == selected_before.repeat_count &&
              a.state().reverse == selected_before.reverse &&
              a.state().completed == selected_before.completed,
              "original step selector preserves elapsed/repeat/reverse/completion");
        a.select_step(255);
        check(a.state().step_index == 0 && a.state().elapsed_ms == selected_before.elapsed_ms,
              "out-of-range step selector preserves state");
        const std::vector<g1::AtlasStep> saved_steps{{7, 100}, {8, 30}, {9, 200}};
        const auto same_state = [](const g1::SpriteAnimationState& x,
                                   const g1::SpriteAnimationState& y) {
            return x.step_index == y.step_index && x.elapsed_ms == y.elapsed_ms &&
                   x.repeat_count == y.repeat_count && x.reverse == y.reverse &&
                   x.completed == y.completed;
        };
        // select_step leaves 189 ms on a 100 ms step. This valid original
        // state must survive restoration without normalizing its elapsed time.
        auto restored = g1::SpriteAnimation::restore(saved_steps, a.state());
        check(same_state(restored.state(), a.state()) && restored.current_frame() == a.current_frame(),
              "restore preserves selected-step elapsed time");
        for (const auto delta : {0U, 17U, 200U, 1000U}) {
            const auto expected_changed = a.advance_ms(delta);
            check(restored.advance_ms(delta) == expected_changed &&
                  same_state(restored.state(), a.state()) && restored.current_frame() == a.current_frame(),
                  "restored reverse continuation differs");
            restored = g1::SpriteAnimation::restore(saved_steps, restored.state());
        }
        a.reset(1);
        a.advance_ms(300);
        a.select_step(0);
        restored = g1::SpriteAnimation::restore(saved_steps, a.state());
        check(restored.state().completed && restored.state().step_index == 0,
              "restore incorrectly constrains sticky completion to the last step");
        for (const auto delta : {0U, 31U, 330U}) {
            const auto expected_changed = a.advance_ms(delta);
            check(restored.advance_ms(delta) == expected_changed && same_state(restored.state(), a.state()),
                  "restored completed forward continuation differs");
        }
        auto invalid_state = a.state();
        invalid_state.step_index = saved_steps.size();
        rejects([&] { g1::SpriteAnimation::restore(saved_steps, invalid_state); },
                "out-of-range saved step accepted");
        invalid_state = a.state();
        invalid_state.elapsed_ms = -1;
        rejects([&] { g1::SpriteAnimation::restore(saved_steps, invalid_state); },
                "negative saved elapsed time accepted");
        rejects([&] { g1::SpriteAnimation::restore({}, a.state()); },
                "restore accepted an empty decoded sequence");
        rejects([&] { g1::SpriteAnimation::restore({{0, 0}}, {}); },
                "restore accepted a zero-duration decoded sequence");
        const g1::SpriteAnimationState maximum{0, std::numeric_limits<std::int32_t>::max(),
                                             std::numeric_limits<std::int32_t>::min(), false, false};
        restored = g1::SpriteAnimation::restore(saved_steps, maximum);
        check(same_state(restored.state(), maximum), "restore narrowed signed state fields");
        rejects([&] { restored.advance_ms(1); }, "restored elapsed overflow accepted");
        check(same_state(restored.state(), maximum), "restored overflow rejection changed state");
        std::cout << "Passed " << checks << " sprite animation checks; no original runtime required.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
