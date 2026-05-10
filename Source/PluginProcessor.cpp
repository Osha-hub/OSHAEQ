// ==============================================================================
// PluginProcessor.cpp — OSHAEQ DSP Processor Implementation
// ==============================================================================
//
// SIGNAL FLOW:
//   DAW Audio Buffer (stereo float)
//     │
//     ├─ Left channel AudioBlock ──► Band 0 IIR ──► Band 1 IIR ──► … ──► Band 5 IIR ─┐
//     │                                                                                 ├─► Output
//     └─ Right channel AudioBlock ─► Band 0 IIR ──► Band 1 IIR ──► … ──► Band 5 IIR ─┘
//
// Each "IIR" stage is a juce::dsp::IIR::Filter<float> (Direct-Form II biquad).
// Bypassed bands are skipped entirely — the audio passes through unchanged.
//
// PARAMETER CHANGE HANDLING:
//   parameterChanged() (any thread) sets parametersChanged = true.
//   processBlock() checks the flag at the top of each block and, if set,
//   calls updateFilters() to rebuild all 12 coefficient sets before processing.
//   Coefficient assignment is done on the audio thread only, which avoids
//   races with the filter's internal state variables (z1, z2).
//
// REFERENCES:
//   Audio EQ Cookbook — Robert Bristow-Johnson (RBJ)
//   https://www.w3.org/TR/audio-eq-cookbook
// ==============================================================================

#include "PluginProcessor.h"
#include "PluginEditor.h"

// ============================================================
// SECTION: Construction / Destruction
// ============================================================

OSHAEQAudioProcessor::OSHAEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                      ),
      // APVTS constructor: (processor, undoManager, stateIdentifier, parameterLayout)
      // Passing nullptr for undoManager disables undo — acceptable for a plugin DSP.
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    // Register this processor as a listener for every band parameter so that
    // parameterChanged() is called whenever any knob/slider moves.
    // This sets parametersChanged = true, which triggers coefficient recalc
    // at the start of the next processBlock call.
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        juce::String prefix = "band" + juce::String (i) + "_";
        apvts.addParameterListener (prefix + "freq",   this);
        apvts.addParameterListener (prefix + "gain",   this);
        apvts.addParameterListener (prefix + "q",      this);
        apvts.addParameterListener (prefix + "type",   this);
        apvts.addParameterListener (prefix + "bypass", this);
    }
}

OSHAEQAudioProcessor::~OSHAEQAudioProcessor()
{
    // Remove all listeners before the processor is destroyed to prevent
    // dangling callbacks into a partially-destructed object.
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        juce::String prefix = "band" + juce::String (i) + "_";
        apvts.removeParameterListener (prefix + "freq",   this);
        apvts.removeParameterListener (prefix + "gain",   this);
        apvts.removeParameterListener (prefix + "q",      this);
        apvts.removeParameterListener (prefix + "type",   this);
        apvts.removeParameterListener (prefix + "bypass", this);
    }
}

// ============================================================
// SECTION: Parameter Layout — APVTS Factory
// ============================================================

juce::AudioProcessorValueTreeState::ParameterLayout
OSHAEQAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Default band settings specified in the project brief:
    //   Band 0: LowCut    80 Hz   Q=0.707 Gain=0
    //   Band 1: LowShelf  250 Hz  Q=0.707 Gain=0
    //   Band 2: Peak      800 Hz  Q=1.0   Gain=0
    //   Band 3: Peak      3200 Hz Q=1.0   Gain=0
    //   Band 4: HighShelf 8000 Hz Q=0.707 Gain=0
    //   Band 5: HighCut   16000 Hz Q=0.707 Gain=0

    // Per-band defaults (indexed 0..5):
    const float defaultFreqs[NUM_BANDS]  = { 80.f, 250.f,  800.f, 3200.f, 8000.f, 16000.f };
    const float defaultQs   [NUM_BANDS]  = { 0.707f, 0.707f, 1.0f,  1.0f,   0.707f,  0.707f };
    const int   defaultTypes [NUM_BANDS] = { 0, 1, 2, 2, 3, 4 }; // FilterType enum values

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        juce::String prefix = "band" + juce::String (i) + "_";

        // --- Frequency parameter -------------------------------------------
        // NormalisableRange with skewFactor 0.25 creates a logarithmic-feeling
        // mapping so that low frequencies have finer control resolution than
        // high ones — matching how human hearing perceives pitch spacing.
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "freq", 1 },
            "Band " + juce::String (i) + " Frequency",
            juce::NormalisableRange<float> (20.0f, 20000.0f, 0.1f, 0.25f),
            defaultFreqs[i]  // Hz
        ));

        // --- Gain parameter ------------------------------------------------
        // Gain is linear in dB from -24 to +24.  Cut filter types ignore this.
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "gain", 1 },
            "Band " + juce::String (i) + " Gain",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f),
            0.0f  // dB
        ));

        // --- Q parameter --------------------------------------------------
        // Q controls bandwidth.  Low Q = wide, gentle curve.  High Q = narrow, surgical.
        // skewFactor 0.5 (sqrt) gives better resolution at the musically useful
        // range of 0.1–3.0 while still allowing up to 10.
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "q", 1 },
            "Band " + juce::String (i) + " Q",
            juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.5f),
            defaultQs[i]
        ));

        // --- Type parameter (choice) --------------------------------------
        // Maps to FilterType enum: 0=LowCut 1=LowShelf 2=Peak 3=HighShelf 4=HighCut
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { prefix + "type", 1 },
            "Band " + juce::String (i) + " Type",
            juce::StringArray { "LowCut", "LowShelf", "Peak", "HighShelf", "HighCut" },
            defaultTypes[i]
        ));

        // --- Bypass parameter (bool) --------------------------------------
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { prefix + "bypass", 1 },
            "Band " + juce::String (i) + " Bypass",
            false  // not bypassed by default
        ));
    }

    return { params.begin(), params.end() };
}

