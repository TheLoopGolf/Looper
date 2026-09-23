#pragma once

#include "PluginProcessor.h"
#include "../UI/MainView.h"
#include "../UI/ReviewMapView.h"
#include "../UI/SettingsView.h"
#include "../UI/LooperLookAndFeel.h"

class LooperAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit LooperAudioProcessorEditor (LooperAudioProcessor&);
    ~LooperAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void showMain();
    void showReview();
    void showSettings();

private:
    void handleImport (const juce::Array<juce::File>& files);
    void handleAcceptMap();
    void handleBackToPlay();
    void handleOpenReview();
    void handleOpenSettings();

    LooperAudioProcessor& processorRef_;
    looper::LooperLookAndFeel lookAndFeel_;
    looper::MainView mainView_;
    looper::ReviewMapView reviewMapView_;
    looper::SettingsView settingsView_;
    enum class Screen { Main, Review, Settings };
    Screen screen_ = Screen::Main;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperAudioProcessorEditor)
};
