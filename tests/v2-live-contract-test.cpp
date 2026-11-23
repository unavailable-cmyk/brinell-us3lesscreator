#include "v2-fixture.hpp"
#include "v2/AtlasPlan.hpp"
#include "v2/TranslationPrecision.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <tuple>

using namespace v2test;
int main() {
    try {
        AtlasPlan plan;
        plan.insert(2, {ExecutionPath::sharedChain, 1, 0, 0, 6, 0});
        plan.insert(3, {ExecutionPath::sharedChain, 1, 0, 6, 6, 1});
        plan.insert(5, {ExecutionPath::sharedChain, 1, 1, 12, 6, 2});
        plan.insert(6, {ExecutionPath::sharedChain, 2, 1, 18, 6, 3});
        std::vector<std::tuple<char, unsigned, unsigned>> calls;
        plan.submit(0, 9, [&](auto n, auto start) { calls.emplace_back('N', start, n); },
            [&](const Draw& draw) { calls.emplace_back('V', draw.firstVertex, draw.vertexCount); });
        require(calls == std::vector<std::tuple<char, unsigned, unsigned>>{
            {'N', 0, 2}, {'V', 0, 12}, {'N', 4, 1}, {'V', 12, 6}, {'V', 18, 6}, {'N', 7, 2}},
            "Mixed ownership changed native atlas batching/order");
        calls.clear();
        plan.submit(3, 3, [&](auto n, auto start) { calls.emplace_back('N', start, n); },
            [&](const Draw& draw) { calls.emplace_back('V', draw.firstVertex, draw.vertexCount); });
        require(calls == std::vector<std::tuple<char, unsigned, unsigned>>{{'V', 6, 6}, {'N', 4, 1}, {'V', 12, 6}},
            "Partial native atlas draw range");
        require(plan.setVisible(3, false), "Owned atlas slot not found for hiding");
        calls.clear();
        plan.submit(0, 9, [&](auto n, auto start) { calls.emplace_back('N', start, n); },
            [&](const Draw& draw) { calls.emplace_back('V', draw.firstVertex, draw.vertexCount); });
        require(calls == std::vector<std::tuple<char, unsigned, unsigned>>{
            {'N', 0, 2}, {'V', 0, 6}, {'N', 4, 1}, {'V', 12, 6}, {'V', 18, 6}, {'N', 7, 2}},
            "Hidden owned slot was drawn or fell back to native");
        require(plan.setVisible(3, true), "Owned atlas slot not found for showing");
        calls.clear();
        plan.submit(2, 2, [&](auto n, auto start) { calls.emplace_back('N', start, n); },
            [&](const Draw& draw) { calls.emplace_back('V', draw.firstVertex, draw.vertexCount); });
        require(calls == std::vector<std::tuple<char, unsigned, unsigned>>{{'V', 0, 12}},
            "Restored visible range did not preserve order and coalescing");
        plan.release(2); plan.release(3); plan.release(5); plan.release(6);
        calls.clear();
        plan.submit(0, 100000, [&](auto n, auto start) { calls.emplace_back('N', start, n); },
            [&](const Draw&) { throw std::runtime_error("Released object still drew through V2"); });
        require(calls.size() == 1 && std::get<2>(calls[0]) == 100000, "Native fallback fragmented into sprites");

        auto input = fixture(10000, true);
        auto scene = Scene::compile(input);
        Native native(input);
        scene.translate(7, {.1, .2}, 1, native);
        std::vector<Handle> handles;
        for (ObjectId id = 0; id < 5000; ++id) handles.push_back(scene.handle(id));
        handles.push_back(handles.front());
        scene.acquireNativeBatch(handles, native);
        require(native.handoffs == 5000, "Bulk handoff duplicate processing");
        scene.translate(7, {2, 3}, 2, native);
        require(native.positions[0].x == input[0].position.x + .1 + 2, "Bulk handoff lost accumulated movement");
        require(scene.path(scene.handle(9999)) == ExecutionPath::sharedChain, "Bulk handoff migrated unrelated owner");
        scene.clearCounters();
        for (ObjectId id = 5000; id < 10000; ++id) scene.drawFor(scene.handle(id));
        require(scene.counters().drawListObjectVisits == 0, "Direct atlas ranges rebuild a full scene list");

        double maxObserved = 0;
        unsigned accepted = 0, rejected = 0;
        for (double coordinate : {0., 400., 10000., 100000., 1000000.}) {
            for (double scale : {.125, .5, 1., 4., 16.}) {
                TranslationPrecision precision{std::abs(coordinate), 0};
                Translation offset{}, reference{coordinate, coordinate};
                for (unsigned step = 0; step < 100000; ++step) {
                    Translation delta{std::sin(step * .13) * .03125, .0013};
                    precision.accumulatedError = precision.nextError(offset, delta);
                    offset.x += delta.x; offset.y += delta.y;
                    reference.x += delta.x; reference.y += delta.y;
                    const auto renderedX = static_cast<float>(coordinate) + static_cast<float>(offset.x);
                    const auto renderedY = static_cast<float>(coordinate) + static_cast<float>(offset.y);
                    const auto error = scale * std::max(std::abs(static_cast<double>(renderedX) - static_cast<float>(reference.x)),
                        std::abs(static_cast<double>(renderedY) - static_cast<float>(reference.y)));
                    const auto bound = precision.pixelBound(offset, scale, precision.accumulatedError);
                    require(error <= bound, "Numerical admission bound failed at representative GD coordinates");
                    if (bound <= .125) { ++accepted; maxObserved = std::max(maxObserved, error); }
                    else ++rejected;
                }
            }
        }
        TranslationPrecision extreme{400, 0};
        const Translation cancellation{1e12, -1e12};
        require(extreme.pixelBound(cancellation, 1, extreme.nextError({}, cancellation)) > .125,
            "Cancellation-heavy movement escaped native fallback guard");
        std::cout << "{\"atlas_order_and_batching\":\"pass\",\"bulk_handoff\":\"pass\",\"numerical_samples\":"
            << accepted + rejected << ",\"accepted\":" << accepted << ",\"rejected_by_pixel_budget\":" << rejected
            << ",\"maximum_observed_accepted_pixel_divergence\":" << maxObserved << ",\"bound_violations\":0}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
