# OSHAEQ

A parametric equaliser VST3/AU plugin built with JUCE, inspired by FabFilter Pro-Q 3.

![OSHAEQ](https://img.shields.io/badge/version-1.0.0-blue) ![JUCE](https://img.shields.io/badge/JUCE-7.x-green) ![Formats](https://img.shields.io/badge/formats-VST3%20%7C%20AU-orange)

---

## Features

### 6-Band Parametric EQ
| Band | Default Type | Default Frequency |
|------|-------------|-------------------|
| 1 | Low Cut (High-Pass) | 80 Hz |
| 2 | Low Shelf | 250 Hz |
| 3 | Peak | 800 Hz |
| 4 | Peak | 3200 Hz |
| 5 | High Shelf | 8000 Hz |
| 6 | High Cut (Low-Pass) | 16000 Hz |

Each band exposes:
- **Frequency** — 20 Hz to 20 kHz (logarithmic)
- **Gain** — ±24 dB (Peak and Shelf types only)
- **Q** — 0.1 to 10.0
- **Type** — Low Cut, Low Shelf, Peak, High Shelf, High Cut
- **Bypass** — per-band toggle

### Real-Time Spectrum Analyser
- 2048-point FFT with Hann windowing
- Post-EQ analysis — shows the signal as it leaves the plugin
- 1/6-octave proportional smoothing for a clean, readable display
- Asymmetric time smoothing (fast attack, slow release)

### Interactive EQ Display
- Drag band nodes horizontally to change frequency
- Drag band nodes vertically to change gain (Peak and Shelf types)
- Double-click a node to toggle bypass
- 512-point magnitude response curve redraws at 30 fps

### DAW Integration
- Full **AudioProcessorValueTreeState** (APVTS) — all parameters are automatable
- Preset save/load via standard DAW project state
- Proper gesture begin/end calls for automation recording

---

## DSP Architecture

```
Audio Input (stereo)
    │
    ├─ Left channel ──► [Band 0 IIR] ──► [Band 1 IIR] ──► … ──► [Band 5 IIR] ─┐
    │                                                                             ├─► Output
    └─ Right channel ─► [Band 0 IIR] ──► [Band 1 IIR] ──► … ──► [Band 5 IIR] ─┘
                                                                        │
                                                               FFT Analyser (left ch)
```

All filter coefficients are computed using the **Audio EQ Cookbook** formulas by Robert Bristow-Johnson. Coefficient arithmetic runs in double precision; audio processing uses float buffers.

---

## File Structure

```
OSHAEQ/
├── Source/
│   ├── EQBand.h                      — FilterType enum, EQBand snapshot struct
│   ├── PluginProcessor.h/.cpp        — DSP engine, APVTS, IIR filters, FFT
│   ├── PluginEditor.h/.cpp           — 900×500 plugin window, band control strip
│   ├── FrequencyResponseDisplay.h/.cpp — EQ curve + spectrum analyser component
├── JuceLibraryCode/                  — Auto-generated JUCE module wrappers
├── Builds/
│   ├── MacOSX/                       — Xcode project
│   └── VisualStudio2026/             — Visual Studio solution
├── OSHAEQ.jucer                      — Projucer project file
└── README.md
```

---

## Building

### Requirements

| Tool | Version |
|------|---------|
| JUCE | 7.x (at `/Users/tyagofe/JUCE`) |
| Xcode | 14+ (macOS) |
| Visual Studio | 2022/2026 (Windows) |

### macOS (Xcode)

1. Open `OSHAEQ.jucer` in **Projucer**
2. Click **Save and Export** to regenerate the Xcode project
3. Open `Builds/MacOSX/OSHAEQ.xcodeproj`
4. Select the **OSHAEQ - VST3** scheme and build

```bash
# Copy to system VST3 folder after building
cp -r Builds/MacOSX/build/Debug/OSHAEQ.vst3 \
      ~/Library/Audio/Plug-Ins/VST3/
```

### Windows (Visual Studio)

1. Open `OSHAEQ.jucer` in **Projucer** and re-export
2. Open `Builds/VisualStudio2026/OSHAEQ.sln`
3. Build the **VST3** target in Release configuration

---

## Loading in a DAW

After copying the `.vst3` bundle to your system plugin folder, rescan plugins in your DAW:

- **Ableton Live** — Preferences → Plug-Ins → Rescan
- **Logic Pro** — Rescans automatically on launch; run `killall -9 AudioComponentRegistrar` if the AU doesn't appear
- **JUCE AudioPluginHost** — Options → Scan for new or updated VST3 plug-ins

> **Apple Silicon note:** The plugin builds as `arm64`. If your DAW runs under Rosetta, open it natively (uncheck "Open using Rosetta" in Finder → Get Info).

---

## Roadmap

- [ ] Mid/Side processing mode
- [ ] Adjustable filter slope (12/24/48 dB/oct for cut filters)
- [ ] Preset browser
- [ ] Spectrum analyser peak-hold display
- [ ] MIDI learn for all parameters

---

## License

Private / internal use. Built with [JUCE](https://juce.com) — see JUCE licence terms for distribution requirements.
