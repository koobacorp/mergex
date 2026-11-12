// MergeX — parameter table shared by the engine, the plugin and the tests (plain values, not normalised).
#pragma once
#include <cmath>
#include <cstring>

namespace mx {
enum Kind { K_FLOAT, K_BOOL, K_CHOICE };
enum Group { G_MAIN, G_SOURCE, G_MOTION, G_SHAPE, G_OUTPUT };

inline const char* const kStyles[] = {"Smooth", "Talk", "Swap", "Grain"};
enum Style { ST_SMOOTH, ST_TALK, ST_SWAP, ST_GRAIN };
inline const char* const kTalkDirs[] = {"A talks through B", "B talks through A"};
inline const char* const kSwapDirs[] = {"Pitch of B, tone of A", "Pitch of A, tone of B"};
inline const char* const kBSources[] = {"Sidechain (auto)", "Built-in Noise", "Built-in Saw", "Built-in Pad"};
enum BSource { BS_AUTO, BS_NOISE, BS_SAW, BS_PAD };
inline const char* const kMotionModes[] = {"Off", "LFO", "Follow A", "Follow B"};
enum MotionMode { MM_OFF, MM_LFO, MM_FOLLOW_A, MM_FOLLOW_B };
inline const char* const kMotionDivs[] = {"1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars"};
inline constexpr double kMotionDivBeats[] = {1, 2, 4, 8, 16, 32};
inline const char* const kShapes[] = {"Sine", "Triangle", "Ramp up", "Ramp down"};

struct ParamDef {
    const char* id; const char* name; int group; int kind;
    float min, max, def;
    float centre;           // skew centre for log-ish knobs (0 = linear)
    const char* unit;
    const char* const* choices; int nChoices;
};

enum P {
    P_MORPH, P_SHAPE, P_STYLE, P_TALK_DIR, P_SWAP_DIR,
    P_BSRC, P_NOTE, P_BAL,
    P_MOT_MODE, P_MOT_DIV, P_MOT_SHAPE, P_MOT_DEPTH,
    P_SMOOTH, P_FORMANT, P_TRANS,
    P_MIX, P_OUT, P_LIMIT, P_COMPARE, P_BYPASS,
    kNumParams
};

#define MX_F(id, nm, g, mn, mx, df, ctr, unit) {id, nm, g, K_FLOAT, mn, mx, df, ctr, unit, nullptr, 0}
#define MX_B(id, nm, g, df) {id, nm, g, K_BOOL, 0, 1, df, 0, "", nullptr, 0}
#define MX_C(id, nm, g, arr, df) {id, nm, g, K_CHOICE, 0, (float)(sizeof(arr) / sizeof(arr[0]) - 1), df, 0, "", arr, (int)(sizeof(arr) / sizeof(arr[0]))}

inline const ParamDef kParams[kNumParams] = {
    MX_F("morph", "Morph", G_MAIN, 0, 100, 0, 0, "%"),
    MX_F("shape", "Pad Y (Shape)", G_MAIN, 0, 100, 50, 0, "%"),
    MX_C("style", "Style", G_MAIN, kStyles, 0),
    MX_C("talk_dir", "Talk Direction", G_MAIN, kTalkDirs, 0),
    MX_C("swap_dir", "Swap Direction", G_MAIN, kSwapDirs, 0),
    MX_C("b_source", "B Source", G_SOURCE, kBSources, 0),
    MX_F("tone_note", "Built-in Note", G_SOURCE, 24, 72, 45, 0, "st"),
    MX_F("balance", "A/B Balance", G_SOURCE, -12, 12, 0, 0, "dB"),
    MX_C("motion", "Motion", G_MOTION, kMotionModes, 0),
    MX_C("motion_rate", "Motion Rate", G_MOTION, kMotionDivs, 2),
    MX_C("motion_shape", "Motion Shape", G_MOTION, kShapes, 0),
    MX_F("motion_depth", "Motion Depth", G_MOTION, -100, 100, 50, 0, "%"),
    MX_F("smooth", "Smoothness", G_SHAPE, 0, 100, 20, 0, "%"),
    MX_F("formant", "Formant", G_SHAPE, -12, 12, 0, 0, "st"),
    MX_F("transient", "Transient Keep", G_SHAPE, 0, 100, 0, 0, "%"),
    MX_F("mix", "Dry/Wet", G_OUTPUT, 0, 100, 100, 0, "%"),
    MX_F("out_gain", "Output", G_OUTPUT, -24, 12, 0, 0, "dB"),
    MX_B("limit", "Safety Limiter", G_OUTPUT, 1),
    MX_B("compare", "Before (hear A)", G_OUTPUT, 0),
    MX_B("bypass", "Bypass", G_OUTPUT, 0),
};

inline void setDefaults(float* p) { for (int i = 0; i < kNumParams; ++i) p[i] = kParams[i].def; }
inline int paramIndex(const char* id) { for (int i = 0; i < kNumParams; ++i) if (!std::strcmp(kParams[i].id, id)) return i; return -1; }
/// Label of the XY pad's vertical axis for a style.
inline const char* shapeLabel(int style) { static const char* n[] = {"Tone first  <->  Texture first", "Vocoder bands", "Pitch glide", "Grain size"}; return n[style < 0 || style > 3 ? 0 : style]; }
inline const char* shapeShort(int style) { static const char* n[] = {"Lead", "Bands", "Glide", "Size"}; return n[style < 0 || style > 3 ? 0 : style]; }
inline float noteHz(float midi) { return 440.f * std::pow(2.f, (midi - 69.f) / 12.f); }
} // namespace mx
