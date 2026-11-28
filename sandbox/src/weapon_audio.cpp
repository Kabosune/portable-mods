#include "weapon_audio.h"
#include "math_types.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sb {

uint32_t WeaponAudioSynth::next_u32(uint32_t& state) {
    uint32_t x = state ? state : 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

float WeaponAudioSynth::next_signed_unit(uint32_t& state) {
    // Map upper 24 bits uniformly to [-1.0, +1.0]
    const uint32_t bits = next_u32(state) >> 8;
    return (static_cast<float>(bits) / 8388607.5f) - 1.0f;
}

WeaponAudioSynth::Voice& WeaponAudioSynth::alloc_voice_locked() {
    int oldest_idx = 0;
    int oldest_age = -1;
    for (int i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].kind == VoiceKind::None) {
            return voices_[i];
        }
        if (voices_[i].sample_index > oldest_age) {
            oldest_age = voices_[i].sample_index;
            oldest_idx = i;
        }
    }
    return voices_[oldest_idx];
}

void WeaponAudioSynth::set_volume(float vol) {
    std::lock_guard<std::mutex> lock(mutex_);
    volume_ = std::clamp(vol, 0.0f, 1.5f);
}

void WeaponAudioSynth::set_muted(bool muted) {
    std::lock_guard<std::mutex> lock(mutex_);
    muted_ = muted;
}

void WeaponAudioSynth::set_hit_confirm_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    hit_confirm_enabled_ = enabled;
}

float WeaponAudioSynth::volume() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return volume_;
}

bool WeaponAudioSynth::muted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return muted_;
}

bool WeaponAudioSynth::hit_confirm_enabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hit_confirm_enabled_;
}

void WeaponAudioSynth::trigger_gunshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    Voice& v = alloc_voice_locked();
    v = {};
    v.kind          = VoiceKind::Gunshot;
    v.sample_index  = 0;
    v.total_samples = (kSampleRate * 105) / 1000; // 105 ms crisp shot
    seed_counter_  += 0x9E3779B9u;
    v.rng_state     = seed_counter_ ^ 0xA511E9B3u;
    // Slight pitch randomization (+/-4.5%) so rapid full-auto sounds natural & non-fatiguing
    const float jitter = next_signed_unit(v.rng_state);
    v.pitch_mult = 1.0f + jitter * 0.045f;
}

void WeaponAudioSynth::trigger_hit_confirm() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hit_confirm_enabled_) return;
    Voice& v = alloc_voice_locked();
    v = {};
    v.kind          = VoiceKind::HitConfirm;
    v.sample_index  = 0;
    v.total_samples = (kSampleRate * 36) / 1000; // 36 ms crisp tick/ping
    seed_counter_  += 0x7F4A7C15u;
    v.rng_state     = seed_counter_ ^ 0x3C6EF372u;
    const float jitter = next_signed_unit(v.rng_state);
    v.pitch_mult = 1.0f + jitter * 0.015f;
}

void WeaponAudioSynth::clear_voices() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& v : voices_) {
        v = {};
    }
}

int WeaponAudioSynth::active_voice_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int count = 0;
    for (const auto& v : voices_) {
        if (v.kind != VoiceKind::None) count++;
    }
    return count;
}

