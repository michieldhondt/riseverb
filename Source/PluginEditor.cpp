#include "PluginEditor.h"

using namespace juce;

// =============================================================== LookAndFeel

RiseVerbLookAndFeel::RiseVerbLookAndFeel()
{
    setColour (Slider::textBoxTextColourId, rv::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Label::textColourId, rv::textDim);
    setColour (TextButton::textColourOffId, rv::text);
    setColour (TextButton::textColourOnId, rv::text);
    setColour (ComboBox::textColourId, rv::text);
    setColour (ComboBox::arrowColourId, rv::accent);
    setColour (PopupMenu::backgroundColourId, rv::panel);
    setColour (PopupMenu::textColourId, rv::text);
    setColour (PopupMenu::highlightedBackgroundColourId, rv::accent.withAlpha (0.35f));
}

void RiseVerbLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                            float start, float end, Slider&)
{
    auto b = Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = start + pos * (end - start);

    // body
    g.setColour (rv::panelHi);
    g.fillEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f);

    // track + value arc
    Path track, value;
    track.addCentredArc (c.x, c.y, r * 0.9f, r * 0.9f, 0, start, end, true);
    value.addCentredArc (c.x, c.y, r * 0.9f, r * 0.9f, 0, start, angle, true);
    g.setColour (rv::bg.brighter (0.15f));
    g.strokePath (track, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setGradientFill (ColourGradient (rv::accent, c.x - r, c.y, rv::accent2, c.x + r, c.y, false));
    g.strokePath (value, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));

    // pointer
    Path p;
    p.addRoundedRectangle (-1.5f, -r * 0.66f, 3.0f, r * 0.32f, 1.5f);
    g.setColour (rv::text);
    g.fillPath (p, AffineTransform::rotation (angle).translated (c));
}

void RiseVerbLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool isRecord = b.getName() == "record";
    if (b.getToggleState() && isRecord)
        g.setColour (rv::rec);
    else if (b.getToggleState())
        g.setGradientFill (ColourGradient (rv::accent, r.getX(), 0, rv::accent2, r.getRight(), 0, false));
    else
        g.setColour (rv::panelHi);
    if (! b.isEnabled()) g.setOpacity (0.35f);
    g.fillRoundedRectangle (r, 7.0f);
    if (over || down)
    {
        g.setColour (Colours::white.withAlpha (down ? 0.18f : 0.08f));
        g.fillRoundedRectangle (r, 7.0f);
    }
}

void RiseVerbLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
    g.setColour (rv::panelHi);
    g.fillRoundedRectangle (r, 6.0f);
    Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (box.findColour (ComboBox::arrowColourId));
    g.fillPath (arrow);
}

// =============================================================== Waveform

void WaveformView::setRiser (const AudioBuffer<float>& b)
{
    peaks.clear();
    const int n = b.getNumSamples();
    const int cols = jmax (1, getWidth() - 24);
    if (n == 0) { repaint(); return; }
    peaks.resize ((size_t) cols, 0.0f);
    for (int i = 0; i < cols; ++i)
    {
        const int s0 = (int) ((int64) i * n / cols), s1 = jmax (s0 + 1, (int) ((int64) (i + 1) * n / cols));
        float m = 0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            m = jmax (m, b.getMagnitude (c, s0, s1 - s0));
        peaks[(size_t) i] = m;
    }
    repaint();
}

void WaveformView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (rv::panel);
    g.fillRoundedRectangle (r, 10.0f);

    auto area = r.reduced (12.0f, 14.0f);
    const float mid = area.getCentreY();

    g.setColour (rv::textDim.withAlpha (0.25f));
    g.drawHorizontalLine ((int) mid, area.getX(), area.getRight());

    if (! peaks.empty())
    {
        Path wavePath;
        const float half = area.getHeight() * 0.48f;
        const float dx = area.getWidth() / (float) peaks.size();
        wavePath.startNewSubPath (area.getX(), mid);
        for (size_t i = 0; i < peaks.size(); ++i)
            wavePath.lineTo (area.getX() + (float) i * dx, mid - peaks[i] * half);
        for (size_t i = peaks.size(); i-- > 0;)
            wavePath.lineTo (area.getX() + (float) i * dx, mid + peaks[i] * half);
        wavePath.closeSubPath();

        g.setGradientFill (ColourGradient (rv::accent.withAlpha (0.55f), area.getX(), mid,
                                           rv::accent2, area.getRight(), mid, false));
        g.fillPath (wavePath);

        // "vocal enters here" marker
        g.setColour (rv::text.withAlpha (0.8f));
        g.drawVerticalLine ((int) area.getRight(), area.getY(), area.getBottom());
        g.setFont (FontOptions (11.0f));
        g.drawText ("VOCAL IN", Rectangle<float> (area.getRight() - 70, area.getY() - 12, 66, 12),
                    Justification::centredRight);

        if (progress >= 0.0f)
        {
            const float px = area.getX() + jmin (1.0f, progress) * area.getWidth();
            g.setColour (progress > 1.0f ? rv::accent2 : Colours::white);
            g.drawLine (px, area.getY(), px, area.getBottom(), 2.0f);
        }
    }

    if (busy)
    {
        g.setColour (rv::text.withAlpha (0.8f));
        g.setFont (FontOptions (12.0f));
        g.drawText ("rendering...", area.removeFromTop (16), Justification::topLeft);
    }

    if (peaks.empty() || status.isNotEmpty())
    {
        g.setColour (peaks.empty() ? rv::text : rv::text.withAlpha (0.85f));
        g.setFont (FontOptions (peaks.empty() ? 15.0f : 12.0f));
        g.drawFittedText (status, getLocalBounds().reduced (24, peaks.empty() ? 20 : 8),
                          peaks.empty() ? Justification::centred : Justification::bottomLeft, 3);
    }
}

