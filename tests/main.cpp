// Headless test: feed a vocal into the plugin, capture, render, preview, export, snapshot UI.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
using namespace juce;
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    RiseVerbProcessor p;
    const double sr = 44100; const int bs = 512;
    p.setRateAndBufferSizeDetails (sr, bs);
    p.prepareToPlay (sr, bs);

    AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (File (argv[1])));
    AudioBuffer<float> vox (2, (int) r->lengthInSamples); r->read (&vox, 0, vox.getNumSamples(), 0, true, true);

    std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
    p.startCapture();
    MidiBuffer m; float maxDiff = 0;
    for (int pos = 0; pos + bs <= vox.getNumSamples(); pos += bs)
    {
        AudioBuffer<float> b (2, bs);
        for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, vox, c, pos, bs);
        p.processBlock (b, m);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) maxDiff = jmax (maxDiff, std::abs (b.getSample (c, i) - vox.getSample (c, pos + i)));
    }
    std::printf ("passthrough max diff while capturing: %g\n", maxDiff);
    std::printf ("captured %.2f s\n", p.getCaptureSeconds());
    p.stopCapture();
    for (int i = 0; i < 300 && (p.isRendering() || ! p.hasRiser()); ++i) Thread::sleep (10);
    std::printf ("riser: %.3f s (expect 2.000 at 120 bpm, 1 bar)\n", p.getRiserSeconds());

    // change length to 2 bars via parameter -> editor timer re-renders
    p.apvts.getParameter ("length")->setValueNotifyingHost (p.apvts.getParameter ("length")->convertTo0to1 (2));
    p.apvts.getParameter ("depth")->setValueNotifyingHost (0.5f);
    for (int i = 0; i < 100; ++i) { MessageManager::getInstance()->runDispatchLoopUntil (20); }
    for (int i = 0; i < 300 && p.isRendering(); ++i) Thread::sleep (10);
    MessageManager::getInstance()->runDispatchLoopUntil (100);
    std::printf ("riser after 2 bars: %.3f s (expect 4.000)\n", p.getRiserSeconds());

    p.togglePreview();
    float rms = 0; int blocks = 0;
    while (p.isPreviewing() && blocks < 2000) { AudioBuffer<float> b (2, bs); b.clear(); p.processBlock (b, m); rms += b.getRMSLevel (0, 0, bs); ++blocks; }
    std::printf ("preview played %d blocks (%.2f s), avg rms %.3f\n", blocks, blocks * bs / sr, rms / jmax (1, blocks));

    auto f = p.exportRiser();
    std::printf ("exported: %s (%lld bytes)\n", f.getFullPathName().toRawUTF8(), (long long) f.getSize());

    MessageManager::getInstance()->runDispatchLoopUntil (200);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    File outF (argv[2]); outF.deleteFile(); FileOutputStream os (outF); 
    PNGImageFormat().writeImageToStream (img, os);
    ed.reset();
    return 0;
}
