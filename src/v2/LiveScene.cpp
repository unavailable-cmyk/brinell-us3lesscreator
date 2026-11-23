#include "LiveScene.hpp"
#include "SpikeAudit.hpp"
#include "RevocationCaller.hpp"
#include "PositionSetterAudit.hpp"
#include "AtlasPlan.hpp"
#include "GlRenderer.hpp"
#include "VisibilityMirrorWorklist.hpp"
#include "VisibilityCensus.hpp"
#include "VisibilitySectionFilter.hpp"
#include "VisibilityOwnership.hpp"
#include "FrameGpuTimer.hpp"
#include "TranslationPrecision.hpp"
#include "SharedHistoryPrototype.hpp"
#include "backend/RotationScheduler.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/SpawnTriggerGameObject.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/GroupCommandObject2.hpp>
#include <Geode/binding/CCMoveCNode.hpp>
#include <Geode/binding/DynamicMoveCalculation.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LocalLevelManager.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/GJSpriteColor.hpp>
#include <Geode/binding/ColorAction.hpp>
#include <Geode/cocos/kazmath/include/kazmath/GL/matrix.h>
#include <windows.h>
#include <intrin.h>
#include <GL/gl.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;
namespace vulkanizer::v2::live {
namespace {
using Clock = std::chrono::steady_clock;
constexpr double pixelBudget = .125;
constexpr float nativeSectionLimit = 10'000'000.f;
// Reserved command group bit: absolute resident position update, not a route.
constexpr std::uint32_t residentRebaseBit = 0x80000000u;
const char* aggressiveRejection(const GameObject*);
struct EnvFlags {
    static bool isSet(const char* name) { return std::getenv(name) != nullptr; }
    static bool isOne(const char* name) {
        const auto* value = std::getenv(name);
        return value && std::strcmp(value, "1") == 0;
    }
    static std::optional<double> number(const char* name) {
        if (const auto* value = std::getenv(name)) return std::atof(value);
        return std::nullopt;
    }
    static std::optional<std::string> text(const char* name) {
        if (const auto* value = std::getenv(name)) return std::string(value);
        return std::nullopt;
    }

    bool v2 = isSet("VULKANIZER_V2");
    bool fixedProfile = isSet("VULKANIZER_V2_TEST") && isOne("VULKANIZER_V2_FIXED_PROFILE");
    bool mirrorWorklistAudit = isSet("VULKANIZER_V2_TEST") && isOne("VULKANIZER_V2_MIRROR_WORKLIST_AUDIT");
    bool mirrorWorklistNegative = mirrorWorklistAudit && isOne("VULKANIZER_V2_MIRROR_WORKLIST_NEGATIVE");
    bool visibilityCensus = isSet("VULKANIZER_V2_TEST") && isOne("VULKANIZER_V2_VISIBILITY_CENSUS");
    bool visibilityFilter = isSet("VULKANIZER_V2_TEST") && isOne("VULKANIZER_V2_VISIBILITY_FILTER");
    bool visibilityFilterAudit = isSet("VULKANIZER_V2_TEST") && isOne("VULKANIZER_V2_VISIBILITY_FILTER_AUDIT");
    bool packedVisibilityFilter = isSet("VULKANIZER_V2_TEST") && isOne("VULKANIZER_V2_PACKED_VISIBILITY_FILTER");
    bool mirrorPrefetch = isSet("VULKANIZER_V2_TEST") && isOne("VULKANIZER_V2_MIRROR_PREFETCH");
    bool visibilityFilterNegative = visibilityFilter && visibilityFilterAudit && isOne("VULKANIZER_V2_VISIBILITY_FILTER_NEGATIVE");
    bool performanceOnly = fixedProfile || mirrorWorklistAudit || visibilityCensus || visibilityFilterAudit || (isSet("VULKANIZER_V2_TEST") && isOne("VULKANIZER_V2_PERFORMANCE_ONLY"));
    bool gpuResident = isOne("VULKANIZER_V2_GPU_RESIDENT");
    bool aggressive = gpuResident && isOne("VULKANIZER_V2_AGGRESSIVE_OWNERSHIP");
    bool hyperAggressive = aggressive && isSet("VULKANIZER_V2_TEST") && isOne("VULKANIZER_V2_HYPER_AGGRESSIVE");
    bool hyperNegative = hyperAggressive && !performanceOnly && isOne("VULKANIZER_V2_HYPER_NEGATIVE");
    bool moveSections = isSet("VULKANIZER_V2_MOVE_SECTIONS");
    bool cachedPolicies = isOne("VULKANIZER_V2_CACHED_POLICIES");
    bool baseline = isSet("VULKANIZER_V2_BASELINE");
    bool census = !performanceOnly && isSet("VULKANIZER_V2_CENSUS");
    bool audit = !performanceOnly && isOne("VULKANIZER_V2_RETENTION_AUDIT");
    bool nativeCallers = !performanceOnly && isOne("VULKANIZER_V2_NATIVE_CALLERS");
    bool revocationAudit = !performanceOnly && isOne("VULKANIZER_V2_REVOCATION_AUDIT");
    bool setterAudit = !performanceOnly && isOne("VULKANIZER_V2_SETTER_AUDIT");
    int setterShadowGroup = performanceOnly ? 0 : [] {
        const auto v = number("VULKANIZER_V2_SETTER_SHADOW_GROUP").value_or(0);
        return v == 815 || v == 575 || v == 848 ? static_cast<int>(v) : 0;
    }();
    bool setterShadowNative = isOne("VULKANIZER_V2_SETTER_SHADOW_NATIVE");
    bool setterShadowAligned = !performanceOnly && isOne("VULKANIZER_V2_SETTER_SHADOW_ALIGNED");
    bool spikeAudit = fixedProfile || (!performanceOnly && isOne("VULKANIZER_V2_SPIKE_AUDIT"));
    bool nativeVisibilityProfile = fixedProfile && hyperAggressive &&
        isOne("VULKANIZER_V2_NATIVE_VISIBILITY_PROFILE");
    bool visibilityOwnershipShadow = fixedProfile &&
        isOne("VULKANIZER_V2_VISIBILITY_OWNERSHIP_SHADOW");
    bool temporalReplay = isOne("VULKANIZER_V2_TEMPORAL_REPLAY");
    bool temporalControl = isOne("VULKANIZER_V2_TEMPORAL_CONTROL");
    bool lazyRoutes = isOne("VULKANIZER_V2_LAZY_ROUTES");
    bool persistentEdits = isOne("VULKANIZER_V2_PERSISTENT_EDITS");
    bool groupRotation = isOne("VULKANIZER_V2_GROUP_ROTATION");
    bool colorShakeFixture = isSet("VULKANIZER_V2_COLOR_SHAKE_FIXTURE");
    bool denseOwners = isSet("VULKANIZER_V2_DENSE_OWNERS");
    bool disabledCamera = isSet("VULKANIZER_V2_DISABLED_CAMERA");
    bool disabledHeadless = isSet("VULKANIZER_V2_DISABLED_HEADLESS");
    bool headless = isSet("VULKANIZER_V2_HEADLESS");
    bool headlessAlpha = isSet("VULKANIZER_V2_HEADLESS_ALPHA");
    bool headlessFixture = isSet("VULKANIZER_V2_HEADLESS_FIXTURE");
    bool headlessMaterial = isSet("VULKANIZER_V2_HEADLESS_MATERIAL");
    bool lateDependencies = isSet("VULKANIZER_V2_LATE_DEPENDENCIES");
    bool localColorShake = isSet("VULKANIZER_V2_LOCAL_COLOR_SHAKE");
    bool persistVisibility = isSet("VULKANIZER_V2_PERSIST_VISIBILITY");
    bool resolvedXlock = isSet("VULKANIZER_V2_RESOLVED_XLOCK");
    bool sharedHistory = isSet("VULKANIZER_V2_SHARED_HISTORY");
    bool staticTransforms = isSet("VULKANIZER_V2_STATIC_TRANSFORMS");
    bool structuralFixture = isSet("VULKANIZER_V2_STRUCTURAL_FIXTURE");
    bool test = isSet("VULKANIZER_V2_TEST");
    bool toggleFixture = isSet("VULKANIZER_V2_TOGGLE_FIXTURE");
    bool validate = !performanceOnly && isSet("VULKANIZER_V2_VALIDATE");
    bool xlockFixture = isSet("VULKANIZER_V2_XLOCK_FIXTURE");
    std::optional<double> captureTime = number("VULKANIZER_V2_CAPTURE_TIME");
    std::optional<double> testSeconds = number("VULKANIZER_V2_TEST_SECONDS");
    std::optional<std::string> capturePath = text("VULKANIZER_V2_CAPTURE");
    std::optional<std::string> reportPath = text("VULKANIZER_V2_REPORT");
    std::optional<std::string> levelName = text("VULKANIZER_TEST_LEVEL_NAME");
};

const EnvFlags& envFlags() {
    static const EnvFlags flags;
    return flags;
}

bool residentAtlasFixture(const EnvFlags& flags) {
    return flags.gpuResident && flags.validate && flags.test && flags.toggleFixture && !flags.levelName;
}

enum class FilterClass : unsigned {
    Unclassified, Eligible, NoSpace, Composite, Animated, OffsetOrSkew,
    InvalidSize, Oversized, InvalidSection, Retired, Count
};
constexpr const char* filterClassNames[]{"unclassified", "eligible", "unknown_space", "composite", "animated_particle",
    "offset_or_skew", "invalid_size", "oversized", "invalid_section", "retired"};
enum class FilterChange : unsigned { Batch, BatchAttach, BatchDetach, Scale, Skew, Flip, ObjectReset, ObjectType, Quad, Section, Count };
constexpr const char* filterChangeNames[]{"batch_change", "batch_attach", "batch_detach", "scale", "skew", "flip", "object_reset", "object_type", "quad_mutation", "section_change"};
enum class FilterDecision : unsigned { Deferred, TrackingUnavailable, GeometryPending, GeometryUnsupported, CurrentNear, Visible, OldNear, NativeEdit, AlreadyCurrent, Count };
constexpr const char* filterDecisionNames[]{"deferred", "tracking_unavailable", "geometry_pending", "geometry_unsupported", "current_section_near",
    "native_visible", "old_section_near", "native_edit", "no_pending_command"};
enum PackedFilterFlag : std::uint8_t { packedFilterEligible = 1u, packedFilterGeometryPending = 2u, packedFilterNativeEdit = 4u };
struct PackedFilterMetadata {
    int currentOuter = -1, currentMiddle = -1, oldOuter = -1, oldMiddle = -1;
    unsigned visibilitySpace = invalidVisibilitySpace;
    std::uint8_t flags = 0;
};
struct Entry {
    Ref<GameObject> object;
    Ref<CCSpriteBatchNode> batch;
    Ref<CCTextureAtlas> atlas;
    unsigned slot = 0;
    bool owned = false;
    bool headless = false;
    bool disabledHeadless = false;
    bool residentQuad = false, nativeMaterial = false;
    Translation reference;
    Translation historyBase, initialPrevious, sectionOrigin;
    std::optional<std::uint32_t> initialStamp;
    std::uint32_t residentStamp = 0;
    std::uint32_t residentApplied = 0, spriteApplied = 0;
    unsigned visibilitySpace = invalidVisibilitySpace;
    FilterClass visibilityClass = FilterClass::Unclassified;
    bool visibilityGeometryQueued = false;
    std::array<double, 4> sectionBounds{};
    bool sectionIndexed = false;
    bool nativeEdit = false;
    bool shadowSetterInFlight = false;
    std::uint64_t shadowRetentions = 0, shadowFirstStep = 0, shadowLastStep = 0;
    float editRotationX = 0, editRotationY = 0;
    float editRotationOffsetX = 0, editRotationOffsetY = 0;
    int editOuter = -1, editMiddle = -1;
    std::size_t pendingRebase = std::numeric_limits<std::size_t>::max();
    std::vector<std::pair<std::size_t, unsigned>> memberships;
};
using RevocationCause = std::tuple<std::string, std::string, int>;
struct RevocationMass {
    std::uint64_t members = 0, moveCountAtReleaseSum = 0;
};
struct RotationAudit {
    std::uint64_t calls = 0, recipientSlots = 0, released = 0;
    double first = 0, last = 0;
    void record(unsigned count, double time) {
        if (!calls++) first = time;
        last = time;
        recipientSlots += count;
    }
};
struct AuditTimes {
    double nativeMoveMs = 0, nativeMoveExclusiveMs = 0, nativeDrawMs = 0;
    double refreshMs = 0, frameMs = 0;
    std::uint64_t nativeCalls = 0, refreshes = 0, refreshSlots = 0;
};
struct Route {
    GroupId token = 0;
    Ref<CCArray> source;
    Ref<CCArray> native;
    std::vector<ObjectId> members;
    std::vector<ChainId> chains;
    std::vector<unsigned> nativeSlots;
    std::vector<std::uint32_t> residentActions;
    int nativeGroup = -1;
    bool valid = true;
    std::uint64_t residentVisits = 0, residentNativeVisits = 0;
    unsigned count = 0, owned = 0, ownedHeadless = 0, residentWidth = 0;
    std::uint64_t censusCalls = 0;
    std::uint64_t auditMoves = 0;
    unsigned auditInitialOwned = 0;
    bool auditMembershipChanged = false;
    std::map<RevocationCause, RevocationMass> auditRevocations;
};
struct Space {
    Ref<CCNode> batch;
    double left = 0, right = 0, bottom = 0, top = 0;
    bool headless = false;
};
struct Chain {
    std::vector<Handle> objects;
    std::vector<MirrorWord> mirrorMask;
    std::vector<Space> spaces;
    double minDx = -std::numeric_limits<double>::infinity(), maxDx = std::numeric_limits<double>::infinity();
    double minDy = -std::numeric_limits<double>::infinity(), maxDy = std::numeric_limits<double>::infinity();
    TranslationPrecision precision;
    std::optional<SharedHistoryPrototype> history;
    std::vector<GroupId> residentGroups;
    std::vector<std::uint32_t> residentTape;
    std::uint32_t residentTapeRevision = 0, lastResidentAction = 0;
    std::uint32_t lastQueuedGpuAction = 0;
    unsigned owned = 0;
    bool disabledHeadless = false;
};
// Offset thresholds are relative to the immutable position uploaded for each
// object. A move visits a cohort's boundary candidates, never its member list.
using SectionIndex = std::array<std::set<std::pair<double, ObjectId>>, 4>;
struct Atlas {
    Ref<CCSpriteBatchNode> batch;
    AtlasPlan plan;
    std::vector<ObjectId> objects;
    std::vector<ObjectId> nativeResidents;
    bool dirty = false;
};
struct Statistics {
    std::uint64_t translations = 0, eliminatedVisits = 0, nativeMoveVisits = 0;
    std::uint64_t nativeCommitCallbacks = 0, handoffCommits = 0, geometrySuppressed = 0, spriteVisits = 0;
    std::uint64_t nativeDraws = 0, v2Draws = 0, nativeQuads = 0, v2Quads = 0;
    std::uint64_t pageBreaks = 0, materialBoundaries = 0, ownershipBoundaries = 0;
    std::uint64_t chainCameraChecks = 0, validationVisits = 0, validationReadbackBytes = 0;
    std::uint64_t dependencyGuardVisits = 0;
    std::uint64_t nativeAtlasDirtyBytes = 0, nativeAtlasUploads = 0, ownershipCommitViolations = 0;
    std::uint64_t renderFailures = 0;
    std::uint64_t acquired = 0, released = 0, maxActiveChains = 0, maxDirty = 0;
    std::uint64_t headlessAcquired = 0, headlessReleased = 0, headlessEliminatedVisits = 0;
    std::uint64_t cachedPolicyHits = 0, cachedPolicyMisses = 0, structuralPredicatesAvoided = 0;
    std::uint64_t denseIdentityHits = 0, hashLookupFallbacks = 0, cacheInvalidations = 0;
    std::uint64_t dynamicSectionChecks = 0, precompiledRouteHits = 0, routeHashLookups = 0;
    std::uint64_t cacheInvalidationsAfterReady = 0;
    std::array<std::uint64_t, static_cast<std::size_t>(MovePolicyInvalidation::count)> cacheInvalidationsBySource{};
    std::uint64_t denseOwnershipCalls = 0, denseOwnershipSamples = 0, denseOwnershipMismatches = 0;
    double maxDivergence = 0, maxPixelBound = 0;
};
struct RevocationCounts {
    std::uint64_t objects = 0, pending = 0, stale = 0, pendingAndStale = 0;
    void add(unsigned flags) {
        ++objects;
        pending += (flags & 1) != 0;
        stale += (flags & 2) != 0;
        pendingAndStale += (flags & 3) == 3;
    }
};
struct RevocationSite {
    RevocationCounts counts;
    std::map<int, RevocationCounts> groups;
    std::uint64_t maxBatchCommands = 0, maxPendingStepSpan = 0;
};
// reason, hook phase, measured spike phase, provenance, module, caller RVA, parent RVA
using RevocationSiteKey = std::tuple<std::string, std::string, std::string,
    std::string, std::string, std::uintptr_t, std::uintptr_t>;
struct SetterEvent {
    setter_audit::Snapshot entry, before, after;
    std::uint64_t step = 0, frame = 0;
    ObjectId id = 0;
    std::uintptr_t caller = 0, parent = 0;
    const char* provenance = "unresolved", *module = "unknown", *phase = "outside_frame";
    std::uint32_t inputX = 0, inputY = 0, nativeX = 0, nativeY = 0;
    unsigned groupMask = 0, cursorFlags = 0;
    bool visibility = false, beforeCaptured = false, completed = false, retained = false;
    bool shadowAccepted = false, shadowRepeat = false;
};
struct SetterShadowSample {
    ObjectId id;
    std::uint64_t frame, step, levelTime, playerX, playerY;
    setter_audit::Snapshot snapshot;
    bool owned, retainedOnce;
    std::uint64_t retentionCount = 0, firstRetentionStep = 0;
    std::uint64_t stepSignature = 0, stateSignature = 0, passOrdinal = 0;
};
struct AlignedShadowIdentity {
    std::uint64_t sourceIndex = 0, groupHash = 0;
    int objectKind = 0;
    bool control = false;
};
struct HyperReason {
    std::uint64_t attempted = 0, retained = 0, emergencyReleased = 0, unmodeled = 0;
    std::unordered_set<ObjectId> objects;
    std::map<int, std::uint64_t> groups;
    std::uint64_t firstStep = 0, lastStep = 0;
};
struct HyperFailure {
    std::string reason, field;
    ObjectId object;
    std::uint64_t step, command, expected, actual;
    int group;
};
struct VisibilityReadCounts { std::uint64_t calls = 0, pending = 0, stale = 0, synchronized = 0; };
struct PivotlessRule {
    unsigned rotationMask = 0; // angles X/Y then offsets X/Y, additive float operations
    unsigned dirtySet = 0;
    unsigned finishMode = 0; // 0 preserve, 1 clear, 2 set
};
unsigned pivotlessClass(GameObject* object) {
    return unsigned(bool(object->m_canRotateFree)) | (unsigned(bool(object->m_isDecoration2)) << 1);
}
unsigned rotationDirtyBits(GameObject* object) {
    return (object->m_isDirty ? 1u : 0u) | (object->m_isObjectPosDirty ? 2u : 0u) |
        (object->m_isUnmodifiedPosDirty ? 4u : 0u) | (object->m_isObjectRectDirty ? 8u : 0u) |
        (object->m_isOrientedBoxDirty ? 16u : 0u);
}
void setRotationDirtyBits(GameObject* object, unsigned bits) {
    object->m_isDirty = (bits & 1u) != 0;
    object->m_isObjectPosDirty = (bits & 2u) != 0;
    object->m_isUnmodifiedPosDirty = (bits & 4u) != 0;
    object->m_isObjectRectDirty = (bits & 8u) != 0;
    object->m_isOrientedBoxDirty = (bits & 16u) != 0;
}
struct Live final : NativeSink, RenderSink {
    PlayLayer* layer = nullptr;
    std::optional<Scene> scene;
    std::unique_ptr<GlRenderer> renderer;
    std::unique_ptr<FrameGpuTimer> gpuTimer;
    std::unique_ptr<FrameGpuTimer> residentTimer;
    std::vector<gpu::ResidentTranslation> residentJournal, residentPending;
    std::uint32_t residentSubmittedJournalEnd = 0;
    RevocationCounts revocationTotals;
    std::map<RevocationSiteKey, RevocationSite> revocationSites;
    std::map<int, std::map<unsigned, std::uint64_t>> initialParentMasks;
    double revocationAuditMs = 0;
    std::vector<SetterEvent> setterEvents;
    std::map<std::string_view, VisibilityReadCounts> visibilityReads;
    double setterAuditMs = 0;
    // Diagnostic-only experiment. Native reference leaves just the selected group
    // unowned from scene compilation; every other ownership policy is unchanged.
    std::vector<ObjectId> setterShadowTargets, setterShadowSamples;
    std::map<ObjectId, std::array<std::uint64_t, 2>> setterShadowInitial;
    std::map<ObjectId, AlignedShadowIdentity> alignedShadowIdentities;
    std::uint64_t alignedClockFrames = 0, alignedPassOrdinal = 0,
        alignedLastSampleStep = std::numeric_limits<std::uint64_t>::max(),
        alignedStepSignature = UINT64_C(14695981039346656037);
    std::vector<SetterShadowSample> setterShadowFrames;
    std::map<std::string_view, std::uint64_t> setterShadowRejected;
    std::uint64_t shadowRetained = 0, shadowRepeat = 0, shadowLaterStep = 0, shadowUnique = 0,
        shadowPostFailures = 0, shadowReferenceChecks = 0, shadowReferenceMismatch = 0,
        shadowVisibilityChecks = 0, shadowVisibilityMismatch = 0, shadowVisibilitySyncs = 0,
        shadowBeforeVisibilityStale = 0;
    bool shadowDisabled = false;
    std::map<std::string, HyperReason> hyperReasons;
    std::optional<HyperFailure> hyperFirstFailure;
    std::uint64_t hyperUnmodeled = 0, hyperEmergency = 0, hyperSetterChecks = 0,
        hyperSetterFailures = 0, hyperPivotlessChecks = 0, hyperPivotlessFailures = 0,
        hyperAngleChecks = 0, hyperAngleFailures = 0;
    std::array<PivotlessRule, 4> pivotlessRules{};
    bool pivotlessProbed = false, pivotlessProbePassed = false, pivotlessAbort = false;
    std::uint64_t pivotlessCommands = 0, pivotlessGpuRecipients = 0, pivotlessNativeCalls = 0,
        pivotlessBatches = 0, pivotlessSectionEvents = 0, pendingPivotlessCommands = 0,
        pivotlessNativeMaterializations = 0;
    bool hyperNegativeInjected = false, hyperNegativeCaught = false, hyperTearingDown = false;
    ObjectId hyperNegativeObject = std::numeric_limits<ObjectId>::max();
    std::vector<ObjectId> hyperRebaseWatch;
    std::unordered_set<ObjectId> hyperRebaseObjects, hyperPreVisibilityDiverged;
    std::uint64_t hyperRebases = 0, hyperRebaseChecks = 0, hyperRebaseFailures = 0,
        hyperRebaseBeforeVisibility = 0, hyperRebaseDuringVisibility = 0;
    bool hyperVisibilityActive = false, perfFrameStartedReady = false;
    std::uint64_t hyperPerfRebaseRejected = 0;
    Clock::time_point perfPreviousFrameStart{};
    std::vector<double> perfIntervals;
    struct PerfFrame { std::uint64_t steps; double intervalMs, wallMs, levelTime; };
    std::vector<PerfFrame> perfTrace;
    struct NativeVisibilityFrame {
        std::uint64_t index = 0, simulationSteps = 0, spriteTransformVisits = 0,
            geometrySuppressedVisits = 0, nativeSectionCallbacks = 0, residentSectionTransitions = 0;
        std::array<double, spike::phaseCount> phaseExclusiveMs{};
        std::array<std::uint64_t, spike::eventCount> events{};
    };
    std::vector<NativeVisibilityFrame> nativeVisibilityFrames;
    struct VisibilityOwnershipMismatch {
        ObjectId object = UINT32_MAX;
        std::uint64_t frame = 0, step = 0;
        VisibilitySection nativeSection{-1, -1}, indexedSection{-1, -1};
        unsigned nativeSpace = invalidVisibilitySpace, indexedSpace = invalidVisibilitySpace;
        bool nativeVisible = false, candidate = false, activated = false,
            groupDisabled = false, groupDisabledTemp = false, hide = false, batchAttached = false;
        double x = 0, y = 0;
    };
    struct VisibilityOwnershipFrame {
        std::uint64_t index = 0, simulationSteps = 0, candidates = 0, nativeVisibleOwned = 0,
            coveredVisible = 0, candidateFalsePositives = 0, candidateFalseNegatives = 0,
            candidateEntries = 0, candidateExits = 0, cameraEntries = 0, cameraExits = 0,
            objectEntries = 0, objectExits = 0, nativeEntries = 0, nativeExits = 0,
            candidateEntryMismatches = 0, candidateExitMismatches = 0,
            ownedBatchAttachments = 0, ownedBatchDetachments = 0, ownedSectionTransitions = 0;
        bool candidateCoverageAvailable = false;
        std::array<double, spike::phaseCount> phaseExclusiveMs{};
        std::array<std::uint64_t, spike::eventCount> events{};
    };
    struct VisibilityOwnershipShadow {
        VisibilityOwnershipIndex sectionIndex, index;
        bool initialized = false, passObserved = false, candidateCoverageAvailable = false;
        std::unordered_set<ObjectId> nativeVisibleOwned, candidates, previousCandidates,
            candidateEntered, candidateExited, nativeEntered, nativeExited, sectionChangedThisFrame,
            sectionMismatchObjects;
        std::vector<std::uint64_t> lastDetachFrame;
        std::vector<VisibilityOwnershipFrame> frames;
        std::uint64_t ownedAtInitialization = 0, sectionIndexedAtInitialization = 0,
            eligibleAtInitialization = 0,
            unsupportedAtInitialization = 0, candidatePasses = 0, candidateObservations = 0,
            nativeVisibleObservations = 0, coveredVisibleObservations = 0,
            candidateFalsePositiveObservations = 0, candidateFalseNegativeObservations = 0,
            candidateEntries = 0, candidateExits = 0, cameraEntries = 0, cameraExits = 0,
            objectEntries = 0, objectExits = 0, nativeEntries = 0, nativeExits = 0,
            candidateEntryMismatches = 0, candidateExitMismatches = 0,
            nativeVisibilityTransitions = 0, ownedBatchAttachments = 0, ownedBatchDetachments = 0,
            ownedSectionRegistrations = 0, ownedSectionTransitions = 0,
            sectionMismatchObservations = 0, negativeControlChecks = 0, negativeControlCaught = 0;
        std::uint64_t frameCandidates = 0, frameNativeVisibleOwned = 0, frameCoveredVisible = 0,
            frameCandidateFalsePositives = 0, frameCandidateFalseNegatives = 0,
            frameCandidateEntries = 0, frameCandidateExits = 0, frameCameraEntries = 0,
            frameCameraExits = 0, frameObjectEntries = 0, frameObjectExits = 0,
            frameNativeEntries = 0, frameNativeExits = 0, frameCandidateEntryMismatches = 0,
            frameCandidateExitMismatches = 0, frameOwnedBatchAttachments = 0,
            frameOwnedBatchDetachments = 0, frameOwnedSectionTransitions = 0;
        std::unordered_map<std::string, std::uint64_t> detachReattachHistogram;
        std::optional<VisibilityOwnershipMismatch> firstCandidateFalsePositive,
            firstCandidateFalseNegative, firstCandidateEntryMismatch, firstCandidateExitMismatch,
            firstSectionMismatch, firstNegativeControl;
        ObjectId negativeControlObject = UINT32_MAX;
    } visibilityOwnership;
    std::uint64_t perfInitialTicks = 0, fixedProfileClockFrames = 0;
    double perfCurrentIntervalMs = 0;
    std::map<std::string_view, std::uint64_t> profilePivotlessMirrors;
    VisibilityMirrorWorklist mirrorWorklist;
    std::uint64_t mirrorPasses = 0, mirrorEmptyPasses = 0, mirrorFullScanSlots = 0,
        mirrorChainMarks = 0, mirrorChainDrains = 0, mirrorWordMerges = 0,
        mirrorCandidateVisits = 0, mirrorMaterializations = 0,
        mirrorAuditPasses = 0, mirrorAuditSlots = 0, mirrorAuditMisses = 0;
    std::uint64_t mirrorLastAuditStep = 0;
    bool mirrorFailed = false, mirrorNegativeInjected = false, mirrorNegativeCaught = false;
    ChainId mirrorNegativeChain = UINT32_MAX;
    struct MirrorMiss { ObjectId object; ChainId chain; std::uint32_t applied, expected; std::uint64_t step, command; };
    std::optional<MirrorMiss> mirrorFirstMiss;
    struct CensusSample { ObjectId id; VisibilityRegion oldRegion, region; bool visible; };
    struct CensusBucket {
        std::uint64_t samples = 0, visibleBefore = 0, paired = 0, visibleAfter = 0, entered = 0, exited = 0;
    };
    std::array<CensusBucket, static_cast<unsigned>(VisibilityRegion::Count)> visibilityBuckets{};
    std::vector<CensusSample> visibilitySamples;
    std::unordered_map<CCNode*, CensusBounds> visibilityCameras;
    std::vector<std::uint64_t> visibilityLastFrame;
    std::uint64_t visibilityCensusEvents = 0, visibilityRepeatInFrame = 0, visibilityUnpaired = 0,
        visibilityFarHiddenBoth = 0, visibilityStableFarHidden = 0;
    struct FilterSpace {
        Ref<CCNode> node;
        double radius = 0;
        CensusBounds previous{};
        bool hasPrevious = false;
        VisibilitySectionRange range{};
    };
    VisibilitySectionFilter visibilityDeferred;
    std::vector<PackedFilterMetadata> packedFilterMetadata;
    std::vector<FilterSpace> filterSpaces;
    std::map<std::pair<CCNode*, unsigned>, unsigned> filterSpaceLookup;
    std::vector<ObjectId> filterGeometryQueue;
    std::array<std::uint64_t, static_cast<unsigned>(FilterClass::Count)> filterInitialClasses{}, filterCurrentClasses{},
        filterInitialHeadlessClasses{}, filterUnsupportedVisits{};
    std::array<std::uint64_t, static_cast<unsigned>(FilterChange::Count)> filterGeometryEvents{};
    std::array<std::uint64_t, static_cast<unsigned>(FilterDecision::Count)> filterDecisions{};
    std::uint64_t filterInitialEligible = 0, filterInitialHeadless = 0, filterInitialHeadlessEligible = 0,
        filterReclassifications = 0, filterReadmitted = 0, filterSpaceChanges = 0, filterGeometryForced = 0;
    bool filterInitialized = false, filterTrackingLost = false, filterFailed = false;
    std::uint64_t filterEligible = 0, filterDeferredVisits = 0, filterEpisodes = 0,
        filterCameraCandidates = 0, filterResumed = 0, filterConsumerResumes = 0,
        filterSectionMoves = 0, filterInvalidations = 0, filterLateVisible = 0,
        filterAuditChecks = 0, filterAuditFailures = 0, filterPeakDeferred = 0;
    std::uint64_t filterCheckpointStep = UINT64_MAX, filterCheckpoints = 0, filterCheckpointObjects = 0;
    struct FilterMiss { ObjectId object; std::uint64_t step, command; const char* reason; };
    std::optional<FilterMiss> filterFirstMiss;
    struct PackedFilterMismatch {
        ObjectId object;
        std::uint64_t step, command;
        FilterDecision packed, reference;
        VisibilitySection cachedCurrent, cachedOld, nativeCurrent, nativeOld;
        unsigned cachedSpace, nativeSpace;
        std::uint8_t cachedFlags;
        FilterClass nativeClass;
        bool nativeGeometryQueued, nativeEdit, nativeVisible, nativeOwned;
        double historyBaseX, historyBaseY;
    };
    std::optional<PackedFilterMismatch> packedFilterFirstMismatch;
    std::uint64_t packedFilterPredicateComparisons = 0, packedFilterPredicateMismatches = 0;
    ObjectId filterNegativeObject = UINT32_MAX;
    bool filterNegativeCaught = false;
    struct FilterTrace {
        std::uint64_t step, command, visibility, logical, sprite, state, visibleCount, relevantCount;
    };
    std::vector<FilterTrace> filterTrace;
    std::uint32_t replayCapacity = GlRenderer::defaultReplayCapacity;
    std::size_t replaySectionCapacity = 1u << 20;
    std::uint64_t pendingFirstStep = 0, pendingLastStep = 0;
    std::uint64_t replayBatches = 0, replayCommands = 0, replayMultiStepBatches = 0,
        replayMultiStepCommands = 0, replayMaxCommands = 0, replayMaxStepSpan = 0,
        replayMaxSectionPotential = 0, replayMaxSectionEvents = 0;
    std::map<std::string_view, std::uint64_t> replayFlushReasons;
    double nativeInitMs = 0, descriptorCompileMs = 0, legacyPrecompileMs = 0, sceneCompileMs = 0;
    struct DescriptorGroup {
        std::uint64_t moves = 0, rotations = 0, staticParams = 0, dynamicParams = 0,
            touch = 0, spawn = 0, pivot = 0, memberCount = 0;
        double maxDuration = 0;
    };
    std::map<int, DescriptorGroup> descriptorGroups;
    std::vector<ObjectId> residentRetired;
    std::vector<ObjectId> nativeEdits;
    std::vector<gpu::ResidentTranslation> residentRebases;
    unsigned nativeEditDepth = 0;
    bool persistentEdits = false;
    bool groupRotation = false, rotationClaim = false, flushingResidents = false;
    std::size_t sectionPotential = 0;
    std::uint64_t groupRotationCommands = 0, groupRotationRecipients = 0, groupRotationNativeRecipients = 0;
    std::uint64_t sectionEventBatches = 0, sectionEventReadbackBytes = 0;
    double sectionEventMs = 0, residentDispatchWallMs = 0, residentSectionCommitWallMs = 0;
    std::uint64_t retainedNativeEdits = 0, retainedPositionRebases = 0, retainedGeometryChanges = 0;
    std::uint64_t retainedRebaseBatches = 0, retainedReentrantFallbacks = 0;
    bool residentEnabled = false;
    bool aggressive = false;
    std::vector<SectionIndex> sectionIndices;
    float sectionXFactor = 0, sectionYFactor = 0;
    std::map<std::string_view, std::uint64_t> residentMirrors;
    struct NativeCallerHistogram {
        std::uint64_t calls = 0, samples = 0;
        // First and second GD executable return PCs, as ASLR-independent RVAs.
        std::map<std::pair<std::uintptr_t, std::uintptr_t>, std::uint64_t> callers;
    };
    std::array<NativeCallerHistogram, 3> nativeCallerHistograms{};
    std::uint64_t residentSectionCandidates = 0, residentSectionCommits = 0, residentNativeApplies = 0;
    std::uint64_t residentValidationChecks = 0, residentExactnessViolations = 0;
    std::size_t residentValidatedActions = 0;
    double residentHandoffMs = 0;
    double residentSyncMs = 0;
    std::map<std::string_view, double> residentSyncByReason;
    bool audit = false, lazyRoutes = false;
    bool spikeStarted = false;
    int auditRotationTarget = -1;
    unsigned auditNativeDepth = 0;
    std::uint64_t auditUncompiledVisits = 0, auditSectionSamples = 0;
    double auditSectionSampleMs = 0;
    double auditAttributionMs = 0;
    AuditTimes auditTimes, auditWindowStart, auditWindowEnd;
    double auditWindowStartTime = 0, auditWindowEndTime = 0;
    std::map<std::pair<int, bool>, RotationAudit> auditRotationClaims;
    std::map<int, RotationAudit> auditRotationArrays;
    std::map<std::string, std::uint64_t> auditSetterPhases;
    std::uint64_t auditRevocationsThisFrame = 0, auditMaxRevocationsPerFrame = 0;
    double auditMaxHandoffPerFrame = 0, auditPreviousHandoff = 0;
    HGLRC context = nullptr;
    std::vector<Entry> entries;
    std::vector<Route> routes;
    std::vector<Chain> chains;
    std::unordered_map<CCArray*, std::size_t> routeIndices;
    std::unordered_map<CCSprite*, ObjectId> ids;
    std::vector<ObjectId> denseIds;
    std::int64_t denseIdBase = 0;
    std::vector<std::uint8_t> moveSectionPolicies;
    std::uint64_t movePolicySceneGeneration = 0;
    CCArray* recentMoveArray = nullptr;
    std::size_t recentMoveRoute = std::numeric_limits<std::size_t>::max();
    std::uint64_t recentMoveSceneGeneration = 0;
    std::unordered_map<CCTextureAtlas*, Atlas> atlases;
    std::map<std::string, std::uint64_t> rejected, transitions;
    std::map<std::string, std::uint64_t> inactiveReadiness;
    std::map<int, std::uint64_t> unknownTriggers;
    std::vector<std::string> unknownMods;
    std::map<int, std::uint64_t> colorTargets, shakeTargets;
    std::uint64_t colorCopy = 0, triggeredBySpawn = 0, triggeredByTouch = 0;
    Statistics stats;
    bool compiling = false, internal = false, drawing = false, ready = false, visiblePass = false;
    bool assessed = false, baseline = false, validate = false, cachedPoliciesEnabled = false;
    std::uint64_t order = 0, frames = 0, ticks = 0, eligible = 0, total = 0;
    Clock::time_point frameStart;
    std::vector<double> cpuFrames;
    std::string levelName;
    std::optional<matjson::Value> capture;
    bool gpuStarted = false, frameCaptured = false;
    bool testCompleted = false;
    unsigned retainedAtCompletion = 0, headlessRetainedAtCompletion = 0;
    bool structuralHandoffTest = false, structuralHandoffOwned = false;
    std::vector<GameObject*> structuralHandoffObjects;
    unsigned toggleFixturePhase = 0;
    bool residentAtlasFixtureMoved = false;
    std::vector<Ref<GameObject>> toggleFixtureObjects;
    std::optional<matjson::Value> toggleFixtureTrace;
    Clock::time_point progressStart;
    std::vector<double> cpuOwnedFrames;
    std::vector<double> cpuBenchmarkFrames;
    Statistics benchmarkStart{}, benchmarkEnd{};
    bool benchmarkStarted = false, benchmarkFinished = false;
    std::vector<std::uint64_t> censusMasks;
    std::vector<std::string> initialMoveRejections;
    std::vector<std::uint16_t> censusProtectionCauses;
    std::uint64_t censusCandidateChains = 0;
    std::uint64_t censusV1ParentCandidateChains = 0;
    std::uint64_t censusStructuralParentChains = 0;
    std::string gpuStatus = "unavailable";
    CCTextureAtlas* drawingAtlas = nullptr;
    void updateResidentTape(Chain& chain) {
        if (chain.residentTapeRevision == chain.lastResidentAction) return;
        const auto first = chain.residentTape.size();
        for (auto group : chain.residentGroups) {
            const auto& tape = routes[group - 1].residentActions;
            chain.residentTape.insert(chain.residentTape.end(),
                std::lower_bound(tape.begin(), tape.end(), chain.residentTapeRevision), tape.end());
        }
        std::sort(chain.residentTape.begin() + first, chain.residentTape.end());
        chain.residentTapeRevision = chain.lastResidentAction;
    }
    struct Replay {
        Translation position;
        Translation previous;
        std::uint32_t stamp;
        bool invalidateRect = false;
        float rotationX = 0, rotationY = 0, offsetX = 0, offsetY = 0;
        bool rotated = false, finished = false;
        bool positionTouched = false, pivotlessOrientation = false, pivotlessTouched = false, writesFinished = false;
        unsigned pivotlessDirty = 0;
    };
    Replay replayResident(ObjectId id) {
        auto& entry = entries[id];
        auto& chain = chains[scene->chainFor(scene->handle(id))];
        auto* object = entry.object.data();
        updateResidentTape(chain);
        Replay result{entry.historyBase, entry.initialPrevious, entry.residentStamp};
        result.rotationX = object->getRotationX();
        result.rotationY = object->getRotationY();
        result.offsetX = object->m_rotationXOffset;
        result.offsetY = object->m_rotationYOffset;
        for (auto action = std::lower_bound(chain.residentTape.begin(), chain.residentTape.end(),
                entry.residentApplied); action != chain.residentTape.end(); ++action) {
            const auto& command = residentJournal[*action];
            if (command.kind == 2) {
                result.pivotlessTouched = true;
                const auto& rule = pivotlessRules[pivotlessClass(object)];
                if (rule.rotationMask & 1u) result.rotationX += command.orientation;
                if (rule.rotationMask & 2u) result.rotationY += command.orientation;
                if (rule.rotationMask & 4u) result.offsetX += command.orientation;
                if (rule.rotationMask & 8u) result.offsetY += command.orientation;
                result.pivotlessOrientation |= rule.rotationMask != 0;
                result.pivotlessDirty |= rule.dirtySet;
                if (rule.finishMode) { result.writesFinished = true; result.finished = rule.finishMode == 2; }
                continue; // no previous-position, command-stamp or section effects
            }
            result.positionTouched = true;
            if (result.stamp != command.stamp) {
                if (!object->m_isDecoration2) {
                    result.previous = {static_cast<float>(result.position.x), static_cast<float>(result.position.y)};
                    result.invalidateRect = true;
                    result.stamp = command.stamp;
                } else if (command.flags & gpu::residentDecorationStamp) {
                    result.stamp = command.stamp;
                }
            }
            if (command.kind == 1) {
                gpu::residentRotatePosition(result.position.x, result.position.y, command);
                result.rotated = true;
                result.writesFinished = true;
                result.finished = (command.flags & gpu::residentRotationFinished) != 0;
                if (object->m_canRotateFree && command.orientation != 0.f) {
                    result.rotationX += command.orientation;
                    result.rotationY += command.orientation;
                    result.offsetX += command.orientation;
                    result.offsetY += command.orientation;
                }
            } else {
                if (command.dx != 0) result.position.x += command.dx;
                if (command.dy != 0) result.position.y += command.dy;
            }
        }
        return result;
    }
    void applyResident(ObjectId id, const Replay& result) {
        auto& entry = entries[id];
        auto* object = entry.object.data();
        if (result.invalidateRect) {
            object->m_lastPosition = CCPoint(static_cast<float>(result.previous.x), static_cast<float>(result.previous.y));
            object->dirtifyObjectRect();
        }
        if (result.positionTouched) {
            object->m_unk4C4 = static_cast<int>(result.stamp);
            object->m_positionX = result.position.x;
            object->m_positionY = result.position.y;
        }
        if (result.rotated || result.pivotlessOrientation) {
            const bool previous = internal;
            internal = true;
            const auto dirtyBefore = rotationDirtyBits(object);
            if (std::bit_cast<std::uint32_t>(object->getRotationX()) != std::bit_cast<std::uint32_t>(result.rotationX)) object->setRotationX(result.rotationX);
            if (std::bit_cast<std::uint32_t>(object->getRotationY()) != std::bit_cast<std::uint32_t>(result.rotationY)) object->setRotationY(result.rotationY);
            object->m_rotationXOffset = result.offsetX;
            object->m_rotationYOffset = result.offsetY;
            if (result.rotated) {
                object->m_isDirty = true;
                object->m_isUnmodifiedPosDirty = true;
            } else setRotationDirtyBits(object, dirtyBefore | result.pivotlessDirty);
            internal = previous;
        }
        if (result.writesFinished) object->m_unk4fb = result.finished;
        if (result.positionTouched) object->dirtifyObjectPos();
        if (result.pivotlessDirty) setRotationDirtyBits(object, rotationDirtyBits(object) | result.pivotlessDirty);
        entry.historyBase = result.position;
        filterCacheOldSection(id);
        entry.initialPrevious = result.previous;
        entry.residentStamp = result.stamp;
        entry.residentApplied = chains[scene->chainFor(scene->handle(id))].lastResidentAction;
    }
    bool syncResident(ObjectId id, const char* reason, bool sprite = false) {
        auto& entry = entries[id];
        if (entry.nativeEdit) return false; // Native state is authoritative within this scope.
        const auto last = chains[scene->chainFor(scene->handle(id))].lastResidentAction;
        if (envFlags().visibilityFilter && visibilityDeferred.erase(id)) {
            if (std::string_view(reason) == "hyper_before_visibility") ++filterResumed;
            else ++filterConsumerResumes;
        }
        if (entry.residentApplied == last && (!sprite || entry.spriteApplied == last)) return false;
        const bool measure = !envFlags().performanceOnly || envFlags().fixedProfile;
        const auto started = measure ? Clock::now() : Clock::time_point{};
        spike::Scope profile(spike::Phase::residentSync);
        if (entry.residentApplied != last) {
            // Native consumers can update cached history between reads.
            entry.initialPrevious = {entry.object->m_lastPosition.x, entry.object->m_lastPosition.y};
            entry.residentStamp = static_cast<std::uint32_t>(entry.object->m_unk4C4);
            const auto previousApplied = entry.residentApplied;
            const auto replay = replayResident(id);
            applyResident(id, replay);
            if (replay.pivotlessTouched) {
                ++pivotlessNativeMaterializations;
                if (envFlags().fixedProfile) ++profilePivotlessMirrors[reason];
            }
            // An angle-only action does not invalidate an already-current sprite position.
            if (!replay.positionTouched && entry.spriteApplied == previousApplied) entry.spriteApplied = last;
            ++residentNativeApplies;
        }
        if (sprite && entry.spriteApplied != last) {
            entry.object->cocos2d::CCSprite::setPosition(CCPoint(
                static_cast<float>(entry.historyBase.x), static_cast<float>(entry.historyBase.y)));
            entry.spriteApplied = last;
        }
        if (measure) {
            ++residentMirrors[reason];
            const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
            residentSyncMs += elapsed;
            if (audit || envFlags().fixedProfile) residentSyncByReason[reason] += elapsed;
        }
        return true;
    }
    void markMirrorChain(ChainId id) {
        if (envFlags().hyperAggressive && chains[id].owned && mirrorWorklist.mark(id)) ++mirrorChainMarks;
    }
    bool mirrorStale(ObjectId id) {
        const auto& entry = entries[id];
        return entry.owned && !entry.nativeEdit &&
            entry.residentApplied != chains[scene->chainFor(scene->handle(id))].lastResidentAction;
    }
    VisibilitySection filterCurrentSection(const Entry& entry) const {
        return {entry.object->m_outerSectionIndex, entry.object->m_middleSectionIndex};
    }
    VisibilityOwnershipMismatch ownershipMismatch(ObjectId id, bool candidate) {
        const auto& entry = entries[id];
        auto* object = entry.object.data();
        VisibilityOwnershipMismatch mismatch;
        mismatch.object = id;
        mismatch.frame = frames + 1;
        mismatch.step = ticks;
        mismatch.nativeSection = filterCurrentSection(entry);
        mismatch.indexedSection = visibilityOwnership.sectionIndex.objectSection(id);
        mismatch.nativeSpace = entry.visibilitySpace;
        mismatch.indexedSpace = visibilityOwnership.index.objectSpace(id);
        mismatch.nativeVisible = visibilityOwnership.nativeVisibleOwned.contains(id);
        mismatch.candidate = candidate;
        mismatch.activated = object->m_isActivated;
        mismatch.groupDisabled = object->m_isGroupDisabled;
        mismatch.groupDisabledTemp = object->m_isGroupDisabledTemp;
        mismatch.hide = object->m_isHide;
        mismatch.batchAttached = object->getBatchNode() != nullptr;
        mismatch.x = object->m_positionX;
        mismatch.y = object->m_positionY;
        return mismatch;
    }
    void updateVisibilityOwnershipIndex(ObjectId id) {
        auto& shadow = visibilityOwnership;
        if (!shadow.initialized || id >= entries.size()) return;
        const auto& entry = entries[id];
        const bool wasIndexed = shadow.sectionIndex.indexed(id);
        const auto oldSection = shadow.sectionIndex.objectSection(id);
        const auto section = filterCurrentSection(entry);
        const auto nativeSectionSpace = entry.owned ? 0u : invalidVisibilitySpace;
        shadow.sectionIndex.set(id, nativeSectionSpace, section);
        if (wasIndexed && entry.owned && oldSection != section) {
            ++shadow.ownedSectionTransitions;
            ++shadow.frameOwnedSectionTransitions;
            shadow.sectionChangedThisFrame.insert(id);
        }
        const auto space = entry.owned && entry.visibilityClass == FilterClass::Eligible
            ? entry.visibilitySpace : invalidVisibilitySpace;
        shadow.index.set(id, space, section);
    }
    void initializeVisibilityOwnershipShadow() {
        auto& shadow = visibilityOwnership;
        if (!envFlags().visibilityOwnershipShadow) return;
        shadow.sectionIndex.initialize(entries.size(), 1);
        shadow.index.initialize(entries.size(), filterSpaces.size());
        shadow.nativeVisibleOwned.clear();
        shadow.nativeVisibleOwned.reserve(owned());
        shadow.candidates.reserve(owned());
        shadow.previousCandidates.reserve(owned());
        shadow.candidateEntered.reserve(owned() / 8 + 16);
        shadow.candidateExited.reserve(owned() / 8 + 16);
        shadow.nativeEntered.reserve(owned() / 8 + 16);
        shadow.nativeExited.reserve(owned() / 8 + 16);
        shadow.sectionChangedThisFrame.reserve(owned() / 16 + 16);
        shadow.sectionMismatchObjects.reserve(16);
        shadow.lastDetachFrame.assign(entries.size(), UINT64_MAX);
        shadow.frames.reserve(1200);
        shadow.ownedAtInitialization = owned();
        shadow.sectionIndexedAtInitialization = 0;
        shadow.eligibleAtInitialization = 0;
        shadow.unsupportedAtInitialization = 0;
        for (ObjectId id = 0; id < entries.size(); ++id) {
            auto& entry = entries[id];
            if (!entry.owned) continue;
            if (entry.object->isVisible()) shadow.nativeVisibleOwned.insert(id);
            if (shadow.sectionIndex.set(id, 0, filterCurrentSection(entry)))
                ++shadow.sectionIndexedAtInitialization;
            if (entry.visibilityClass == FilterClass::Eligible &&
                entry.visibilitySpace != invalidVisibilitySpace &&
                shadow.index.set(id, entry.visibilitySpace, filterCurrentSection(entry))) {
                ++shadow.eligibleAtInitialization;
            } else {
                ++shadow.unsupportedAtInitialization;
            }
        }
        shadow.initialized = true;
        shadow.candidateCoverageAvailable = filterInitialized && !filterTrackingLost && !filterSpaces.empty();
    }
    void removeVisibilityOwnership(ObjectId id) {
        auto& shadow = visibilityOwnership;
        if (!shadow.initialized || id >= entries.size()) return;
        shadow.sectionIndex.erase(id);
        shadow.index.erase(id);
        shadow.nativeVisibleOwned.erase(id);
        shadow.candidates.erase(id);
        shadow.previousCandidates.erase(id);
        shadow.candidateEntered.erase(id);
        shadow.candidateExited.erase(id);
        shadow.nativeEntered.erase(id);
        shadow.nativeExited.erase(id);
        shadow.sectionChangedThisFrame.erase(id);
    }
    void recordVisibilityOwnershipNativeState(ObjectId id, bool visible) {
        auto& shadow = visibilityOwnership;
        if (!shadow.initialized || id >= entries.size() || !entries[id].owned) return;
        const bool wasVisible = shadow.nativeVisibleOwned.contains(id);
        if (visible == wasVisible) return;
        ++shadow.nativeVisibilityTransitions;
        if (visible) {
            shadow.nativeVisibleOwned.insert(id);
            shadow.nativeEntered.insert(id);
            ++shadow.nativeEntries;
            ++shadow.frameNativeEntries;
        } else {
            shadow.nativeVisibleOwned.erase(id);
            shadow.nativeExited.insert(id);
            ++shadow.nativeExits;
            ++shadow.frameNativeExits;
        }
    }
    void recordVisibilityOwnershipBatchChange(ObjectId id, CCSpriteBatchNode* oldBatch,
            CCSpriteBatchNode* newBatch) {
        auto& shadow = visibilityOwnership;
        if (!shadow.initialized || id >= entries.size() || !entries[id].owned || oldBatch == newBatch) return;
        const auto frame = frames + 1;
        if (oldBatch) {
            ++shadow.ownedBatchDetachments;
            ++shadow.frameOwnedBatchDetachments;
            if (id < shadow.lastDetachFrame.size()) shadow.lastDetachFrame[id] = frame;
        }
        if (newBatch) {
            ++shadow.ownedBatchAttachments;
            ++shadow.frameOwnedBatchAttachments;
            if (id < shadow.lastDetachFrame.size() && shadow.lastDetachFrame[id] != UINT64_MAX) {
                const auto interval = frame - shadow.lastDetachFrame[id];
                const char* bucket = interval == 0 ? "same_frame" : interval == 1 ? "1_frame" :
                    interval <= 3 ? "2_3_frames" : interval <= 7 ? "4_7_frames" :
                    interval <= 15 ? "8_15_frames" : interval <= 30 ? "16_30_frames" : "31_plus_frames";
                ++shadow.detachReattachHistogram[bucket];
                shadow.lastDetachFrame[id] = UINT64_MAX;
            }
        }
    }
    void auditVisibilityOwnershipShadow() {
        auto& shadow = visibilityOwnership;
        if (!envFlags().visibilityOwnershipShadow || !shadow.initialized) return;
        shadow.passObserved = true;
        shadow.candidateCoverageAvailable = filterInitialized && !filterTrackingLost && !filterSpaces.empty();
        shadow.candidates.clear();
        shadow.candidateEntered.clear();
        shadow.candidateExited.clear();
        if (shadow.candidateCoverageAvailable) {
            filterDrainReclassifications();
            filterRefreshWindows();
            for (unsigned space = 0; space < filterSpaces.size(); ++space)
                shadow.index.collect(space, filterSpaces[space].range, [&](ObjectId id) {
                    if (id < entries.size() && entries[id].owned) shadow.candidates.insert(id);
                });
            for (const auto id : shadow.candidates)
                if (!shadow.previousCandidates.contains(id)) shadow.candidateEntered.insert(id);
            for (const auto id : shadow.previousCandidates)
                if (!shadow.candidates.contains(id) && id < entries.size() && entries[id].owned)
                    shadow.candidateExited.insert(id);

            shadow.candidatePasses++;
            shadow.frameCandidates = shadow.candidates.size();
            shadow.frameNativeVisibleOwned = shadow.nativeVisibleOwned.size();
            shadow.candidateObservations += shadow.candidates.size();
            shadow.nativeVisibleObservations += shadow.nativeVisibleOwned.size();
            std::uint64_t covered = 0;
            for (const auto id : shadow.nativeVisibleOwned) {
                if (shadow.candidates.contains(id)) ++covered;
                else {
                    ++shadow.candidateFalseNegativeObservations;
                    ++shadow.frameCandidateFalseNegatives;
                    if (!shadow.firstCandidateFalseNegative)
                        shadow.firstCandidateFalseNegative = ownershipMismatch(id, false);
                }
            }
            for (const auto id : shadow.candidates) {
                if (!shadow.nativeVisibleOwned.contains(id)) {
                    ++shadow.candidateFalsePositiveObservations;
                    ++shadow.frameCandidateFalsePositives;
                    if (!shadow.firstCandidateFalsePositive)
                        shadow.firstCandidateFalsePositive = ownershipMismatch(id, true);
                }
            }
            shadow.coveredVisibleObservations += covered;
            shadow.frameCoveredVisible = covered;
            shadow.frameCandidateEntries = shadow.candidateEntered.size();
            shadow.frameCandidateExits = shadow.candidateExited.size();
            shadow.candidateEntries += shadow.candidateEntered.size();
            shadow.candidateExits += shadow.candidateExited.size();
            for (const auto id : shadow.candidateEntered) {
                if (shadow.sectionChangedThisFrame.contains(id)) {
                    ++shadow.objectEntries;
                    ++shadow.frameObjectEntries;
                } else {
                    ++shadow.cameraEntries;
                    ++shadow.frameCameraEntries;
                }
                if (!shadow.nativeEntered.contains(id)) {
                    ++shadow.candidateEntryMismatches;
                    ++shadow.frameCandidateEntryMismatches;
                    if (!shadow.firstCandidateEntryMismatch)
                        shadow.firstCandidateEntryMismatch = ownershipMismatch(id, true);
                }
            }
            for (const auto id : shadow.candidateExited) {
                if (shadow.sectionChangedThisFrame.contains(id)) {
                    ++shadow.objectExits;
                    ++shadow.frameObjectExits;
                } else {
                    ++shadow.cameraExits;
                    ++shadow.frameCameraExits;
                }
                if (!shadow.nativeExited.contains(id)) {
                    ++shadow.candidateExitMismatches;
                    ++shadow.frameCandidateExitMismatches;
                    if (!shadow.firstCandidateExitMismatch)
                        shadow.firstCandidateExitMismatch = ownershipMismatch(id, false);
                }
            }
            for (const auto id : shadow.nativeEntered) {
                if (!shadow.candidateEntered.contains(id)) {
                    ++shadow.candidateEntryMismatches;
                    ++shadow.frameCandidateEntryMismatches;
                    if (!shadow.firstCandidateEntryMismatch)
                        shadow.firstCandidateEntryMismatch = ownershipMismatch(id, shadow.candidates.contains(id));
                }
            }
            for (const auto id : shadow.nativeExited) {
                if (!shadow.candidateExited.contains(id)) {
                    ++shadow.candidateExitMismatches;
                    ++shadow.frameCandidateExitMismatches;
                    if (!shadow.firstCandidateExitMismatch)
                        shadow.firstCandidateExitMismatch = ownershipMismatch(id, shadow.candidates.contains(id));
                }
            }
            auto checkSection = [&](ObjectId id) {
                if (id >= entries.size() || !entries[id].owned) return;
                const auto& entry = entries[id];
                const auto nativeSection = filterCurrentSection(entry);
                const bool sectionExpected = nativeSection.first >= 0 && nativeSection.second >= 0;
                const bool sectionIndexed = shadow.sectionIndex.indexed(id);
                const bool candidateExpected = entry.visibilityClass == FilterClass::Eligible &&
                    entry.visibilitySpace != invalidVisibilitySpace && sectionExpected;
                const bool candidateIndexed = shadow.index.indexed(id);
                const bool sectionMismatch = (sectionExpected && (!sectionIndexed ||
                        shadow.sectionIndex.objectSection(id) != nativeSection)) || (!sectionExpected && sectionIndexed);
                const bool candidateMismatch = (candidateExpected && (!candidateIndexed ||
                        shadow.index.objectSpace(id) != entry.visibilitySpace ||
                        shadow.index.objectSection(id) != nativeSection)) || (!candidateExpected && candidateIndexed);
                if (sectionMismatch || candidateMismatch) {
                    if (shadow.sectionMismatchObjects.insert(id).second) ++shadow.sectionMismatchObservations;
                    if (!shadow.firstSectionMismatch)
                        shadow.firstSectionMismatch = ownershipMismatch(id, shadow.candidates.contains(id));
                }
            };
            for (const auto id : shadow.candidates) checkSection(id);
            for (const auto id : shadow.nativeVisibleOwned) checkSection(id);
            // The negative control changes only a local audit bit. Select a
            // candidate already observed native-visible so the injected false
            // negative must pass through the same comparison path.
            if (!shadow.negativeControlCaught) {
                for (const auto id : shadow.nativeVisibleOwned) {
                    if (!shadow.candidates.contains(id)) continue;
                    shadow.negativeControlObject = id;
                    const bool injectedPrediction = false;
                    ++shadow.negativeControlChecks;
                    if (injectedPrediction != shadow.nativeVisibleOwned.contains(id)) {
                        ++shadow.negativeControlCaught;
                        if (!shadow.firstNegativeControl)
                            shadow.firstNegativeControl = ownershipMismatch(id, injectedPrediction);
                    }
                    break;
                }
            }
            shadow.previousCandidates.swap(shadow.candidates);
            shadow.candidates.clear();
            // Keep only the just-computed count and transitions for the frame;
            // the active candidate set is rebuilt from section bins next pass.
            shadow.frameCandidates = shadow.previousCandidates.size();
        } else {
            shadow.previousCandidates.clear();
            shadow.frameCandidates = 0;
            shadow.frameNativeVisibleOwned = shadow.nativeVisibleOwned.size();
            shadow.frameCoveredVisible = 0;
            shadow.frameCandidateFalsePositives = 0;
            shadow.frameCandidateFalseNegatives = 0;
            shadow.frameCandidateEntries = shadow.frameCandidateExits = 0;
            shadow.frameCameraEntries = shadow.frameCameraExits = 0;
            shadow.frameObjectEntries = shadow.frameObjectExits = 0;
            shadow.frameCandidateEntryMismatches = shadow.frameCandidateExitMismatches = 0;
        }
        shadow.sectionChangedThisFrame.clear();
    }
    VisibilitySection filterOldSection(const Entry& entry) const {
        if (!std::isfinite(entry.historyBase.x) || !std::isfinite(entry.historyBase.y)) return {-1, -1};
        return {visibilitySectionCoordinate(entry.historyBase.x, sectionXFactor, nativeSectionLimit),
            visibilitySectionCoordinate(entry.historyBase.y, sectionYFactor, nativeSectionLimit)};
    }
    bool packedFilterMetadataEnabled() const {
        return envFlags().packedVisibilityFilter && packedFilterMetadata.size() == entries.size();
    }
    void filterCacheCurrentSection(ObjectId id) {
        if (!packedFilterMetadataEnabled() || id >= entries.size()) return;
        const auto section = filterCurrentSection(entries[id]);
        auto& metadata = packedFilterMetadata[id];
        metadata.currentOuter = section.first;
        metadata.currentMiddle = section.second;
    }
    void filterCacheOldSection(ObjectId id) {
        if (!packedFilterMetadataEnabled() || id >= entries.size()) return;
        const auto section = filterOldSection(entries[id]);
        auto& metadata = packedFilterMetadata[id];
        metadata.oldOuter = section.first;
        metadata.oldMiddle = section.second;
    }
    void filterCacheState(ObjectId id) {
        if (!packedFilterMetadataEnabled() || id >= entries.size()) return;
        const auto& entry = entries[id];
        auto& metadata = packedFilterMetadata[id];
        metadata.visibilitySpace = entry.visibilitySpace;
        metadata.flags = static_cast<std::uint8_t>((entry.visibilityClass == FilterClass::Eligible ? packedFilterEligible : 0) |
            (entry.visibilityGeometryQueued ? packedFilterGeometryPending : 0) |
            (entry.nativeEdit ? packedFilterNativeEdit : 0));
    }
    void filterCacheAll(ObjectId id) {
        filterCacheCurrentSection(id);
        filterCacheOldSection(id);
        filterCacheState(id);
    }
    VisibilitySection filterCandidateCurrentSection(ObjectId id) const {
        if (packedFilterMetadataEnabled()) {
            const auto& metadata = packedFilterMetadata[id];
            return {metadata.currentOuter, metadata.currentMiddle};
        }
        return filterCurrentSection(entries[id]);
    }
    VisibilitySection filterCandidateOldSection(ObjectId id) const {
        if (packedFilterMetadataEnabled()) {
            const auto& metadata = packedFilterMetadata[id];
            return {metadata.oldOuter, metadata.oldMiddle};
        }
        return filterOldSection(entries[id]);
    }
    unsigned filterCandidateSpace(ObjectId id) const {
        return packedFilterMetadataEnabled() ? packedFilterMetadata[id].visibilitySpace : entries[id].visibilitySpace;
    }
    // Bound all rotations independently of the current mirror angle. Large,
    // composite, skewed or otherwise unfamiliar geometry stays eager.
    FilterClass filterGeometry(const Entry& entry, double& radius) const {
        auto* object = entry.object.data();
        // headless means "no batch/atlas at scene compilation", not a
        // different logical object type. The live isVisible() test still
        // applies, and the stock activation path still owns attachment.
        if (object->m_particle || object->m_hasParticles || object->m_hasAnimatedChild) return FilterClass::Animated;
        if (object->getChildrenCount() || object->m_colorSprite || object->m_glowSprite) return FilterClass::Composite;
        if (object->getSkewX() || object->getSkewY() || object->m_positionXOffset || object->m_positionYOffset ||
            object->m_tempOffsetXRelated) return FilterClass::OffsetOrSkew;
        const auto size = object->getContentSize();
        const auto anchor = object->getAnchorPoint();
        if (!(size.width > 0 && size.height > 0) || !std::isfinite(anchor.x) || !std::isfinite(anchor.y))
            return FilterClass::InvalidSize;
        radius = 2.0 * (std::abs(double(size.width) * object->getScaleX()) * std::max(std::abs(double(anchor.x)), std::abs(1.0-anchor.x)) +
            std::abs(double(size.height) * object->getScaleY()) * std::max(std::abs(double(anchor.y)), std::abs(1.0-anchor.y)));
        if (!std::isfinite(radius) || radius < 0) return FilterClass::InvalidSize;
        if (radius > 2048) return FilterClass::Oversized;
        return FilterClass::Eligible;
    }
    double filterObjectRadius(const Entry& entry) const {
        double radius = 0;
        return filterGeometry(entry, radius) == FilterClass::Eligible ? radius : std::numeric_limits<double>::infinity();
    }
    CCNode* filterObjectSpace(const Entry& entry) const {
        auto* object = entry.object.data();
        auto* batch = object->getBatchNode();
        auto* parent = object->getParent();
        if (parent && parent != batch && parent != layer->m_objectLayer) return nullptr;
        if (parent) return parent;
        if (batch) return batch;
        // Detached decorations still have logical coordinates in the object
        // layer. Never use a stale compilation-time batch after detachment.
        return static_cast<CCNode*>(layer->m_objectLayer);
    }
    void filterReclassify(ObjectId id, bool initial = false) {
        auto& entry = entries[id];
        entry.visibilityGeometryQueued = false;
        const auto oldClass = entry.visibilityClass;
        const auto oldSpace = entry.visibilitySpace;
        unsigned newSpace = invalidVisibilitySpace;
        double radius = 0;
        auto classification = entry.owned ? filterGeometry(entry, radius) : FilterClass::Retired;
        if (classification == FilterClass::Eligible) {
            auto* node = filterObjectSpace(entry);
            const auto section = filterCurrentSection(entry);
            if (!node) classification = FilterClass::NoSpace;
            else if (section.first < 0 || section.second < 0) classification = FilterClass::InvalidSection;
            else {
                unsigned bucket = 16;
                while (bucket < radius) bucket *= 2;
                auto [it, inserted] = filterSpaceLookup.try_emplace({node, bucket}, static_cast<unsigned>(filterSpaces.size()));
                if (inserted) {
                    filterSpaces.push_back({node, double(bucket)});
                    visibilityDeferred.addSpace();
                }
                newSpace = it->second;
            }
        }
        if (oldClass != FilterClass::Unclassified) --filterCurrentClasses[static_cast<unsigned>(oldClass)];
        ++filterCurrentClasses[static_cast<unsigned>(classification)];
        if (initial) {
            ++filterInitialClasses[static_cast<unsigned>(classification)];
            if (classification == FilterClass::Eligible) ++filterInitialEligible;
            if (entry.headless) {
                ++filterInitialHeadless;
                ++filterInitialHeadlessClasses[static_cast<unsigned>(classification)];
                if (classification == FilterClass::Eligible) ++filterInitialHeadlessEligible;
            }
        } else {
            ++filterReclassifications;
            if (oldClass == FilterClass::Eligible && classification != FilterClass::Eligible) ++filterInvalidations;
            if (oldClass != FilterClass::Eligible && classification == FilterClass::Eligible) ++filterReadmitted;
            if (oldSpace != invalidVisibilitySpace && newSpace != invalidVisibilitySpace && oldSpace != newSpace) ++filterSpaceChanges;
        }
        entry.visibilityClass = classification;
        entry.visibilitySpace = newSpace;
        if (envFlags().visibilityOwnershipShadow) updateVisibilityOwnershipIndex(id);
        filterCacheCurrentSection(id);
        filterCacheState(id);
        if (initial) filterCacheOldSection(id);
        filterEligible = filterCurrentClasses[static_cast<unsigned>(FilterClass::Eligible)];
        if (!visibilityDeferred.indexed(id)) return;
        if (classification == FilterClass::Eligible) {
            // Re-home both current and stale-position section memberships.
            visibilityDeferred.defer(id, newSpace, filterCandidateCurrentSection(id), filterCandidateOldSection(id));
        } else {
            visibilityDeferred.erase(id);
            if (entry.owned) {
                mirrorWorklist.mergeObject(id);
                ++filterGeometryForced;
            }
        }
    }
    void initializeVisibilityFilter() {
        if (!envFlags().visibilityFilter && !envFlags().visibilityFilterAudit &&
            !envFlags().visibilityOwnershipShadow) return;
        if (!(sectionXFactor >= 0 && sectionYFactor >= 0 && (sectionXFactor > 0 || sectionYFactor > 0)) ||
            double(nativeSectionLimit * sectionXFactor) >= std::numeric_limits<int>::max() - 1.0 ||
            double(nativeSectionLimit * sectionYFactor) >= std::numeric_limits<int>::max() - 1.0) {
            filterTrackingLost = true;
            return;
        }
        visibilityDeferred.initialize(entries.size());
        filterInitialized = true;
        filterGeometryQueue.reserve(entries.size());
        if (envFlags().packedVisibilityFilter) {
            packedFilterMetadata.resize(entries.size());
            for (ObjectId id = 0; id < entries.size(); ++id) filterCacheAll(id);
        }
        for (ObjectId id = 0; id < entries.size(); ++id)
            if (entries[id].owned) filterReclassify(id, true);
    }
    void filterQueueReclassify(ObjectId id, FilterChange reason) {
        if (!filterInitialized) return;
        auto& entry = entries[id];
        if (!entry.owned) return;
        ++filterGeometryEvents[static_cast<unsigned>(reason)];
        if (entry.visibilityGeometryQueued) return;
        entry.visibilityGeometryQueued = true;
        filterCacheState(id);
        filterGeometryQueue.push_back(id);
    }
    void filterDrainReclassifications() {
        for (auto id : filterGeometryQueue) filterReclassify(id);
        filterGeometryQueue.clear();
    }
    void filterSectionChanged(ObjectId id) {
        if (envFlags().visibilityOwnershipShadow) updateVisibilityOwnershipIndex(id);
        if (!filterInitialized) return;
        filterCacheCurrentSection(id);
        if (entries[id].visibilityClass == FilterClass::InvalidSection) filterQueueReclassify(id, FilterChange::Section);
        if (!envFlags().visibilityFilter || !visibilityDeferred.indexed(id)) return;
        const auto section = filterCurrentSection(entries[id]);
        if (section.first < 0 || section.second < 0) { filterQueueReclassify(id, FilterChange::Section); return; }
        visibilityDeferred.move(id, section);
        ++filterSectionMoves;
    }
    void filterRefreshWindows() {
        if (!envFlags().visibilityFilter && !envFlags().visibilityFilterAudit &&
            !envFlags().visibilityOwnershipShadow) return;
        if (layer->m_sectionXFactor != sectionXFactor || layer->m_sectionYFactor != sectionYFactor)
            filterTrackingLost = true;
        visibilityCameras.clear();
        for (auto& space : filterSpaces) {
            space.range = {};
            if (filterTrackingLost || layer->m_startOptimization) continue;
            auto* node = space.node.data();
            auto found = visibilityCameras.find(node);
            if (found == visibilityCameras.end()) {
                const auto size = CCDirector::sharedDirector()->getWinSize();
                const auto a = node->convertToNodeSpace({0,0}), b = node->convertToNodeSpace({size.width,0}),
                    c = node->convertToNodeSpace({0,size.height}), d = node->convertToNodeSpace({size.width,size.height});
                CensusBounds view{std::min({a.x,b.x,c.x,d.x}), std::max({a.x,b.x,c.x,d.x}),
                    std::min({a.y,b.y,c.y,d.y}), std::max({a.y,b.y,c.y,d.y})};
                for (auto p : {a,b,c,d}) if (!std::isfinite(p.x) || !std::isfinite(p.y)) view = {};
                found = visibilityCameras.emplace(node, view).first;
            }
            const auto actual = found->second;
            if (!(actual.left < actual.right && actual.bottom < actual.top)) { space.hasPrevious = false; continue; }
            // Current post-step viewport plus one previous displacement of
            // prefetch. Include previous range for camera-driven exits/zoom.
            auto view = actual;
            double motion = 0;
            if (space.hasPrevious) {
                const auto& p = space.previous;
                motion = std::max({std::abs(view.left-p.left), std::abs(view.right-p.right),
                    std::abs(view.bottom-p.bottom), std::abs(view.top-p.top)});
                view = {std::min(view.left,p.left), std::max(view.right,p.right),
                    std::min(view.bottom,p.bottom), std::max(view.top,p.top)};
            }
            space.previous = actual;
            space.hasPrevious = true;
            space.range = visibilitySectionRange(view, visibilityCensusMargin + space.radius + motion,
                sectionXFactor, sectionYFactor, nativeSectionLimit);
        }
    }
    bool filterNearCurrent(const Entry& entry) const {
        return filterTrackingLost || entry.visibilityGeometryQueued || entry.visibilitySpace == invalidVisibilitySpace ||
            filterSpaces[entry.visibilitySpace].range.contains(filterCurrentSection(entry));
    }
    FilterDecision filterDecisionReference(ObjectId id) const {
        const auto& entry = entries[id];
        if (entry.nativeEdit) return FilterDecision::NativeEdit;
        if (filterTrackingLost) return FilterDecision::TrackingUnavailable;
        if (entry.visibilityGeometryQueued) return FilterDecision::GeometryPending;
        if (entry.visibilitySpace == invalidVisibilitySpace) return FilterDecision::GeometryUnsupported;
        if (filterSpaces[entry.visibilitySpace].range.contains(filterCurrentSection(entry))) return FilterDecision::CurrentNear;
        if (entry.object->isVisible()) return FilterDecision::Visible;
        if (filterSpaces[entry.visibilitySpace].range.contains(filterOldSection(entry))) return FilterDecision::OldNear;
        return FilterDecision::Deferred;
    }
    FilterDecision filterDecisionPacked(ObjectId id) const {
        const auto& entry = entries[id];
        const auto& metadata = packedFilterMetadata[id];
        if (metadata.flags & packedFilterNativeEdit) return FilterDecision::NativeEdit;
        if (filterTrackingLost) return FilterDecision::TrackingUnavailable;
        if (metadata.flags & packedFilterGeometryPending) return FilterDecision::GeometryPending;
        if (!(metadata.flags & packedFilterEligible) || metadata.visibilitySpace == invalidVisibilitySpace)
            return FilterDecision::GeometryUnsupported;
        const auto& range = filterSpaces[metadata.visibilitySpace].range;
        if (range.contains({metadata.currentOuter, metadata.currentMiddle})) return FilterDecision::CurrentNear;
        // Native visibility is deliberately read from the object. setVisible is
        // not the only proven mutation boundary for this state.
        if (entry.object->isVisible()) return FilterDecision::Visible;
        if (range.contains({metadata.oldOuter, metadata.oldMiddle})) return FilterDecision::OldNear;
        return FilterDecision::Deferred;
    }
    FilterDecision filterDecision(ObjectId id) {
        if (!packedFilterMetadataEnabled()) return filterDecisionReference(id);
        const auto packed = filterDecisionPacked(id);
        if (envFlags().visibilityFilterAudit) {
            ++packedFilterPredicateComparisons;
            const auto reference = filterDecisionReference(id);
            if (packed != reference) {
                ++packedFilterPredicateMismatches;
                if (!packedFilterFirstMismatch) {
                    const auto& entry = entries[id];
                    const auto& metadata = packedFilterMetadata[id];
                    packedFilterFirstMismatch = PackedFilterMismatch{id, ticks,
                        static_cast<std::uint64_t>(layer->m_gameState.m_commandIndex), packed, reference,
                        {metadata.currentOuter, metadata.currentMiddle}, {metadata.oldOuter, metadata.oldMiddle},
                        filterCurrentSection(entry), filterOldSection(entry), metadata.visibilitySpace, entry.visibilitySpace,
                        metadata.flags, entry.visibilityClass, entry.visibilityGeometryQueued, entry.nativeEdit,
                        entry.object->isVisible(), entry.owned, entry.historyBase.x, entry.historyBase.y};
                }
                filterMiss(id, "packed_predicate_mismatch");
                // Keep the audit run on the reference result after recording a
                // mismatch so a stale packed value cannot hide the next failure.
                return reference;
            }
        }
        return packed;
    }
    bool filterMayDefer(ObjectId id) {
        return filterDecision(id) == FilterDecision::Deferred;
    }
    void filterMiss(ObjectId id, const char* reason) {
        ++filterAuditFailures;
        if (id == filterNegativeObject) filterNegativeCaught = true;
        if (!filterFirstMiss) {
            filterFirstMiss = FilterMiss{id, ticks, static_cast<std::uint64_t>(layer->m_gameState.m_commandIndex), reason};
            log::error("VULKANIZER visibility filter miss object={} step={} reason={}", id, ticks, reason);
        }
        if (envFlags().visibilityFilterAudit) filterFailed = true;
    }
    void filterActivated(ObjectId id) {
        if (!envFlags().visibilityFilter || !visibilityDeferred.indexed(id)) return;
        if (hyperVisibilityActive) { ++filterLateVisible; filterMiss(id, "deferred_became_visible"); }
        syncResident(id, "visibility_filter_activation", true);
    }
    void auditFilterBeforeVisibility() {
        if (!envFlags().visibilityFilterAudit || !envFlags().visibilityFilter ||
            (filterNegativeObject == UINT32_MAX && filterCheckpointStep != UINT64_MAX && ticks - filterCheckpointStep < 240)) return;
        filterCheckpointStep = ticks;
        ++filterCheckpoints;
        // Independent full scan in audit mode only. Verify the section used
        // for skipping against ordered replay, not against stale CPU position.
        for (ObjectId id = 0; id < entries.size(); ++id) {
            if (!mirrorStale(id)) continue;
            ++filterCheckpointObjects;
            if (!visibilityDeferred.indexed(id) || !filterMayDefer(id)) {
                filterMiss(id, "stale_without_safe_deferred_membership");
                syncResident(id, "visibility_filter_audit_recovery");
                break;
            }
            const auto replay = replayResident(id);
            if (!std::isfinite(replay.position.x) || !std::isfinite(replay.position.y) ||
                filterCurrentSection(entries[id]) != VisibilitySection{
                    visibilitySectionCoordinate(replay.position.x, sectionXFactor, nativeSectionLimit),
                    visibilitySectionCoordinate(replay.position.y, sectionYFactor, nativeSectionLimit)}) {
                filterMiss(id, "post_command_section_disagrees_with_replay");
                syncResident(id, "visibility_filter_audit_recovery");
                break;
            }
        }
    }
    void auditFilterAfterVisibility() {
        if (!envFlags().visibilityFilterAudit || !ready || testCompleted) return;
        FilterTrace row{ticks, static_cast<std::uint64_t>(layer->m_gameState.m_commandIndex),
            UINT64_C(14695981039346656037), UINT64_C(14695981039346656037), UINT64_C(14695981039346656037),
            UINT64_C(14695981039346656037), 0, 0};
        auto mix = [](std::uint64_t& h, std::uint64_t v) { h = (h ^ v) * UINT64_C(1099511628211); };
        auto real = [&](std::uint64_t& h, auto v) { mix(h, setter_audit::bits(v)); };
        real(row.state, layer->m_gameState.m_levelTime);
        real(row.state, layer->m_gameState.m_cameraPosition.x); real(row.state, layer->m_gameState.m_cameraPosition.y);
        real(row.state, layer->m_objectLayer->getScaleX()); real(row.state, layer->m_objectLayer->getScaleY());
        real(row.state, layer->m_objectLayer->getPositionX()); real(row.state, layer->m_objectLayer->getPositionY());
        for (auto* player : {layer->m_player1, layer->m_player2}) if (player) {
            real(row.state, player->getPositionX()); real(row.state, player->getPositionY());
        }
        for (ObjectId id = 0; id < entries.size(); ++id) {
            const auto& entry = entries[id];
            auto* object = entry.object.data();
            const bool visible = object->isVisible();
            mix(row.visibility, visible); mix(row.visibility, entry.owned);
            row.visibleCount += visible;
            if (!entry.owned) continue;
            const bool relevant = visible || filterNearCurrent(entry);
            if (visibilityDeferred.indexed(id)) {
                ++filterAuditChecks;
                if (relevant || !filterMayDefer(id)) filterMiss(id, "deferred_relevant_after_visibility");
                const auto& space = filterSpaces[entry.visibilitySpace];
                const auto radius = filterObjectRadius(entry);
                if (!std::isfinite(radius) || radius > space.radius || filterObjectSpace(entry) != space.node.data())
                    filterMiss(id, "deferred_geometry_changed");
            }
            if (!relevant) continue;
            ++row.relevantCount;
            mix(row.logical, id);
            real(row.logical, object->m_positionX); real(row.logical, object->m_positionY);
            real(row.logical, object->getRotationX()); real(row.logical, object->getRotationY());
            real(row.logical, object->m_rotationXOffset); real(row.logical, object->m_rotationYOffset);
            real(row.logical, object->m_lastPosition.x); real(row.logical, object->m_lastPosition.y);
            mix(row.logical, object->m_unk4C4);
            mix(row.logical, object->m_outerSectionIndex); mix(row.logical, object->m_middleSectionIndex);
            if (visible) {
                mix(row.sprite, id);
                real(row.sprite, object->getPositionX()); real(row.sprite, object->getPositionY());
                mix(row.sprite, object->getOpacity());
            }
        }
        filterTrace.push_back(row);
    }
    VisibilityRegion censusRegion(const Entry& entry, double x, double y) {
        auto* object = entry.object.data();
        auto* batch = object->getBatchNode();
        auto* parent = object->getParent();
        // The ordinary resident quad uses the batch's logical coordinate
        // space. Nested unexpected parents are not a safe coordinate proof.
        if (parent && parent != batch && parent != layer->m_objectLayer) return VisibilityRegion::Unknown;
        CCNode* space = parent ? parent : (batch ? static_cast<CCNode*>(batch) : static_cast<CCNode*>(entry.batch.data()));
        if (!space) space = layer->m_objectLayer;
        if (!space || object->getSkewX() || object->getSkewY() || object->m_positionXOffset || object->m_positionYOffset)
            return VisibilityRegion::Unknown;
        auto found = visibilityCameras.find(space);
        if (found == visibilityCameras.end()) {
            const auto size = CCDirector::sharedDirector()->getWinSize();
            const auto a = space->convertToNodeSpace({0, 0}), b = space->convertToNodeSpace({size.width, 0}),
                c = space->convertToNodeSpace({0, size.height}), d = space->convertToNodeSpace({size.width, size.height});
            CensusBounds bounds{std::min({a.x,b.x,c.x,d.x}), std::max({a.x,b.x,c.x,d.x}),
                std::min({a.y,b.y,c.y,d.y}), std::max({a.y,b.y,c.y,d.y})};
            for (auto point : {a,b,c,d}) if (!std::isfinite(point.x) || !std::isfinite(point.y)) bounds = {};
            found = visibilityCameras.emplace(space, bounds).first;
        }
        // A rotation-independent L1 radius, including non-centered anchors.
        // This is a geometric estimate, not a proof of native visibility semantics.
        const auto size = object->getContentSize();
        const auto anchor = object->getAnchorPoint();
        if (size.width <= 0 || size.height <= 0 || !std::isfinite(anchor.x) || !std::isfinite(anchor.y))
            return VisibilityRegion::Unknown;
        const double radius = std::abs(double(size.width) * object->getScaleX()) * std::max(std::abs(double(anchor.x)), std::abs(1.0-anchor.x)) +
            std::abs(double(size.height) * object->getScaleY()) * std::max(std::abs(double(anchor.y)), std::abs(1.0-anchor.y));
        return classifyVisibility(found->second, x, y, radius);
    }
    void censusMirror(ObjectId id) {
        auto& entry = entries[id];
        const bool sampled = sampleVisibility(mirrorMaterializations, id);
        const bool wasVisible = sampled && entry.object->isVisible();
        const auto old = sampled ? censusRegion(entry, entry.object->m_positionX, entry.object->m_positionY) : VisibilityRegion::Unknown;
        if (!syncResident(id, "hyper_before_visibility")) return;
        ++mirrorMaterializations;
        ++visibilityCensusEvents;
        if (visibilityLastFrame[id] == frames) ++visibilityRepeatInFrame;
        visibilityLastFrame[id] = frames;
        if (!sampled) return;
        const auto region = censusRegion(entry, entry.object->m_positionX, entry.object->m_positionY);
        auto& bucket = visibilityBuckets[static_cast<unsigned>(region)];
        ++bucket.samples;
        bucket.visibleBefore += wasVisible;
        visibilitySamples.push_back({id, old, region, wasVisible});
    }
    void finishVisibilityCensus() {
        if (!envFlags().visibilityCensus || visibilitySamples.empty()) return;
        visibilityCameras.clear();
        for (const auto& sample : visibilitySamples) {
            const auto& entry = entries[sample.id];
            if (!entry.owned) { ++visibilityUnpaired; continue; }
            const bool visible = entry.object->isVisible();
            auto& bucket = visibilityBuckets[static_cast<unsigned>(sample.region)];
            ++bucket.paired;
            bucket.visibleAfter += visible;
            bucket.entered += !sample.visible && visible;
            bucket.exited += sample.visible && !visible;
            if (sample.region == VisibilityRegion::FarOffscreen && !sample.visible && !visible) {
                ++visibilityFarHiddenBoth;
                if (sample.oldRegion == VisibilityRegion::FarOffscreen &&
                    censusRegion(entry, entry.object->m_positionX, entry.object->m_positionY) == VisibilityRegion::FarOffscreen)
                    ++visibilityStableFarHidden;
            }
        }
        visibilitySamples.clear();
        visibilityCameras.clear();
    }
    void prefetchMirrorCandidate(ObjectId id) const {
        if (id >= entries.size()) return;
        const auto* entry = &entries[id];
        _mm_prefetch(reinterpret_cast<const char*>(entry), _MM_HINT_T0);
        if (auto* object = entry->object.data())
            _mm_prefetch(reinterpret_cast<const char*>(object), _MM_HINT_T0);
    }
    void syncVisibilityMirrors() {
        ++mirrorPasses;
        mirrorFullScanSlots += entries.size();
        const auto visitsBefore = mirrorCandidateVisits;
        const auto materializationsBefore = mirrorMaterializations;
        spike::Scope timing(spike::Phase::visibilityMirrorScan);
        const auto forcedBefore = filterGeometryForced;
        filterDrainReclassifications();
        filterRefreshWindows();
        const auto cameraBefore = filterCameraCandidates;
        if (envFlags().visibilityFilter) {
            for (unsigned i = 0; i < filterSpaces.size(); ++i)
                visibilityDeferred.collect(i, filterSpaces[i].range, [&](ObjectId id) {
                    mirrorWorklist.mergeObject(id);
                    ++filterCameraCandidates;
                });
        }
        if (mirrorWorklist.empty() && cameraBefore == filterCameraCandidates && forcedBefore == filterGeometryForced) ++mirrorEmptyPasses;
        else {
            if (envFlags().visibilityCensus) visibilityCameras.clear();
            for (auto id : mirrorWorklist.begin()) {
                auto& chain = chains[id];
                ++mirrorChainDrains;
                if (!chain.owned) continue;
                // Deliberately omit one genuinely pending chain in the oracle-only
                // negative control. The oracle repairs the miss before native reads.
                if (envFlags().mirrorWorklistNegative && !mirrorNegativeInjected &&
                    std::any_of(chain.objects.begin(), chain.objects.end(), [&](auto h) { return mirrorStale(h.object); })) {
                    mirrorNegativeChain = id;
                    mirrorNegativeInjected = true;
                    continue;
                }
                mirrorWordMerges += chain.mirrorMask.size();
                mirrorWorklist.merge(chain.mirrorMask);
            }
            auto consumeCandidate = [&](ObjectId id) {
                ++mirrorCandidateVisits;
                auto& entry = entries[id];
                if (!entry.owned) return;
                if (envFlags().visibilityFilterAudit) {
                    const auto decision = entry.nativeEdit ? FilterDecision::NativeEdit :
                        !mirrorStale(id) ? FilterDecision::AlreadyCurrent : filterDecision(id);
                    ++filterDecisions[static_cast<unsigned>(decision)];
                    if (decision == FilterDecision::GeometryUnsupported)
                        ++filterUnsupportedVisits[static_cast<unsigned>(entry.visibilityClass)];
                }
                if (entry.nativeEdit) {
                    // A pending mirror cannot be applied during an authoritative edit.
                    markMirrorChain(scene->chainFor(scene->handle(id)));
                    return;
                }
                if (envFlags().visibilityCensus) censusMirror(id);
                else if (envFlags().visibilityFilterNegative && filterNegativeObject == UINT32_MAX && ticks >= 8 &&
                    filterCandidateSpace(id) != invalidVisibilitySpace && mirrorStale(id) && !filterMayDefer(id)) {
                    // Plant a missed in-range/visible materialization. The
                    // independent checkpoint must catch it before vanilla reads.
                    filterNegativeObject = id;
                    visibilityDeferred.defer(id, filterCandidateSpace(id), filterCandidateCurrentSection(id), filterCandidateOldSection(id));
                } else if (envFlags().visibilityFilter && filterMayDefer(id) && mirrorStale(id)) {
                    ++filterDeferredVisits;
                    if (visibilityDeferred.defer(id, filterCandidateSpace(id), filterCandidateCurrentSection(id), filterCandidateOldSection(id))) {
                        ++filterEpisodes;
                        filterPeakDeferred = std::max<std::uint64_t>(filterPeakDeferred, visibilityDeferred.size());
                    }
                } else mirrorMaterializations += syncResident(id, "hyper_before_visibility");
            };
            if (envFlags().mirrorPrefetch)
                mirrorWorklist.consume(consumeCandidate, [&](ObjectId ahead) { prefetchMirrorCandidate(ahead); });
            else mirrorWorklist.consume(consumeCandidate);
        }
        if (spike::audit.active)
            spike::audit.frame.phases[static_cast<std::size_t>(spike::Phase::visibilityMirrorScan)].work += mirrorCandidateVisits - visitsBefore;
        if (envFlags().nativeVisibilityProfile || envFlags().visibilityOwnershipShadow)
            spike::event(spike::Event::mirrorMaterializations, mirrorMaterializations - materializationsBefore);
        auditFilterBeforeVisibility();
    }
    void auditVisibilityMirrors(bool force = false) {
        const auto& flags = envFlags();
        if (!flags.mirrorWorklistAudit && !(validate && !flags.performanceOnly)) return;
        if (!force && mirrorAuditPasses && ticks - mirrorLastAuditStep < 240 && !mirrorNegativeInjected) return;
        mirrorLastAuditStep = ticks;
        ++mirrorAuditPasses;
        mirrorAuditSlots += entries.size();
        // Independent old scan: any remaining eligible stale cursor means the
        // worklist failed to cover a materialization the old code performed.
        for (ObjectId id = 0; id < entries.size(); ++id) {
            if (!mirrorStale(id)) continue;
            const auto chain = scene->chainFor(scene->handle(id));
            ++mirrorAuditMisses;
            mirrorFailed = true;
            if (mirrorNegativeInjected && chain == mirrorNegativeChain) mirrorNegativeCaught = true;
            if (!mirrorFirstMiss) {
                mirrorFirstMiss = MirrorMiss{id, chain, entries[id].residentApplied, chains[chain].lastResidentAction,
                    ticks, static_cast<std::uint64_t>(layer->m_gameState.m_commandIndex)};
                log::error("VULKANIZER mirror oracle missed object={} chain={} step={} command={} applied={} expected={}",
                    id, chain, ticks, layer->m_gameState.m_commandIndex, entries[id].residentApplied, chains[chain].lastResidentAction);
            }
            syncResident(id, "mirror_oracle_recovery");
        }
    }
    void forgetSections(ObjectId id, ChainId chain) {
        auto& entry = entries[id];
        if (!entry.sectionIndexed) return;
        for (unsigned i = 0; i < entry.sectionBounds.size(); ++i)
            sectionIndices[chain][i].erase({entry.sectionBounds[i], id});
        entry.sectionIndexed = false;
    }
    void indexSections(ObjectId id, ChainId chain) {
        auto& entry = entries[id];
        auto* object = entry.object.data();
        if (envFlags().visibilityOwnershipShadow) updateVisibilityOwnershipIndex(id);
        if (object->m_outerSectionIndex < 0) return;
        auto bounds = [](int index, float factor, double base) {
            const auto infinity = std::numeric_limits<double>::infinity();
            if (index < 0 || (factor == 0 && index != 0)) return std::array{infinity, infinity};
            const auto last = static_cast<int>(nativeSectionLimit * factor);
            return std::array{index > 0 && factor > 0 ? index / double(factor) - base : -infinity,
                index < last && factor > 0 ? (double(index) + 1) / double(factor) - base : infinity};
        };
        const auto x = bounds(object->m_outerSectionIndex, sectionXFactor, entry.sectionOrigin.x);
        const auto y = bounds(object->m_middleSectionIndex, sectionYFactor, entry.sectionOrigin.y);
        entry.sectionBounds = {x[0], x[1], y[0], y[1]};
        for (unsigned i = 0; i < entry.sectionBounds.size(); ++i)
            sectionIndices[chain][i].emplace(entry.sectionBounds[i], id);
        entry.sectionIndexed = true;
    }
    void beginNativeEdit(ObjectId id) {
        auto& entry = entries[id];
        if (entry.nativeEdit || !entry.owned) return;
        syncResident(id, "retained_native_edit", true);
        auto* object = entry.object.data();
        entry.editRotationX = object->getRotationX();
        entry.editRotationY = object->getRotationY();
        entry.editRotationOffsetX = object->m_rotationXOffset;
        entry.editRotationOffsetY = object->m_rotationYOffset;
        entry.editOuter = object->m_outerSectionIndex;
        entry.editMiddle = object->m_middleSectionIndex;
        entry.nativeEdit = true;
        filterCacheState(id);
        nativeEdits.push_back(id);
        ++retainedNativeEdits;
    }
    // Keep Scene ownership and the ordered membership routes unchanged. Only
    // rebase the exact native result and the CPU replay cursor.
    void finishNativeEdit(ObjectId id, bool keep) {
        auto& entry = entries[id];
        if (!entry.nativeEdit) return;
        entry.nativeEdit = false;
        filterCacheState(id);
        if (!entry.owned) return;
        auto* object = entry.object.data();
        const Translation result{object->m_positionX, object->m_positionY};
        const bool moved = std::bit_cast<std::uint64_t>(result.x) != std::bit_cast<std::uint64_t>(entry.historyBase.x) ||
            std::bit_cast<std::uint64_t>(result.y) != std::bit_cast<std::uint64_t>(entry.historyBase.y);
        const auto chainId = scene->chainFor(scene->handle(id));
        auto& chain = chains[chainId];
        entry.historyBase = result;
        filterCacheOldSection(id);
        filterCacheCurrentSection(id);
        entry.initialPrevious = {object->m_lastPosition.x, object->m_lastPosition.y};
        entry.residentStamp = static_cast<std::uint32_t>(object->m_unk4C4);
        entry.residentApplied = chain.lastResidentAction;
        // A native setter may only have dirtied the sprite position.
        if (moved) entry.spriteApplied = UINT32_MAX;
        if (!keep) return;
        if (moved) {
            const auto offset = scene->chainTranslation(chainId);
            entry.sectionOrigin = {result.x - offset.x, result.y - offset.y};
            chain.precision.coordinateMagnitude = std::max({chain.precision.coordinateMagnitude,
                std::abs(result.x), std::abs(result.y), std::abs(entry.sectionOrigin.x), std::abs(entry.sectionOrigin.y)});
            chain.precision.accumulatedError = std::max(chain.precision.accumulatedError,
                8 * std::numeric_limits<double>::epsilon() *
                std::max({std::abs(result.x), std::abs(result.y), std::abs(offset.x), std::abs(offset.y)}));
            const gpu::ResidentTranslation command{result.x, result.y, residentRebaseBit | id, 0, 0};
            if (entry.pendingRebase == std::numeric_limits<std::size_t>::max()) {
                entry.pendingRebase = residentRebases.size();
                residentRebases.push_back(command);
            } else residentRebases[entry.pendingRebase] = command;
            entry.reference = result;
            residentValidatedActions = std::numeric_limits<std::size_t>::max();
            ++retainedPositionRebases;
        }
        if (moved || entry.editOuter != object->m_outerSectionIndex ||
                entry.editMiddle != object->m_middleSectionIndex) {
            forgetSections(id, chainId);
            indexSections(id, chainId);
        }
        if (entry.editRotationX != object->getRotationX() || entry.editRotationY != object->getRotationY() ||
            entry.editRotationOffsetX != object->m_rotationXOffset || entry.editRotationOffsetY != object->m_rotationYOffset) {
            entry.residentQuad = false;
            if (entry.atlas) atlases.at(entry.atlas.data()).dirty = true;
            ++retainedGeometryChanges;
        }
    }
    void translate(std::span<const ObjectId>, Translation, std::uint64_t) noexcept override {
        // Native callbacks run after Scene::translate returns so they can
        // request dependency handoff without re-entering the scene runtime.
    }
    void materializeLegacy(ObjectId id, Handle handle, Translation p) {
        auto& entry = entries[id];
        auto* object = entry.object.data();
        const auto transform = scene->nativeTransform(handle);
        if (object->getRotationX() != transform.rotationX) object->setRotationX(transform.rotationX);
        if (object->getRotationY() != transform.rotationY) object->setRotationY(transform.rotationY);
        if (object->getScaleX() != transform.scaleX) object->setScaleX(transform.scaleX);
        if (object->getScaleY() != transform.scaleY) object->setScaleY(transform.scaleY);
        if (object->getSkewX() != transform.skewX) object->setSkewX(transform.skewX);
        if (object->getSkewY() != transform.skewY) object->setSkewY(transform.skewY);
        if (object->isFlipX() != transform.flipX) object->setFlipX(transform.flipX);
        if (object->isFlipY() != transform.flipY) object->setFlipY(transform.flipY);
        if (object->getAnchorPoint().x != transform.anchorX || object->getAnchorPoint().y != transform.anchorY)
            object->setAnchorPoint({transform.anchorX, transform.anchorY});
        const auto& chain = chains[scene->chainFor(handle)];
        if (residentEnabled) {
            const auto result = replayResident(id);
            p = result.position;
            if (result.invalidateRect) {
                object->m_unk4C4 = static_cast<int>(result.stamp);
                object->m_lastPosition = CCPoint(static_cast<float>(result.previous.x), static_cast<float>(result.previous.y));
                object->dirtifyObjectRect();
            }
            residentRetired.push_back(id);
        } else if (const auto& history = chain.history) {
            const auto native = history->materialize(entry.historyBase, entry.initialPrevious);
            p = native.position;
            if (native.invalidateRect) {
                object->m_lastPosition = CCPoint(static_cast<float>(native.previous.x), static_cast<float>(native.previous.y));
                object->m_unk4C4 = static_cast<int>(native.stamp);
                object->dirtifyObjectRect();
            }
        }
        object->m_positionX = p.x;
        object->m_positionY = p.y;
        object->dirtifyObjectPos();
        layer->updateObjectSection(object);
        object->cocos2d::CCSprite::setPosition({static_cast<float>(p.x), static_cast<float>(p.y)});
        if (object->getBatchNode()) object->updateTransform();
    }
    void materialize(ObjectId id, Translation p, bool) noexcept override {
        auto& entry = entries[id];
        auto* object = entry.object.data();
        internal = true;
        const auto handle = scene->handle(id);
        if (aggressive) {
            syncResident(id, "ownership_revocation", true);
            forgetSections(id, scene->chainFor(handle));
            residentRetired.push_back(id);
            if (object->getBatchNode()) object->updateTransform();
        } else {
            materializeLegacy(id, handle, p);
        }
        internal = false;
        entry.owned = false;
        if (envFlags().visibilityOwnershipShadow) removeVisibilityOwnership(id);
        if (filterInitialized && entry.visibilityClass != FilterClass::Unclassified) filterReclassify(id);
        if (entry.atlas) atlases.at(entry.atlas.data()).plan.release(entry.slot);
        for (auto [routeIndex, slot] : entry.memberships) {
            auto& route = routes[routeIndex];
            if (!route.valid) continue;
            if (aggressive && lazyRoutes) {
                route.valid = false;
                continue;
            }
            const auto at = std::lower_bound(route.nativeSlots.begin(), route.nativeSlots.end(), slot);
            const auto index = static_cast<unsigned>(at - route.nativeSlots.begin());
            route.nativeSlots.insert(at, slot);
            route.native->insertObject(object, index);
            --route.owned;
            if (entry.headless) --route.ownedHeadless;
        }
        ++stats.handoffCommits;
        ++stats.released;
        if (entry.headless) ++stats.headlessReleased;
    }
    void visibility(ObjectId, bool) noexcept override {}
    void reset(ObjectId, Translation, bool) noexcept override {}
    void bindMaterial(MaterialId) override {
        // Cocos normally binds the atlas texture inside drawNumberOfQuads.
        // A fully V2 range bypasses that body, so reproduce that material step.
        ccGLBindTexture2DN(0, drawingAtlas->getTexture()->getName());
    }
    void drawNative(ObjectId) override { throw std::logic_error("Live fallback requires an original atlas range"); }
    unsigned owned() const { return static_cast<unsigned>(stats.acquired - stats.released); }
    void rebuildNativeRoutes() {
        for (auto& route : routes) {
            route.native = CCArray::createWithCapacity(route.source->count());
            route.nativeSlots.clear();
            route.nativeSlots.reserve(route.source->count());
            route.owned = 0;
            route.ownedHeadless = 0;
            route.chains.clear();
            unsigned slot = 0;
            for (auto* object : CCArrayExt<GameObject, false>(route.source)) {
                auto found = ids.find(object);
                if (found != ids.end() && entries[found->second].owned) {
                    const auto id = found->second;
                    ++route.owned;
                    if (entries[id].headless) ++route.ownedHeadless;
                    route.chains.push_back(scene->chainFor(scene->handle(id)));
                } else { route.native->addObject(object); route.nativeSlots.push_back(slot); }
                ++slot;
            }
            std::sort(route.chains.begin(), route.chains.end());
            route.chains.erase(std::unique(route.chains.begin(), route.chains.end()), route.chains.end());
        }
    }
};
std::unique_ptr<Live> state;
thread_local Live* spikeFrameOwner = nullptr;
spike::Counters spikeCounters(const Live& live) {
    return {live.ticks, live.stats.nativeMoveVisits, live.stats.eliminatedVisits,
        live.stats.spriteVisits, live.stats.nativeCommitCallbacks, live.residentSectionCommits,
        live.stats.released, live.stats.nativeAtlasDirtyBytes, live.stats.nativeAtlasUploads,
        live.renderer ? live.renderer->counters().residentSubmissions : 0,
        live.groupRotationNativeRecipients, live.groupRotationRecipients,
        live.residentSyncMs, live.residentHandoffMs, live.residentDispatchWallMs, live.residentSectionCommitWallMs};
}
bool testRunning = false, captured = false, testLaunched = false;
Clock::time_point testStarted;
std::uint64_t testTicks = 0;
double testInitialLevelTime = -1;
bool controlledToggleVisibility() {
    const auto& flags = envFlags();
    return flags.persistVisibility && flags.toggleFixture &&
        flags.test && !flags.levelName;
}

void* resolve(const char* name) {
    auto address = wglGetProcAddress(name);
    auto bits = reinterpret_cast<std::uintptr_t>(address);
    if (bits <= 3 || bits == ~std::uintptr_t{}) {
        address = reinterpret_cast<PROC>(GetProcAddress(GetModuleHandleW(L"opengl32.dll"), name));
    }
    return reinterpret_cast<void*>(address);
}
struct Camera {
    double left, right, bottom, top, scale;
};
std::optional<Camera> camera(CCNode* batch) {
    auto size = CCDirector::sharedDirector()->getWinSize();
    const auto a = batch->convertToNodeSpace({0, 0});
    const auto b = batch->convertToNodeSpace({size.width, 0});
    const auto c = batch->convertToNodeSpace({0, size.height});
    if (std::abs(b.y - a.y) > .01 || std::abs(c.x - a.x) > .01 || b.x <= a.x || c.y <= a.y) return {};
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    return Camera{a.x, b.x, a.y, c.y, std::max(viewport[2] / (b.x - a.x), viewport[3] / (c.y - a.y))};
}

struct CameraCache {
    std::unordered_map<CCNode*, std::optional<Camera>> entries;

    std::optional<Camera> get(CCNode* node) {
        auto it = entries.find(node);
        if (it != entries.end())
            return it->second;

        auto result = camera(node);
        entries.emplace(node, result);
        return result;
    }

    void clear() {
        entries.clear();
    }
};

const char* unsafe(
    ChainId id,
    Translation candidate,
    double error,
    CameraCache* cameras = nullptr
) {
    auto& live = *state;
    const auto& chain = live.chains[id];
    if (!std::isfinite(candidate.x) || !std::isfinite(candidate.y)) return "nonfinite_translation";
    const double sectionError = envFlags().gpuResident ? error +
        8 * std::numeric_limits<double>::epsilon() * chain.precision.coordinateMagnitude : 0;
    if (candidate.x - sectionError < chain.minDx || candidate.x + sectionError >= chain.maxDx ||
        candidate.y - sectionError < chain.minDy || candidate.y + sectionError >= chain.maxDy) return "section_boundary";
    for (const auto& space : chain.spaces) {
        const auto view = cameras
    ? cameras->get(space.batch.data())
    : camera(space.batch.data());
        if (!view) return "camera_transform";
        constexpr double margin = 32;
        if (space.headless) {
            const auto left = std::min(space.left, space.left + candidate.x);
            const auto right = std::max(space.right, space.right + candidate.x);
            const auto bottom = std::min(space.bottom, space.bottom + candidate.y);
            const auto top = std::max(space.top, space.top + candidate.y);
            if (!(chain.disabledHeadless && std::all_of(chain.spaces.begin(), chain.spaces.end(),
                    [](const auto& value) { return value.headless; })) &&
                !(right < view->left - margin || left > view->right + margin ||
                  top < view->bottom - margin || bottom > view->top + margin)) return "headless_visibility_boundary";
            const auto pixels = chain.precision.pixelBound(candidate, view->scale, error);
            live.stats.maxPixelBound = std::max(live.stats.maxPixelBound, pixels);
            if (pixels > pixelBudget) return "numerical_pixel_budget";
            continue;
        }
        if (std::min(space.left, space.left + candidate.x) <= view->left + margin ||
            std::max(space.right, space.right + candidate.x) >= view->right - margin ||
            std::min(space.bottom, space.bottom + candidate.y) <= view->bottom + margin ||
            std::max(space.top, space.top + candidate.y) >= view->top - margin) return "visibility_boundary";
        const auto pixels = chain.precision.pixelBound(candidate, view->scale, error);
        live.stats.maxPixelBound = std::max(live.stats.maxPixelBound, pixels);
        if (pixels > pixelBudget) return "numerical_pixel_budget";
    }
    return nullptr;
}
bool flushResident(Live& live, const char* reason);
struct ExecutableRange { std::uintptr_t base, begin, end; const char* module; };
const std::vector<ExecutableRange>& callerExecutableRanges() {
    static const auto ranges = [] {
        std::vector<ExecutableRange> result;
        auto add = [&](HMODULE module, const char* name) {
            if (!module) return;
            const auto base = reinterpret_cast<std::uintptr_t>(module);
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            const auto* section = IMAGE_FIRST_SECTION(nt);
            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
                if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
                const auto begin = base + section->VirtualAddress;
                const auto end = begin + section->Misc.VirtualSize;
                if (end > begin) result.push_back({base, begin, end, name});
            }
        };
        add(GetModuleHandleW(nullptr), "GeometryDash.exe");
        add(GetModuleHandleW(L"libcocos2d.dll"), "libcocos2d.dll");
        return result;
    }();
    return ranges;
}
struct CallerLocation {
    const char* provenance = "unresolved", *module = "unknown";
    std::uintptr_t rva = 0, parent = 0;
};
CallerLocation revocationCaller(const void* anchor) {
    CallerLocation result;
    const auto& ranges = callerExecutableRanges();
    auto accept = [&](std::uintptr_t pc, bool heuristic) {
        for (const auto& range : ranges) {
            if (pc < range.begin || pc >= range.end) continue;
            if (heuristic) {
                const std::size_t size = std::min<std::uintptr_t>(8, pc - range.begin);
                if (!caller_audit::callEndsAt({reinterpret_cast<const std::uint8_t*>(pc - size), size})) continue;
            }
            if (!result.rva) {
                result.module = range.module;
                result.rva = pc - range.base;
                result.provenance = heuristic ? "stack_candidate" : "unwound_return_pc";
            } else if (std::string_view(result.module) == range.module && result.rva != pc - range.base) {
                result.parent = pc - range.base;
                return true;
            }
            break;
        }
        return false;
    };
    void* frames[48]{};
    const auto n = CaptureStackBackTrace(0, 48, frames, nullptr);
    for (unsigned i = 0; i < n; ++i)
        if (accept(reinterpret_cast<std::uintptr_t>(frames[i]), false)) break;
    if (result.rva || !anchor) return result;
    // Trampolines can terminate unwinding. Scan only readable committed stack
    // memory near the hook return slot, with an explicit heuristic label.
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(anchor, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return result;
    const auto start = reinterpret_cast<std::uintptr_t>(anchor);
    const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (start >= end) return result;
    const auto count = std::min<std::uintptr_t>(256, (end - start) / sizeof(std::uintptr_t));
    for (std::uintptr_t i = 0; i < count; ++i) {
        std::uintptr_t pc = 0;
        std::memcpy(&pc, reinterpret_cast<const void*>(start + i * sizeof(pc)), sizeof(pc));
        if (accept(pc, true)) break;
    }
    return result;
}
bool inVisibilityScope() {
    for (auto* scope = spike::Scope::current; scope; scope = scope->parent)
        if (scope->phase == spike::Phase::visibility || scope->phase == spike::Phase::preVisibility) return true;
    return false;
}
unsigned setterCursorFlags(const Live& live, ObjectId id) {
    const auto& entry = live.entries[id];
    const auto& chain = live.chains[live.scene->chainFor(live.scene->handle(id))];
    const bool stale = entry.residentApplied < chain.lastResidentAction;
    const bool mismatch = !stale && (setter_audit::bits(entry.historyBase.x) != setter_audit::bits(double(entry.object->m_positionX)) ||
        setter_audit::bits(entry.historyBase.y) != setter_audit::bits(double(entry.object->m_positionY)));
    return (chain.lastQueuedGpuAction > live.residentSubmittedJournalEnd ? 1u : 0u) |
        (stale ? 2u : 0u) | (entry.spriteApplied != chain.lastResidentAction ? 4u : 0u) |
        (mismatch ? 8u : 0u) | (entry.pendingRebase != std::numeric_limits<std::size_t>::max() ? 16u : 0u);
}
setter_audit::Snapshot setterSnapshot(GameObject* object, unsigned* targetMask = nullptr) {
    using setter_audit::bits;
    setter_audit::Snapshot s;
    s.logical = {bits(double(object->m_positionX)), bits(double(object->m_positionY))};
    s.offsets = {bits(float(object->m_positionXOffset)), bits(float(object->m_positionYOffset))};
    s.sprite = {bits(float(object->cocos2d::CCNode::getPositionX())), bits(float(object->cocos2d::CCNode::getPositionY()))};
    s.previous = {bits(float(object->m_lastPosition.x)), bits(float(object->m_lastPosition.y))};
    s.rotation = {bits(float(object->getRotationX())), bits(float(object->getRotationY())),
        bits(float(object->m_rotationXOffset)), bits(float(object->m_rotationYOffset))};
    s.outer = object->m_outerSectionIndex; s.middle = object->m_middleSectionIndex; s.stamp = object->m_unk4C4;
    s.dirty = (object->m_isDirty ? 1u : 0u) | (object->m_isObjectPosDirty ? 2u : 0u) |
        (object->m_isUnmodifiedPosDirty ? 4u : 0u) | (object->m_isObjectRectDirty ? 8u : 0u) |
        (object->m_isOrientedBoxDirty ? 16u : 0u);
    s.visibility = (object->isVisible() ? 1u : 0u) | (object->m_isActivated ? 2u : 0u) |
        (object->m_isGroupDisabled ? 4u : 0u) | (object->m_isGroupDisabledTemp ? 8u : 0u) | (object->m_isHide ? 16u : 0u);
    s.groupCount = object->m_groupCount; s.linkedGroup = object->m_linkedGroup;
    s.groupHash = UINT64_C(14695981039346656037);
    if (object->m_groups) for (short i = 0; i < object->m_groupCount; ++i) {
        const int group = (*object->m_groups)[i];
        s.groupHash = (s.groupHash ^ static_cast<std::uint64_t>(group)) * UINT64_C(1099511628211);
        if (targetMask) *targetMask |= group == 575 ? 1u : group == 848 ? 2u : group == 815 ? 4u : 0u;
    }
    return s;
}
bool setterShadowMember(GameObject* object, int target) {
    if (!target || !object->m_groups) return false;
    for (short i = 0; i < object->m_groupCount; ++i)
        if ((*object->m_groups)[i] == target) return true;
    return false;
}
void hyperAttempt(Live& live, ObjectId id, const char* reason, bool emergency, bool modeled) {
    if (envFlags().performanceOnly) {
        // Keep operational stop conditions; omit per-object sets, maps and group scans.
        if (emergency) ++live.hyperEmergency;
        else if (!modeled) ++live.hyperUnmodeled;
        return;
    }
    auto& site = live.hyperReasons[reason];
    if (!site.attempted++) site.firstStep = live.ticks;
    site.lastStep = live.ticks;
    site.objects.insert(id);
    auto* object = live.entries[id].object.data();
    for (int group : {575, 848, 815}) if (setterShadowMember(object, group)) ++site.groups[group];
    if (emergency) { ++site.emergencyReleased; ++live.hyperEmergency; }
    else {
        ++site.retained;
        if (!modeled) { ++site.unmodeled; ++live.hyperUnmodeled; }
    }
}
void hyperFailure(Live& live, ObjectId id, const char* reason, const char* field,
                  std::uint64_t expected, std::uint64_t actual, int group = 0) {
    if (live.hyperFirstFailure) return;
    if (!group) {
        auto* object = live.entries[id].object.data();
        for (int candidate : {envFlags().setterShadowGroup, 575, 848, 815})
            if (candidate && setterShadowMember(object, candidate)) { group = candidate; break; }
    }
    live.hyperFirstFailure = HyperFailure{reason, field, id, live.ticks,
        static_cast<std::uint64_t>(live.layer->m_gameState.m_commandIndex), expected, actual, group};
    log::error("VULKANIZER_HYPER first mismatch reason={} field={} object={} step={} command={} expected={} actual={}",
        reason, field, id, live.ticks, live.layer->m_gameState.m_commandIndex, expected, actual);
}
bool hyperEmergencyReason(std::string_view reason) {
    // Resource/lifetime failures are not policy choices. Do not keep a stale
    // GL context or an actually retired object alive just to satisfy a counter.
    return reason.find("context_replacement") != std::string_view::npos ||
        reason == "resident_compute_failure" || reason == "resident_validation_failure" ||
        reason == "renderer_failure" || reason == "resident_atlas_refresh_failure" ||
        reason == "compilation_failure" || reason == "resident_journal_address_limit" ||
        reason == "resident_precision_or_journal_limit" || reason == "native_object_retirement" ||
        reason == "native_section_configuration_changed" || reason == "resident_unsupported_numeric_move" ||
        reason == "resident_position_range";
}
void handoff(std::span<const Handle> handles, const char* reason, const void* callerAnchor = nullptr) {
    auto& live = *state;
    if (handles.empty() || !live.scene) return;
    if (envFlags().hyperAggressive && live.aggressive && !live.hyperTearingDown && !live.testCompleted) {
        const bool emergency = hyperEmergencyReason(reason);
        const bool setter = std::string_view(reason) == "native_position_setter";
        for (auto h : handles) {
            auto& entry = live.entries[h.object];
            if (!entry.owned) continue;
            if (envFlags().visibilityFilter || envFlags().visibilityFilterAudit) {
                const std::string_view why(reason);
                if (why.starts_with("native_scale")) live.filterQueueReclassify(h.object, FilterChange::Scale);
                else if (why.starts_with("native_skew")) live.filterQueueReclassify(h.object, FilterChange::Skew);
                else if (why.starts_with("native_flip")) live.filterQueueReclassify(h.object, FilterChange::Flip);
                else if (why == "native_object_reset") live.filterQueueReclassify(h.object, FilterChange::ObjectReset);
                else if (why == "native_object_type") live.filterQueueReclassify(h.object, FilterChange::ObjectType);
                else if (why == "sprite_batch_change") live.filterQueueReclassify(h.object, FilterChange::Batch);
            }
            hyperAttempt(live, h.object, reason, emergency, setter);
            if (!emergency && live.residentEnabled && !live.flushingResidents) {
                // Native consumers still need a current CPU mirror. Keeping
                // the mirror is not the same as giving up the GPU route.
                live.syncResident(h.object, "hyper_blocked_handoff", true);
                if (setter && entry.object->getBatchNode()) {
                    const bool previous = live.internal;
                    live.internal = true;
                    entry.object->updateTransform();
                    live.internal = previous;
                }
            }
        }
        // Unknown semantics are diagnostic-only: preserve membership and count
        // every unmodeled attempt. Never claim equivalence in this case.
        if (!emergency) return;
    }
    const bool auditRevocations = envFlags().revocationAudit && !live.testCompleted;
    std::vector<unsigned char> snapshots;
    std::uint64_t pendingCommands = 0, pendingSpan = 0;
    if (auditRevocations) {
        const auto start = Clock::now();
        snapshots.reserve(handles.size());
        pendingCommands = live.residentPending.size();
        if (pendingCommands) pendingSpan = live.pendingLastStep - live.pendingFirstStep + 1;
        for (auto h : handles) {
            const auto& entry = live.entries[h.object];
            unsigned flags = 0;
            if (entry.owned) {
                const auto& chain = live.chains[live.scene->chainFor(h)];
                flags = 4 | (chain.lastQueuedGpuAction > live.residentSubmittedJournalEnd ? 1 : 0) |
                    (entry.residentApplied < chain.lastResidentAction ? 2 : 0);
            }
            snapshots.push_back(static_cast<unsigned char>(flags));
        }
        live.revocationAuditMs += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    // Do not discard earlier section events when a recipient leaves GPU ownership.
    if (live.groupRotation && live.sectionPotential && !live.flushingResidents)
        flushResident(live, "ownership_handoff");
    std::vector<Handle> owned;
    for (auto h : handles) if (live.entries[h.object].owned) owned.push_back(h);
    if (owned.empty()) return;
    if (auditRevocations) {
        const auto start = Clock::now();
        const auto caller = revocationCaller(callerAnchor ? callerAnchor : _AddressOfReturnAddress());
        const auto* scope = spike::Scope::current;
        auto& site = live.revocationSites[{reason, auditPhase,
            scope ? spike::phaseNames[static_cast<std::size_t>(scope->phase)] : "outside_frame",
            caller.provenance, caller.module, caller.rva, caller.parent}];
        site.maxBatchCommands = std::max(site.maxBatchCommands, pendingCommands);
        site.maxPendingStepSpan = std::max(site.maxPendingStepSpan, pendingSpan);
        for (std::size_t i = 0; i < handles.size(); ++i) {
            const auto flags = snapshots[i];
            if (!(flags & 4) || !live.entries[handles[i].object].owned) continue;
            site.counts.add(flags);
            live.revocationTotals.add(flags);
            const auto* object = live.entries[handles[i].object].object.data();
            if (object->m_groups) for (short j = 0; j < object->m_groupCount; ++j) {
                const int group = (*object->m_groups)[j];
                bool duplicate = false;
                for (short k = 0; k < j; ++k) duplicate |= (*object->m_groups)[k] == group;
                if (!duplicate) site.groups[group].add(flags);
            }
        }
        live.revocationAuditMs += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    for (auto h : owned) live.finishNativeEdit(h.object, false);
    const auto started = live.residentEnabled ? Clock::now() : Clock::time_point{};
    const auto syncBefore = live.residentSyncMs;
    if (live.audit && !live.testCompleted) {
        const auto attributionStarted = Clock::now();
        const RevocationCause cause{reason, auditPhase, live.auditRotationTarget};
        if (std::string_view(reason) == "native_position_setter")
            live.auditSetterPhases[auditPhase] += owned.size();
        live.auditRevocationsThisFrame += owned.size();
        for (auto h : owned) {
            for (auto [routeIndex, slot] : live.entries[h.object].memberships) {
                auto& route = live.routes[routeIndex];
                auto& mass = route.auditRevocations[cause];
                ++mass.members;
                mass.moveCountAtReleaseSum += route.auditMoves;
            }
        }
        live.auditAttributionMs += std::chrono::duration<double, std::milli>(
            Clock::now() - attributionStarted).count();
    }
    live.scene->acquireNativeBatch(owned, live);
    for (auto h : owned) {
        const auto id = live.scene->chainFor(h);
        auto& chain = live.chains[id];
        if (--chain.owned == 0 && live.aggressive)
            for (auto group : chain.residentGroups) std::erase(live.routes[group - 1].chains, id);
    }
    if (!live.aggressive)
        for (auto& route : live.routes)
            std::erase_if(route.chains, [&](auto id) { return !live.chains[id].owned; });
    if (live.residentEnabled)
        live.residentHandoffMs += std::max(0.0,
            std::chrono::duration<double, std::milli>(Clock::now() - started).count() -
            (live.residentSyncMs - syncBefore));
    live.transitions[reason] += owned.size();
    if (!live.aggressive)
        log::info("VULKANIZER_V2_OWNERSHIP scene={} transition=V2_to_Native objects={} reason={} remaining={} runtime_native_to_v2=0",
            live.scene->generation(), owned.size(), reason, live.owned());
}
void handoffChain(ChainId id, const char* reason) { handoff(state->chains[id].objects, reason); }

bool flushResident(Live& live, const char* reason) {
    if (!live.residentEnabled || !live.owned() ||
        (live.residentPending.empty() && live.residentRetired.empty() && live.residentRebases.empty())) return true;
    spike::Scope timing(spike::Phase::residentFlush);
    if (live.context != wglGetCurrentContext()) {
        live.sectionPotential = 0;
        releaseAll("resident_context_replacement");
        return false;
    }
    if (live.flushingResidents) return true;
    live.flushingResidents = true;
    struct FlushGuard { Live& live; ~FlushGuard() { live.flushingResidents = false; } } guard{live};
    try {
        if (envFlags().temporalReplay) {
            ++live.replayFlushReasons[reason];
            live.replayMaxSectionPotential = std::max<std::uint64_t>(live.replayMaxSectionPotential, live.sectionPotential);
            const auto commands = static_cast<std::uint64_t>(live.residentPending.size());
            if (commands) {
                const auto span = live.pendingLastStep - live.pendingFirstStep + 1;
                ++live.replayBatches;
                live.replayCommands += commands;
                live.replayMaxCommands = std::max(live.replayMaxCommands, commands);
                live.replayMaxStepSpan = std::max(live.replayMaxStepSpan, span);
                if (span > 1) {
                    ++live.replayMultiStepBatches;
                    live.replayMultiStepCommands += commands;
                }
            }
        }
        if (live.residentTimer) live.residentTimer->begin();
        std::vector<gpu::ResidentSectionEvent> events;
        const auto sectionStart = Clock::now();
        live.renderer->dispatchResident(live.residentPending, live.residentRetired,
            live.sectionPotential ? &events : nullptr, live.sectionXFactor, live.sectionYFactor);
        if (live.pendingPivotlessCommands) ++live.pivotlessBatches;
        live.pendingPivotlessCommands = 0;
        if ((envFlags().revocationAudit || envFlags().setterAudit || envFlags().hyperAggressive) && !live.residentPending.empty())
            live.residentSubmittedJournalEnd = live.residentPending.back().journalIndex + 1;
        live.residentDispatchWallMs +=
            std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();
        if (live.sectionPotential) {
            ++live.sectionEventBatches;
            live.sectionEventReadbackBytes += sizeof(std::uint32_t) + events.size() * sizeof(gpu::ResidentSectionEvent);
            if (envFlags().temporalReplay)
                live.replayMaxSectionEvents = std::max<std::uint64_t>(live.replayMaxSectionEvents, events.size());
        }
        live.sectionPotential = 0;
        if (!live.residentRebases.empty()) {
            const auto rebaseStart = Clock::now();
            live.renderer->dispatchResident(live.residentRebases, {});
            live.residentDispatchWallMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - rebaseStart).count();
            ++live.retainedRebaseBatches;
            for (const auto& command : live.residentRebases) {
                const auto id = command.group & ~residentRebaseBit;
                auto& entry = live.entries[id];
                entry.pendingRebase = std::numeric_limits<std::size_t>::max();
                if (envFlags().hyperAggressive && entry.owned && live.hyperRebaseObjects.contains(id)) {
                    auto* object = entry.object.data();
                    live.renderer->updateResidentSection(id, object->m_outerSectionIndex, object->m_middleSectionIndex);
                }
            }
            live.residentRebases.clear();
        }
        if (live.residentTimer) live.residentTimer->end();
        live.residentPending.clear();
        live.residentRetired.clear();
        if (!events.empty()) {
            spike::Scope profile(spike::Phase::residentSectionCommit, events.size());
            const auto commitStart = Clock::now();
            // GPU appends are unordered. Restore original action and native
            // recipient order, for actual decorative section transitions only.
            auto slot = [&](const gpu::ResidentSectionEvent& e) {
                const auto route = live.residentJournal[e.action].group;
                const auto& memberships = live.entries[e.object].memberships;
                const auto it = std::lower_bound(memberships.begin(), memberships.end(), route,
                    [](const auto& a, std::size_t b) { return a.first < b; });
                return it != memberships.end() && it->first == route ? it->second : UINT32_MAX;
            };
            std::sort(events.begin(), events.end(), [&](const auto& a, const auto& b) {
                return a.action != b.action ? a.action < b.action : slot(a) < slot(b);
            });
            const bool previous = live.internal;
            live.internal = true;
            for (const auto& event : events) {
                if (event.action < live.residentJournal.size() && live.residentJournal[event.action].kind == 2) {
                    ++live.pivotlessSectionEvents;
                    live.pivotlessAbort = true;
                    continue;
                }
                if (event.object >= live.entries.size() || !live.entries[event.object].owned) continue;
                auto* object = live.entries[event.object].object.data();
                const double x = object->m_positionX, y = object->m_positionY;
                // The section registration helper needs the position at this
                // crossing, not a stale or future CPU mirror.
                object->m_positionX = event.x; object->m_positionY = event.y;
                object->dirtifyObjectPos();
                live.layer->updateObjectSection(object);
                object->m_positionX = x; object->m_positionY = y;
                object->dirtifyObjectPos();
                live.filterSectionChanged(event.object);
                ++live.residentSectionCommits;
            }
            live.internal = previous;
            live.residentSectionCommitWallMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - commitStart).count();
        }
        if (live.groupRotation) live.sectionEventMs +=
            std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();
        return true;
    } catch (const std::exception& error) {
        if (live.residentTimer) live.residentTimer->end();
        ++live.stats.renderFailures;
        log::error("VULKANIZER_V2_RESIDENT disabling compute after {}", error.what());
        live.sectionPotential = 0;
        releaseAll("resident_compute_failure");
        return false;
    }
}

bool queueResidentCommand(Live& live, std::size_t routeIndex, gpu::ResidentTranslation command) {
    if (live.residentJournal.size() >= UINT32_MAX) {
        releaseAll("resident_journal_address_limit");
        return false;
    }
    auto& route = live.routes[routeIndex];
    if (command.kind != 2 && live.groupRotation && !live.layer->m_startOptimization) command.flags |= gpu::residentTrackSections;
    if ((envFlags().visibilityFilter || envFlags().visibilityFilterAudit) && command.kind != 2 &&
        (command.kind == 1 || command.dx != 0 || command.dy != 0) &&
        !(command.flags & gpu::residentTrackSections)) live.filterTrackingLost = true;
    const std::size_t potential = (command.flags & gpu::residentTrackSections) ? route.residentWidth : 0;
    if (live.residentPending.size() >= live.replayCapacity || !live.residentRebases.empty() ||
        (live.sectionPotential && live.sectionPotential + potential > live.replaySectionCapacity)) {
        const char* reason = !live.residentRebases.empty() ? "native_rebase" :
            live.residentPending.size() >= live.replayCapacity ? "command_capacity" : "section_event_capacity";
        if (!flushResident(live, reason)) return false;
    }
    command.journalIndex = static_cast<std::uint32_t>(live.residentJournal.size());
    route.residentActions.push_back(command.journalIndex);
    live.residentJournal.push_back(command);
    const bool queued = command.kind || !live.aggressive || command.dx != 0 || command.dy != 0 || potential;
    if (queued) {
        if (envFlags().temporalReplay) {
            if (live.residentPending.empty()) live.pendingFirstStep = live.ticks;
            live.pendingLastStep = live.ticks;
        }
        live.residentPending.push_back(command);
        if (command.kind == 2) ++live.pendingPivotlessCommands;
        live.sectionPotential += potential;
    }
    for (auto id : route.chains) {
        live.chains[id].lastResidentAction = static_cast<std::uint32_t>(live.residentJournal.size());
        live.markMirrorChain(id);
        if (queued && (envFlags().revocationAudit || envFlags().setterAudit || envFlags().hyperAggressive))
            live.chains[id].lastQueuedGpuAction = static_cast<std::uint32_t>(live.residentJournal.size());
    }
    return true;
}

void validateResident(Live& live) {
    if (!live.residentEnabled || !live.validate || !live.owned() ||
        live.residentValidatedActions == live.residentJournal.size()) return;
    try {
        const auto states = live.renderer->validateResidentPositions();
        for (ObjectId id = 0; id < states.size(); ++id) {
            if (!live.entries[id].owned) continue;
            const auto actual = states[id].position;
            const auto expected = live.entries[id].reference;
            ++live.residentValidationChecks;
            const bool rebased = envFlags().hyperAggressive && live.hyperRebaseObjects.contains(id);
            if (rebased) ++live.hyperRebaseChecks;
            if (std::bit_cast<std::uint64_t>(actual.x) != std::bit_cast<std::uint64_t>(expected.x) ||
                std::bit_cast<std::uint64_t>(actual.y) != std::bit_cast<std::uint64_t>(expected.y)) {
                ++live.residentExactnessViolations;
                if (rebased) ++live.hyperRebaseFailures;
                if (envFlags().hyperAggressive) {
                    const bool x = setter_audit::bits(actual.x) != setter_audit::bits(expected.x);
                    hyperFailure(live, id, "gpu_position", x ? "x" : "y", setter_audit::bits(x ? expected.x : expected.y), setter_audit::bits(x ? actual.x : actual.y));
                }
            }
            if (envFlags().hyperAggressive) {
                // CPU replay keeps its own ordered rotation arithmetic, even
                // when the current native mirror has not consumed the tape.
                const auto replay = live.replayResident(id);
                ++live.hyperAngleChecks;
                const auto ex = setter_audit::bits(replay.rotationX), ey = setter_audit::bits(replay.rotationY);
                const auto ax = setter_audit::bits(states[id].rotationX), ay = setter_audit::bits(states[id].rotationY);
                if (ex != ax || ey != ay) {
                    ++live.hyperAngleFailures;
                    if (live.hyperNegativeInjected && id == live.hyperNegativeObject)
                        live.hyperNegativeCaught = true;
                    hyperFailure(live, id, "gpu_rotation", ex != ax ? "rotation_x" : "rotation_y", ex != ax ? ex : ey, ex != ax ? ax : ay);
                }
            }
        }
        live.residentValidatedActions = live.residentJournal.size();
        if (live.residentExactnessViolations && !envFlags().hyperAggressive) releaseAll("resident_exactness_failure");
    } catch (const std::exception& error) {
        ++live.stats.renderFailures;
        log::error("VULKANIZER_V2_RESIDENT validation failed: {}", error.what());
        releaseAll("resident_validation_failure");
    }
}

constexpr std::uint8_t movePolicyValid = 1u << 0;
constexpr std::uint8_t movePolicyStructurallyEligible = 1u << 1;
constexpr std::uint64_t cachedStructuralPredicateCount = 4;



bool stableMoveSectionEligibility(const GameObject* object) {
    return object &&
        object->m_objectType == GameObjectType::Decoration &&
        object->m_classType == GameObjectClassType::Game &&
        !object->m_isTrigger &&
        !object->m_isStartPos;
}

bool dynamicMoveSectionEligibility(GJBaseGameLayer* layer, GameObject* object) {
    if (!layer || !object ||
        object->m_hasGroupParent ||
        object->m_hasAreaParent ||
        object->m_hasGroupParentsString ||
        object->m_updateParents ||
        object->m_linkedGroup ||
        object->m_parentMode > 0 ||
        object->m_isUIObject ||
        object->m_hasExtendedCollision)
        return false;

    const double x = object->m_positionX;
    const double y = object->m_positionY;
    if (!std::isfinite(x) || !std::isfinite(y) ||
        x > 1'000'000.0 || y > 1'000'000.0 ||
        object->m_outerSectionIndex < 0 ||
        object->m_middleSectionIndex < 0)
        return false;

    const int sx = x <= 0 ? 0 : static_cast<int>(layer->m_sectionXFactor * x);
    const int sy = y <= 0 ? 0 : static_cast<int>(layer->m_sectionYFactor * y);
    return sx == object->m_outerSectionIndex && sy == object->m_middleSectionIndex;
}

void recordSectionCommitKnown(ObjectId id, GameObject* object) {
    if (!state || state->internal || !state->ready) return;
    ++state->stats.nativeCommitCallbacks;
    if (id >= state->entries.size()) return;
    auto& entry = state->entries[id];
    if (entry.object.data() != object || !entry.owned) return;
    if (entry.nativeEdit) return;
    ++state->stats.ownershipCommitViolations;
    const Handle h = state->scene->handle(id);
    if (state->aggressive &&
        (object->m_positionX != entry.historyBase.x || object->m_positionY != entry.historyBase.y)) {
        // An uncovered native writer has already consumed or replaced stale
        // positions. Preserve its result and expose the parity violation.
        entry.historyBase = {object->m_positionX, object->m_positionY};
        state->filterCacheOldSection(id);
        state->filterCacheCurrentSection(id);
        if (entry.spriteApplied == entry.residentApplied && entry.spriteApplied > 0)
            --entry.spriteApplied;
        entry.residentApplied = state->chains[state->scene->chainFor(h)].lastResidentAction;
    }
    handoff(std::span<const Handle>(&h, 1), "unexpected_authoritative_section_consumer");
}

std::string rejection(GameObject* o, const std::unordered_set<int>& protectedGroups, bool uncertainLevel) {
    const auto& flags = envFlags();
    if (o->m_objectType != GameObjectType::Decoration) return "non_decoration";
    if (o->m_classType != GameObjectClassType::Game || o->m_isTrigger || o->m_isStartPos) return "object_class";
    if (uncertainLevel) return "uncertain_level_dependency";
    if (!o->m_groups || !o->m_groupCount) return "no_translation_membership";
    for (short i = 0; i < o->m_groupCount; ++i)
        if (protectedGroups.contains((*o->m_groups)[i])) return "protected_dependency_group";
    // A real-game fixture exercises -1 with no group, area, or linked parent,
    // translation, and a subsequent native handoff. Keep all
    // other nonzero modes conservative until their native semantics are shown.
    if (o->m_hasGroupParent || o->m_hasAreaParent ||
        (o->m_parentMode != 0 && o->m_parentMode != -1) || o->m_linkedGroup) return "parent_dependency";
    if (flags.gpuResident && (o->m_hasGroupParentsString || o->m_updateParents || o->m_hasExtendedCollision))
        return "resident_authoritative_dependency";
    if (o->m_particle || o->m_hasParticles || o->m_hasAnimatedChild || o->m_isUIObject) return "dynamic_visual";
    if (!o->m_isDontEnter) return "native_enter_effect";
    if (!o->m_isDontFade) return "native_fade_effect";
    if (o->m_tempOffsetXRelated || o->m_positionXOffset || o->m_positionYOffset) return "position_offset";
    if (o->m_colorSprite || o->m_glowSprite || o->getChildrenCount()) return "compound_sprite";
    const bool nonIdentity = o->getScaleX() != 1 || o->getScaleY() != 1 || o->getRotationX() || o->getRotationY() ||
        o->getSkewX() || o->getSkewY() || o->isFlipX() || o->isFlipY();
    if (nonIdentity && !flags.staticTransforms) return "unsupported_local_transform";
    const auto anchor = o->getAnchorPoint();
    if (!std::isfinite(o->getScaleX()) || !std::isfinite(o->getScaleY()) ||
        !std::isfinite(o->getRotationX()) || !std::isfinite(o->getRotationY()) ||
        !std::isfinite(o->getSkewX()) || !std::isfinite(o->getSkewY()) ||
        !std::isfinite(anchor.x) || !std::isfinite(anchor.y)) return "nonfinite_local_transform";
    const bool headless = flags.headless && !o->isVisible() && !o->getBatchNode() && !o->getTextureAtlas();
    if (!o->m_isDecoration2 && !(headless && flags.sharedHistory)) return "native_command_history";
    const bool disabledHeadless =
    flags.disabledHeadless &&
    headless &&
    o->m_isGroupDisabled &&
    !o->m_isHide &&
    !o->getParent();

if ((o->m_isGroupDisabled ||
     o->m_isGroupDisabledTemp ||
     o->m_isHide ||
     (!o->isVisible() && !headless)) &&
    !disabledHeadless)
    return "native_inactive";
    if ((!o->getBatchNode() || !o->getTextureAtlas()) && !headless) return "unbatched";
    if (headless) {
        if (o->getParent()) return "headless_unexpected_parent";
        if (!std::isfinite(o->m_positionX) || !std::isfinite(o->m_positionY) ||
            o->m_outerSectionIndex < 0 || o->m_middleSectionIndex < 0) return "invalid_spatial_state";
        const auto bounds = o->boundingBox();
        if (!std::isfinite(bounds.origin.x) || !std::isfinite(bounds.origin.y) ||
            !std::isfinite(bounds.size.width) || !std::isfinite(bounds.size.height) ||
            bounds.size.width <= 0 || bounds.size.height <= 0) return "headless_invalid_bounds";
        return {};
    }
    if (o->getBatchNode()->getShaderProgram() != CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor)) return "custom_shader";
    if (!std::isfinite(o->m_positionX) || !std::isfinite(o->m_positionY) ||
        o->m_outerSectionIndex < 0 || o->m_middleSectionIndex < 0) return "invalid_spatial_state";
    const auto slot = o->getAtlasIndex();
    auto* descendants = o->getBatchNode()->getDescendants();
    if (slot >= descendants->count() || descendants->objectAtIndex(slot) != o) return "unresolved_atlas_order";
    return {};
}
const char* aggressiveRejection(const GameObject* object) {
    if (object->m_objectType != GameObjectType::Decoration) return "non_decoration";
    if (!stableMoveSectionEligibility(object)) return "object_class";
    if (!object->m_groups || !object->m_groupCount) return "no_translation_membership";
    if (object->m_hasExtendedCollision || object->m_isUIObject) return "native_collision_or_ui";
    if (object->m_objectID == 1816) return "special_native_section_callback";
    if (object->m_hasGroupParent || object->m_hasAreaParent || object->m_hasGroupParentsString ||
        object->m_updateParents || object->m_linkedGroup)
        return "native_parent_propagation";
    if (object->m_tempOffsetXRelated || object->m_positionXOffset || object->m_positionYOffset)
        return "native_axis_mask_or_position_offset";
    if (!std::isfinite(object->m_positionX) || !std::isfinite(object->m_positionY) ||
        std::abs(object->m_positionX) > std::numeric_limits<float>::max() ||
        std::abs(object->m_positionY) > std::numeric_limits<float>::max())
        return "nonfinite_position";
    return "";
}
bool residentAtlasCompatible(GameObject* object, CCSpriteBatchNode* batch) {
    return batch && batch->getShaderProgram() ==
        CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor) &&
        !object->m_colorSprite && !object->m_glowSprite && !object->getChildrenCount() &&
        !object->m_particle && !object->m_hasParticles && !object->m_hasAnimatedChild;
}
std::optional<std::array<Corner, 4>> residentQuad(GameObject* object) {
    const auto quad = object->getQuad();
    const ccV3F_C4B_T2F corners[]{quad.bl, quad.br, quad.tl, quad.tr};
    std::array<Corner, 4> result;
    for (unsigned i = 0; i < result.size(); ++i) {
        const auto& v = corners[i];
        if (!std::isfinite(v.vertices.x) || !std::isfinite(v.vertices.y) || !std::isfinite(v.vertices.z) ||
            !std::isfinite(v.texCoords.u) || !std::isfinite(v.texCoords.v)) return std::nullopt;
        result[i] = {v.vertices.x, v.vertices.y, v.vertices.z, v.texCoords.u, v.texCoords.v,
            {v.colors.r, v.colors.g, v.colors.b, v.colors.a}};
    }
    return result;
}
bool probePivotlessRotation(Live& live);
void compile() {
    const auto& flags = envFlags();
    auto& live = *state;
    const auto compileStarted = Clock::now();
    live.compiling = true;
    live.assessed = true;
    live.aggressive = flags.aggressive && !live.baseline;
    live.audit = flags.audit && live.aggressive;
    live.lazyRoutes = flags.lazyRoutes && live.aggressive;
    live.groupRotation = flags.groupRotation && live.aggressive;
    if (flags.temporalReplay && !flags.temporalControl && live.groupRotation) {
        live.replayCapacity = GlRenderer::temporalReplayCapacity;
        // Conservative upper bound: 2M possible events, 64 MiB of SSBO
        // storage at 32 bytes/event. Actual events are still read synchronously.
        live.replaySectionCapacity = 2u << 20;
    }
    // Never use the per-object retained-edit mechanism in the group path.
    live.persistentEdits = flags.persistentEdits && live.aggressive && !live.groupRotation;
    live.sectionXFactor = live.layer->m_sectionXFactor;
    live.sectionYFactor = live.layer->m_sectionYFactor;
    auto validFactor = [](float factor) {
        return std::isfinite(factor) && factor >= 0 &&
            double(nativeSectionLimit * factor) <= std::numeric_limits<int>::max();
    };
    if (live.aggressive && (!validFactor(live.sectionXFactor) || !validFactor(live.sectionYFactor)))
        throw std::runtime_error("Unsupported native section factors");
    flushPendingVisualTransforms(live.layer, true);
    resetRotationPlans();
    std::unordered_set<int> protectedGroups;
    std::unordered_set<int> alphaTargets;
    std::unordered_map<int, std::uint16_t> protectionCauses;
    const bool collectCensus = flags.census;
    // Opt-in: direct, unresolved and inherited color consumers stay native.
    // Native owns camera shake; the renderer consumes GD's current view matrix.
    const bool localizedMaterials = flags.localColorShake;
    std::unordered_set<int> mutableColorChannels;
    bool uncertain = false;
    enum : std::uint16_t { centerReference = 1, xLockRecipient = 2, otherMoveTarget = 4,
        nonMoveTarget = 8, targetMapping = 16, dynamicReference = 32,
        target1346 = 64, target1007 = 128, target1049 = 256, target1006 = 512 };
    auto protect = [&](int group, std::uint16_t cause, bool required = true) {
        if (group <= 0) return;
        if (collectCensus) protectionCauses[group] |= cause;
        if (required && !live.aggressive) protectedGroups.insert(group);
    };
    std::vector<GameObject*> inputObjects;
    for (auto* object : CCArrayExt<GameObject, false>(live.layer->m_objects)) {
        inputObjects.push_back(object);
        if (!object->m_isTrigger || object->m_isStartPos) continue;
        auto* effect = typeinfo_cast<EffectGameObject*>(object);
        if (!effect) { uncertain = true; ++live.unknownTriggers[object->m_objectID]; continue; }
        if (object->m_objectID == 899) {
            ++live.colorTargets[object->m_targetColor];
            if (effect->m_copyColorID) ++live.colorCopy;
            if (object->m_targetColor > 0) mutableColorChannels.insert(object->m_targetColor);
            else if (localizedMaterials) uncertain = true;
        }
        if (object->m_objectID == 1006 && localizedMaterials) {
            // Pulse target modes use different identifiers. Treat both as
            // potentially mutable channels in addition to group protection.
            if (object->m_targetColor > 0) mutableColorChannels.insert(object->m_targetColor);
            if (effect->m_targetGroupID > 0) mutableColorChannels.insert(effect->m_targetGroupID);
            if (object->m_targetColor <= 0 && effect->m_targetGroupID <= 0) uncertain = true;
        }
        if (object->m_objectID == 1520) ++live.shakeTargets[effect->m_targetGroupID];
        if (object->m_objectID == 899 || object->m_objectID == 1520) {
            live.triggeredBySpawn += effect->m_isSpawnTriggered;
            live.triggeredByTouch += effect->m_isTouchTriggered;
        }
        protect(effect->m_centerGroupID, centerReference);
        protect(effect->m_rotationTargetID, centerReference);
        protect(effect->m_targetModCenterID, centerReference);
        if (auto* spawn = typeinfo_cast<SpawnTriggerGameObject*>(object);
            spawn && (!spawn->m_remapObjects.empty() || !spawn->m_remapKeys.empty())) uncertain = true;
        switch (object->m_objectID) {
            case 31: break;
            case 899: case 1520:
                if (!localizedMaterials) { uncertain = true; ++live.unknownTriggers[object->m_objectID]; }
                break;
            case 901:
                if (effect->m_lockToPlayerX)
                    protect(effect->m_targetGroupID, xLockRecipient, !flags.resolvedXlock);
                if (effect->m_useMoveTarget || effect->m_isDynamicMode || effect->m_specialTarget ||
                    effect->m_targetPlayer1 || effect->m_targetPlayer2 ||
                    effect->m_lockToPlayerY || effect->m_lockToCameraX || effect->m_lockToCameraY)
                    protect(effect->m_targetGroupID, otherMoveTarget);
                break;
            case 1346: protect(effect->m_targetGroupID, nonMoveTarget | target1346,
                !flags.lateDependencies); break;
            case 1007:
                protect(effect->m_targetGroupID, nonMoveTarget | target1007, !flags.headlessAlpha);
                alphaTargets.insert(effect->m_targetGroupID);
                break;
            case 1049: protect(effect->m_targetGroupID, nonMoveTarget | target1049,
                !controlledToggleVisibility() && !flags.lateDependencies); break;
            case 1006: protect(effect->m_targetGroupID, nonMoveTarget | target1006); break;
            case 1268: case 1616: case 1612: case 1613: case 1812: case 1814: case 1818: case 1819:
                break;
            default: uncertain = true; ++live.unknownTriggers[object->m_objectID]; break;
        }
    }
    for (auto* mod : Loader::get()->getAllMods()) {
        if (!mod->isLoaded() || mod == Mod::get() || mod->getID() == "geode.loader") continue;
        uncertain = true;
        live.unknownMods.emplace_back(mod->getID().view());
    }
    for (const auto& [group, target] : live.layer->m_targetGroups) {
        protect(group, targetMapping); protect(target.first, targetMapping); protect(target.second, targetMapping);
    }
    for (const auto& calculation : live.layer->m_effectManager->m_unkVector708) {
        auto* object = calculation.m_gameObject;
        if (object && object->m_groups) for (short i = 0; i < object->m_groupCount; ++i) protect((*object->m_groups)[i], dynamicReference);
    }
    // Native getters prepare the lazy optimized arrays before ownership is
    // compiled. Trigger activation and action ordering remain native.
    std::unordered_set<int> membershipGroups;
    for (auto* object : inputObjects) if (object->m_groups)
        for (short i = 0; i < object->m_groupCount; ++i) membershipGroups.insert((*object->m_groups)[i]);
    for (auto group : membershipGroups) {
        if (group <= 0 || static_cast<std::size_t>(group) >= live.layer->m_groups.size()) continue;
        live.layer->getGroup(group);
        live.layer->getOptimizedGroup(group);
        live.layer->getStaticGroup(group);
    }
    // Resolve atlas order from native descendants. The host's atlas call order
    // remains authoritative across materials, clipping nodes and batch nodes.
    std::unordered_set<GameObject*> all(inputObjects.begin(), inputObjects.end()), inserted;
    std::vector<GameObject*> ordered;
    std::unordered_set<CCSpriteBatchNode*> batches;
    std::unordered_map<CCSpriteBatchNode*, MaterialId> materials;
    for (auto* object : inputObjects) {
        auto* batch = object->getBatchNode();
        if (!batch || !batches.insert(batch).second) continue;
        materials.emplace(batch, static_cast<MaterialId>(materials.size()));
        for (auto* sprite : CCArrayExt<CCSprite, false>(batch->getDescendants())) {
            auto* candidate = typeinfo_cast<GameObject*>(sprite);
            if (all.contains(candidate) && inserted.insert(candidate).second) ordered.push_back(candidate);
        }
    }
    for (auto* object : inputObjects) if (inserted.insert(object).second) ordered.push_back(object);
    live.total = ordered.size();
    live.entries.resize(ordered.size());
    std::vector<Decoration> decorations(ordered.size());
    std::vector<std::string> rejectionReasons(ordered.size());
    std::map<std::tuple<int, int, std::optional<std::uint32_t>, bool>, std::uint32_t> headlessPartitions;
    auto independentColor = [&](GJSpriteColor* color) {
        if (!color) return false;
        const int id = color->getColorMode();
        if (id <= 0 || mutableColorChannels.contains(id) ||
            (color->m_defaultColorID > 0 && mutableColorChannels.contains(color->m_defaultColorID)) ||
            color->m_usesHSV || color->m_usesCustomBlend) return false;
        auto* action = live.layer->m_effectManager->getColorAction(id);
        return action && action->m_copyID == 0 && !action->m_inheritanceNode && action->m_playerColor <= 0;
    };
    for (ObjectId id = 0; id < ordered.size(); ++id) {
        auto* object = ordered[id];
        if (id < 4 && flags.colorShakeFixture) {
            auto* color = object->m_baseColor;
            auto* action = color && color->getColorMode() > 0 ? live.layer->m_effectManager->getColorAction(color->getColorMode()) : nullptr;
            log::info("VULKANIZER_V2_COLOR_FIXTURE base_present={} id={} default_id={} action_present={} copy={} inheritance={} player={} hsv={} custom_blend={} detail_present={}",
                color != nullptr, color ? color->m_colorID : -1, color ? color->m_defaultColorID : -1,
                action != nullptr, action ? action->m_copyID : -1, action && action->m_inheritanceNode,
                action ? action->m_playerColor : -1, color && color->m_usesHSV,
                color && color->m_usesCustomBlend, object->m_detailColor != nullptr);
        }
        auto& entry = live.entries[id];
        entry.object = object;
        entry.reference = {object->m_positionX, object->m_positionY};
        if (flags.gpuResident) {
            entry.historyBase = entry.reference;
            entry.initialPrevious = {object->m_lastPosition.x, object->m_lastPosition.y};
            entry.residentStamp = static_cast<std::uint32_t>(object->m_unk4C4);
        }
        live.ids[object] = id;
        decorations[id].position = entry.reference;
        const auto anchor = object->getAnchorPoint();
        decorations[id].nativeTransform = {entry.reference, object->getRotationX(), object->getRotationY(),
            object->getScaleX(), object->getScaleY(), object->getSkewX(), object->getSkewY(),
            anchor.x, anchor.y, object->isFlipX(), object->isFlipY()};
        decorations[id].dependency = Dependency::authoritativeCpu;
        if (live.aggressive) {
            const auto* reason = aggressiveRejection(object);
            if (*reason) {
                if (!std::isfinite(entry.reference.x) || !std::isfinite(entry.reference.y) ||
                    std::abs(entry.reference.x) > std::numeric_limits<float>::max() ||
                    std::abs(entry.reference.y) > std::numeric_limits<float>::max())
                    decorations[id].position = {};
                rejectionReasons[id] = reason;
                if (flags.revocationAudit && std::string_view(reason) == "native_parent_propagation" && object->m_groups) {
                    const unsigned mask = (object->m_hasGroupParent ? 1u : 0u) |
                        (object->m_hasAreaParent ? 2u : 0u) | (object->m_hasGroupParentsString ? 4u : 0u) |
                        (object->m_updateParents ? 8u : 0u) | (object->m_linkedGroup ? 16u : 0u);
                    for (const int target : {575, 848, 815}) {
                        bool member = false;
                        for (short i = 0; i < object->m_groupCount; ++i) member |= (*object->m_groups)[i] == target;
                        if (member) ++live.initialParentMasks[target][mask];
                    }
                }
                continue;
            }
            if (flags.setterAudit && setterShadowMember(object, flags.setterShadowGroup)) {
                live.setterShadowTargets.push_back(id);
                if (flags.setterShadowNative) {
                    rejectionReasons[id] = "setter_shadow_native_reference";
                    continue;
                }
            }
            decorations[id].dependency = Dependency::decorativeTranslation;
            if (flags.hyperAggressive && object->m_groupCount == 1 && setterShadowMember(object, 554))
                live.hyperRebaseWatch.push_back(id);
            auto* batch = object->getBatchNode();
            auto* texture = object->getTextureAtlas();
            entry.headless = !batch && !texture;
            decorations[id].visible = object->isVisible();
            if (!batch || !texture) continue;
            auto& atlas = live.atlases[texture];
            atlas.batch = batch;
            const auto slot = object->getAtlasIndex();
            if (residentAtlasCompatible(object, batch) && object->isVisible() &&
                slot < batch->getDescendants()->count() && batch->getDescendants()->objectAtIndex(slot) == object) {
                object->updateTransform();
                if (const auto quad = residentQuad(object)) {
                    entry.batch = batch;
                    entry.atlas = texture;
                    entry.slot = slot;
                    entry.residentQuad = true;
                    decorations[id].material = materials.at(batch);
                    decorations[id].quad = *quad;
                    atlas.objects.push_back(id);
                    continue;
                }
            }
            atlas.nativeResidents.push_back(id);
            continue;
        }
        auto reason = rejection(object, protectedGroups, uncertain);
        const bool headlessMaterial = flags.headlessMaterial &&
            flags.headless && !object->isVisible() && !object->getBatchNode() && !object->getTextureAtlas();
        if (reason.empty() && !headlessMaterial && flags.headlessAlpha)
            for (short i = 0; i < object->m_groupCount; ++i)
                if (alphaTargets.contains((*object->m_groups)[i])) { reason = "native_alpha_render_state"; break; }
        if (reason.empty() && localizedMaterials && !headlessMaterial && !mutableColorChannels.empty() &&
            (!independentColor(object->m_baseColor) || (object->m_detailColor && !independentColor(object->m_detailColor))))
            reason = "mutable_or_unresolved_color_channel";
        if (reason == "native_inactive") {
            if (!object->isVisible()) ++live.inactiveReadiness["invisible"];
            if (object->m_isGroupDisabled || object->m_isGroupDisabledTemp || object->m_isHide)
                ++live.inactiveReadiness["logically_disabled_or_hidden"];
            if (object->getBatchNode() && object->getTextureAtlas()) {
                ++live.inactiveReadiness["batched"];
                if (!object->isVisible() && !object->m_isGroupDisabled && !object->m_isGroupDisabledTemp && !object->m_isHide)
                    ++live.inactiveReadiness["batched_invisible_logically_active"];
            }
        }
        if (!reason.empty()) { rejectionReasons[id] = reason; continue; }
        if (!object->isVisible() && !object->getBatchNode() && !object->getTextureAtlas()) {
            entry.headless = true;
            entry.disabledHeadless = flags.disabledCamera && object->m_isGroupDisabled;
            if (!object->m_isDecoration2) {
                entry.initialStamp = static_cast<std::uint32_t>(object->m_unk4C4);
                entry.historyBase = entry.reference;
                entry.initialPrevious = {object->m_lastPosition.x, object->m_lastPosition.y};
            }
            decorations[id].visible = false;
            decorations[id].dependency = Dependency::decorativeTranslation;
            const auto key = std::make_tuple(object->m_outerSectionIndex, object->m_middleSectionIndex, entry.initialStamp, entry.disabledHeadless);
            auto [partition, added] = headlessPartitions.try_emplace(key, static_cast<std::uint32_t>(headlessPartitions.size() + 1));
            decorations[id].chainPartition = partition->second;
            continue;
        }
        object->updateTransform();
        const auto quad = object->getQuad();
        const ccV3F_C4B_T2F corners[]{quad.bl, quad.br, quad.tl, quad.tr};
        bool finiteQuad = true;
        for (const auto& corner : corners)
            finiteQuad &= std::isfinite(corner.vertices.x) && std::isfinite(corner.vertices.y) &&
                std::isfinite(corner.vertices.z) && std::isfinite(corner.texCoords.u) && std::isfinite(corner.texCoords.v);
        if (!finiteQuad) { rejectionReasons[id] = "nonfinite_baked_quad"; continue; }
        entry.batch = object->getBatchNode();
        entry.atlas = object->getTextureAtlas();
        entry.slot = object->getAtlasIndex();
        auto& atlas = live.atlases[entry.atlas.data()];
        atlas.batch = entry.batch;
        atlas.objects.push_back(id);
        decorations[id].material = static_cast<MaterialId>(std::distance(batches.begin(), batches.find(entry.batch.data())));
        decorations[id].dependency = Dependency::decorativeTranslation;
        for (unsigned corner = 0; corner < 4; ++corner) {
            const auto& value = corners[corner];
            decorations[id].quad[corner] = {value.vertices.x, value.vertices.y, value.vertices.z,
                value.texCoords.u, value.texCoords.v, {value.colors.r, value.colors.g, value.colors.b, value.colors.a}};
        }
    }
    if (collectCensus) {
        live.censusMasks.resize(ordered.size());
        live.censusProtectionCauses.resize(ordered.size());
        std::set<std::vector<short>> candidateChains;
        std::set<std::vector<short>> v1ParentChains;
        std::set<std::vector<short>> structuralParentChains;
        for (ObjectId id = 0; id < ordered.size(); ++id) {
            auto* o = ordered[id];
            std::uint64_t mask = 0;
            auto flag = [&](unsigned bit, bool value) { if (value) mask |= 1ull << bit; };
            flag(0, o->m_objectType != GameObjectType::Decoration);
            flag(1, o->m_classType != GameObjectClassType::Game || o->m_isTrigger || o->m_isStartPos);
            flag(2, !o->m_groups || !o->m_groupCount);
            bool protectedMember = false;
            std::uint16_t causes = 0;
            std::vector<short> groups;
            if (o->m_groups) for (short j = 0; j < o->m_groupCount; ++j) {
                const auto group = (*o->m_groups)[j];
                groups.push_back(group);
                protectedMember |= protectedGroups.contains(group);
                auto found = protectionCauses.find(group);
                if (found != protectionCauses.end()) causes |= found->second;
            }
            flag(3, protectedMember);
            flag(4, o->m_hasGroupParent || o->m_hasAreaParent || (o->m_parentMode != 0 && o->m_parentMode != -1) || o->m_linkedGroup);
            flag(5, o->m_particle || o->m_hasParticles || o->m_hasAnimatedChild || o->m_isUIObject);
            flag(6, !o->m_isDecoration2 || !o->m_isDontEnter || !o->m_isDontFade);
            flag(7, o->m_tempOffsetXRelated || o->m_positionXOffset || o->m_positionYOffset);
            flag(8, o->m_colorSprite || o->m_glowSprite || o->getChildrenCount());
            flag(9, o->getScaleX() != 1 || o->getScaleY() != 1 || o->getRotationX() || o->getRotationY() || o->getSkewX() || o->getSkewY() || o->isFlipX() || o->isFlipY());
            flag(10, !o->isVisible() || o->m_isGroupDisabled || o->m_isGroupDisabledTemp || o->m_isHide);
            flag(11, !o->getBatchNode() || !o->getTextureAtlas());
            flag(12, o->getBatchNode() && o->getBatchNode()->getShaderProgram() != CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor));
            flag(13, !std::isfinite(o->m_positionX) || !std::isfinite(o->m_positionY) || o->m_outerSectionIndex < 0 || o->m_middleSectionIndex < 0);
            flag(14, uncertain);
            flag(15, (o->m_baseColor && live.colorTargets.contains(o->m_baseColor->m_colorID)) ||
                     (o->m_detailColor && live.colorTargets.contains(o->m_detailColor->m_colorID)));
            flag(16, !live.shakeTargets.empty());
            flag(17, o->m_hasGroupParent); flag(18, o->m_hasAreaParent); flag(19, o->m_parentMode > 0);
            flag(20, o->m_linkedGroup != 0); flag(21, o->m_particle != nullptr); flag(22, o->m_hasParticles);
            flag(23, o->m_hasAnimatedChild); flag(24, o->m_isUIObject); flag(25, !o->m_isDecoration2);
            flag(26, !o->m_isDontEnter); flag(27, !o->m_isDontFade); flag(28, o->m_tempOffsetXRelated);
            flag(29, o->m_positionXOffset != 0); flag(30, o->m_positionYOffset != 0);
            flag(31, o->m_colorSprite != nullptr); flag(32, o->m_glowSprite != nullptr); flag(33, o->getChildrenCount() != 0);
            flag(34, o->getScaleX() != 1 || o->getScaleY() != 1); flag(35, o->getRotationX() != 0 || o->getRotationY() != 0);
            flag(36, o->getSkewX() != 0 || o->getSkewY() != 0); flag(37, o->isFlipX() || o->isFlipY());
            flag(38, o->m_parentMode < 0);
            live.censusMasks[id] = mask;
            live.censusProtectionCauses[id] = causes;
            if (!(mask & (0x3ffu & ~(1u << 4) & ~(1u << 9))) && !(mask & ((1ull<<17)|(1ull<<18)|(1ull<<19)|(1ull<<20)))) {
                auto chainGroups = groups;
                std::sort(chainGroups.begin(), chainGroups.end());
                chainGroups.erase(std::unique(chainGroups.begin(), chainGroups.end()), chainGroups.end());
                structuralParentChains.insert(std::move(chainGroups));
            }
            // Visibility and atlas readiness are observations at capture time.
            // Exclude them and the unresolved level-wide veto from this upper bound.
            constexpr std::uint32_t ignored = (1u << 10) | (1u << 11) | (1u << 13) | (1u << 14);
            if (!(mask & 0x3ffu)) {
                std::sort(groups.begin(), groups.end());
                groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
                candidateChains.insert(groups);
            }
            if (!(mask & (0x3ffu & ~(1u << 4))) && !(mask & ((1ull<<17)|(1ull<<18)|(1ull<<19)|(1ull<<20)))) {
                std::sort(groups.begin(), groups.end());
                groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
                v1ParentChains.insert(std::move(groups));
            }
        }
        live.censusCandidateChains = candidateChains.size();
        live.censusV1ParentCandidateChains = v1ParentChains.size();
        live.censusStructuralParentChains = structuralParentChains.size();
    }
    // Every actual native array gets a token. Optimized/static variants may
    // contain different members, so a numeric GD group ID is insufficient.
    auto routeArray = [&](CCArray* array, int nativeGroup) {
        if (!array || !array->count()) return;
        if (auto existing = live.routeIndices.find(array); existing != live.routeIndices.end()) {
            if (live.routes[existing->second].nativeGroup != nativeGroup)
                live.routes[existing->second].nativeGroup = -1;
            return;
        }
        Route route;
        route.nativeGroup = nativeGroup;
        route.token = static_cast<GroupId>(live.routes.size() + 1);
        route.source = array;
        route.native = CCArray::createWithCapacity(array->count());
        route.count = array->count();
        std::unordered_set<ObjectId> unique;
        bool unknownMember = false;
        unsigned slot = 0;
        for (auto* object : CCArrayExt<GameObject, false>(array)) {
            const auto sourceSlot = slot++;
            auto found = live.ids.find(object);
            if (found == live.ids.end()) { unknownMember = true; continue; }
            if (!unique.insert(found->second).second) {
                decorations[found->second].dependency = Dependency::authoritativeCpu;
                if (rejectionReasons[found->second].empty()) rejectionReasons[found->second] = "duplicate_native_array_member";
                continue;
            }
            route.members.push_back(found->second);
            live.entries[found->second].memberships.emplace_back(live.routes.size(), sourceSlot);
            decorations[found->second].groups.push_back(route.token);
        }
        if (unknownMember && !live.aggressive) for (auto id : route.members) {
            decorations[id].dependency = Dependency::authoritativeCpu;
            if (rejectionReasons[id].empty()) rejectionReasons[id] = "unknown_array_member";
        }
        live.routeIndices[array] = live.routes.size();
        live.routes.push_back(std::move(route));
    };
    auto routeBuffers = [&](const auto& arrays) {
        for (std::size_t id = 0; id < arrays.size(); ++id) routeArray(arrays[id], static_cast<int>(id));
    };
    routeBuffers(live.layer->m_groups);
    routeBuffers(live.layer->m_optimizedGroups);
    routeBuffers(live.layer->m_staticGroups);
    for (ObjectId id = 0; id < decorations.size(); ++id) {
        if (decorations[id].dependency == Dependency::decorativeTranslation && decorations[id].groups.empty()) {
            decorations[id].dependency = Dependency::authoritativeCpu;
            rejectionReasons[id] = "unresolved_native_group";
        }
    }
    live.scene.emplace(Scene::compile(decorations));
    auto addShadowSample = [&](ObjectId id) {
        live.setterShadowSamples.push_back(id);
        const auto& p = live.entries[id].reference;
        live.setterShadowInitial[id] = {setter_audit::bits(p.x), setter_audit::bits(p.y)};
    };
    if (flags.setterShadowAligned && flags.setterShadowGroup) {
        // Atlas traversal order is NOT an identity. Use the original level
        // object array index, in both runs, before ownership is applied.
        std::unordered_map<GameObject*, std::uint64_t> sourceIndex;
        for (std::size_t i = 0; i < inputObjects.size(); ++i) sourceIndex.emplace(inputObjects[i], i);
        auto targets = live.setterShadowTargets;
        std::vector<ObjectId> controls;
        for (ObjectId id = 0; id < live.entries.size(); ++id) {
            auto* o = live.entries[id].object.data();
            if (setterShadowMember(o, flags.setterShadowGroup) && *aggressiveRejection(o)) controls.push_back(id);
        }
        auto select = [&](std::vector<ObjectId>& population, std::size_t limit, bool control) {
            std::sort(population.begin(), population.end(), [&](ObjectId a, ObjectId b) {
                return sourceIndex.at(live.entries[a].object.data()) < sourceIndex.at(live.entries[b].object.data());
            });
            const auto n = std::min(limit, population.size());
            for (std::size_t i = 0; i < n; ++i) {
                const auto id = population[i * population.size() / n];
                auto* o = live.entries[id].object.data();
                addShadowSample(id);
                live.alignedShadowIdentities[id] = {sourceIndex.at(o), setterSnapshot(o).groupHash,
                    static_cast<int>(o->m_objectID), control};
            }
        };
        select(targets, 512, false);
        select(controls, 128, true);
        if (controls.size() < 128) {
            // Some groups have almost no initially native members (g575: 2).
            // Supplement with independently ineligible native decorations.
            std::vector<ObjectId> extra;
            for (ObjectId id = 0; id < live.entries.size(); ++id) {
                auto* o = live.entries[id].object.data();
                if (!setterShadowMember(o, flags.setterShadowGroup) && o->m_objectType == GameObjectType::Decoration &&
                    *aggressiveRejection(o) && std::isfinite(o->m_positionX) && std::isfinite(o->m_positionY)) extra.push_back(id);
            }
            select(extra, 128 - controls.size(), true);
        }
    } else {
        // Original lightweight audit; unchanged outside aligned mode.
        for (std::size_t i = 0, n = std::min<std::size_t>(128, live.setterShadowTargets.size()); i < n; ++i)
            addShadowSample(live.setterShadowTargets[i * live.setterShadowTargets.size() / n]);
    }
    live.chains.resize(live.scene->chainCount());
    if (flags.visibilityCensus) live.visibilityLastFrame.assign(live.entries.size(), UINT64_MAX);
    if (flags.hyperAggressive) live.mirrorWorklist.initialize(live.chains.size(), live.entries.size());
    if (live.aggressive) live.sectionIndices.resize(live.chains.size());
    for (ObjectId id = 0; id < live.entries.size(); ++id) {
        auto h = live.scene->handle(id);
        if (live.scene->path(h) != ExecutionPath::sharedChain) continue;
        auto& entry = live.entries[id];
        auto& chain = live.chains[live.scene->chainFor(h)];
        if (flags.gpuResident && chain.objects.empty()) chain.residentGroups = decorations[id].groups;
        chain.disabledHeadless = entry.disabledHeadless;
        if (entry.initialStamp && !chain.history)
            chain.history.emplace(SharedHistoryPrototype::RouteEvidence{decorations[id].groups, *entry.initialStamp, false});
        chain.objects.push_back(h);
        if (flags.hyperAggressive) appendMirrorObject(chain.mirrorMask, id);
        if (live.aggressive) {
            entry.sectionOrigin = entry.reference;
            chain.precision.coordinateMagnitude = std::max({chain.precision.coordinateMagnitude,
                std::abs(entry.reference.x), std::abs(entry.reference.y)});
            live.indexSections(id, live.scene->chainFor(h));
            continue;
        }
        auto* spaceNode = entry.headless ? static_cast<CCNode*>(live.layer->m_objectLayer) : static_cast<CCNode*>(entry.batch.data());
        auto space = std::find_if(chain.spaces.begin(), chain.spaces.end(), [&](const auto& value) { return value.batch.data() == spaceNode && value.headless == entry.headless; });
        const auto& quad = decorations[id].quad;
        double left = quad[0].x, right = left, bottom = quad[0].y, top = bottom;
        if (entry.headless) {
            auto bounds = entry.object->boundingBox();
            left = bounds.origin.x + entry.reference.x - entry.object->getPositionX();
            bottom = bounds.origin.y + entry.reference.y - entry.object->getPositionY();
            right = left + bounds.size.width;
            top = bottom + bounds.size.height;
        } else {
            for (const auto& corner : quad) {
                left = std::min<double>(left, corner.x); right = std::max<double>(right, corner.x);
                bottom = std::min<double>(bottom, corner.y); top = std::max<double>(top, corner.y);
            }
        }
        if (space == chain.spaces.end()) chain.spaces.push_back({spaceNode, left, right, bottom, top, entry.headless});
        else { space->left = std::min(space->left, left); space->right = std::max(space->right, right);
            space->bottom = std::min(space->bottom, bottom); space->top = std::max(space->top, top); }
        chain.precision.coordinateMagnitude = std::max({chain.precision.coordinateMagnitude,
            std::abs(left), std::abs(right), std::abs(bottom), std::abs(top), std::abs(entry.reference.x), std::abs(entry.reference.y)});
        auto* object = entry.object.data();
        const double fx = live.layer->m_sectionXFactor, fy = live.layer->m_sectionYFactor;
        if (fx < 0 || fy < 0 || (fx == 0 && object->m_outerSectionIndex != 0) ||
            (fy == 0 && object->m_middleSectionIndex != 0)) { chain.minDx = 1; chain.maxDx = -1; }
        if (fx > 0) {
            if (object->m_outerSectionIndex > 0)
                chain.minDx = std::max(chain.minDx, object->m_outerSectionIndex / fx - entry.reference.x);
            chain.maxDx = std::min(chain.maxDx, (object->m_outerSectionIndex + 1) / fx - entry.reference.x);
        }
        if (fy > 0) {
            if (object->m_middleSectionIndex > 0)
                chain.minDy = std::max(chain.minDy, object->m_middleSectionIndex / fy - entry.reference.y);
            chain.maxDy = std::min(chain.maxDy, (object->m_middleSectionIndex + 1) / fy - entry.reference.y);
        }
        if (live.validate && id < 2) log::info("VULKANIZER_V2_BASE object={} native=({},{}) node=({},{}) section=({},{}) factors=({},{}) quad=({},{})",
            id, object->m_positionX, object->m_positionY, object->getPositionX(), object->getPositionY(),
            object->m_outerSectionIndex, object->m_middleSectionIndex, fx, fy, left, bottom);
    }
    std::vector<Handle> nativeOnly;
    for (ChainId id = 0; id < live.chains.size(); ++id) {
        auto& chain = live.chains[id];
        const auto* reason = live.aggressive ? nullptr : unsafe(id, {}, 0);
        if (live.validate && reason && id < 8) {
            const auto view = chain.spaces.empty() ? std::optional<Camera>{} : camera(chain.spaces.front().batch.data());
            log::info("VULKANIZER_V2_ADMISSION chain={} reason={} section_delta=({},{},{},{}) camera=({},{},{},{})",
                id, reason, chain.minDx, chain.maxDx, chain.minDy, chain.maxDy,
                view ? view->left : 0, view ? view->right : 0, view ? view->bottom : 0, view ? view->top : 0);
        }
        if (reason || live.baseline) {
            if (reason) for (auto h : chain.objects) rejectionReasons[h.object] = reason;
            else live.eligible += chain.objects.size();
            nativeOnly.insert(nativeOnly.end(), chain.objects.begin(), chain.objects.end());
        } else {
            live.eligible += chain.objects.size();
            for (auto h : chain.objects) {
                auto& entry = live.entries[h.object];
                entry.owned = true;
                ++chain.owned;
                ++live.stats.acquired;
                if (entry.headless) ++live.stats.headlessAcquired;
                if (entry.atlas) live.atlases.at(entry.atlas.data()).plan.insert(
                    entry.slot, live.scene->drawFor(h), entry.object->isVisible());
            }
        }
    }
    // Rejected candidates have never been owned. The sink performs no writes.
    struct AdmissionSink : NativeSink {
        void translate(std::span<const ObjectId>, Translation, std::uint64_t) noexcept override {}
        void materialize(ObjectId, Translation, bool) noexcept override {}
        void visibility(ObjectId, bool) noexcept override {}
        void reset(ObjectId, Translation, bool) noexcept override {}
    } admission;
    live.scene->acquireNativeBatch(nativeOnly, admission);
    for (const auto& reason : rejectionReasons) if (!reason.empty()) ++live.rejected[reason];
    if (collectCensus || flags.spikeAudit) {
    live.initialMoveRejections = rejectionReasons;

    for (std::size_t id = 0; id < live.entries.size(); ++id) {
        if (live.initialMoveRejections[id].empty()) {
            live.initialMoveRejections[id] =
                live.entries[id].owned
                    ? "initially_admitted"
                    : "native_without_recorded_reason";
        }
    }
}
    live.rebuildNativeRoutes();
    if (live.audit)
        for (auto& route : live.routes) route.auditInitialOwned = route.owned;
    if (!ordered.empty() &&
        (flags.denseOwners || live.cachedPoliciesEnabled || live.aggressive)) {

    int minId = ordered.front()->m_uniqueID;
    int maxId = minId;

    for (auto* object : ordered) {
        minId = std::min(minId, object->m_uniqueID);
        maxId = std::max(maxId, object->m_uniqueID);
    }

    const auto span =
        static_cast<std::int64_t>(maxId) - minId + 1;

    constexpr ObjectId invalid =
    std::numeric_limits<ObjectId>::max();

    if (span > 0 && span <= 4000000) {
        live.denseIdBase = minId;
        live.denseIds.assign(static_cast<std::size_t>(span), invalid);
        if (live.cachedPoliciesEnabled) live.moveSectionPolicies.assign(static_cast<std::size_t>(span), 0);
        std::vector<std::uint8_t> duplicateIds(live.cachedPoliciesEnabled ? static_cast<std::size_t>(span) : 0, 0);

        for (ObjectId id = 0; id < ordered.size(); ++id) {
            const auto denseIndex = static_cast<std::size_t>(static_cast<std::int64_t>(ordered[id]->m_uniqueID) - minId);
            auto& slot = live.denseIds[denseIndex];

            if (slot == invalid)
                slot = id;
            else if (live.cachedPoliciesEnabled)
                duplicateIds[denseIndex] = 1;
        }

        if (live.cachedPoliciesEnabled) {
            live.movePolicySceneGeneration = live.scene->generation();
            if (live.unknownMods.empty()) {
                for (std::size_t denseIndex = 0; denseIndex < live.denseIds.size(); ++denseIndex) {
                    const auto id = live.denseIds[denseIndex];
                    if (id == invalid || duplicateIds[denseIndex]) continue;
                    auto flags = movePolicyValid;
                    if (stableMoveSectionEligibility(live.entries[id].object.data()))
                        flags |= movePolicyStructurallyEligible;
                    live.moveSectionPolicies[denseIndex] = flags;
                }
            }
            log::info("VULKANIZER_V2_CACHED_POLICIES enabled=true slots={} scene_generation={} third_party_mods={} stable_predicates=4",
                live.moveSectionPolicies.size(), live.movePolicySceneGeneration, live.unknownMods.size());
        }

        if (flags.denseOwners)
            log::info("VULKANIZER_DENSE_OWNERS objects={} slots={}", ordered.size(), live.denseIds.size());
    }
}
    live.context = wglGetCurrentContext();
    if (live.owned()) {
        live.renderer = std::make_unique<GlRenderer>(resolve);
        if (flags.gpuResident) {
            for (auto& route : live.routes) route.residentWidth = route.owned;
            std::vector<ResidentObject> objects(live.entries.size());
            // Scene route tokens start at one. The GPU uses dense zero-based
            // route indices and a single immutable membership upload.
            for (ObjectId id = 0; id < objects.size(); ++id) {
                auto& groups = decorations[id].groups;
                for (auto& group : groups) --group;
                auto* object = live.entries[id].object.data();
                objects[id] = {live.entries[id].reference, live.entries[id].owned ? std::span<const GroupId>(groups) : std::span<const GroupId>{},
                    live.groupRotation ? object->getRotationX() : 0.f, live.groupRotation ? object->getRotationY() : 0.f,
                    object->m_outerSectionIndex, object->m_middleSectionIndex, bool(object->m_canRotateFree)};
            }
            live.renderer->attachResident(*live.scene, objects, live.routes.size(), live.replayCapacity);
            live.residentJournal.reserve(65536);
            live.residentPending.reserve(live.replayCapacity);
            live.residentRetired.reserve(live.owned());
            live.residentEnabled = true;
            if (!flags.performanceOnly || flags.fixedProfile) {
                try { live.residentTimer = std::make_unique<FrameGpuTimer>(resolve); } catch (const std::exception&) {}
            }
        } else live.renderer->attach(*live.scene);
    }
    if (!flags.performanceOnly) {
        try { live.gpuTimer = std::make_unique<FrameGpuTimer>(resolve); live.gpuStatus = "async_timestamps_first_atlas_to_swap"; }
        catch (const std::exception& error) { live.gpuStatus = error.what(); }
    }
    if (flags.hyperAggressive && live.residentEnabled && !probePivotlessRotation(live)) {
        log::warn("VULKANIZER_HYPER native angle-only parity probe failed; any owned pivotless command will abort the test");
    }
    live.initializeVisibilityFilter();
    live.initializeVisibilityOwnershipShadow();
    live.ready = true;
    live.compiling = false;
    live.sceneCompileMs = std::chrono::duration<double, std::milli>(Clock::now() - compileStarted).count();
    log::info("VULKANIZER_V2_COMPILE scene={} level={} total={} eligible={} owned={} chains={} native_arrays={} atlas_materials={} native_order=atlas_ranges baseline={}",
        live.scene->generation(), live.levelName, live.total, live.eligible, live.owned(), live.scene->chainCount(), live.routes.size(), live.atlases.size(), live.baseline);
    for (const auto& [reason, count] : live.rejected) log::info("VULKANIZER_V2_REJECT reason={} objects={}", reason, count);
    if (live.owned()) log::info("VULKANIZER_V2_OWNERSHIP scene={} transition=Native_to_V2 objects={} reason=level_compilation", live.scene->generation(), live.owned());
}
double percentile(std::span<const double> input, double fraction) {
    std::vector<double> values(input.begin(), input.end());
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(std::ceil(values.size() * fraction)) - 1];
}
matjson::Value retentionAudit(const Live& live) {
    const auto& t = live.auditTimes;
    auto result = matjson::makeObject({
        {"lazy_routes", live.lazyRoutes},
        {"native_array_calls", t.nativeCalls},
        {"native_move_inclusive_ms", t.nativeMoveMs},
        {"native_move_excluding_nested_handoff_and_sync_ms", t.nativeMoveExclusiveMs},
        {"native_atlas_draw_inclusive_ms", t.nativeDrawMs},
        {"frame_elapsed_sum_ms", t.frameMs},
        {"native_move_exclusive_frame_share", t.frameMs > 0 ? t.nativeMoveExclusiveMs / t.frameMs : 0.0},
        {"route_refreshes", t.refreshes}, {"route_refresh_ms_inclusive", t.refreshMs},
        {"route_refresh_source_slots", t.refreshSlots},
        {"max_revocations_per_frame", live.auditMaxRevocationsPerFrame},
        {"max_handoff_and_sync_ms_per_frame", live.auditMaxHandoffPerFrame},
        {"uncompiled_native_visits", live.auditUncompiledVisits},
        {"base_section_samples", live.auditSectionSamples},
        {"base_section_sample_ms", live.auditSectionSampleMs},
        {"base_section_sample_weight", 1024},
        {"base_section_sample_scope", "forwarded native move, base hook, after skips; included in native array time"},
        {"handoff_exclusive_ms", live.residentHandoffMs},
        {"attribution_overhead_ms_in_handoff", live.auditAttributionMs},
        {"sync_ms", live.residentSyncMs},
        {"simulation_ticks", live.ticks}});
    result["sync_ms_by_reason"] = matjson::Value::object();
    for (const auto& [reason, ms] : live.residentSyncByReason)
        result["sync_ms_by_reason"][std::string(reason)] = ms;
    result["position_setter_revocations_by_phase"] = matjson::Value::object();
    for (const auto& [phase, count] : live.auditSetterPhases)
        result["position_setter_revocations_by_phase"][phase] = count;
    result["rotation_claims"] = matjson::Value::array();
    for (const auto& [key, claim] : live.auditRotationClaims)
        result["rotation_claims"].push(matjson::makeObject({
            {"target_group", key.first}, {"optimized_array", key.second},
            {"calls", claim.calls}, {"recipient_slots_over_all_claims", claim.recipientSlots},
            {"released", claim.released}, {"first_level_time", claim.first}, {"last_level_time", claim.last}}));
    result["rotation_array_calls"] = matjson::Value::array();
    for (const auto& [group, rotation] : live.auditRotationArrays)
        result["rotation_array_calls"].push(matjson::makeObject({
            {"group", group}, {"calls", rotation.calls}, {"recipient_slots", rotation.recipientSlots},
            {"first_level_time", rotation.first}, {"last_level_time", rotation.last}}));
    std::map<RevocationCause, std::uint64_t> byCause;
    std::map<int, std::map<RevocationCause, std::uint64_t>> byGroup;
    std::uint64_t estimated = 0, expected = 0, changedRoutes = 0, changedRouteVisits = 0, mismatchedRoutes = 0;
    result["hot_routes"] = matjson::Value::array();
    for (const auto& route : live.routes) {
        if (route.nativeGroup == 575 || route.nativeGroup == 848)
            result["hot_routes"].push(matjson::makeObject({
                {"group", route.nativeGroup}, {"route_token", route.token},
                {"slots", route.source->count()}, {"initial_owned", route.auditInitialOwned},
                {"move_calls", route.auditMoves}, {"native_visits", route.residentNativeVisits},
                {"membership_changed", route.auditMembershipChanged}}));
        if (route.auditMembershipChanged) {
            ++changedRoutes;
            changedRouteVisits += route.residentNativeVisits;
            continue;
        }
        std::uint64_t attributed = 0;
        for (const auto& [cause, mass] : route.auditRevocations) {
            const auto total = mass.members * route.auditMoves;
            if (total < mass.moveCountAtReleaseSum) continue;
            const auto visits = total - mass.moveCountAtReleaseSum;
            if (!visits) continue;
            attributed += visits;
            byCause[cause] += visits;
            byGroup[route.nativeGroup][cause] += visits;
        }
        const auto initialMass = route.auditInitialOwned * route.auditMoves;
        const auto expectedRoute = initialMass >= route.residentVisits ? initialMass - route.residentVisits : 0;
        if (initialMass < route.residentVisits || attributed != expectedRoute ||
            attributed > route.residentNativeVisits) ++mismatchedRoutes;
        estimated += attributed;
        expected += expectedRoute;
    }
    result["post_revocation_visits_stable_routes"] = estimated;
    result["expected_post_revocation_visits_stable_routes"] = expected;
    result["attribution_mismatched_routes"] = mismatchedRoutes;
    result["excluded_membership_changed_routes"] = changedRoutes;
    result["excluded_membership_changed_native_visits"] = changedRouteVisits;
    result["post_revocation_by_cause"] = matjson::Value::array();
    auto causeRow = [](const RevocationCause& cause, std::uint64_t visits) {
        return matjson::makeObject({{"reason", std::get<0>(cause)}, {"phase", std::get<1>(cause)},
            {"rotation_target_group", std::get<2>(cause)}, {"native_visits", visits}});
    };
    for (const auto& [cause, visits] : byCause)
        result["post_revocation_by_cause"].push(causeRow(cause, visits));
    result["post_revocation_by_move_group"] = matjson::Value::array();
    for (const auto& [group, causes] : byGroup)
        for (const auto& [cause, visits] : causes) {
            auto row = causeRow(cause, visits);
            row["move_group"] = group;
            result["post_revocation_by_move_group"].push(std::move(row));
        }
    if (live.benchmarkFinished) {
        const auto& a = live.auditWindowStart;
        const auto& b = live.auditWindowEnd;
        const auto frameMs = b.frameMs - a.frameMs;
        const auto exclusive = b.nativeMoveExclusiveMs - a.nativeMoveExclusiveMs;
        result["window_80_90"] = matjson::makeObject({
            {"actual_level_start", live.auditWindowStartTime}, {"actual_level_end", live.auditWindowEndTime},
            {"native_move_inclusive_ms", b.nativeMoveMs - a.nativeMoveMs},
            {"native_move_exclusive_ms", exclusive},
            {"native_atlas_draw_ms", b.nativeDrawMs - a.nativeDrawMs},
            {"route_refresh_ms", b.refreshMs - a.refreshMs},
            {"frame_elapsed_sum_ms", frameMs},
            {"native_move_exclusive_frame_share", frameMs > 0 ? exclusive / frameMs : 0.0},
            {"native_move_visits", live.benchmarkEnd.nativeMoveVisits - live.benchmarkStart.nativeMoveVisits}});
    }
    return result;
}
matjson::Value spikeMetrics(const std::array<spike::Metric, spike::phaseCount>& metrics) {
    auto result = matjson::Value::object();
    for (std::size_t i = 0; i < metrics.size(); ++i) {
        const auto& m = metrics[i];
        if (m.calls) result[spike::phaseNames[i]] = matjson::makeObject({{"calls", m.calls},
            {"work_slots", m.work}, {"inclusive_ms", m.inclusive}, {"exclusive_ms", m.exclusive}});
    }
    return result;
}
matjson::Value spikeGroup(const spike::Group& g) {
    return matjson::makeObject({{"group", g.id}, {"total_slots", g.total()},
        {"native_move", g.nativeMove}, {"resident_move", g.residentMove},
        {"native_rotation", g.nativeRotate}, {"resident_rotation", g.residentRotate}});
}
matjson::Value spikeReport(const Live& live) {
    const auto& a = spike::audit;
    auto worst = matjson::Value::array();
    for (std::size_t i = 0; i < a.worstCount; ++i) {
        const auto& f = a.worst[i]; const auto& c = f.counters;
        auto steps = matjson::Value::array();
        for (std::size_t j = 0; j < std::min<std::uint64_t>(f.steps, spike::maxSteps); ++j)
            steps.push(f.stepTimes[j]);
        auto events = matjson::Value::object();
        for (std::size_t j = 0; j < f.events.size(); ++j) events[spike::eventNames[j]] = f.events[j];
        auto groups = matjson::Value::array();
        for (const auto& g : f.groups) if (g.total()) groups.push(spikeGroup(g));
        worst.push(matjson::makeObject({{"frame", f.index}, {"level_start", f.levelStart}, {"level_end", f.levelEnd},
            {"wall_ms", f.wallMs}, {"previous_frame_start_interval_ms", f.previousFrameIntervalMs},
            {"scheduler_input_dt_sum_s", f.schedulerDt}, {"scheduler_calls", f.schedulerCalls},
            {"game_update_input_dt_sum_s", f.gameDt}, {"game_update_calls", f.gameCalls},
            {"observed_move_steps", f.steps}, {"observed_move_step_dt_sum_s", f.stepDt},
            {"move_step0_ms", f.step0Ms}, {"remaining_move_steps_ms", f.stepSumMs - f.step0Ms},
            {"max_move_step_ms", f.maxStepMs}, {"max_move_step_index", f.maxStepIndex},
            {"move_step_durations_ms", std::move(steps)}, {"move_step_durations_truncated", f.steps > spike::maxSteps},
            {"top_level_scope_ms", f.topLevelMs}, {"outside_instrumented_scopes_ms", std::max(0.0, f.wallMs - f.topLevelMs)},
            {"phase_timings", spikeMetrics(f.phases)}, {"events", std::move(events)},
            {"top_groups_by_recipient_slots", std::move(groups)}, {"unattributed_slots", spikeGroup(f.unknown)},
            {"counter_deltas", matjson::makeObject({{"observed_physics_checkpoint_ticks", c.ticks},
                {"native_move_visits", c.nativeVisits}, {"eliminated_move_visits", c.eliminatedVisits},
                {"sprite_visits", c.spriteVisits}, {"native_section_callbacks", c.sectionCallbacks},
                {"resident_section_transitions", c.sectionTransitions}, {"revocations", c.revocations},
                {"atlas_dirty_bytes", c.dirtyBytes}, {"atlas_upload_events", c.atlasUploads},
                {"resident_submissions", c.submissions}, {"native_rotation_recipients", c.rotationNative},
                {"resident_rotation_recipients", c.rotationResident}, {"resident_sync_ms", c.syncMs},
                {"resident_handoff_ms", c.handoffMs}, {"resident_dispatch_ms", c.dispatchMs},
                {"resident_section_commit_ms", c.sectionCommitMs}})}}));
    }
    auto population = matjson::Value::object();
    for (const int target : {575, 848, 815}) {
        std::set<ObjectId> members;
        unsigned arrays = 0; std::uint64_t sourceSlots = 0, uncompiledSlots = 0;
        auto* canonical = target < live.layer->m_groups.size() ? live.layer->m_groups[target] : nullptr;
        const auto canonicalSlots = canonical ? canonical->count() : 0;
        if (canonical) for (auto* object : CCArrayExt<GameObject, false>(canonical)) {
            const auto found = live.ids.find(object);
            if (found != live.ids.end()) members.insert(found->second);
        }
        for (const auto& route : live.routes) if (route.nativeGroup == target) {
            ++arrays; sourceSlots += route.source->count();
            for (auto* object : CCArrayExt<GameObject, false>(route.source)) {
                const auto found = live.ids.find(object);
                if (found != live.ids.end()) members.insert(found->second);
                else ++uncompiledSlots;
            }
        }
        auto reasons = matjson::Value::object();
        std::map<std::string, std::uint64_t> counts;
        std::uint64_t owned = 0;
        for (auto id : members) if (id < live.entries.size()) {
            if (live.entries[id].owned) ++owned;
            ++counts[id < live.initialMoveRejections.size() ? live.initialMoveRejections[id] : "unavailable"];
        }
        for (const auto& [reason, count] : counts) reasons[reason] = count;
        population[std::to_string(target)] = matjson::makeObject({{"route_arrays", arrays},
            {"canonical_group_slots", canonicalSlots}, {"current_source_slots", sourceSlots},
            {"unique_compiled_group_and_route_members", members.size()},
            {"uncompiled_source_slots", uncompiledSlots}, {"compiled_source_members_owned_at_end", owned},
            {"initial_admission_by_reason", std::move(reasons)}});
    }
    return matjson::makeObject({{"schema", 1}, {"frames", a.frames}, {"observed_move_steps", a.totalSteps},
        {"wall_ms", a.totalWallMs}, {"top_level_scope_ms", a.totalTopLevelMs},
        {"outside_instrumented_scopes_ms", std::max(0.0, a.totalWallMs - a.totalTopLevelMs)},
        {"phase_totals", spikeMetrics(a.totals)}, {"worst_frames", std::move(worst)},
        {"target_group_population", std::move(population)},
        {"step_scope", "processMoveActionsStep only; not full physics step"},
        {"dt_scope", "observed hook input, not guaranteed unclamped"},
        {"attribution_scope", "scheduled recipient slots, not measured per-group time; unhooked paths may be missing"},
        {"trigger_unique_ids_available", false}});
}
matjson::Value nativeVisibilityProfileReport(const Live& live) {
    using ProfileFrame = Live::NativeVisibilityFrame;
    const auto& frames = live.nativeVisibilityFrames;
    std::array<std::uint64_t, spike::eventCount> eventTotals{};
    std::uint64_t steps = 0, spriteVisits = 0, suppressedVisits = 0;
    std::uint64_t nativeSectionCallbacks = 0, residentSectionTransitions = 0;
    double preVisibilityMs = 0, visibilityMs = 0;
    auto phaseValue = [](const ProfileFrame& frame, spike::Phase phase) {
        return frame.phaseExclusiveMs[static_cast<std::size_t>(phase)];
    };
    auto eventValue = [](const ProfileFrame& frame, spike::Event event) {
        return frame.events[static_cast<std::size_t>(event)];
    };
    auto rows = matjson::Value::array();
    for (const auto& frame : frames) {
        steps += frame.simulationSteps;
        spriteVisits += frame.spriteTransformVisits;
        suppressedVisits += frame.geometrySuppressedVisits;
        nativeSectionCallbacks += frame.nativeSectionCallbacks;
        residentSectionTransitions += frame.residentSectionTransitions;
        preVisibilityMs += phaseValue(frame, spike::Phase::preVisibility);
        visibilityMs += phaseValue(frame, spike::Phase::visibility);
        auto phaseTimes = matjson::Value::object();
        for (const auto phase : {spike::Phase::preVisibility, spike::Phase::stockPreVisibility,
                spike::Phase::preVisibilityHooks, spike::Phase::visibility, spike::Phase::stockVisibility,
                spike::Phase::visibilityHooks, spike::Phase::visibilityMirrorScan, spike::Phase::residentSync,
                spike::Phase::nativeSection, spike::Phase::batchVisit, spike::Phase::batchSort,
                spike::Phase::batchDraw, spike::Phase::atlasDraw, spike::Phase::residentFlush,
                spike::Phase::residentDispatch, spike::Phase::residentSectionCommit}) {
            phaseTimes[spike::phaseNames[static_cast<std::size_t>(phase)]] =
                phaseValue(frame, phase);
        }
        auto events = matjson::Value::object();
        for (std::size_t i = 0; i < frame.events.size(); ++i) {
            events[spike::eventNames[i]] = frame.events[i];
            eventTotals[i] += frame.events[i];
        }
        rows.push(matjson::makeObject({{"frame", frame.index}, {"simulation_steps", frame.simulationSteps},
            {"sprite_transform_visits", frame.spriteTransformVisits},
            {"geometry_suppressed_visits", frame.geometrySuppressedVisits},
            {"section_commit_hook_callbacks", frame.nativeSectionCallbacks},
            {"resident_section_transitions", frame.residentSectionTransitions},
            {"phase_exclusive_ms", std::move(phaseTimes)}, {"event_deltas", std::move(events)}}));
    }
    auto totals = matjson::Value::object();
    for (std::size_t i = 0; i < eventTotals.size(); ++i) totals[spike::eventNames[i]] = eventTotals[i];

    auto correlation = [&](auto xValue, auto yValue) {
        double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
        for (const auto& frame : frames) {
            const double x = xValue(frame), y = yValue(frame);
            sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y;
        }
        const double n = static_cast<double>(frames.size());
        const double covariance = n * sxy - sx * sy;
        const double vx = n * sxx - sx * sx, vy = n * syy - sy * sy;
        const bool available = n > 1 && vx > 0 && vy > 0;
        return matjson::makeObject({{"sample_frames", frames.size()}, {"available", available},
            {"pearson_r", available ? covariance / std::sqrt(vx * vy) : 0.0}});
    };
    auto timingCorrelations = [&](spike::Phase phase) {
        auto result = matjson::Value::object();
        result["simulation_steps"] = correlation(
            [&](const auto& frame) { return static_cast<double>(frame.simulationSteps); },
            [&](const auto& frame) { return phaseValue(frame, phase); });
        result["native_section_registrations"] = correlation(
            [&](const auto& frame) { return static_cast<double>(eventValue(frame, spike::Event::nativeSectionRegistrations)); },
            [&](const auto& frame) { return phaseValue(frame, phase); });
        result["native_section_transitions"] = correlation(
            [&](const auto& frame) { return static_cast<double>(eventValue(frame, spike::Event::nativeSectionTransitions)); },
            [&](const auto& frame) { return phaseValue(frame, phase); });
        result["batch_attachments"] = correlation(
            [&](const auto& frame) { return static_cast<double>(eventValue(frame, spike::Event::spriteBatchAttach)); },
            [&](const auto& frame) { return phaseValue(frame, phase); });
        result["batch_detachments"] = correlation(
            [&](const auto& frame) { return static_cast<double>(eventValue(frame, spike::Event::spriteBatchDetach)); },
            [&](const auto& frame) { return phaseValue(frame, phase); });
        result["mirror_materializations"] = correlation(
            [&](const auto& frame) { return static_cast<double>(eventValue(frame, spike::Event::mirrorMaterializations)); },
            [&](const auto& frame) { return phaseValue(frame, phase); });
        return result;
    };
    const std::array visibilityPhases{
        spike::Phase::preVisibility, spike::Phase::stockPreVisibility, spike::Phase::preVisibilityHooks,
        spike::Phase::visibility, spike::Phase::stockVisibility, spike::Phase::visibilityHooks,
        spike::Phase::visibilityMirrorScan, spike::Phase::residentSync, spike::Phase::nativeSection,
        spike::Phase::batchVisit, spike::Phase::batchSort, spike::Phase::batchDraw, spike::Phase::atlasDraw,
        spike::Phase::residentFlush, spike::Phase::residentDispatch, spike::Phase::residentSectionCommit
    };
    struct Hotspot { std::size_t phase; double exclusiveMs; };
    std::vector<Hotspot> hotspots;
    for (const auto phase : visibilityPhases) {
        const auto index = static_cast<std::size_t>(phase);
        if (spike::audit.totals[index].calls)
            hotspots.push_back({index, spike::audit.totals[index].exclusive});
    }
    std::sort(hotspots.begin(), hotspots.end(), [](const auto& a, const auto& b) {
        return a.exclusiveMs > b.exclusiveMs;
    });
    auto ranked = matjson::Value::array();
    for (std::size_t i = 0; i < hotspots.size(); ++i) {
        const auto& hotspot = hotspots[i];
        const auto& metric = spike::audit.totals[hotspot.phase];
        ranked.push(matjson::makeObject({{"rank", i + 1}, {"phase", spike::phaseNames[hotspot.phase]},
            {"calls", metric.calls}, {"exclusive_ms", hotspot.exclusiveMs},
            {"inclusive_ms", metric.inclusive}}));
    }
    auto correlations = matjson::makeObject({
        {"pre_visibility_ms", timingCorrelations(spike::Phase::preVisibility)},
        {"visibility_ms", timingCorrelations(spike::Phase::visibility)}});
    return matjson::makeObject({{"enabled", true}, {"frames", frames.size()},
        {"simulation_steps", steps}, {"pre_visibility_exclusive_ms", preVisibilityMs},
        {"visibility_exclusive_ms", visibilityMs}, {"sprite_transform_visits", spriteVisits},
        {"geometry_suppressed_visits", suppressedVisits},
        {"section_commit_hook_callbacks", nativeSectionCallbacks},
        {"resident_section_transitions", residentSectionTransitions}, {"event_totals", std::move(totals)},
        {"visible_counts_available", false}, {"owned_visible_counts_available", false},
        {"visible_count_limit", "The existing hooks do not observe every visibility mutation; counting would require a full object traversal."},
        {"phase_timing_model", "All reported phase values are exclusive; nested phase time is excluded from its parent."},
        {"simulation_step_scope", "Observed processMoveActionsStep calls; this is not a complete physics-step timer."},
        {"native_section_transition_scope", "Changes to outer or middle section indices at the outermost hooked updateObjectSection return; nested calls are coalesced."},
        {"phase_hotspots_ranked", std::move(ranked)}, {"correlations", std::move(correlations)},
        {"frames_detail", std::move(rows)},
        {"correlation_caveat", "Pearson correlations describe co-variation across frames and do not establish causation."},
        {"attribution_limit", "Visibility and rendering callbacks are nested with Vulkanizer hooks. Per-object CPU timers were not added; ETW stack samples are required for function-level attribution."}});
}
matjson::Value visibilityOwnershipShadowReport(const Live& live) {
    const auto& shadow = live.visibilityOwnership;
    using Frame = Live::VisibilityOwnershipFrame;
    auto mismatchJson = [](const std::optional<Live::VisibilityOwnershipMismatch>& value) {
        if (!value) return matjson::Value(nullptr);
        const auto& m = *value;
        auto nativeSection = matjson::Value::array();
        nativeSection.push(m.nativeSection.first); nativeSection.push(m.nativeSection.second);
        auto indexedSection = matjson::Value::array();
        indexedSection.push(m.indexedSection.first); indexedSection.push(m.indexedSection.second);
        auto position = matjson::Value::array();
        position.push(m.x); position.push(m.y);
        return matjson::makeObject({{"object_id", m.object}, {"frame", m.frame}, {"simulation_step", m.step},
            {"native_section", std::move(nativeSection)}, {"indexed_section", std::move(indexedSection)},
            {"native_space", m.nativeSpace}, {"indexed_space", m.indexedSpace}, {"native_visible", m.nativeVisible},
            {"candidate", m.candidate}, {"activated", m.activated}, {"group_disabled", m.groupDisabled},
            {"group_disabled_temp", m.groupDisabledTemp}, {"hide", m.hide}, {"batch_attached", m.batchAttached},
            {"position", std::move(position)}});
    };
    auto histogram = matjson::Value::object();
    std::map<std::string, std::uint64_t> orderedHistogram(shadow.detachReattachHistogram.begin(),
        shadow.detachReattachHistogram.end());
    for (const auto& [bucket, count] : orderedHistogram) histogram[bucket] = count;

    auto phase = [](const Frame& frame, spike::Phase p) {
        return frame.phaseExclusiveMs[static_cast<std::size_t>(p)];
    };
    auto event = [](const Frame& frame, spike::Event e) {
        return frame.events[static_cast<std::size_t>(e)];
    };
    auto correlation = [&](auto xValue, auto yValue) {
        double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
        for (const auto& frame : shadow.frames) {
            const double x = xValue(frame), y = yValue(frame);
            sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y;
        }
        const double n = static_cast<double>(shadow.frames.size());
        const double covariance = n * sxy - sx * sy;
        const double vx = n * sxx - sx * sx, vy = n * syy - sy * sy;
        const bool available = n > 1 && vx > 0 && vy > 0;
        return matjson::makeObject({{"sample_frames", shadow.frames.size()}, {"available", available},
            {"pearson_r", available ? covariance / std::sqrt(vx * vy) : 0.0}});
    };
    auto phaseCorrelations = [&](spike::Phase p) {
        return matjson::makeObject({
            {"conservative_candidates", correlation([](const auto& f) { return double(f.candidates); },
                [&](const auto& f) { return phase(f, p); })},
            {"native_visible_owned", correlation([](const auto& f) { return double(f.nativeVisibleOwned); },
                [&](const auto& f) { return phase(f, p); })},
            {"owned_batch_attachments", correlation([](const auto& f) { return double(f.ownedBatchAttachments); },
                [&](const auto& f) { return phase(f, p); })},
            {"owned_batch_detachments", correlation([](const auto& f) { return double(f.ownedBatchDetachments); },
                [&](const auto& f) { return phase(f, p); })},
            {"owned_section_transitions", correlation([](const auto& f) { return double(f.ownedSectionTransitions); },
                [&](const auto& f) { return phase(f, p); })},
            {"mirror_materializations", correlation([&](const auto& f) {
                return double(event(f, spike::Event::mirrorMaterializations)); },
                [&](const auto& f) { return phase(f, p); })}});
    };

    double preVisibilityMs = 0, stockPreVisibilityMs = 0, visibilityMs = 0, stockVisibilityMs = 0;
    std::uint64_t simulationSteps = 0, nativeSectionTransitions = 0, mirrorMaterializations = 0;
    auto rows = matjson::Value::array();
    for (const auto& frame : shadow.frames) {
        simulationSteps += frame.simulationSteps;
        preVisibilityMs += phase(frame, spike::Phase::preVisibility);
        stockPreVisibilityMs += phase(frame, spike::Phase::stockPreVisibility);
        visibilityMs += phase(frame, spike::Phase::visibility);
        stockVisibilityMs += phase(frame, spike::Phase::stockVisibility);
        nativeSectionTransitions += event(frame, spike::Event::nativeSectionTransitions);
        mirrorMaterializations += event(frame, spike::Event::mirrorMaterializations);
        auto phases = matjson::makeObject({
            {"pre_visibility", phase(frame, spike::Phase::preVisibility)},
            {"stock_pre_visibility", phase(frame, spike::Phase::stockPreVisibility)},
            {"pre_visibility_hooks", phase(frame, spike::Phase::preVisibilityHooks)},
            {"visibility", phase(frame, spike::Phase::visibility)},
            {"stock_visibility", phase(frame, spike::Phase::stockVisibility)},
            {"visibility_hooks", phase(frame, spike::Phase::visibilityHooks)},
            {"visibility_mirror_scan", phase(frame, spike::Phase::visibilityMirrorScan)},
            {"resident_sync", phase(frame, spike::Phase::residentSync)},
            {"native_section", phase(frame, spike::Phase::nativeSection)}});
        rows.push(matjson::makeObject({{"frame", frame.index}, {"simulation_steps", frame.simulationSteps},
            {"candidate_coverage_available", frame.candidateCoverageAvailable},
            {"conservative_candidates", frame.candidates}, {"native_visible_owned", frame.nativeVisibleOwned},
            {"visible_owned_covered_by_candidates", frame.coveredVisible},
            {"candidate_overcoverage_observations", frame.candidateFalsePositives},
            {"visible_owned_outside_candidates", frame.candidateFalseNegatives},
            {"candidate_entries", frame.candidateEntries}, {"candidate_exits", frame.candidateExits},
            {"camera_driven_candidate_entries", frame.cameraEntries}, {"camera_driven_candidate_exits", frame.cameraExits},
            {"object_driven_candidate_entries", frame.objectEntries}, {"object_driven_candidate_exits", frame.objectExits},
            {"native_visibility_entries", frame.nativeEntries}, {"native_visibility_exits", frame.nativeExits},
            {"candidate_vs_native_entry_mismatches", frame.candidateEntryMismatches},
            {"candidate_vs_native_exit_mismatches", frame.candidateExitMismatches},
            {"owned_batch_attachments", frame.ownedBatchAttachments},
            {"owned_batch_detachments", frame.ownedBatchDetachments},
            {"owned_section_transitions", frame.ownedSectionTransitions},
            {"native_section_transitions_all_objects", event(frame, spike::Event::nativeSectionTransitions)},
            {"mirror_materializations", event(frame, spike::Event::mirrorMaterializations)},
            {"phase_exclusive_ms", std::move(phases)}}));
    }

    auto unresolvedPredicates = matjson::Value::array();
    unresolvedPredicates.push("The exact stock preUpdateVisibility and PlayLayer updateVisibility culling predicates are not exposed by the targeted code or bindings.");
    unresolvedPredicates.push("Activation, group-disabled and hide fields are recorded as context but are not treated as a complete visibility rule.");
    unresolvedPredicates.push("Batch/Cocos geometry clipping and game-specific visibility conditions are not claimed as exact.");
    return matjson::makeObject({
        {"enabled", true}, {"mode", "shadow_only"}, {"frames", shadow.frames.size()},
        {"simulation_steps", simulationSteps}, {"pre_visibility_exclusive_ms", preVisibilityMs},
        {"stock_pre_visibility_exclusive_ms", stockPreVisibilityMs}, {"visibility_exclusive_ms", visibilityMs},
        {"stock_visibility_exclusive_ms", stockVisibilityMs},
        {"section_index", matjson::makeObject({{"owned_at_initialization", shadow.ownedAtInitialization},
            {"section_indexed_at_initialization", shadow.sectionIndexedAtInitialization},
            {"visibility_candidate_indexed_at_initialization", shadow.eligibleAtInitialization},
            {"unsupported_visibility_candidates_at_initialization", shadow.unsupportedAtInitialization},
            {"currently_indexed_owned_objects", shadow.sectionIndex.size()},
            {"currently_indexed_visibility_candidates", shadow.index.size()},
            {"occupied_space_section_bins", shadow.sectionIndex.occupiedSections()},
            {"maximum_objects_in_one_space_section", shadow.sectionIndex.maxSectionPopulation()},
            {"owned_section_registration_callbacks", shadow.ownedSectionRegistrations},
            {"owned_section_transitions", shadow.ownedSectionTransitions},
            {"membership_update_policy", "initialized once after scene compilation; updated from indexSections, filter section events, and native registration boundaries; swap removal by per-object slot"}})},
        {"visibility", matjson::makeObject({{"exact_predictions_available", false},
            {"objects_covered_by_exact_predictions", 0}, {"exact_predicted_visible_count_available", false},
            {"conservative_candidate_selection_available", shadow.candidateCoverageAvailable},
            {"candidate_passes", shadow.candidatePasses}, {"candidate_observations", shadow.candidateObservations},
            {"conservative_candidate_count", shadow.previousCandidates.size()},
            {"native_visible_owned_observations", shadow.nativeVisibleObservations},
            {"currently_native_visible_owned", shadow.nativeVisibleOwned.size()},
            {"owned_visible_count_available", true},
            {"visible_count_available_for_all_native_objects", false},
            {"candidate_visible_coverage_observations", shadow.coveredVisibleObservations},
            {"candidate_false_positive_observations", shadow.candidateFalsePositiveObservations},
            {"candidate_false_negative_observations", shadow.candidateFalseNegativeObservations},
            {"candidate_overcoverage_observations", shadow.candidateFalsePositiveObservations},
            {"visible_owned_outside_candidate_observations", shadow.candidateFalseNegativeObservations},
            {"candidate_overcoverage_first_mismatch", mismatchJson(shadow.firstCandidateFalsePositive)},
            {"candidate_undercoverage_first_mismatch", mismatchJson(shadow.firstCandidateFalseNegative)},
            {"unresolved_predicates", std::move(unresolvedPredicates)}})},
        {"transitions", matjson::makeObject({{"candidate_entries", shadow.candidateEntries},
            {"candidate_exits", shadow.candidateExits}, {"camera_driven_candidate_entries", shadow.cameraEntries},
            {"camera_driven_candidate_exits", shadow.cameraExits}, {"object_driven_candidate_entries", shadow.objectEntries},
            {"object_driven_candidate_exits", shadow.objectExits}, {"native_setVisible_entries", shadow.nativeEntries},
            {"native_setVisible_exits", shadow.nativeExits},
            {"candidate_vs_native_entry_mismatch_observations", shadow.candidateEntryMismatches},
            {"candidate_vs_native_exit_mismatch_observations", shadow.candidateExitMismatches},
            {"exact_enter_exit_mismatch_count", 0}, {"exact_enter_exit_mismatch_available", false},
            {"first_entry_mismatch", mismatchJson(shadow.firstCandidateEntryMismatch)},
            {"first_exit_mismatch", mismatchJson(shadow.firstCandidateExitMismatch)}})},
        {"section_membership_mismatch_unique_objects", shadow.sectionMismatchObjects.size()},
        {"section_membership_mismatch_first", mismatchJson(shadow.firstSectionMismatch)},
        {"owned_batch_attachments", shadow.ownedBatchAttachments},
        {"owned_batch_detachments", shadow.ownedBatchDetachments},
        {"detach_to_reattach_interval_frames_histogram", std::move(histogram)},
        {"negative_control", matjson::makeObject({{"enabled", true}, {"injected_mismatches_checked", shadow.negativeControlChecks},
            {"incorrect_prediction_detected", shadow.negativeControlCaught != 0},
            {"detected_count", shadow.negativeControlCaught}, {"object_id", shadow.negativeControlObject},
            {"first_mismatch", mismatchJson(shadow.firstNegativeControl)},
            {"method", "A candidate observed native-visible after the stock pass is assigned a local false prediction for comparison only; no native state or candidate index is changed."}})},
        {"frame_time_correlations", matjson::makeObject({
            {"pre_visibility_ms", phaseCorrelations(spike::Phase::preVisibility)},
            {"visibility_ms", phaseCorrelations(spike::Phase::visibility)}})},
        {"native_section_transitions_all_objects", nativeSectionTransitions},
        {"mirror_materializations", mirrorMaterializations},
        {"frames_detail", std::move(rows)},
        {"correlation_caveat", "Per-frame Pearson correlations describe co-variation and do not establish causation."},
        {"candidate_transition_cause_limit", "Object-driven means the object's section coordinates changed during the frame; remaining candidate transitions are camera or visibility-space changes and are not guaranteed to be camera-only."},
        {"visibility_count_limit", "Owned-visible counts are initialized once from the compiled entries and maintained at the hooked GameObject::setVisible boundary; all-native-object visible counts are unavailable. Direct writes to native visibility storage would not be observed."},
        {"candidate_comparison_limit", "Overcoverage and undercoverage compare conservative candidates with native-visible state; they are coverage diagnostics, not exact prediction errors. Exact predicate attribution remains unresolved."},
        {"profiling_limit", "Phase times use existing exclusive SpikeAudit scopes. They are wall-clock CPU scopes and do not identify native versus hook self-time."}});
}
matjson::Value revocationCountsJson(const RevocationCounts& counts) {
    return matjson::makeObject({{"objects", counts.objects}, {"pending_gpu_commands", counts.pending},
        {"stale_cpu_mirror", counts.stale}, {"both", counts.pendingAndStale}});
}
matjson::Value revocationReport(const Live& live) {
    auto sites = matjson::Value::array();
    std::uint64_t unresolved = 0, heuristic = 0, unwound = 0;
    for (const auto& [key, site] : live.revocationSites) {
        const auto& [reason, phase, spikePhase, provenance, module, caller, parent] = key;
        if (provenance == "unresolved") unresolved += site.counts.objects;
        else if (provenance == "stack_candidate") heuristic += site.counts.objects;
        else unwound += site.counts.objects;
        auto groups = matjson::Value::object();
        for (const auto& [group, counts] : site.groups) groups[std::to_string(group)] = revocationCountsJson(counts);
        sites.push(matjson::makeObject({{"reason", reason}, {"hook_phase", phase}, {"spike_phase", spikePhase},
            {"caller_provenance", provenance}, {"module", module}, {"return_pc_rva", caller}, {"parent_return_pc_rva", parent},
            {"counts", revocationCountsJson(site.counts)}, {"by_group", std::move(groups)},
            {"max_global_pending_commands", site.maxBatchCommands}, {"max_global_pending_step_span", site.maxPendingStepSpan}}));
    }
    auto parents = matjson::Value::object();
    for (const auto& [group, masks] : live.initialParentMasks) {
        auto values = matjson::Value::object();
        for (const auto& [mask, count] : masks) values[std::to_string(mask)] = count;
        parents[std::to_string(group)] = std::move(values);
    }
    return matjson::makeObject({{"schema", 1}, {"totals", revocationCountsJson(live.revocationTotals)},
        {"released_counter", live.stats.released}, {"count_reconciled", live.revocationTotals.objects == live.stats.released},
        {"unwound_objects", unwound}, {"heuristic_objects", heuristic}, {"unresolved_objects", unresolved},
        {"audit_wall_ms", live.revocationAuditMs}, {"sites", std::move(sites)}, {"initial_parent_masks", std::move(parents)},
        {"parent_mask_bits", "1=hasGroupParent,2=hasAreaParent,4=hasGroupParentsString,8=updateParents,16=linkedGroup"},
        {"scope", "actual runtime handoffs; flags sampled before ownership_handoff flush; group counts overlap"},
        {"caller_scope", "return PC RVAs, not function entry RVAs; stack_candidate is heuristic and requires disassembly confirmation"},
        {"pending_scope", "chain has an unsubmitted queued journal command; stale mirrors may remain after GPU submission"}});
}
matjson::Value setterReport(const Live& live) {
    using namespace setter_audit;
    constexpr const char* bucketNames[]{"bitwise_identical_input_and_logical", "different_input_unchanged_logical", "logical_position_changed"};
    constexpr const char* changeNames[]{"logical", "sprite", "history", "sections", "dirty", "visibility", "offsets", "rotation", "membership"};
    struct Summary {
        std::uint64_t rows = 0, conservativeCandidates = 0, retained = 0, entryInputMatches = 0, inputChanged = 0;
        std::array<std::uint64_t, setter_audit::count> buckets{};
        std::array<std::uint64_t, 9> setterChanges{}, handoffChanges{};
        std::array<std::uint64_t, 5> cursorFlags{};
        void add(const SetterEvent& e) {
            ++rows; retained += e.retained;
            entryInputMatches += matchesInput(e.entry, e.inputX, e.inputY);
            inputChanged += e.inputX != e.nativeX || e.inputY != e.nativeY;
            const auto bucket = classify(e.before, e.after, e.nativeX, e.nativeY);
            ++buckets[bucket];
            const auto setter = changes(e.before, e.after), handoff = changes(e.entry, e.before);
            // Only a strict candidate count, not a guarantee of hidden side-effect equivalence.
            conservativeCandidates += bucket == identical && !e.cursorFlags && !handoff && !setter &&
                e.inputX == e.nativeX && e.inputY == e.nativeY;
            for (unsigned i = 0; i < setterChanges.size(); ++i) {
                setterChanges[i] += (setter & (1u << i)) != 0;
                handoffChanges[i] += (handoff & (1u << i)) != 0;
            }
            for (unsigned i = 0; i < cursorFlags.size(); ++i) cursorFlags[i] += (e.cursorFlags & (1u << i)) != 0;
        }
    };
    auto summaryJson = [&](const Summary& s) {
        auto buckets = matjson::Value::object(), setter = matjson::Value::object(), handoff = matjson::Value::object();
        for (unsigned i = 0; i < s.buckets.size(); ++i) buckets[bucketNames[i]] = s.buckets[i];
        for (unsigned i = 0; i < s.setterChanges.size(); ++i) {
            setter[changeNames[i]] = s.setterChanges[i]; handoff[changeNames[i]] = s.handoffChanges[i];
        }
        return matjson::makeObject({{"calls", s.rows}, {"retained", s.retained}, {"strict_candidates", s.conservativeCandidates},
            {"entry_input_bitwise_matches", s.entryInputMatches}, {"input_changed_before_setter", s.inputChanged},
            {"pending_gpu", s.cursorFlags[0]}, {"stale_cpu_cursor", s.cursorFlags[1]}, {"stale_sprite_cursor", s.cursorFlags[2]},
            {"current_cursor_history_mismatch", s.cursorFlags[3]}, {"pending_rebase", s.cursorFlags[4]},
            {"buckets", std::move(buckets)}, {"setter_changes", std::move(setter)}, {"before_setter_changes", std::move(handoff)}});
    };
    Summary all, visibility;
    std::map<int, Summary> groups;
    std::map<RevocationSiteKey, Summary> sites;
    std::uint64_t incomplete = 0;
    for (const auto& e : live.setterEvents) {
        if (!e.completed) { ++incomplete; continue; }
        all.add(e);
        if (e.visibility) visibility.add(e);
        sites[{"native_position_setter", "other", e.phase, e.provenance, e.module, e.caller, e.parent}].add(e);
        for (const auto& [mask, group] : {std::pair{1u, 575}, std::pair{2u, 848}, std::pair{4u, 815}})
            if (e.groupMask & mask) groups[group].add(e);
    }
    auto groupsJson = matjson::Value::object(), sitesJson = matjson::Value::array(), reads = matjson::Value::object();
    for (const auto& [group, summary] : groups) groupsJson[std::to_string(group)] = summaryJson(summary);
    for (const auto& [key, summary] : sites) {
        const auto& [reason, phase, scope, provenance, module, caller, parent] = key;
        sitesJson.push(matjson::makeObject({{"phase", scope}, {"provenance", provenance}, {"module", module},
            {"caller_rva", caller}, {"parent_rva", parent}, {"summary", summaryJson(summary)}}));
    }
    for (const auto& [reason, c] : live.visibilityReads) reads[std::string(reason)] = matjson::makeObject({
        {"calls", c.calls}, {"pending_gpu", c.pending}, {"stale_cpu_cursor", c.stale}, {"synchronized", c.synchronized}});
    // Raw bits in a separate TSV: no per-frame file I/O, no JSON number rounding.
    bool traceWritten = false;
    if (const auto& file = envFlags().reportPath) {
        auto path = std::filesystem::path(*file); path.replace_extension(".setter-events.tsv");
        std::ofstream out(path);
        out << "object_id\tframe\tstep\tphase\tvisibility_scope\tprovenance\tmodule\tcaller_rva\tparent_rva\tgroup_mask\tcursor_flags\tinput_x_f32\tinput_y_f32\tnative_input_x_f32\tnative_input_y_f32\tcompleted\tretained\tbefore_change_mask\tsetter_change_mask\tbucket";
        constexpr const char* fields[]{"logical_x_f64", "logical_y_f64", "offset_x_f32", "offset_y_f32", "sprite_x_f32", "sprite_y_f32", "prev_x_f32", "prev_y_f32", "rot_x_f32", "rot_y_f32", "rot_offset_x_f32", "rot_offset_y_f32", "outer", "middle", "stamp", "dirty", "visibility", "group_count", "linked_group", "group_hash"};
        for (const char* prefix : {"entry", "before", "after"}) for (const char* field : fields) out << '\t' << prefix << '_' << field;
        out << "\tshadow_accepted\tshadow_repeat\n";
        auto snapshot = [&](const Snapshot& s) {
            for (auto x : s.logical) out << '\t' << x;
            for (auto x : s.offsets) out << '\t' << x;
            for (auto x : s.sprite) out << '\t' << x;
            for (auto x : s.previous) out << '\t' << x;
            for (auto x : s.rotation) out << '\t' << x;
            out << '\t' << s.outer << '\t' << s.middle << '\t' << s.stamp << '\t' << s.dirty << '\t' << s.visibility
                << '\t' << s.groupCount << '\t' << s.linkedGroup << '\t' << s.groupHash;
        };
        for (const auto& e : live.setterEvents) {
            out << e.id << '\t' << e.frame << '\t' << e.step << '\t' << e.phase << '\t' << e.visibility << '\t' << e.provenance
                << '\t' << e.module << '\t' << e.caller << '\t' << e.parent << '\t' << e.groupMask << '\t' << e.cursorFlags
                << '\t' << e.inputX << '\t' << e.inputY << '\t' << e.nativeX << '\t' << e.nativeY << '\t' << e.completed << '\t' << e.retained
                << '\t' << changes(e.entry, e.before) << '\t' << changes(e.before, e.after) << '\t'
                << (e.completed ? bucketNames[classify(e.before, e.after, e.nativeX, e.nativeY)] : "incomplete");
            snapshot(e.entry); snapshot(e.before); snapshot(e.after);
            out << '\t' << e.shadowAccepted << '\t' << e.shadowRepeat << '\n';
        }
        out.close(); traceWritten = bool(out);
    }
    return matjson::makeObject({{"schema", 1}, {"all", summaryJson(all)}, {"visibility", summaryJson(visibility)},
        {"by_group", std::move(groupsJson)}, {"by_caller", std::move(sitesJson)}, {"visibility_read_boundaries", std::move(reads)},
        {"incomplete", incomplete}, {"trace_rows", live.setterEvents.size()}, {"trace_written", traceWritten}, {"audit_wall_ms", live.setterAuditMs},
        {"cursor_scope", "journal tracking, not GPU readback; known hooked visibility consumers only, direct native field reads are unobserved"},
        {"trace_scope", "three snapshots: hook entry, after original handoff before native setter, after native setter; unsigned decimal raw IEEE bits"},
        {"group_mask_bits", "1=g575,2=g848,4=g815"}, {"cursor_flag_bits", "1=pending_gpu,2=stale_cpu,4=stale_sprite,8=history_mismatch,16=pending_rebase"},
        {"change_mask_bits", "1=logical,2=sprite,4=history,8=sections,16=dirty,32=visibility,64=offsets,128=rotation,256=membership"},
        {"policy", "diagnostic only unless a shadow group is explicitly selected; strict candidates retain the original conservative definition"}});
}
matjson::Value setterShadowReport(const Live& live) {
    const auto& flags = envFlags();
    auto reasons = matjson::Value::object();
    for (const auto& [reason, count] : live.setterShadowRejected) reasons[std::string(reason)] = count;
    std::uint64_t owned = 0, retainedOwned = 0;
    for (const auto id : live.setterShadowTargets) {
        owned += live.entries[id].owned;
        retainedOwned += live.entries[id].owned && live.entries[id].shadowRetentions != 0;
    }
    bool written = false;
    if (const auto& file = flags.reportPath) {
        auto path = std::filesystem::path(*file); path.replace_extension(".shadow-samples.tsv");
        std::ofstream out(path);
        out << "object_id\tframe\tstep\tlevel_time_f64\tplayer_x_f64\tplayer_y_f64\tinitial_x_f64\tinitial_y_f64\towned\tretained_once\tlogical_x_f64\tlogical_y_f64\tsprite_x_f32\tsprite_y_f32\tvisibility\touter\tmiddle\trotation_x_f32\trotation_y_f32\tgroup_hash";
        if (flags.setterShadowAligned) out << "\tsource_index\tinitial_group_hash\tobject_kind\tcontrol\tretention_count\tfirst_retention_step\tstep_signature\tstate_signature\tpass_ordinal";
        out << '\n';
        for (const auto& row : live.setterShadowFrames) {
            const auto& p = live.setterShadowInitial.at(row.id);
            const auto& s = row.snapshot;
            out << row.id << '\t' << row.frame << '\t' << row.step << '\t' << row.levelTime << '\t' << row.playerX << '\t' << row.playerY
                << '\t' << p[0] << '\t' << p[1] << '\t' << row.owned << '\t' << row.retainedOnce << '\t' << s.logical[0] << '\t' << s.logical[1]
                << '\t' << s.sprite[0] << '\t' << s.sprite[1] << '\t' << s.visibility << '\t' << s.outer << '\t' << s.middle
                << '\t' << s.rotation[0] << '\t' << s.rotation[1] << '\t' << s.groupHash;
            if (flags.setterShadowAligned) {
                const auto& identity = live.alignedShadowIdentities.at(row.id);
                out << '\t' << identity.sourceIndex << '\t' << identity.groupHash << '\t' << identity.objectKind
                    << '\t' << identity.control << '\t' << row.retentionCount << '\t' << row.firstRetentionStep
                    << '\t' << row.stepSignature << '\t' << row.stateSignature << '\t' << row.passOrdinal;
            }
            out << '\n';
        }
        out.close();
        written = bool(out);
    }
    return matjson::makeObject({
        {"group", flags.setterShadowGroup}, {"native_reference", flags.setterShadowNative},
        {"target_population", live.setterShadowTargets.size()}, {"target_owned_at_completion", owned},
        {"retained_calls", live.shadowRetained}, {"unique_retained_objects", live.shadowUnique},
        {"retained_objects_still_owned", retainedOwned}, {"repeat_calls", live.shadowRepeat},
        {"later_step_calls", live.shadowLaterStep}, {"postcondition_failures", live.shadowPostFailures},
        {"setter_reference_checks", live.shadowReferenceChecks}, {"setter_reference_mismatches", live.shadowReferenceMismatch},
        {"visibility_reference_checks", live.shadowVisibilityChecks}, {"visibility_reference_mismatches", live.shadowVisibilityMismatch},
        {"visibility_pre_sync_stale", live.shadowBeforeVisibilityStale}, {"visibility_pre_syncs", live.shadowVisibilitySyncs},
        {"guard_rejections", std::move(reasons)}, {"disabled_after_failure", live.shadowDisabled},
        {"sample_objects", live.setterShadowSamples.size()}, {"sample_rows", live.setterShadowFrames.size()}, {"sample_written", written},
        {"aligned_mode", flags.setterShadowAligned}, {"aligned_clock_frames", live.alignedClockFrames},
        {"aligned_final_step", live.ticks}, {"aligned_final_step_signature", std::to_string(live.alignedStepSignature)},
        {"sample_scope", "after native visibility; compare matching step, level time, player position and initial object identity; sprite meaningful when visible"},
        {"reference_scope", "per-command CPU reference at setter and pre-visibility; actual GPU readback separately in resident_gpu; native-group reference is a separate run"}});
}
matjson::Value mirrorWorklistReport(const Live& live) {
    auto first = matjson::Value::object();
    if (const auto& miss = live.mirrorFirstMiss) {
        first = matjson::makeObject({{"object", miss->object}, {"chain", miss->chain},
            {"applied_cursor", miss->applied}, {"expected_cursor", miss->expected},
            {"step", miss->step}, {"command_index", miss->command}});
    }
    return matjson::makeObject({{"enabled", envFlags().hyperAggressive}, {"prefetch_enabled", envFlags().mirrorPrefetch},
        {"passes", live.mirrorPasses}, {"empty_passes", live.mirrorEmptyPasses},
        {"old_full_scan_slots", live.mirrorFullScanSlots}, {"candidate_visits", live.mirrorCandidateVisits},
        {"materializations", live.mirrorMaterializations}, {"chain_marks", live.mirrorChainMarks},
        {"chain_drains", live.mirrorChainDrains}, {"mask_word_merges", live.mirrorWordMerges},
        {"audit_passes", live.mirrorAuditPasses}, {"audit_slots", live.mirrorAuditSlots},
        {"audit_misses", live.mirrorAuditMisses}, {"failed", live.mirrorFailed},
        {"negative_requested", envFlags().mirrorWorklistNegative}, {"negative_injected", live.mirrorNegativeInjected},
        {"negative_caught", live.mirrorNegativeCaught}, {"first_miss", std::move(first)}});
}
matjson::Value visibilityCensusReport(const Live& live) {
    auto buckets = matjson::Value::object();
    std::uint64_t total = 0, paired = 0;
    for (unsigned i = 0; i < live.visibilityBuckets.size(); ++i) {
        const auto& b = live.visibilityBuckets[i];
        total += b.samples; paired += b.paired;
        buckets[visibilityRegionNames[i]] = matjson::makeObject({{"samples", b.samples},
            {"native_visible_before", b.visibleBefore}, {"paired", b.paired}, {"native_visible_after", b.visibleAfter},
            {"became_visible", b.entered}, {"became_hidden", b.exited}});
    }
    return matjson::makeObject({{"enabled", true}, {"population_materializations", live.visibilityCensusEvents},
        {"sampled_materializations", total}, {"paired_samples", paired},
        {"unpaired_samples", live.visibilityUnpaired + live.visibilitySamples.size()},
        {"repeat_materializations_same_frame", live.visibilityRepeatInFrame}, {"sampling_denominator", 32},
        {"margin_local_units", visibilityCensusMargin}, {"regions", std::move(buckets)},
        {"far_hidden_before_and_after", live.visibilityFarHiddenBoth},
        {"far_at_old_new_and_post_visibility_position_hidden_both", live.visibilityStableFarHidden},
        {"scope", "Sampled current logical position after unchanged mirror materialization; viewport AABB in local node space plus rotation-independent size radius. Near means outside viewport but within margin. Far is an estimate, not permission to skip native reads. Paired flags observed after stock updateVisibility; repeats counted exactly."}});
}
matjson::Value visibilityFilterReport(const Live& live) {
    auto trace = matjson::Value::array();
    for (const auto& p : live.filterTrace) {
        auto row = matjson::Value::array();
        row.push(p.step); row.push(p.command); row.push(std::to_string(p.state));
        row.push(std::to_string(p.visibility)); row.push(std::to_string(p.logical)); row.push(std::to_string(p.sprite));
        row.push(p.visibleCount); row.push(p.relevantCount);
        trace.push(std::move(row));
    }
    auto first = matjson::Value::object();
    if (live.filterFirstMiss) {
        const auto& m = *live.filterFirstMiss;
        first = matjson::makeObject({{"object", m.object}, {"step", m.step}, {"command_index", m.command}, {"reason", m.reason}});
    }
    auto packedFirst = matjson::Value::object();
    if (const auto& mismatch = live.packedFilterFirstMismatch) {
        auto section = [](VisibilitySection value) {
            auto result = matjson::Value::array();
            result.push(value.first); result.push(value.second);
            return result;
        };
        const auto& m = *mismatch;
        auto history = matjson::Value::array(); history.push(m.historyBaseX); history.push(m.historyBaseY);
        packedFirst = matjson::makeObject({{"object_id", m.object}, {"step", m.step}, {"command_index", m.command},
            {"packed_decision", filterDecisionNames[static_cast<unsigned>(m.packed)]},
            {"reference_decision", filterDecisionNames[static_cast<unsigned>(m.reference)]},
            {"cached_fields", matjson::makeObject({{"current_section", section(m.cachedCurrent)},
                {"old_section", section(m.cachedOld)}, {"visibility_space", m.cachedSpace},
                {"eligible", bool(m.cachedFlags & packedFilterEligible)},
                {"geometry_pending", bool(m.cachedFlags & packedFilterGeometryPending)},
                {"native_edit", bool(m.cachedFlags & packedFilterNativeEdit)}})},
            {"native_fields", matjson::makeObject({{"current_section", section(m.nativeCurrent)},
                {"old_section", section(m.nativeOld)}, {"history_base", std::move(history)},
                {"visibility_space", m.nativeSpace}, {"classification", filterClassNames[static_cast<unsigned>(m.nativeClass)]},
                {"geometry_pending", m.nativeGeometryQueued}, {"native_edit", m.nativeEdit},
                {"visible", m.nativeVisible}, {"owned", m.nativeOwned}})}});
    }
    auto initial = matjson::Value::object(), current = matjson::Value::object(), headless = matjson::Value::object();
    auto events = matjson::Value::object(), decisions = matjson::Value::object(), unsupported = matjson::Value::object();
    for (unsigned i = 0; i < live.filterInitialClasses.size(); ++i) {
        initial[filterClassNames[i]] = live.filterInitialClasses[i];
        current[filterClassNames[i]] = live.filterCurrentClasses[i];
        headless[filterClassNames[i]] = live.filterInitialHeadlessClasses[i];
        unsupported[filterClassNames[i]] = live.filterUnsupportedVisits[i];
    }
    for (unsigned i = 0; i < live.filterGeometryEvents.size(); ++i) events[filterChangeNames[i]] = live.filterGeometryEvents[i];
    for (unsigned i = 0; i < live.filterDecisions.size(); ++i) decisions[filterDecisionNames[i]] = live.filterDecisions[i];
    return matjson::makeObject({{"enabled", envFlags().visibilityFilter}, {"audit", envFlags().visibilityFilterAudit}, {"eligibility_revision", 2},
        {"packed_enabled", envFlags().packedVisibilityFilter},
        {"packed_predicate_comparisons", live.packedFilterPredicateComparisons},
        {"packed_predicate_mismatches", live.packedFilterPredicateMismatches},
        {"packed_first_mismatch", std::move(packedFirst)},
        {"eligible", live.filterEligible}, {"initial_eligible", live.filterInitialEligible},
        {"initial_headless_owned", live.filterInitialHeadless}, {"initial_headless_eligible", live.filterInitialHeadlessEligible},
        {"initial_classifications", std::move(initial)}, {"initial_headless_classifications", std::move(headless)},
        {"current_classifications", std::move(current)}, {"geometry_events", std::move(events)},
        {"candidate_decisions_audit_only", std::move(decisions)}, {"unsupported_candidate_visits_audit_only", std::move(unsupported)},
        {"reclassifications", live.filterReclassifications}, {"readmitted", live.filterReadmitted},
        {"space_changes", live.filterSpaceChanges}, {"geometry_forced_materialization_candidates", live.filterGeometryForced},
        {"pending_reclassifications", live.filterGeometryQueue.size()},
        {"spaces", live.filterSpaces.size()}, {"tracking_lost", live.filterTrackingLost},
        {"deferred_visits", live.filterDeferredVisits}, {"defer_episodes", live.filterEpisodes},
        {"deferred_now", live.visibilityDeferred.size()}, {"peak_deferred", live.filterPeakDeferred},
        {"camera_candidates", live.filterCameraCandidates}, {"resumed_at_visibility", live.filterResumed},
        {"resumed_by_other_consumers", live.filterConsumerResumes}, {"section_moves", live.filterSectionMoves},
        {"geometry_invalidations", live.filterInvalidations}, {"late_visible", live.filterLateVisible},
        {"audit_checks", live.filterAuditChecks}, {"audit_checkpoints", live.filterCheckpoints},
        {"audit_checkpoint_objects", live.filterCheckpointObjects}, {"audit_failures", live.filterAuditFailures},
        {"failed", live.filterFailed}, {"first_miss", std::move(first)},
        {"negative_requested", envFlags().visibilityFilterNegative},
        {"negative_injected", live.filterNegativeObject != UINT32_MAX}, {"negative_caught", live.filterNegativeCaught},
        {"trace_columns", "simulation_step,command_index,simulation_state,visible_set,logical_state,sprite_state,visible_count,relevant_count"},
        {"trace", std::move(trace)}});
}
void report(const char* reason) {
    if (!state || !state->scene) return;
    const auto& live = *state;
    if ((live.pivotlessAbort || live.mirrorFailed || live.filterFailed) && std::string_view(reason) != "test_complete") return;
    if (envFlags().performanceOnly) {
        // No periodic serialization, and teardown must not overwrite the
        // completed measurement after releasing the scene.
        if (std::string_view(reason) != "test_complete" && std::string_view(reason) != "aligned_timeout") return;
        const auto& times = live.cpuFrames;
        double sum = 0, worst = 0;
        for (double t : times) { sum += t; worst = std::max(worst, t); }
        const double mean = times.empty() ? 0 : sum / times.size();
        double intervalSum = 0;
        for (double t : live.perfIntervals) intervalSum += t;
        const double fps = intervalSum > 0 ? 1000.0 * live.perfIntervals.size() / intervalSum : 0;
        auto json = matjson::makeObject({{"schema", 1}, {"kind", "v2_performance"}, {"performance_only", true},
            {"reason", reason}, {"level", live.levelName}, {"test_completed", live.testCompleted},
            {"hyper_aggressive_enabled", envFlags().hyperAggressive}, {"baseline", live.baseline},
            {"frame_samples", times.size()}, {"cpu_frame_mean_ms", mean}, {"mean_fps", fps},
            {"frame_interval_samples", live.perfIntervals.size()}, {"frame_interval_p50_ms", percentile(live.perfIntervals, .5)},
            {"frame_interval_p95_ms", percentile(live.perfIntervals, .95)}, {"frame_interval_p99_ms", percentile(live.perfIntervals, .99)},
            {"cpu_frame_p50_ms", percentile(times, .5)}, {"cpu_frame_p95_ms", percentile(times, .95)},
            {"cpu_frame_p99_ms", percentile(times, .99)}, {"cpu_frame_max_ms", worst},
            {"retained_at_test_completion", live.retainedAtCompletion}, {"peak_owned", live.stats.acquired},
            {"ownership_revocations", live.stats.released}, {"native_position_rebases", live.hyperRebases},
            {"rebase_rejections", live.hyperPerfRebaseRejected}, {"render_failures", live.stats.renderFailures},
            {"emergency_releases", live.hyperEmergency}, {"unmodeled_attempts", live.hyperUnmodeled},
            {"pivotless_commands", live.pivotlessCommands}, {"pivotless_gpu_recipients", live.pivotlessGpuRecipients},
            {"pivotless_native_calls", live.pivotlessNativeCalls}, {"pivotless_batches", live.pivotlessBatches},
            {"pivotless_native_materializations", live.pivotlessNativeMaterializations},
            {"pivotless_probe_passed", live.pivotlessProbePassed}, {"pivotless_abort", live.pivotlessAbort},
            {"pivotless_section_events", live.pivotlessSectionEvents},
            {"resident_submissions", live.renderer ? live.renderer->counters().residentSubmissions : 0},
            {"resident_command_upload_bytes", live.renderer ? live.renderer->counters().residentCommandBytes : 0},
            {"native_move_visits", live.stats.nativeMoveVisits}, {"eliminated_move_visits", live.stats.eliminatedVisits},
            {"native_rotation_recipients", live.groupRotationNativeRecipients}, {"gpu_rotation_recipients", live.groupRotationRecipients},
            {"native_section_callbacks", live.stats.nativeCommitCallbacks}, {"resident_section_transitions", live.residentSectionCommits},
            {"routine_section_readback_bytes", live.sectionEventReadbackBytes}, {"resident_section_readback_batches", live.sectionEventBatches},
            {"simulation_steps", live.ticks}, {"level_time", live.layer->m_gameState.m_levelTime},
            {"sample_scope", "drawScene wall time after ownership compilation, including presentation; mean FPS uses start-to-start intervals; no reference validation"}});
        auto samples = matjson::Value::array();
        for (double t : times) samples.push(t);
        json["frame_times_ms"] = std::move(samples);
        auto trace = matjson::Value::array();
        std::map<std::uint64_t, std::uint64_t> histogram;
        double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
        std::uint64_t pairs = 0;
        for (const auto& f : live.perfTrace) {
            auto row = matjson::Value::array();
            row.push(f.steps); row.push(f.intervalMs); row.push(f.wallMs); row.push(f.levelTime);
            trace.push(std::move(row));
            ++histogram[f.steps];
            if (f.intervalMs > 0) {
                const double x = f.intervalMs, y = double(f.steps);
                ++pairs; sx += x; sy += y; sxx += x*x; syy += y*y; sxy += x*y;
            }
        }
        auto hist = matjson::Value::object();
        for (const auto& [steps, count] : histogram) hist[std::to_string(steps)] = count;
        json["step_histogram"] = std::move(hist);
        json["frame_trace_columns"] = "move_steps,start_to_start_ms,draw_wall_ms,level_time_s";
        json["frame_trace"] = std::move(trace);
        const double covariance = pairs*sxy - sx*sy;
        const double vx = pairs*sxx - sx*sx, vy = pairs*syy - sy*sy;
        if (pairs > 1 && vx > 0 && vy > 0) json["step_interval_correlation"] = covariance / std::sqrt(vx*vy);
        json["fixed_profile"] = envFlags().fixedProfile;
        json["mirror_worklist"] = mirrorWorklistReport(live);
        json["mirror_worklist_audit"] = envFlags().mirrorWorklistAudit;
        if (envFlags().visibilityFilter || envFlags().visibilityFilterAudit) {
            json["visibility_filter"] = visibilityFilterReport(live);
            json["fixed_clock_frames"] = live.fixedProfileClockFrames;
            if (envFlags().visibilityFilterAudit) {
                json["fixed_input_dt_s"] = 1.f / 30.f;
                json["sample_scope"] = "visibility filter parity audit, fixed director input; timings are diagnostic";
            }
        }
        if (envFlags().visibilityCensus) {
            json["visibility_census"] = visibilityCensusReport(live);
            json["fixed_clock_frames"] = live.fixedProfileClockFrames;
            json["fixed_input_dt_s"] = 1.f / 30.f;
            json["sample_scope"] = "visibility census with fixed director input; timings are diagnostic, not performance results";
        }
        if (envFlags().mirrorWorklistAudit) {
            json["fixed_clock_frames"] = live.fixedProfileClockFrames;
            json["fixed_input_dt_s"] = 1.f / 30.f;
            json["sample_scope"] = "mirror coverage oracle with fixed director input; timings are diagnostic, not performance results";
        }
        if (envFlags().fixedProfile) {
            json["fixed_input_dt_s"] = 1.f / 30.f;
            json["fixed_clock_frames"] = live.fixedProfileClockFrames;
            json["spike_audit"] = spikeReport(live);
            if (envFlags().nativeVisibilityProfile)
                json["native_visibility_profile"] = nativeVisibilityProfileReport(live);
            if (envFlags().visibilityOwnershipShadow)
                json["visibility_ownership_shadow"] = visibilityOwnershipShadowReport(live);
            auto mirrors = matjson::Value::object();
            for (const auto& [consumer, count] : live.residentMirrors) {
                const auto it = live.residentSyncByReason.find(consumer);
                const auto pivotless = live.profilePivotlessMirrors.find(consumer);
                mirrors[std::string(consumer)] = matjson::makeObject({{"materializations", count},
                    {"cpu_ms", it == live.residentSyncByReason.end() ? 0.0 : it->second},
                    {"pivotless_materializations", pivotless == live.profilePivotlessMirrors.end() ? 0u : pivotless->second}});
            }
            json["mirrors_by_consumer"] = std::move(mirrors);
            auto flushes = matjson::Value::object();
            for (const auto& [consumer, count] : live.replayFlushReasons) flushes[std::string(consumer)] = count;
            json["flushes_by_reason"] = std::move(flushes);
            if (live.residentTimer) {
                const auto gpu = live.residentTimer->samples();
                json["resident_gpu_timestamp_samples"] = gpu.size();
                json["resident_gpu_timestamp_p50_ms"] = percentile(gpu, .5);
                json["resident_gpu_timestamp_p95_ms"] = percentile(gpu, .95);
                json["resident_gpu_timestamp_skipped"] = live.residentTimer->skipped();
            }
            json["profile_scope"] = "fixed director input; native physics unmodified; no validation readbacks, setter traces or legacy diagnostics; exclusive phase times exclude nested instrumented scopes; GPU timestamp spans may include host gaps";
        }
        if (const auto& file = envFlags().reportPath) { std::ofstream stream(*file); stream << json.dump(); }
        return;
    }
    auto json = matjson::makeObject({{"schema", 2}, {"kind", "v2_live"}, {"reason", reason}, {"level", live.levelName},
        {"total", live.total}, {"eligible", live.eligible}, {"actually_owned", live.owned()}, {"peak_owned", live.stats.acquired},
        {"headless_acquired", live.stats.headlessAcquired}, {"headless_retained", live.stats.headlessAcquired - live.stats.headlessReleased},
        {"headless_eliminated_native_move_member_visits", live.stats.headlessEliminatedVisits},
        {"retained_at_test_completion", live.retainedAtCompletion},
        {"headless_retained_at_test_completion", live.headlessRetainedAtCompletion},
        {"chains", live.scene->chainCount()}, {"max_active_chains", live.stats.maxActiveChains}, {"max_dirty_palette_entries", live.stats.maxDirty},
        {"frames", live.frames}, {"native_move_member_visits", live.stats.nativeMoveVisits}, {"native_section_commit_callbacks", live.stats.nativeCommitCallbacks},
        {"eliminated_native_move_member_visits", live.stats.eliminatedVisits}, {"handoff_cpu_commits", live.stats.handoffCommits},
        {"sprite_transform_entry_visits", live.stats.spriteVisits}, {"native_sprite_geometry_evaluations_suppressed", live.stats.geometrySuppressed},
        {"native_atlas_draws", live.stats.nativeDraws}, {"v2_draws", live.stats.v2Draws}, {"native_atlas_quads", live.stats.nativeQuads}, {"v2_quads", live.stats.v2Quads},
        {"palette_page_boundaries", live.stats.pageBreaks}, {"material_boundaries", live.stats.materialBoundaries}, {"ownership_boundaries", live.stats.ownershipBoundaries},
        {"camera_chain_checks", live.stats.chainCameraChecks}, {"validation_object_visits", live.stats.validationVisits},
        {"dependency_guard_member_visits", live.stats.dependencyGuardVisits},
        {"cached_policies_enabled", live.cachedPoliciesEnabled},
        {"cached_policy_hits", live.stats.cachedPolicyHits}, {"cached_policy_misses", live.stats.cachedPolicyMisses},
        {"structural_predicate_evaluations_avoided", live.stats.structuralPredicatesAvoided},
        {"dense_identity_hits", live.stats.denseIdentityHits}, {"hash_lookup_fallbacks", live.stats.hashLookupFallbacks},
        {"cache_invalidations", live.stats.cacheInvalidations}, {"dynamic_section_checks", live.stats.dynamicSectionChecks},
        {"precompiled_route_hits", live.stats.precompiledRouteHits}, {"route_hash_lookups", live.stats.routeHashLookups},
        {"native_atlas_dirty_payload_bytes", live.stats.nativeAtlasDirtyBytes}, {"native_atlas_upload_events", live.stats.nativeAtlasUploads},
        {"v2_native_commit_violations", live.stats.ownershipCommitViolations},
        {"render_failures", live.stats.renderFailures}, {"test_completed", live.testCompleted},
        {"reference_validation_enabled", live.validate},
        {"validation_readback_bytes", live.stats.validationReadbackBytes}, {"routine_transform_readback_bytes", 0},
        {"routine_v2_cpu_position_commits", 0}, {"routine_geometry_upload_bytes", 0}, {"max_reference_position_error", live.stats.maxDivergence},
        {"max_conservative_pixel_error_bound", live.stats.maxPixelBound}, {"pixel_error_budget", pixelBudget},
        {"cpu_frame_p50_ms", percentile(live.cpuFrames, .5)}, {"cpu_frame_p95_ms", percentile(live.cpuFrames, .95)}, {"cpu_frame_p99_ms", percentile(live.cpuFrames, .99)},
        {"gpu_timing_status", live.gpuStatus},
        {"native_fallback_drawing", "original_contiguous_atlas_ranges"}, {"runtime_native_to_v2", 0}});
    json["loading"] = matjson::makeObject({{"native_playlayer_init_ms", live.nativeInitMs},
        {"descriptor_scan_ms", live.descriptorCompileMs}, {"legacy_precompile_ms", live.legacyPrecompileMs},
        {"first_visible_frame_scene_compile_ms", live.sceneCompileMs},
        {"scope", "separate CPU wall scopes; scene compile occurs after initial visibility, not inside native init"}});
    if (envFlags().hyperAggressive) {
        json["mirror_worklist"] = mirrorWorklistReport(live);
        auto reasons = matjson::Value::object();
        std::uint64_t retained = 0;
        for (const auto& [reason, site] : live.hyperReasons) {
            auto groups = matjson::Value::object();
            for (const auto& [group, count] : site.groups) groups[std::to_string(group)] = count;
            retained += site.retained;
            reasons[reason] = matjson::makeObject({{"attempted", site.attempted}, {"retained", site.retained},
                {"emergency_released", site.emergencyReleased}, {"unmodeled", site.unmodeled},
                {"unique_objects", site.objects.size()}, {"first_step", site.firstStep}, {"last_step", site.lastStep},
                {"by_group", std::move(groups)}});
        }
        auto first = matjson::Value::object();
        if (const auto& f = live.hyperFirstFailure) {
            first = matjson::makeObject({{"reason", f->reason}, {"field", f->field}, {"object", f->object},
                {"step", f->step}, {"command_index", f->command}, {"group", f->group},
                {"expected_bits", std::to_string(f->expected)}, {"actual_bits", std::to_string(f->actual)}});
        }
        auto pivotlessRules = matjson::Value::array();
        for (unsigned cls = 0; cls < live.pivotlessRules.size(); ++cls) {
            const auto& rule = live.pivotlessRules[cls];
            pivotlessRules.push(matjson::makeObject({{"class", cls}, {"rotation_mask", rule.rotationMask},
                {"dirty_set", rule.dirtySet}, {"finish_mode", rule.finishMode}}));
        }
        json["hyper_aggressive"] = matjson::makeObject({{"enabled", true}, {"policy_attempts_retained", retained},
            {"unmodeled_attempts", live.hyperUnmodeled}, {"emergency_released", live.hyperEmergency},
            {"setter_checks", live.hyperSetterChecks}, {"setter_failures", live.hyperSetterFailures},
            {"pivotless_checks", live.hyperPivotlessChecks}, {"pivotless_failures", live.hyperPivotlessFailures},
            {"pivotless_commands", live.pivotlessCommands}, {"pivotless_gpu_recipients", live.pivotlessGpuRecipients},
            {"pivotless_native_calls", live.pivotlessNativeCalls}, {"pivotless_batches", live.pivotlessBatches},
            {"pivotless_native_materializations", live.pivotlessNativeMaterializations},
            {"pivotless_probe_passed", live.pivotlessProbePassed}, {"pivotless_abort", live.pivotlessAbort},
            {"pivotless_section_events", live.pivotlessSectionEvents},
            {"pivotless_rules", std::move(pivotlessRules)},
            {"angle_checks", live.hyperAngleChecks}, {"angle_failures", live.hyperAngleFailures},
            {"native_position_rebases", live.hyperRebases}, {"native_rebase_unique_objects", live.hyperRebaseObjects.size()},
            {"native_rebase_validation_checks", live.hyperRebaseChecks}, {"native_rebase_validation_failures", live.hyperRebaseFailures},
            {"native_rebase_diverged_before_visibility", live.hyperRebaseBeforeVisibility},
            {"native_rebase_diverged_during_visibility", live.hyperRebaseDuringVisibility},
            {"negative_requested", envFlags().hyperNegative}, {"negative_injected", live.hyperNegativeInjected},
            {"negative_caught", live.hyperNegativeCaught}, {"negative_object", live.hyperNegativeObject},
            {"first_mismatch", std::move(first)}, {"reasons", std::move(reasons)},
            {"experimental_only", true}, {"scope", "Suppress policy handoffs; unsupported semantics counted; fatal resource/lifetime failures still release; no correctness claim from ownership alone"}});
    }
    if (envFlags().revocationAudit && std::string_view(reason) == "test_complete")
        json["revocation_audit"] = revocationReport(live);
    if (envFlags().setterAudit && std::string_view(reason) == "test_complete")
        json["position_setter_audit"] = setterReport(live);
    if (envFlags().setterShadowGroup && std::string_view(reason) == "test_complete")
        json["setter_shadow"] = setterShadowReport(live);
    if (envFlags().temporalReplay) {
        auto reasons = matjson::Value::object();
        for (const auto& [name, count] : live.replayFlushReasons) reasons[std::string(name)] = count;
        auto groups = matjson::Value::object();
        std::map<int, std::array<std::uint64_t, 2>> visits;
        for (const auto& route : live.routes) {
            visits[route.nativeGroup][0] += route.residentNativeVisits;
            visits[route.nativeGroup][1] += route.residentVisits;
        }
        std::uint64_t nativeStaticOnlyGroups = 0, nativeMixedGroups = 0;
        for (const auto& [id, d] : live.descriptorGroups) {
            const auto v = visits[id];
            if (d.staticParams && !d.dynamicParams) nativeStaticOnlyGroups += v[0];
            else if (d.staticParams) nativeMixedGroups += v[0];
            groups[std::to_string(id)] = matjson::makeObject({{"move_descriptors", d.moves},
                {"rotate_descriptors", d.rotations}, {"static_parameter_candidates", d.staticParams},
                {"dynamic_parameter_candidates", d.dynamicParams}, {"touch_activated", d.touch},
                {"spawn_activated", d.spawn}, {"pivot_references", d.pivot}, {"initial_group_members", d.memberCount},
                {"max_duration_s", d.maxDuration}, {"observed_native_move_visits", v[0]},
                {"observed_resident_move_visits", v[1]}});
        }
        json["temporal_replay"] = matjson::makeObject({{"capacity", live.replayCapacity},
            {"section_event_capacity", live.replaySectionCapacity},
            {"max_section_event_potential", live.replayMaxSectionPotential},
            {"max_actual_section_events", live.replayMaxSectionEvents},
            {"ordered_batches", live.replayBatches}, {"commands_replayed", live.replayCommands},
            {"multi_step_batches", live.replayMultiStepBatches}, {"multi_step_commands", live.replayMultiStepCommands},
            {"max_commands_in_batch", live.replayMaxCommands}, {"max_observed_step_span", live.replayMaxStepSpan},
            {"flush_reasons", std::move(reasons)}, {"trigger_groups", std::move(groups)},
            {"native_visits_static_parameter_only_groups", nativeStaticOnlyGroups},
            {"native_visits_mixed_parameter_groups", nativeMixedGroups},
            {"classification_scope", "group-level descriptor screen, not proof of object eligibility or per-trigger runtime attribution"},
            {"step_scope", "completed processMoveActionsStep count at submission, not absolute activation timeline"},
            {"semantics", "existing native boundaries, ordered GPU replay, no step skipping or affine composition"}});
    }
    static constexpr std::array invalidationNames{
        "layer_group_add", "layer_group_remove", "group_parent_add", "group_parent_remove",
        "group_parent_set", "group_parent_remove_one", "group_parent_promote", "group_parent_load",
        "object_type", "start_position", "object_group_add", "object_group_remove", "object_reset",
        "retirement", "scene_destroy"
    };
    static_assert(invalidationNames.size() == static_cast<std::size_t>(MovePolicyInvalidation::count));
    auto invalidations = matjson::Value::object();
    for (std::size_t i = 0; i < invalidationNames.size(); ++i)
        invalidations[invalidationNames[i]] = live.stats.cacheInvalidationsBySource[i];
    json["cache_invalidations_by_source"] = std::move(invalidations);
    json["cache_invalidations_after_ready"] = live.stats.cacheInvalidationsAfterReady;
    if (live.validate) {
        json["dense_ownership_validation_samples"] = live.stats.denseOwnershipSamples;
        json["dense_ownership_validation_mismatches"] = live.stats.denseOwnershipMismatches;
    }
    json["level_time"] = live.layer->m_gameState.m_levelTime;
    json["native_progress_wall_s"] = live.progressStart == Clock::time_point{} ? 0 : std::chrono::duration<double>(Clock::now() - live.progressStart).count();
    json["cpu_while_owned_samples"] = live.cpuOwnedFrames.size();
    if (!live.cpuOwnedFrames.empty()) {
        json["cpu_while_owned_p50_ms"] = percentile(live.cpuOwnedFrames, .5);
        json["cpu_while_owned_p95_ms"] = percentile(live.cpuOwnedFrames, .95);
        json["cpu_while_owned_p99_ms"] = percentile(live.cpuOwnedFrames, .99);
    }
    if (live.renderer) {
        const auto& counters = live.renderer->counters();
        json["initial_geometry_and_palette_upload_bytes"] = counters.initialUploadBytes;
        json["palette_upload_bytes"] = counters.paletteUploadBytes;
        json["draw_uniform_upload_bytes"] = counters.drawUniformUploadBytes;
        if (live.residentEnabled) {
            auto groups = matjson::Value::object();
            std::map<int, std::array<std::uint64_t, 2>> visits;
            for (const auto& route : live.routes) {
                if (!route.residentVisits && !route.residentNativeVisits) continue;
                visits[route.nativeGroup][0] += route.residentVisits;
                visits[route.nativeGroup][1] += route.residentNativeVisits;
            }
            for (const auto& [group, counts] : visits) {
                const auto total = counts[0] + counts[1];
                groups[std::to_string(group)] = matjson::makeObject({{"owned", counts[0]}, {"native", counts[1]},
                    {"logical", total}, {"coverage", total ? double(counts[0]) / double(total) : 0.0}});
            }
            const auto logical = live.stats.nativeMoveVisits + live.stats.eliminatedVisits;
            auto mirrors = matjson::Value::object();
            for (const auto& [reason, count] : live.residentMirrors) mirrors[std::string(reason)] = count;
            json["resident_gpu"] = matjson::makeObject({{"aggressive_ownership", live.aggressive},
                {"logical_move_member_operations", logical},
                {"movement_coverage", logical ? double(live.stats.eliminatedVisits) / double(logical) : 0.0},
                {"ownership_acquisitions", live.stats.acquired}, {"ownership_revocations", live.stats.released},
                {"persistent_edits", live.persistentEdits}, {"retained_native_edits", live.retainedNativeEdits},
                {"retained_position_rebases", live.retainedPositionRebases}, {"retained_geometry_changes", live.retainedGeometryChanges},
                {"retained_rebase_batches", live.retainedRebaseBatches}, {"retained_reentrant_fallbacks", live.retainedReentrantFallbacks},
                {"native_state_applications", live.residentNativeApplies}, {"mirrors_by_consumer", std::move(mirrors)},
                {"section_candidates", live.residentSectionCandidates}, {"section_transition_callbacks", live.residentSectionCommits},
                {"commands", live.residentJournal.size()}, {"submissions", counters.residentSubmissions},
                {"dispatches", counters.dispatches}, {"command_upload_bytes", counters.residentCommandBytes},
                {"draws_after_dispatch", counters.residentMovedDrawCalls}, {"geometry_refresh_upload_bytes", counters.residentGeometryBytes},
                {"cpu_command_pack_and_upload_ms", counters.residentUploadMs}, {"cpu_handoff_ms", live.residentHandoffMs},
                {"cpu_sync_ms", live.residentSyncMs},
                {"validation_checks", live.residentValidationChecks}, {"exactness_violations", live.residentExactnessViolations},
                {"validation_readback_bytes", counters.residentValidationReadbackBytes},
                {"validation_readback_wait_ms", counters.residentValidationWaitMs},
                {"group_rotation", live.groupRotation}, {"rotation_group_commands", live.groupRotationCommands},
                {"rotation_gpu_recipient_operations", live.groupRotationRecipients},
                {"rotation_native_recipient_operations", live.groupRotationNativeRecipients},
                {"section_event_batches", live.sectionEventBatches}, {"section_event_ms", live.sectionEventMs},
                {"synchronous_waits_in_routine_path", live.sectionEventBatches},
                {"routine_readback_bytes", live.sectionEventReadbackBytes}, {"group_visits", std::move(groups)}});
            auto& residentReport = json["resident_gpu"];
            residentReport["dispatch_layout"] = "gpu_chain_masks";
            residentReport["dispatch_cpu_wall_ms"] = live.residentDispatchWallMs;
            residentReport["section_commit_cpu_wall_ms"] = live.residentSectionCommitWallMs;
            residentReport["compute_timing_scope"] = "elapsed_gpu_timestamps_across_dispatch_and_host_submission_gaps";
            residentReport["membership_chain_count"] = counters.membershipChains;
            residentReport["chain_group_incidences"] = counters.membershipGroupIncidences;
            residentReport["max_groups_per_membership_chain"] = counters.maxGroupsPerMembershipChain;
            residentReport["chain_member_blocks"] = counters.membershipBlocks;
            auto group575 = matjson::Value::array();
            for (const auto& route : live.routes)
                if (route.nativeGroup == 575)
                    group575.push(matjson::makeObject({{"resident_width", route.residentWidth}}));
            residentReport["group575_routes"] = std::move(group575);
            if (envFlags().nativeCallers) {
                auto histograms = matjson::Value::object();
                constexpr const char* names[]{"native_position_setter", "resident_texture_rectangle_read", "native_opacity_change"};
                for (std::size_t i = 0; i < live.nativeCallerHistograms.size(); ++i) {
                    const auto& hist = live.nativeCallerHistograms[i];
                    auto callers = matjson::Value::array();
                    for (const auto& [pcs, count] : hist.callers)
                        callers.push(matjson::makeObject({{"caller_rva", pcs.first}, {"parent_rva", pcs.second},
                            {"samples", count}}));
                    histograms[names[i]] = matjson::makeObject({{"eligible_calls", hist.calls}, {"samples", hist.samples},
                        {"sampling_denominator", i == 0 ? 8 : 64}, {"callers", std::move(callers)}});
                }
                residentReport["native_caller_histograms"] = std::move(histograms);
            }
            if (live.residentTimer) {
                const auto times = live.residentTimer->samples();
                double total = 0;
                for (double ms : times) total += ms;
                json["resident_gpu"]["compute_samples"] = times.size();
                json["resident_gpu"]["compute_sample_sum_ms"] = total;
                json["resident_gpu"]["compute_batch_p50_ms"] = percentile(times, .5);
                json["resident_gpu"]["compute_batch_p95_ms"] = percentile(times, .95);
            }
        }
    }
    if (live.gpuTimer && !live.gpuTimer->samples().empty()) {
        json["gpu_frame_p50_ms"] = percentile(live.gpuTimer->samples(), .5);
        json["gpu_frame_p95_ms"] = percentile(live.gpuTimer->samples(), .95);
        json["gpu_frame_p99_ms"] = percentile(live.gpuTimer->samples(), .99);
        json["gpu_samples"] = live.gpuTimer->samples().size();
        json["gpu_samples_skipped_without_waiting"] = live.gpuTimer->skipped();
    }
    if (live.capture) json["capture"] = *live.capture;
    if (live.toggleFixtureTrace) json["native_toggle_fixture"] = *live.toggleFixtureTrace;
    if (live.benchmarkFinished && !live.cpuBenchmarkFrames.empty()) {
        json["level_80_90"] = matjson::makeObject({
            {"level_start", 80}, {"level_end", 90}, {"frames", live.cpuBenchmarkFrames.size()},
            {"cpu_frame_p50_ms", percentile(live.cpuBenchmarkFrames, .5)},
            {"cpu_frame_p95_ms", percentile(live.cpuBenchmarkFrames, .95)},
            {"cpu_frame_p99_ms", percentile(live.cpuBenchmarkFrames, .99)},
            {"native_move_member_visits", live.benchmarkEnd.nativeMoveVisits - live.benchmarkStart.nativeMoveVisits},
            {"eliminated_native_move_member_visits", live.benchmarkEnd.eliminatedVisits - live.benchmarkStart.eliminatedVisits},
            {"gpu_timing", "not isolated for this interval"}});
    }
    if (live.structuralHandoffTest) {
        json["structural_handoff_fixture"] = matjson::makeObject({{"applied", true}, {"all_were_v2_owned", live.structuralHandoffOwned}});
        json["structural_handoff_fixture"]["objects"] = matjson::Value::array();
        for (auto* o : live.structuralHandoffObjects) json["structural_handoff_fixture"]["objects"].push(matjson::makeObject({
            {"position_x", o->m_positionX}, {"position_y", o->m_positionY},
            {"rotation_x", o->getRotationX()}, {"rotation_y", o->getRotationY()},
            {"scale_x", o->getScaleX()}, {"scale_y", o->getScaleY()},
            {"skew_x", o->getSkewX()}, {"skew_y", o->getSkewY()},
            {"anchor_x", o->getAnchorPoint().x}, {"anchor_y", o->getAnchorPoint().y},
            {"flip_x", o->isFlipX()}, {"flip_y", o->isFlipY()}}));
    }
    json["rejections"] = matjson::Value::object();
    for (const auto& [key, value] : live.rejected) json["rejections"][key] = value;
    json["inactive_readiness"] = matjson::Value::object();
    for (const auto& [key, value] : live.inactiveReadiness) json["inactive_readiness"][key] = value;
    json["ownership_handoffs"] = matjson::Value::object();
    for (const auto& [key, value] : live.transitions) json["ownership_handoffs"][key] = value;
    json["unsupported_trigger_ids"] = matjson::Value::object();
    for (const auto& [key, value] : live.unknownTriggers) json["unsupported_trigger_ids"][std::to_string(key)] = value;
    json["unreviewed_loaded_mods"] = matjson::Value::array();
    for (const auto& name : live.unknownMods) json["unreviewed_loaded_mods"].push(name);
    if (!live.censusMasks.empty() && (std::string_view(reason) == "test_complete" || std::string_view(reason) == "director_purge")) {
        auto triggers = matjson::makeObject({{"color_copy_references", live.colorCopy}, {"spawn_triggered", live.triggeredBySpawn}, {"touch_triggered", live.triggeredByTouch}});
        triggers["color_channel_targets"] = matjson::Value::object();
        for (auto [id, count] : live.colorTargets) triggers["color_channel_targets"][std::to_string(id)] = count;
        triggers["shake_target_group_field"] = matjson::Value::object();
        for (auto [id, count] : live.shakeTargets) triggers["shake_target_group_field"][std::to_string(id)] = count;
        json["trigger_899_1520"] = std::move(triggers);
    }
    if (!live.censusMasks.empty() && (std::string_view(reason) == "test_complete" || std::string_view(reason) == "director_purge")) {
        static constexpr const char* labels[]{"non_decoration", "object_class", "no_membership", "protected_group", "parent_or_linked", "animated_particle_or_ui", "history_enter_fade", "position_offset", "compound_sprite", "local_transform", "initially_inactive", "initially_unbatched", "custom_shader_observed", "spatial_state_unavailable", "unresolved_level_dependency", "direct_color_channel_target", "camera_shake_level", "group_parent", "area_parent", "parent_mode_positive", "linked_group", "particle_pointer", "has_particles", "animated_child", "ui_object", "non_decoration2", "enter_effect", "fade_effect", "temporary_x_offset", "position_x_offset", "position_y_offset", "color_sprite", "glow_sprite", "children", "scale", "rotation", "skew", "flip", "parent_mode_negative"};
        std::array<std::uint64_t, 39> objects{}, visits{};
        std::array<std::uint64_t, 10> causeObjects{}, causeVisits{};
        std::array<std::uint64_t, 4> stageObjects{}, stageVisits{};
        // These are cumulative hypothetical screens over known compile-time
        // attributes, not ownership or predictions of speedup.
        constexpr std::uint64_t core = (1ull << 15) - 1;
        constexpr std::uint64_t readinessAndVeto = (1ull << 10) | (1ull << 11) | (1ull << 13) | (1ull << 14);
        const std::array<std::uint64_t, 4> ignoredStages{
            readinessAndVeto,
            readinessAndVeto | (1ull << 9),
            readinessAndVeto | (1ull << 9) | (1ull << 6),
            readinessAndVeto | (1ull << 9) | (1ull << 6) | (1ull << 3)};
        std::uint64_t candidateObjects = 0, v1ParentCandidates = 0, v1ParentCandidateVisits = 0, fullyCompatible = 0, coveredVisits = 0;
        std::uint64_t structuralParentCandidates = 0, structuralParentVisits = 0;
        std::uint64_t fundamental = 0, functionality = 0, implementation = 0;
        constexpr std::uint32_t ignored = (1u << 10) | (1u << 11) | (1u << 13) | (1u << 14);
        for (std::size_t id = 0; id < live.censusMasks.size(); ++id) {
            const auto mask = live.censusMasks[id];
            for (unsigned bit = 0; bit < objects.size(); ++bit) if (mask & (1ull << bit)) ++objects[bit];
            for (unsigned bit = 0; bit < causeObjects.size(); ++bit)
                if (live.censusProtectionCauses[id] & (1u << bit)) ++causeObjects[bit];
            for (unsigned stage = 0; stage < ignoredStages.size(); ++stage)
                if (!(mask & core & ~ignoredStages[stage])) ++stageObjects[stage];
            if (!(mask & 0x3ffu)) ++candidateObjects;
            if (!(mask & (0x3ffu & ~(1u << 4))) && !(mask & ((1ull<<17)|(1ull<<18)|(1ull<<19)|(1ull<<20)))) ++v1ParentCandidates;
            if (!(mask & (0x3ffu & ~(1u << 4) & ~(1u << 9))) && !(mask & ((1ull<<17)|(1ull<<18)|(1ull<<19)|(1ull<<20)))) ++structuralParentCandidates;
            if (!mask) ++fullyCompatible;
            if (mask & 3u) ++fundamental;
            else if (mask & ((1u<<3)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<12))) ++functionality;
            else if (mask) ++implementation;
        }
        std::map<std::string, std::uint64_t> rejectionVisits;
        for (const auto& route : live.routes) if (route.censusCalls) {
            for (auto id : route.members) {
                coveredVisits += route.censusCalls;
                rejectionVisits[live.initialMoveRejections[id]]
                += route.censusCalls;
                const auto mask = live.censusMasks[id];
                for (unsigned bit = 0; bit < causeVisits.size(); ++bit)
                    if (live.censusProtectionCauses[id] & (1u << bit)) causeVisits[bit] += route.censusCalls;
                for (unsigned stage = 0; stage < ignoredStages.size(); ++stage)
                    if (!(mask & core & ~ignoredStages[stage])) stageVisits[stage] += route.censusCalls;
                if (!(mask & (0x3ffu & ~(1u << 4))) && !(mask & ((1ull<<17)|(1ull<<18)|(1ull<<19)|(1ull<<20))))
                    v1ParentCandidateVisits += route.censusCalls;
                if (!(mask & (0x3ffu & ~(1u << 4) & ~(1u << 9))) && !(mask & ((1ull<<17)|(1ull<<18)|(1ull<<19)|(1ull<<20))))
                    structuralParentVisits += route.censusCalls;
                for (unsigned bit = 0; bit < visits.size(); ++bit) if (mask & (1ull << bit)) visits[bit] += route.censusCalls;
            }
        }
        auto census = matjson::makeObject({{"scope", "all_loaded_objects_independent_overlapping_reasons"},
            {"potential_candidates_excluding_transient_readiness_and_unresolved_level_veto", candidateObjects},
            {"potential_membership_chains", live.censusCandidateChains}, {"fully_compatible_now", fullyCompatible},
            {"potential_candidates_with_v1_nonpositive_parent_mode", v1ParentCandidates},
            {"potential_chains_with_v1_nonpositive_parent_mode", live.censusV1ParentCandidateChains},
            {"potential_v1_parent_candidate_move_visits", v1ParentCandidateVisits},
            {"prospective_structural_and_nonpositive_parent_objects", structuralParentCandidates},
            {"prospective_structural_and_nonpositive_parent_membership_chains", live.censusStructuralParentChains},
            {"prospective_structural_and_nonpositive_parent_observed_move_visits", structuralParentVisits},
            {"prospective_scope", "upper bound only: ignores transient readiness and unresolved level veto; structural and parent compatibility not proven generally"},
            {"fundamental_priority_category", fundamental}, {"additional_functionality_priority_category", functionality},
            {"implementation_limitations_priority_category", implementation}, {"covered_native_move_visits", coveredVisits},
            {"native_move_visits_total", live.stats.nativeMoveVisits}, {"work_scope", "observed_compiled_array_move_calls_only_overlapping_reasons"}});
        census["reasons"] = matjson::Value::object();
        for (unsigned bit = 0; bit < objects.size(); ++bit)
            census["reasons"][labels[bit]] = matjson::makeObject({{"objects", objects[bit]}, {"observed_move_visits", visits[bit]}});
        static constexpr const char* causeLabels[]{"center_or_rotation_reference", "player_x_lock_recipient",
            "other_dynamic_move_target", "non_move_target", "target_group_mapping", "dynamic_reference",
            "trigger_1346_target", "trigger_1007_target", "trigger_1049_target", "trigger_1006_target"};
        census["protection_causes_overlapping"] = matjson::Value::object();
        for (unsigned bit = 0; bit < causeObjects.size(); ++bit)
            census["protection_causes_overlapping"][causeLabels[bit]] = matjson::makeObject({{"objects", causeObjects[bit]}, {"observed_move_visits", causeVisits[bit]}});
        static constexpr const char* stageLabels[]{"ignore_unresolved_veto_and_initial_readiness", "plus_static_transforms",
            "plus_history_enter_fade", "plus_protected_groups"};
        census["cumulative_hypothetical_screens"] = matjson::Value::object();
        for (unsigned stage = 0; stage < ignoredStages.size(); ++stage)
            census["cumulative_hypothetical_screens"][stageLabels[stage]] = matjson::makeObject({
                {"objects", stageObjects[stage]}, {"observed_move_visits", stageVisits[stage]}});
        census["hypothetical_screen_scope"] = "upper bounds, not eligibility: ignores only named blockers; unknown shader and indirect material dependencies remain unresolved; reasons overlap";
        json["move_rejection_workload"] = matjson::Value::object();

for (const auto& [reason, visits] : rejectionVisits) {
    json["move_rejection_workload"][reason] = visits;
}
        json["eligibility_census"] = std::move(census);
    }
    if (live.audit && std::string_view(reason) == "test_complete")
        json["retention_audit"] = retentionAudit(live);
    if (envFlags().spikeAudit && std::string_view(reason) == "test_complete")
        json["spike_audit"] = spikeReport(live);
    log::info("VULKANIZER_V2_REPORT {}", json.dump(matjson::NO_INDENTATION));
    // Keep the completed measurement rather than the subsequent teardown.
    if (const auto& file = envFlags().reportPath; file &&
        ((!live.audit && !envFlags().spikeAudit) || !live.testCompleted || std::string_view(reason) == "test_complete")) {
        std::ofstream stream(*file); stream << json.dump();
    }
}
}
bool requested() {
    static const bool enabled = envFlags().v2 || envFlags().gpuResident ||
        Mod::get()->getSettingValue<bool>("experimental-v2-shared-chains");
    return enabled;
}
bool nativeVisibilityProfileEnabled() { return envFlags().nativeVisibilityProfile; }
bool visibilityOwnershipShadowEnabled() { return state && envFlags().visibilityOwnershipShadow; }
void recordVisibilityOwnershipNativeVisibility(GameObject* object, bool visible) {
    if (!state || !envFlags().visibilityOwnershipShadow || !state->ready || !object) return;
    const auto found = state->ids.find(object);
    if (found == state->ids.end()) return;
    state->recordVisibilityOwnershipNativeState(found->second, visible);
}
bool moveSectionsRequested() { return envFlags().moveSections && !envFlags().baseline; }
bool cachedPoliciesRequested() { return envFlags().cachedPolicies; }
bool auditRequested() { return envFlags().audit && envFlags().aggressive; }
void recordAuditSectionSample(double milliseconds) {
    if (!state || !state->audit || !state->ready || state->internal) return;
    ++state->auditSectionSamples;
    state->auditSectionSampleMs += milliseconds;
}
void recordAuditRotationArray(CCArray* objects) {
    if (!state || !state->audit || !state->ready || !objects || state->internal) return;
    const auto found = state->routeIndices.find(objects);
    const int group = found == state->routeIndices.end() ? -1 : state->routes[found->second].nativeGroup;
    state->auditRotationArrays[group].record(objects->count(), state->layer->m_gameState.m_levelTime);
}
void recordSpikeRotationArray(CCArray* objects) {
    if (!spike::audit.active || !state || !objects || state->internal) return;
    const auto found = state->routeIndices.find(objects);
    const int group = found == state->routeIndices.end() ? -1 : state->routes[found->second].nativeGroup;
    spike::audit.recordGroup(group, spike::Work::nativeRotate, objects->count());
}
bool canSkipMoveSection(GJBaseGameLayer* layer, GameObject* object) {
    return stableMoveSectionEligibility(object) && dynamicMoveSectionEligibility(layer, object);
}
CachedMoveSectionResult cachedMoveSection(GJBaseGameLayer* layer, GameObject* object) {
    if (!state || state->layer != layer || !state->ready || state->internal || !state->cachedPoliciesEnabled)
        return CachedMoveSectionResult::notApplicable;

    auto& live = *state;
    constexpr ObjectId invalid = std::numeric_limits<ObjectId>::max();
    ObjectId id = invalid;
    std::size_t denseIndex = 0;
    if (object) {
        const auto candidate = static_cast<std::int64_t>(object->m_uniqueID) - live.denseIdBase;
        if (candidate >= 0 && static_cast<std::size_t>(candidate) < live.denseIds.size()) {
            denseIndex = static_cast<std::size_t>(candidate);
            const auto mapped = live.denseIds[denseIndex];
            if (mapped < live.entries.size() && live.entries[mapped].object.data() == object) {
                id = mapped;
                ++live.stats.denseIdentityHits;
            }
        }
    }

    if (id == invalid) {
        ++live.stats.cachedPolicyMisses;
        ++live.stats.hashLookupFallbacks;
        if (object) {
            const auto found = live.ids.find(object);
            if (found != live.ids.end() && found->second < live.entries.size() &&
                live.entries[found->second].object.data() == object) {
                recordSectionCommitKnown(found->second, object);
                return CachedMoveSectionResult::native;
            }
        }
        ++live.stats.nativeCommitCallbacks;
        return CachedMoveSectionResult::native;
    }

    if (live.movePolicySceneGeneration != live.scene->generation() ||
        denseIndex >= live.moveSectionPolicies.size() ||
        !(live.moveSectionPolicies[denseIndex] & movePolicyValid)) {
        ++live.stats.cachedPolicyMisses;
        recordSectionCommitKnown(id, object);
        return CachedMoveSectionResult::native;
    }

    ++live.stats.cachedPolicyHits;
    live.stats.structuralPredicatesAvoided += cachedStructuralPredicateCount;
    if (!(live.moveSectionPolicies[denseIndex] & movePolicyStructurallyEligible) || live.entries[id].owned) {
        recordSectionCommitKnown(id, object);
        return CachedMoveSectionResult::native;
    }

    ++live.stats.dynamicSectionChecks;
    if (dynamicMoveSectionEligibility(layer, object))
        return CachedMoveSectionResult::skip;

    recordSectionCommitKnown(id, object);
    return CachedMoveSectionResult::native;
}
void invalidateMoveSectionPolicy(GameObject* object, MovePolicyInvalidation source) {
    if (state && state->audit && state->ready && object &&
        (source == MovePolicyInvalidation::objectGroupAdd ||
         source == MovePolicyInvalidation::objectGroupRemove ||
         source == MovePolicyInvalidation::objectReset || source == MovePolicyInvalidation::retirement)) {
        const auto found = state->ids.find(object);
        if (found != state->ids.end())
            for (auto [routeIndex, slot] : state->entries[found->second].memberships)
                state->routes[routeIndex].auditMembershipChanged = true;
    }
    if (!state || !state->cachedPoliciesEnabled) return;
    auto& live = *state;
    ++live.stats.cacheInvalidations;
    ++live.stats.cacheInvalidationsBySource[static_cast<std::size_t>(source)];
    if (live.ready) ++live.stats.cacheInvalidationsAfterReady;
    live.recentMoveArray = nullptr;
    live.recentMoveRoute = std::numeric_limits<std::size_t>::max();
    live.recentMoveSceneGeneration = 0;
    if (!object || live.moveSectionPolicies.empty()) return;
    const auto found = live.ids.find(object);
    if (found == live.ids.end() || found->second >= live.entries.size() ||
        live.entries[found->second].object.data() != object) return;
    const auto denseIndex = static_cast<std::int64_t>(object->m_uniqueID) - live.denseIdBase;
    if (denseIndex < 0 || static_cast<std::size_t>(denseIndex) >= live.denseIds.size() ||
        live.denseIds[denseIndex] != found->second) return;
    live.moveSectionPolicies[denseIndex] = 0;
}
void invalidateAllMoveSectionPolicies(MovePolicyInvalidation source) {
    if (!state || !state->cachedPoliciesEnabled) return;
    auto& live = *state;
    ++live.stats.cacheInvalidations;
    ++live.stats.cacheInvalidationsBySource[static_cast<std::size_t>(source)];
    if (live.ready) ++live.stats.cacheInvalidationsAfterReady;
    live.recentMoveArray = nullptr;
    live.recentMoveRoute = std::numeric_limits<std::size_t>::max();
    live.recentMoveSceneGeneration = 0;
    std::fill(live.moveSectionPolicies.begin(), live.moveSectionPolicies.end(), 0);
}
void beforeObjectRetirement(GameObject* object) {
    if (!object) return;
    release(object, "native_object_retirement");
    invalidateMoveSectionPolicy(object, MovePolicyInvalidation::retirement);
}
namespace {
constexpr ObjectId invalidObjectId = std::numeric_limits<ObjectId>::max();

ObjectId hashObjectId(const Live& live, CCSprite* sprite) {
    const auto found = live.ids.find(sprite);
    return found == live.ids.end() ? invalidObjectId : found->second;
}

ObjectId ownedLookupId(Live& live, GameObject* object) {
    if (!object) return invalidObjectId;
    const auto index = static_cast<std::int64_t>(object->m_uniqueID) - live.denseIdBase;
    if (index >= 0 && static_cast<std::size_t>(index) < live.denseIds.size()) {
        const auto id = live.denseIds[index];
        if (id < live.entries.size() && live.entries[id].object.data() == object) {
            if (live.validate && (live.stats.denseOwnershipCalls++ & 1023) == 0) {
                ++live.stats.denseOwnershipSamples;
                if (hashObjectId(live, object) != id) {
                    ++live.stats.denseOwnershipMismatches;
                    return hashObjectId(live, object);
                }
            }
            return id;
        }
    }
    return hashObjectId(live, object);
}
}
bool owns(CCSprite* sprite) {
    if (!state || state->internal || !state->ready || !state->owned()) return false;
    // Plain Cocos sprites have no GD object ID.
    const auto id = hashObjectId(*state, sprite);
    return id != invalidObjectId && state->entries[id].owned;
}

bool owns(GameObject* object) {
    if (!object || !state || state->internal || !state->ready || !state->owned()) return false;
    const auto id = ownedLookupId(*state, object);
    return id != invalidObjectId && state->entries[id].owned;
}

bool nativeCallersRequested() { return envFlags().nativeCallers; }
bool revocationAuditRequested() { return envFlags().revocationAudit; }
bool positionSetterAuditRequested() { return envFlags().setterAudit || (envFlags().hyperAggressive && envFlags().performanceOnly); }
PositionSetterAuditToken beginPositionSetterAudit(GameObject* object, float x, float y, const void* anchor) {
    const bool perf = envFlags().performanceOnly;
    if (perf && (!state || !state->hyperVisibilityActive || object->m_groupCount != 1 || !setterShadowMember(object, 554))) return {};
    if (!state || state->testCompleted || !state->aggressive || !state->residentEnabled || !owns(object)) return {};
    auto& live = *state;
    const auto started = perf ? Clock::time_point{} : Clock::now();
    const auto id = ownedLookupId(live, object);
    SetterEvent event;
    event.id = id; event.step = live.ticks; event.frame = live.frames;
    event.inputX = setter_audit::bits(x); event.inputY = setter_audit::bits(y);
    event.cursorFlags = setterCursorFlags(live, id);
    if (perf && event.cursorFlags != 8u) return {};
    event.entry = setterSnapshot(object, &event.groupMask);
    event.visibility = perf ? live.hyperVisibilityActive : inVisibilityScope();
    if (!perf) {
        if (auto* scope = spike::Scope::current) event.phase = spike::phaseNames[static_cast<std::size_t>(scope->phase)];
        const auto caller = revocationCaller(anchor);
        event.provenance = caller.provenance; event.module = caller.module; event.caller = caller.rva; event.parent = caller.parent;
    }
    const auto index = live.setterEvents.size();
    live.setterEvents.push_back(event);
    if (!perf) live.setterAuditMs += std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    return {&live, index};
}
void beforePositionSetterNative(PositionSetterAuditToken token, GameObject* object, float x, float y) {
    if (!state || state.get() != token.scene || token.index >= state->setterEvents.size()) return;
    const auto started = envFlags().performanceOnly ? Clock::time_point{} : Clock::now();
    auto& event = state->setterEvents[token.index];
    if (state->entries[event.id].object.data() != object) return;
    event.before = setterSnapshot(object);
    event.nativeX = setter_audit::bits(x); event.nativeY = setter_audit::bits(y);
    event.beforeCaptured = true;
    if (!envFlags().performanceOnly) state->setterAuditMs += std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}
bool retainPositionSpriteSync(PositionSetterAuditToken token, GameObject* object, float x, float y) {
    const auto& flags = envFlags();
    if (!flags.setterShadowGroup || flags.setterShadowNative || !state ||
        state.get() != token.scene || token.index >= state->setterEvents.size()) return false;
    auto& live = *state;
    const auto id = live.setterEvents[token.index].id;
    auto& entry = live.entries[id];
    if (!entry.owned || entry.object.data() != object ||
        !setterShadowMember(object, flags.setterShadowGroup)) return false;
    auto reject = [&](const char* reason) {
        ++live.setterShadowRejected[reason];
        return false;
    };
    if (!live.validate || live.shadowDisabled || !live.groupRotation || live.internal ||
        entry.nativeEdit || entry.shadowSetterInFlight || live.flushingResidents)
        return reject("inactive_or_reentrant");
    if (!inVisibilityScope()) return reject("outside_visibility");
    if (*aggressiveRejection(object)) return reject("no_longer_eligible");
    const auto current = setterSnapshot(object);
    const auto cursor = setterCursorFlags(live, id);
    ++live.shadowReferenceChecks;
    const bool referenceMatches = current.logical == std::array{
        setter_audit::bits(entry.reference.x), setter_audit::bits(entry.reference.y)};
    if (!referenceMatches) {
        ++live.shadowReferenceMismatch;
        live.shadowDisabled = true;
        return reject("logical_reference_mismatch");
    }
    if (!setter_audit::shadowEligible(current, cursor, setter_audit::bits(x), setter_audit::bits(y)))
        return reject("cursor_or_input_guard");
    // Preserve what handoff did to the native sprite, without changing ownership,
    // routes or GPU position. Do not skip the actual GameObject setter.
    entry.shadowSetterInFlight = true;
    live.syncResident(id, "shadow_sprite_sync", true);
    const auto synced = setterSnapshot(object);
    if (!entry.owned || synced.logical != current.logical ||
        synced.sprite != std::array{setter_audit::bits(x), setter_audit::bits(y)} ||
        setterCursorFlags(live, id)) {
        entry.shadowSetterInFlight = false;
        return reject("sprite_sync_guard");
    }
    // Keep the stock batch update when native geometry is authoritative.
    // The ordinary prepareSpriteTransform hook suppresses only GPU atlas quads.
    if (object->getBatchNode()) object->updateTransform();
    const auto prepared = setterSnapshot(object);
    if (!entry.owned || setterCursorFlags(live, id) ||
        (setter_audit::changes(current, prepared) & ~setter_audit::sprite) ||
        prepared.sprite != std::array{setter_audit::bits(x), setter_audit::bits(y)}) {
        entry.shadowSetterInFlight = false;
        return reject("batch_update_guard");
    }
    auto& event = live.setterEvents[token.index];
    event.shadowAccepted = true;
    event.shadowRepeat = entry.shadowRetentions != 0;
    return true;
}
// The stock visibility setter is a sprite mirror, not the logical writer.
// g554 has an uncovered native absolute position path. Accept its exact
// observed result only when every journal cursor is current and the native
// call itself is demonstrably a sprite-only synchronization.
bool hyperRebaseNativeSetter(Live& live, const SetterEvent& event) {
    auto& entry = live.entries[event.id];
    auto* object = entry.object.data();
    if (!envFlags().hyperAggressive || !event.retained || !event.visibility ||
        event.cursorFlags != 8u || setterCursorFlags(live, event.id) != 8u ||
        object->m_groupCount != 1 || !setterShadowMember(object, 554) ||
        entry.nativeEdit || live.flushingResidents ||
        (setter_audit::changes(event.entry, event.before) & ~setter_audit::sprite) ||
        (setter_audit::changes(event.before, event.after) & ~setter_audit::sprite) ||
        event.after.offsets[0] || event.after.offsets[1] ||
        !setter_audit::matchesRoundedLogical(event.after, event.nativeX, event.nativeY) ||
        event.after.sprite != std::array{event.nativeX, event.nativeY}) return false;
    const Translation result{object->m_positionX, object->m_positionY};
    if (std::abs(result.x) > nativeSectionLimit || std::abs(result.y) > nativeSectionLimit ||
        !std::isfinite(result.x) || !std::isfinite(result.y)) return false;
    const auto chainId = live.scene->chainFor(live.scene->handle(event.id));
    auto& chain = live.chains[chainId];
    const auto offset = live.scene->chainTranslation(chainId);
    live.forgetSections(event.id, chainId);
    entry.sectionOrigin = {result.x - offset.x, result.y - offset.y};
    chain.precision.coordinateMagnitude = std::max({chain.precision.coordinateMagnitude,
        std::abs(result.x), std::abs(result.y), std::abs(entry.sectionOrigin.x), std::abs(entry.sectionOrigin.y)});
    chain.precision.accumulatedError = std::max(chain.precision.accumulatedError,
        8 * std::numeric_limits<double>::epsilon() *
        std::max({std::abs(result.x), std::abs(result.y), std::abs(offset.x), std::abs(offset.y)}));
    live.indexSections(event.id, chainId);
    entry.historyBase = entry.reference = result;
    live.filterCacheAll(event.id);
    entry.initialPrevious = {object->m_lastPosition.x, object->m_lastPosition.y};
    entry.residentStamp = static_cast<std::uint32_t>(object->m_unk4C4);
    entry.residentApplied = entry.spriteApplied = chain.lastResidentAction;
    entry.pendingRebase = live.residentRebases.size();
    live.residentRebases.push_back({result.x, result.y, residentRebaseBit | event.id, 0, 0});
    live.residentValidatedActions = std::numeric_limits<std::size_t>::max();
    ++live.retainedPositionRebases;
    ++live.hyperRebases;
    if (live.hyperRebaseObjects.insert(event.id).second && !envFlags().performanceOnly)
        log::info("VULKANIZER_HYPER native rebase group=554 object={} step={} caller={} parent={}",
            event.id, event.step, event.caller, event.parent);
    if (live.hyperPreVisibilityDiverged.contains(event.id)) ++live.hyperRebaseBeforeVisibility;
    else ++live.hyperRebaseDuringVisibility;
    return true;
}
void endPositionSetterAudit(PositionSetterAuditToken token, GameObject* object) {
    if (!state || state.get() != token.scene || token.index >= state->setterEvents.size()) return;
    const auto started = envFlags().performanceOnly ? Clock::time_point{} : Clock::now();
    auto& event = state->setterEvents[token.index];
    if (!event.beforeCaptured || state->entries[event.id].object.data() != object) return;
    event.after = setterSnapshot(object);
    event.retained = state->entries[event.id].owned;
    event.completed = true;
    if (envFlags().performanceOnly) {
        if (!hyperRebaseNativeSetter(*state, event)) ++state->hyperPerfRebaseRejected;
        // In the performance run this vector contains only transient rebase
        // guard snapshots, never a full setter trace.
        return;
    }
    if (envFlags().hyperAggressive && event.retained && !event.shadowAccepted) {
        auto& live = *state;
        const auto& entry = live.entries[event.id];
        ++live.hyperSetterChecks;
        hyperRebaseNativeSetter(live, event);
        const auto changes = setter_audit::changes(event.before, event.after);
        const auto expected = std::array{setter_audit::bits(entry.reference.x), setter_audit::bits(entry.reference.y)};
        if ((changes & ~setter_audit::sprite) || event.after.logical != expected ||
            event.after.sprite != std::array{event.nativeX, event.nativeY}) {
            ++live.hyperSetterFailures;
            if (event.after.logical != expected) {
                const bool x = event.after.logical[0] != expected[0];
                hyperFailure(live, event.id, "native_position_setter", x ? "logical_x" : "logical_y", expected[x ? 0 : 1], event.after.logical[x ? 0 : 1]);
            } else if (changes & ~setter_audit::sprite) {
                hyperFailure(live, event.id, "native_position_setter", "change_mask", 0, changes & ~setter_audit::sprite);
            } else {
                const bool x = event.after.sprite[0] != event.nativeX;
                hyperFailure(live, event.id, "native_position_setter", x ? "sprite_x" : "sprite_y",
                    x ? event.nativeX : event.nativeY, event.after.sprite[x ? 0 : 1]);
            }
        }
    }
    if (event.shadowAccepted) {
        auto& live = *state;
        auto& entry = live.entries[event.id];
        entry.shadowSetterInFlight = false;
        const bool valid = entry.owned && !setterCursorFlags(live, event.id) &&
            setter_audit::shadowSetterEquivalent(event.before, event.after, event.nativeX, event.nativeY) &&
            event.after.logical == std::array{setter_audit::bits(entry.reference.x), setter_audit::bits(entry.reference.y)};
        if (valid) {
            ++live.shadowRetained;
            live.shadowRepeat += event.shadowRepeat;
            live.shadowLaterStep += event.shadowRepeat && event.step > entry.shadowLastStep;
            if (!entry.shadowRetentions) entry.shadowFirstStep = event.step;
            entry.shadowLastStep = event.step;
            live.shadowUnique += entry.shadowRetentions++ == 0;
        } else {
            ++live.shadowPostFailures;
            live.shadowDisabled = true;
            // We entered with a fully current journal and sprite cursor. Do
            // not try to rebase an unexpected native logical edit into the GPU.
            // The existing handoff keeps the native result authoritative.
            release(object, "setter_shadow_postcondition");
            // A nested native callback may grow setterEvents during handoff.
            live.setterEvents[token.index].retained = entry.owned;
        }
    }
    state->setterAuditMs += std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}
void recordNativeCaller(GameObject* object, NativeCallerKind kind) {
    if (!state || state->testCompleted || !state->aggressive || !owns(object)) return;
    const auto index = static_cast<std::size_t>(kind);
    auto& hist = state->nativeCallerHistograms[index];
    // Deterministic hash sampling avoids aliasing with repeating native loops.
    // This intentionally measures calls, not exact population by call site.
    std::uint64_t hash = ++hist.calls;
    hash = (hash ^ (hash >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    hash = (hash ^ (hash >> 27)) * UINT64_C(0x94d049bb133111eb);
    hash ^= hash >> 31;
    if (hash % (index == 0 ? 8u : 64u) != 0) return;
    ++hist.samples;
    static const auto image = [] {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        return std::pair{base, static_cast<std::uintptr_t>(nt->OptionalHeader.SizeOfImage)};
    }();
    void* frames[32]{};
    const auto count = CaptureStackBackTrace(0, 32, frames, nullptr);
    std::uintptr_t native[2]{};
    unsigned found = 0;
    for (unsigned i = 0; i < count && found < 2; ++i) {
        const auto pc = reinterpret_cast<std::uintptr_t>(frames[i]);
        if (pc >= image.first && pc - image.first < image.second)
            native[found++] = pc - image.first;
    }
    ++hist.callers[{native[0], native[1]}];
}

bool ownsHeadlessMaterial(GameObject* object) {
    if (!object || !envFlags().headlessMaterial || !owns(object)) return false;
    auto found = state->ids.find(object);
    return found != state->ids.end() && state->entries[found->second].headless && !object->getBatchNode() && !object->getTextureAtlas();
}
void beforeHistoryRead(GameObject* object) {
    if (state && state->aggressive) {
        beforeResidentRead(object, "native_rectangle_history_read");
        return;
    }
    if (object && !object->m_isDecoration2 && owns(object))
        release(object, "native_rectangle_history_read");
    else beforeResidentRead(object, "resident_rectangle_read");
}
void beforeResidentRead(GameObject* object, const char* reason) {
    if (!state || !state->residentEnabled || state->residentJournal.empty() ||
        state->internal || !state->ready || !state->owned()) return;
    const auto id = ownedLookupId(*state, object);
    if (id == invalidObjectId || !state->entries[id].owned) return;
    if (state->aggressive) {
        if (envFlags().setterAudit && !state->testCompleted && inVisibilityScope()) {
            auto& count = state->visibilityReads[reason];
            const auto flags = setterCursorFlags(*state, id);
            ++count.calls; count.pending += (flags & 1) != 0; count.stale += (flags & 2) != 0;
            count.synchronized += state->syncResident(id, reason);
        } else state->syncResident(id, reason);
        return;
    }
    const auto handle = state->scene->handle(id);
    const auto& chain = state->chains[state->scene->chainFor(handle)];
    if (std::any_of(chain.residentGroups.begin(), chain.residentGroups.end(), [&](auto group) {
        return !state->routes[group - 1].residentActions.empty();
    })) handoff(std::span<const Handle>(&handle, 1), reason);
}
bool retainVisibility(GameObject* object, bool visible) {
    if (!object || (!controlledToggleVisibility() && !(state && state->residentEnabled)) || !owns(object)) return false;
    auto found = state->ids.find(object);
    if (found == state->ids.end()) return false;
    auto& entry = state->entries[found->second];
    if (state->aggressive) {
        if (visible) state->filterActivated(found->second);
        if (entry.atlas) {
            auto& atlas = state->atlases.at(entry.atlas.data());
            if (!atlas.dirty) atlas.plan.setVisible(entry.slot, visible);
        } else if (visible && !entry.nativeMaterial && object->getTextureAtlas()) {
            atlasMutation(object->getTextureAtlas(), "visible_atlas_member");
        }
        return true;
    }
    if (!entry.atlas) return false;
    try { return state->atlases.at(entry.atlas.data()).plan.setVisible(entry.slot, visible); }
    catch (...) { release(object, "visibility_plan_failure"); return false; }
}
bool retainActivation(GameObject* object) {
    if (state && state->aggressive && owns(object)) {
        state->syncResident(ownedLookupId(*state, object), "native_activation", true);
        return true;
    }
    return object && controlledToggleVisibility() && owns(object);
}
bool prepareSpriteTransform(CCSprite* sprite) {
    if (!state || !state->aggressive) return owns(sprite);
    if (!state->ready || state->internal || !state->owned()) return false;
    const auto id = hashObjectId(*state, sprite);
    if (id == invalidObjectId || !state->entries[id].owned) return false;
    auto& entry = state->entries[id];
    // Native rotation may explicitly request an updated native quad.
    if (entry.nativeEdit) return false;
    if (entry.atlas && !state->atlases.at(entry.atlas.data()).dirty) return true;
    if (sprite->isVisible()) state->syncResident(id, "native_sprite_transform", true);
    return false;
}
void beforeNodeVisit(CCNode* node) {
    if (!state || !state->aggressive || !state->ready || state->internal || !state->owned() ||
        !node->isVisible()) return;
    // GameObject uses CCNode as its primary base. Lookup uses pointer identity
    // only and never dereferences the key as a CCSprite.
    const auto id = hashObjectId(*state, reinterpret_cast<CCSprite*>(node));
    if (id == invalidObjectId || !state->entries[id].owned || state->entries[id].object->getBatchNode()) return;
    state->syncResident(id, "native_unbatched_draw", true);
}
bool nativeMaterialChanged(CCSprite* sprite, const char* reason) {
    if (!state || !state->aggressive || !state->ready || state->internal || !state->owned()) return false;
    const auto id = hashObjectId(*state, sprite);
    if (id == invalidObjectId || !state->entries[id].owned) return false;
    auto& entry = state->entries[id];
    if (envFlags().visibilityFilter && state->visibilityDeferred.indexed(id))
        state->syncResident(id, reason);
    if (std::string_view(reason) == "quad_material_mutation") state->filterQueueReclassify(id, FilterChange::Quad);
    entry.nativeMaterial = true;
    entry.residentQuad = false;
    const bool wasResident = entry.atlas.data() != nullptr;
    if (wasResident) {
        auto& atlas = state->atlases.at(entry.atlas.data());
        atlas.plan.release(entry.slot);
        entry.atlas = nullptr;
        atlas.nativeResidents.push_back(id);
    }
    if (sprite->isVisible()) state->syncResident(id, reason, true);
    if (sprite->isVisible() && wasResident) {
        state->internal = true;
        sprite->updateTransform();
        state->internal = false;
    }
    return true;
}
void spriteBatchChanged(CCSprite* sprite, CCSpriteBatchNode* oldBatch) {
    if (!state || !state->aggressive || !state->ready || state->internal || !state->owned()) return;
    const auto id = hashObjectId(*state, sprite);
    if (id == invalidObjectId || !state->entries[id].owned) return;
    auto& entry = state->entries[id];
    if (envFlags().visibilityOwnershipShadow)
        state->recordVisibilityOwnershipBatchChange(id, oldBatch, sprite->getBatchNode());
    if (envFlags().visibilityFilter && state->visibilityDeferred.indexed(id))
        state->syncResident(id, "visibility_filter_batch_change");
    state->filterQueueReclassify(id, sprite->getBatchNode() ? FilterChange::BatchAttach : FilterChange::BatchDetach);
    if (oldBatch) atlasMutation(oldBatch->getTextureAtlas(), "sprite_batch_change");
    entry.atlas = nullptr;
    if (auto* batch = sprite->getBatchNode(); batch && sprite->getTextureAtlas()) {
        auto& atlas = state->atlases[sprite->getTextureAtlas()];
        atlas.batch = batch;
        atlas.dirty = true;
    }
}
bool aggressiveActive() {
    return state && state->aggressive && state->ready && !state->internal && state->owned();
}
bool groupRotationActive(GJBaseGameLayer* layer) {
    return state && state->layer == layer && state->ready && state->residentEnabled &&
        state->groupRotation && state->owned() && !state->internal;
}
bool groupRotationClaimActive(GJBaseGameLayer* layer) {
    return groupRotationActive(layer) && state->rotationClaim;
}
bool hyperAggressiveActive() {
    return envFlags().hyperAggressive && aggressiveActive() && state->groupRotation && !state->hyperTearingDown;
}
// Calibrate the actual shipped GD rotateObject once on disposable decorations.
// There is no native call or GL query for an owned gameplay recipient here.
namespace {
bool probePivotlessRotation(Live& live) {
    if (live.pivotlessProbed) return live.pivotlessProbePassed;
    live.pivotlessProbed = true;
    auto* layer = live.layer;
    struct Guard {
        Live& l; bool old; int moved, rotated;
        ~Guard() { l.layer->m_movedCount = moved; l.layer->m_rotatedCount = rotated; l.internal = old; }
    } guard{live, live.internal, layer->m_movedCount, layer->m_rotatedCount};
    live.internal = true;
    for (unsigned cls = 0; cls < 4; ++cls) {
        auto& rule = live.pivotlessRules[cls];
        bool first = true;
        unsigned finishObserved[2]{};
        for (unsigned initialDirty : {0u, 31u, 10u}) for (unsigned finished : {0u, 1u}) {
            for (float delta : {13.25f, -7.625f}) {
                auto* object = GameObject::createWithKey(1757);
                if (!object) return false;
                auto reject = [&](const char* reason, unsigned expected, unsigned actual) {
                    log::error("VULKANIZER_HYPER pivotless probe {} class={} dirty={} finished={} delta={} expected={} actual={}",
                        reason, cls, initialDirty, finished, delta, expected, actual);
                    return false;
                };
                object->m_objectType = GameObjectType::Decoration;
                object->m_isDecoration2 = (cls & 2u) != 0;
                object->m_canRotateFree = (cls & 1u) != 0;
                object->m_positionX = 10000.125;
                object->m_positionY = 50.375;
                object->setRotationX(17.125f);
                object->setRotationY(-9.25f);
                object->m_rotationXOffset = 3.5f;
                object->m_rotationYOffset = -2.75f;
                object->m_unk4C4 = 123;
                object->m_unk4fb = finished != 0;
                setRotationDirtyBits(object, initialDirty);
                const auto before = setterSnapshot(object);
                layer->rotateObject(object, delta);
                const auto after = setterSnapshot(object);
                ++live.hyperPivotlessChecks;
                const auto changes = setter_audit::changes(before, after);
                // No translation/history/visibility/membership or dirty-bit clearing is allowed.
                if (changes & ~(setter_audit::rotation | setter_audit::dirty))
                    return reject("unexpected fields", 0, changes & ~(setter_audit::rotation | setter_audit::dirty));
                if (before.dirty & ~after.dirty) return reject("cleared dirty bits", before.dirty, after.dirty);
                unsigned mask = 0;
                for (unsigned i = 0; i < 4; ++i) {
                    if (before.rotation[i] == after.rotation[i]) continue;
                    const float expected = std::bit_cast<float>(before.rotation[i]) + delta;
                    if (setter_audit::bits(expected) != after.rotation[i])
                        return reject("non-additive angle/offset", setter_audit::bits(expected), after.rotation[i]);
                    mask |= 1u << i;
                }
                if (first) { rule.rotationMask = mask; rule.dirtySet = after.dirty; first = false; }
                if (mask != rule.rotationMask) return reject("inconsistent angle mask", rule.rotationMask, mask);
                if (after.dirty != (before.dirty | rule.dirtySet)) return reject("inconsistent dirty mask", before.dirty | rule.dirtySet, after.dirty);
                const unsigned observed = object->m_unk4fb ? 2u : 1u;
                if (finishObserved[finished] && finishObserved[finished] != observed)
                    return reject("inconsistent finish state", finishObserved[finished], observed);
                finishObserved[finished] = observed;
            }
        }
        if (finishObserved[0] == finishObserved[1]) rule.finishMode = finishObserved[0];
        else if (finishObserved[0] != 1 || finishObserved[1] != 2) return false;
    }
    // GPU topology encodes the free-rotation bit, not the decoration2 flag.
    for (unsigned i = 0; i < 2; ++i)
        if ((live.pivotlessRules[i].rotationMask & 3u) != (live.pivotlessRules[i + 2].rotationMask & 3u)) return false;
    live.pivotlessProbePassed = true;
    return true;
}
}
void recordPivotlessNativeCalls(std::uint64_t count) {
    if (state) state->pivotlessNativeCalls += count;
}
void setGroupRotationClaim(GJBaseGameLayer* layer, bool active) {
    if (state && state->layer == layer) state->rotationClaim = active;
}
bool activeFor(GJBaseGameLayer* layer) { return state && state->layer == layer && state->ready; }
NativeEditScope::NativeEditScope() {
    if (!aggressiveActive() || !state->persistentEdits || !state->residentEnabled) return;
    token = state.get();
    ++state->nativeEditDepth;
}
NativeEditScope::~NativeEditScope() {
    if (!token || state.get() != token || !state->nativeEditDepth) return;
    auto& live = *state;
    if (--live.nativeEditDepth) return;
    // Move out: a fallback can invoke native hooks during materialization.
    auto edits = std::move(live.nativeEdits);
    live.nativeEdits.clear();
    std::vector<Handle> unsupported;
    for (auto id : edits) {
        auto& entry = live.entries[id];
        if (!entry.nativeEdit) continue;
        const bool keep = !*aggressiveRejection(entry.object.data());
        live.finishNativeEdit(id, keep);
        if (!keep) unsupported.push_back(live.scene->handle(id));
    }
    handoff(unsupported, "retained_edit_became_ineligible");
    // Retain the allocation for the next native phase.
    if (live.nativeEdits.empty()) {
        edits.clear();
        live.nativeEdits.swap(edits);
    }
}
bool retainNativeEdit(GameObject* object) {
    if (!aggressiveActive() || !state->persistentEdits || !state->nativeEditDepth) return false;
    const auto id = ownedLookupId(*state, object);
    if (id == invalidObjectId || !state->entries[id].owned) return false;
    state->beginNativeEdit(id);
    return true;
}
bool retainNativeEditArray(CCArray* objects) {
    if (!aggressiveActive() || !state->persistentEdits || !state->nativeEditDepth) return false;
    if (objects) {
        for (auto* object : CCArrayExt<GameObject, false>(objects)) retainNativeEdit(object);
    }
    return true;
}
void releaseArray(CCArray* objects, const char* reason) {
    if (!state || state->internal || !state->ready || !state->owned() || !objects) return;
    if (state->aggressive) {
        const auto found = state->routeIndices.find(objects);
        if (found != state->routeIndices.end()) {
            const auto& route = state->routes[found->second];
            if (route.valid && route.count == objects->count() && !route.owned) return;
        }
    }
    std::vector<Handle> affected;
    for (auto* object : CCArrayExt<GameObject, false>(objects)) {
        ++state->stats.dependencyGuardVisits;
        auto id = state->ids.find(object);
        if (id != state->ids.end() && state->entries[id->second].owned) affected.push_back(state->scene->handle(id->second));
    }
    handoff(affected, reason);
}
void beginLevel(PlayLayer* layer, double nativeInitMs) {
    const auto& flags = envFlags();
    if (!requested()) return;
    if (state) destroy(state->layer, "level_replacement");
    static bool fixturePrepared = false;
    if (flags.headlessFixture && flags.sharedHistory &&
        flags.test && !flags.levelName) {
        for (auto* object : CCArrayExt<GameObject, false>(layer->m_objects))
            if (object->m_objectType == GameObjectType::Decoration) object->m_isDecoration2 = false;
    }
    if (flags.toggleFixture && flags.test &&
        !flags.levelName && layer->m_objects) {
        unsigned found = 0;
        for (auto* object : CCArrayExt<GameObject, false>(layer->m_objects)) {
            if (object->m_objectID != 1757 || !object->m_groups) continue;
            bool member = false;
            for (short i = 0; i < object->m_groupCount; ++i) member |= (*object->m_groups)[i] == 9998;
            if (!member) continue;
            if (++found == 2) { object->m_isDecoration2 = false; break; }
        }
    }
    if (flags.xlockFixture && flags.test &&
        !flags.levelName && layer->m_objects) {
        for (auto* object : CCArrayExt<GameObject, false>(layer->m_objects)) {
            auto* effect = typeinfo_cast<EffectGameObject*>(object);
            if (effect && object->m_objectID == 901) {
                effect->m_lockToPlayerX = true;
                log::info("VULKANIZER_V2_XLOCK_FIXTURE native_move_trigger_enabled");
                break;
            }
        }
    }
    if (flags.structuralFixture && flags.test &&
        !flags.levelName && !fixturePrepared && layer->m_objects && layer->m_objects->count() >= 64) {
        unsigned i = 0;
        for (auto* object : CCArrayExt<GameObject, false>(layer->m_objects)) {
            if (object->m_objectID != 1757) continue;
            object->m_parentMode = -1;
            switch (i++ % 4) {
                case 0: object->setRotation(27); object->setAnchorPoint({.25f, .75f}); break;
                case 1: object->setScaleX(1.3f); object->setScaleY(.75f); break;
                case 2: object->setSkewX(12); object->setSkewY(-7); break;
                case 3: object->setFlipX(true); object->setFlipY(true); break;
            }
        }
        fixturePrepared = i > 0;
        log::info("VULKANIZER_V2_STRUCTURAL_FIXTURE objects={}", i);
    }
    state = std::make_unique<Live>();
    state->layer = layer;
    state->nativeInitMs = nativeInitMs;
    state->baseline = flags.baseline;
    state->cachedPoliciesEnabled = flags.cachedPolicies && !state->baseline;
    state->validate = flags.validate;
    state->levelName = layer->m_level->m_levelName;
    state->cpuFrames.reserve(4096);
    state->cpuOwnedFrames.reserve(4096);
    // Compile descriptor metadata once from the freshly parsed level before
    // gameplay. Activation time remains a native runtime parameter.
    if (flags.temporalReplay && layer->m_objects) {
        const auto started = Clock::now();
        for (auto* object : CCArrayExt<GameObject, false>(layer->m_objects)) {
            if (!object || !object->m_isTrigger || object->m_isStartPos ||
                (object->m_objectID != 901 && object->m_objectID != 1346)) continue;
            auto* effect = typeinfo_cast<EffectGameObject*>(object);
            if (!effect || effect->m_targetGroupID <= 0) continue;
            const int group = effect->m_targetGroupID;
            auto& d = state->descriptorGroups[group];
            if (object->m_objectID == 901) ++d.moves; else ++d.rotations;
            const bool pivot = effect->m_centerGroupID > 0 || effect->m_rotationTargetID > 0 ||
                effect->m_targetModCenterID > 0;
            const bool dynamic = pivot || effect->m_useMoveTarget || effect->m_isDynamicMode ||
                effect->m_specialTarget || effect->m_targetPlayer1 || effect->m_targetPlayer2 ||
                effect->m_lockToPlayerX || effect->m_lockToPlayerY ||
                effect->m_lockToCameraX || effect->m_lockToCameraY;
            if (dynamic) ++d.dynamicParams; else ++d.staticParams;
            d.pivot += pivot;
            d.touch += effect->m_isTouchTriggered;
            d.spawn += effect->m_isSpawnTriggered;
            if (std::isfinite(effect->m_duration)) d.maxDuration = std::max(d.maxDuration, double(effect->m_duration));
            if (static_cast<std::size_t>(group) < layer->m_groups.size() && layer->m_groups[group])
                d.memberCount = layer->m_groups[group]->count();
        }
        state->descriptorCompileMs = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    }
}
void recordLegacyPrecompile(PlayLayer* layer, double ms) {
    if (state && state->layer == layer) state->legacyPrecompileMs = ms;
}
void release(GameObject* object, const char* reason, const void* callerAnchor) {
    if (!state || state->internal || state->compiling || !state->ready) return;
    if (!state->owned()) return;
    const auto id = ownedLookupId(*state, object);
    if (id == invalidObjectId || !state->entries[id].owned) return;
    const Handle h = state->scene->handle(id);
    handoff(std::span<const Handle>(&h, 1), reason, callerAnchor);
}
void releaseAll(const char* reason) {
    if (!state || state->internal || state->compiling || !state->scene || !state->owned()) return;
    std::vector<Handle> handles;
    for (ObjectId id = 0; id < state->entries.size(); ++id)
        if (state->entries[id].owned) handles.push_back(state->scene->handle(id));
    handoff(handles, reason);
}
void destroy(PlayLayer* layer, const char* reason) {
    if (!state || state->layer != layer) return;
    state->hyperTearingDown = true;
    releaseAll(reason);
    invalidateAllMoveSectionPolicies(MovePolicyInvalidation::sceneDestroy);
    report(reason);
    if (state->renderer && state->context != wglGetCurrentContext()) state->renderer->abandonContext();
    if (state->gpuTimer && state->context != wglGetCurrentContext()) state->gpuTimer->abandonContext();
    if (state->residentTimer && state->context != wglGetCurrentContext()) state->residentTimer->abandonContext();
    state.reset();
}
void beforeVisibility(PlayLayer* layer) {
    if (!state || state->layer != layer || !state->ready || !state->owned()) return;
    if (state->context != wglGetCurrentContext()) {
        releaseAll("context_replacement");
        if (state->renderer) state->renderer->abandonContext();
        if (state->gpuTimer) state->gpuTimer->abandonContext();
        if (state->residentTimer) state->residentTimer->abandonContext();
        state->renderer.reset();
        state->gpuTimer.reset();
        state->residentTimer.reset();
        return;
    }
    if (state->aggressive) {
        if (state->groupRotation && state->sectionPotential) flushResident(*state, "visibility_section_consumer");
        // Shadow-only conservative boundary: direct native field reads are not
        // hooked. Make the selected group's logical mirrors current BEFORE
        // visibility, not after a setter has already consumed the value.
        // Keep sprite writes at their native synchronization point.
        auto& live = *state;
        if (envFlags().hyperAggressive) {
            live.hyperVisibilityActive = true;
            if (!envFlags().performanceOnly) {
                live.hyperPreVisibilityDiverged.clear();
                for (const auto id : live.hyperRebaseWatch)
                    if (live.entries[id].owned && (setterCursorFlags(live, id) & 8u))
                        live.hyperPreVisibilityDiverged.insert(id);
            }
            // Preserve both native visibility boundaries, but only replay
            // members of dirty immutable chains, in the old global ID order.
            live.syncVisibilityMirrors();
            live.auditVisibilityMirrors();
        }
        if (envFlags().setterShadowGroup && !envFlags().setterShadowNative && live.validate) {
            for (const auto id : live.setterShadowTargets) {
                auto& entry = live.entries[id];
                if (!entry.owned) continue;
                const auto cursor = setterCursorFlags(live, id);
                live.shadowBeforeVisibilityStale += (cursor & 2u) != 0;
                live.shadowVisibilitySyncs += live.syncResident(id, "shadow_before_visibility");
                ++live.shadowVisibilityChecks;
                if (setter_audit::bits(double(entry.object->m_positionX)) != setter_audit::bits(entry.reference.x) ||
                    setter_audit::bits(double(entry.object->m_positionY)) != setter_audit::bits(entry.reference.y)) {
                    ++live.shadowVisibilityMismatch;
                    live.shadowDisabled = true;
                    release(entry.object.data(), "setter_shadow_visibility_mismatch");
                }
            }
        }
        return;
    }
    CameraCache cameras;

for (ChainId id = 0; id < state->chains.size(); ++id) {
    const auto& chain = state->chains[id];
    if (!chain.owned) continue;

    ++state->stats.chainCameraChecks;

    if (auto* reason = unsafe(
        id,
        state->scene->chainTranslation(id),
        chain.precision.accumulatedError,
        &cameras
    )) {
        handoffChain(id, reason);
        cameras.clear();
    }
}
}
void afterVisibility(PlayLayer* layer) {
    if (state && state->layer == layer && envFlags().performanceOnly) {
        state->hyperVisibilityActive = false;
        state->setterEvents.clear();
    }
    if (!state || state->layer != layer) return;
    state->finishVisibilityCensus();
    state->auditFilterAfterVisibility();
    if (envFlags().visibilityOwnershipShadow) state->auditVisibilityOwnershipShadow();
    state->visiblePass = true;
    const auto& flags = envFlags();
    if (flags.setterShadowGroup && state->ready && !state->testCompleted) {
        auto& live = *state;
        auto stateSignature = UINT64_C(14695981039346656037);
        if (flags.setterShadowAligned) {
            auto mix = [&](std::uint64_t v) { stateSignature = (stateSignature ^ v) * UINT64_C(1099511628211); };
            auto real = [&](double v) { mix(setter_audit::bits(v)); };
            mix(layer->m_gameState.m_commandIndex);
            real(layer->m_gameState.m_timeWarp);
            real(layer->m_gameState.m_cameraPosition.x); real(layer->m_gameState.m_cameraPosition.y);
            real(layer->m_gameState.m_cameraStepDiff.x); real(layer->m_gameState.m_cameraStepDiff.y);
            real(layer->m_gameState.m_cameraShakeStrength);
            real(layer->m_cameraWidth); real(layer->m_cameraHeight);
            real(layer->m_objectLayer->getPositionX()); real(layer->m_objectLayer->getPositionY());
            real(layer->m_objectLayer->getScaleX()); real(layer->m_objectLayer->getScaleY());
            for (auto* p : {layer->m_player1, layer->m_player2}) {
                mix(p != nullptr);
                if (!p) continue;
                real(p->getPositionX()); real(p->getPositionY()); real(p->getCurrentXVelocity()); real(p->m_yVelocity);
                real(p->m_accelerationOrSpeed); real(p->m_playerSpeed); real(p->m_speedMultiplier); real(p->m_playerSpeedAC);
                mix(bool(p->m_isShip)); mix(bool(p->m_isBird)); mix(bool(p->m_isBall));
                mix(bool(p->m_isDart)); mix(bool(p->m_isRobot)); mix(bool(p->m_isSpider));
                mix(bool(p->m_isUpsideDown)); mix(bool(p->m_isDead));
            }
            if (live.alignedLastSampleStep == live.ticks) ++live.alignedPassOrdinal;
            else { live.alignedLastSampleStep = live.ticks; live.alignedPassOrdinal = 0; }
        }
        // Observe, never materialize, the sample after the stock visibility pass.
        for (const auto id : live.setterShadowSamples) {
            const auto& entry = live.entries[id];
            live.setterShadowFrames.push_back({id, live.frames, live.ticks,
                setter_audit::bits(double(layer->m_gameState.m_levelTime)),
                setter_audit::bits(double(layer->m_player1->getPositionX())),
                setter_audit::bits(double(layer->m_player1->getPositionY())),
                setterSnapshot(entry.object.data()), entry.owned, entry.shadowRetentions != 0,
                entry.shadowRetentions, entry.shadowFirstStep, live.alignedStepSignature, stateSignature, live.alignedPassOrdinal});
        }
    }
    if (!residentAtlasFixture(flags) || !state->ready || state->residentAtlasFixtureMoved ||
        layer->m_gameState.m_levelTime < flags.captureTime.value_or(.12) ||
        layer->m_groups.size() <= 9998 || !layer->m_groups[9998] || !layer->m_groups[9997]) return;
    state->residentAtlasFixtureMoved = true;
    // Exercise ordered GPU positions in an atlas draw before stock visibility
    // queries require the authoritative native rectangles on the next pass.
    layer->moveObjects(layer->m_groups[9998], 0.5, 0, false);
    layer->moveObjects(layer->m_groups[9997], -0.25, 0.25, false);
    layer->moveObjects(layer->m_groups[9998], 0.25, 0, false);
    // This test submits moves after the game's normal sprite sync point.
    // Present the native result in the same capture as the GPU result.
    for (auto& entry : state->entries) {
        if (entry.owned || !entry.batch) continue;
        auto* object = entry.object.data();
        object->cocos2d::CCSprite::setPosition(CCPoint(static_cast<float>(object->m_positionX),
            static_cast<float>(object->m_positionY)));
        object->updateTransform();
    }
}
void beforeDependencies(GJBaseGameLayer* layer) {
    if (!state || state->layer != layer || !state->ready || !state->owned()) return;
    if (state->aggressive) {
        for (const auto& calculation : layer->m_effectManager->m_unkVector708)
            beforeResidentRead(calculation.m_gameObject, "dynamic_move_anchor");
        return;
    }
    for (const auto& calculation : layer->m_effectManager->m_unkVector708) release(calculation.m_gameObject, "dynamic_move_anchor");
    for (auto* action : layer->m_effectManager->m_unkVector5b0) {
        if (!action) continue;
        auto releaseGroup = [&](int group) {
            if (group <= 0 || static_cast<std::size_t>(group) >= layer->m_groups.size()) return;
            auto found = state->routeIndices.find(layer->m_groups[group]);
            if (found == state->routeIndices.end()) return;
            const auto chains = state->routes[found->second].chains;
            for (auto id : chains) handoffChain(id, "runtime_rotation_dependency");
        };
        releaseGroup(action->m_targetGroupID);
        releaseGroup(action->m_centerGroupID);
    }
}
void beforeNativeGroupTransform(GJBaseGameLayer* layer, int group, bool optimized, const char* reason) {
    if (!state || !state->aggressive || !state->ready || state->internal || state->layer != layer) return;
    auto& buffers = optimized ? layer->m_optimizedGroups : layer->m_groups;
    const auto index = static_cast<std::size_t>(std::clamp(group, 0, 9999));
    if (index >= buffers.size()) return;
    if (state->audit && std::string_view(reason) == "native_claimed_rotation_or_transform") {
        auto& live = *state;
        auto& claim = live.auditRotationClaims[{static_cast<int>(index), optimized}];
        claim.record(buffers[index] ? buffers[index]->count() : 0, layer->m_gameState.m_levelTime);
        const auto releasedBefore = live.stats.released;
        const auto previousTarget = live.auditRotationTarget;
        live.auditRotationTarget = static_cast<int>(index);
        if (!retainNativeEditArray(buffers[index])) releaseArray(buffers[index], reason);
        live.auditRotationTarget = previousTarget;
        claim.released += live.stats.released - releasedBefore;
    } else if (state->owned()) {
        if (std::string_view(reason) != "native_claimed_rotation_or_transform" ||
            !retainNativeEditArray(buffers[index])) releaseArray(buffers[index], reason);
    }
}
void beforeNativeFollow(GJBaseGameLayer* layer) {
    if (!aggressiveActive() || state->layer != layer) return;
    // The pinned follow loop mutates the normal group inline, before the
    // section callback can intervene.
    static_assert(offsetof(GJEffectManager, m_unkVector6d8) == 0x7b0);
    for (auto* node : layer->m_effectManager->m_unkVector6d8)
        if (node) beforeNativeGroupTransform(layer, node->getTag(), false, "native_follow_recipient");
}
void beforeMembershipMutation(GJBaseGameLayer* layer, GameObject* object, int group, const char* reason) {
    if (!state || state->layer != layer || !state->ready || state->internal) return;
    if (!state->aggressive) { releaseAll(reason); return; }
    if (envFlags().hyperAggressive) {
        const auto id = ownedLookupId(*state, object);
        if (id != invalidObjectId && state->entries[id].owned)
            state->markMirrorChain(state->scene->chainFor(state->scene->handle(id)));
    }
    if (group > 0) {
        const auto invalid = static_cast<std::size_t>(std::min(group, 9999));
        auto invalidate = [&](const auto& groups) {
            if (invalid >= groups.size()) return;
            const auto found = state->routeIndices.find(groups[invalid]);
            if (found != state->routeIndices.end()) {
                auto& route = state->routes[found->second];
                route.valid = false;
                if (state->audit) route.auditMembershipChanged = true;
            }
        };
        invalidate(layer->m_groups);
        invalidate(layer->m_staticGroups);
        invalidate(layer->m_optimizedGroups);
    }
    release(object, reason);
}
void beforeSectionRegistration(GJBaseGameLayer* layer, GameObject* object) {
    if (!groupRotationActive(layer) || !state->sectionPotential) return;
    const auto id = ownedLookupId(*state, object);
    if (id != invalidObjectId && state->entries[id].owned) flushResident(*state, "native_section_registration");
}
void afterSectionRegistration(GJBaseGameLayer* layer, GameObject* object) {
    if (!aggressiveActive() || state->layer != layer) return;
    const auto id = ownedLookupId(*state, object);
    if (id == invalidObjectId || !state->entries[id].owned) return;
    if (state->visibilityOwnership.initialized) {
        ++state->visibilityOwnership.ownedSectionRegistrations;
        state->updateVisibilityOwnershipIndex(id);
    }
    if (state->groupRotation) {
        // This is an actual registration event, not a periodic object check.
        state->renderer->updateResidentSection(id, object->m_outerSectionIndex, object->m_middleSectionIndex);
        state->filterSectionChanged(id);
        return;
    }
    if (state->entries[id].nativeEdit) return; // Finalize against the rebased origin.
    const auto chain = state->scene->chainFor(state->scene->handle(id));
    state->forgetSections(id, chain);
    state->indexSections(id, chain);
}
void beforeToggle(GJBaseGameLayer* layer, int group) {
    if (!state || state->layer != layer || !state->ready || state->internal || !state->owned() || controlledToggleVisibility()) return;
    if (state->aggressive) return;
    // GD clamps negative IDs to 0 and IDs above 9999 to 9999. Compilation
    // prepares every positive membership array; an absent array has no owned
    // members. Membership changes already force a handoff before mutation.
    if (group <= 0) return;
    const auto normalized = static_cast<std::size_t>(std::min(group, 9999));
    if (normalized >= layer->m_groups.size()) { releaseAll("unresolved_native_toggle_group"); return; }
    auto* objects = layer->m_groups[normalized];
    if (!objects) return;
    releaseArray(objects, "native_toggle_dependency");
}
void atlasMutation(CCTextureAtlas* atlas, const char* reason) {
    if (!state || state->internal || state->compiling || !state->ready || !state->owned()) return;
    auto found = state->atlases.find(atlas);
    if (found == state->atlases.end()) return;
    if (state->aggressive) {
        auto& entry = found->second;
        entry.dirty = true;
        if (std::string_view(reason) == "batch_texture_change") {
            for (auto id : entry.objects) state->entries[id].residentQuad = false;
        }
        return;
    }
    std::vector<Handle> handles;
    for (auto id : found->second.objects) if (state->entries[id].owned) handles.push_back(state->scene->handle(id));
    handoff(handles, reason);
}
bool quadMutation(CCTextureAtlas* atlas, unsigned slot, const void* quad) {
    if (!state || state->internal || state->compiling || !state->ready || !state->owned()) return false;
    auto found = state->atlases.find(atlas);
    if (found == state->atlases.end()) return false;
    auto* descendants = found->second.batch->getDescendants();
    if (slot >= descendants->count()) return false;
    auto* object = typeinfo_cast<GameObject*>(descendants->objectAtIndex(slot));
    if (!object || !owns(object)) return false;
    if (slot < atlas->getTotalQuads() && std::memcmp(&atlas->getQuads()[slot], quad, sizeof(ccV3F_C4B_T2F_Quad)) == 0) return false;
    if (state->aggressive) {
        const auto id = ownedLookupId(*state, object);
        if (state->entries[id].nativeEdit) {
            // Native updateTransform supplied real vertices, not a stale
            // material-only quad. Keep the write and refresh the resident
            // geometry at draw time.
            state->entries[id].residentQuad = false;
            found->second.dirty = true;
            return false;
        }
        if (!state->entries[id].atlas || found->second.dirty) return false;
        nativeMaterialChanged(object, "quad_material_mutation");
        return true;
    }
    release(object, "quad_material_mutation");
    return true;
}
namespace {
using SectionCandidate = std::pair<unsigned, ObjectId>;

void refreshNativeRoute(Live& live, std::size_t routeIndex) {
    spike::Scope timing(spike::Phase::routeRefresh);
    auto& route = live.routes[routeIndex];
    const auto auditStarted = live.audit ? Clock::now() : Clock::time_point{};
    if (live.audit) {
        ++live.auditTimes.refreshes;
        live.auditTimes.refreshSlots += route.source->count();
        if (route.count != route.source->count()) route.auditMembershipChanged = true;
    }
    route.valid = false;
    std::unordered_set<ObjectId> present;
    std::vector<Handle> changed;
    for (auto* object : CCArrayExt<GameObject, false>(route.source)) {
        const auto id = ownedLookupId(live, object);
        if (id == invalidObjectId || !live.entries[id].owned) continue;
        const auto& membership = live.entries[id].memberships;
        const auto found = std::lower_bound(membership.begin(), membership.end(), routeIndex,
            [](const auto& item, std::size_t route) { return item.first < route; });
        if (!present.insert(id).second || found == membership.end() || found->first != routeIndex)
            changed.push_back(live.scene->handle(id));
    }
    for (auto id : route.members)
        if (live.entries[id].owned && !present.contains(id)) changed.push_back(live.scene->handle(id));
    std::sort(changed.begin(), changed.end(), [](auto a, auto b) { return a.object < b.object; });
    changed.erase(std::unique(changed.begin(), changed.end(), [](auto a, auto b) {
        return a.object == b.object;
    }), changed.end());
    if (live.audit && !changed.empty()) route.auditMembershipChanged = true;
    if (envFlags().hyperAggressive)
        for (auto h : changed) live.markMirrorChain(live.scene->chainFor(h));
    handoff(changed, "native_route_membership_changed");

    route.native = CCArray::createWithCapacity(route.source->count());
    route.nativeSlots.clear();
    route.members.clear();
    route.chains.clear();
    route.owned = route.ownedHeadless = 0;
    unsigned slot = 0;
    for (auto* object : CCArrayExt<GameObject, false>(route.source)) {
        const auto id = ownedLookupId(live, object);
        if (id != invalidObjectId && live.entries[id].owned) {
            auto& entry = live.entries[id];
            auto member = std::lower_bound(entry.memberships.begin(), entry.memberships.end(), routeIndex,
                [](const auto& item, std::size_t route) { return item.first < route; });
            member->second = slot;
            route.members.push_back(id);
            route.chains.push_back(live.scene->chainFor(live.scene->handle(id)));
            ++route.owned;
            if (entry.headless) ++route.ownedHeadless;
        } else {
            route.native->addObject(object);
            route.nativeSlots.push_back(slot);
        }
        ++slot;
    }
    std::sort(route.chains.begin(), route.chains.end());
    route.chains.erase(std::unique(route.chains.begin(), route.chains.end()), route.chains.end());
    route.count = slot;
    route.valid = true;
    if (live.audit) live.auditTimes.refreshMs +=
        std::chrono::duration<double, std::milli>(Clock::now() - auditStarted).count();
}

void collectSectionCandidates(Live& live, std::size_t routeIndex, std::vector<SectionCandidate>& candidates) {
    const auto& route = live.routes[routeIndex];
    if (live.layer->m_startOptimization) return; // PlayLayer skips decoration section commits here.
    for (auto chainId : route.chains) {
        const auto offset = live.scene->chainTranslation(chainId);
        const auto error = live.chains[chainId].precision.accumulatedError;
        const auto& index = live.sectionIndices[chainId];
        const double edges[]{offset.x - error, offset.x + error, offset.y - error, offset.y + error};
        for (unsigned axis = 0; axis < index.size(); ++axis) {
            const auto& edge = index[axis];
            const bool lower = axis % 2 == 0;
            if (edge.empty() || (lower ? edge.rbegin()->first <= edges[axis] : edge.begin()->first > edges[axis]))
                continue;
            const auto cut = edge.upper_bound({edges[axis], std::numeric_limits<ObjectId>::max()});
            for (auto candidate = lower ? cut : edge.begin(); candidate != (lower ? edge.end() : cut); ++candidate) {
                const auto id = candidate->second;
                const auto& membership = live.entries[id].memberships;
                const auto source = std::lower_bound(membership.begin(), membership.end(), routeIndex,
                    [](const auto& item, std::size_t route) { return item.first < route; });
                if (source != membership.end() && source->first == routeIndex)
                    candidates.emplace_back(source->second, id);
            }
        }
    }
    if (candidates.empty()) return;
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    live.residentSectionCandidates += candidates.size();
    std::erase_if(candidates, [&](const SectionCandidate& candidate) {
        const auto id = candidate.second;
        const auto& entry = live.entries[id];
        const auto position = live.replayResident(id).position;
        // Same clamp and promotions as the pinned PlayLayer section callback.
        auto section = [](double value, float factor) {
            if (value <= 0) return 0;
            if (value >= nativeSectionLimit) return static_cast<int>(nativeSectionLimit * factor);
            return static_cast<int>(value * factor);
        };
        return entry.object->m_outerSectionIndex == section(position.x, live.sectionXFactor) &&
            entry.object->m_middleSectionIndex == section(position.y, live.sectionYFactor);
    });
}
void commitResidentSection(Live& live, ObjectId id) {
    const auto chain = live.scene->chainFor(live.scene->handle(id));
    live.syncResident(id, "section_transition");
    live.internal = true;
    live.layer->updateObjectSection(live.entries[id].object.data());
    live.internal = false;
    live.forgetSections(id, chain);
    live.indexSections(id, chain);
    ++live.residentSectionCommits;
}
void refreshResidentAtlas(Live& live, CCTextureAtlas* texture, Atlas& atlas) {
    if (!atlas.dirty) return;
    for (auto id : atlas.objects)
        if (live.entries[id].atlas.data() == texture) live.entries[id].atlas = nullptr;
    atlas.plan = {};
    atlas.objects.clear();
    atlas.nativeResidents.clear();
    auto* batch = atlas.batch.data();
    if (batch->getTextureAtlas() == texture) {
        auto* descendants = batch->getDescendants();
        const auto count = std::min(descendants->count(), static_cast<unsigned>(texture->getTotalQuads()));
        for (unsigned slot = 0; slot < count; ++slot) {
            auto* sprite = static_cast<CCSprite*>(descendants->objectAtIndex(slot));
            const auto id = hashObjectId(live, sprite);
            if (id == invalidObjectId || !live.entries[id].owned) continue;
            auto& entry = live.entries[id];
            auto* object = entry.object.data();
            if (object->getBatchNode() != batch) continue;
            if (entry.nativeMaterial || !residentAtlasCompatible(object, batch)) {
                atlas.nativeResidents.push_back(id);
                continue;
            }
            const auto draw = live.scene->drawFor(live.scene->handle(id));
            if (!entry.residentQuad || entry.batch.data() != batch) {
                if (!object->isVisible()) { atlas.nativeResidents.push_back(id); continue; }
                live.internal = true;
                live.syncResident(id, "native_atlas_attachment", true);
                object->updateTransform();
                live.internal = false;
                const auto quad = residentQuad(object);
                if (!quad) { atlas.nativeResidents.push_back(id); continue; }
                live.renderer->updateResidentQuad(draw, *quad, {object->m_positionX, object->m_positionY},
                    live.groupRotation ? object->getRotationX() : 0.f,
                    live.groupRotation ? object->getRotationY() : 0.f);
                entry.residentQuad = true;
            }
            entry.batch = batch;
            entry.atlas = texture;
            entry.slot = slot;
            atlas.objects.push_back(id);
            atlas.plan.insert(slot, draw, object->isVisible());
        }
    }
    atlas.dirty = false;
}
}
cocos2d::CCArray* submitPivotlessRotation(GJBaseGameLayer* layer, CCArray* objects, float orientation, int group) {
    if (!hyperAggressiveActive() || !groupRotationActive(layer) || !objects || !objects->count()) return objects;
    auto& live = *state;
    if (live.pivotlessAbort) return CCArray::create();
    auto unsupported = [&](const char* reason) {
        ++live.hyperPivotlessFailures;
        live.pivotlessAbort = true;
        log::error("VULKANIZER_HYPER pivotless command rejected: {} group={} step={}", reason, group, live.ticks);
        // Never substitute the old per-recipient upload bridge. The experimental
        // run terminates at frame end and does not claim valid gameplay.
        return CCArray::create();
    };
    const auto found = live.routeIndices.find(objects);
    if (found == live.routeIndices.end()) {
        // Unknown runtime arrays can legitimately be entirely native.
        for (auto* object : CCArrayExt<GameObject, false>(objects)) {
            const auto id = ownedLookupId(live, object);
            if (id != invalidObjectId && live.entries[id].owned) return unsupported("uncompiled owned route");
        }
        live.groupRotationNativeRecipients += objects->count();
        return objects;
    }
    auto& route = live.routes[found->second];
    if (!route.valid || objects->count() != route.count) refreshNativeRoute(live, found->second);
    if (!route.owned) { live.groupRotationNativeRecipients += objects->count(); return objects; }
    if (!std::isfinite(orientation)) return unsupported("nonfinite angle");
    if (!probePivotlessRotation(live)) return unsupported("native angle-only parity probe");
    gpu::ResidentTranslation command{0, 0, static_cast<std::uint32_t>(found->second),
        static_cast<std::uint32_t>(layer->m_gameState.m_commandIndex),
        ((live.pivotlessRules[0].rotationMask & 3u) << gpu::residentPivotlessLockedShift) |
        ((live.pivotlessRules[1].rotationMask & 3u) << gpu::residentPivotlessFreeShift)};
    command.kind = 2;
    command.orientation = orientation;
    if (!queueResidentCommand(live, found->second, command)) return unsupported("queue failure");
    ++live.pivotlessCommands;
    live.pivotlessGpuRecipients += route.owned;
    ++live.groupRotationCommands;
    live.groupRotationRecipients += route.owned;
    live.groupRotationNativeRecipients += route.native->count();
    // Negative control corrupts only the queued GPU copy, not the CPU journal.
    // No direct GL call or extra dispatch, even in the negative experiment.
    if (envFlags().hyperNegative && !live.hyperNegativeInjected) {
        for (auto id : route.members) {
            auto& entry = live.entries[id];
            if (!entry.owned || !(live.pivotlessRules[pivotlessClass(entry.object.data())].rotationMask & 3u)) continue;
            live.residentPending.back().orientation = orientation + 16.f;
            live.hyperNegativeObject = id;
            live.hyperNegativeInjected = true;
            break;
        }
    }
    return route.native.data();
}
cocos2d::CCArray* submitGroupRotation(GJBaseGameLayer* layer, CCArray* objects,
    double centerX, double centerY, double dx, double dy,
    float a, float b, float c, float d, float tx, float ty, float orientation, bool finished) {
    if (!groupRotationActive(layer) || !objects || !objects->count()) return objects;
    auto& live = *state;
    const auto found = live.routeIndices.find(objects);
    auto fallback = [&]() {
        releaseArray(objects, "resident_unsupported_rotation");
        live.groupRotationNativeRecipients += objects->count();
        return objects;
    };
    if (found == live.routeIndices.end()) return fallback();
    auto& route = live.routes[found->second];
    if (!route.valid || objects->count() != route.count) refreshNativeRoute(live, found->second);
    if (!route.owned) { live.groupRotationNativeRecipients += objects->count(); return objects; }
    if (layer->m_sectionXFactor != live.sectionXFactor || layer->m_sectionYFactor != live.sectionYFactor ||
        !std::isfinite(centerX) || !std::isfinite(centerY) || !std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) || !std::isfinite(d) ||
        !std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(orientation)) return fallback();
    gpu::ResidentTranslation command{dx, dy, static_cast<std::uint32_t>(found->second), layer->m_gameState.m_commandIndex,
        (finished ? gpu::residentRotationFinished : 0u) |
        (!layer->m_startOptimization ? gpu::residentDecorationStamp : 0u)};
    command.kind = 1;
    command.centerX = centerX; command.centerY = centerY;
    command.matrixA = a; command.matrixB = b; command.matrixC = c; command.matrixD = d;
    command.matrixTx = tx; command.matrixTy = ty; command.orientation = orientation;
    if (!queueResidentCommand(live, found->second, command)) return objects;
    ++live.groupRotationCommands;
    live.groupRotationRecipients += route.owned;
    live.groupRotationNativeRecipients += route.native->count();
    if (live.validate) for (auto id : route.members) {
        auto& entry = live.entries[id];
        if (entry.owned) gpu::residentRotatePosition(entry.reference.x, entry.reference.y, command);
    }
    return route.native.data();
}

bool move(GJBaseGameLayer* layer, CCArray* objects, double dx, double dy, bool lockPlayerY,
          const std::function<void(CCArray*)>& native) {
    if (!state || state->layer != layer || !state->ready || state->internal || !objects) return false;
    if (!objects->count()) { native(objects); return true; }
    auto& live = *state;
    // A nested translation inside a native mutation could otherwise replay
    // stale state. Conservatively retire these exceptional in-flight edits.
    if (!live.nativeEdits.empty()) {
        std::vector<Handle> affected;
        for (auto id : live.nativeEdits) {
            if (!live.entries[id].nativeEdit) continue;
            live.finishNativeEdit(id, false);
            affected.push_back(live.scene->handle(id));
        }
        live.nativeEdits.clear();
        live.retainedReentrantFallbacks += affected.size();
        handoff(affected, "resident_reentrant_native_edit");
    }
    // Commands recorded before a native write must reach the GPU before the
    // absolute rebase, and all later translations must follow the rebase.
    if (!live.residentRebases.empty() && !flushResident(live, "native_rebase")) {
        native(objects);
        return true;
    }
    auto timedNative = [&](CCArray* array, int group = -1) {
        spike::Scope timing(spike::Phase::nativeMove, array->count());
        spike::audit.recordGroup(group, spike::Work::nativeMove, array->count());
        if (!live.audit) { native(array); return; }
        ++live.auditTimes.nativeCalls;
        // Avoid charging recursively forwarded arrays twice.
        if (live.auditNativeDepth++) {
            native(array);
            --live.auditNativeDepth;
            return;
        }
        const auto nestedBefore = live.residentHandoffMs + live.residentSyncMs;
        const auto started = Clock::now();
        native(array);
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        --live.auditNativeDepth;
        live.auditTimes.nativeMoveMs += elapsed;
        live.auditTimes.nativeMoveExclusiveMs += std::max(0.0,
            elapsed - (live.residentHandoffMs + live.residentSyncMs - nestedBefore));
    };
    std::size_t routeIndex = std::numeric_limits<std::size_t>::max();
    const auto generation = live.scene->generation();
    const bool cacheRoute = live.cachedPoliciesEnabled || live.aggressive;
    if (cacheRoute && live.recentMoveArray == objects &&
        live.recentMoveSceneGeneration == generation &&
        live.recentMoveRoute < live.routes.size() &&
        live.routes[live.recentMoveRoute].source.data() == objects) {
        routeIndex = live.recentMoveRoute;
        ++live.stats.precompiledRouteHits;
    } else {
        if (cacheRoute) ++live.stats.routeHashLookups;
        auto found = live.routeIndices.find(objects);
        if (found != live.routeIndices.end()) {
            routeIndex = found->second;
            if (cacheRoute) {
                live.recentMoveArray = objects;
                live.recentMoveRoute = routeIndex;
                live.recentMoveSceneGeneration = generation;
            }
        }
    }
    if (routeIndex == std::numeric_limits<std::size_t>::max()) {
        releaseArray(objects, "uncompiled_native_array");
        live.stats.nativeMoveVisits += objects->count();
        if (live.audit) live.auditUncompiledVisits += objects->count();
        timedNative(objects);
        return true;
    }
    auto& route = live.routes[routeIndex];
    if (!live.censusMasks.empty()) ++route.censusCalls;
    bool auditCommitted = false;
    auto commitAuditMove = [&] {
        if (live.audit && !auditCommitted) {
            ++route.auditMoves;
            auditCommitted = true;
        }
    };
    auto forward = [&](CCArray* array) {
        commitAuditMove();
        live.stats.nativeMoveVisits += array->count();
        if (live.residentEnabled) route.residentNativeVisits += array->count();
        timedNative(array, route.nativeGroup);
    };
    // The pinned 2.2091 moveObjects body does not read lockPlayerY. The native
    // action processor has already resolved its translation parameters.
    (void)lockPlayerY;
    if (!route.valid || objects->count() != route.count) {
        if (live.aggressive) refreshNativeRoute(live, routeIndex);
        else {
            if (route.owned) releaseAll("native_array_or_lock_changed");
            forward(objects); return true;
        }
    }
    if (!route.owned) { forward(objects); return true; }
    if (live.aggressive && (layer->m_sectionXFactor != live.sectionXFactor ||
            layer->m_sectionYFactor != live.sectionYFactor)) {
        releaseAll("native_section_configuration_changed");
        forward(objects); return true;
    }
    if (live.residentEnabled) {
        if (live.residentJournal.size() >= (live.aggressive ? UINT32_MAX : 65536u)) {
            releaseAll("resident_journal_address_limit");
            forward(objects); return true;
        }
        if (std::fpclassify(dx) == FP_SUBNORMAL || std::fpclassify(dy) == FP_SUBNORMAL ||
            !std::isfinite(dx) || !std::isfinite(dy)) {
            if (live.aggressive) releaseArray(objects, "resident_unsupported_numeric_move");
            else releaseAll("resident_precision_or_journal_limit");
            forward(objects); return true;
        }
        if (live.residentPending.size() >= live.replayCapacity && !flushResident(live, "command_capacity")) {
            forward(objects); return true;
        }
    }
    const Translation delta{dx, dy};
    if (live.aggressive) {
        constexpr double limit = std::numeric_limits<float>::max();
        const bool overflow = std::any_of(route.chains.begin(), route.chains.end(), [&](auto id) {
            const auto offset = live.scene->chainTranslation(id);
            const auto magnitude = live.chains[id].precision.coordinateMagnitude;
            return std::abs(offset.x + dx) + magnitude > limit || std::abs(offset.y + dy) + magnitude > limit;
        });
        if (overflow) {
            releaseArray(objects, "resident_position_range");
            forward(objects); return true;
        }
    } else {
        const auto active = route.chains;
        CameraCache cameras;
        for (auto id : active) {
            const auto offset = live.scene->chainTranslation(id);
            const auto error = live.chains[id].precision.nextError(offset, delta);
            if (auto* reason = unsafe(id, {offset.x + dx, offset.y + dy}, error, &cameras)) {
                handoffChain(id, reason);
                cameras.clear();
            }
        }
    }
    if (!route.owned) { forward(objects); return true; }
    for (auto id : route.chains) {
        auto& chain = live.chains[id];
        chain.precision.accumulatedError = chain.precision.nextError(live.scene->chainTranslation(id), delta);
    }
    const auto owned = route.owned;
    const Ref<CCArray> nativeSubset = route.native;
    // Count the logical route invocation once, before its recipient snapshot
    // is executed. A later release cannot retroactively become a native visit.
    commitAuditMove();
    if (live.residentEnabled) {
        const gpu::ResidentTranslation command{dx, dy, static_cast<std::uint32_t>(routeIndex), layer->m_gameState.m_commandIndex,
            live.aggressive && !layer->m_startOptimization ? gpu::residentDecorationStamp : 0u};
        if (!queueResidentCommand(live, routeIndex, command)) { forward(objects); return true; }
        route.residentVisits += owned;
    } else {
        for (auto id : route.chains)
            if (auto& history = live.chains[id].history)
                history->move(route.token, delta, live.order + 1, layer->m_gameState.m_commandIndex);
    }
    live.scene->translate(route.token, delta, ++live.order, live);
    layer->m_movedCount += owned;
    live.stats.eliminatedVisits += owned;
    spike::audit.recordGroup(route.nativeGroup, spike::Work::residentMove, owned);
    live.stats.headlessEliminatedVisits += route.ownedHeadless;
    ++live.stats.translations;
    live.stats.maxActiveChains = std::max<std::uint64_t>(live.stats.maxActiveChains, route.chains.size());
    live.stats.maxDirty = std::max<std::uint64_t>(live.stats.maxDirty, live.scene->dirtyCount());
    if (live.validate) {
        for (auto id : route.members) {
            auto& entry = live.entries[id];
            if (!entry.owned) continue;
            if (dx != 0) entry.reference.x += dx;
            if (dy != 0) entry.reference.y += dy;
            if (live.residentEnabled) continue;
            const auto actual = live.scene->position(live.scene->handle(id));
            live.stats.maxDivergence = std::max({live.stats.maxDivergence,
                std::abs(actual.x - entry.reference.x), std::abs(actual.y - entry.reference.y)});
            ++live.stats.validationVisits;
        }
    }
    std::vector<SectionCandidate> sectionCandidates;
    if (live.aggressive && !live.groupRotation) collectSectionCandidates(live, routeIndex, sectionCandidates);
    if (sectionCandidates.empty()) {
        if (nativeSubset->count()) forward(nativeSubset.data());
        return true;
    }
    // Merge actual section commits into the native recipient order. The native
    // helper only sees its own recipients, and only on a crossing do we split
    // that precompiled subset at the affected slots.
    Ref<CCArray> range;
    if (nativeSubset->count()) range = CCArray::createWithCapacity(nativeSubset->count());
    unsigned cursor = 0;
    auto forwardUntil = [&](unsigned slot) {
        if (!range) return;
        while (cursor < route.nativeSlots.size() && route.nativeSlots[cursor] < slot)
            range->addObject(nativeSubset->objectAtIndex(cursor++));
        if (!range->count()) return;
        forward(range.data());
        range->removeAllObjects();
    };
    for (const auto& [slot, id] : sectionCandidates) {
        forwardUntil(slot);
        commitResidentSection(live, id);
    }
    forwardUntil(std::numeric_limits<unsigned>::max());
    return true;
}
bool drawAtlas(CCTextureAtlas* atlas, unsigned count, unsigned first, const std::function<void(unsigned, unsigned)>& native) {
    if (!state || state->internal || state->drawing || !state->visiblePass) return false;
    auto& live = *state;
    if (live.gpuTimer && !live.gpuStarted && live.context == wglGetCurrentContext()) {
        live.gpuTimer->begin(); live.gpuStarted = true;
    }
    live.drawing = true;
    struct DrawingScope { Live& live; ~DrawingScope() { live.drawing = false; } } drawingScope{live};
    // GD 2.2091 uploads only [start, start + count) on a dirty atlas draw,
    // then clears the atlas-wide dirty flag. A split mixed-ownership draw
    // must carry that original dirty state to every native subrange.
    bool originalAtlasDirty = atlas->isDirty();
    auto nativeRange = [&](unsigned n, unsigned start) {
        if (!n) return;
        ++live.stats.nativeDraws;
        live.stats.nativeQuads += n;
        if (originalAtlasDirty) atlas->setDirty(true);
        const bool dirty = atlas->isDirty();
        const auto auditStarted = live.audit ? Clock::now() : Clock::time_point{};
        native(n, start);
        if (live.audit) live.auditTimes.nativeDrawMs +=
            std::chrono::duration<double, std::milli>(Clock::now() - auditStarted).count();
        if (dirty && !atlas->isDirty()) {
            ++live.stats.nativeAtlasUploads;
            live.stats.nativeAtlasDirtyBytes += static_cast<std::uint64_t>(n) * sizeof(ccV3F_C4B_T2F_Quad);
        }
    };
    if (!live.ready || !live.renderer || !live.owned()) { nativeRange(count, first); return true; }
    auto found = live.atlases.find(atlas);
    if (found == live.atlases.end()) { nativeRange(count, first); return true; }
    if (live.context != wglGetCurrentContext()) {
        releaseAll("draw_context_replacement");
        if (live.residentTimer) live.residentTimer->abandonContext();
        live.renderer->abandonContext(); live.renderer.reset();
        nativeRange(count, first); return true;
    }
    if (!flushResident(live, "atlas_draw")) { nativeRange(count, first); return true; }
    if (live.aggressive) {
        try {
            refreshResidentAtlas(live, atlas, found->second);
            for (auto id : found->second.nativeResidents) {
                auto& entry = live.entries[id];
                if (!entry.owned || !entry.object->isVisible() || entry.object->getTextureAtlas() != atlas) continue;
                if (live.syncResident(id, "native_atlas_draw", true))
                    entry.object->updateTransform();
            }
            originalAtlasDirty = atlas->isDirty();
        } catch (const std::exception& error) {
            ++live.stats.renderFailures;
            log::error("VULKANIZER_V2_RESIDENT atlas refresh failed: {}", error.what());
            releaseAll("resident_atlas_refresh_failure");
            originalAtlasDirty = atlas->isDirty();
            nativeRange(count, first);
            return true;
        }
    }
    kmMat4 projection, view, mvp;
    kmGLGetMatrix(KM_GL_PROJECTION, &projection); kmGLGetMatrix(KM_GL_MODELVIEW, &view);
    kmMat4Multiply(&mvp, &projection, &view);
    std::array<float, 16> matrix;
    std::copy_n(mvp.mat, 16, matrix.begin());
    std::optional<std::uint32_t> previousPage;
    live.drawingAtlas = atlas;
    unsigned nativeRanges = 0, sharedRanges = 0;
    unsigned completed = first;
    try { found->second.plan.submit(first, count, [&](unsigned n, unsigned start) { ++nativeRanges; nativeRange(n, start); completed = start + n; },
        [&](const Draw& draw) {
            const auto original = live.scene->drawFor(live.scene->handle(draw.nativeObject));
            // Once a range has been attempted, recovery continues after it.
            // This prevents duplicate blending if a driver reports a late error.
            completed = live.entries[draw.nativeObject].slot + (draw.firstVertex - original.firstVertex + draw.vertexCount) / 6;
            live.renderer->renderRanges(*live.scene, std::span<const Draw>(&draw, 1), matrix, live);
            ++live.stats.v2Draws;
            live.stats.v2Quads += draw.vertexCount / 6;
            ++sharedRanges;
            if (previousPage && *previousPage != draw.page) ++live.stats.pageBreaks;
            previousPage = draw.page;
        }); } catch (const std::exception& error) {
        ++live.stats.renderFailures;
        log::error("VULKANIZER_V2_RENDER recovery={} attempted_range_not_replayed=true", error.what());
        releaseAll("renderer_failure");
        live.renderer.reset();
        if (completed < first + count) nativeRange(first + count - completed, completed);
    }
    if (sharedRanges) ++live.stats.materialBoundaries;
    if (nativeRanges && sharedRanges) live.stats.ownershipBoundaries += nativeRanges + sharedRanges - 1;
    return true;
}
void recordSpriteVisit(CCSprite*, bool suppressed) {
    if (!state || !state->ready || state->internal) return;
    ++state->stats.spriteVisits;
    if (suppressed) ++state->stats.geometrySuppressed;
}
void recordSectionCommit(GameObject* object) {
    if (!state || !state->ready || state->internal) return;
    ++state->stats.nativeCommitCallbacks;
    if (owns(object)) {
        const auto id = ownedLookupId(*state, object);
        if (id != invalidObjectId && state->entries[id].nativeEdit) return;
        ++state->stats.ownershipCommitViolations;
        release(object, "unexpected_authoritative_section_consumer");
    }
}
void frameBegin() {
    spike::audit.active = false;
    spikeFrameOwner = nullptr;
    if (!state) return;
    if (envFlags().visibilityCensus && !state->visibilitySamples.empty()) {
        state->visibilityUnpaired += state->visibilitySamples.size();
        state->visibilitySamples.clear();
        state->visibilityCameras.clear();
    }
    state->frameStart = Clock::now();
    state->perfFrameStartedReady = state->ready;
    if (envFlags().performanceOnly && state->ready) {
        state->perfInitialTicks = state->ticks;
        state->perfCurrentIntervalMs = 0;
        if (state->perfPreviousFrameStart != Clock::time_point{}) {
            state->perfCurrentIntervalMs = std::chrono::duration<double, std::milli>(state->frameStart - state->perfPreviousFrameStart).count();
            state->perfIntervals.push_back(state->perfCurrentIntervalMs);
        }
        state->perfPreviousFrameStart = state->frameStart;
    }
    if (envFlags().spikeAudit && state->ready && !state->testCompleted) {
        if (!state->spikeStarted) {
            spike::audit.reset();
            if (envFlags().nativeVisibilityProfile || envFlags().visibilityOwnershipShadow)
                state->nativeVisibilityFrames.reserve(1200);
            state->spikeStarted = true;
        }
        spikeFrameOwner = state.get();
        spike::audit.begin(state->frames + 1, state->layer->m_gameState.m_levelTime, spikeCounters(*state));
    }
    state->frameCaptured = false;
    state->gpuStarted = false;
    if (state->gpuTimer && state->context == wglGetCurrentContext()) state->gpuTimer->collect();
    if (state->residentTimer && state->context == wglGetCurrentContext()) state->residentTimer->collect();
}
void beforeSwap() {
    const auto& flags = envFlags();
    if (!state) return;
    auto& live = *state;
    if (flushResident(live, "before_swap")) validateResident(live);
    if (live.gpuTimer && live.context == wglGetCurrentContext()) live.gpuTimer->end();
    const auto& path = flags.capturePath;
    const auto captureTime = flags.captureTime.value_or(residentAtlasFixture(flags) ? .12 : .75);
    if (!path || captured || live.layer->m_gameState.m_levelTime < captureTime) return;
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) return;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(viewport[2]) * viewport[3] * 4);
    glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream file(*path, std::ios::binary);
    file << "P6\n" << viewport[2] << ' ' << viewport[3] << "\n255\n";
    for (int row = viewport[3] - 1; row >= 0; --row)
        for (int column = 0; column < viewport[2]; ++column)
            file.write(reinterpret_cast<const char*>(pixels.data() + (static_cast<std::size_t>(row) * viewport[2] + column) * 4), 3);
    live.stats.validationReadbackBytes += pixels.size();
    live.capture = matjson::makeObject({{"level_time", live.layer->m_gameState.m_levelTime}, {"ticks", testTicks},
        {"player_x", live.layer->m_player1->getPositionX()}, {"player_y", live.layer->m_player1->getPositionY()},
        {"owned", live.owned()}, {"width", viewport[2]}, {"height", viewport[3]}});
    if (flags.gpuResident && flags.validate && flags.headlessFixture && flags.xlockFixture && flags.test && !flags.levelName) {
        unsigned overlapping = 0;
        for (ObjectId id = 0; id < live.entries.size(); ++id) {
            auto& entry = live.entries[id];
            auto* object = entry.object.data();
            if (object->m_objectID != 1757 || object->m_groupCount < 2) continue;
            if (++overlapping != 2) continue;
            const bool wasOwned = entry.owned;
            const auto position = object->getRealPosition();
            (*live.capture)["resident_authoritative_read"] = matjson::makeObject({
                {"id", id}, {"was_owned", wasOwned}, {"still_owned", entry.owned},
                {"read_x", position.x}, {"read_y", position.y},
                {"x", object->m_positionX}, {"y", object->m_positionY},
                {"history_x", object->m_lastPosition.x}, {"history_y", object->m_lastPosition.y},
                {"stamp", object->m_unk4C4}, {"outer_section", object->m_outerSectionIndex},
                {"middle_section", object->m_middleSectionIndex}});
            break;
        }
    }
    if (flags.colorShakeFixture && flags.test &&
        !flags.levelName) {
        for (const auto& entry : live.entries) if (entry.batch) {
            const auto p = entry.batch->convertToWorldSpace({0, 0});
            const auto x = entry.batch->convertToWorldSpace({1, 0});
            const auto y = entry.batch->convertToWorldSpace({0, 1});
            (*live.capture)["fixture_camera"] = matjson::makeObject({{"x", p.x}, {"y", p.y},
                {"xx", x.x - p.x}, {"xy", x.y - p.y}, {"yx", y.x - p.x}, {"yy", y.y - p.y}});
            break;
        }
    }
    if ((flags.xlockFixture || flags.headlessFixture || residentAtlasFixture(flags)) && flags.test &&
        !flags.levelName) {
        (*live.capture)["fixture_positions"] = matjson::Value::array();
        for (ObjectId id = 0; id < live.entries.size(); ++id) {
            auto& entry = live.entries[id];
            if (!entry.batch && !entry.headless) continue;
            auto p = entry.owned ? (live.residentEnabled && live.validate ? entry.reference :
                live.scene->position(live.scene->handle(id))) :
                Translation{entry.object->m_positionX, entry.object->m_positionY};
            const auto quad = entry.object->getQuad();
            auto sample = matjson::makeObject({
                {"id", id}, {"x", p.x}, {"y", p.y}, {"owned", entry.owned},
                {"quad_bl_x", quad.bl.vertices.x}, {"quad_bl_y", quad.bl.vertices.y},
                {"quad_tr_x", quad.tr.vertices.x}, {"quad_tr_y", quad.tr.vertices.y},
                {"color_r", quad.bl.colors.r}, {"color_g", quad.bl.colors.g},
                {"color_b", quad.bl.colors.b}, {"color_a", quad.bl.colors.a},
                {"history_x", entry.object->m_lastPosition.x}, {"history_y", entry.object->m_lastPosition.y},
                {"stamp", entry.object->m_unk4C4},
                {"visible", entry.object->isVisible()}, {"atlas_index", entry.object->getAtlasIndex()}});
            const auto slot = entry.object->getAtlasIndex();
            if (entry.atlas && slot < entry.atlas->getTotalQuads()) {
                const auto atlasQuad = entry.atlas->getQuads()[slot];
                sample["atlas_bl_x"] = atlasQuad.bl.vertices.x;
                sample["atlas_bl_y"] = atlasQuad.bl.vertices.y;
                sample["atlas_tr_x"] = atlasQuad.tr.vertices.x;
                sample["atlas_tr_y"] = atlasQuad.tr.vertices.y;
            }
            (*live.capture)["fixture_positions"].push(std::move(sample));
        }
    }
    auto size = CCDirector::sharedDirector()->getWinSize();
    double left = viewport[2], right = 0, bottom = viewport[3], top = 0;
    for (ObjectId id = 0; id < live.entries.size(); ++id) {
        const auto& entry = live.entries[id];
        if (!entry.batch) continue;
        const auto offset = entry.owned ? live.scene->chainTranslation(live.scene->chainFor(live.scene->handle(id))) : Translation{};
        const auto quad = entry.object->getQuad();
        for (auto corner : {quad.bl, quad.br, quad.tl, quad.tr}) {
            const auto point = entry.batch->convertToWorldSpace({corner.vertices.x + static_cast<float>(offset.x), corner.vertices.y + static_cast<float>(offset.y)});
            const double x = point.x / size.width * viewport[2], y = viewport[3] - point.y / size.height * viewport[3];
            left = std::min(left, x); right = std::max(right, x); bottom = std::min(bottom, y); top = std::max(top, y);
        }
    }
    (*live.capture)["roi_left"] = std::max(0, static_cast<int>(std::floor(left)) - 4);
    (*live.capture)["roi_top"] = std::max(0, static_cast<int>(std::floor(bottom)) - 4);
    (*live.capture)["roi_right"] = std::min(viewport[2], static_cast<int>(std::ceil(right)) + 4);
    (*live.capture)["roi_bottom"] = std::min(viewport[3], static_cast<int>(std::ceil(top)) + 4);
    captured = true;
    live.frameCaptured = true;
}
float testFixedFrameDelta() {
    if (!testRunning || !state || state->testCompleted) return 0.f;
    if (envFlags().fixedProfile || envFlags().mirrorWorklistAudit || envFlags().visibilityCensus || envFlags().visibilityFilterAudit) { ++state->fixedProfileClockFrames; return 1.f / 30.f; }
    if (!envFlags().setterShadowAligned) return 0.f;
    ++state->alignedClockFrames;
    return .1f;
}
void physicsStep(PlayLayer* layer, float dt, bool visibleFrame) {
    if (state && state->layer == layer) {
        ++state->ticks;
        if (envFlags().setterShadowAligned) {
            auto& hash = state->alignedStepSignature;
            auto mix = [&](std::uint64_t v) { hash = (hash ^ v) * UINT64_C(1099511628211); };
            mix(setter_audit::bits(dt)); mix(visibleFrame);
            mix(setter_audit::bits(double(layer->m_gameState.m_levelTime)));
            mix(layer->m_gameState.m_commandIndex);
        }
    }
    if (testRunning) ++testTicks;
}
void frameEnd() {
    const auto& flags = envFlags();
    const bool finishedSpikeFrame = spike::audit.active && state &&
        state.get() == spikeFrameOwner && state->spikeStarted;
    if (finishedSpikeFrame) {
        spike::audit.finish(state->layer->m_gameState.m_levelTime, spikeCounters(*state));
        if (flags.nativeVisibilityProfile || flags.visibilityOwnershipShadow) {
            const auto& source = spike::audit.frame;
            Live::NativeVisibilityFrame frame;
            frame.index = source.index;
            frame.simulationSteps = source.steps;
            frame.spriteTransformVisits = source.counters.spriteVisits;
            frame.geometrySuppressedVisits = source.counters.eliminatedVisits;
            frame.nativeSectionCallbacks = source.counters.sectionCallbacks;
            frame.residentSectionTransitions = source.counters.sectionTransitions;
            for (std::size_t i = 0; i < source.phases.size(); ++i)
                frame.phaseExclusiveMs[i] = source.phases[i].exclusive;
            frame.events = source.events;
            state->nativeVisibilityFrames.push_back(std::move(frame));
            if (flags.visibilityOwnershipShadow && state->visibilityOwnership.initialized) {
                auto& shadow = state->visibilityOwnership;
                Live::VisibilityOwnershipFrame row;
                row.index = source.index;
                row.simulationSteps = source.steps;
                row.candidates = shadow.frameCandidates;
                row.nativeVisibleOwned = shadow.nativeVisibleOwned.size();
                row.coveredVisible = shadow.frameCoveredVisible;
                row.candidateFalsePositives = shadow.frameCandidateFalsePositives;
                row.candidateFalseNegatives = shadow.frameCandidateFalseNegatives;
                row.candidateEntries = shadow.frameCandidateEntries;
                row.candidateExits = shadow.frameCandidateExits;
                row.cameraEntries = shadow.frameCameraEntries;
                row.cameraExits = shadow.frameCameraExits;
                row.objectEntries = shadow.frameObjectEntries;
                row.objectExits = shadow.frameObjectExits;
                row.nativeEntries = shadow.frameNativeEntries;
                row.nativeExits = shadow.frameNativeExits;
                row.candidateEntryMismatches = shadow.frameCandidateEntryMismatches;
                row.candidateExitMismatches = shadow.frameCandidateExitMismatches;
                row.ownedBatchAttachments = shadow.frameOwnedBatchAttachments;
                row.ownedBatchDetachments = shadow.frameOwnedBatchDetachments;
                row.ownedSectionTransitions = shadow.frameOwnedSectionTransitions;
                row.candidateCoverageAvailable = shadow.passObserved && shadow.candidateCoverageAvailable;
                for (std::size_t i = 0; i < source.phases.size(); ++i)
                    row.phaseExclusiveMs[i] = source.phases[i].exclusive;
                row.events = source.events;
                shadow.frames.push_back(std::move(row));
                shadow.passObserved = false;
                shadow.frameCandidates = shadow.frameNativeVisibleOwned = shadow.frameCoveredVisible = 0;
                shadow.frameCandidateFalsePositives = shadow.frameCandidateFalseNegatives = 0;
                shadow.frameCandidateEntries = shadow.frameCandidateExits = 0;
                shadow.frameCameraEntries = shadow.frameCameraExits = 0;
                shadow.frameObjectEntries = shadow.frameObjectExits = 0;
                shadow.frameNativeEntries = shadow.frameNativeExits = 0;
                shadow.frameCandidateEntryMismatches = shadow.frameCandidateExitMismatches = 0;
                shadow.frameOwnedBatchAttachments = shadow.frameOwnedBatchDetachments = 0;
                shadow.frameOwnedSectionTransitions = 0;
                shadow.nativeEntered.clear();
                shadow.nativeExited.clear();
                shadow.sectionChangedThisFrame.clear();
            }
        }
    } else spike::audit.active = false;
    spikeFrameOwner = nullptr;
    if (!state) return;
    auto& live = *state;
    if (!live.ready && !live.assessed && live.visiblePass) {
        try { compile(); }
        catch (const std::exception& error) {
            log::error("VULKANIZER_V2_COMPILE failure={} native_recovery=true", error.what());
            live.compiling = false;
            live.ready = true;
            releaseAll("compilation_failure");
        }
    }
    if (live.ready && flags.toggleFixture &&
        flags.test && !flags.levelName) {
        if (!live.toggleFixtureTrace) {
            live.toggleFixtureTrace = matjson::Value::array();
            if (live.layer->m_groups.size() > 9998 && live.layer->m_groups[9998])
                for (auto* object : CCArrayExt<GameObject, false>(live.layer->m_groups[9998])) {
                    if (object->m_objectType != GameObjectType::Decoration) continue;
                    live.toggleFixtureObjects.push_back(object);
                    if (live.toggleFixtureObjects.size() == 2) break;
                }
        }
        const auto time = live.layer->m_gameState.m_levelTime;
        const double thresholds[]{.20, .45, .70};
        if (live.toggleFixturePhase < 3 && time >= thresholds[live.toggleFixturePhase] && live.toggleFixtureObjects.size() == 2) {
            auto event = matjson::makeObject({{"phase", live.toggleFixturePhase}, {"level_time", time},
                {"command_index", live.layer->m_gameState.m_commandIndex}});
            event["objects"] = matjson::Value::array();
            for (auto& ref : live.toggleFixtureObjects) {
                auto* o = ref.data();
                auto p = Translation{o->m_positionX, o->m_positionY};
                auto id = live.ids.find(o);
                const bool owned = id != live.ids.end() && live.entries[id->second].owned;
                if (owned) p = live.scene->position(live.scene->handle(id->second));
                event["objects"].push(matjson::makeObject({{"x", p.x}, {"y", p.y}, {"v2_owned", owned},
                    {"history_x", o->m_lastPosition.x}, {"history_y", o->m_lastPosition.y},
                    {"stamp", o->m_unk4C4}, {"decoration2", o->m_isDecoration2},
                    {"group_disabled", o->m_isGroupDisabled}, {"visible", o->isVisible()}}));
            }
            live.toggleFixtureTrace->push(std::move(event));
            if (live.toggleFixturePhase == 0) live.layer->toggleGroup(9998, false);
            if (live.toggleFixturePhase == 1) live.layer->toggleGroup(9998, true);
            ++live.toggleFixturePhase;
        }
    }
    if (live.ready && !live.structuralHandoffTest && flags.structuralFixture && flags.test &&
        !flags.levelName && live.layer->m_gameState.m_levelTime >= .4) {
        live.structuralHandoffOwned = true;
        for (auto& entry : live.entries) {
            auto* object = entry.object.data();
            if (object->m_objectID != 1757 || !entry.batch) continue;
            live.structuralHandoffTest = true;
            live.structuralHandoffOwned &= entry.owned;
            live.structuralHandoffObjects.push_back(object);
            object->setRotation(object->getRotation() + 19.f);
            if (live.structuralHandoffObjects.size() == 4) break;
        }
    }
    ++live.frames;
    if (live.audit) {
        if (live.frameStart != Clock::time_point{})
            live.auditTimes.frameMs += std::chrono::duration<double, std::milli>(Clock::now() - live.frameStart).count();
        live.auditMaxRevocationsPerFrame = std::max(live.auditMaxRevocationsPerFrame, live.auditRevocationsThisFrame);
        live.auditRevocationsThisFrame = 0;
        const double totalHandoff = live.residentHandoffMs + live.residentSyncMs;
        live.auditMaxHandoffPerFrame = std::max(live.auditMaxHandoffPerFrame, totalHandoff - live.auditPreviousHandoff);
        live.auditPreviousHandoff = totalHandoff;
    }
    if (!flags.performanceOnly && flags.test && live.layer->m_gameState.m_levelTime >= 80 && !live.benchmarkStarted) {
        live.benchmarkStarted = true;
        live.benchmarkStart = live.stats;
        live.auditWindowStart = live.auditTimes;
        live.auditWindowStartTime = live.layer->m_gameState.m_levelTime;
    }
    if (live.benchmarkStarted && !live.benchmarkFinished && live.layer->m_gameState.m_levelTime >= 90) {
        live.benchmarkFinished = true;
        live.benchmarkEnd = live.stats;
        live.auditWindowEnd = live.auditTimes;
        live.auditWindowEndTime = live.layer->m_gameState.m_levelTime;
    }
    if (live.progressStart == Clock::time_point{} && live.layer->m_gameState.m_levelTime > 0) live.progressStart = Clock::now();
    if (live.frameStart != Clock::time_point{} && !live.frameCaptured && (!flags.performanceOnly || live.perfFrameStartedReady)) {
        if (!flags.performanceOnly && live.cpuFrames.size() == 4096) live.cpuFrames.erase(live.cpuFrames.begin(), live.cpuFrames.begin() + 2048);
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - live.frameStart).count();
        if (live.benchmarkStarted && !live.benchmarkFinished) live.cpuBenchmarkFrames.push_back(elapsed);
        live.cpuFrames.push_back(elapsed);
        if (flags.performanceOnly && live.perfFrameStartedReady)
            live.perfTrace.push_back({live.ticks - live.perfInitialTicks, live.perfCurrentIntervalMs, elapsed, live.layer->m_gameState.m_levelTime});
        if (!flags.performanceOnly && live.owned() && live.layer->m_gameState.m_levelTime > 0) {
            if (live.cpuOwnedFrames.size() == 4096) live.cpuOwnedFrames.erase(live.cpuOwnedFrames.begin(), live.cpuOwnedFrames.begin() + 2048);
            live.cpuOwnedFrames.push_back(elapsed);
        }
    }
    if (!flags.performanceOnly && live.frames % 240 == 0) report("frame_window");
    const auto duration = flags.testSeconds.value_or(8);
    if (testRunning && testInitialLevelTime < 0) testInitialLevelTime = live.layer->m_gameState.m_levelTime;
    if (testRunning && (flags.setterShadowAligned || flags.hyperAggressive || flags.performanceOnly) && std::chrono::duration<double>(Clock::now() - testStarted).count() > (flags.visibilityFilterAudit ? 900 : 300)) {
        // Never label a wall-clock timeout a completed deterministic replay.
        report("aligned_timeout");
        testRunning = false;
        PostMessageW(WindowFromDC(wglGetCurrentDC()), WM_CLOSE, 0, 0);
        return;
    }
    if (testRunning && (live.hyperEmergency || live.pivotlessAbort || live.mirrorFailed || live.filterFailed || live.layer->m_gameState.m_levelTime - testInitialLevelTime >= duration ||
        (!flags.setterShadowAligned && !flags.hyperAggressive && !flags.performanceOnly && std::chrono::duration<double>(Clock::now() - testStarted).count() > duration + 40))) {
        live.testCompleted = !live.pivotlessAbort && !live.mirrorFailed && !live.filterFailed;
        live.retainedAtCompletion = live.owned();
        live.headlessRetainedAtCompletion = static_cast<unsigned>(live.stats.headlessAcquired - live.stats.headlessReleased);
        report("test_complete");
        log::info("VULKANIZER_V2_TEST complete native_ticks={} level_time={} player=({},{})", testTicks,
            live.layer->m_gameState.m_levelTime, live.layer->m_player1->getPositionX(), live.layer->m_player1->getPositionY());
        testRunning = false;
        PostMessageW(WindowFromDC(wglGetCurrentDC()), WM_CLOSE, 0, 0);
    }
}
bool startTest() {
    const auto& flags = envFlags();
    if (!requested() || !flags.test) return false;
    if (testLaunched) return true;
    testLaunched = true;
    GJGameLevel* level = nullptr;
    if (const auto& name = flags.levelName) {
        for (auto* item : CCArrayExt<GJGameLevel, false>(LocalLevelManager::sharedState()->getCreatedLevels(0)))
            if (item->m_levelName == name->c_str()) { level = item; break; }
    } else {
        level = GJGameLevel::create();
        level->m_levelName = "V2 Translation Control";
        std::string setup = "kS38,1,kA13,0,kA15,0,kA16,0,kA6,1;";
        for (unsigned i = 0; i < 64; ++i) {
            const auto groups = i % 3 == 0 ? "9998.9997" : i % 3 == 1 ? "9998" : "9997";
            const auto originX = flags.headlessFixture ? 900 : 230;
            setup += fmt::format("1,1757,2,{},3,{},57,{},64,1,67,1", originX + i % 8 * 6, 140 + i / 8 * 6, groups);
            if (flags.colorShakeFixture && i % 2 == 0) setup += ",21,42";
            setup += ";";
        }
        // Real native triggers and a gameplay solid share the group in the
        // controlled level. The solid must keep native transformation/drawing.
        setup += "1,1,2,6000,3,15,57,9998;";
        if (!residentAtlasFixture(flags))
            setup += "1,901,2,15,3,15,51,9998,28,16,29,0,10,4;1,901,2,20,3,15,51,9997,28,-8,29,4,10,4;";
        if (flags.colorShakeFixture)
            setup += "1,899,2,15,3,15,23,42,7,255,8,10,9,10,10,2;1,1520,2,15,3,15,10,2,75,1,84,0.2;";
        if (flags.headlessFixture && flags.headlessAlpha)
            setup += "1,1007,2,16,3,15,51,9998,35,0.4,10,1;";
        if (flags.toggleFixture)
            setup += "1,1049,2,6000,3,15,51,9998,56,1;";
        level->m_levelString = setup;
    }
    if (!level) { log::error("VULKANIZER_V2_TEST requested level missing"); return true; }
    if ((flags.colorShakeFixture && !flags.levelName) || flags.setterShadowAligned) std::srand(314159);
    auto* scene = PlayLayer::scene(level, false);
    if (!scene) { log::error("VULKANIZER_V2_TEST PlayLayer scene creation failed"); return true; }
    CCDirector::sharedDirector()->replaceScene(scene);
    testRunning = true; testTicks = 0; testInitialLevelTime = -1; captured = false; testStarted = Clock::now();
    log::info("VULKANIZER_V2_TEST started level={} baseline={} native_gameplay_timing=true", level->m_levelName, flags.baseline);
    return true;
}
}
