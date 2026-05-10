// ==============================================================================
// FrequencyResponseDisplay.cpp — EQ Curve Component Implementation
// ==============================================================================
// See FrequencyResponseDisplay.h for the full description of responsibilities.
//
// KEY DESIGN NOTES:
//   • All pixel↔parameter mapping uses logarithmic X (frequency) and linear Y (dB).
//   • Magnitude response is computed by creating temporary double-precision
//     Coefficients for each active band, then calling getMagnitudeForFrequency()
//     at 512 log-spaced points and accumulating results in dB.
//   • Band node drag uses APVTS gesture calls so DAW automation works correctly:
//       beginChangeGesture() on mouseDown
//       setValueNotifyingHost() on mouseDrag
//       endChangeGesture()     on mouseUp
// ==============================================================================

#include "FrequencyResponseDisplay.h"
#include "PluginProcessor.h"   // full definition needed here (only forward-declared in .h)

// ============================================================
// SECTION: Construction
// ============================================================

FrequencyResponseDisplay::FrequencyResponseDisplay (OSHAEQAudioProcessor& processorRef)
    : processor (processorRef)
{
    // Start the 30 fps repaint timer.
    // The timer fires timerCallback() which calls repaint(), keeping the curve
    // in sync with parameter changes made via the bottom strip controls or automation.
    startTimerHz (30);
}

// ============================================================
// SECTION: Colour Helpers
// ============================================================

juce::Colour FrequencyResponseDisplay::bandColour (FilterType type) noexcept
{
    switch (type)
    {
        case FilterType::LowCut:    return juce::Colour (0xff4a90d9); // blue
        case FilterType::LowShelf:  return juce::Colour (0xff00b8a0); // teal
        case FilterType::Peak:      return juce::Colour (0xffdddddd); // light grey
        case FilterType::HighShelf: return juce::Colour (0xffff8c42); // orange
        case FilterType::HighCut:   return juce::Colour (0xffff4757); // red
        default:                    return juce::Colours::white;
    }
}

// ============================================================
// SECTION: Coordinate Mapping
// ============================================================

float FrequencyResponseDisplay::frequencyToX (float freq) const noexcept
{
    // Logarithmic mapping: X = width × log(f / f_min) / log(f_max / f_min)
    // This matches how human hearing perceives pitch — each octave occupies
    // the same horizontal distance regardless of absolute frequency.
    const float logMin = std::log (kMinFreq);
    const float logMax = std::log (kMaxFreq);
    return (float) getWidth() * (std::log (freq) - logMin) / (logMax - logMin);
}

float FrequencyResponseDisplay::xToFrequency (float x) const noexcept
{
    // Inverse of frequencyToX:  f = f_min × (f_max/f_min)^(x/width)
    const float logMin = std::log (kMinFreq);
    const float logMax = std::log (kMaxFreq);
    return std::exp (logMin + (x / (float) getWidth()) * (logMax - logMin));
}

float FrequencyResponseDisplay::gainDbToY (float gainDb) const noexcept
{
    // Linear mapping: 0 dB → vertical centre; +24 dB → top; -24 dB → bottom.
    const float gainRange = kMaxGainDb - kMinGainDb; // 48 dB total
    return (float) getHeight() * (1.0f - (gainDb - kMinGainDb) / gainRange);
}

float FrequencyResponseDisplay::yToGainDb (float y) const noexcept
{
    // Inverse of gainDbToY.
    const float gainRange = kMaxGainDb - kMinGainDb;
    return kMinGainDb + (1.0f - y / (float) getHeight()) * gainRange;
}

// ============================================================
// SECTION: Magnitude Response Computation
// ============================================================

