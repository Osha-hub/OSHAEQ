// ==============================================================================
// PluginEditor.h — OSHAEQ GUI Editor Header
// ==============================================================================
// OSHAEQAudioProcessorEditor lays out the full 900×500 plugin window:
//
//   ┌─────────────────────────────────────────────┐  ← 900 px wide
//   │  FrequencyResponseDisplay  (top 70%)        │  ← 350 px tall
//   │  EQ curve + drag nodes                      │
//   ├─────────────────────────────────────────────┤
//   │  Band Control Strip       (bottom 30%)      │  ← 150 px tall
//   │  [ Band 0 ] [ Band 1 ] … [ Band 5 ]         │
//   └─────────────────────────────────────────────┘
//
// Each band column in the strip contains:
//   • Type label ("LP CUT", "PEAK", etc.)
//   • Frequency knob (rotary slider + APVTS attachment)
//   • Gain knob      (rotary slider + APVTS attachment, greyed for cut types)
//   • Q knob         (rotary slider + APVTS attachment)
//   • Bypass LED button (APVTS attachment)
//
// All sliders and buttons use APVTS attachments — changes made in the strip
// are automatically reflected in the display curve and DSP engine.
// ==============================================================================

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FrequencyResponseDisplay.h"

// ============================================================
// SECTION: Editor Class Declaration
// ============================================================

class OSHAEQAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit OSHAEQAudioProcessorEditor (OSHAEQAudioProcessor&);
    ~OSHAEQAudioProcessorEditor() override;

    // ----------------------------------------------------------
    // Component overrides
    // ----------------------------------------------------------
    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    // ============================================================
    // SECTION: Processor Reference
    // ============================================================

    OSHAEQAudioProcessor& audioProcessor;

    // ============================================================
    // SECTION: EQ Curve Display
    // ============================================================

    FrequencyResponseDisplay freqDisplay;

    // ============================================================
    // SECTION: Band Control Strip (6 bands × 5 controls)
    // ============================================================

    // Rotary sliders for frequency, gain, and Q (one per band).
    juce::Slider freqSliders [NUM_BANDS];
    juce::Slider gainSliders [NUM_BANDS];
    juce::Slider qSliders    [NUM_BANDS];

    // TextButton used as a LED-style bypass toggle.
    // Green text = active; grey = bypassed.
    juce::TextButton bypassButtons [NUM_BANDS];

    // Labels displayed above each band column (e.g. "LP CUT", "PEAK").
    juce::Label typeLabels [NUM_BANDS];

    // Value readout labels below each knob row (freq Hz, gain dB, Q).
    juce::Label freqLabels [NUM_BANDS];
    juce::Label gainLabels [NUM_BANDS];
    juce::Label qLabels    [NUM_BANDS];

    // ============================================================
    // SECTION: APVTS Attachments
    // ============================================================

    // Attachments wire sliders/buttons to APVTS parameters so DAW automation,
    // preset loading, and GUI interaction all stay in sync automatically.
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        freqAttachments [NUM_BANDS],
        gainAttachments [NUM_BANDS],
        qAttachments    [NUM_BANDS];

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        bypassAttachments [NUM_BANDS];

    // ============================================================
    // SECTION: Private Helpers
    // ============================================================

    /// Returns a short display name for the given filter type index.
    static juce::String filterTypeName (int typeIndex);

    /// Configures a rotary slider with the shared look-and-feel settings.
    static void setupRotarySlider (juce::Slider& s);

    /// Updates readout labels and gain-knob alpha from current APVTS values.
    void updateReadoutLabels();

    /// juce::Timer callback — fires at 10 Hz to refresh readout labels.
    void timerCallback() override { updateReadoutLabels(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OSHAEQAudioProcessorEditor)
};