// ============================================================
// SECTION: Lifecycle — prepare / release
// ============================================================

void OSHAEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // ProcessSpec describes the audio context to all DSP objects.
    // numChannels = 1 because we process each channel with its own filter array.
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels      = 1; // single-channel blocks fed to each filter

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        // prepare() allocates internal delay-line memory and resets filter state.
        leftFilters [i].prepare (spec);
        rightFilters[i].prepare (spec);
    }

    // Force coefficient calculation on the first processBlock call.
    parametersChanged = true;
}

void OSHAEQAudioProcessor::releaseResources()
{
    // Nothing to explicitly free; JUCE filter objects manage their own memory.
}

// ============================================================
// SECTION: Bus Layout Validation
// ============================================================

#ifndef JucePlugin_PreferredChannelConfigurations
bool OSHAEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // Accept mono or stereo only; input must match output.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

// ============================================================
// SECTION: Audio Processing — processBlock
// ============================================================

void OSHAEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals; // flush subnormals to zero (CPU performance)

    // ── Coefficient update ─────────────────────────────────────────────────
    // exchange() atomically reads the flag and sets it to false in one operation,
    // ensuring no parameter change is missed between the read and the clear.
    if (parametersChanged.exchange (false))
        updateFilters();

    // ── Clear unused output channels ────────────────────────────────────────
    auto totalIn  = getTotalNumInputChannels();
    auto totalOut = getTotalNumOutputChannels();
    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // ── Create an AudioBlock view over the entire buffer ───────────────────
    // AudioBlock is a non-owning view; it does not copy audio data.
    juce::dsp::AudioBlock<float> block (buffer);

    // ── Process left channel ───────────────────────────────────────────────
    // getSingleChannelBlock returns a 1-channel sub-view sharing the same memory.
    auto leftBlock = block.getSingleChannelBlock (0);
    juce::dsp::ProcessContextReplacing<float> leftCtx (leftBlock);

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        // Read bypass flag from the atomic float (0.0 = active, 1.0 = bypassed).
        bool bypassed = apvts.getRawParameterValue ("band" + juce::String (i) + "_bypass")
                             ->load() > 0.5f;
        if (!bypassed)
            leftFilters[i].process (leftCtx); // in-place biquad filtering
    }

    // ── Process right channel (stereo only) ────────────────────────────────
    if (buffer.getNumChannels() > 1)
    {
        auto rightBlock = block.getSingleChannelBlock (1);
        juce::dsp::ProcessContextReplacing<float> rightCtx (rightBlock);

        for (int i = 0; i < NUM_BANDS; ++i)
        {
            bool bypassed = apvts.getRawParameterValue ("band" + juce::String (i) + "_bypass")
                                 ->load() > 0.5f;
            if (!bypassed)
                rightFilters[i].process (rightCtx);
        }
    }
}

// ============================================================
// SECTION: Filter Coefficient Calculation
// ============================================================