std::vector<float>
FrequencyResponseDisplay::computeMagnitudeResponse (double sampleRate) const
{
    // Accumulate the response in dB so that multiplying linear magnitudes
    // becomes simple addition:  dB_total = sum(dB_band_i)
    std::vector<float> magDb (kNumPoints, 0.0f); // initialised to 0 dB = unity

    auto& apvts = processor.getAPVTS();

    for (int band = 0; band < NUM_BANDS; ++band)
    {
        juce::String prefix = "band" + juce::String (band) + "_";

        // Read all parameters atomically (single load per parameter).
        const float freq    = apvts.getRawParameterValue (prefix + "freq")->load();
        const float gainDb  = apvts.getRawParameterValue (prefix + "gain")->load();
        const float q       = apvts.getRawParameterValue (prefix + "q")->load();
        const int   typeInt = static_cast<int> (apvts.getRawParameterValue (prefix + "type")->load());
        const bool  bypass  = apvts.getRawParameterValue (prefix + "bypass")->load() > 0.5f;

        if (bypass) continue; // bypassed bands contribute 0 dB (no change)

        const auto type = static_cast<FilterType> (typeInt);
        const float gainLinear = juce::Decibels::decibelsToGain (gainDb);

        // Build temporary double-precision coefficients for accurate display.
        // These are NOT the same objects as the audio-thread filter coefficients —
        // they are local, display-only and allocated / freed each repaint.
        juce::dsp::IIR::Coefficients<double>::Ptr coeffs;

        switch (type)
        {
            case FilterType::LowCut:
                coeffs = juce::dsp::IIR::Coefficients<double>::makeHighPass (sampleRate, freq, q);
                break;
            case FilterType::LowShelf:
                coeffs = juce::dsp::IIR::Coefficients<double>::makeLowShelf (sampleRate, freq, q, gainLinear);
                break;
            case FilterType::Peak:
                coeffs = juce::dsp::IIR::Coefficients<double>::makePeakFilter (sampleRate, freq, q, gainLinear);
                break;
            case FilterType::HighShelf:
                coeffs = juce::dsp::IIR::Coefficients<double>::makeHighShelf (sampleRate, freq, q, gainLinear);
                break;
            case FilterType::HighCut:
                coeffs = juce::dsp::IIR::Coefficients<double>::makeLowPass (sampleRate, freq, q);
                break;
            default:
                continue;
        }

        if (coeffs == nullptr) continue;

        for (int k = 0; k < kNumPoints; ++k)
        {
            // Logarithmically spaced frequency for point k.
            const double f = kMinFreq * std::pow ((double)kMaxFreq / kMinFreq,
                                                   (double)k / (kNumPoints - 1));

            // getMagnitudeForFrequency evaluates |H(e^jω)| for this biquad
            // by substituting z = e^(j2πf/sr) into the transfer function H(z).
            // Returns a linear (non-dB) magnitude ratio.
            const double mag = coeffs->getMagnitudeForFrequency (f, sampleRate);

            // Convert to dB and accumulate.  Using gainToDecibels with a small
            // minimum to avoid log(0) when the filter produces complete attenuation.
            magDb[k] += (float)juce::Decibels::gainToDecibels (mag, -200.0);
        }
    }

    return magDb;
}

// ============================================================
// SECTION: Band Node Helpers
// ============================================================

juce::Point<float>
FrequencyResponseDisplay::bandNodeCentre (int bandIndex, double sampleRate) const
{
    auto& apvts = processor.getAPVTS();
    juce::String prefix = "band" + juce::String (bandIndex) + "_";

    const float freq   = apvts.getRawParameterValue (prefix + "freq")->load();
    const float gainDb = apvts.getRawParameterValue (prefix + "gain")->load();
    const int   type   = static_cast<int> (apvts.getRawParameterValue (prefix + "type")->load());

    const float x = frequencyToX (freq);

    // For cut filters gain is meaningless — pin the node to the 0 dB line.
    const bool isGainless = (type == (int)FilterType::LowCut ||
                             type == (int)FilterType::HighCut);
    const float y = isGainless ? gainDbToY (0.0f) : gainDbToY (gainDb);

    return { x, y };
}

int FrequencyResponseDisplay::hitTestBandNode (juce::Point<float> pos,
                                                double sampleRate) const
{
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        const auto centre = bandNodeCentre (i, sampleRate);
        // Hit if the click is within (radius + 2px) of the node centre for
        // slightly forgiving interaction.
        if (centre.getDistanceFrom (pos) <= kNodeRadius + 2.0f)
            return i;
    }
    return -1;
}

// ============================================================
// SECTION: Mouse Interaction
// ============================================================

