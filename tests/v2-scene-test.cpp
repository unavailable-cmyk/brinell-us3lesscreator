#include "v2-fixture.hpp"
#include "v2/SharedHistoryPrototype.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace v2test;
int main() {
    try {
        {
            std::vector<Decoration> divided{decoration(10, 0, {7}), decoration(20, 0, {7})};
            divided[0].chainPartition = 1;
            divided[1].chainPartition = 2;
            Native sink(divided);
            auto partitioned = Scene::compile(divided);
            require(partitioned.chainCount() == 2, "Spatial partitions must not share visibility boundaries");
            partitioned.translate(7, {3, 0}, 1, sink);
            require(partitioned.position(partitioned.handle(0)).x == 13 && partitioned.position(partitioned.handle(1)).x == 23,
                "A native group must update all of its spatial partitions");
            partitioned.acquireNative(partitioned.handle(0), sink);
            partitioned.translate(7, {2, 0}, 2, sink);
            require(sink.positions[0].x == 15 && partitioned.position(partitioned.handle(1)).x == 25,
                "Spatial partition handoff duplicated or lost a native action");
        }
        std::vector<Decoration> input{
            decoration(0, 0, {9, 2, 9}), decoration(0, 0, {2, 9}),
            decoration(20, 4, {2}), decoration(30, 5, {9}),
            decoration(40, 6, {9}), decoration(50, 7, {9})};
        input[2].visible = false;
        input[3].dependency = Dependency::authoritativeCpu;
        input[4].dependency = Dependency::independentTransform;
        input[5].material = 4;
        Native native(input);
        auto scene = Scene::compile(input);
        require(scene.chainCount() == 3, "Canonical membership chains");
        require(scene.path(scene.handle(4)) == ExecutionPath::nativeCpu, "Path B retains native owner");
        require(scene.geometry().size() == 24, "Only shared objects have resident geometry");
        const auto initial = scene.draws();
        require(initial.size() == 4 && initial[0].vertexCount == 12 &&
            initial[1].nativeObject == 3 && initial[2].nativeObject == 4 && initial[3].material == 4,
            "Visibility, native order and material boundaries");
        scene.clearCounters();
        // Sorting actions by group would change this cancellation result.
        scene.translate(9, {1e16, 0}, 1, native);
        scene.translate(2, {-1e16, 0}, 2, native);
        scene.translate(9, {1, 0}, 3, native);
        require(scene.position(scene.handle(0)).x == 1, "Native activation ordering");
        require(scene.position(scene.handle(1)).x == 1, "Duplicate membership applies once");
        require(native.visits == 4 && native.orders == std::vector<std::uint64_t>({1, 3}), "Exclusive native routing");
        require(scene.counters().chainEvaluations == 6, "Evaluate each affected chain once per action");
        require(scene.updates().size() == 3 && scene.updates().size() == 3, "Dirty-chain coalescing and retry");
        scene.acknowledgeUpdates();
        require(scene.updates().empty(), "Idle frame has no updates");
        scene.draws();
        require(scene.counters().drawListObjectVisits == 0, "Routine frame avoids object traversal");
        const auto actions = scene.counters().actions;
        rejects([&] { scene.translate(9, {2, 0}, 3, native); }, "Out of order accepted");
        rejects([&] { scene.translate(9, {std::numeric_limits<double>::infinity(), 0}, 4, native); }, "Nonfinite accepted");
        require(scene.counters().actions == actions && native.visits == 4, "Rejected operation changed ownership state");
        scene.acquireNative(scene.handle(0), native);
        require(native.positions[0].x == 1 && native.handoffs == 1, "Materialize before CPU read");
        scene.acquireNative(scene.handle(0), native);
        require(native.handoffs == 1, "Handoff is idempotent");
        scene.translate(9, {2, 0}, 4, native);
        require(native.positions[0].x == 3 && scene.position(scene.handle(1)).x == 3, "Mixed group processed once per owner");
        scene.setVisible(scene.handle(2), true, native);
        scene.setVisible(scene.handle(0), false, native);
        require(!native.visible[0], "Native visibility mutation");
        scene.reset(native);
        require(scene.path(scene.handle(0)) == ExecutionPath::nativeCpu && native.positions[0].x == 0 && native.visible[0],
            "Reset retains dependency promotion and restores native state");
        require(scene.position(scene.handle(1)).x == 0, "Reset shared translation");
        scene.translate(9, {4, 0}, 1, native);
        require(native.positions[0].x == 4, "Reset native ordering epoch");
        const auto old = scene.handle(0);
        scene = Scene::compile(input);
        rejects([&] { scene.path(old); }, "Stale scene handle accepted");
        auto empty = Scene::compile({});
        empty.translate(999, {}, 0, native);
        require(empty.draws().empty() && empty.updates().empty(), "Empty scene");

        // More chains than one mobile-compatible palette page, with no sorting
        // of object/material order to disguise the extra ordered ranges.
        auto many = fixture(1000, false);
        for (unsigned i = 0; i < many.size(); ++i) many[i].groups = {i};
        auto pages = Scene::compile(many);
        require(pages.chainCount() == 1000 && pages.draws().size() == 4, "Palette paging");
        Native manyNative(many);
        pages.translate(999, {1, 2}, 1, manyNative);
        require(pages.updates().size() == 1 && pages.updates()[0].chain == 999, "Reverse active group mapping");

        auto overflow = Scene::compile(std::span<const Decoration>(input).first(1));
        overflow.translate(9, {std::numeric_limits<float>::max(), 0}, 1, native);
        rejects([&] { overflow.translate(9, {std::numeric_limits<float>::max(), 0}, 2, native); }, "Overflow accepted");
        require(overflow.counters().actions == 1, "Overflow partially submitted");
        auto longRun = Scene::compile(std::span<const Decoration>(input).first(1));
        for (std::uint64_t order = 1; order <= 100000; ++order)
            longRun.translate(9, {.125, 0}, order, native);
        require(longRun.position(longRun.handle(0)).x == 12500 && longRun.updates().size() == 1,
            "Persistent state relies on an action-history limit");
        longRun.acquireNative(longRun.handle(0), native);
        require(native.positions[0].x == 12500, "Long-running chain handoff");
        longRun.acknowledgeUpdates();
        longRun.clearCounters();
        longRun.translate(9, {1, 0}, 100001, native);
        require(longRun.counters().chainEvaluations == 0 && native.positions[0].x == 12501,
            "Chain with no consumers still receives shared operations");
        const std::array<GroupId, 3> newGroups{3, 3, 4};
        longRun.setGroups(longRun.handle(0), newGroups, native);
        longRun.translate(9, {100, 0}, 100002, native);
        require(native.positions[0].x == 12501, "Old membership retained after mutation");
        longRun.translate(3, {2, 0}, 100003, native);
        require(native.positions[0].x == 12503, "New duplicate membership applied twice");
        auto changing = Scene::compile(std::span<const Decoration>(input).first(2));
        changing.translate(9, {2, 0}, 1, native);
        changing.setGroups(changing.handle(0), newGroups, native);
        changing.translate(9, {3, 0}, 2, native);
        changing.translate(3, {4, 0}, 3, native);
        require(native.positions[0].x == 6 && changing.position(changing.handle(1)).x == 5,
            "Dynamic membership failed to isolate chain owners before CPU read");
        const auto retired = changing.handle(1);
        changing.retire(retired, native);
        rejects([&] { changing.position(retired); }, "Retired object remained accessible");
        changing.reset(native);
        changing.clearCounters();
        changing.translate(9, {1, 0}, 1, native);
        require(changing.counters().chainEvaluations == 0 && changing.counters().nativeVisits == 0,
            "Retired object still receives operations after reset");
        auto fractionalInput = fixture(1000, true);
        auto fractional = Scene::compile(fractionalInput);
        Native fractionalNative(fractionalInput);
        auto reference = fractionalNative.positions;
        for (std::uint64_t order = 1; order <= 4096; ++order) {
            const GroupId group = order % 2 ? 7 : 2;
            const Translation delta{std::sin(static_cast<double>(order)) * .1, .013};
            fractional.translate(group, delta, order, fractionalNative);
            for (ObjectId id = 0; id < fractionalInput.size(); ++id) {
                const auto& groups = fractionalInput[id].groups;
                if (std::find(groups.begin(), groups.end(), group) == groups.end()) continue;
                reference[id].x += delta.x;
                reference[id].y += delta.y;
            }
        }
        for (ObjectId id = 0; id < fractionalInput.size(); ++id) {
            const auto actual = fractional.position(fractional.handle(id));
            require(std::abs(actual.x - reference[id].x) < 1e-8 && std::abs(actual.y - reference[id].y) < 1e-8,
                "Fractional translation exceeds controlled reference tolerance");
        }
        auto invalid = input;
        invalid[0].quad[0].x = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { Scene::compile(invalid); }, "Invalid geometry accepted");
        auto metadataInput = decoration(10, 20, {1});
        metadataInput.nativeTransform = {{10, 20}, 30, 31, 2, 3, 4, 5, .25f, .75f, true, false};
        auto metadataScene = Scene::compile(std::span<const Decoration>(&metadataInput, 1));
        Native metadataNative(std::span<const Decoration>(&metadataInput, 1));
        metadataScene.translate(1, {3, 4}, 1, metadataNative);
        const auto metadata = metadataScene.nativeTransform(metadataScene.handle(0));
        require(metadata.position.x == 13 && metadata.position.y == 24 && metadata.rotationX == 30 &&
            metadata.rotationY == 31 && metadata.scaleX == 2 && metadata.scaleY == 3 && metadata.skewX == 4 &&
            metadata.skewY == 5 && metadata.anchorX == .25f && metadata.anchorY == .75f && metadata.flipX && !metadata.flipY,
            "Native transform metadata lost");
        using History = SharedHistoryPrototype;
        std::vector<History::RouteEvidence> evidence{{{7, 2}, 4, false}, {{7, 2}, 4, false}};
        History history(evidence);
        history.move(7, {1, 2}, 1, 5);
        history.move(2, {3, 4}, 2, 5);
        require(history.historyOffset().x == 0 && history.positionOffset().x == 4 && history.stamp() == 5,
            "History must update once per native command stamp");
        history.move(7, {2, 0}, 3, 6);
        require(history.historyOffset().x == 4 && history.positionOffset().x == 6 && history.stamp() == 6,
            "History must retain the pre-command position");
        evidence[1].orderedRoutes = {2, 7};
        rejects([&] { History mismatch(evidence); }, "Different native route order shared history");
        rejects([&] { history.move(7, {1, 0}, 3, 7); }, "History accepted duplicate action order");
        History ordered(History::RouteEvidence{{7, 2}, 4, false});
        ordered.move(7, {1, 0}, 1, 4);
        ordered.move(2, {-1e16, 0}, 2, 5);
        ordered.move(7, {2, 0}, 3, 5);
        const auto reconstructed = ordered.materialize({1e16, -0.0}, {44, 55});
        require(reconstructed.position.x == 2 && reconstructed.previous.x == static_cast<float>(1e16) &&
            reconstructed.stamp == 5 && reconstructed.invalidateRect && std::signbit(reconstructed.position.y),
            "Native handoff must replay ordered additions and only capture history on a new command");
        std::cout << "PASS V2 scene ownership, overlap, ordering, visibility, reset, fallback, lifetime, paging and rejection\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
