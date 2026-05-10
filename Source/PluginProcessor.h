// ==============================================================================
// PluginProcessor.h — OSHAEQ DSP Processor Header
// ==============================================================================
// OSHAEQAudioProcessor is the audio engine of the plugin:
//   • Owns the AudioProcessorValueTreeState (APVTS) — the single source of
//     truth for all 30 band parameters (freq, gain, Q, type, bypass × 6 bands).
//   • Owns 6 IIR biquad filters per channel (left + right = 12 total).
//   • Listens for parameter changes and recalculates filter coefficients on
//     the audio thread at the start of the next processBlock call.
//   • Exposes getAPVTS() and getCurrentSampleRate() to the editor and display.
//
// Signal flow overview (full details in PluginProcessor.cpp):
//   AudioBuffer → Band 0 IIR → Band 1 IIR → … → Band 5 IIR → Output
// ==============================================================================

#pragma once

#include <JuceHeader.h>
#include "EQBand.h"

// ============================================================
// SECTION: Constants
// ============================================================

static constexpr int NUM_BANDS = 6; ///< Fixed number of EQ bands

// ============================================================
// SECTION: Processor Class Declaration
// ============================================================

class OSHAEQAudioProcessor : public juce::AudioProcessor,
                               public juce::AudioProcessorValueTreeState::Listener
{
public:
    // ----------------------------------------------------------
    // Construction / destruction
    // ----------------------------------------------------------
    OSHAEQAudioProcessor();
    ~OSHAEQAudioProcessor() override;

    // ----------------------------------------------------------
    // AudioProcessor overrides — lifecycle
    // ----------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    // ----------------------------------------------------------
    // AudioProcessor overrides — bus layout
    // ----------------------------------------------------------
   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    // ----------------------------------------------------------
    // AudioProcessor overrides — processing
    // ----------------------------------------------------------
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // ----------------------------------------------------------
    // AudioProcessor overrides — editor
    // ----------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ----------------------------------------------------------
    // AudioProcessor overrides — identity
    // ----------------------------------------------------------
    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi()  const override
    {
       #if JucePlugin_WantsMidiInput
        return true;
       #else
        return false;
       #endif
    }
    bool producesMidi() const override
    {
       #if JucePlugin_ProducesMidiOutput
        return true;
       #else
        return false;
       #endif
    }
    bool isMidiEffect() const override
    {
       #if JucePlugin_IsMidiEffect
        return true;
       #else
        return false;
       #endif
    }
    double getTailLengthSeconds() const override { return 0.0; }

    // ----------------------------------------------------------
    // AudioProcessor overrides — programs / presets
    // ----------------------------------------------------------
    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // ----------------------------------------------------------
    // AudioProcessor overrides — state save/load
    // ----------------------------------------------------------
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ----------------------------------------------------------
    // APVTS::Listener override
    // ----------------------------------------------------------
    /// Called on any thread when a parameter value changes.
    /// Simply raises the atomic flag so processBlock recalculates coefficients.
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    // ----------------------------------------------------------
    // FFT size constants — declared public so FrequencyResponseDisplay
    // can size its smoothedSpectrum array without hardcoding a literal.
    // Must appear before getFFTMagnitudes() which uses fftSize in its signature.
    // ----------------------------------------------------------
    static constexpr int fftOrder = 11;         // 2^11 = 2048 samples per FFT block
    static constexpr int fftSize  = 1 << fftOrder;

    // ----------------------------------------------------------
    // Public accessors used by the editor and display component
    // ----------------------------------------------------------
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    /// Safe to call from the message thread; returns 0 before prepareToPlay.
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }

    /// Called by FrequencyResponseDisplay to retrieve the latest spectrum data.
    /// Returns false if no new FFT block has arrived since the last call.
    bool getFFTMagnitudes (std::array<float, fftSize / 2>& dest)
    {
        if (! fftDataReady.exchange (false))
            return false;
        dest = fftMagnitudes;
        return true;
    }

    // ----------------------------------------------------------
    // Static factory — called by the APVTS member initialiser
    // ----------------------------------------------------------
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    // ============================================================
    // SECTION: Parameter State
    // ============================================================

    /// APVTS is the canonical store for all 30 band parameters.
    /// It handles serialisation, DAW automation, and thread-safe atomic reads
    /// via getRawParameterValue() which returns std::atomic<float>*.
    juce::AudioProcessorValueTreeState apvts;

    // ============================================================
    // SECTION: IIR Filter Banks
    // ============================================================

    /// One juce::dsp::IIR::Filter (Direct-Form II biquad) per band, per channel.
    /// Processing is performed channel-by-channel using single-channel AudioBlock
    /// views so each filter maintains independent state per channel.
    std::array<juce::dsp::IIR::Filter<float>, NUM_BANDS> leftFilters;
    std::array<juce::dsp::IIR::Filter<float>, NUM_BANDS> rightFilters;

    // ============================================================
    // SECTION: State Tracking
    // ============================================================

    double currentSampleRate { 0.0 };

    /// Set true by parameterChanged() (any thread); atomically cleared by
    /// processBlock() (audio thread) when it recalculates coefficients.
    /// Initialised true so prepareToPlay triggers the first coefficient update.
    std::atomic<bool> parametersChanged { true };

    // ============================================================
    // SECTION: Spectrum Analyser (FFT)
    // ============================================================
    // fftOrder and fftSize are declared public above (needed in getFFTMagnitudes signature).

    juce::dsp::FFT fft { fftOrder };

    /// Hann window reduces spectral leakage at the cost of slightly wider peaks.
    /// Applied to each block of samples before the FFT.
    juce::dsp::WindowingFunction<float> window {
        (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };

    /// Circular accumulation buffer — samples from the left channel are pushed
    /// here one at a time until fftSize samples are ready.
    std::array<float, fftSize * 2> fftFifo {};   // *2 for in-place complex FFT
    int fifoIndex { 0 };

    /// The magnitude spectrum written by the audio thread and read by the display.
    /// Accessing this from two threads without a lock is intentional for a visual
    /// display: the worst case is one corrupted frame (unnoticeable to the eye).
    std::array<float, fftSize / 2> fftMagnitudes {};

    /// Set true by the audio thread when a fresh FFT block is available.
    /// Cleared by the display thread after it copies the data.
    std::atomic<bool> fftDataReady { false };

    /// Pushes one sample into the accumulation FIFO.  When the FIFO is full,
    /// computes the FFT and writes the result to fftMagnitudes.
    void pushSampleToFFTFifo (float sample) noexcept;

    // ============================================================
    // SECTION: Private Helpers
    // ============================================================

    /// Reads current APVTS values and rebuilds IIR coefficients for all bands.
    /// Must only be called from the audio thread (inside processBlock).
    void updateFilters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OSHAEQAudioProcessor)
};
