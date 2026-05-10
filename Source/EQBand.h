// ==============================================================================
// EQBand.h — EQ Band Data Structures
// ==============================================================================
// Defines FilterType (the IIR shape enum) and EQBand (a plain-data snapshot
// of one band's parameters).  This header has no JUCE dependencies, so it can
// be included anywhere without pulling in heavy JUCE headers.
//
// Architecture role:
//   OSHAEQAudioProcessor owns 6 juce::dsp::IIR::Filter objects.
//   FrequencyResponseDisplay reads EQBand snapshots to draw the curve.
//   FilterType values are matched 1-to-1 with AudioParameterChoice indices
//   in the APVTS, so (int → FilterType) casts are safe and intentional.
// ==============================================================================

#pragma once

// ============================================================
// SECTION: Filter Type Enumeration
// ============================================================

/// Each value maps to a choice index in the APVTS parameter "band{n}_type".
/// Do NOT reorder without also updating createParameterLayout() in
/// PluginProcessor.cpp and the switch statements in updateFilters() and
/// FrequencyResponseDisplay::computeMagnitudeResponse().
enum class FilterType
{
    LowCut    = 0,  ///< 2nd-order Butterworth high-pass (removes low frequencies)
    LowShelf  = 1,  ///< Low-frequency shelf boost/cut
    Peak      = 2,  ///< Symmetric bell boost/cut around centre frequency
    HighShelf = 3,  ///< High-frequency shelf boost/cut
    HighCut   = 4   ///< 2nd-order Butterworth low-pass (removes high frequencies)
};

// ============================================================
// SECTION: EQBand Snapshot Struct
// ============================================================

/// Lightweight, copyable snapshot of one band's parameter state.
/// FrequencyResponseDisplay fills an array of these each repaint cycle so it
/// can compute the magnitude response curve without touching APVTS atomics
/// more than once per paint (avoids tearing between parameter reads).
struct EQBand
{
    float      frequency { 1000.0f };            ///< Centre / corner frequency in Hz
    float      gainDb    { 0.0f };               ///< Gain in dB (ignored for LowCut / HighCut)
    float      q         { 0.707f };             ///< Q factor — higher = narrower bandwidth
    FilterType type      { FilterType::Peak };   ///< IIR filter shape
    bool       bypassed  { false };              ///< true = band produces no effect on audio
};
