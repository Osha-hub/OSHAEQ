// ==============================================================================
// PluginEditor.cpp — OSHAEQ GUI Editor Implementation
// ==============================================================================
// Builds the 900×500 plugin window described in PluginEditor.h.
// All APVTS parameter bindings are made via SliderAttachment / ButtonAttachment
// so the GUI stays automatically in sync with automation and preset loading.
// ==============================================================================

#include "PluginProcessor.h"
#include "PluginEditor.h"

// ============================================================
// SECTION: Construction / Destruction
// ============================================================

OSHAEQAudioProcessorEditor::OSHAEQAudioProcessorEditor (OSHAEQAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      freqDisplay (p)   // FrequencyResponseDisplay also holds a reference to p
{
    // ── Global size ────────────────────────────────────────────────────────
    setSize (900, 500);

    // ── EQ curve display ──────────────────────────────────────────────────
    addAndMakeVisible (freqDisplay);

    // ── Per-band controls ─────────────────────────────────────────────────
    auto& apvts = audioProcessor.getAPVTS();

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        juce::String prefix = "band" + juce::String (i) + "_";

        // ── Type label ────────────────────────────────────────────────────
        const int typeIdx = static_cast<int> (
            apvts.getRawParameterValue (prefix + "type")->load());
        typeLabels[i].setText (filterTypeName (typeIdx), juce::dontSendNotification);
        typeLabels[i].setJustificationType (juce::Justification::centred);
        typeLabels[i].setColour (juce::Label::textColourId, juce::Colour (0xff8888aa));
        typeLabels[i].setFont (juce::Font (juce::FontOptions (10.0f)));
        addAndMakeVisible (typeLabels[i]);

        // ── Frequency knob ────────────────────────────────────────────────
        setupRotarySlider (freqSliders[i]);
        addAndMakeVisible (freqSliders[i]);
        freqAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, prefix + "freq", freqSliders[i]);

        // ── Gain knob ─────────────────────────────────────────────────────
        setupRotarySlider (gainSliders[i]);
        addAndMakeVisible (gainSliders[i]);
        gainAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, prefix + "gain", gainSliders[i]);

        // ── Q knob ────────────────────────────────────────────────────────
        setupRotarySlider (qSliders[i]);
        addAndMakeVisible (qSliders[i]);
        qAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, prefix + "q", qSliders[i]);

        // ── Bypass LED button ─────────────────────────────────────────────
        // Acts as a toggle: green text when active, grey when bypassed.
        bypassButtons[i].setButtonText ("ON");
        bypassButtons[i].setClickingTogglesState (true);
        bypassButtons[i].setColour (juce::TextButton::buttonColourId,    juce::Colour (0xff1e1e38));
        bypassButtons[i].setColour (juce::TextButton::buttonOnColourId,  juce::Colour (0xff00c060));
        bypassButtons[i].setColour (juce::TextButton::textColourOffId,   juce::Colour (0xff555580));
        bypassButtons[i].setColour (juce::TextButton::textColourOnId,    juce::Colour (0xffffffff));
        addAndMakeVisible (bypassButtons[i]);
        bypassAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, prefix + "bypass", bypassButtons[i]);

        // ── Readout labels ────────────────────────────────────────────────
        for (auto* lbl : { &freqLabels[i], &gainLabels[i], &qLabels[i] })
        {
            lbl->setJustificationType (juce::Justification::centred);
            lbl->setColour (juce::Label::textColourId, juce::Colour (0xff6868a0));
            lbl->setFont (juce::Font (juce::FontOptions (9.0f)));
            addAndMakeVisible (lbl);
        }
    }

    // ── Refresh readout labels immediately, then every 100 ms ─────────────
    updateReadoutLabels();
    startTimerHz (10);
}

OSHAEQAudioProcessorEditor::~OSHAEQAudioProcessorEditor()
{
    stopTimer();

    // APVTS attachments must be destroyed before the sliders/buttons they
    // reference — unique_ptr members are destroyed in reverse declaration order,
    // which is: attachments first (declared last), then controls (declared before).
    // The order in the header is fine because arrays are the same order.
}

// ============================================================
// SECTION: Static Helpers
// ============================================================

juce::String OSHAEQAudioProcessorEditor::filterTypeName (int typeIndex)
{
    switch (typeIndex)
    {
        case 0: return "LP CUT";
        case 1: return "LO SHELF";
        case 2: return "PEAK";
        case 3: return "HI SHELF";
        case 4: return "HP CUT";
        default: return "?";
    }
}

void OSHAEQAudioProcessorEditor::setupRotarySlider (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff00c8c8));
    s.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff2e2e50));
    s.setColour (juce::Slider::thumbColourId, juce::Colour (0xff00e5e5));
}

// ============================================================
// SECTION: Readout Label Update (Timer Callback)
// ============================================================

