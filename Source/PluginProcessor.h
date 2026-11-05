#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "RiserEngine.h"

struct FxInfo { const char* id; const char* label; };

class RiseVerbProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener
{
public:
    RiseVerbProcessor();
    ~RiseVerbProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "RiseVerb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor (message thread) ----
    void startCapture();
    void stopCapture();
    bool isCapturing() const { return capturing.load(); }
    double getCaptureSeconds() const;

    bool loadVocalFile (const juce::File& f);
    bool hasVocal() const;
    bool hasRiser() const;

    void togglePreview();
    void stopPreview();
    bool isPreviewing() const { return previewing.load() || fallbackActive; }
    float getPreviewProgress() const;   // 0..1 over the riser, >1 while the vocal intro plays, -1 when stopped
    void previewTick();                 // called by the editor timer: handles the stand-alone preview fallback

    void requestRender();
    bool isRendering() const { return rendering.load(); }
    int getRenderVersion() const { return renderVersion.load(); }

    juce::AudioBuffer<float> getRiserCopy() const;
    double getRiserSeconds() const;
    double getBpm() const { return hostBpm.load(); }

    juce::File exportRiser();

    // ---- presets ----
    juce::StringArray getFactoryPresetNames() const;
    void applyFactoryPreset (int index);
    static juce::File getUserPresetFolder();
    juce::Array<juce::File> getUserPresets() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& f);
    juce::String getPresetName() const { return apvts.state.getProperty ("presetName", "Init").toString(); }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static const juce::StringArray lengthChoices, rateChoices, modeChoices;
    static const std::vector<FxInfo> fxList;

    std::atomic<bool> bpmChanged { false }, paramsChanged { false }, autoStopRequested { false };

private:
    void parameterChanged (const juce::String&, float) override;
    riser::Settings currentSettings() const;
    void renderJob();
    void rebuildPreviewBuffer();
    void setParam (const juce::String& id, float realValue);
    bool hostIsProcessing() const;
    void startFallbackPreview();
    void stopFallbackPreview();

    double sr = 48000.0;

    // capture (audio thread writes, message thread reads after stop)
    juce::AudioBuffer<float> captureBuffer;
    std::atomic<int> captureWritePos { 0 };
    std::atomic<bool> capturing { false };
    bool wasPlaying = false;

    // source vocal + rendered riser
    mutable juce::CriticalSection dataLock;
    riser::Stereo vocal;
    juce::AudioBuffer<float> riserBuffer;
    double riserSampleRate = 48000.0;
    double riserBpm = 120.0;

    // preview through the host (riser followed by the start of the vocal)
    juce::SpinLock previewLock;
    juce::AudioBuffer<float> previewBuffer;
    std::atomic<int> previewRiserLength { 0 };
    std::atomic<int> previewPos { 0 };
    std::atomic<bool> previewing { false };
    juce::uint32 previewStartMs = 0;
    std::atomic<juce::uint32> lastProcessMs { 0 };

    // stand-alone preview when the host is not calling processBlock (e.g. Logic with the transport stopped)
    std::unique_ptr<juce::AudioDeviceManager> deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioBuffer<float> fallbackBuffer;
    std::unique_ptr<juce::MemoryAudioSource> memorySource;
    std::unique_ptr<juce::ResamplingAudioSource> resampler;
    bool fallbackActive = false;
    double fallbackDeviceRate = 48000.0;

    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> rendering { false }, renderAgain { false };
    std::atomic<int> renderVersion { 0 };
    juce::ThreadPool pool { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiseVerbProcessor)
};
