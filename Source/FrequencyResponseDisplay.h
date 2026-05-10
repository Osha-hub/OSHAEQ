// ==============================================================================
// FrequencyResponseDisplay.h — EQ Curve Component Header
// ==============================================================================
// A custom JUCE Component that renders the combined EQ magnitude response and
// provides interactive band nodes (drag handles).
//
// Responsibilities:
//   1. Draws a frequency/gain grid: 20 Hz – 20 kHz (log X), ±24 dB (linear Y).
//   2. Evaluates each active band's IIR transfer function across 512 log-spaced
//      frequency points and draws the combined response as a filled curve.
//   3. Renders colour-coded circular nodes at each band's (freq, gain) position.
//   4. Handles mouse interaction for real-time parameter editing:
//        drag X → frequency  (all filter types, logarithmic)
//        drag Y → gain dB    (Peak and Shelf types only, linear)
//      Uses APVTS gesture calls so DAW automation records correctly.
//   5. Double-clicking a node toggles that band's bypass state.
//   6. Repaints at ~30 fps via juce::Timer.
//
// Architecture role:
//   Instantiated and owned by OSHAEQAudioProcessorEditor.
//   Holds a reference to OSHAEQAudioProcessor to read APVTS and sample rate.
// ==============================================================================

#pragma once

#include <JuceHeader.h>
#include "EQBand.h"

// Forward declaration — avoids a circular include with PluginProcessor.h.
// FrequencyResponseDisplay.cpp includes PluginProcessor.h for the full type.
class OSHAEQAudioProcessor;

// ============================================================
// SECTION: FrequencyResponseDisplay Class
// ============================================================

class FrequencyResponseDisplay : public juce::Component,
                                  public juce::Timer
{
public:
    // ----------------------------------------------------------
    // Construction / destruction
    // ----------------------------------------------------------
    explicit FrequencyResponseDisplay (OSHAEQAudioProcessor& processorRef);
    ~FrequencyResponseDisplay() override = default;

    // ----------------------------------------------------------
    // Component overrides
    // ----------------------------------------------------------
    void paint   (juce::Graphics& g) override;
    void resized () override {}  // layout is dynamic — no fixed children

    // ----------------------------------------------------------
    // Mouse overrides — band node interaction
    // ----------------------------------------------------------
    void mouseDown        (const juce::MouseEvent& e) override;
    void mouseDrag        (const juce::MouseEvent& e) override;
    void mouseUp          (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    // ----------------------------------------------------------
    // Timer — repaint at ~30 fps
    // ----------------------------------------------------------
    void timerCallback() override { updateSpectrum(); repaint(); }

private:
    // ============================================================
    // SECTION: Data
    // ============================================================

    OSHAEQAudioProcessor& processor; ///< Reference to processor (APVTS + sample rate)

    int  draggedBandIndex { -1 };    ///< Band currently being dragged; -1 = none
    bool dragGainActive   { false }; ///< true when the drag also updates gain

    // ============================================================
    // SECTION: Display Constants
    // ============================================================

    static constexpr float kMinFreq    = 20.0f;    ///< Hz — left edge of display
    static constexpr float kMaxFreq    = 20000.0f; ///< Hz — right edge
    static constexpr float kMinGainDb  = -24.0f;   ///< dB — bottom of display
    static constexpr float kMaxGainDb  =  24.0f;   ///< dB — top of display
    static constexpr float kNodeRadius =  8.0f;    ///< Pixel radius of band nodes
    static constexpr int   kNumPoints  = 512;       ///< Frequency points for curve

    // ============================================================
    // SECTION: Colour Helpers
    // ============================================================

    /// Returns the colour used to paint a band node of the given type.
    static juce::Colour bandColour (FilterType type) noexcept;

    // ============================================================
    // SECTION: Coordinate Mapping
    // ============================================================

    /// Maps a frequency (Hz) → X pixel in this component (logarithmic).
    float frequencyToX (float freq) const noexcept;

    /// Maps an X pixel → frequency (Hz).
    float xToFrequency (float x) const noexcept;

    /// Maps a gain (dB) → Y pixel (0 dB is vertically centred).
    float gainDbToY (float gainDb) const noexcept;

    /// Maps a Y pixel → gain (dB).
    float yToGainDb (float y) const noexcept;

    // ============================================================
    // SECTION: Drawing Helpers
    // ============================================================

    void drawBackground (juce::Graphics& g) const;
    void drawGrid       (juce::Graphics& g) const;
    void drawCurve      (juce::Graphics& g, double sampleRate) const;
    void drawBandNodes  (juce::Graphics& g, double sampleRate) const;

    // ============================================================
    // SECTION: Magnitude Response Computation
    // ============================================================

    /// Builds a vector of kNumPoints dB values representing the combined
    /// magnitude response of all active EQ bands at log-spaced frequencies.
    std::vector<float> computeMagnitudeResponse (double sampleRate) const;

    // ============================================================
    // SECTION: Spectrum Analyser
    // ============================================================

    /// Latest smoothed spectrum data, maintained by the display (message thread).
    /// Size matches OSHAEQAudioProcessor::fftSize / 2.
    std::array<float, 1024> smoothedSpectrum {};

    /// Polls the processor for new FFT data and blends it into smoothedSpectrum.
    /// Called each timerCallback tick before repaint().
    void updateSpectrum();

    /// Draws the smoothed spectrum as a filled translucent background layer.
    void drawSpectrum (juce::Graphics& g, double sampleRate) const;

    // ============================================================
    // SECTION: Hit Testing
    // ============================================================

    /// Returns the pixel-space centre of a band node.
    juce::Point<float> bandNodeCentre (int bandIndex, double sampleRate) const;

    /// Returns the band index whose node contains pos, or -1 if none.
    int hitTestBandNode (juce::Point<float> pos, double sampleRate) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrequencyResponseDisplay)
};
