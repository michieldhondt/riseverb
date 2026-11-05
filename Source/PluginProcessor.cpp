#include "PluginProcessor.h"
#include "PluginEditor.h"

const juce::StringArray RiseVerbProcessor::lengthChoices { "1/2 bar", "1 bar", "2 bars", "4 bars" };
const juce::StringArray RiseVerbProcessor::rateChoices   { "1/4", "1/8", "1/16", "1/32", juce::String::fromUTF8 ("Build \xe2\x86\x91") };
const juce::StringArray RiseVerbProcessor::modeChoices   { "Smooth", "Gate", "Repeat" };

const std::vector<FxInfo> RiseVerbProcessor::fxList {
    { "fx_shimmer", "SHIMMER" }, { "fx_dark", "DARK OCT" }, { "fx_rise", "PITCH RISE" }, { "fx_sweep", "FILTER SWEEP" },
    { "fx_lofi", "LO-FI" }, { "fx_radio", "RADIO" }, { "fx_delay", "DELAY" }, { "fx_tape", "TAPE" } };

static juce::StringArray allParamIds()
{
    juce::StringArray ids { "length", "size", "tone", "width", "mode", "rate", "depth", "output", "deess" };
    for (auto& f : RiseVerbProcessor::fxList) ids.add (f.id);
    return ids;
}

static constexpr double kMaxCaptureSeconds = 60.0;
static constexpr double kPreviewVocalSeconds = 2.5;

RiseVerbProcessor::RiseVerbProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto& id : allParamIds())
        apvts.addParameterListener (id, this);
}

RiseVerbProcessor::~RiseVerbProcessor()
{
    for (auto& id : allParamIds())
        apvts.removeParameterListener (id, this);
    stopFallbackPreview();
    pool.removeAllJobs (true, 10000);
}

juce::AudioProcessorValueTreeState::ParameterLayout RiseVerbProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v * 100)) + "%"; });

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "length", 1 }, "Length", lengthChoices, 1));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "size", 1 },   "Size",  NormalisableRange<float> (0.f, 1.f), 0.6f, pct));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "tone", 1 },   "Tone",  NormalisableRange<float> (0.f, 1.f), 0.6f, pct));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "width", 1 },  "Width", NormalisableRange<float> (0.f, 1.f), 0.8f, pct));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 },   "Stutter", modeChoices, 1));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "rate", 1 },   "Rate", rateChoices, 2));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "depth", 1 },  "Depth", NormalisableRange<float> (0.f, 1.f), 0.0f, pct));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "output", 1 }, "Output",
                     NormalisableRange<float> (-24.f, 6.f, 0.1f), 0.0f,
                     AudioParameterFloatAttributes().withLabel ("dB")
                        .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "deess", 1 }, "De-Ess (auto)", true));
    for (auto& f : fxList)
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { f.id, 1 }, String (f.label).toLowerCase(), false));
    return { p.begin(), p.end() };
}

bool RiseVerbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void RiseVerbProcessor::prepareToPlay (double sampleRate, int)
{
    if (sampleRate != sr || captureBuffer.getNumSamples() == 0)
    {
        sr = sampleRate;
        captureBuffer.setSize (2, (int) (kMaxCaptureSeconds * sr));
        captureBuffer.clear();
        if (hasVocal()) requestRender();
    }
}

void RiseVerbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, n);

    lastProcessMs.store (juce::Time::getMillisecondCounter());

    // Host tempo + transport
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                if (*bpm > 20.0 && std::abs (*bpm - hostBpm.load()) > 0.01)
                {
                    hostBpm.store (*bpm);
                    bpmChanged.store (true);
                }
            const bool playing = pos->getIsPlaying();
            // 1-click flow: when Logic stops playing, the capture stops by itself
            if (capturing.load() && wasPlaying && ! playing && captureWritePos.load() > 0)
                autoStopRequested.store (true);
            wasPlaying = playing;
        }

    // Capture: audio passes through untouched, we just record it.
    if (capturing.load())
    {
        int pos = captureWritePos.load();
        const int todo = juce::jmin (n, captureBuffer.getNumSamples() - pos);
        if (todo > 0)
        {
            for (int c = 0; c < 2; ++c)
                captureBuffer.copyFrom (c, pos, buffer, juce::jmin (c, chans - 1), 0, todo);
            captureWritePos.store (pos + todo);
        }
    }

    // Preview: mix riser (+ the first seconds of the vocal) into the output.
    if (previewing.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (previewLock);
        if (lock.isLocked() && previewBuffer.getNumSamples() > 0)
        {
            int pos = previewPos.load();
            const int todo = juce::jmin (n, previewBuffer.getNumSamples() - pos);
            for (int c = 0; c < chans && todo > 0; ++c)
                buffer.addFrom (c, 0, previewBuffer, juce::jmin (c, 1), pos, todo);
            pos += juce::jmax (0, todo);
            previewPos.store (pos);
            if (pos >= previewBuffer.getNumSamples())
                previewing.store (false);
        }
    }
}

// ---------------------------------------------------------------- capture / load

void RiseVerbProcessor::startCapture()
{
    stopPreview();
    autoStopRequested.store (false);
    captureWritePos.store (0);
    capturing.store (true);
}

void RiseVerbProcessor::stopCapture()
{
    capturing.store (false);
    const int len = captureWritePos.load();
    if (len <= 0) return;

    riser::Stereo v;
    v.L.assign (captureBuffer.getReadPointer (0), captureBuffer.getReadPointer (0) + len);
    v.R.assign (captureBuffer.getReadPointer (1), captureBuffer.getReadPointer (1) + len);

    // Ignore a capture that is pure silence
    float peak = 0;
    for (int i = 0; i < len; ++i) peak = std::max (peak, std::max (std::abs (v.L[(size_t) i]), std::abs (v.R[(size_t) i])));
    if (peak < 1.0e-4f) return;

    {
        const juce::ScopedLock sl (dataLock);
        vocal = std::move (v);
    }
    requestRender();
}

double RiseVerbProcessor::getCaptureSeconds() const
{
    return captureWritePos.load() / sr;
}

bool RiseVerbProcessor::loadVocalFile (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr) return false;

    const int maxLen = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (kMaxCaptureSeconds * reader->sampleRate));
    juce::AudioBuffer<float> tmp (2, maxLen);
    reader->read (&tmp, 0, maxLen, 0, true, true);

    // Resample to the session rate
    const double ratio = reader->sampleRate / sr;
    const int outLen = (int) std::floor (maxLen / ratio);
    riser::Stereo v;
    v.L.resize ((size_t) outLen);
    v.R.resize ((size_t) outLen);
    for (int c = 0; c < 2; ++c)
    {
        juce::LagrangeInterpolator interp;
        interp.process (ratio, tmp.getReadPointer (c), c == 0 ? v.L.data() : v.R.data(), outLen);
    }

    {
        const juce::ScopedLock sl (dataLock);
        vocal = std::move (v);
    }
    requestRender();
    return true;
}

bool RiseVerbProcessor::hasVocal() const
{
    const juce::ScopedLock sl (dataLock);
    return vocal.size() > 0;
}

bool RiseVerbProcessor::hasRiser() const
{
    const juce::ScopedLock sl (dataLock);
    return riserBuffer.getNumSamples() > 0;
}

// ---------------------------------------------------------------- render