void OSHAEQAudioProcessor::updateFilters()
{
    // Guard: coefficients cannot be computed without a valid sample rate.
    if (currentSampleRate <= 0.0)
        return;

    const double sr = currentSampleRate; // cache to avoid repeated reads

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        // Read all parameters from their atomic floats in one pass.
        juce::String prefix = "band" + juce::String (i) + "_";
        const float freq    = apvts.getRawParameterValue (prefix + "freq")->load();
        const float gainDb  = apvts.getRawParameterValue (prefix + "gain")->load();
        const float q       = apvts.getRawParameterValue (prefix + "q")->load();
        const int   typeInt = static_cast<int> (
                                  apvts.getRawParameterValue (prefix + "type")->load());

        const auto type = static_cast<FilterType> (typeInt);

        // Convert gain from dB to linear amplitude ratio.
        // The IIR shelf/peak formulas use linear gain: dBtoGain(0 dB) = 1.0 (unity).
        const float gainLinear = juce::Decibels::decibelsToGain (gainDb);

        // Build the coefficient set using Coefficients<float> to match the filter type.
        // Although the storage is float, the JUCE factory methods accept double arguments
        // for sampleRate, frequency, and Q — so the intermediate arithmetic (the RBJ
        // formulas for cos/sin, prewarping, etc.) runs at double precision.
        // This satisfies the spec: "double-precision internally for coefficient
        // calculation, float for audio buffers."
        // Reference: Audio EQ Cookbook by RBJ — each recipe cited below.
        juce::dsp::IIR::Coefficients<float>::Ptr coeffs;

        switch (type)
        {
            case FilterType::LowCut:
                // "Low Cut" = removes LOW frequencies → this is a HIGH-PASS filter.
                // RBJ recipe: HPF.  Q controls resonance at the corner frequency.
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, freq, q);
                break;

            case FilterType::LowShelf:
                // Boosts or cuts all frequencies below 'freq' by 'gainLinear'.
                // RBJ recipe: Low shelf filter.  Q adjusts the shelf transition slope.
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                             sr, freq, q, gainLinear);
                break;

            case FilterType::Peak:
                // Bell-shaped boost/cut centred at 'freq' with bandwidth set by Q.
                // Higher Q = narrower bell.  gainLinear = 1.0 (0 dB) → allpass.
                // RBJ recipe: Peaking EQ filter.
                coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                             sr, freq, q, gainLinear);
                break;

            case FilterType::HighShelf:
                // Boosts or cuts all frequencies above 'freq' by 'gainLinear'.
                // RBJ recipe: High shelf filter.
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                             sr, freq, q, gainLinear);
                break;

            case FilterType::HighCut:
                // "High Cut" = removes HIGH frequencies → this is a LOW-PASS filter.
                // RBJ recipe: LPF.
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, freq, q);
                break;

            default:
                jassertfalse; // Unhandled FilterType — should never happen
                continue;
        }

        if (coeffs == nullptr)
            continue; // Safety: skip if coefficient factory returned null

        // Dereference and copy: *filter.coefficients is the live Coefficients<float>
        // object held inside the IIR filter; *coeffs is our freshly computed one.
        // Both are the same template type, so operator= is well-defined and simply
        // copies the five b/a coefficient values.
        // This runs on the audio thread only, so there is no race with the filter state.
        *leftFilters [i].coefficients = *coeffs;
        *rightFilters[i].coefficients = *coeffs;
    }
}

// ============================================================
// SECTION: Parameter Change Callback
// ============================================================

void OSHAEQAudioProcessor::parameterChanged (const juce::String& /*parameterID*/,
                                              float /*newValue*/)
{
    // Set the atomic flag.  processBlock() will pick this up at the start of
    // the next audio callback and call updateFilters().
    // We do NOT recalculate here because parameterChanged() may be called from
    // the message thread, which would race with the audio thread's filter state.
    parametersChanged = true;
}

// ============================================================
// SECTION: Editor
// ============================================================

juce::AudioProcessorEditor* OSHAEQAudioProcessor::createEditor()
{
    return new OSHAEQAudioProcessorEditor (*this);
}

// ============================================================
// SECTION: State Save / Load (preset support)
// ============================================================

void OSHAEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Serialise the entire APVTS state tree to binary XML.
    // This automatically covers all 30 band parameters.
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void OSHAEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Restore APVTS state from binary XML (called by the DAW on project load).
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr
        && xmlState->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
        // Flag a coefficient update so the filters immediately reflect the loaded state.
        parametersChanged = true;
    }
}

// ============================================================
// SECTION: Plugin Entry Point
// ============================================================

/// DAW calls this to instantiate the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OSHAEQAudioProcessor();
}
