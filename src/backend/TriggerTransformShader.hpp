#pragma once

namespace vulkanizer::gpu {

// Shared by the standalone parity harness and the in-game hybrid backend.
// Action order is preserved per object through Span + ActionIndices.
inline constexpr char triggerTransformComputeSource[] = R"GLSL(#version 430 core
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x = 64) in;

struct SectionEvent { double x; double y; uint actionIndex; uint reserved; };

struct ObjectState {
    double x;
    double y;
    float rotation;
    float padding;
    float offsetX;
    float offsetY;
    float rotationOffsetX;
    float rotationOffsetY;
    uint options;
    uint commandStamp;
    int sectionX;
    int sectionY;
    float sectionFactorX;
    float sectionFactorY;
    float historyX;
    float historyY;
    uint eventCount;
    uint reserved;
    SectionEvent events[4];
};
struct Action {
    uint kind;
    uint flags;
    double dx;
    double dy;
    double centerX;
    double centerY;
    float cosine;
    float sine;
    float rotationDelta;
    float padding;
    float matrixA;
    float matrixB;
    float matrixC;
    float matrixD;
    float matrixTx;
    float matrixTy;
    float objectOffsetX;
    float objectOffsetY;
};
struct Span { uint offset; uint count; };

layout(std430, binding = 0) buffer Objects { ObjectState objects[]; };
layout(std430, binding = 1) readonly buffer Actions { Action actions[]; };
layout(std430, binding = 2) readonly buffer Spans { Span spans[]; };
layout(std430, binding = 3) readonly buffer ActionIndices { uint actionIndices[]; };
uniform uint objectCount;

