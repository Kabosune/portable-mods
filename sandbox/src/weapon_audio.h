#pragma once

#include <cstdint>
#include <mutex>

namespace sb {

// Lightweight procedural float-PCM weapon & hit-confirm synthesizer (48 kHz stereo).
// Zero OS/SDL dependencies so both the live SDL3 audio stream and headless unit tests
// exercise the exact same synthesis code.
class WeaponAudioSynth {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels   = 2;
    static constexpr int kMaxVoices  = 16;

    WeaponAudioSynth() = default;

    void set_volume(float vol);
    void set_muted(bool muted);
    void set_hit_confirm_enabled(bool enabled);

    float volume() const;
    bool  muted() const;
    bool  hit_confirm_enabled() const;

    // Trigger a short punchy gunshot (~105 ms) with slight per-shot pitch randomization.
    void trigger_gunshot();
    // Trigger a crisp high-pitched hit-confirm tick/ping (~36 ms) when enabled.
    void trigger_hit_confirm();
    // Silence all active voices immediately.
    void clear_voices();

    // Render `frame_count` interleaved stereo float samples into `out_stereo` (size >= frame_count * 2).
    // Always outputs finite samples soft-limited to [-1, +1].
    void render_stereo(float* out_stereo, int frame_count);

    int active_voice_count() const;

private:
    enum class VoiceKind : uint8_t {
        None = 0,
        Gunshot,
        HitConfirm,
    };

    struct Voice {
        VoiceKind kind           = VoiceKind::None;
        int       sample_index   = 0;
        int       total_samples  = 0;
        float     pitch_mult     = 1.0f;
        float     phase_body     = 0.0f;
        float     phase_overtone = 0.0f;
        float     lp_body_l      = 0.0f;
        float     lp_body_r      = 0.0f;
        float     hp_prev_in     = 0.0f;
        float     hp_prev_out    = 0.0f;
        float     bp_lp_state    = 0.0f;
        uint32_t  rng_state      = 1u;
    };

    static uint32_t next_u32(uint32_t& state);
    static float    next_signed_unit(uint32_t& state);
    Voice&          alloc_voice_locked();

    mutable std::mutex mutex_;
    Voice              voices_[kMaxVoices]{};
    uint32_t           seed_counter_        = 0x9E3779B9u;
    float              volume_              = 0.70f;
    bool               muted_               = false;
    bool               hit_confirm_enabled_ = true;
};

} // namespace sb
