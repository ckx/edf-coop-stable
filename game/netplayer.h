#pragma once
// Remote player sync (docs/net-re/player.md): what the game-free part computes and sends.
//
// The game sends a player's position only every sixth sync frame, as three half floats (0.25-1 m steps on the big
// maps), with no velocity and no time; the remote copy runs its own movement from the replicated stick input and
// is pulled toward the last position by 7.5% per frame, with no vertical pull at all in the air. Two machines
// simulating a jump or a boost differ, and the pull lags behind whatever the copy did on its own.
//
// The new data is one more element at the end of the player's record, behind a mask bit the game does not use
// (kPlayerBlockBit): sequence number, the sender's clock, full precision position and velocity. The receiver
// extrapolates the last sample to "now", and drives the copy toward that estimate itself: the copy's own motion is
// measured and replaced by the sender's velocity (feed-forward), and the remaining error converges with a time
// constant, on all three axes. Everything here is plain arithmetic on values, so it is unit tested
// (tests/netplayer_test.cpp); netplayer_game.cpp is the part that reads and writes the game's objects.
#include <cstddef>
#include <cstdint>

namespace multislot {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
Vec3 operator+(Vec3 a, Vec3 b);
Vec3 operator-(Vec3 a, Vec3 b);
Vec3 operator*(Vec3 a, float s);
float Length(Vec3 a);

// The SoldierBase record mask bit carrying the block (the game uses 0x1..0x800, PaleWing's 0x800 included).
constexpr std::uint16_t kPlayerBlockBit = 0x1000;
constexpr std::uint8_t kPlayerBlockVersion = 1;
// version (1), seq (2), sender ms (4), position (12), velocity (12)
constexpr std::size_t kPlayerBlockBytes = 31;

struct PlayerSample {
    std::uint16_t seq = 0;
    std::uint32_t senderMs = 0;  // the sender's monotonic clock, milliseconds (wraps)
    Vec3 position;
    Vec3 velocity;  // metres per second of wall time
};

// Little endian, kPlayerBlockBytes. Encode returns the bytes written (0 when `capacity` is short); Decode refuses
// a short block or another version.
std::size_t EncodePlayerBlock(const PlayerSample& sample, std::uint8_t* out, std::size_t capacity);
bool DecodePlayerBlock(const std::uint8_t* in, std::size_t size, PlayerSample& out);

// eos::Serialize, the game's message buffer (constructor EDF+12B4570): the data at +0x10, at most 0x5E0 bytes; a
// writer's length and a reader's end at +0x5F0, a reader's position at +8.
constexpr std::size_t kSerializeReadPos = 0x8;
constexpr std::size_t kSerializeData = 0x10;
constexpr std::size_t kSerializeEnd = 0x5F0;
constexpr std::size_t kSerializeCapacity = 0x5E0;
// The game's "bin" element (written by EDF+12B5200, read by EDF+12B49D0): a tag byte 0xA0 | (length >> 8 & 0x1F),
// the low byte of the length, the bytes. These check the buffer's bounds, which the game's own do not: a write that
// does not fit writes nothing, and a read that does not find a whole element of at most `capacity` bytes moves
// nothing.
bool WriteBinElement(void* serialize, const std::uint8_t* data, std::size_t size);
bool ReadBinElement(void* serialize, std::uint8_t* out, std::size_t capacity, std::size_t& size);

// Sender: one sample per block. The velocity is the displacement over about kVelocityWindowMs, the interval at
// which the receivers get the blocks: one in three, 100 ms apart (every accepted block in the host logs of
// 2026-10-22 is 3 sequence numbers and 100 ms after the last; where the game thins them is not established,
// docs/net-re/player.md). A velocity measured over the one write since the last (33 ms) was 3 times noisier than
// the interval it is extrapolated over, and the start of a dash exceeded the speed cap in that short window and
// went out as 0: the copy then stood still for 100 ms and was 5-9 m off at the next sample.
struct SendTrack {
    static constexpr std::size_t kHistory = 8;
    Vec3 positions[kHistory];
    double times[kHistory]{};
    std::size_t count = 0, next = 0;  // a ring: `next` is the oldest once full
    std::uint16_t seq = 0;
};
constexpr double kVelocityWindowMs = 90.0;  // measure over the newest write at least this old
constexpr float kMaxSentSpeed = 120.0f;     // m/s over the window; above that the motion is a warp, not movement
constexpr float kWarpDistance = 20.0f;      // m over the window
constexpr double kMaxVelocityGapMs = 250.0;  // writes further apart than this say nothing about the velocity
PlayerSample MakeSample(SendTrack& track, Vec3 position, double nowMs);

struct NetPlayerParams {
    float tauMs = 80.0f;               // error time constant: a tenth left after ~2.3 tau
    float maxExtrapolateMs = 350.0f;   // how far ahead of its last sample a player is extrapolated
    float snapDistance = 8.0f;         // larger errors warp the copy to the estimate...
    float warpLeadSeconds = 0.25f;     // ...or, when faster, errors above speed * this (a 40 m/s boost turning:
                                       // prediction can be wrong during an unseen turn)
    float maxSpeed = 60.0f;            // cap of what is added per frame, m/s
    float feedForward = 1.0f;          // 0..1: how much of the sender's velocity replaces the copy's own motion
    float deadband = 0.03f;            // m: errors below this are left alone
    float staleMs = 400.0f;            // without samples for this long, the game's own correction takes over
    float clockCreepMs = 0.5f;         // per sample: how fast the latency baseline follows a slower path
};

// Receiver state of one remote player.
struct RemoteTrack {
    bool have = false;
    PlayerSample last;
    double receivedMs = 0.0;   // local time the last sample arrived
    double senderTime = 0.0;   // last sample's sender clock, unwrapped
    double offset = 0.0;       // local minus sender time of the fastest recent sample
    // the driving loop
    bool stepped = false;
    Vec3 lastPosition;         // where the copy was at the last step
    Vec3 lastAdded;            // what that step added
    Vec3 ownVelocity;          // the copy's own metres per second of wall time, filtered
    double lastStepMs = 0.0;
    // The last steps' motion and requests, which the own velocity is measured over (StepRemote).
    static constexpr std::size_t kStepWindow = 4;
    Vec3 movedWindow[kStepWindow], addedWindow[kStepWindow];
    double elapsedWindow[kStepWindow]{};
    std::size_t windowNext = 0, windowCount = 0;
};
constexpr float kMaxOwnSpeed = 150.0f;  // m/s: faster is the game moving the copy itself (a snapshot), not motion
constexpr double kMinStepMs = 4.0;      // a step called again sooner than this follows no physics step: nothing added

// False for a sample that is not newer than the last one (reordered or repeated). A track that went stale starts
// over with any sample.
bool AcceptSample(RemoteTrack& track, const PlayerSample& sample, double localMs, const NetPlayerParams& params);
bool TrackFresh(const RemoteTrack& track, double nowMs, const NetPlayerParams& params);
// Where the player is now by its last sample: position + velocity * age, the age capped at maxExtrapolateMs.
Vec3 EstimatePosition(const RemoteTrack& track, double nowMs, const NetPlayerParams& params);
// How old the last sample is on this machine's clock (latency baseline removed), ms.
double SampleAgeMs(const RemoteTrack& track, double nowMs);
// The error above which the copy is warped: max(snapDistance, speed * warpLeadSeconds).
float WarpDistance(const RemoteTrack& track, const NetPlayerParams& params);

struct RemoteStep {
    bool warp = false;  // put the copy at `target`
    Vec3 target;        // the estimate
    Vec3 add;           // displacement to add to the copy's own motion this step (metres)
};
// One frame of the copy at `current`.
RemoteStep StepRemote(RemoteTrack& track, Vec3 current, double nowMs, const NetPlayerParams& params);
// The copy was not driven this frame (the game's correction was off, a ride, a warp): measure afresh.
void ResetSteps(RemoteTrack& track);

// The packet controller flush interval EDF6Coop sets ([NetPlayer] FlushIntervalMs), or 0 to leave the game's 90 ms.
// It is written into the controller's constructor, so it is decided once at start, by this machine's own switch:
// with [Netcode] PlayerSync=0 the game's 90 ms stays. With the switch on it applies in every room, mixed rooms too:
// it only changes how often this machine's own datagrams leave (nothing on the wire another build reads
// differently), at about 5 kbit/s more header traffic per member at 45 ms.
int EffectiveFlushIntervalMs(int configuredMs, bool playerSyncOnLocally);

}  // namespace multislot