riser::Settings RiseVerbProcessor::currentSettings() const
{
    static const double bars[] = { 0.5, 1.0, 2.0, 4.0 };
    static const double divs[] = { 4.0, 8.0, 16.0, 32.0, 0.0 };

    riser::Settings s;
    s.sampleRate   = sr;
    s.bpm          = hostBpm.load();
    s.lengthBars   = bars[juce::jlimit (0, 3, (int) apvts.getRawParameterValue ("length")->load())];
    s.size         = apvts.getRawParameterValue ("size")->load();
    s.tone         = apvts.getRawParameterValue ("tone")->load();
    s.width        = apvts.getRawParameterValue ("width")->load();
    s.tremoloDiv   = divs[juce::jlimit (0, 4, (int) apvts.getRawParameterValue ("rate")->load())];
    s.stutterMode  = juce::jlimit (0, 2, (int) apvts.getRawParameterValue ("mode")->load());
    s.tremoloDepth = apvts.getRawParameterValue ("depth")->load();
    s.outputDb     = apvts.getRawParameterValue ("output")->load();
    auto on = [this] (const char* id) { return apvts.getRawParameterValue (id)->load() > 0.5f; };
    s.deEss       = on ("deess");
    s.shimmer     = on ("fx_shimmer");
    s.darkOctave  = on ("fx_dark");
    s.pitchRise   = on ("fx_rise");
    s.filterSweep = on ("fx_sweep");
    s.lofi        = on ("fx_lofi");
    s.radio       = on ("fx_radio");
    s.delay       = on ("fx_delay");
    s.tape        = on ("fx_tape");
    return s;
}

void RiseVerbProcessor::parameterChanged (const juce::String&, float)
{
    // May be called from the audio thread (automation): only flag it, the editor timer renders.
    paramsChanged.store (true);
}

void RiseVerbProcessor::requestRender()
{
    if (rendering.exchange (true))
    {
        renderAgain.store (true);   // a render is running; do one more afterwards
        return;
    }
    pool.addJob ([this] { renderJob(); });
}

void RiseVerbProcessor::renderJob()
{
    do
    {
        renderAgain.store (false);
        riser::Stereo v;
        {
            const juce::ScopedLock sl (dataLock);
            v = vocal;
        }
        const auto s = currentSettings();
        auto out = riser::render (v, s);

        juce::AudioBuffer<float> b (2, (int) out.size());
        std::copy (out.L.begin(), out.L.end(), b.getWritePointer (0));
        std::copy (out.R.begin(), out.R.end(), b.getWritePointer (1));
        {
            const juce::ScopedLock sl (dataLock);
            riserBuffer = std::move (b);
            riserSampleRate = s.sampleRate;
            riserBpm = s.bpm;
        }
        rebuildPreviewBuffer();
    }
    while (renderAgain.load());

    rendering.store (false);
    ++renderVersion;
}

void RiseVerbProcessor::rebuildPreviewBuffer()
{
    juce::AudioBuffer<float> pb;
    int riserLen = 0;
    {
        const juce::ScopedLock sl (dataLock);
        riserLen = riserBuffer.getNumSamples();
        const size_t onset = riser::findOnset (vocal);
        const int vocLen = (int) juce::jmin<size_t> (vocal.size() - onset, (size_t) (kPreviewVocalSeconds * sr));
        pb.setSize (2, riserLen + juce::jmax (0, vocLen));
        pb.clear();
        for (int c = 0; c < 2; ++c)
            pb.copyFrom (c, 0, riserBuffer, c, 0, riserLen);
        for (int i = 0; i < vocLen; ++i)
        {
            const float fade = i > vocLen - 2000 ? (float) (vocLen - i) / 2000.0f : 1.0f;
            pb.setSample (0, riserLen + i, vocal.L[onset + (size_t) i] * fade);
            pb.setSample (1, riserLen + i, vocal.R[onset + (size_t) i] * fade);
        }
    }
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewBuffer = std::move (pb);
    previewRiserLength.store (riserLen);
    if (previewPos.load() >= previewBuffer.getNumSamples())
        previewing.store (false);
}

// ---------------------------------------------------------------- preview / export

bool RiseVerbProcessor::hostIsProcessing() const
{
    return juce::Time::getMillisecondCounter() - lastProcessMs.load() < 120;
}

void RiseVerbProcessor::togglePreview()
{
    if (isPreviewing()) { stopPreview(); return; }
    if (! hasRiser()) return;
    previewPos.store (0);
    previewStartMs = juce::Time::getMillisecondCounter();
    if (hostIsProcessing())
        previewing.store (true);
    else
        startFallbackPreview();   // host is idle (transport stopped): play it ourselves
}

