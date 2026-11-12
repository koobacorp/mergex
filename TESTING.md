# MergeX – how it was tested

Plain summary: the sound engine and the plugin wrapper were tested on a Linux box.
The real macOS build (the .vst3 you'd load in FL Studio) has **not** been built yet,
because GitHub Actions refused to start the Mac job (account billing). Nothing has been tried in FL Studio.

## What ran (Linux box, 12 Nov 2026)

| What | Result |
|---|---|
| Engine tests (`tests/test_engine.cpp`, pure C++, no JUCE) | **460 passed, 0 failed** |
| Plugin tests (`tests/plugin_tests.cpp`, JUCE processor + editor, under Xvfb) | **213 passed, 0 failed** |
| pluginval, strictness 10, on a **Linux** VST3 build | **SUCCESS** (all test groups passed) |
| CPU, 48 kHz, 256-sample blocks, one core | about 2.2–3.1 % (Swap with glide is the heaviest) |
| Latency reported to the host | 1024 samples at 44.1/48 kHz (23.2 / 21.3 ms); 2048 at 88.2/96 kHz; 4096 at 192 kHz |

Logs: `dist/engine-test-log.txt`, `dist/plugin-test-log.txt`, `dist/pluginval-linux-strictness10-log.txt`.

### What the engine tests check (short version)
- Morph 0% is bit-for-bit the dry input, just delayed by the reported latency, in every style, every B source,
  odd block sizes and random settings. The same holds for Dry/Wet 0, Before, and bypass.
- Morph 100% matches B's character (correlation above 0.995 in Smooth).
- The middle is a real spectral blend (a tilt test lands 11.6 dB against 12.0 dB predicted, and the brightness centroid moves smoothly).
- Talk, Swap and Grain each do what the README says (pitch, envelope and grain-share measurements).
- There are no clicks when knobs move, no runaway levels over 60 random setups, no NaNs or denormals, and the limiter holds.
- The LFO shapes and followers, transient keeper, formant shift, smoothness, presets and randomize all behave as designed.

## Linux-only notes (do not affect the Mac build)
- The box compiler (zig/clang 20) rejects one unused template constructor in JUCE 8.0.4's
  `juce_AudioPluginInstance.h`. A **local copy** of JUCE was patched for the Linux check only. The repo and the Mac CI use stock JUCE 8.0.4.
- zig-built Linux `.so` files crash at process exit after being unloaded. This is a toolchain bug, reproduced with a
  5-line library that has nothing to do with MergeX. The Linux check links with `-z nodelete` to avoid it. macOS is not affected.
- The screenshots in `dist/screenshots/` come from this Linux build (JUCE draws the same on macOS, but fonts may differ slightly).

## Still to do on macOS (CI workflow `.github/workflows/macos.yml`)
Once GitHub billing is fixed, re-run the workflow. It builds universal arm64 + x86_64, runs both test suites,
runs pluginval at strictness 10 on the real Mac VST3, ad-hoc signs it, and makes the zip.
