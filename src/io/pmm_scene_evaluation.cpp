#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include <cmath>
#include <unordered_map>
#include <utility>

namespace mikudancestudio::format {
namespace {
template <class Key> struct Bracket {
    const Key* left = nullptr;
    const Key* right = nullptr;
};
template <class Key> struct IndexedTrack {
    const PmmKeyTrack<Key>& track;
    std::unordered_map<std::uint32_t, const Key*> keys;
    bool valid = true;
    explicit IndexedTrack(const PmmKeyTrack<Key>& source) : track(source) {
        keys.reserve(track.dense.size() + track.sparse.size());
        for (std::size_t i = 0; i < track.dense.size(); ++i)
            keys.emplace(static_cast<std::uint32_t>(i), &track.dense[i]);
        for (const auto& sparse : track.sparse)
            if (sparse.index < 0 ||
                !keys.emplace(static_cast<std::uint32_t>(sparse.index), &sparse.value).second)
                valid = false;
    }
    bool Find(std::size_t head, double frame, Bracket<Key>& bracket) const {
        if (!valid || head >= track.dense.size())
            return false;
        std::uint32_t index = static_cast<std::uint32_t>(head);
        const Key* current = &track.dense[head];
        if (frame <= current->frame) {
            bracket = {current, current};
            return true;
        }
        for (std::size_t steps = 0; steps < keys.size(); ++steps) {
            if (current->next == 0) {
                bracket = {current, current};
                return true;
            }
            const auto next = keys.find(current->next);
            if (next == keys.end() || next->second->previous != index ||
                next->second->frame <= current->frame)
                return false;
            const Key* following = next->second;
            if (frame <= following->frame) {
                bracket = frame == following->frame ? Bracket<Key>{following, following}
                                                    : Bracket<Key>{current, following};
                return true;
            }
            index = current->next;
            current = following;
        }
        return false;
    }
};
float BoneEase(const PmmBoneKey& key, int channel, float t) {
    return EvaluatePmmBoneCurve(key.interpolation.data(), channel, t);
}
PmmEvaluatedBone EvaluateBone(const PmmBoneKey& key) {
    return {key.position, key.rotation, key.physicsDisabled};
}
PmmEvaluatedCamera CameraKeyPose(const PmmCameraKey& key) {
    return {key.eye,         key.target,      key.distance,  static_cast<float>(key.fov),
            key.perspective, key.parentModel, key.parentBone};
}
float CameraEase(const PmmCameraKey& key, int channel, float t) {
    const auto& curve = key.interpolation;
    if (curve[0][channel] == curve[1][channel] && curve[2][channel] == curve[3][channel])
        return t;
    const float x1 = static_cast<float>(static_cast<std::int8_t>(curve[0][channel])) / 127.0f;
    const float x2 = static_cast<float>(static_cast<std::int8_t>(curve[2][channel])) / 127.0f;
    const float y1 = static_cast<float>(static_cast<std::int8_t>(curve[1][channel]));
    const float y2 = static_cast<float>(static_cast<std::int8_t>(curve[3][channel]));
    float u = 0.5f, half = 0.25f;
    for (int i = 0; i < 12; ++i) {
        const float v = 1.0f - u;
        const float v3 = v * 3.0f;
        const float x = (v3 * v * u) * x1 + (v3 * u * u) * x2 + u * u * u;
        if (x == t)
            break;
        u = x >= t ? u - half : u + half;
        half *= 0.5f;
    }
    const float v = 1.0f - u;
    const float v3 = v * 3.0f;
    return (v3 * v * u) * y1 / 127.0f + (v3 * u * u) * y2 / 127.0f + u * u * u;
}
}

bool EvaluatePmmCameraTrack(const PmmKeyTrack<PmmCameraKey>& track, double frame,
                            PmmCameraInterpolation mode, PmmEvaluatedCamera& output) {
    if (!std::isfinite(frame) || frame < 0.0)
        return false;
    IndexedTrack<PmmCameraKey> indexed(track);
    if (!indexed.valid || track.dense.size() != 1)
        return false;
    const PmmCameraKey* current = &track.dense[0];
    std::uint32_t index = 0;
    for (std::size_t steps = 0; static_cast<double>(current->frame) < frame; ++steps) {
        if (steps >= indexed.keys.size())
            return false;
        if (!current->next)
            break;
        const auto found = indexed.keys.find(current->next);
        if (found == indexed.keys.end() || found->second->previous != index ||
            found->second->frame <= current->frame)
            return false;
        index = current->next;
        current = found->second;
    }
    if (static_cast<double>(current->frame) <= frame) {
        output = CameraKeyPose(*current);
        return true;
    }
    const auto previous = indexed.keys.find(current->previous);
    if (previous == indexed.keys.end() || previous->second->next != index ||
        previous->second->frame >= current->frame)
        return false;
    const auto& left = *previous->second;
    if (mode == PmmCameraInterpolation::Playback && current->frame - 1 == left.frame) {
        output = CameraKeyPose(left);
        output.parentModel = left.parentModel;
        output.parentBone = left.parentBone;
        return true;
    }
    const float t = mode == PmmCameraInterpolation::Playback
                        ? (static_cast<float>(frame) - static_cast<float>(left.frame)) /
                              static_cast<float>(current->frame - left.frame)
                        : static_cast<float>((frame - static_cast<double>(left.frame)) /
                                             static_cast<double>(current->frame - left.frame));
    PmmEvaluatedCamera result;
    result.perspective = left.perspective;
    result.parentModel = left.parentModel;
    result.parentBone = left.parentBone;
    for (int axis = 0; axis < 3; ++axis) {
        const float eased = CameraEase(*current, axis, t);
        result.position[axis] =
            mode == PmmCameraInterpolation::Seek
                ? static_cast<float>(static_cast<double>(eased) *
                                         (static_cast<double>(current->eye[axis]) -
                                          static_cast<double>(left.eye[axis])) +
                                     static_cast<double>(left.eye[axis]))
                : eased * (current->eye[axis] - left.eye[axis]) + left.eye[axis];
    }
    const float rotationEase = CameraEase(*current, 3, t);
    for (int axis = 0; axis < 3; ++axis)
        result.rotation[axis] =
            mode == PmmCameraInterpolation::Seek
                ? static_cast<float>(static_cast<double>(rotationEase) *
                                         (static_cast<double>(current->target[axis]) -
                                          static_cast<double>(left.target[axis])) +
                                     static_cast<double>(left.target[axis]))
                : rotationEase * (current->target[axis] - left.target[axis]) + left.target[axis];
    const float distanceEase = CameraEase(*current, 4, t);
    const float fovEase = CameraEase(*current, 5, t);
    result.distance = mode == PmmCameraInterpolation::Seek
                          ? static_cast<float>(static_cast<double>(distanceEase) *
                                                   (static_cast<double>(current->distance) -
                                                    static_cast<double>(left.distance)) +
                                               static_cast<double>(left.distance))
                          : distanceEase * (current->distance - left.distance) + left.distance;
    result.fov =
        mode == PmmCameraInterpolation::Seek
            ? static_cast<float>(static_cast<double>(fovEase) *
                                     static_cast<double>(current->fov - left.fov) +
                                 left.fov)
            : fovEase * static_cast<float>(current->fov - left.fov) + static_cast<float>(left.fov);
    output = result;
    return true;
}

bool EvaluatePmmAccessoryTrack(const PmmKeyTrack<PmmAccessoryKey>& track, std::uint8_t slot,
                               double frame, PmmEvaluatedAccessory& output) {
    if (!std::isfinite(frame) || frame < 0.0 || track.dense.size() != 1)
        return false;
    IndexedTrack<PmmAccessoryKey> indexed(track);
    if (!indexed.valid)
        return false;
    const auto& head = track.dense[0];
    const PmmAccessoryKey* left = &head;
    const PmmAccessoryKey* right = &head;
    std::uint32_t index = 0;
    for (std::size_t steps = 0; static_cast<double>(right->frame) < frame && right->next; ++steps) {
        if (steps >= indexed.keys.size())
            return false;
        const auto next = indexed.keys.find(right->next);
        if (next == indexed.keys.end() || next->second->previous != index ||
            next->second->frame <= right->frame)
            return false;
        left = right;
        index = right->next;
        right = next->second;
    }
    if (static_cast<double>(right->frame) <= frame || right == &head ||
        static_cast<double>(right->frame) == frame)
        left = right;
    PmmEvaluatedAccessory pose;
    pose.slot = slot;
    pose.visible = left->visible;
    pose.shadowEnabled = left->shadowEnabled;
    pose.parentModel = left->parentModel;
    pose.parentBone = left->parentBone;
    pose.position = left->position;
    pose.rotation = left->rotation;
    pose.scale = left->scale;
    pose.opacity = left->opacity;
    if (left != right) {
        const float t = static_cast<float>((frame - static_cast<double>(left->frame)) /
                                           static_cast<double>(right->frame - left->frame));
        pose.scale = left->scale + t * (right->scale - left->scale);
        pose.opacity = left->opacity + t * (right->opacity - left->opacity);
    }
    output = pose;
    return true;
}

bool EvaluatePmmSelfShadowTrack(const PmmKeyTrack<PmmSelfShadowKey>& track, double frame,
                                std::uint8_t& mode, float& distance) {
    if (!std::isfinite(frame) || frame < 0.0 || track.dense.size() != 1)
        return false;
    IndexedTrack<PmmSelfShadowKey> indexed(track);
    if (!indexed.valid)
        return false;
    const PmmSelfShadowKey* held = &track.dense[0];
    std::uint32_t index = 0;
    for (std::size_t steps = 0; held->next && static_cast<double>(held->frame) < frame; ++steps) {
        if (steps >= indexed.keys.size())
            return false;
        const auto next = indexed.keys.find(held->next);
        if (next == indexed.keys.end() || next->second->previous != index ||
            next->second->frame <= held->frame)
            return false;
        if (static_cast<double>(next->second->frame) > frame)
            break;
        index = held->next;
        held = next->second;
    }
    mode = held->mode;
    distance = held->distance;
    return true;
}

bool EvaluatePmmGravityTrack(const PmmKeyTrack<PmmGravityKey>& track, double frame,
                             PmmEvaluatedGravity& output, bool frameSeek) {
    if (!std::isfinite(frame) || frame < 0.0 || track.dense.size() != 1)
        return false;
    IndexedTrack<PmmGravityKey> indexed(track);
    if (!indexed.valid)
        return false;
    const PmmGravityKey* left = &track.dense[0];
    const PmmGravityKey* right = left;
    std::uint32_t index = 0;
    for (std::size_t steps = 0; static_cast<double>(right->frame) < frame && right->next; ++steps) {
        if (steps >= indexed.keys.size())
            return false;
        const auto found = indexed.keys.find(right->next);
        if (found == indexed.keys.end() || found->second->previous != index ||
            found->second->frame <= right->frame)
            return false;
        left = right;
        index = right->next;
        right = found->second;
    }
    if (static_cast<double>(right->frame) <= frame)
        left = right;
    PmmEvaluatedGravity result;
    result.noiseEnabled = left->noiseEnabled;
    result.noise = left->noise;
    result.acceleration = left->acceleration;
    result.direction = left->direction;
    if (left != right) {
        if (frameSeek)
            result.noiseEnabled = right->noiseEnabled;
        const float fraction = (static_cast<float>(frame) - static_cast<float>(left->frame)) /
                               static_cast<float>(right->frame - left->frame);
        result.noise += static_cast<int>(static_cast<float>(right->noise - left->noise) * fraction);
        result.acceleration =
            (right->acceleration - left->acceleration) * fraction + left->acceleration;
        for (int axis = 0; axis < 3; ++axis)
            result.direction[axis] =
                (right->direction[axis] - left->direction[axis]) * fraction + left->direction[axis];
    }
    output = result;
    return true;
}

float EvaluatePmmBoneCurve(const std::uint8_t* interpolation, int channel, float t) {
    constexpr double kCpScale = 0.02362200058996677;
    constexpr float kCpScaleF = 0.02362200058996677f;
    const unsigned char x1 = interpolation[channel];
    const unsigned char y1 = interpolation[channel + 4];
    const unsigned char x2 = interpolation[channel + 8];
    const unsigned char y2 = interpolation[channel + 12];
    if (x2 == y2 && x1 == y1)
        return t; // linear curve (0x4A05DE)

#if defined(_M_IX86)
    // x86 0x4A05A0: double scale locals, x87-extended polynomial, float
    // u/step stack slots (the compare keeps the pre-store value).
    const double x1c = static_cast<double>(static_cast<signed char>(x1)) * kCpScale;
    const double x2c = static_cast<double>(static_cast<signed char>(x2)) * kCpScale;

    // 12 halving bisection steps on X(u) = t; u/step kept as float exactly
    // like the original stack slots (the step halves BEFORE the update).
    float u = 0.5f, step = 0.5f;
    for (int i = 0; i < 12; ++i) {
        const float om = 1.0f - u;
        const double xu = (double)om * (double)om * (double)u * x1c +
                          (double)om * (double)u * (double)u * x2c +
                          (double)u * (double)u * (double)u;
        if (xu == (double)t)
            break;
        step *= 0.5f;
        u = xu >= (double)t ? (float)(u - step) : (float)(u + step);
    }

    const float om = 1.0f - u;
    const double y1c = static_cast<double>(static_cast<signed char>(y1)) * kCpScale;
    const double y2c = static_cast<double>(static_cast<signed char>(y2)) * kCpScale;
    return static_cast<float>((double)u * ((double)u * (double)u) +
                              (double)om * (double)om * (double)u * y1c +
                              (double)om * (double)u * (double)u * y2c);
#else
    // x64 sub_7FF7CB4EDB00: single precision end to end - the byte*scale
    // products (mulss dword_7FF7CB552988 = 0x3CC182ED), the bisection
    // compare (ucomiss on float gx) and the tail, which multiplies the
    // raw y bytes first and applies the scale afterwards
    // (0x7FF7CB4EDBC0..0x7FF7CB4EDE2E).
    const float x1c = static_cast<float>(static_cast<signed char>(x1)) * kCpScaleF;
    const float x2c = static_cast<float>(static_cast<signed char>(x2)) * kCpScaleF;
    const float y1b = static_cast<float>(static_cast<signed char>(y1));
    const float y2b = static_cast<float>(static_cast<signed char>(y2));

    // 12 halving bisection steps on X(u) = t (the step halves BEFORE the
    // update: mulss xmm2, 0.5f at each unrolled block head).
    float u = 0.5f, step = 0.5f;
    for (int i = 0; i < 12; ++i) {
        const float om = 1.0f - u;
        const float gx = (om * om * u) * x1c + (om * u * u) * x2c + u * u * u;
        if (gx == t)
            break; // ucomiss
        step *= 0.5f;
        u = gx >= t ? u - step : u + step;
    }

    const float om = 1.0f - u;
    return (om * om * u) * y1b * kCpScaleF + (om * u * u) * y2b * kCpScaleF + u * u * u;
#endif
}

// Match the original float pi constant promoted to double, then halved.
constexpr double kSeekHalfPi = static_cast<double>(3.141592f) * 0.5;

// Keep the original clamp inside the acos domain.
const float kQuaternionClamp = 0.999999f;

void InterpolatePmmBoneSeekRotation(const float* previousRotation, const float* currentRotation,
                                    float rotationFraction, float* output) {
#if defined(_M_IX86)
    // Preserve float rounding between the double-precision stages.
    const double dotDouble = (double)previousRotation[0] * currentRotation[0] +
                             (double)previousRotation[1] * currentRotation[1] +
                             (double)previousRotation[2] * currentRotation[2] +
                             (double)previousRotation[3] * currentRotation[3];
    const float dot = (float)dotDouble;
    if (static_cast<float>(1.0 - (double)dot * (double)dot) == 0.0f) {
        for (int c = 0; c < 4; ++c)
            output[c] = previousRotation[c];
    } else {
        float clampedDot = dot;
        if (clampedDot > 1.0f)
            clampedDot = kQuaternionClamp;
        else if (clampedDot < -1.0f)
            clampedDot = -kQuaternionClamp;
        const float angle = (float)std::acos((double)clampedDot);
        if ((double)angle > kSeekHalfPi && (double)clampedDot < 0.0) {
            const float shortAngle = (float)std::acos(-(double)clampedDot);
            const float s = (float)std::sin((double)shortAngle);
            const float a0 = (float)((1.0 - (double)rotationFraction) * (double)shortAngle);
            const float previousSine = (float)std::sin((double)a0);
            const float previousWeight = (float)((double)previousSine / (double)s);
            const float endAngle = (float)((double)rotationFraction * (double)shortAngle);
            const float nextSine = (float)std::sin((double)endAngle);
            const float nextWeight = (float)((double)nextSine / (double)s);
            for (int c = 0; c < 4; ++c)
                output[c] = (float)((double)previousRotation[c] * (double)previousWeight -
                                    (double)nextWeight * (double)currentRotation[c]);
        } else {
            const float s = (float)std::sin((double)angle);
            const float a0 = (float)((1.0 - (double)rotationFraction) * (double)angle);
            const float previousSine = (float)std::sin((double)a0);
            const float previousWeight = (float)((double)previousSine / (double)s);
            const float endAngle = (float)((double)rotationFraction * (double)angle);
            const float nextSine = (float)std::sin((double)endAngle);
            const float nextWeight = (float)((double)nextSine / (double)s);
            for (int c = 0; c < 4; ++c)
                output[c] = (float)((double)previousRotation[c] * (double)previousWeight +
                                    (double)nextWeight * (double)currentRotation[c]);
        }
    }
#else
    const float dot =
        previousRotation[0] * currentRotation[0] + previousRotation[1] * currentRotation[1] +
        previousRotation[2] * currentRotation[2] + previousRotation[3] * currentRotation[3];
    if (1.0f - dot * dot == 0.0f) {
        for (int c = 0; c < 4; ++c)
            output[c] = previousRotation[c];
    } else {
        float clampedDot = dot;
        if (clampedDot > 1.0f)
            clampedDot = kQuaternionClamp;
        else if (clampedDot < -1.0f)
            clampedDot = -kQuaternionClamp;
        const float angle = std::acos(clampedDot);
        if (angle > 1.570796012878418f && clampedDot < 0.0f) {
            const float shortAngle = std::acos(-clampedDot);
            const float s = std::sin(shortAngle);
            const float previousWeight = std::sin((1.0f - rotationFraction) * shortAngle) / s;
            const float nextWeight = std::sin(rotationFraction * shortAngle) / s;
            for (int c = 0; c < 4; ++c)
                output[c] = previousRotation[c] * previousWeight - nextWeight * currentRotation[c];
        } else {
            const float s = std::sin(angle);
            const float previousWeight = std::sin((1.0f - rotationFraction) * angle) / s;
            const float nextWeight = std::sin(rotationFraction * angle) / s;
            for (int c = 0; c < 4; ++c)
                output[c] = previousRotation[c] * previousWeight + nextWeight * currentRotation[c];
        }
    }
#endif
}

bool SelectPmmDisplayKey(ReadPmmDisplayKeyLink read, void* context, double frame,
                         PmmDisplayTraversal traversal, PmmDisplayCursor& cursor,
                         PmmDisplaySelection& selection) {
    if (!read || !std::isfinite(frame) || frame < 0.0)
        return false;
    PmmDisplayCursor nextCursor = cursor;
    PmmDisplaySelection result;
    if (traversal != PmmDisplayTraversal::AdvancePlayback)
        nextCursor = {};
    else if (!nextCursor.active) {
        selection = result;
        return true;
    }
    PmmDisplayKeyLink key;
    if (!read(context, nextCursor.index, key))
        return false;
    for (std::uint32_t steps = 0; static_cast<double>(key.frame) < frame; ++steps) {
        if (steps >= 1000000)
            return false;
        if (key.next == 0) {
            result.kind = PmmDisplaySelectionKind::Terminal;
            result.heldIndex = result.cursorIndex = nextCursor.index;
            result.visible = key.visible;
            nextCursor.active = false;
            cursor = nextCursor;
            selection = result;
            return true;
        }
        PmmDisplayKeyLink following;
        if (!read(context, key.next, following) || following.previous != nextCursor.index ||
            following.frame <= key.frame)
            return false;
        nextCursor.index = key.next;
        key = following;
    }
    if (traversal == PmmDisplayTraversal::InitializePlayback) {
        cursor = nextCursor;
        selection = result;
        return true;
    }
    result.cursorIndex = nextCursor.index;
    if (static_cast<double>(key.frame) == frame) {
        result.kind = PmmDisplaySelectionKind::Exact;
        result.heldIndex = nextCursor.index;
        result.visible = key.visible;
    } else {
        PmmDisplayKeyLink previous;
        if (!read(context, key.previous, previous))
            return false;
        result.kind = PmmDisplaySelectionKind::Between;
        result.heldIndex = key.previous;
        result.visible = previous.visible;
    }
    cursor = nextCursor;
    selection = result;
    return true;
}

bool EvaluatePmmSelectorWindow(ReadPmmDisplayKeyLink readKey, ReadPmmSelectorPair readPair,
                               void* context, const PmmDisplaySelection& selection,
                               std::uint32_t selectorIndex, PmmEvaluatedSelector& output) {
    if (!readKey || !readPair || selection.kind == PmmDisplaySelectionKind::None)
        return false;
    PmmDisplayKeyLink heldKey;
    PmmRigidState heldPair;
    if (!readKey(context, selection.heldIndex, heldKey) ||
        !readPair(context, selection.heldIndex, selectorIndex, heldPair))
        return false;
    PmmEvaluatedSelector result;
    result.linkedModel = heldPair.modelIndex;
    result.linkedBone = heldPair.boneIndex;
    const auto samePair = [&](std::uint32_t keyIndex, bool& same) {
        PmmRigidState pair;
        if (!readPair(context, keyIndex, selectorIndex, pair))
            return false;
        same = pair.modelIndex == heldPair.modelIndex && pair.boneIndex == heldPair.boneIndex;
        return true;
    };
    std::uint32_t walk = selection.heldIndex;
    for (std::uint32_t steps = 0; steps < 1000000; ++steps) {
        PmmDisplayKeyLink key;
        if (!readKey(context, walk, key))
            return false;
        if (key.frame == 0) {
            result.windowStart = 0;
            break;
        }
        PmmDisplayKeyLink previous;
        if (!readKey(context, key.previous, previous) || previous.next != walk ||
            previous.frame >= key.frame)
            return false;
        bool same = false;
        if (!samePair(key.previous, same))
            return false;
        if (!same) {
            result.windowStart = key.frame;
            break;
        }
        walk = key.previous;
        if (steps == 999999)
            return false;
    }
    if (selection.kind == PmmDisplaySelectionKind::Terminal) {
        output = result;
        return true;
    }
    std::uint32_t forward = selection.cursorIndex;
    bool same = false;
    if (!samePair(forward, same))
        return false;
    if (!same) {
        PmmDisplayKeyLink key;
        if (!readKey(context, forward, key))
            return false;
        result.windowEnd = key.frame;
        output = result;
        return true;
    }
    for (std::uint32_t steps = 0; steps < 1000000; ++steps) {
        PmmDisplayKeyLink key;
        if (!readKey(context, forward, key))
            return false;
        if (key.next == 0) {
            result.windowEnd = 0;
            output = result;
            return true;
        }
        PmmDisplayKeyLink next;
        if (!readKey(context, key.next, next) || next.previous != forward ||
            next.frame <= key.frame)
            return false;
        forward = key.next;
        if (!samePair(forward, same))
            return false;
        if (!same) {
            result.windowEnd = next.frame;
            output = result;
            return true;
        }
    }
    return false;
}

PmmBoneSelectorResult SelectPmmBoneSegmentPose(std::uint32_t previousFrame, std::uint32_t nextFrame,
                                               double frame, const PmmEvaluatedSelector& selector) {
    // Physics may move the previous-frame cursor past the requested frame.
    // The native selector still resolves crossings against that cursor.
    if (previousFrame >= nextFrame)
        return PmmBoneSelectorResult::Unchanged;
    const auto boundary = [&](std::uint32_t value) {
        return value > previousFrame && value < nextFrame;
    };
    std::uint32_t crossing = 0;
    if (boundary(selector.windowStart))
        crossing = selector.windowStart;
    // The end boundary is inclusive at the next bone key. The native
    // selector branch snaps throughout that segment, even before the end.
    else if (selector.windowEnd > previousFrame && selector.windowEnd <= nextFrame)
        crossing = selector.windowEnd;
    if (!crossing)
        return PmmBoneSelectorResult::Unchanged;
    return frame >= static_cast<double>(crossing) ? PmmBoneSelectorResult::NextKey
                                                  : PmmBoneSelectorResult::PreviousKey;
}

PmmBoneSelectorResult SelectPmmBoneTerminalPose(std::uint32_t terminalFrame, double frame,
                                                const PmmEvaluatedSelector& selector) {
    return terminalFrame < selector.windowStart &&
                   frame >= static_cast<double>(selector.windowStart)
               ? PmmBoneSelectorResult::Reset
               : PmmBoneSelectorResult::Unchanged;
}

bool ApplyPmmBoneSelectorPose(PmmBoneSelectorResult decision, const PmmBoneKey& previous,
                              const PmmBoneKey& next, bool playback, PmmEvaluatedBone& pose) {
    PmmEvaluatedBone result;
    switch (decision) {
    case PmmBoneSelectorResult::PreviousKey:
        result = EvaluateBone(previous);
        break;
    case PmmBoneSelectorResult::NextKey:
        result = EvaluateBone(next);
        break;
    case PmmBoneSelectorResult::Reset:
        result = EvaluateBone(next);
        result.translation = {};
        result.rotation[0] = result.rotation[1] = result.rotation[2] = 0.0f;
        if (!playback)
            result.rotation[3] = 1.0f;
        break;
    case PmmBoneSelectorResult::Unchanged:
        return false;
    }
    pose = result;
    return true;
}

float InterpolatePmmMorphValue(float previousValue, float nextValue, std::uint32_t previousFrame,
                               std::uint32_t nextFrame, std::uint32_t frame) {
    const float fraction =
        static_cast<float>(frame - previousFrame) /
        static_cast<float>(static_cast<int>(nextFrame) - static_cast<int>(previousFrame));
    return fraction * (nextValue - previousValue) + previousValue;
}

float InterpolatePmmMorphValue(float previousValue, float nextValue, std::uint32_t previousFrame,
                               std::uint32_t nextFrame, double frame) {
    const float fraction =
        (static_cast<float>(frame) - static_cast<float>(previousFrame)) /
        static_cast<float>(static_cast<int>(nextFrame) - static_cast<int>(previousFrame));
    return fraction * (nextValue - previousValue) + previousValue;
}

bool InterpolatePmmBoneSegment(const PmmBoneKey& left, const PmmBoneKey& right, std::uint32_t frame,
                               PmmEvaluatedBone& output, PmmBoneEase ease, void* context) {
    if (right.frame <= left.frame || frame <= left.frame || frame >= right.frame)
        return false;
    PmmEvaluatedBone result;
    result.physicsDisabled = left.physicsDisabled;
    const float t =
        static_cast<float>(frame - left.frame) /
        static_cast<float>(static_cast<int>(right.frame) - static_cast<int>(left.frame));
    const auto curve = [&](int channel) {
        return ease ? ease(context, channel, t) : BoneEase(right, channel, t);
    };
    const float fraction = curve(3);
    for (int axis = 0; axis < 3; ++axis) {
        const float difference = right.position[axis] - left.position[axis];
        if (difference == 0.0f) {
            result.translation[axis] = left.position[axis];
        } else {
            const float eased = curve(axis);
#if defined(_M_IX86)
            result.translation[axis] = static_cast<float>(static_cast<double>(eased) * difference +
                                                          static_cast<double>(left.position[axis]));
#else
            // x64 RVA 0xCD5E8/0xCD5EC: mulss rounds before addss.
            result.translation[axis] = eased * difference + left.position[axis];
#endif
        }
    }
    InterpolatePmmBoneSeekRotation(left.rotation.data(), right.rotation.data(), fraction,
                                   result.rotation.data());
    output = result;
    return true;
}

bool InterpolatePmmBonePlaybackSegment(const PmmBoneKey& left, const PmmBoneKey& right,
                                       double frame, PmmEvaluatedBone& output) {
    if (right.frame <= left.frame || frame <= left.frame || frame >= right.frame)
        return false;
    PmmEvaluatedBone result;
    result.physicsDisabled = left.physicsDisabled;
    const float t =
        (static_cast<float>(frame) - static_cast<float>(left.frame)) /
        static_cast<float>(static_cast<int>(right.frame) - static_cast<int>(left.frame));
    const float fraction = BoneEase(right, 3, t);
    const float dot = left.rotation[0] * right.rotation[0] + left.rotation[1] * right.rotation[1] +
                      left.rotation[2] * right.rotation[2] + left.rotation[3] * right.rotation[3];
    if (1.0f - dot * dot == 0.0f) {
        result.rotation = left.rotation;
    } else {
        float clamped = dot;
        if (clamped > 1.0f)
            clamped = 0.999999f;
        else if (clamped < -1.0f)
            clamped = -0.999999f;
        const float angle = std::acos(clamped);
        if (angle > 1.570796012878418f && clamped < 0.0f) {
            const float shortest = std::acos(-clamped);
            const float reciprocal = 1.0f / std::sin(shortest);
            const float w0 = std::sin((1.0f - fraction) * shortest) * reciprocal;
            const float w1 = std::sin(fraction * shortest) * reciprocal;
            for (int i = 0; i < 4; ++i)
                result.rotation[i] = left.rotation[i] * w0 - w1 * right.rotation[i];
        } else {
            const float reciprocal = 1.0f / std::sin(angle);
            const float w0 = std::sin((1.0f - fraction) * angle) * reciprocal;
            const float w1 = std::sin(fraction * angle) * reciprocal;
            for (int i = 0; i < 4; ++i)
                result.rotation[i] = left.rotation[i] * w0 + w1 * right.rotation[i];
        }
    }
    for (int axis = 0; axis < 3; ++axis) {
        const float difference = right.position[axis] - left.position[axis];
        result.translation[axis] =
            difference == 0.0f ? left.position[axis]
                               : BoneEase(right, axis, t) * difference + left.position[axis];
    }
    output = result;
    return true;
}

bool InterpolatePmmLightSegment(const PmmLightKey& left, const PmmLightKey& right, double frame,
                                PmmLightInterpolation mode, std::array<float, 6>& output) {
    if (right.frame <= left.frame || frame <= left.frame || frame >= right.frame)
        return false;
    std::array<float, 6> result{};
    const float fraction = (static_cast<float>(frame) - static_cast<float>(left.frame)) /
                           static_cast<float>(right.frame - left.frame);
    const double preciseFraction =
        (frame - static_cast<double>(left.frame)) / static_cast<double>(right.frame - left.frame);
    for (int i = 0; i < 3; ++i) {
        if (mode == PmmLightInterpolation::FrameSeek) {
            result[i] = static_cast<float>((right.color[i] - left.color[i]) * preciseFraction +
                                           left.color[i]);
            result[i + 3] = static_cast<float>(
                (right.direction[i] - left.direction[i]) * preciseFraction + left.direction[i]);
        } else {
            result[i] = fraction * (right.color[i] - left.color[i]) + left.color[i];
            result[i + 3] = fraction * (right.direction[i] - left.direction[i]) + left.direction[i];
        }
    }
    output = result;
    return true;
}

bool EvaluatePmmLightTrack(const PmmKeyTrack<PmmLightKey>& track, double frame,
                           PmmLightInterpolation mode, std::array<float, 6>& output) {
    if (!std::isfinite(frame) || frame < 0 || frame > 4294967295.0 || track.dense.size() != 1)
        return false;
    IndexedTrack<PmmLightKey> indexed(track);
    Bracket<PmmLightKey> bracket;
    for (const auto& item : indexed.keys)
        for (unsigned i = 0; i < 3; ++i)
            if (!std::isfinite(item.second->color[i]) || !std::isfinite(item.second->direction[i]))
                return false;
    Bracket<PmmLightKey> terminal;
    if (!indexed.Find(0, 4294967295.0, terminal))
        return false;
    if (!indexed.Find(0, frame, bracket))
        return false;
    if (bracket.left != bracket.right)
        return InterpolatePmmLightSegment(*bracket.left, *bracket.right, frame, mode, output);
    for (unsigned i = 0; i < 3; ++i) {
        output[i] = bracket.right->color[i];
        output[i + 3] = bracket.right->direction[i];
    }
    return true;
}

bool EvaluatePmmSceneFrame(const PmmSceneDocument& scene, std::uint32_t frame,
                           PmmSceneEvaluation& evaluation) {
    PmmSceneEvaluation result;
    result.frame = frame;
    if (!EvaluatePmmCameraTrack(scene.cameraKeys, frame, PmmCameraInterpolation::Seek,
                                result.camera))
        return false;
    IndexedTrack<PmmLightKey> lightTrack(scene.lightKeys);
    Bracket<PmmLightKey> light;
    if (!lightTrack.Find(0, frame, light))
        return false;
    if (light.left == light.right) {
        for (int i = 0; i < 3; ++i) {
            result.lightState[i] = light.right->color[i];
            result.lightState[i + 3] = light.right->direction[i];
        }
    } else {
        if (!InterpolatePmmLightSegment(*light.left, *light.right, static_cast<double>(frame),
                                        PmmLightInterpolation::Playback, result.lightState))
            return false;
    }
    result.selfShadowMode = scene.selfShadowMode;
    result.selfShadowDistance = scene.physicsInterval;
    result.gravity = {scene.physics.gravityNoiseEnabled, scene.physics.gravityNoise,
                      scene.physics.gravityMagnitude, scene.physics.gravityDirection};
    if (!scene.gravityKeys.dense.empty() &&
        !EvaluatePmmGravityTrack(scene.gravityKeys, frame, result.gravity, true))
        return false;
    if (!scene.selfShadowKeys.dense.empty() &&
        !EvaluatePmmSelfShadowTrack(scene.selfShadowKeys, frame, result.selfShadowMode,
                                    result.selfShadowDistance))
        return false;
    result.models.reserve(scene.models.size());
    for (const auto& model : scene.models) {
        PmmEvaluatedModel output;
        output.slot = model.slot;
        IndexedTrack<PmmBoneKey> boneTrack(model.boneKeys);
        output.bones.reserve(model.layout.boneCount);
        for (std::size_t i = 0; i < model.layout.boneCount; ++i) {
            Bracket<PmmBoneKey> bone;
            if (!boneTrack.Find(i, frame, bone))
                return false;
            PmmEvaluatedBone evaluatedBone;
            if (bone.left == bone.right)
                evaluatedBone = EvaluateBone(*bone.right);
            else if (!InterpolatePmmBoneSegment(*bone.left, *bone.right, frame, evaluatedBone))
                return false;
            output.bones.push_back(evaluatedBone);
        }
        IndexedTrack<PmmMorphKey> morphTrack(model.morphKeys);
        output.morphValues.reserve(model.layout.morphCount);
        for (std::size_t i = 0; i < model.layout.morphCount; ++i) {
            Bracket<PmmMorphKey> morph;
            if (!morphTrack.Find(i, frame, morph))
                return false;
            float value = morph.right->value;
            if (morph.left != morph.right) {
                value = InterpolatePmmMorphValue(morph.left->value, morph.right->value,
                                                 morph.left->frame, morph.right->frame, frame);
            }
            output.morphValues.push_back(value);
        }
        IndexedTrack<PmmDisplayKey> displayTrack(model.displayKeys);
        if (!displayTrack.valid || model.displayKeys.dense.empty())
            return false;
        const auto readDisplay = [](void* context, std::uint32_t index,
                                    PmmDisplayKeyLink& link) -> bool {
            const auto& track = *static_cast<IndexedTrack<PmmDisplayKey>*>(context);
            const auto found = track.keys.find(index);
            if (found == track.keys.end())
                return false;
            const auto& key = *found->second;
            link = {key.frame, key.previous, key.next, key.visible};
            return true;
        };
        PmmDisplayCursor displayCursor;
        PmmDisplaySelection displaySelection;
        if (!SelectPmmDisplayKey(readDisplay, &displayTrack, static_cast<double>(frame),
                                 PmmDisplayTraversal::Seek, displayCursor, displaySelection))
            return false;
        const PmmDisplayKey* held = displayTrack.keys.at(displaySelection.heldIndex);
        output.visible = displaySelection.visible;
        if (held->ikEnabled.size() != model.layout.ikCount)
            return false;
        output.ikEnabled = held->ikEnabled;
        const auto readPair = [](void* context, std::uint32_t keyIndex, std::uint32_t selectorIndex,
                                 PmmRigidState& pair) -> bool {
            const auto& track = *static_cast<IndexedTrack<PmmDisplayKey>*>(context);
            const auto found = track.keys.find(keyIndex);
            if (found == track.keys.end() || selectorIndex >= found->second->rigidStates.size())
                return false;
            pair = found->second->rigidStates[selectorIndex];
            return true;
        };
        output.selectors.reserve(model.rigidCount);
        for (std::uint32_t i = 0; i < model.rigidCount; ++i) {
            PmmEvaluatedSelector selector;
            if (!EvaluatePmmSelectorWindow(readDisplay, readPair, &displayTrack, displaySelection,
                                           i, selector))
                return false;
            output.selectors.push_back(selector);
        }
        if (model.rigidSources.size() != output.selectors.size())
            return false;
        for (std::size_t i = 0; i < output.selectors.size(); ++i) {
            const auto sourceBone = model.rigidSources[i];
            if (sourceBone < 0)
                continue; // root selector has no bone pose
            if (static_cast<std::uint32_t>(sourceBone) >= model.layout.boneCount)
                return false;
            Bracket<PmmBoneKey> bone;
            if (!boneTrack.Find(static_cast<std::size_t>(sourceBone), frame, bone))
                return false;
            PmmBoneSelectorResult decision = PmmBoneSelectorResult::Unchanged;
            if (bone.left != bone.right)
                decision = SelectPmmBoneSegmentPose(bone.left->frame, bone.right->frame, frame,
                                                    output.selectors[i]);
            else if (frame > bone.right->frame)
                decision = SelectPmmBoneTerminalPose(bone.right->frame, frame, output.selectors[i]);
            ApplyPmmBoneSelectorPose(decision, *bone.left, *bone.right, false,
                                     output.bones[sourceBone]);
        }
        result.models.push_back(std::move(output));
    }
    result.accessories.reserve(scene.accessories.size());
    for (const auto& accessory : scene.accessories) {
        PmmEvaluatedAccessory pose;
        if (!EvaluatePmmAccessoryTrack(accessory.keyRecords, accessory.slot, frame, pose))
            return false;
        result.accessories.push_back(pose);
    }
    evaluation = std::move(result);
    return true;
}
}