void FrequencyResponseDisplay::mouseDown (const juce::MouseEvent& e)
{
    const double sr = processor.getCurrentSampleRate() > 0.0
                          ? processor.getCurrentSampleRate() : 44100.0;

    draggedBandIndex = hitTestBandNode (e.position, sr);

    if (draggedBandIndex < 0) return; // click was not on any node

    // Determine whether this band type supports gain editing.
    juce::String prefix = "band" + juce::String (draggedBandIndex) + "_";
    const int type = static_cast<int> (
        processor.getAPVTS().getRawParameterValue (prefix + "type")->load());
    dragGainActive = (type != (int)FilterType::LowCut &&
                      type != (int)FilterType::HighCut);

    // Begin automation gesture so the DAW records a parameter touch event.
    processor.getAPVTS().getParameter (prefix + "freq")->beginChangeGesture();
    if (dragGainActive)
        processor.getAPVTS().getParameter (prefix + "gain")->beginChangeGesture();
}

void FrequencyResponseDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedBandIndex < 0) return;

    juce::String prefix = "band" + juce::String (draggedBandIndex) + "_";
    auto& apvts = processor.getAPVTS();

    // ── Frequency (X axis) ────────────────────────────────────────────────
    const float newFreq = juce::jlimit (20.0f, 20000.0f, xToFrequency (e.position.x));
    auto* freqParam = apvts.getParameter (prefix + "freq");
    // Convert Hz value to the 0..1 normalised range the parameter expects.
    freqParam->setValueNotifyingHost (freqParam->convertTo0to1 (newFreq));

    // ── Gain (Y axis) — only for Peak / Shelf ────────────────────────────
    if (dragGainActive)
    {
        const float newGain = juce::jlimit (-24.0f, 24.0f, yToGainDb (e.position.y));
        auto* gainParam = apvts.getParameter (prefix + "gain");
        gainParam->setValueNotifyingHost (gainParam->convertTo0to1 (newGain));
    }
}

void FrequencyResponseDisplay::mouseUp (const juce::MouseEvent& /*e*/)
{
    if (draggedBandIndex < 0) return;

    juce::String prefix = "band" + juce::String (draggedBandIndex) + "_";
    auto& apvts = processor.getAPVTS();

    // End the gesture so the DAW knows the user has released the parameter.
    apvts.getParameter (prefix + "freq")->endChangeGesture();
    if (dragGainActive)
        apvts.getParameter (prefix + "gain")->endChangeGesture();

    draggedBandIndex = -1;
    dragGainActive   = false;
}

void FrequencyResponseDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const double sr = processor.getCurrentSampleRate() > 0.0
                          ? processor.getCurrentSampleRate() : 44100.0;
    const int idx = hitTestBandNode (e.position, sr);
    if (idx < 0) return;

    // Toggle the bypass parameter for the double-clicked band.
    juce::String paramID = "band" + juce::String (idx) + "_bypass";
    auto* param = processor.getAPVTS().getParameter (paramID);
    const float currentVal = param->getValue(); // normalised 0..1 (bool: 0=active 1=bypass)
    param->beginChangeGesture();
    param->setValueNotifyingHost (currentVal > 0.5f ? 0.0f : 1.0f);
    param->endChangeGesture();
}

// ============================================================
// SECTION: Painting
// ============================================================

void FrequencyResponseDisplay::paint (juce::Graphics& g)
{
    double sr = processor.getCurrentSampleRate();
    if (sr <= 0.0) sr = 44100.0; // fallback when not yet prepared (editor opened standalone)

    drawBackground (g);
    drawGrid       (g);
    drawCurve      (g, sr);
    drawBandNodes  (g, sr);
}

// ── Background ──────────────────────────────────────────────────────────────

void FrequencyResponseDisplay::drawBackground (juce::Graphics& g) const
{
    // Deep navy background — inspired by FabFilter Pro-Q 3's dark aesthetic.
    g.fillAll (juce::Colour (0xff1a1a2e));
}

// ── Grid ────────────────────────────────────────────────────────────────────

