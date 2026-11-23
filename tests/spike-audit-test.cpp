#include "v2/SpikeAudit.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace vulkanizer::v2::spike;
int main() {
    audit.reset();
    audit.begin(1, 80., {});
    {
        Scope outer(Phase::director);
        {
            Scope inner(Phase::moveStep, 0, true, 1. / 240.);
            audit.recordGroup(575, Work::nativeMove, 100);
            audit.recordGroup(575, Work::residentMove, 200);
            audit.recordGroup(848, Work::nativeRotate, 1000);
            audit.recordGroup(815, Work::residentRotate, 500);
            audit.recordGroup(9999, Work::nativeMove, 10);
            audit.recordGroup(-1, Work::nativeMove, 20);
            event(Event::atlasRemove, 4);
        }
        { Scope next(Phase::moveStep, 0, true, 1. / 240.); }
    }
    Counters counts{}; counts.nativeVisits = 120;
    audit.finish(80.01, counts);
    const auto& f = audit.worst[0];
    assert(f.steps == 2 && f.groups[0].id == 848 && f.groups[1].id == 815 && f.groups[2].id == 575);
    assert(f.groups[2].total() == 300 && f.unknown.total() == 20);
    assert(f.counters.nativeVisits == 120 && f.events[static_cast<std::size_t>(Event::atlasRemove)] == 4);
    double exclusive = 0;
    for (const auto& p : f.phases) { assert(p.inclusive >= p.exclusive); exclusive += p.exclusive; }
    assert(std::abs(exclusive - f.topLevelMs) < 1e-9);
    assert(Scope::current == nullptr);
    for (unsigned n = 2; n < 20; ++n) {
        audit.begin(n, 80., counts);
        audit.start -= std::chrono::milliseconds(n * 100);
        if (n == 19) {
            audit.recordGroup(575, Work::nativeMove, 1);
            for (unsigned j = 0; j < maxSteps + 2; ++j) audit.recordStep(1. / 240., j + 1.);
        }
        audit.finish(81., counts);
    }
    assert(audit.worstCount == 10 && audit.worst[0].index == 19 && audit.worst[9].index == 10);
    assert(audit.worst[0].groups[0].total() == 1 && audit.worst[0].groups[1].total() == 0);
    assert(audit.worst[0].steps == maxSteps + 2 && audit.worst[0].maxStepIndex == maxSteps + 1);
    assert(audit.worst[0].stepTimes.back() == maxSteps);
    audit.reset(); audit.begin(1, 0., {}); audit.finish(1., {});
    assert(audit.worstCount == 1 && audit.worst[0].groups[0].total() == 0 && audit.frames == 1);
    std::cout << "spike audit: nested scopes, bounded top ten, sparse groups, counter deltas, step overflow OK\n";
}