void OSHAEQAudioProcessorEditor::updateReadoutLabels()
{
    auto& apvts = audioProcessor.getAPVTS();

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        juce::String prefix = "band" + juce::String (i) + "_";

        const float freq   = apvts.getRawParameterValue (prefix + "freq")->load();
        const float gain   = apvts.getRawParameterValue (prefix + "gain")->load();
        const float q      = apvts.getRawParameterValue (prefix + "q")->load();
        const int   type   = static_cast<int> (apvts.getRawParameterValue (prefix + "type")->load());
        const bool  bypass = apvts.getRawParameterValue (prefix + "bypass")->load() > 0.5f;

        // Frequency: show as kHz when >= 1000 Hz.
        juce::String freqStr = freq >= 1000.0f
            ? juce::String (freq / 1000.0f, 2) + " kHz"
            : juce::String ((int)freq) + " Hz";
        freqLabels[i].setText (freqStr, juce::dontSendNotification);

        // Gain: show value; grey out for cut filter types where gain is irrelevant.
        const bool gainRelevant = (type == 1 || type == 2 || type == 3); // Shelf or Peak
        juce::String gainStr = (gain >= 0.0f ? "+" : "")
                             + juce::String (gain, 1) + " dB";
        gainLabels[i].setText (gainStr, juce::dontSendNotification);
        gainLabels[i].setColour (juce::Label::textColourId,
            gainRelevant && !bypass
                ? juce::Colour (0xff8888cc)
                : juce::Colour (0xff404060));
        gainSliders[i].setAlpha (gainRelevant ? 1.0f : 0.35f);

        // Q value.
        qLabels[i].setText ("Q " + juce::String (q, 2), juce::dontSendNotification);

        // Type label stays in sync (user could drag-change type via APVTS in future).
        typeLabels[i].setText (filterTypeName (type), juce::dontSendNotification);
    }
}

// ============================================================
// SECTION: Layout
// ============================================================

void OSHAEQAudioProcessorEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // ── FrequencyResponseDisplay — top 70% of height ──────────────────────
    const int displayH = (int)(h * 0.70f);
    freqDisplay.setBounds (0, 0, w, displayH);

    // ── Band control strip — bottom 30% ───────────────────────────────────
    const int stripY = displayH;

    // Each of the 6 band columns gets an equal share of the total width.
    const int colW = w / NUM_BANDS;

    // Vertical layout within each column (from top of strip):
    //   type label   12 px
    //   knobs row    (freq / gain / Q side by side)  ~60 px
    //   readouts row 12 px
    //   bypass btn   18 px

    const int labelH   = 12;
    const int knobSize = juce::jmin (42, (colW - 12) / 3); // three knobs per column
    const int readoutH = 12;
    const int btnH     = 18;

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        const int colX = i * colW;

        // Type label at the very top of the strip.
        typeLabels[i].setBounds (colX, stripY + 2, colW, labelH);

        // Three knobs side by side.
        const int knobY    = stripY + labelH + 4;
        const int knobXOff = (colW - knobSize * 3) / 2; // centre the trio

        freqSliders[i].setBounds (colX + knobXOff,                knobY, knobSize, knobSize);
        gainSliders[i].setBounds (colX + knobXOff + knobSize,     knobY, knobSize, knobSize);
        qSliders   [i].setBounds (colX + knobXOff + knobSize * 2, knobY, knobSize, knobSize);

        // Readout labels below the knobs, each centred under its knob.
        const int readY = knobY + knobSize + 2;
        freqLabels[i].setBounds (colX + knobXOff,                readY, knobSize, readoutH);
        gainLabels[i].setBounds (colX + knobXOff + knobSize,     readY, knobSize, readoutH);
        qLabels   [i].setBounds (colX + knobXOff + knobSize * 2, readY, knobSize, readoutH);

        // Bypass button centred at the bottom of the column.
        const int btnY = readY + readoutH + 2;
        bypassButtons[i].setBounds (colX + (colW - 36) / 2, btnY, 36, btnH);
    }
}

// ============================================================
// SECTION: Paint (background only — controls paint themselves)
// ============================================================

void OSHAEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Overall background — slightly lighter than the display for visual separation.
    g.fillAll (juce::Colour (0xff16162a));

    // Divider line between curve display and control strip.
    const int divY = (int)(getHeight() * 0.70f);
    g.setColour (juce::Colour (0xff2a2a45));
    g.drawHorizontalLine (divY, 0.0f, (float) getWidth());

    // "OSHAEQ" title text in the top-left corner of the display area.
    g.setColour (juce::Colour (0xff3a3a60));
    g.setFont (juce::Font (juce::FontOptions (14.0f)));
    g.drawText ("OSHAEQ", 8, 4, 100, 16, juce::Justification::left, false);

    // Small column headers ("FREQ", "GAIN", "Q") above knobs.
    const int colW = getWidth() / NUM_BANDS;
    const int stripY = divY;
    const int knobSize = juce::jmin (42, (colW - 12) / 3);
    const int knobXOffBase = (colW - knobSize * 3) / 2;
    const int headerY = stripY + 14; // just below type label

    g.setFont (juce::Font (juce::FontOptions (8.0f)));
    g.setColour (juce::Colour (0xff404060));

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        const int colX = i * colW;
        g.drawText ("FREQ", colX + knobXOffBase,                headerY, knobSize, 10,
                    juce::Justification::centred, false);
        g.drawText ("GAIN", colX + knobXOffBase + knobSize,     headerY, knobSize, 10,
                    juce::Justification::centred, false);
        g.drawText ("Q",    colX + knobXOffBase + knobSize * 2, headerY, knobSize, 10,
                    juce::Justification::centred, false);
    }
}
