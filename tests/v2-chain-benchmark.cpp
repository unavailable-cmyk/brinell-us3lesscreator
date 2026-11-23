#include "v2-fixture.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>

using namespace v2test;
using Clock = std::chrono::steady_clock;
int main() {
    try {
        std::cout << "{\"schema\":1,\"scope\":\"controlled_shared_translation_cpu\",\"cases\":[\n";
        bool first = true;
        for (const auto count : {1u, 1000u, 10000u, 100000u}) {
            for (bool overlap : {false, true}) {
                auto input = fixture(count, overlap);
                auto scene = Scene::compile(input);
                Native native(input);
                scene.draws();
                scene.clearCounters();
                std::vector<double> samples;
                samples.reserve(240);
                std::uint64_t order = 0;
                std::uint64_t uploadBytes = 0;
                for (unsigned frame = 0; frame < 240; ++frame) {
                    const auto start = Clock::now();
                    scene.translate(7, {.125, -.0625}, ++order, native);
                    if (overlap) scene.translate(2, {-.0625, .125}, ++order, native);
                    uploadBytes += scene.updates().size() * sizeof(PaletteEntry);
                    scene.acknowledgeUpdates();
                    samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
                }
                double maxError = 0;
                for (ObjectId id = 0; id < input.size(); ++id) {
                    auto expected = input[id].position;
                    for (unsigned frame = 0; frame < 240; ++frame) {
                        if (std::find(input[id].groups.begin(), input[id].groups.end(), 7) != input[id].groups.end()) {
                            expected.x += .125; expected.y -= .0625;
                        }
                        if (overlap && std::find(input[id].groups.begin(), input[id].groups.end(), 2) != input[id].groups.end()) {
                            expected.x -= .0625; expected.y += .125;
                        }
                    }
                    const auto actual = scene.position(scene.handle(id));
                    maxError = std::max({maxError, std::abs(actual.x - expected.x), std::abs(actual.y - expected.y)});
                }
                require(maxError == 0 && native.visits == 0, "Transformation reference mismatch");
                require(scene.counters().drawListObjectVisits == 0, "Routine traversal regression");
                std::sort(samples.begin(), samples.end());
                if (!first) std::cout << ",\n";
                first = false;
                std::cout << std::setprecision(9) << "{\"objects\":" << count << ",\"overlap\":" << (overlap ? "true" : "false")
                    << ",\"chains\":" << scene.chainCount() << ",\"frames\":240,\"cpu_submission_p50_us\":" << samples[120]
                    << ",\"cpu_submission_p95_us\":" << samples[228] << ",\"chain_evaluations\":" << scene.counters().chainEvaluations
                    << ",\"palette_updates\":" << scene.counters().paletteUpdates << ",\"palette_upload_bytes\":" << uploadBytes
                    << ",\"routine_object_visits\":0,\"native_visits\":0,\"max_position_error\":" << maxError
                    << ",\"gpu_time_ms\":null,\"dispatch_count\":0,\"draw_count\":null}";
            }
        }
        // Hold object count fixed while increasing the number of active chains.
        for (unsigned chains : {1u, 100u, 1000u}) {
            auto input = fixture(100000, false);
            for (unsigned i = 0; i < input.size(); ++i) input[i].groups = {7, 100 + i % chains};
            auto scene = Scene::compile(input);
            Native native(input);
            scene.draws();
            scene.clearCounters();
            const auto started = Clock::now();
            for (unsigned frame = 1; frame <= 240; ++frame) {
                scene.translate(7, {.125, 0}, frame, native);
                scene.updates(); scene.acknowledgeUpdates();
            }
            const auto us = std::chrono::duration<double, std::micro>(Clock::now() - started).count() / 240;
            require(scene.counters().chainEvaluations == chains * 240ull, "Active chain scaling");
            std::cout << ",\n{\"objects\":100000,\"active_chain_sweep\":true,\"chains\":" << chains
                << ",\"frames\":240,\"cpu_submission_mean_us\":" << us
                << ",\"chain_evaluations\":" << scene.counters().chainEvaluations << ",\"routine_object_visits\":0}";
        }
        std::cout << "\n]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
