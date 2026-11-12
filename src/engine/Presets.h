// MergeX — 30 factory presets (original) and Randomize.
#pragma once
#include "Params.h"
#include "Dsp.h"
#include <initializer_list>
#include <utility>
#include <vector>

namespace mx {
struct Preset { const char* name; const char* category; std::vector<std::pair<int, float>> v; };

inline const std::vector<Preset>& factoryPresets() {
    static const std::vector<Preset> ps = {
        {"Init", "Start", {}},
        {"Halfway Blend", "Smooth", {{P_MORPH, 50}, {P_STYLE, ST_SMOOTH}}},
        {"Silk Morph", "Smooth", {{P_MORPH, 60}, {P_STYLE, ST_SMOOTH}, {P_SMOOTH, 45}}},
        {"Tone First", "Smooth", {{P_MORPH, 45}, {P_STYLE, ST_SMOOTH}, {P_SHAPE, 90}}},
        {"Texture First", "Smooth", {{P_MORPH, 45}, {P_STYLE, ST_SMOOTH}, {P_SHAPE, 10}}},
        {"Slow Bloom", "Smooth", {{P_MORPH, 0}, {P_STYLE, ST_SMOOTH}, {P_MOT_MODE, MM_LFO}, {P_MOT_DIV, 4}, {P_MOT_DEPTH, 100}, {P_SMOOTH, 40}}},
        {"Bar Breather", "Smooth", {{P_MORPH, 10}, {P_STYLE, ST_SMOOTH}, {P_MOT_MODE, MM_LFO}, {P_MOT_DIV, 2}, {P_MOT_DEPTH, 70}}},
        {"Drum Keeper", "Smooth", {{P_MORPH, 70}, {P_STYLE, ST_SMOOTH}, {P_TRANS, 85}, {P_SMOOTH, 25}}},
        {"Glass Ghost", "Smooth", {{P_MORPH, 55}, {P_STYLE, ST_SMOOTH}, {P_FORMANT, 5}, {P_SMOOTH, 60}, {P_MIX, 80}}},
        {"Deep Shadow", "Smooth", {{P_MORPH, 55}, {P_STYLE, ST_SMOOTH}, {P_FORMANT, -6}, {P_SMOOTH, 35}}},
        {"Talkbox", "Talk", {{P_MORPH, 100}, {P_STYLE, ST_TALK}, {P_SHAPE, 60}, {P_SMOOTH, 8}}},
        {"Robot Choir", "Talk", {{P_MORPH, 100}, {P_STYLE, ST_TALK}, {P_SHAPE, 35}, {P_SMOOTH, 15}, {P_BSRC, BS_PAD}}},
        {"Whisper Talk", "Talk", {{P_MORPH, 100}, {P_STYLE, ST_TALK}, {P_SHAPE, 70}, {P_BSRC, BS_NOISE}}},
        {"Saw Speaker", "Talk", {{P_MORPH, 90}, {P_STYLE, ST_TALK}, {P_SHAPE, 55}, {P_BSRC, BS_SAW}, {P_NOTE, 45}}},
        {"Half Vocoder", "Talk", {{P_MORPH, 50}, {P_STYLE, ST_TALK}, {P_SHAPE, 50}}},
        {"Reverse Talk", "Talk", {{P_MORPH, 100}, {P_STYLE, ST_TALK}, {P_TALK_DIR, 1}, {P_SHAPE, 50}}},
        {"Lofi Bands", "Talk", {{P_MORPH, 100}, {P_STYLE, ST_TALK}, {P_SHAPE, 0}, {P_SMOOTH, 30}}},
        {"Pitch Swap", "Swap", {{P_MORPH, 100}, {P_STYLE, ST_SWAP}, {P_SHAPE, 100}}},
        {"Glide Middle", "Swap", {{P_MORPH, 50}, {P_STYLE, ST_SWAP}, {P_SHAPE, 100}}},
        {"Snap Swap", "Swap", {{P_MORPH, 60}, {P_STYLE, ST_SWAP}, {P_SHAPE, 0}}},
        {"Tone Swap", "Swap", {{P_MORPH, 100}, {P_STYLE, ST_SWAP}, {P_SWAP_DIR, 1}}},
        {"Bar Pitch Ride", "Swap", {{P_MORPH, 0}, {P_STYLE, ST_SWAP}, {P_SHAPE, 100}, {P_MOT_MODE, MM_LFO}, {P_MOT_DIV, 3}, {P_MOT_SHAPE, 1}, {P_MOT_DEPTH, 100}}},
        {"Grain Shuffle", "Grain", {{P_MORPH, 50}, {P_STYLE, ST_GRAIN}, {P_SHAPE, 45}, {P_SMOOTH, 0}}},
        {"Glitch Dust", "Grain", {{P_MORPH, 40}, {P_STYLE, ST_GRAIN}, {P_SHAPE, 15}, {P_SMOOTH, 50}}},
        {"Cloud Swap", "Grain", {{P_MORPH, 60}, {P_STYLE, ST_GRAIN}, {P_SHAPE, 80}, {P_SMOOTH, 80}, {P_MIX, 85}}},
        {"Stutter Beat", "Grain", {{P_MORPH, 50}, {P_STYLE, ST_GRAIN}, {P_SHAPE, 30}, {P_TRANS, 70}}},
        {"Loud Pushes B", "Motion", {{P_MORPH, 0}, {P_STYLE, ST_SMOOTH}, {P_MOT_MODE, MM_FOLLOW_A}, {P_MOT_DEPTH, 100}, {P_SMOOTH, 20}}},
        {"B Takes Over", "Motion", {{P_MORPH, 0}, {P_STYLE, ST_SMOOTH}, {P_MOT_MODE, MM_FOLLOW_B}, {P_MOT_DEPTH, 100}}},
        {"Ramp Up Bar", "Motion", {{P_MORPH, 0}, {P_STYLE, ST_TALK}, {P_MOT_MODE, MM_LFO}, {P_MOT_DIV, 2}, {P_MOT_SHAPE, 2}, {P_MOT_DEPTH, 100}}},
        {"Pad Dream (no sidechain)", "Built-in", {{P_MORPH, 55}, {P_STYLE, ST_SMOOTH}, {P_BSRC, BS_PAD}, {P_SMOOTH, 50}, {P_NOTE, 45}}},
        {"Noise Air (no sidechain)", "Built-in", {{P_MORPH, 35}, {P_STYLE, ST_SMOOTH}, {P_BSRC, BS_NOISE}, {P_SMOOTH, 30}, {P_MIX, 90}}},
    };
    return ps;
}
inline void applyPreset(int i, float* out) { setDefaults(out); const auto& ps = factoryPresets(); if (i < 0 || i >= (int)ps.size()) return; for (auto& kv : ps[(size_t)i].v) out[kv.first] = kv.second; }

/// Randomize the sound-shaping controls; leaves B source, Dry/Wet, Output, Limiter, Before and Bypass alone.
inline void randomize(float* v, Rng& r) {
    auto pick = [&](int i) { const auto& d = kParams[i]; float u = r.uni(); float x = d.min + u * (d.max - d.min); v[i] = d.kind == K_FLOAT ? x : std::floor(d.min + u * (d.max - d.min + 1)); if (v[i] > d.max) v[i] = d.max; };
    for (int i : {P_MORPH, P_SHAPE, P_STYLE, P_TALK_DIR, P_SWAP_DIR, P_SMOOTH, P_MOT_DIV, P_MOT_SHAPE}) pick(i);
    v[P_MORPH] = 20 + 0.7f * v[P_MORPH];                        // keep it audible
    v[P_FORMANT] = r.uni() < 0.6f ? 0.f : std::round(r.bi() * 7.f);
    v[P_TRANS] = r.uni() < 0.5f ? 0.f : std::round(r.uni() * 90.f);
    float u = r.uni(); v[P_MOT_MODE] = u < 0.45f ? MM_OFF : (u < 0.8f ? MM_LFO : (u < 0.9f ? MM_FOLLOW_A : MM_FOLLOW_B));
    v[P_MOT_DEPTH] = std::round(30 + r.uni() * 70.f) * (r.uni() < 0.2f ? -1.f : 1.f);
    for (int i : {P_MORPH, P_SHAPE, P_SMOOTH}) v[i] = std::round(v[i]);
}
} // namespace mx