void main() {
    uint objectIndex = gl_GlobalInvocationID.x;
    if (objectIndex >= objectCount) return;
    ObjectState objectState = objects[objectIndex];
    Span objectSpan = spans[objectIndex];
    for (uint i = 0; i < objectSpan.count; ++i) {
        Action action = actions[actionIndices[objectSpan.offset + i]];
        if ((action.flags & 32u) != 0u && (objectState.options & 256u) == 0u &&
            objectState.commandStamp != uint(action.padding)) {
            objectState.historyX = float(objectState.x);
            objectState.historyY = float(objectState.y);
            objectState.commandStamp = uint(action.padding);
            objectState.options |= 512u;
        }
        if (action.kind == 0u) {
            if ((action.flags & 4u) == 0u && ((action.flags & 32u) == 0u ||
                (action.dx != 0.0 && (objectState.options & 4u) == 0u)))
                objectState.x = objectState.x + action.dx;
            if ((action.flags & 32u) == 0u || action.dy != 0.0)
                objectState.y = objectState.y + action.dy;
        } else if ((action.flags & 8u) != 0u) {
            float offsetX = (action.flags & 16u) != 0u ? objectState.offsetX : action.objectOffsetX;
            float offsetY = (action.flags & 16u) != 0u ? objectState.offsetY : action.objectOffsetY;
            precise float rx = float((objectState.x - double(offsetX)) - action.centerX);
            precise float ry = float((objectState.y - double(offsetY)) - action.centerY);
            precise double xa = double(rx) * double(action.matrixA);
            precise double yc = double(ry) * double(action.matrixC);
            precise double xb = double(rx) * double(action.matrixB);
            precise double yd = double(ry) * double(action.matrixD);
            precise float tx = float((yc + xa) + double(action.matrixTx));
            precise float ty = float((yd + xb) + double(action.matrixTy));
            precise float movedX = tx + float(action.dx);
            precise float movedY = ty + float(action.dy);
            precise double dx = double(movedX) - objectState.x;
            precise double dy = double(movedY) - objectState.y;
            dx = dx + double(offsetX);
            dy = dy + double(offsetY);
            precise double resultX = objectState.x + dx;
            precise double resultY = objectState.y + dy;
            objectState.x = resultX;
            objectState.y = resultY;
        } else if ((action.flags & 2u) != 0u) {
            // Mirrors the vanilla affine path for the restricted hybrid pilot:
            // form a float center-relative point, apply Cocos' resolved matrix,
            // then add the resulting float delta to the double object position.
            // Vanilla evaluates the complete center-relative expression in
            // double, then narrows once while constructing its float CCPoint.
            precise float baseX = float(objectState.x - double(action.objectOffsetX));
            precise float baseY = float(objectState.y - double(action.objectOffsetY));
            precise float relativeX = baseX - float(action.centerX);
            precise float relativeY = baseY - float(action.centerY);
            // GD uses affine CONCAT (RVA 0x6190), whose translation uses
            // float intermediates; PointApplyAffineTransform uses different
            // double intermediates and is not an equivalent reference.
            precise float x0 = action.matrixA * relativeX;
            precise float x1 = action.matrixC * relativeY;
            precise float y0 = action.matrixB * relativeX;
            precise float y1 = action.matrixD * relativeY;
            precise float rotatedX = (x1 + x0) + action.matrixTx;
            precise float rotatedY = (y1 + y0) + action.matrixTy;
            precise float translatedX = rotatedX + float(action.dx);
            precise float translatedY = rotatedY + float(action.dy);
            precise float deltaX = translatedX - baseX;
            precise float deltaY = translatedY - baseY;
            objectState.x = objectState.x + double(deltaX);
            objectState.y = objectState.y + double(deltaY);
            if ((action.flags & 1u) == 0u)
                objectState.rotation = objectState.rotation + action.rotationDelta;
        } else {
            precise float relativeX = float(objectState.x - action.centerX);
            precise float relativeY = float(objectState.y - action.centerY);
            precise float xCos = action.cosine * relativeX;
            precise float ySin = action.sine * relativeY;
            precise float xSin = action.sine * relativeX;
            precise float yCos = action.cosine * relativeY;
            precise float deltaX = (xCos - ySin) - relativeX;
            precise float deltaY = (xSin + yCos) - relativeY;
            objectState.x = objectState.x + double(deltaX);
            objectState.y = objectState.y + double(deltaY);
            if ((action.flags & 1u) == 0u)
                objectState.rotation = objectState.rotation + action.rotationDelta;
        }
        if ((action.flags & 32u) != 0u) {
            if (action.kind == 1u) {
                objectState.options |= 32u;
                if ((objectState.options & 1u) != 0u && action.rotationDelta != 0.0) {
                    float oldX = objectState.rotation;
                    float oldY = objectState.padding;
                    objectState.rotation = objectState.rotation + action.rotationDelta;
                    objectState.padding = objectState.padding + action.rotationDelta;
                    if (objectState.rotation != oldX) objectState.options |= 64u;
                    if (objectState.padding != oldY) objectState.options |= 128u;
                    objectState.rotationOffsetX = objectState.rotationOffsetX + action.rotationDelta;
                    objectState.rotationOffsetY = objectState.rotationOffsetY + action.rotationDelta;
                }
                objectState.options = (objectState.options & ~2u) | ((action.flags & 64u) != 0u ? 2u : 0u);
            } else objectState.options |= 16u;
            objectState.commandStamp = uint(action.padding);
            if (objectState.sectionX >= 0) {
                if (isnan(objectState.x) || isnan(objectState.y) ||
                    isinf(objectState.x) || isinf(objectState.y) ||
                    objectState.x > 1000000.0 || objectState.y > 1000000.0) {
                    objectState.options |= 8u;
                } else {
                    int sx = objectState.x <= 0.0 ? 0 : int(double(objectState.sectionFactorX) * objectState.x);
                    int sy = objectState.y <= 0.0 ? 0 : int(double(objectState.sectionFactorY) * objectState.y);
                    if (sx != objectState.sectionX || sy != objectState.sectionY) {
                        if (objectState.eventCount < 4u) {
                            SectionEvent event;
                            event.x = objectState.x; event.y = objectState.y;
                            event.actionIndex = uint(action.cosine); event.reserved = 0u;
                            objectState.events[objectState.eventCount++] = event;
                        } else objectState.options |= 8u;
                        objectState.sectionX = sx; objectState.sectionY = sy;
                    }
                }
            }
        }
    }
    objects[objectIndex] = objectState;
}
)GLSL";

} // namespace vulkanizer::gpu
