#include "diagnostics/GameplaySpeedWindow.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>

static void check(bool okay, char const* name) {
    if (!okay) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
int main() {
    vulkanizer::GameplaySpeedWindow native, slow;
    // 30 rendered frames still require 240 native simulation ticks per second.
    for (unsigned frame = 0; frame < 60; ++frame) {
        native.frame(1.0 / 30); slow.frame(1.0 / 60);
        for (unsigned step = 0; step < 8; ++step) native.tick(1.0 / 240);
        for (unsigned step = 0; step < 4; ++step) slow.tick(1.0 / 240);
    }
    check(native.fullSpeed(2, native.physicsSeconds, 1), "native catch-up at 30 FPS");
    check(!slow.fullSpeed(2, slow.physicsSeconds, 1), "reject fixed-delta half-speed FPS result");
    check(native.ticks == 480 && slow.ticks == 240, "preserve measured tick counts");
    check(!native.fullSpeed(2, 2, 0.5), "time warp needs separate native comparison");
    check(!native.fullSpeed(2, -1, 1), "reject reset/seek window");
    check(!native.fullSpeed(0, 0, 1), "reject empty wall interval");
    native.tick(std::numeric_limits<double>::quiet_NaN());
    check(!native.fullSpeed(2, 2, 1), "reject invalid delta");
    std::cout << "PASS: native catch-up, half-speed rejection, tick counts, time warp, reset, invalid delta\n";
}
