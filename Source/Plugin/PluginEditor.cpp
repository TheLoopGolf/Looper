#include "PluginEditor.h"

LooperAudioProcessorEditor::LooperAudioProcessorEditor (LooperAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef_ (p), mainView_ (p), settingsView_ (p)
{
    setLookAndFeel (&lookAndFeel_);
    addAndMakeVisible (mainView_);
    addChildComponent (reviewMapView_);
    addChildComponent (settingsView_);
    mainView_.setImportCallback ([this] (const juce::Array<juce::File>& files) { handleImport (files); });
    mainView_.setReviewCallback ([this] { handleOpenReview(); });
    mainView_.setSettingsCallback ([this] { handleOpenSettings(); });
    reviewMapView_.setAcceptCallback ([this] { handleAcceptMap(); });
    reviewMapView_.setBackCallback ([this] { handleBackToPlay(); });
    settingsView_.setBackCallback ([this] { showMain(); });
    setSize (1000, 680);
    showMain();
}

LooperAudioProcessorEditor::~LooperAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void LooperAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (looper::Palette::bg());
}

void LooperAudioProcessorEditor::resized()
{
    mainView_.setBounds (getLocalBounds());
    reviewMapView_.setBounds (getLocalBounds());
    settingsView_.setBounds (getLocalBounds());
}

void LooperAudioProcessorEditor::showMain()
{
    screen_ = Screen::Main;
    mainView_.refreshFromProcessor();
    mainView_.setVisible (true);
    reviewMapView_.setVisible (false);
    settingsView_.setVisible (false);
}

void LooperAudioProcessorEditor::showReview()
{
    screen_ = Screen::Review;
    mainView_.setVisible (false);
    reviewMapView_.setVisible (true);
    settingsView_.setVisible (false);
}

void LooperAudioProcessorEditor::showSettings()
{
    screen_ = Screen::Settings;
    settingsView_.refreshFromProcessor();
    mainView_.setVisible (false);
    reviewMapView_.setVisible (false);
    settingsView_.setVisible (true);
}

void LooperAudioProcessorEditor::handleImport (const juce::Array<juce::File>& files)
{
    if (files.isEmpty()) return;
    if (! processorRef_.importAudioFiles (files))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "Import", "No supported audio files found (WAV / AIFF / FLAC).");
        return;
    }
    if (! processorRef_.importController().hasPending())
        return;

    if (processorRef_.sessionPrefs().openReviewAfterImport)
    {
        if (const auto* pending = processorRef_.importController().pendingResult())
        {
            reviewMapView_.setResult (*pending, processorRef_.importController().pendingSampleRefs());
            showReview();
        }
    }
    else
    {
        processorRef_.acceptPendingMap();
        showMain();
    }
}

void LooperAudioProcessorEditor::handleAcceptMap()
{
    if (! processorRef_.acceptPendingMap()) return;
    showMain();
}

void LooperAudioProcessorEditor::handleBackToPlay()
{
    processorRef_.discardPendingMap();
    showMain();
}

void LooperAudioProcessorEditor::handleOpenReview()
{
    if (! processorRef_.reopenLastReview())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
            "Review map", "No previous auto-map to review. Import samples first.");
        return;
    }
    if (const auto* pending = processorRef_.importController().pendingResult())
    {
        reviewMapView_.setResult (*pending, processorRef_.importController().pendingSampleRefs());
        showReview();
    }
}

void LooperAudioProcessorEditor::handleOpenSettings()
{
    showSettings();
}
