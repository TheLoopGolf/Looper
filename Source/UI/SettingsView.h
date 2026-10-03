#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "LooperLookAndFeel.h"
#include <functional>

class LooperAudioProcessor;

namespace looper {

/** Settings / preferences - left nav Engine | Mapping | MIDI | Files | Memory | About. */
class SettingsView : public juce::Component, private juce::Timer
{
public:
    using BackFn = std::function<void()>;

    explicit SettingsView(LooperAudioProcessor& processor);
    ~SettingsView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void refreshFromProcessor();

    void setBackCallback(BackFn fn) { onBack_ = std::move(fn); }

    /** Open on the Memory tab (main-view memory chip). */
    void showMemoryTab() { setTab(Tab::Memory); }

private:
    enum class Tab { Engine, Mapping, Midi, Files, Memory, About };
    void timerCallback() override { refreshMemoryStatus(); }
    void visibilityChanged() override;
    void refreshMemoryStatus();
    void applyMemoryFromUi();

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
    /** Lays out `count` rows from the top of `area` and consumes that space. */
    void layoutRows(juce::Rectangle<int>& area, PrefRow* rows, int count);
    void applyEngineFromUi();
    void applyMappingFromUi();
    void applyMidiFromUi();
    void revealLastPatchFolder();

    LooperAudioProcessor& processor_;
    BackFn onBack_;
    Tab tab_ = Tab::Engine;

    LooperLookAndFeel lookAndFeel_;

    juce::Label brand_, title_, subtitle_, chromeHint_;
    juce::TextButton backBtn_; // "<- Back to play" (arrow glyph set in the constructor)

    juce::TextButton navEngine_ { "Engine" };
    juce::TextButton navMapping_ { "Mapping" };
    juce::TextButton navMidi_ { "MIDI" };
    juce::TextButton navFiles_ { "Files" };
    juce::TextButton navMemory_ { "Memory" };
    juce::TextButton navAbout_ { "About" };

    juce::Label sectionTitle_, sectionSub_;

    // Engine
    juce::ComboBox polyBox_, interpBox_, glideBox_, softClipBox_, filterBox_;
    PrefRow engineRows_[5];

    // Mapping
    juce::ComboBox midCBox_, spanBox_, rrBox_, velBox_, unpitchedBox_, startNoteBox_, reviewBox_;
    PrefRow mappingRows_[7];
    /** Note-name items (start key, fixed-root label) follow the C4=60 / C3=60 convention. */
    void refreshNoteLabels(bool middleCIsC4);
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rrAttachment_;

    // MIDI
    juce::ComboBox bendBox_, modBox_;
    juce::TextButton clearLearnBtn_ { "Clear MIDI learn" };
    PrefRow midiRows_[3];
    juce::Label sustainNote_;

    // Files
    juce::Label patchPathValue_, missingPolicy_;
    juce::TextButton revealFolderBtn_ { "Reveal last patch folder" };
    PrefRow filesRows_[2];

    // Memory (disk streaming)
    juce::ComboBox ramBox_, preloadBox_;
    juce::TextButton resetDropoutsBtn_ { "Reset dropout count" };
    PrefRow memoryRows_[3];

    // About
    juce::Label aboutName_, aboutCompany_, aboutVersion_, aboutLink_;

    juce::Label footerNote_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsView)
};

} // namespace looper
