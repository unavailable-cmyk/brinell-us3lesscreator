#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <iterator>

// Diagnostic-only, on the game thread. No per-object clocks or allocations.
namespace vulkanizer::v2::spike {
using Clock = std::chrono::steady_clock;
enum class Phase : std::size_t {
    director, scheduler, actionManager, gameUpdate, commands, moveStep, moveActions,
    moveArray, nativeMove, rotation, rotationLoop, pivotlessLoop, disabledPositions,
    rotationArray, rotationClaim, follow, transform, area, areaEffects, spawn, toggle,
    preVisibility, visibility, batchVisit, batchSort, batchDraw, atlasDraw,
    presentation, residentFlush, routeRefresh, residentSync, visibilityMirrorScan,
    residentDispatch, residentReadback, residentSectionCommit, nativeSection,
    stockPreVisibility, preVisibilityHooks, stockVisibility, visibilityHooks, count
};
inline constexpr const char* phaseNames[] = {
    "director", "scheduler", "action_manager", "game_update", "commands", "move_step", "move_actions",
    "move_array", "native_move", "rotation", "rotation_native_loop", "rotation_no_center_loop", "rotation_disabled_positions",
    "rotation_array", "rotation_claim", "follow", "transform", "area", "area_effects", "spawn", "toggle",
    "pre_visibility", "visibility", "batch_visit", "batch_sort", "batch_draw", "atlas_draw",
    "presentation", "resident_flush", "route_refresh", "resident_sync", "visibility_mirror_scan",
    "resident_dispatch", "resident_section_readback", "resident_section_commit", "native_section",
    "stock_pre_visibility_callback", "pre_visibility_hooks", "stock_visibility_callback", "visibility_hooks"
};
constexpr std::size_t phaseCount = static_cast<std::size_t>(Phase::count);
static_assert(std::size(phaseNames) == phaseCount);
constexpr std::size_t groupCount = 10000, topCount = 10, maxSteps = 512;
enum class Event : std::size_t {
    atlasInsert, atlasRemove, atlasReorder, atlasResize, batchAdd, batchRemove, batchReorder,
    batchClear, batchTexture, spawnCalls, spriteBatchAttach, spriteBatchDetach,
    nativeSectionRegistrations, nativeSectionTransitions, visibilitySetChanges,
    activationTransitions, deactivationTransitions, opacityChanges, spriteColorUpdates,
    mirrorMaterializations, count
};
inline constexpr const char* eventNames[] = {"atlas_insert_slots", "atlas_remove_slots", "atlas_reorder_slots",
    "atlas_resize_calls", "batch_add_calls", "batch_remove_calls", "batch_reorder_calls",
    "batch_clear_calls", "batch_texture_calls", "spawn_group_calls", "sprite_batch_attachments",
    "sprite_batch_detachments", "native_section_registrations", "native_section_transitions",
    "visibility_set_changes", "activation_transitions", "deactivation_transitions",
    "opacity_changes", "sprite_color_updates", "mirror_materializations"};
constexpr std::size_t eventCount = static_cast<std::size_t>(Event::count);
static_assert(std::size(eventNames) == eventCount);
struct Metric { std::uint64_t calls = 0, work = 0; double inclusive = 0, exclusive = 0; };
struct Group {
    int id = -1;
    std::uint64_t nativeMove = 0, residentMove = 0, nativeRotate = 0, residentRotate = 0;
    std::uint64_t total() const { return nativeMove + residentMove + nativeRotate + residentRotate; }
};
enum class Work { nativeMove, residentMove, nativeRotate, residentRotate };
struct Counters {
    std::uint64_t ticks = 0, nativeVisits = 0, eliminatedVisits = 0, spriteVisits = 0, sectionCallbacks = 0,
        sectionTransitions = 0, revocations = 0, dirtyBytes = 0, atlasUploads = 0, submissions = 0,
        rotationNative = 0, rotationResident = 0;
    double syncMs = 0, handoffMs = 0, dispatchMs = 0, sectionCommitMs = 0;
};
inline Counters delta(const Counters& a, const Counters& b) {
    return {b.ticks-a.ticks,b.nativeVisits-a.nativeVisits,b.eliminatedVisits-a.eliminatedVisits,
        b.spriteVisits-a.spriteVisits,b.sectionCallbacks-a.sectionCallbacks,b.sectionTransitions-a.sectionTransitions,
        b.revocations-a.revocations,b.dirtyBytes-a.dirtyBytes,b.atlasUploads-a.atlasUploads,b.submissions-a.submissions,
        b.rotationNative-a.rotationNative,b.rotationResident-a.rotationResident,b.syncMs-a.syncMs,b.handoffMs-a.handoffMs,
        b.dispatchMs-a.dispatchMs,b.sectionCommitMs-a.sectionCommitMs};
}
struct Frame {
    std::uint64_t index = 0;
    double levelStart = 0, levelEnd = 0, wallMs = 0, previousFrameIntervalMs = 0;
    double schedulerDt = 0, gameDt = 0, stepDt = 0, topLevelMs = 0;
    std::uint64_t schedulerCalls = 0, gameCalls = 0, steps = 0;
    double stepSumMs = 0, step0Ms = 0, maxStepMs = 0;
    std::uint64_t maxStepIndex = 0;
    std::array<double, maxSteps> stepTimes{};
    std::array<Metric,phaseCount> phases{};
    std::array<std::uint64_t,eventCount> events{};
    std::array<Group,3> groups{};
    Group unknown{};
    Counters counters{};
};
struct Audit {
    bool active = false;
    Clock::time_point start{}, previousStart{};
    Frame frame{};
    Counters initial{};
    std::array<Group,groupCount> groups{};
    std::array<unsigned,groupCount> touched{};
    unsigned touchedCount = 0;
    std::array<Frame,topCount> worst{};
    std::size_t worstCount = 0;
    std::array<Metric,phaseCount> totals{};
    std::uint64_t frames = 0, totalSteps = 0;
    double totalWallMs = 0, totalTopLevelMs = 0;
    void reset() {
        active=false; previousStart={}; worstCount=0; totals={}; frames=0; totalSteps=0;
        totalWallMs=0; totalTopLevelMs=0;
    }
    void begin(std::uint64_t index, double level, const Counters& counts) {
        for (unsigned i = 0; i < touchedCount; ++i) groups[touched[i]] = {};
        touchedCount = 0;
        frame = {};
        frame.index = index; frame.levelStart = level; initial = counts;
        start = Clock::now();
        if (previousStart != Clock::time_point{}) frame.previousFrameIntervalMs =
            std::chrono::duration<double,std::milli>(start-previousStart).count();
        previousStart = start; active = true;
    }
    void recordGroup(int id, Work kind, std::uint64_t count) {
        if (!active || !count) return;
        Group* group = &frame.unknown;
        if (id >= 0 && static_cast<unsigned>(id) < groupCount) {
            group = &groups[id];
            if (!group->total()) { touched[touchedCount++] = static_cast<unsigned>(id); group->id = id; }
        }
        switch (kind) {
        case Work::nativeMove: group->nativeMove += count; break;
        case Work::residentMove: group->residentMove += count; break;
        case Work::nativeRotate: group->nativeRotate += count; break;
        case Work::residentRotate: group->residentRotate += count; break;
        }
    }
    void recordStep(double dt, double ms) {
        const auto index = frame.steps++;
        frame.stepDt += dt; frame.stepSumMs += ms;
        if (index == 0) frame.step0Ms = ms;
        if (index < maxSteps) frame.stepTimes[index] = ms;
        if (ms > frame.maxStepMs) { frame.maxStepMs = ms; frame.maxStepIndex = index; }
    }
    void finish(double level, const Counters& counts) {
        if (!active) return;
        const auto end = Clock::now(); active = false;
        frame.wallMs = std::chrono::duration<double,std::milli>(end-start).count();
        frame.levelEnd = level; frame.counters = delta(initial, counts);
        ++frames; totalSteps += frame.steps; totalWallMs += frame.wallMs; totalTopLevelMs += frame.topLevelMs;
        for (std::size_t i=0;i<phaseCount;++i) {
            const auto& f=frame.phases[i]; auto& t=totals[i];
            t.calls+=f.calls; t.work+=f.work; t.inclusive+=f.inclusive; t.exclusive+=f.exclusive;
        }
        if (worstCount == topCount && frame.wallMs <= worst[worstCount-1].wallMs) return;
        for (unsigned i=0;i<touchedCount;++i) {
            const auto& g=groups[touched[i]];
            for (unsigned j=0;j<frame.groups.size();++j) if (g.total()>frame.groups[j].total()) {
                for (auto k=frame.groups.size()-1;k>j;--k) frame.groups[k]=frame.groups[k-1];
                frame.groups[j]=g; break;
            }
        }
        auto pos = std::min(worstCount, topCount-1);
        while (pos && frame.wallMs > worst[pos-1].wallMs) { worst[pos]=worst[pos-1]; --pos; }
        worst[pos]=frame;
        worstCount=std::min(worstCount+1,topCount);
    }
};
inline thread_local Audit audit{};
struct Scope {
    inline static thread_local Scope* current = nullptr;
    Phase phase;
    Scope* parent = nullptr;
    Clock::time_point start{};
    double children = 0;
    bool running = false, substep = false;
    double dt = 0;
    explicit Scope(Phase p, std::uint64_t work=0, bool step=false, double stepDt=0, bool enabled=true) : phase(p) {
        if (!enabled || !audit.active) return;
        parent=current; current=this; running=true; substep=step; dt=stepDt;
        auto& metric=audit.frame.phases[static_cast<std::size_t>(p)]; ++metric.calls; metric.work+=work;
        start=Clock::now();
    }
    ~Scope() {
        if (!running) return;
        const double elapsed=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        auto& metric=audit.frame.phases[static_cast<std::size_t>(phase)];
        metric.inclusive+=elapsed; metric.exclusive+=std::max(0.0,elapsed-children);
        if (parent) parent->children+=elapsed; else audit.frame.topLevelMs+=elapsed;
        if (substep) audit.recordStep(dt,elapsed);
        current=parent;
    }
    Scope(const Scope&)=delete; Scope& operator=(const Scope&)=delete;
};
inline void event(Event e,std::uint64_t count=1) { if(audit.active) audit.frame.events[static_cast<std::size_t>(e)]+=count; }
inline void schedulerInput(double dt) { if(audit.active) { audit.frame.schedulerDt+=dt; ++audit.frame.schedulerCalls; } }
inline void gameInput(double dt) { if(audit.active) { audit.frame.gameDt+=dt; ++audit.frame.gameCalls; } }
}