void RiseVerbProcessor::stopPreview()
{
    previewing.store (false);
    stopFallbackPreview();
}

void RiseVerbProcessor::previewTick()
{
    // Host preview started but the host stopped calling us -> switch to stand-alone playback
    if (previewing.load() && ! fallbackActive
        && juce::Time::getMillisecondCounter() - previewStartMs > 150 && previewPos.load() == 0)
    {
        previewing.store (false);
        startFallbackPreview();
    }

    if (fallbackActive && memorySource != nullptr
        && memorySource->getNextReadPosition() >= memorySource->getTotalLength())
        stopFallbackPreview();
}

void RiseVerbProcessor::startFallbackPreview()
{
    stopFallbackPreview();
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        fallbackBuffer = previewBuffer;
    }
    if (fallbackBuffer.getNumSamples() == 0) return;

    if (deviceManager == nullptr)
    {
        deviceManager = std::make_unique<juce::AudioDeviceManager>();
        const auto err = deviceManager->initialiseWithDefaultDevices (0, 2);
        if (err.isNotEmpty()) { deviceManager.reset(); return; }
    }
    auto* dev = deviceManager->getCurrentAudioDevice();
    if (dev == nullptr) return;
    fallbackDeviceRate = dev->getCurrentSampleRate();

    memorySource = std::make_unique<juce::MemoryAudioSource> (fallbackBuffer, false, false);
    resampler = std::make_unique<juce::ResamplingAudioSource> (memorySource.get(), false, 2);
    resampler->setResamplingRatio (sr / fallbackDeviceRate);
    sourcePlayer.setSource (resampler.get());
    deviceManager->addAudioCallback (&sourcePlayer);
    fallbackActive = true;
}

void RiseVerbProcessor::stopFallbackPreview()
{
    if (deviceManager != nullptr)
        deviceManager->removeAudioCallback (&sourcePlayer);
    sourcePlayer.setSource (nullptr);
    resampler.reset();
    memorySource.reset();
    fallbackActive = false;
}

float RiseVerbProcessor::getPreviewProgress() const
{
    const int riserLen = previewRiserLength.load();
    if (riserLen <= 0) return -1.0f;
    if (fallbackActive && memorySource != nullptr)
        return (float) memorySource->getNextReadPosition() / (float) riserLen;
    if (! previewing.load()) return -1.0f;
    return (float) previewPos.load() / (float) riserLen;
}

juce::AudioBuffer<float> RiseVerbProcessor::getRiserCopy() const
{
    const juce::ScopedLock sl (dataLock);
    return riserBuffer;
}

double RiseVerbProcessor::getRiserSeconds() const
{
    const juce::ScopedLock sl (dataLock);
    return riserBuffer.getNumSamples() / riserSampleRate;
}

juce::File RiseVerbProcessor::exportRiser()
{
    juce::AudioBuffer<float> b;
    double rate, bpm;
    {
        const juce::ScopedLock sl (dataLock);
        b = riserBuffer;
        rate = riserSampleRate;
        bpm = riserBpm;
    }
    if (b.getNumSamples() == 0) return {};

    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("RiseVerb");
    if (! dir.createDirectory()) dir = juce::File::getSpecialLocation (juce::File::tempDirectory);

    const auto lenName = lengthChoices[(int) apvts.getRawParameterValue ("length")->load()].removeCharacters (" /");
    const auto name = "RiseVerb_" + juce::String (juce::roundToInt (bpm)) + "bpm_" + lenName + "_"
                      + juce::Time::getCurrentTime().formatted ("%H%M%S") + ".wav";
    auto file = dir.getChildFile (name);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
    if (os == nullptr) return {};
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (os.get(), rate, 2, 24, {}, 0));
    if (writer == nullptr) return {};
    os.release();
    writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    writer.reset();
    return file;
}