// =============================================================== Drag tile

void DragToDawTile::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool ready = proc.hasRiser();
    g.setGradientFill (ColourGradient (rv::accent, r.getX(), 0, rv::accent2, r.getRight(), 0, false));
    g.setOpacity (ready ? 1.0f : 0.3f);
    g.fillRoundedRectangle (r, 7.0f);
    if (isMouseOver() && ready)
    {
        g.setColour (Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (r, 7.0f);
    }
    g.setColour (Colours::white.withAlpha (ready ? 1.0f : 0.5f));
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawText (String::fromUTF8 ("\xe2\x87\xa2  DRAG TO DAW"), getLocalBounds(), Justification::centred);
}

void DragToDawTile::mouseDown (const MouseEvent&)
{
    dragging = false;
    lastFile = proc.hasRiser() ? proc.exportRiser() : File();
}

void DragToDawTile::mouseDrag (const MouseEvent& e)
{
    if (dragging || ! lastFile.existsAsFile() || e.getDistanceFromDragStart() < 4) return;
    dragging = true;
    DragAndDropContainer::performExternalDragDropOfFiles ({ lastFile.getFullPathName() }, false, this);
}

void DragToDawTile::mouseUp (const MouseEvent&)
{
    if (! dragging && lastFile.existsAsFile())
    {
        lastFile.revealToUser();
        if (onMessage) onMessage ("Saved: " + lastFile.getFileName() + "  (Music/RiseVerb)");
    }
    dragging = false;
}

// =============================================================== Editor

RiseVerbEditor::RiseVerbEditor (RiseVerbProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (wave);

    captureBtn.setName ("record");
    captureBtn.setClickingTogglesState (false);
    captureBtn.onClick = [this]
    {
        if (proc.isCapturing()) proc.stopCapture();
        else proc.startCapture();
        updateStatus();
    };
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Choose a vocal", File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile() && ! proc.loadVocalFile (fc.getResult()))
                                  { flashMessage = "Could not read that file."; flashTicks = 90; }
                              });
    };
    previewBtn.onClick = [this]
    {
        proc.togglePreview();
        if (! proc.isPreviewing() && proc.getPreviewStatus().startsWith ("Preview failed"))
        { flashMessage = proc.getPreviewStatus(); flashTicks = 240; }
    };

    // presets
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= 1 && id < 1000) proc.applyFactoryPreset (id - 1);
        else if (id >= 1000 && id - 1000 < userPresetFiles.size()) proc.loadUserPreset (userPresetFiles[id - 1000]);
    };
    addAndMakeVisible (presetBox);
    savePresetBtn.onClick = [this] { showSaveDialog(); };
    addAndMakeVisible (savePresetBtn);
    refreshPresetList();

    // one-click effects
    for (auto& f : RiseVerbProcessor::fxList)
    {
        auto* b = fxButtons.add (new TextButton (f.label));
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
        fxAttachments.add (new AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, f.id, *b));
    }
    deessBtn.setClickingTogglesState (true);
    deessBtn.setTooltip ("Automatic sibilance detection: keeps harsh S sounds out of the riser");
    addAndMakeVisible (deessBtn);
    deessAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "deess", deessBtn);
    dragTile.onMessage = [this] (const String& m) { flashMessage = m; flashTicks = 150; };

    for (auto* b : { &captureBtn, &loadBtn, &previewBtn })
        addAndMakeVisible (b);
    addAndMakeVisible (dragTile);

    auto setupKnob = [this] (Knob& k, const String& id, const String& name)
    {
        k.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 70, 16);
        k.slider.setColour (Slider::textBoxTextColourId, rv::text);
        k.slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        k.slider.setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        addAndMakeVisible (k.slider);
        k.label.setText (name, dontSendNotification);
        k.label.setJustificationType (Justification::centred);
        k.label.setFont (FontOptions (11.0f, Font::bold));
        addAndMakeVisible (k.label);
        k.att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
    };
    setupKnob (size, "size", "SIZE");
    setupKnob (tone, "tone", "TONE");
    setupKnob (width, "width", "WIDTH");
    setupKnob (depth, "depth", "DEPTH");
    setupKnob (output, "output", "OUTPUT");

    lengthBox.addItemList (RiseVerbProcessor::lengthChoices, 1);
    rateBox.addItemList (RiseVerbProcessor::rateChoices, 1);
    modeBox.addItemList (RiseVerbProcessor::modeChoices, 1);
    modeAtt   = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode", modeBox);
    lengthAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "length", lengthBox);
    rateAtt   = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "rate", rateBox);
    for (auto* l : { &lengthLabel, &rateLabel, &modeLabel })
    {
        l->setJustificationType (Justification::centred);
        l->setFont (FontOptions (11.0f, Font::bold));
        addAndMakeVisible (l);
    }
    lengthLabel.setText ("LENGTH", dontSendNotification);
    rateLabel.setText ("RATE", dontSendNotification);
    modeLabel.setText ("STUTTER", dontSendNotification);
    addAndMakeVisible (modeBox);
    addAndMakeVisible (lengthBox);
    addAndMakeVisible (rateBox);

    setSize (860, 466);   // last, so resized() lays out every component
    updateStatus();
    startTimerHz (30);
}

