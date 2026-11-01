// Player sync without the game (netplayer.h): the block format, the game's bin element inside an
// eos::Serialize-shaped buffer, the sender's velocity, and a simulated remote copy driven by StepRemote through
// latency, jitter, a jump, a stop and a warp. The game's own Serialize functions are exercised against the same
// format by GameNet_playersync.
//   NetPlayerTests
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../game/netfeature.h"
#include "../../game/netplayer.h"
#include "../../game/netplayer_tracks.h"

using namespace multislot;

namespace {

int checks = 0, failures = 0;
void Check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) {
        std::printf("FAIL: %s\n", label.c_str());
        ++failures;
    }
}
bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
bool Same(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

struct alignas(16) FakeSerialize {
    unsigned char bytes[0x600]{};
    void SetEnd(std::uint64_t end) { std::memcpy(bytes + kSerializeEnd, &end, sizeof(end)); }
    void SetPos(std::uint64_t pos) { std::memcpy(bytes + kSerializeReadPos, &pos, sizeof(pos)); }
    std::uint64_t End() const {
        std::uint64_t v = 0;
        std::memcpy(&v, bytes + kSerializeEnd, sizeof(v));
        return v;
    }
    std::uint64_t Pos() const {
        std::uint64_t v = 0;
        std::memcpy(&v, bytes + kSerializeReadPos, sizeof(v));
        return v;
    }
};

void TestBlock() {
    PlayerSample sample;
    sample.seq = 0xBEEF;
    sample.senderMs = 0xFFFFFFF0u;
    sample.position = {1234.5678f, -12.25f, 987.0625f};
    sample.velocity = {6.5f, 9.8f, -3.25f};
    std::uint8_t block[64]{};
    Check(EncodePlayerBlock(sample, block, kPlayerBlockBytes - 1) == 0, "encode refuses a short buffer");
    Check(EncodePlayerBlock(sample, block, sizeof(block)) == kPlayerBlockBytes, "encode writes kPlayerBlockBytes");
    PlayerSample out;
    Check(DecodePlayerBlock(block, kPlayerBlockBytes, out), "decode");
    Check(out.seq == sample.seq && out.senderMs == sample.senderMs && Same(out.position, sample.position) &&
              Same(out.velocity, sample.velocity),
          "decode gives back every field exactly (full precision, unlike the game's half floats)");
    Check(!DecodePlayerBlock(block, kPlayerBlockBytes - 1, out), "decode refuses a short block");
    block[0] = 2;
    Check(!DecodePlayerBlock(block, kPlayerBlockBytes, out), "decode refuses another version");
    block[0] = kPlayerBlockVersion;
    const float nan = std::nanf("");
    std::memcpy(block + 7, &nan, sizeof(nan));
    Check(!DecodePlayerBlock(block, kPlayerBlockBytes, out), "decode refuses a non-finite position");
}

void TestBin() {
    FakeSerialize s;
    s.SetEnd(5);  // other fields before ours
    const std::uint8_t data[31] = {1, 2, 3};
    Check(WriteBinElement(s.bytes, data, sizeof(data)), "bin written");
    Check(s.End() == 5 + 2 + 31, "bin length is header + data");
    Check(s.bytes[kSerializeData + 5] == 0xA0 && s.bytes[kSerializeData + 6] == 31, "bin tag as EDF+12B5200 writes it");
    s.SetPos(5);
    std::uint8_t out[64]{};
    std::size_t size = 0;
    Check(ReadBinElement(s.bytes, out, sizeof(out), size) && size == 31 && out[2] == 3, "bin read back");
    Check(s.Pos() == s.End(), "read moved to the end");
    Check(!ReadBinElement(s.bytes, out, sizeof(out), size), "nothing left to read");
    // A truncated element, a wrong tag and a too small buffer move nothing.
    s.SetPos(5);
    s.SetEnd(5 + 2 + 30);
    Check(!ReadBinElement(s.bytes, out, sizeof(out), size) && s.Pos() == 5, "truncated element refused");
    s.SetEnd(5 + 2 + 31);
    Check(!ReadBinElement(s.bytes, out, 30, size) && s.Pos() == 5, "element larger than the buffer refused");
    s.bytes[kSerializeData + 5] = 0xC2;  // the game's full float tag
    Check(!ReadBinElement(s.bytes, out, sizeof(out), size) && s.Pos() == 5, "another element type refused");
    FakeSerialize full;
    full.SetEnd(kSerializeCapacity - 10);
    Check(!WriteBinElement(full.bytes, data, sizeof(data)) && full.End() == kSerializeCapacity - 10,
          "a write that does not fit writes nothing");
}

void TestSender() {
    SendTrack track;
    PlayerSample a = MakeSample(track, {0, 0, 0}, 1000.0);
    Check(a.seq == 1 && Length(a.velocity) == 0.0f, "first sample: no velocity");
    PlayerSample b = MakeSample(track, {0.2f, 0.1f, 0}, 1033.3333);
    Check(b.seq == 2 && Near(b.velocity.x, 6.0f, 0.01f) && Near(b.velocity.y, 3.0f, 0.01f),
          "one write so far: velocity over its 33 ms");
    PlayerSample c = MakeSample(track, {50.0f, 0.1f, 0}, 1066.6666);
    Check(Length(c.velocity) == 0.0f, "a warp carries no velocity");
    MakeSample(track, {50.0f, 0.1f, 0}, 2000.0);
    PlayerSample d = MakeSample(track, {51.0f, 0.1f, 0}, 2600.0);
    Check(Length(d.velocity) == 0.0f, "a long gap carries no velocity");
    Check(d.senderMs == 2600u, "sender time in ms");
    // The velocity covers the interval the receivers get blocks at (one write in three, 100 ms), not the one write
    // since the last: a dash that starts 33 ms before a write.
    {
        SendTrack t;
        double now = 5000.0;
        float x = 0.0f;
        for (int i = 0; i < 6; ++i, now += 33.3333, x += 0.2f) MakeSample(t, {x, 0, 0}, now);  // 6 m/s
        PlayerSample run = MakeSample(t, {x, 0, 0}, now);
        Check(Near(run.velocity.x, 6.0f, 0.05f), "steady running: 6 m/s over the window (" + std::to_string(run.velocity.x) + ")");
        now += 33.3333;
        x += 0.2f;
        MakeSample(t, {x, 0, 0}, now);
        now += 33.3333;
        x += 3.0f;  // 90 m/s for one write: a Fencer's dash starting
        PlayerSample dash = MakeSample(t, {x, 0, 0}, now);
        Check(dash.velocity.x > 25.0f && dash.velocity.x < 45.0f,
              "a dash start is the window's average, not 0 for passing the cap in one write (" +
                  std::to_string(dash.velocity.x) + ")");
        now += 33.3333;
        x += 3.0f;
        now += 33.3333;
        x += 3.0f;
        PlayerSample dashing = MakeSample(t, {x, 0, 0}, now);
        Check(dashing.velocity.x > 80.0f && dashing.velocity.x <= kMaxSentSpeed,
              "a 90 m/s dash is sent as such (" + std::to_string(dashing.velocity.x) + ")");
        // After a pause the old writes say nothing: the first write after it carries no velocity, the next one
        // measures from the resumed writes only.
        now += 1000.0;
        PlayerSample resumed = MakeSample(t, {x, 0, 0}, now);
        Check(Length(resumed.velocity) == 0.0f, "first write after a pause: no velocity");
        now += 33.3333;
        PlayerSample next = MakeSample(t, {x + 0.2f, 0, 0}, now);
        Check(Near(next.velocity.x, 6.0f, 0.05f), "the write after measures from the resumed writes");
    }
}

void TestAcceptAndClock() {
    NetPlayerParams p;
    RemoteTrack t;
    PlayerSample s;
    s.seq = 10;
    s.senderMs = 0xFFFFFF00u;
    Check(AcceptSample(t, s, 5000.0, p), "first sample");
    s.seq = 10;
    Check(!AcceptSample(t, s, 5010.0, p), "repeated seq dropped");
    s.seq = 9;
    Check(!AcceptSample(t, s, 5020.0, p), "older seq dropped");
    s.seq = 11;
    s.senderMs = 0xFFFFFF00u + 0x100u;  // wraps
    Check(AcceptSample(t, s, 5000.0 + 256.0 + 30.0, p), "newer seq across the sender clock wrap");
    // offset: first 5000 - senderTime; second delay 30 ms more than the first, so the baseline creeps 0.5 ms
    Check(Near(static_cast<float>(t.offset - (5000.0 - static_cast<double>(0xFFFFFF00u))), p.clockCreepMs, 0.01f),
          "a slower sample moves the latency baseline only by the creep");
    s.seq = 12;
    s.senderMs += 100;
    AcceptSample(t, s, 5000.0 + 356.0 - 10.0, p);
    Check(Near(static_cast<float>(t.offset - (5000.0 - static_cast<double>(0xFFFFFF00u))), -10.0f, 0.01f),
          "a faster sample sets the baseline");
    s.seq = 1;  // a new life of that player after a pause: the old sequence does not matter
    Check(AcceptSample(t, s, 9000.0, p) && t.last.seq == 1, "stale track starts over");
    Check(TrackFresh(t, 9000.0 + p.staleMs, p) && !TrackFresh(t, 9001.0 + p.staleMs, p), "freshness window");
}

void TestEstimate() {
    NetPlayerParams p;
    RemoteTrack t;
    PlayerSample s;
    s.seq = 1;
    s.senderMs = 100000;
    s.position = {10, 0, 0};
    s.velocity = {5, 0, 0};
    AcceptSample(t, s, 200000.0, p);
    Check(Near(EstimatePosition(t, 200000.0, p).x, 10.0f, 1e-4f), "estimate at arrival is the sample");
    Check(Near(EstimatePosition(t, 200100.0, p).x, 10.5f, 1e-3f), "100 ms later: +0.5 m");
    Check(Near(EstimatePosition(t, 201000.0, p).x, 11.75f, 1e-3f), "extrapolation capped at maxExtrapolateMs");
    s.seq = 2;
    s.senderMs = 100050;
    s.velocity = {};
    s.position = {10.25f, 0, 0};
    AcceptSample(t, s, 200050.0, p);
    Check(Near(EstimatePosition(t, 200150.0, p).x, 10.25f, 1e-4f), "a stopped player is estimated where it stopped");
}

// The remote copy as the game moves it: its own motion (from replicated input, possibly wrong) plus what StepRemote
// adds each frame. The true player sends samples every `sendMs`, which arrive `latencyMs` (+ jitter) later.
struct Sim {
    NetPlayerParams p;
    RemoteTrack track;
    double now = 0.0;
    Vec3 copy;
    std::uint16_t seq = 0;
    struct Pending {
        double arrive;
        PlayerSample sample;
    };
    std::vector<Pending> inFlight;
    int warps = 0;

    // truth(t), the copy's own velocity(t)
    template <typename Truth, typename Own>
    float Run(double untilMs, Truth truth, Own own, double sendMs, double latencyMs, bool jitter, double measureFromMs,
              Vec3 truthVel(double)) {
        float worst = 0.0f;
        double nextSend = now;
        while (now < untilMs) {
            if (now >= nextSend) {
                PlayerSample s;
                s.seq = ++seq;
                s.senderMs = static_cast<std::uint32_t>(now + 1000000.0);
                s.position = truth(now);
                s.velocity = truthVel(now);
                const double extra = jitter ? (seq * 7919 % 40) : 0.0;
                inFlight.push_back({now + latencyMs + extra, s});
                nextSend += sendMs;
            }
            for (auto it = inFlight.begin(); it != inFlight.end();) {
                if (it->arrive <= now) {
                    AcceptSample(track, it->sample, now, p);
                    it = inFlight.erase(it);
                } else {
                    ++it;
                }
            }
            const double dt = 1000.0 / 60.0;
            if (TrackFresh(track, now, p)) {
                const RemoteStep step = StepRemote(track, copy, now, p);
                if (step.warp) {
                    copy = step.target;
                    ++warps;
                } else {
                    copy = copy + own(now) * static_cast<float>(dt / 1000.0) + step.add;
                }
                copy.y = (std::max)(copy.y, 0.0f);  // the ground, as the proxy's collision keeps it
            } else {
                copy = copy + own(now) * static_cast<float>(dt / 1000.0);
            }
            now += dt;
            // Compare with where the player was one latency ago at the most: what any extrapolation can know.
            if (now >= measureFromMs) worst = (std::max)(worst, Length(copy - truth(now - latencyMs)));
        }
        return worst;
    }
};

Vec3 Zero(double) { return {}; }

void TestConverge() {
    // A player runs at 6 m/s; the copy stands still on its own (its input never arrived). With latency the copy
    // must still track the true position closely once the latency baseline is learnt.
    {
        Sim sim;
        sim.copy = {0, 0, 0};
        auto truth = [](double t) { return Vec3{static_cast<float>(6.0 * t / 1000.0), 0, 0}; };
        const float worst = sim.Run(
            3000.0, truth, [](double) { return Vec3{}; }, 33.0, 60.0, false, 1000.0, +[](double) { return Vec3{6, 0, 0}; });
        // The receiver cannot know the one-way latency (60 ms): it shows the player ~latency behind, 0.36 m.
        Check(worst < 0.12f, "running player, copy idle: error vs the position one latency ago (" + std::to_string(worst) + " m)");
    }
    // The copy runs at 6 m/s on its own, the player stops at t=1 s: the copy must stop and settle, not slide on.
    {
        Sim sim;
        auto truth = [](double t) { return Vec3{static_cast<float>(6.0 * (std::min)(t, 1000.0) / 1000.0), 0, 0}; };
        sim.Run(
            1500.0, truth, [](double) { return Vec3{6, 0, 0}; }, 33.0, 40.0, false, 1e9,
            +[](double t) { return t < 1000.0 ? Vec3{6, 0, 0} : Vec3{}; });
        const float after = Length(sim.copy - Vec3{6, 0, 0});
        Check(after < 0.05f, "player stopped: copy that would run on is held within 5 cm after 0.5 s (" +
                                  std::to_string(after) + " m)");
    }
    // A jump the copy does not reproduce (no vertical motion of its own): the vertical axis is corrected too.
    {
        Sim sim;
        auto truth = [](double t) {
            const double s = t / 1000.0 - 1.0;
            const double y = s > 0.0 && s < 1.0 ? 8.0 * s - 8.0 * s * s : 0.0;  // 2 m high, 1 s long
            return Vec3{0, static_cast<float>(y), 0};
        };
        const float worst = sim.Run(
            3000.0, truth, [](double) { return Vec3{}; }, 33.0, 30.0, false, 500.0, +[](double t) {
                const double s = t / 1000.0 - 1.0;
                return Vec3{0, static_cast<float>(s > 0.0 && s < 1.0 ? 8.0 - 16.0 * s : 0.0), 0};
            });
        // What is left is the send interval: a sample is up to 33 ms old when the velocity flips (take-off,
        // landing), 8 m/s * 33 ms = 0.26 m for one interval.
        Check(worst < 0.3f, "unreproduced jump: vertical error stays small (" + std::to_string(worst) + " m)");
        Check(std::fabs(sim.copy.y) <= sim.p.deadband + 0.005f, "and the copy lands with the player (within the deadband)");
    }
    // Jitter (0-40 ms on 50 ms latency) and the copy's own motion 30% too fast: no oscillation, bounded error.
    {
        Sim sim;
        auto truth = [](double t) { return Vec3{static_cast<float>(10.0 * t / 1000.0), 0, 0}; };
        const float worst = sim.Run(
            4000.0, truth, [](double) { return Vec3{13, 0, 0}; }, 33.0, 50.0, true, 1500.0,
            +[](double) { return Vec3{10, 0, 0}; });
        Check(worst < 0.3f, "jitter + a copy 30% too fast: bounded (" + std::to_string(worst) + " m)");
        Check(sim.warps == 0, "no warps for small errors");
    }
    // Far apart (a respawn): one warp, then tracking.
    {
        Sim sim;
        sim.copy = {100, 0, 0};
        sim.Run(
            500.0, [](double) { return Vec3{0, 0, 0}; }, [](double) { return Vec3{}; }, 33.0, 20.0, false, 1e9, &Zero);
        Check(sim.warps == 1 && Length(sim.copy) < 0.01f, "a far copy warps once to the estimate");
    }
    // Samples stop: the track goes stale, the game's own path takes over (StepRemote is no longer called).
    {
        NetPlayerParams p;
        RemoteTrack t;
        PlayerSample s;
        s.seq = 1;
        AcceptSample(t, s, 0.0, p);
        Check(!TrackFresh(t, p.staleMs + 1.0, p), "no samples: stale, back to the game's sync");
    }
    // Step size cap.
    {
        NetPlayerParams p;
        RemoteTrack t;
        PlayerSample s;
        s.seq = 1;
        s.position = {5, 0, 0};
        AcceptSample(t, s, 0.0, p);
        const RemoteStep step = StepRemote(t, {0, 0, 0}, 0.0, p);
        Check(!step.warp && Length(step.add) <= p.maxSpeed / 60.0f + 1e-4f, "one step adds at most maxSpeed * dt");
    }
}

// The copy already follows the sender exactly, but frame gaps vary as in the joiner logs.
// StepRemote must not turn changes in frame duration into alternating corrections.
void TestUnevenFrames() {
    for (const float speed : {6.0f, 40.0f}) {
        NetPlayerParams p;
        RemoteTrack track;
        Vec3 copy{};
        double now = 0.0;
        float worstCorrection = 0.0f;
        int warps = 0;
        for (int frame = 0; frame < 300; ++frame) {
            const double gap = frame % 29 == 0 ? 369.0 : (frame % 7 == 0 ? 45.0 : 1000.0 / 60.0);
            if (frame) {
                now += gap;
                copy.x += speed * static_cast<float>(gap / 1000.0);
            }
            PlayerSample sample;
            sample.seq = static_cast<std::uint16_t>(frame + 1);
            sample.senderMs = static_cast<std::uint32_t>(now);
            sample.position = {speed * static_cast<float>(now / 1000.0), 0, 0};
            sample.velocity = {speed, 0, 0};
            AcceptSample(track, sample, now, p);
            const RemoteStep step = StepRemote(track, copy, now, p);
            worstCorrection = (std::max)(worstCorrection, Length(step.add));
            warps += step.warp ? 1 : 0;
            copy = step.warp ? step.target : copy + step.add;
        }
        Check(worstCorrection < 0.03f && warps == 0,
              "uneven frames do not push an accurately moving copy (" + std::to_string(worstCorrection) + " m)");
    }
}

// The diagnostic 4 log has 100 ms samples with occasional 300 ms accepted-sample gaps.
// Exercise the sender and receiver, dropping two updates while the local copy keeps updating at 60 Hz.
void TestSparseUpdates() {
    for (const float speed : {6.0f, 50.0f}) {
        for (const float ownSpeed : {0.0f, speed}) {
            for (const int turn : {1, 0, -1}) {
                NetPlayerParams p;
                SendTrack sender;
                RemoteTrack track;
                Vec3 copy{};
                float minAdvance = speed, worstError = 0.0f, recoveredError = 0.0f;
                int warps = 0;
                for (int frame = 0; frame <= 120; ++frame) {
                    const double now = frame * 1000.0 / 60.0;
                    const float seconds = static_cast<float>(now / 1000.0);
                    const float truth = speed * (seconds <= 1.0f ? seconds : 1.0f + (seconds - 1.0f) * turn);
                    if (frame % 6 == 0) {
                        const PlayerSample sample = MakeSample(sender, {truth, 0, 0}, now);
                        if (frame != 60 && frame != 66) AcceptSample(track, sample, now, p);
                    }
                    const Vec3 before = copy;
                    const RemoteStep step = StepRemote(track, copy, now, p);
                    if (step.warp) {
                        copy = step.target;
                        ++warps;
                    } else {
                        copy = copy + Vec3{ownSpeed * (frame < 72 ? 1 : turn) / 60.0f, 0, 0} + step.add;
                    }
                    if (frame >= 54 && frame < 72) minAdvance = (std::min)(minAdvance, copy.x - before.x);
                    const float nextSeconds = static_cast<float>((frame + 1) / 60.0);
                    const float nextTruth = speed * (nextSeconds <= 1.0f ? nextSeconds :
                        1.0f + (nextSeconds - 1.0f) * turn);
                    const float error = std::fabs(copy.x - nextTruth);
                    if (frame >= 54) worstError = (std::max)(worstError, error);
                    if (frame >= 108) recoveredError = (std::max)(recoveredError, error);
                }
                const std::string label = "sparse updates, speed=" + std::to_string(speed) +
                    " own=" + std::to_string(ownSpeed) + " turn=" + std::to_string(turn);
                if (turn == 1) {
                    Check(minAdvance > speed / 60.0f * 0.9f, label + ": no pause during a 300 ms gap");
                    Check(worstError < 0.15f, label + ": straight movement stays within 15 cm");
                    Check(warps == 0, label + ": no warp on resuming updates");
                } else {
                    Check(worstError < speed * 0.4f + 0.1f, label + ": missing updates bound stop/turn overshoot");
                    Check(recoveredError < 0.15f, label + ": settles after a stop or reversal");
                    Check(warps <= 1, label + ": no repeated warps after a stop or reversal");
                }
                std::printf("%s: min advance %.3f m, worst error %.3f m, settled error %.3f m, warps %d\n",
                            label.c_str(), minAdvance, worstError, recoveredError, warps);
            }
        }
    }
}

// The physics does not always do what the step asks: the host's motion traces of 2026-10-22 show requests of a
// metre a frame with nothing moving (a landing, an animation) and a standing copy thrown +-35 cm up and down at
// 30 Hz, both from the estimator reading an unapplied request as motion the other way and the feed-forward
// fighting it. A request the physics does not apply must not grow.
void TestBlockedRequest() {
    // The sender stands at y=0; the copy's ground is 5 cm higher (uneven terrain between the machines).
    {
        NetPlayerParams p;
        RemoteTrack track;
        Vec3 copy{0, 0.05f, 0};
        float worstAdd = 0.0f, worstOwn = 0.0f;
        for (int frame = 0; frame < 240; ++frame) {
            const double now = frame * 1000.0 / 60.0;
            if (frame % 6 == 0) {
                PlayerSample s;
                s.seq = static_cast<std::uint16_t>(frame / 6 + 1);
                s.senderMs = static_cast<std::uint32_t>(now);
                AcceptSample(track, s, now, p);
            }
            const RemoteStep step = StepRemote(track, copy, now, p);
            copy = copy + step.add;
            copy.y = (std::max)(copy.y, 0.05f);  // the floor takes the downward part
            if (frame > 60) {
                worstAdd = (std::max)(worstAdd, Length(step.add));
                worstOwn = (std::max)(worstOwn, Length(track.ownVelocity));
            }
        }
        Check(worstAdd < 0.02f, "a request the floor blocks stays at the error's share, it does not wind up (" +
                                    std::to_string(worstAdd) + " m a frame)");
        Check(worstOwn < 0.5f, "and the copy is not taken for moving (" + std::to_string(worstOwn) + " m/s)");
    }
    // The physics applies the request, then the ground puts the copy back the next frame (a downward push into
    // the floor): the correction must settle, not grow to the step cap.
    {
        NetPlayerParams p;
        RemoteTrack track;
        Vec3 copy{0, 0, 0};
        PlayerSample s;
        s.seq = 1;
        s.position = {0, -0.3f, 0};  // the sender's ground is 30 cm lower
        AcceptSample(track, s, 0.0, p);
        float lateAdd = 0.0f;
        bool pushed = false;
        for (int frame = 1; frame < 120; ++frame) {
            const double now = frame * 1000.0 / 60.0;
            if (frame % 6 == 0) {
                s.seq = static_cast<std::uint16_t>(frame / 6 + 1);
                s.senderMs = static_cast<std::uint32_t>(now);
                AcceptSample(track, s, now, p);
            }
            const RemoteStep step = StepRemote(track, copy, now, p);
            copy = pushed ? Vec3{0, 0, 0} : copy + step.add;  // applied one frame, undone the next
            pushed = !pushed;
            if (frame > 60) lateAdd = (std::max)(lateAdd, Length(step.add));
        }
        Check(lateAdd < 0.12f, "a push the ground undoes settles instead of growing (" + std::to_string(lateAdd) + " m a frame)");
    }
    // A copy running 30% too fast on its own, braked by the step (the request opposes its motion) and the physics
    // applying the brake in full: the estimator still reads its own speed right, so the brake holds.
    {
        NetPlayerParams p;
        RemoteTrack track;
        Vec3 copy{};
        float worst = 0.0f;
        for (int frame = 0; frame < 300; ++frame) {
            const double now = frame * 1000.0 / 60.0;
            if (frame % 6 == 0) {
                PlayerSample s;
                s.seq = static_cast<std::uint16_t>(frame / 6 + 1);
                s.senderMs = static_cast<std::uint32_t>(now);
                s.position = {10.0f * static_cast<float>(now / 1000.0), 0, 0};
                s.velocity = {10, 0, 0};
                AcceptSample(track, s, now, p);
            }
            const RemoteStep step = StepRemote(track, copy, now, p);
            copy = copy + Vec3{13.0f / 60.0f, 0, 0} + step.add;  // where the copy is at the next frame
            if (frame > 120) worst = (std::max)(worst, std::fabs(copy.x - 10.0f * static_cast<float>((frame + 1) / 60.0)));
        }
        Check(worst < 0.1f, "a copy too fast on its own is held by the brake (" + std::to_string(worst) + " m)");
    }
    // A moving copy comes to rest on a surface 5 cm beyond the sender. Prior braking credit must
    // expire when the surface blocks it, and normal movement must resume when the constraint goes away.
    for (float sign : {-1.0f, 1.0f}) {
        NetPlayerParams p;
        RemoteTrack track;
        Vec3 copy{0, sign * 0.05f, 0};
        float blockedAdd = 0.0f, blockedOwn = 0.0f;
        for (int frame = 0; frame <= 420; ++frame) {
            const double now = frame * 1000.0 / 60.0;
            const bool moving = frame < 60 || frame >= 300;
            const float distance = frame < 60 ? frame / 60.0f
                                   : frame < 300 ? 1.0f : 1.0f + (frame - 300) / 60.0f;
            if (frame % 6 == 0) {
                PlayerSample sample;
                sample.seq = static_cast<std::uint16_t>(frame / 6 + 1);
                sample.senderMs = static_cast<std::uint32_t>(now);
                sample.position.y = sign * distance;
                sample.velocity.y = moving ? sign : 0.0f;
                AcceptSample(track, sample, now, p);
            }
            const auto step = StepRemote(track, copy, now, p);
            Check(!step.warp, "blocked transition does not warp");
            if (frame >= 180 && frame < 300) {
                blockedAdd = (std::max)(blockedAdd, Length(step.add));
                blockedOwn = (std::max)(blockedOwn, Length(track.ownVelocity));
            }
            copy = copy + step.add + Vec3{0, moving ? sign / 60.0f : 0.0f, 0};
            if (frame < 300) copy.y = sign * (std::max)(sign * copy.y, frame >= 59 ? 1.05f : 0.05f);
        }
        Check(blockedAdd < 0.02f && blockedOwn < 0.05f,
              "a previously moving, now blocked copy loses fictitious braking velocity");
        Check(std::fabs(sign * copy.y - (3.0f + 1.0f / 60)) < 0.05f,
              "movement resumes after the blocked interval");
    }
    // Called again within the same physics frame: nothing more is added and nothing is measured.
    {
        NetPlayerParams p;
        RemoteTrack track;
        PlayerSample s;
        s.seq = 1;
        s.velocity = {6, 0, 0};
        AcceptSample(track, s, 0.0, p);
        const RemoteStep first = StepRemote(track, {0, 0, 0}, 16.0, p);
        const Vec3 own = track.ownVelocity;
        const RemoteStep again = StepRemote(track, {0, 0, 0}, 16.5, p);
        Check(Length(first.add) > 0.0f && Length(again.add) == 0.0f && Same(track.ownVelocity, own) &&
                  Same(track.lastAdded, first.add),
              "a repeated step adds nothing and keeps the measurement");
    }
}

// Review fixes (2026-10-07).
void TestReviewFixes() {
    // 1. The flush interval follows this machine's own PlayerSync switch.
    Check(EffectiveFlushIntervalMs(45, false) == 0, "PlayerSync off locally: the game's 90 ms flush stays");
    Check(EffectiveFlushIntervalMs(45, true) == 45, "PlayerSync on: 45 ms");
    Check(EffectiveFlushIntervalMs(0, true) == 0 && EffectiveFlushIntervalMs(90, true) == 0, "0 or 90: leave it");
    Check(EffectiveFlushIntervalMs(5, true) == 16 && EffectiveFlushIntervalMs(200, true) == 90, "clamped to 16..90");
    // 2. Samples stop arriving while the player ran at 5 m/s: once the estimate stops at the extrapolation limit,
    //    the copy must stop there too, not run on by the old velocity and get pulled back.
    {
        NetPlayerParams p;
        RemoteTrack t;
        PlayerSample s;
        s.seq = 1;
        s.velocity = {5, 0, 0};
        AcceptSample(t, s, 0.0, p);
        Vec3 copy{};
        float furthest = 0.0f;
        for (int frame = 0; frame * 1000.0 / 60.0 < p.staleMs; ++frame) {
            const double now = frame * 1000.0 / 60.0;
            const RemoteStep step = StepRemote(t, copy, now, p);
            copy = copy + step.add;  // the copy has no motion of its own
            furthest = (std::max)(furthest, copy.x);
        }
        const float limit = 5.0f * p.maxExtrapolateMs / 1000.0f;
        Check(furthest <= limit + 0.02f, "no overshoot past the extrapolation limit (" + std::to_string(furthest) +
                                             " m, estimate stops at " + std::to_string(limit) + " m)");
        Check(std::fabs(copy.x - limit) <= p.deadband + 0.01f, "and it settles on the stopped estimate");
    }
    // 3. The warp threshold grows with speed: a 40 m/s boost may be 9 m off for a moment without a warp.
    {
        NetPlayerParams p;
        RemoteTrack t;
        PlayerSample s;
        s.seq = 1;
        AcceptSample(t, s, 0.0, p);
        Check(WarpDistance(t, p) == p.snapDistance, "standing: warp above snapDistance");
        Check(StepRemote(t, {9, 0, 0}, 0.0, p).warp, "standing, 9 m off: warp");
        RemoteTrack fast;
        s.velocity = {40, 0, 0};
        AcceptSample(fast, s, 0.0, p);
        Check(std::fabs(WarpDistance(fast, p) - 10.0f) < 1e-4f, "40 m/s: warp above 10 m");
        Check(!StepRemote(fast, {-9, 0, 0}, 0.0, p).warp, "40 m/s, 9 m off: no warp");
        RemoteTrack fast2;
        AcceptSample(fast2, s, 0.0, p);
        Check(StepRemote(fast2, {-11, 0, 0}, 0.0, p).warp, "40 m/s, 11 m off: warp");
    }
    // 4. A copy whose own position went NaN gets nothing added: the NaN would end up in the game's position.
    {
        NetPlayerParams p;
        RemoteTrack t;
        PlayerSample s;
        s.seq = 1;
        s.velocity = {5, 0, 0};
        AcceptSample(t, s, 0.0, p);
        const RemoteStep step = StepRemote(t, {std::nanf(""), 0, 0}, 16.0, p);
        Check(!step.warp && step.add.x == 0.0f && step.add.y == 0.0f && step.add.z == 0.0f,
              "a NaN copy position: nothing added, no warp");
    }
}

void TestRoomTrackCapacity() {
    // Receive a whole room's samples, including the overlapping mission-end copies, before its frame update.
    // No live copy may lose its samples or convergence history before UpdateHook uses them.
    for (const int count : {4, 8}) {
        PlayerTracks<RemoteTrack> tracks;
        std::vector<int> objects(static_cast<std::size_t>(count) + 1);
        NetPlayerParams params;
        for (int frame = 1; frame <= 3; ++frame) {
            const double now = frame * 33.0;
            for (int i = 0; i < count; ++i) {
                auto* track = tracks.Find(&objects[i], now, true);
                if (frame > 1) Check(track->stepped, "whole-room samples preserve prior convergence state");
                PlayerSample sample;
                sample.seq = static_cast<std::uint16_t>(frame);
                sample.senderMs = static_cast<std::uint32_t>(now);
                sample.position = {static_cast<float>(i), 0, 0};
                Check(AcceptSample(*track, sample, now, params), "whole-room sample accepted");
            }
            for (int i = 0; i < count; ++i) {
                auto* track = tracks.Find(&objects[i], now + 1, false);
                Check(track && track->last.seq == frame && track->last.position.x == static_cast<float>(i),
                      "every supported player remains available to the frame update");
                if (track) {
                    StepRemote(*track, track->last.position, now + 1, params);
                    Check(track->stepped, "each player's convergence runs");
                }
            }
        }
        if (count == 8) {
            // The first player departs; keep the others fresh, then admit its replacement.
            for (int i = 1; i < count; ++i) tracks.Find(&objects[i], 200.0, false);
            auto* newcomer = tracks.Find(&objects[count], 201.0, true);
            Check(!newcomer->have && !newcomer->stepped, "replacement starts with fresh sync state");
            Check(!tracks.Find(&objects[0], 202.0, false), "oldest departed player yields its slot");
            for (int i = 1; i < count; ++i)
                Check(tracks.Find(&objects[i], 202.0, false) != nullptr, "replacement retains other players");
        }
    }
}

void TestFeature() {
    // An INI without [Netcode]: every switch at its default.
    InitNetFeature(L"Z:\\no-such-folder\\netplayer_test.ini");
    Check(NetFeatureEnabledLocally(NetFeature::PlayerSync), "PlayerSync on by default");
    // Outside a room the gate cannot know what the others run: no feature is active.
    Check(!NetFeatureActive(NetFeature::PlayerSync), "not in a room: PlayerSync not active");
    SetNetFeatureForTest(NetFeature::PlayerSync, true);
    Check(NetFeatureActive(NetFeature::PlayerSync), "forced on");
    SetNetFeatureForTest(NetFeature::PlayerSync, false);
    Check(!NetFeatureActive(NetFeature::PlayerSync), "forced off");
    ClearNetFeatureForTest();
}

// Remote aim (StepAim): the native angles, ordered by the block's sequence and written every frame.
void TestAim() {
    const float pi = 3.14159265f, twoPi = 2.0f * pi, frame = 1000.0f / 60.0f;
    const auto arc = [twoPi](float a, float b) { return std::remainder(a - b, twoPi); };  // a - b the short way
    NetPlayerParams p;

    {  // a step of 1 rad at 60 Hz: exp(-7 * 16.7 / 50) = 0.097 left after 7 frames, 0.135 after 6
        RemoteTrack t;
        AcceptAim(t, 1, 0.0f, 0.0f, 0.0, p);
        const AimStep first = StepAim(t, 0.0, p);
        Check(first.write && first.snapped && first.pitch == 0.0f && first.yaw == 0.0f,
              "a new aim track writes its target outright");
        AcceptAim(t, 2, 0.5f, 1.0f, frame, p);
        float previous = 0.0f, remaining[8]{};
        bool monotonic = true;
        for (int i = 1; i <= 7; ++i) {
            const AimStep step = StepAim(t, frame * i, p);
            monotonic = monotonic && !step.snapped && step.yaw > previous && step.yaw < 1.0f &&
                        Near(step.pitch, 0.5f * step.yaw, 1e-5f);
            previous = step.yaw;
            remaining[i] = 1.0f - step.yaw;
        }
        Check(monotonic, "aim approaches a step from below, pitch and yaw together, without snapping");
        Check(remaining[6] > 0.1f && remaining[7] <= 0.1f,
              "a tenth of an aim step is left after 2.3 aimTauMs, not sooner (" + std::to_string(remaining[6]) +
                  ", " + std::to_string(remaining[7]) + ")");
    }

    {  // across +-pi, and the unbounded yaw of the captures (18.8 and -11.4 rad): the short way, never the long one
        struct Turn {
            float from, to;
        };
        for (const Turn turn : {Turn{3.1f, -3.1f}, Turn{-3.1f, 3.1f}, Turn{18.8f, -11.4f}}) {
            RemoteTrack t;
            AcceptAim(t, 1, 0.0f, turn.from, 0.0, p);
            StepAim(t, 0.0, p);
            AcceptAim(t, 2, 0.0f, turn.to, frame, p);
            const float total = arc(turn.to, turn.from);
            float written = turn.from;
            bool shortWay = true;
            for (int i = 1; i <= 30; ++i) {
                if (i % 6 == 0) AcceptAim(t, static_cast<std::uint16_t>(2 + i), 0.0f, turn.to, frame * i, p);
                const AimStep step = StepAim(t, frame * i, p);
                const float moved = arc(step.yaw, written);
                shortWay = shortWay && !step.snapped && moved * total > 0.0f && std::fabs(moved) <= std::fabs(total);
                written = step.yaw;
            }
            Check(shortWay, "yaw " + std::to_string(turn.from) + " to " + std::to_string(turn.to) +
                                " turns the short way (" + std::to_string(total) + " rad) at every step");
            Check(std::fabs(arc(written, turn.to)) < 1e-3f && std::fabs(written - turn.to) < 1e-3f,
                  "yaw " + std::to_string(turn.from) + " to " + std::to_string(turn.to) +
                      " converges on the target's own value");
        }
    }

    {  // the snap radius, pi/2, measured the short way for yaw and absolutely for pitch
        struct Change {
            float pitchFrom, yawFrom, pitch, yaw;
            bool snap;
            const char* label;
        };
        for (const Change c : {Change{0.0f, 0.0f, 0.0f, 1.6f, true, "yaw 1.6 rad off snaps"},
                               Change{0.0f, 0.0f, 0.0f, 1.5f, false, "yaw 1.5 rad off is smoothed"},
                               Change{0.0f, 3.0f, 0.0f, -1.7f, true, "yaw 1.58 rad off across pi snaps"},
                               Change{0.0f, 3.0f, 0.0f, -1.8f, false, "yaw 1.48 rad off across pi is smoothed"},
                               Change{-0.8f, 0.0f, 0.8f, 0.0f, true, "pitch 1.6 rad off snaps"},
                               Change{-0.7f, 0.0f, 0.8f, 0.0f, false, "pitch 1.5 rad off is smoothed"}}) {
            RemoteTrack t;
            AcceptAim(t, 1, c.pitchFrom, c.yawFrom, 0.0, p);
            StepAim(t, 0.0, p);
            AcceptAim(t, 2, c.pitch, c.yaw, frame, p);
            const AimStep step = StepAim(t, frame, p);
            const bool atTarget = step.pitch == c.pitch && step.yaw == c.yaw;
            Check(step.write && step.snapped == c.snap && atTarget == c.snap, c.label);
        }
    }

    {  // order within the freshness window, and a restart after it
        RemoteTrack t;
        Check(!AimFresh(t, 0.0, p) && !StepAim(t, 0.0, p).write, "no aim yet: nothing written");
        Check(AcceptAim(t, 10, 0.1f, 0.2f, 1000.0, p), "first aim");
        Check(!AcceptAim(t, 10, 0.3f, 0.4f, 1010.0, p), "repeated aim sequence refused");
        Check(!AcceptAim(t, 9, 0.3f, 0.4f, 1020.0, p), "older aim sequence refused");
        Check(t.aim.pitch == 0.1f && t.aim.yaw == 0.2f && t.aim.receivedMs == 1000.0,
              "a refused aim leaves the target and its arrival time");
        Check(AcceptAim(t, 0x7000, 0.1f, 0.2f, 1030.0, p) && AcceptAim(t, 0xE000, 0.1f, 0.2f, 1035.0, p) &&
                  AcceptAim(t, 0, 0.1f, 0.2f, 1040.0, p) && !AcceptAim(t, 0xFFFE, 0.1f, 0.2f, 1040.0, p),
              "aim sequence order across the 16-bit wrap");
        StepAim(t, 1040.0, p);
        Check(AimFresh(t, 1040.0 + p.staleMs, p) && !AimFresh(t, 1041.0 + p.staleMs, p), "aim freshness window");
        Check(!StepAim(t, 1041.0 + p.staleMs, p).write, "a stale aim track writes nothing");
        Check(AcceptAim(t, 5, 0.1f, 0.25f, 1500.0, p), "an older aim sequence after a stale gap starts over");
        const AimStep resumed = StepAim(t, 1500.0, p);
        Check(resumed.snapped && resumed.yaw == 0.25f, "aim resumed after a stale gap is written outright");
    }

    {  // the aim's freshness is its own: position every frame, aim (a sender before 3.6.3) rarely
        RemoteTrack t;
        PlayerSample s;
        s.seq = 1;
        AcceptSample(t, s, 0.0, p);
        AcceptAim(t, 1, 0.0f, 0.0f, 0.0, p);
        s.seq = 2;
        AcceptSample(t, s, 300.0, p);
        Check(TrackFresh(t, 500.0, p) && !AimFresh(t, 500.0, p) && !StepAim(t, 500.0, p).write,
              "aim goes stale while the position is fresh");
        // Aim with a block the position refused as reordered, then a position restart: the aim order still holds.
        RemoteTrack u;
        s.seq = 20;
        AcceptSample(u, s, 0.0, p);
        s.seq = 19;
        Check(!AcceptSample(u, s, 300.0, p) && AcceptAim(u, 19, 0.0f, 0.5f, 300.0, p),
              "aim accepted apart from position");
        StepAim(u, 300.0, p);
        s.seq = 18;
        Check(AcceptSample(u, s, 450.0, p) && !AcceptAim(u, 18, 0.0f, 0.4f, 450.0, p) && u.aim.yaw == 0.5f,
              "a position restart keeps the aim order");
        Check(!StepAim(u, 460.0, p).snapped, "a position restart keeps the written aim");
    }

    {  // uneven frames, a 369 ms hitch among them: no overshoot, and the hitch advances only the 50 ms clamp
        RemoteTrack t;
        AcceptAim(t, 1, 0.0f, 0.0f, 0.0, p);
        StepAim(t, 0.0, p);
        double now = 0.0;
        float written = 0.0f, hitchKept = 0.0f;
        bool steady = true;
        for (int i = 1; i <= 40; ++i) {
            const double gap = i == 12 ? 369.0 : (i % 3 == 0 ? 45.0 : 16.0);
            now += gap;
            AcceptAim(t, static_cast<std::uint16_t>(1 + i), 0.0f, 1.2f, now, p);
            const AimStep step = StepAim(t, now, p);
            steady = steady && !step.snapped && step.yaw >= written && step.yaw <= 1.2f;
            if (i == 12) hitchKept = (1.2f - step.yaw) / (1.2f - written);
            written = step.yaw;
        }
        Check(steady, "uneven frames approach the aim without overshoot or snap");
        Check(Near(hitchKept, std::exp(-50.0f / p.aimTauMs), 1e-4f),
              "a 369 ms hitch advances the aim by the 50 ms clamp (" + std::to_string(hitchKept) + " left)");
        Check(1.2f - written < 1e-4f, "uneven frames converge on the aim");
    }

    {  // called again within kMinStepMs: the previous angles, measured from the step that wrote them
        RemoteTrack t;
        AcceptAim(t, 1, 0.0f, 0.0f, 0.0, p);
        StepAim(t, 0.0, p);
        AcceptAim(t, 2, 0.4f, 1.0f, 16.0, p);
        const AimStep step = StepAim(t, 16.0, p);
        const AimStep again = StepAim(t, 19.0, p);
        Check(again.write && !again.snapped && again.pitch == step.pitch && again.yaw == step.yaw,
              "a step within 4 ms writes the previous angles again");
        const AimStep next = StepAim(t, 22.0, p);
        Check(next.yaw > step.yaw, "a step 6 ms after the last real one advances (the repeat did not move the clock)");
    }

    {
        RemoteTrack t;
        Check(!AcceptAim(t, 1, std::nanf(""), 0.0f, 0.0, p) && !AcceptAim(t, 1, 0.0f, INFINITY, 0.0, p) &&
                  !AimFresh(t, 0.0, p),
              "nonfinite aim refused on a new track");
        AcceptAim(t, 1, 0.1f, 0.2f, 0.0, p);
        Check(!AcceptAim(t, 2, 0.0f, -INFINITY, 10.0, p) && t.aim.seq == 1 && t.aim.yaw == 0.2f,
              "nonfinite aim refused without moving the order or the target");
    }
}

}  // namespace

int main() {
    TestBlock();
    TestBin();
    TestSender();
    TestAcceptAndClock();
    TestEstimate();
    TestConverge();
    TestUnevenFrames();
    TestSparseUpdates();
    TestBlockedRequest();
    TestReviewFixes();
    TestRoomTrackCapacity();
    TestFeature();
    TestAim();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