// ---------------------------------------------------------------- presets

namespace
{
struct FactoryPreset { const char* name; std::vector<std::pair<const char*, float>> values; };

// Values are real parameter values (choice = index, bool = 0/1). Anything not listed goes back to default.
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list {
        { "Init", {} },
        { "Classic Swell",  { { "size", 0.65f }, { "tone", 0.55f } } },
        { "Ghost Riser",    { { "length", 2 }, { "size", 0.9f }, { "tone", 0.3f }, { "width", 1.0f }, { "fx_sweep", 1 } } },
        { "Shimmer Lift",   { { "size", 0.7f }, { "tone", 0.75f }, { "fx_shimmer", 1 } } },
        { "Stutter Build",  { { "mode", 1 }, { "rate", 4 }, { "depth", 0.9f } } },
        { "Repeat Hype",    { { "mode", 2 }, { "rate", 2 }, { "depth", 0.85f } } },
        { "Trap Chop",      { { "mode", 1 }, { "rate", 3 }, { "depth", 1.0f }, { "tone", 0.7f } } },
        { "Pitch Climb",    { { "size", 0.7f }, { "fx_rise", 1 } } },
        { "Lo-Fi Tape",     { { "tone", 0.45f }, { "fx_lofi", 1 }, { "fx_tape", 1 } } },
        { "Radio Rise",     { { "width", 0.4f }, { "fx_radio", 1 }, { "fx_sweep", 1 } } },
        { "Dark Octave",    { { "size", 0.8f }, { "tone", 0.35f }, { "fx_dark", 1 } } },
        { "Echo Rise",      { { "width", 1.0f }, { "mode", 0 }, { "rate", 1 }, { "depth", 0.4f }, { "fx_delay", 1 } } },
        { "Big Drop Build", { { "length", 2 }, { "width", 1.0f }, { "mode", 1 }, { "rate", 4 }, { "depth", 0.8f },
                              { "fx_rise", 1 }, { "fx_sweep", 1 } } },
    };
    return list;
}
}

juce::StringArray RiseVerbProcessor::getFactoryPresetNames() const
{
    juce::StringArray names;
    for (auto& p : factoryPresets()) names.add (p.name);
    return names;
}

void RiseVerbProcessor::setParam (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

void RiseVerbProcessor::applyFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) factoryPresets().size())) return;
    const auto& preset = factoryPresets()[(size_t) index];
    for (auto& id : allParamIds())
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->getDefaultValue());
    for (auto& [id, v] : preset.values)
        setParam (id, v);
    apvts.state.setProperty ("presetName", preset.name, nullptr);
    paramsChanged.store (true);
}

juce::File RiseVerbProcessor::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("RiseVerb").getChildFile ("Presets");
}

juce::Array<juce::File> RiseVerbProcessor::getUserPresets() const
{
    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.rvpreset");
    files.sort();
    return files;
}

bool RiseVerbProcessor::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return false;
    auto dir = getUserPresetFolder();
    if (! dir.createDirectory()) return false;
    apvts.state.setProperty ("presetName", name, nullptr);
    if (auto xml = apvts.copyState().createXml())
        return xml->writeTo (dir.getChildFile (name + ".rvpreset"));
    return false;
}

bool RiseVerbProcessor::loadUserPreset (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    auto tree = juce::ValueTree::fromXml (*xml);
    for (auto& id : allParamIds())
        if (auto* p = apvts.getParameter (id))
        {
            auto child = tree.getChildWithProperty ("id", id);
            if (child.isValid())
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child.getProperty ("value")));
            else
                p->setValueNotifyingHost (p->getDefaultValue());
        }
    apvts.state.setProperty ("presetName", f.getFileNameWithoutExtension(), nullptr);
    paramsChanged.store (true);
    return true;
}

// ---------------------------------------------------------------- state

void RiseVerbProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void RiseVerbProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* RiseVerbProcessor::createEditor()
{
    return new RiseVerbEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RiseVerbProcessor();
}
