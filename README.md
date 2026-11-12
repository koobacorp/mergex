# MergeX v1.0

MergeX is a real-time sound-morphing effect (VST3, Mac, universal Apple Silicon + Intel). You put it on one
sound (**A**), feed it a second sound (**B**) through the sidechain, and turn the big **Morph** knob. At 0% you hear
only A, exactly as it was. At 100% you hear B's character. In between you get one new sound that is a real mix of
the two, not a volume crossfade.

Original code, original name and original look. It is inspired by the idea of real-time morphing, not a copy of
any other product.

## The main controls

- **Morph** (the big knob) and the **XY pad**. Left to right on the pad is Morph, from A to B. Up and down on the
  pad changes what the current style does most (see below). Dragging the pad moves the knob too.
- The glowing dot on the pad shows where the morph actually is right now, including Motion and Transient Keep.
  The white ring is where you set it.

## The four styles

| Style | What it does | Pad up/down |
|---|---|---|
| **Smooth** | Blends the tone shape and the fine detail of both sounds. The middle is a genuine hybrid. | Up: B's tone arrives first. Down: B's texture arrives first. |
| **Talk** | Vocoder. One sound "speaks" through the other. Choose *A talks through B* or *B talks through A*. | Number of vocoder bands (8 to 40). |
| **Swap** | Takes the pitch from one sound and the tone from the other. In *Pitch of B, tone of A*, the pitch glides from A's to B's as you turn Morph. | Glide: up = smooth pitch glide, down = quick switch near 50%. |
| **Grain** | Chops both sounds into tiny pieces and shuffles them. Morph sets how many pieces come from B. | Grain size (10 to 250 ms). Smoothness adds scatter. |

## The smaller controls

- **SOURCES**: A and B level meters. **B Source** is *Sidechain (auto)*, which uses the sidechain when it has
  sound and the built-in Pad when it doesn't, or a fixed built-in *Noise*, *Saw* or *Pad*. **Tone Note** sets the
  built-in tone's pitch. The built-in tone follows A's loudness, so it goes quiet when A does. **A/B Balance** sets
  how loud B is against A going into the morph.
- **MOTION** moves Morph for you. *LFO* is synced to FL's tempo (1/4 bar to 8 bars, with Sine, Triangle, Ramp up or
  Ramp down). *Follow A* and *Follow B* push Morph up as that sound gets louder. **Depth** sets how far it pushes,
  and a negative value pulls back toward A.
- **SHAPE**:
  - **Smoothness**: low is fast and detailed, high is slurry and blurred, with a short tail.
  - **Formant**: shifts the voice or tone shape up or down by up to 12 semitones. It isn't used in Grain.
  - **Transient Keep**: keeps drum hits and attacks from A punchy while everything else morphs.
- **OUTPUT**: **Dry/Wet**, **Output** gain and a **Safety Limiter** that only touches peaks above -0.3 dB.
- **BEFORE** (top right) lets you hear only A for a quick before/after check.
- **Presets**: 30 factory presets in groups (`<` `>` or click the name). **Save** stores your own in
  `Music/MergeX Presets`. **Randomize** rolls a new morph and leaves your B source, Dry/Wet and Output alone.
- The window is resizable: drag the bottom-right corner.

## Good to know

- Formant and Smoothness fade in over the first 20% of Morph, so 0% always stays your untouched original.
- MergeX adds about 21 ms of latency, the same at every sample rate, and reports it to FL. FL lines things up on
  playback.
- See INSTALL.txt for installing and for routing a sidechain in FL.