void RiseVerbEditor::refreshPresetList()
{
    presetBox.clear (dontSendNotification);
    presetBox.addSectionHeading ("Factory");
    auto names = proc.getFactoryPresetNames();
    for (int i = 0; i < names.size(); ++i)
        presetBox.addItem (names[i], i + 1);
    userPresetFiles = proc.getUserPresets();
    if (! userPresetFiles.isEmpty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("User");
        for (int i = 0; i < userPresetFiles.size(); ++i)
            presetBox.addItem (userPresetFiles[i].getFileNameWithoutExtension(), 1000 + i);
    }
    presetBox.setText (proc.getPresetName(), dontSendNotification);
}

void RiseVerbEditor::showSaveDialog()
{
    saveDialog = std::make_unique<AlertWindow> ("Save preset", "Name your preset:", MessageBoxIconType::NoIcon, this);
    saveDialog->addTextEditor ("name", proc.getPresetName() == "Init" ? String ("My Riser") : proc.getPresetName());
    saveDialog->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    saveDialog->enterModalState (true, ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveDialog != nullptr)
        {
            const auto name = saveDialog->getTextEditorContents ("name");
            if (proc.saveUserPreset (name)) { flashMessage = "Preset saved: " + name; flashTicks = 90; }
            else { flashMessage = "Could not save preset."; flashTicks = 90; }
            refreshPresetList();
        }
        saveDialog.reset();
    }), false);
}

RiseVerbEditor::~RiseVerbEditor()
{
    setLookAndFeel (nullptr);
}

void RiseVerbEditor::paint (Graphics& g)
{
    g.fillAll (rv::bg);

    auto head = getLocalBounds().removeFromTop (54).reduced (20, 0);
    g.setColour (rv::text);
    g.setFont (FontOptions (22.0f, Font::bold));
    g.drawText ("RISEVERB", head.removeFromLeft (150), Justification::centredLeft);
    g.setColour (rv::textDim);
    g.setFont (FontOptions (12.0f));
    g.drawText ("by M1lobeats", head.removeFromLeft (120), Justification::centredLeft);
    g.drawText (String (proc.getBpm(), 1) + " BPM", head, Justification::centredRight);

    g.setColour (rv::textDim);
    g.setFont (FontOptions (11.0f, Font::bold));
    g.drawText ("FX", Rectangle<int> (20, fxRowY, 40, 32), Justification::centredLeft);

    // control strip background
    g.setColour (rv::panel);
    g.fillRoundedRectangle (Rectangle<int> (20, stripY, getWidth() - 40, 116).toFloat(), 10.0f);

    if (dropHover)
    {
        g.setColour (rv::accent.withAlpha (0.18f));
        g.fillAll();
        g.setColour (rv::accent);
        g.drawRect (getLocalBounds(), 3);
    }
}

void RiseVerbEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 0);
    auto head = r.removeFromTop (54);
    head.removeFromLeft (290);
    head.removeFromRight (90);
    savePresetBtn.setBounds (head.removeFromRight (64).withSizeKeepingCentre (64, 28));
    head.removeFromRight (8);
    presetBox.setBounds (head.removeFromRight (230).withSizeKeepingCentre (230, 28));
    wave.setBounds (r.removeFromTop (170));
    wave.setRiser (proc.getRiserCopy());

    r.removeFromTop (12);
    auto buttons = r.removeFromTop (40);
    const int bw = (buttons.getWidth() - 3 * 10) / 4;
    captureBtn.setBounds (buttons.removeFromLeft (bw)); buttons.removeFromLeft (10);
    loadBtn.setBounds (buttons.removeFromLeft (bw));    buttons.removeFromLeft (10);
    previewBtn.setBounds (buttons.removeFromLeft (bw)); buttons.removeFromLeft (10);
    dragTile.setBounds (buttons);

    r.removeFromTop (12);
    auto fxRow = r.removeFromTop (32);
    fxRowY = fxRow.getY();
    fxRow.removeFromLeft (34);
    deessBtn.setBounds (fxRow.removeFromRight (104));
    fxRow.removeFromRight (14);
    const int fw = (fxRow.getWidth() - 7 * 6) / 8;
    for (auto* b : fxButtons) { b->setBounds (fxRow.removeFromLeft (fw)); fxRow.removeFromLeft (6); }

    r.removeFromTop (12);
    stripY = r.getY();
    auto strip = Rectangle<int> (20, stripY, getWidth() - 40, 116).reduced (10, 8);
    const int cw = strip.getWidth() / 8;

    auto placeCombo = [&] (ComboBox& box, Label& label)
    {
        auto c = strip.removeFromLeft (cw);
        label.setBounds (c.removeFromTop (16));
        box.setBounds (c.withSizeKeepingCentre (cw - 14, 26).translated (0, -6));
    };
    auto placeKnob = [&] (Knob& k)
    {
        auto c = strip.removeFromLeft (cw);
        k.label.setBounds (c.removeFromTop (16));
        k.slider.setBounds (c);
    };
    placeCombo (lengthBox, lengthLabel);
    placeKnob (size);
    placeKnob (tone);
    placeKnob (width);
    placeCombo (modeBox, modeLabel);
    placeCombo (rateBox, rateLabel);
    placeKnob (depth);
    placeKnob (output);
}

bool RiseVerbEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (File (f).hasFileExtension ("wav;aif;aiff;mp3;flac;caf"))
            return true;
    return false;
}

void RiseVerbEditor::filesDropped (const StringArray& files, int, int)
{
    dropHover = false;
    for (auto& f : files)
        if (proc.loadVocalFile (File (f)))
            break;
    repaint();
}

void RiseVerbEditor::updateStatus()
{
    const bool cap = proc.isCapturing();
    captureBtn.setButtonText (cap ? "STOP" : "CAPTURE");
    captureBtn.setToggleState (cap, dontSendNotification);
    previewBtn.setButtonText (proc.isPreviewing() ? "STOP PREVIEW" : "PREVIEW");
    previewBtn.setEnabled (proc.hasRiser());

    String s;
    if (cap)
        s = "Recording... play your vocal in Logic. It stops by itself when you stop playback  (" + String (proc.getCaptureSeconds(), 1) + " s)";
    else if (flashTicks > 0)
        s = flashMessage;
    else if (proc.isPreviewing())
        s = proc.getPreviewStatus();
    else if (! proc.hasRiser() && ! proc.isRendering())
        s = "Press CAPTURE and play your vocal  -  or drop / load an audio file here";
    else if (proc.hasRiser())
        s = "Riser ends exactly where your vocal starts. Drag it into Logic and line its end up with the vocal.";
    wave.setStatus (s);
}

void RiseVerbEditor::timerCallback()
{
    proc.previewTick();

    if (proc.autoStopRequested.exchange (false) && proc.isCapturing())
        proc.stopCapture();

    if (proc.paramsChanged.exchange (false))
    {
        if (proc.hasVocal()) proc.requestRender();
        if (presetBox.getText() != proc.getPresetName())
            presetBox.setText (proc.getPresetName(), dontSendNotification);
    }

    if (proc.bpmChanged.exchange (false))
    {
        if (proc.hasVocal()) proc.requestRender();
        repaint (0, 0, getWidth(), 54);
    }

    if (flashTicks > 0) --flashTicks;

    const int v = proc.getRenderVersion();
    if (v != shownVersion)
    {
        shownVersion = v;
        wave.setRiser (proc.getRiserCopy());
        dragTile.repaint();
    }
    wave.setBusy (proc.isRendering());
    wave.setProgress (proc.getPreviewProgress());
    updateStatus();
}