void WeaponAudioSynth::render_stereo(float* out_stereo, int frame_count) {
    if (!out_stereo || frame_count <= 0) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (muted_ || volume_ <= 0.0001f) {
        std::memset(out_stereo, 0, static_cast<size_t>(frame_count) * kChannels * sizeof(float));
        // Advance active voices so unmuting later does not replay stale shots
        for (auto& v : voices_) {
            if (v.kind == VoiceKind::None) continue;
            v.sample_index += frame_count;
            if (v.sample_index >= v.total_samples) {
                v.kind = VoiceKind::None;
            }
        }
        return;
    }

    constexpr float kInvSr    = 1.0f / static_cast<float>(kSampleRate);
    constexpr float kTwoPi    = 2.0f * kPi;
    // Filter coefficients at 48 kHz
    constexpr float kHpAlpha  = 0.88f; // High-pass ~1.0 kHz for transient crack
    constexpr float kBpLp     = 0.42f; // Low-pass ~5.0 kHz to keep crack smooth, not harsh
    constexpr float kTailLp   = 0.14f; // Low-pass ~1.1 kHz for punchy low-mid blast tail

    const float master_gain = volume_;

    for (int f = 0; f < frame_count; ++f) {
        float mix_l = 0.0f;
        float mix_r = 0.0f;

        for (auto& v : voices_) {
            if (v.kind == VoiceKind::None) continue;

            const int i = v.sample_index;
            if (i >= v.total_samples) {
                v.kind = VoiceKind::None;
                continue;
            }

            const float t = static_cast<float>(i) * kInvSr;
            const int rem = v.total_samples - i;

            if (v.kind == VoiceKind::Gunshot) {
                // Smooth 1.2 ms attack & 12 ms cosine fade-out at end
                const float attack = std::min(1.0f, t / 0.0012f);
                const float fade_out = (rem < 576)
                    ? (0.5f - 0.5f * std::cos(kPi * static_cast<float>(rem) / 576.0f))
                    : 1.0f;

                // 1. Punchy low-mid thump: exponential pitch sweep 195 Hz -> 52 Hz
                const float body_freq = (52.0f + 143.0f * std::exp(-t / 0.016f)) * v.pitch_mult;
                v.phase_body += kTwoPi * body_freq * kInvSr;
                if (v.phase_body > kTwoPi) v.phase_body -= kTwoPi;
                const float body_env = std::exp(-t / 0.028f);
                const float body = std::sin(v.phase_body) * body_env * 0.72f;

                // 2. Band-shaped transient crack (1.0..5.0 kHz)
                const float white_l = next_signed_unit(v.rng_state);
                const float white_r = 0.75f * white_l + 0.25f * next_signed_unit(v.rng_state);
                const float hp = kHpAlpha * (v.hp_prev_out + white_l - v.hp_prev_in);
                v.hp_prev_in  = white_l;
                v.hp_prev_out = hp;
                v.bp_lp_state += (hp - v.bp_lp_state) * kBpLp;
                const float crack_env = std::exp(-t / 0.015f);
                const float crack = v.bp_lp_state * crack_env * 0.78f;

                // 3. Low-mid blast tail (subtle stereo width)
                v.lp_body_l += (white_l - v.lp_body_l) * kTailLp;
                v.lp_body_r += (white_r - v.lp_body_r) * kTailLp;
                const float tail_env = std::exp(-t / 0.036f);
                const float tail_l = v.lp_body_l * tail_env * 0.65f;
                const float tail_r = v.lp_body_r * tail_env * 0.65f;

                const float env = attack * fade_out;
                mix_l += (body + crack + tail_l) * env;
                mix_r += (body + crack + tail_r) * env;
            } else if (v.kind == VoiceKind::HitConfirm) {
                // Crisp dual-harmonic tick/ping (~36 ms)
                const float attack = std::min(1.0f, t / 0.0006f);
                const float fade_out = (rem < 240)
                    ? (0.5f - 0.5f * std::cos(kPi * static_cast<float>(rem) / 240.0f))
                    : 1.0f;

                const float f1 = 2250.0f * v.pitch_mult;
                const float f2 = 3375.0f * v.pitch_mult;
                v.phase_body     += kTwoPi * f1 * kInvSr;
                v.phase_overtone += kTwoPi * f2 * kInvSr;
                if (v.phase_body > kTwoPi) v.phase_body -= kTwoPi;
                if (v.phase_overtone > kTwoPi) v.phase_overtone -= kTwoPi;

                const float tone_env  = std::exp(-t / 0.011f);
                const float click_env = std::exp(-t / 0.0018f);
                const float click = next_signed_unit(v.rng_state) * click_env * 0.25f;
                const float ping = (std::sin(v.phase_body) * 0.68f + std::sin(v.phase_overtone) * 0.32f) * tone_env * 0.55f;
                const float s = (ping + click) * attack * fade_out;
                mix_l += s;
                mix_r += s;
            }

            v.sample_index++;
            if (v.sample_index >= v.total_samples) {
                v.kind = VoiceKind::None;
            }
        }

        // Anti-clip soft limiter (guarantees finite output strictly in [-1, +1])
        out_stereo[f * 2 + 0] = std::tanh(mix_l * master_gain);
        out_stereo[f * 2 + 1] = std::tanh(mix_r * master_gain);
    }
}

} // namespace sb
