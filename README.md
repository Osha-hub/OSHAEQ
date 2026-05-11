# OSHAEQ

A 6-band parametric equaliser VST3 / AU plugin built with JUCE 7. Real-time spectrum analyser, interactive draggable EQ curve, full DAW automation. Inspired by the workflow of FabFilter Pro-Q 3.

![Version](https://img.shields.io/badge/version-1.0.0-blue)
![JUCE](https://img.shields.io/badge/JUCE-7.x-green)
![Formats](https://img.shields.io/badge/formats-VST3%20%7C%20AU-orange)
![Language](https://img.shields.io/badge/language-C%2B%2B17-blueviolet)

![OSHAEQ screenshot](/OSHAEQ.png)

## Features

### 6-band parametric EQ

| Band | Default type | Default frequency |
| --- | --- | --- |
| 1 | Low Cut (high-pass) | 80 Hz |
| 2 | Low Shelf | 250 Hz |
| 3 | Peak | 800 Hz |
| 4 | Peak | 3 200 Hz |
| 5 | High Shelf | 8 000 Hz |
| 6 | High Cut (low-pass) | 16 000 Hz |

Per-band controls: **Frequency** (20 Hz–20 kHz, log scale), **Gain** (±24 dB, Peak and Shelf only), **Q** (0.1–10.0), **Type** (Low Cut / Low Shelf / Peak / High Shelf / High Cut), **Bypass**.

### Real-time spectrum analyser

- 2 048-point FFT with Hann windowing.
- Post-EQ — shows the signal as it leaves the plugin.
- 1/6-octave proportional smoothing for a readable display.
- Asymmetric time smoothing (fast attack, slow release).

### Interactive EQ display

- Drag band nodes horizontally to change frequency.
- Drag band nodes vertically to change gain (Peak and Shelf types).
- Double-click a node to toggle bypass.
- 512-point magnitude-response curve, redrawn at 30 fps.

### DAW integration

- `AudioProcessorValueTreeState` (APVTS) for every parameter — all automatable.
- Preset save/load via standard DAW project state.
- Proper gesture begin/end calls for clean automation recording.

## DSP architecture

```
 Audio in (stereo)
    │
    ├─ L ──► [Band 0 IIR] ─► [Band 1 IIR] ─► … ─► [Band 5 IIR] ─┐
    │                                                            ├─► Audio out
    └─ R ──► [Band 0 IIR] ─► [Band 1 IIR] ─► … ─► [Band 5 IIR] ─┘
                                                  │
                                          FFT analyser (L only)
```

All filter coefficients are derived from the [Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/) formulas by Robert Bristow-Johnson. Coefficient maths runs in double precision; per-sample processing uses single-precision float buffers.

## File structure

```
OSHAEQ/
├── Source/
│   ├── EQBand.h                          — FilterType enum, EQBand snapshot struct
│   ├── PluginProcessor.h / .cpp          — DSP engine, APVTS, IIR filters, FFT
│   ├── PluginEditor.h / .cpp             — 900×500 plugin window, band control strip
│   └── FrequencyResponseDisplay.h / .cpp — EQ curve + spectrum analyser component
├── JuceLibraryCode/                      — Auto-generated JUCE module wrappers
├── Builds/
│   ├── MacOSX/                           — Xcode project (auto-generated)
│   └── VisualStudio2022/                 — Visual Studio solution (auto-generated)
├── OSHAEQ.jucer                          — Projucer project file (source of truth)
└── README.md
```

## Building

### Requirements

| Tool | Version |
| --- | --- |
| JUCE | 7.x (set the path in Projucer's "Global Paths") |
| Xcode | 14+ (macOS) |
| Visual Studio | 2022 (Windows) |

### macOS (Xcode)

1. Open `OSHAEQ.jucer` in **Projucer**.
2. Set your local JUCE path under *Projucer → Global Paths → Path to JUCE*.
3. Click *Save and Export* to regenerate the Xcode project.
4. Open `Builds/MacOSX/OSHAEQ.xcodeproj`.
5. Select the **OSHAEQ - VST3** scheme and build.

```bash
# Copy to the system VST3 folder after building
cp -r Builds/MacOSX/build/Debug/OSHAEQ.vst3 \
      ~/Library/Audio/Plug-Ins/VST3/
```

### Windows (Visual Studio)

1. Open `OSHAEQ.jucer` in **Projucer** and re-export.
2. Open `Builds/VisualStudio2022/OSHAEQ.sln`.
3. Build the **VST3** target in *Release* configuration.

## Loading in a DAW

After copying the `.vst3` bundle to your system plugin folder, rescan plugins:

- **Ableton Live** — Preferences → Plug-Ins → Rescan.
- **Logic Pro** — Rescans automatically on launch; run `killall -9 AudioComponentRegistrar` if the AU doesn't appear.
- **JUCE AudioPluginHost** — Options → Scan for new or updated VST3 plug-ins.

> **Apple Silicon:** the plugin builds as `arm64`. If your DAW runs under Rosetta, open it natively (uncheck "Open using Rosetta" in Finder → Get Info).

## Background

OSHAEQ started as a self-directed C++/JUCE exercise to consolidate the DSP fundamentals from my MSc in Audio & Music Technology at the University of York. I wanted to step beyond textbook IIR examples and build a real, plugin-format-correct EQ — one that handled DAW automation properly, drew a readable real-time spectrum, and gave the user a direct-manipulation curve rather than just sliders. The architectural target was simple: take the cleanest formulation of biquad design (Bristow-Johnson's *Audio EQ Cookbook*) and wire it through JUCE's parameter-state plumbing without shortcuts. The plugin loads as VST3 / AU and behaves like a normal EQ in any modern DAW.

## Honest limitations / what I'd improve next

- Filters are textbook biquads with no smoothing on parameter changes — large frequency or Q sweeps will click on automation. Should add a `LinearSmoothedValue` for each band's parameters.
- Spectrum analyser runs on the left channel only — a stereo or mid/side mode is on the roadmap.
- Filter coefficients recalculate per sample inside the audio thread; for steady-state automation that's wasteful but currently bounded. A dirty-flag → recompute-on-change pattern would be cleaner.
- The Frequency Response Display recalculates the 512-point magnitude curve on every paint at 30 fps. Caching the curve until a parameter changes would cut UI CPU.
- No oversampling option; the EQ aliases mildly near Nyquist on extreme high-shelf boosts.
- No unit tests yet — the coefficient math is testable headlessly without booting JUCE's audio device.

## Roadmap

- [ ] Mid/Side processing mode
- [ ] Adjustable filter slope (12 / 24 / 48 dB/oct on cut filters)
- [ ] Preset browser
- [ ] Spectrum analyser peak-hold display
- [ ] MIDI learn for all parameters
- [ ] Parameter smoothing to eliminate automation zipper

## License

Source available for portfolio review. Not licensed for redistribution or commercial use. Built with [JUCE 7](https://juce.com) under personal licence terms — distribution of compiled binaries would require complying with JUCE's licence (GPL or commercial).

<!--
  Repository description (for GitHub UI):
  6-band parametric EQ — VST3/AU plugin in C++/JUCE. Real-time FFT analyser,
  interactive draggable EQ curve, RBJ Cookbook coefficients, full DAW automation.

  Suggested topics:
  juce, vst3, audio-unit, audio-plugin, equalizer, parametric-eq, cpp,
  dsp, audio-programming, fft, spectrum-analyzer
-->