void FrequencyResponseDisplay::drawGrid (juce::Graphics& g) const
{
    const float w = (float) getWidth();
    const float h = (float) getHeight();

    // Muted grid line colour — visible but not distracting.
    const juce::Colour gridColour  (0xff2e2e4a);
    const juce::Colour labelColour (0xff6868a0);

    g.setFont (juce::Font (juce::FontOptions (10.0f)));

    // ── Vertical lines at standard decade/half-decade frequencies ─────────
    const float freqMarkers[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (float f : freqMarkers)
    {
        const float x = frequencyToX (f);

        g.setColour (gridColour);
        g.drawVerticalLine ((int) x, 0.0f, h);

        // Label — abbreviate kHz values.
        juce::String label = f >= 1000.0f
                           ? juce::String ((int)(f / 1000)) + "k"
                           : juce::String ((int) f);
        g.setColour (labelColour);
        g.drawText (label,
                    (int)x - 15, (int)h - 14, 30, 12,
                    juce::Justification::centred, false);
    }

    // ── Horizontal lines at ±6, ±12, ±18, ±24 dB and 0 dB ────────────────
    const float dbMarkers[] = { -24, -18, -12, -6, 0, 6, 12, 18, 24 };
    for (float db : dbMarkers)
    {
        const float y = gainDbToY (db);

        // 0 dB line is slightly brighter for reference.
        g.setColour (db == 0.0f ? juce::Colour (0xff3e3e60) : gridColour);
        g.drawHorizontalLine ((int) y, 0.0f, w);

        juce::String label = (db > 0.0f ? "+" : "") + juce::String ((int) db) + " dB";
        g.setColour (labelColour);
        g.drawText (label,
                    2, (int)y - 6, 36, 12,
                    juce::Justification::left, false);
    }
}

// ── EQ Curve ────────────────────────────────────────────────────────────────

void FrequencyResponseDisplay::drawCurve (juce::Graphics& g, double sampleRate) const
{
    const auto magDb = computeMagnitudeResponse (sampleRate);
    const float w    = (float) getWidth();
    const float h    = (float) getHeight();

    juce::Path curvePath;

    for (int k = 0; k < kNumPoints; ++k)
    {
        // Clamp to ±30 dB so extreme resonances don't escape the window.
        const float dB = juce::jlimit (-30.0f, 30.0f, magDb[k]);
        const float x  = (float) k / (float)(kNumPoints - 1) * w;
        const float y  = gainDbToY (dB);

        if (k == 0) curvePath.startNewSubPath (x, y);
        else        curvePath.lineTo           (x, y);
    }

    // ── Filled area (semi-transparent cyan) ──────────────────────────────
    juce::Path fillPath = curvePath;
    fillPath.lineTo (w, h);          // close down to bottom-right
    fillPath.lineTo (0.0f, h);       // across to bottom-left
    fillPath.closeSubPath();

    g.setColour (juce::Colour (0x4400c8c8)); // ~25% opacity cyan fill
    g.fillPath  (fillPath);

    // ── Stroke (bright cyan line) ─────────────────────────────────────────
    g.setColour (juce::Colour (0xff00e5e5));
    g.strokePath (curvePath, juce::PathStrokeType (2.0f));
}

// ── Band Nodes ──────────────────────────────────────────────────────────────

void FrequencyResponseDisplay::drawBandNodes (juce::Graphics& g, double sampleRate) const
{
    auto& apvts = processor.getAPVTS();

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        juce::String prefix = "band" + juce::String (i) + "_";
        const bool   bypass = apvts.getRawParameterValue (prefix + "bypass")->load() > 0.5f;
        const int    type   = static_cast<int> (apvts.getRawParameterValue (prefix + "type")->load());
        const auto   colour = bandColour (static_cast<FilterType> (type));

        const auto centre = bandNodeCentre (i, sampleRate);
        const float x     = centre.x;
        const float y     = centre.y;

        // Outer circle — full colour when active, dimmed when bypassed.
        const juce::Colour fillCol = bypass ? colour.withAlpha (0.25f) : colour.withAlpha (0.85f);
        g.setColour (fillCol);
        g.fillEllipse (x - kNodeRadius, y - kNodeRadius,
                       kNodeRadius * 2.0f, kNodeRadius * 2.0f);

        // Border ring.
        g.setColour (bypass ? colour.withAlpha (0.4f) : colour);
        g.drawEllipse (x - kNodeRadius, y - kNodeRadius,
                       kNodeRadius * 2.0f, kNodeRadius * 2.0f, 1.5f);

        // Band index label inside the node.
        g.setColour (juce::Colour (0xff1a1a2e));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.drawText (juce::String (i + 1),
                    (int)(x - kNodeRadius), (int)(y - kNodeRadius),
                    (int)(kNodeRadius * 2), (int)(kNodeRadius * 2),
                    juce::Justification::centred, false);
    }
}
