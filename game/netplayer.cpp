#include "netplayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace multislot {

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Length(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

namespace {

template <typename T>
void Store(std::uint8_t*& at, T value) {
    std::memcpy(at, &value, sizeof(value));
    at += sizeof(value);
}
template <typename T>
T Load(const std::uint8_t*& at) {
    T value{};
    std::memcpy(&value, at, sizeof(value));
    at += sizeof(value);
    return value;
}
void StoreVec(std::uint8_t*& at, Vec3 v) {
    Store(at, v.x);
    Store(at, v.y);
    Store(at, v.z);
}
Vec3 LoadVec(const std::uint8_t*& at) {
    Vec3 v;
    v.x = Load<float>(at);
    v.y = Load<float>(at);
    v.z = Load<float>(at);
    return v;
}
bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

std::uint64_t Field(const void* object, std::size_t offset) {
    std::uint64_t value = 0;
    std::memcpy(&value, static_cast<const std::uint8_t*>(object) + offset, sizeof(value));
    return value;
}
void SetField(void* object, std::size_t offset, std::uint64_t value) {
    std::memcpy(static_cast<std::uint8_t*>(object) + offset, &value, sizeof(value));
}

Vec3 ClampLength(Vec3 v, float limit) {
    const float length = Length(v);
    return length > limit && length > 0.0f ? v * (limit / length) : v;
}

}  // namespace

std::size_t EncodePlayerBlock(const PlayerSample& sample, std::uint8_t* out, std::size_t capacity) {
    if (!out || capacity < kPlayerBlockBytes) return 0;
    std::uint8_t* at = out;
    Store(at, kPlayerBlockVersion);
    Store(at, sample.seq);
    Store(at, sample.senderMs);
    StoreVec(at, sample.position);
    StoreVec(at, sample.velocity);
    return static_cast<std::size_t>(at - out);
}

bool DecodePlayerBlock(const std::uint8_t* in, std::size_t size, PlayerSample& out) {
    if (!in || size < kPlayerBlockBytes || in[0] != kPlayerBlockVersion) return false;
    const std::uint8_t* at = in + 1;
    PlayerSample sample;
    sample.seq = Load<std::uint16_t>(at);
    sample.senderMs = Load<std::uint32_t>(at);
    sample.position = LoadVec(at);
    sample.velocity = LoadVec(at);
    if (!Finite(sample.position) || !Finite(sample.velocity)) return false;
    out = sample;
    return true;
}

bool WriteBinElement(void* serialize, const std::uint8_t* data, std::size_t size) {
    if (!serialize || (size && !data) || size > 0x1FFF) return false;
    const std::uint64_t length = Field(serialize, kSerializeEnd);
    if (length > kSerializeCapacity || kSerializeCapacity - length < size + 2) return false;
    auto* bytes = static_cast<std::uint8_t*>(serialize) + kSerializeData + length;
    bytes[0] = static_cast<std::uint8_t>(0xA0 | (size >> 8 & 0x1F));
    bytes[1] = static_cast<std::uint8_t>(size & 0xFF);
    if (size) std::memcpy(bytes + 2, data, size);
    SetField(serialize, kSerializeEnd, length + 2 + size);
    return true;
}

bool ReadBinElement(void* serialize, std::uint8_t* out, std::size_t capacity, std::size_t& size) {
    if (!serialize) return false;
    const std::uint64_t at = Field(serialize, kSerializeReadPos);
    const std::uint64_t end = Field(serialize, kSerializeEnd);
    if (end > kSerializeCapacity || at > end || end - at < 2) return false;
    const auto* bytes = static_cast<const std::uint8_t*>(serialize) + kSerializeData + at;
    if ((bytes[0] & 0xF0) != 0xA0) return false;  // a bin element of a non-negative length
    const std::size_t length = static_cast<std::size_t>((bytes[0] & 0x0F) << 8 | bytes[1]);
    if (end - at - 2 < length || length > capacity || (length && !out)) return false;
    if (length) std::memcpy(out, bytes + 2, length);
    size = length;
    SetField(serialize, kSerializeReadPos, at + 2 + length);
    return true;
}

PlayerSample MakeSample(SendTrack& track, Vec3 position, double nowMs) {
    PlayerSample sample;
    sample.seq = ++track.seq;
    sample.senderMs = static_cast<std::uint32_t>(static_cast<std::uint64_t>(nowMs));
    sample.position = position;
    // The newest write at least kVelocityWindowMs ago, or the oldest one when none is that old yet; writes further
    // back than kMaxVelocityGapMs (a pause) are left out, so a resumed sender starts afresh.
    std::size_t from = SendTrack::kHistory;
    for (std::size_t i = 0; i < track.count; ++i) {
        const std::size_t at = (track.next + SendTrack::kHistory - track.count + i) % SendTrack::kHistory;  // oldest first
        const double age = nowMs - track.times[at];
        if (age > kMaxVelocityGapMs || age < 1.0) continue;
        if (from == SendTrack::kHistory || age >= kVelocityWindowMs) from = at;
    }
    if (from != SendTrack::kHistory) {
        const Vec3 moved = position - track.positions[from];
        if (Length(moved) <= kWarpDistance) {
            const Vec3 velocity = moved * static_cast<float>(1000.0 / (nowMs - track.times[from]));
            sample.velocity = Length(velocity) <= kMaxSentSpeed ? velocity : Vec3{};
        }
    }
    track.positions[track.next] = position;
    track.times[track.next] = nowMs;
    track.next = (track.next + 1) % SendTrack::kHistory;
    track.count = (std::min)(track.count + 1, SendTrack::kHistory);
    return sample;
}

bool TrackFresh(const RemoteTrack& track, double nowMs, const NetPlayerParams& params) {
    return track.have && nowMs - track.receivedMs <= params.staleMs;
}

bool AcceptSample(RemoteTrack& track, const PlayerSample& sample, double localMs, const NetPlayerParams& params) {
    const bool fresh = TrackFresh(track, localMs, params);
    if (fresh && static_cast<std::int16_t>(sample.seq - track.last.seq) <= 0) return false;
    if (!fresh) {
        track = RemoteTrack{};
        track.senderTime = sample.senderMs;
        track.offset = localMs - track.senderTime;
    } else {
        track.senderTime += static_cast<std::int32_t>(sample.senderMs - track.last.senderMs);
        // The fastest sample sets the baseline (clock offset plus the shortest one-way delay); a slower path pulls
        // it up only slowly, so jitter does not move it.
        const double delay = localMs - track.senderTime;
        track.offset = delay < track.offset ? delay : (std::min)(track.offset + params.clockCreepMs, delay);
    }
    track.have = true;
    track.last = sample;
    track.receivedMs = localMs;
    return true;
}

double SampleAgeMs(const RemoteTrack& track, double nowMs) {
    return (std::max)(nowMs - (track.senderTime + track.offset), 0.0);
}

float WarpDistance(const RemoteTrack& track, const NetPlayerParams& params) {
    return (std::max)(params.snapDistance, Length(track.last.velocity) * params.warpLeadSeconds);
}

Vec3 EstimatePosition(const RemoteTrack& track, double nowMs, const NetPlayerParams& params) {
    const double age = (std::min)(SampleAgeMs(track, nowMs), static_cast<double>(params.maxExtrapolateMs));
    return track.last.position + track.last.velocity * static_cast<float>(age / 1000.0);
}

void ResetSteps(RemoteTrack& track) {
    track.stepped = false;
    track.lastAdded = {};
    track.ownVelocity = {};
    track.windowCount = track.windowNext = 0;
}

RemoteStep StepRemote(RemoteTrack& track, Vec3 current, double nowMs, const NetPlayerParams& params) {
    RemoteStep step;
    step.target = EstimatePosition(track, nowMs, params);
    if (!Finite(current)) {  // the copy's own position is broken: adding to it would spread the NaN, so add nothing
        ResetSteps(track);
        return step;
    }
    const Vec3 error = step.target - current;
    if (Length(error) > WarpDistance(track, params)) {
        step.warp = true;
        ResetSteps(track);
        return step;
    }
    const double elapsedMs = track.stepped ? nowMs - track.lastStepMs : 1000.0 / 60.0;
    // Called again before the physics stepped: nothing moved to measure, and the last request is still pending.
    if (track.stepped && elapsedMs < kMinStepMs) return step;
    const double dtMs = std::clamp(elapsedMs, kMinStepMs, 50.0);
    const float dt = static_cast<float>(dtMs / 1000.0);
    // The estimate moves with the sample's velocity only until the extrapolation limit, and so does the copy: this
    // step gets the velocity for what is left of that time, none past it.
    const double runMs = std::clamp(static_cast<double>(params.maxExtrapolateMs) - SampleAgeMs(track, nowMs), 0.0, dtMs);
    const Vec3 wanted = track.last.velocity * static_cast<float>(runMs / 1000.0);
    if (track.stepped) {
        // The copy's own motion: what it did beyond the requests the physics applied, over the last few steps. The
        // physics applies a request in full, in part (a slope, a wall) or not at all (a landing, a stagger, the
        // ground under a downward request), and never the opposite way. Taking every request as applied read a
        // blocked one as the copy moving against it, and the feed-forward answered with a larger request: the copy
        // of a player standing still was shoved +-35 cm at 30 Hz, and a landed copy was pushed at the step cap
        // (the host's motion traces of 2026-10-22). A standing copy whose requests are blocked and a running copy
        // braked to a standstill look the same in one step (nothing moves either way), so the estimate so far
        // decides: a copy that was moving against the request is being braked, and the request counts as applied;
        // otherwise only what moved along the request was applied. Per axis, as the ground and most walls block.
        // Summed over the window, so a push the ground undoes the next frame cancels instead of reading as motion
        // either way; measured over the actual time, hitches included: a per-step displacement mistakes a longer
        // frame for a faster copy.
        track.movedWindow[track.windowNext] = current - track.lastPosition;
        track.addedWindow[track.windowNext] = track.lastAdded;
        track.elapsedWindow[track.windowNext] = elapsedMs;
        track.windowNext = (track.windowNext + 1) % RemoteTrack::kStepWindow;
        track.windowCount = (std::min)(track.windowCount + 1, RemoteTrack::kStepWindow);
        Vec3 moved, added;
        double overMs = 0.0;
        for (std::size_t i = 0; i < track.windowCount; ++i) {
            moved = moved + track.movedWindow[i];
            added = added + track.addedWindow[i];
            overMs += track.elapsedWindow[i];
        }
        const auto axis = [](float m, float a, float prior) {
            if (a == 0.0f || prior * a < 0.0f) return m - a;
            const float sign = a < 0.0f ? -1.0f : 1.0f;
            return m - sign * std::clamp(m * sign, 0.0f, std::fabs(a));
        };
        const Vec3 own = ClampLength(Vec3{axis(moved.x, added.x, track.ownVelocity.x), axis(moved.y, added.y, track.ownVelocity.y),
                                          axis(moved.z, added.z, track.ownVelocity.z)} *
                                         static_cast<float>(1000.0 / overMs),
                                     kMaxOwnSpeed);
        track.ownVelocity = track.ownVelocity * 0.5f + own * 0.5f;
    } else {
        track.ownVelocity = wanted * (1.0f / dt);  // nothing measured yet: assume it already moves right
    }
    const float gain = 1.0f - std::exp(-static_cast<float>(dtMs) / (std::max)(params.tauMs, 1.0f));
    const Vec3 converge = Length(error) < params.deadband ? Vec3{} : error * gain;
    const Vec3 feed = (wanted - track.ownVelocity * dt) * std::clamp(params.feedForward, 0.0f, 1.0f);
    step.add = ClampLength(converge + feed, params.maxSpeed * dt);
    track.stepped = true;
    track.lastPosition = current;
    track.lastAdded = step.add;
    track.lastStepMs = nowMs;
    return step;
}

int EffectiveFlushIntervalMs(int configuredMs, bool playerSyncOnLocally) {
    if (!playerSyncOnLocally || configuredMs <= 0 || configuredMs == 90) return 0;
    return std::clamp(configuredMs, 16, 90);
}

}  // namespace multislot
