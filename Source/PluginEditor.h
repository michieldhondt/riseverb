#pragma once
#include "PluginProcessor.h"

namespace rv
{
const juce::Colour bg       { 0xff121016 };
const juce::Colour panel    { 0xff1c1922 };
const juce::Colour panelHi  { 0xff2a2533 };
const juce::Colour accent   { 0xffc47bff };
const juce::Colour accent2  { 0xffff6fd8 };
const juce::Colour text     { 0xffe9e4f0 };
const juce::Colour textDim  { 0xff8c8499 };
const juce::Colour rec      { 0xffff4d5e };
}

class RiseVerbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RiseVerbLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton& b, int h) override { return juce::FontOptions (h < 36 ? 11.0f : 13.0f, juce::Font::bold); }
};

class WaveformView : public juce::Component
{
public:
    void setRiser (const juce::AudioBuffer<float>& b);
    void setStatus (const juce::String& s) { status = s; repaint(); }
    void setProgress (float p) { if (p != progress) { progress = p; repaint(); } }
    void setBusy (bool b) { if (b != busy) { busy = b; repaint(); } }
    void paint (juce::Graphics&) override;
private:
    std::vector<float> peaks;
    juce::String status;
    float progress = -1.0f;
    bool busy = false;
};

class DragToDawTile : public juce::Component
{
public:
    explicit DragToDawTile (RiseVerbProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (const juce::String&)> onMessage;
private:
    RiseVerbProcessor& proc;
    juce::File lastFile;
    bool dragging = false;
};

class RiseVerbEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
public:
    explicit RiseVerbEditor (RiseVerbProcessor&);
    ~RiseVerbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }

private:
    void timerCallback() override;
    void updateStatus();

    RiseVerbProcessor& proc;
    RiseVerbLookAndFeel lnf;

    WaveformView wave;
    juce::TextButton captureBtn { "CAPTURE" }, loadBtn { "LOAD FILE" }, previewBtn { "PREVIEW" };
    DragToDawTile dragTile { proc };

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    Knob size, tone, width, depth, output;
    juce::ComboBox lengthBox, rateBox, modeBox;
    juce::Label lengthLabel, rateLabel, modeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lengthAtt, rateAtt, modeAtt;

    juce::ComboBox presetBox;
    juce::TextButton savePresetBtn { "SAVE" };
    juce::TextButton deessBtn { "DE-ESS AUTO" };
    juce::OwnedArray<juce::TextButton> fxButtons;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> fxAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deessAtt;
    juce::Array<juce::File> userPresetFiles;
    std::unique_ptr<juce::AlertWindow> saveDialog;
    void refreshPresetList();
    void showSaveDialog();

    std::unique_ptr<juce::FileChooser> chooser;
    int shownVersion = -1;
    bool dropHover = false;
    int fxRowY = 0, stripY = 0;
    juce::String flashMessage;
    int flashTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiseVerbEditor)
};
