#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LooperLookAndFeel.h"
#include <functional>

class LooperAudioProcessor;

namespace looper {

/** Settings / preferences — left nav Engine | Mapping | MIDI | Files | About. */
class SettingsView : public juce::Component
{
public:
    using BackFn = std::function<void()>;

    explicit SettingsView(LooperAudioProcessor& processor);
    ~SettingsView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void refreshFromProcessor();

    void setBackCallback(BackFn fn) { onBack_ = std::move(fn); }

private:
    enum class Tab { Engine, Mapping, Midi, Files, About };

    struct PrefRow
    {
        juce::Label title;
        juce::Label desc;
        juce::ComboBox* combo = nullptr;
        juce::Component* extra = nullptr;
    };

    void setTab(Tab t);
    void updateNavStyles();
    void updateVisibility();
    void styleCombo(juce::ComboBox& c);
    void layoutRows(juce::Rectangle<int> area, PrefRow* rows, int count);
    void applyEngineFromUi();
    void applyMappingFromUi();
    void applyMidiFromUi();
    void revealLastPatchFolder();

    LooperAudioProcessor& processor_;
    BackFn onBack_;
    Tab tab_ = Tab::Engine;

    LooperLookAndFeel lookAndFeel_;

    juce::Label brand_, title_, subtitle_, chromeHint_;
    juce::TextButton backBtn_ { "← Back to play" };

    juce::TextButton navEngine_ { "Engine" };
    juce::TextButton navMapping_ { "Mapping" };
    juce::TextButton navMidi_ { "MIDI" };
    juce::TextButton navFiles_ { "Files" };
    juce::TextButton navAbout_ { "About" };

    juce::Label sectionTitle_, sectionSub_;

    // Engine
    juce::ComboBox polyBox_, interpBox_, glideBox_, softClipBox_, filterBox_;
    PrefRow engineRows_[5];

    // Mapping
    juce::ComboBox midCBox_, spanBox_, rrBox_, velBox_, unpitchedBox_, reviewBox_;
    PrefRow mappingRows_[6];

    // MIDI
    juce::ComboBox bendBox_, modBox_;
    juce::TextButton clearLearnBtn_ { "Clear MIDI learn" };
    PrefRow midiRows_[3];
    juce::Label sustainNote_;

    // Files
    juce::Label patchPathValue_, missingPolicy_;
    juce::TextButton revealFolderBtn_ { "Reveal last patch folder" };
    PrefRow filesRows_[2];

    // About
    juce::Label aboutName_, aboutCompany_, aboutVersion_, aboutLink_;

    juce::Label footerNote_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsView)
};

} // namespace looper
