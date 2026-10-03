#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "LooperControls.h"
#include "LooperLookAndFeel.h"
#include "ZoneKeyboardComponent.h"
#include "ZoneEditorPanel.h"
#include <array>
#include <functional>
#include <string>
#include <vector>

class LooperAudioProcessor;

namespace looper {

class MainView : public juce::Component,
                 public juce::FileDragAndDropTarget,
                 private juce::ChangeListener
{
public:
    using ImportFn = std::function<void(const juce::Array<juce::File>&)>;
    using ReviewFn = std::function<void()>;
    using SettingsFn = std::function<void()>;
    using RelocateFn = std::function<void()>;

    explicit MainView (LooperAudioProcessor& processor);
    ~MainView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    void setImportCallback (ImportFn fn) { onImport_ = std::move (fn); }
    void setReviewCallback (ReviewFn fn) { onReview_ = std::move (fn); }
    void setSettingsCallback (SettingsFn fn) { onSettings_ = std::move (fn); }
    /** Opens the Relocate screen (banner click, or automatically after loading a patch with missing files). */
    void setRelocateCallback (RelocateFn fn) { onRelocate_ = std::move (fn); }
    void refreshFromProcessor();

    /** Select a zone (index into the processor's map; -1 = none) in list, strip and editor. */
    void selectZone (int zoneIndex);
    int selectedZone() const noexcept { return selectedZone_; }

    /** Cmd/Ctrl+Z undo, Shift+Cmd/Ctrl+Z (or Ctrl+Y) redo for zone edits. */
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    /** Rebuild zone rows, strip, status and editor after an edit / undo / redo. */
    void refreshZoneViews();
    void rebuildZoneRows();
    void updateStatus();
    juce::String noteName (int midi) const;
    void styleKnob (juce::Slider& s, juce::Label& label, const juce::String& name);
    void styleSecondaryButton (juce::TextButton& b);
    void styleBrassButton (juce::TextButton& b);
    void openChooser();
    void openPatchChooser();
    void savePatchChooser();
    void syncZoneKeyboard();

    LooperAudioProcessor& processor_;
    ImportFn onImport_;
    ReviewFn onReview_;
    SettingsFn onSettings_;
    RelocateFn onRelocate_;

    LooperLookAndFeel lookAndFeel_;

    juce::Label brand_, brandSub_, subtitle_, status_, dropHint_, samplesTitle_;
    juce::Label ampTitle_, filterTitle_;
    juce::TextButton reviewBtn_ { "Review map" };
    juce::TextButton addBtn_ { "+ Samples" };
    juce::TextButton openBtn_;   // "Open..." (ellipsis glyph set in the constructor)
    juce::TextButton saveBtn_ { "Save" };
    GearButton settingsBtn_;     // vector gear: no font glyph needed
    juce::TextButton missingBanner_;

    // Round-robin mode (APVTS "rrMode", saved per patch) in the SAMPLES card header
    juce::Label rrLabel_;
    SegmentedChoice rrToggle_ { juce::StringArray { "Cycle", "Random" } };
    juce::TooltipWindow tooltips_ { this, 600 };

    ZoneKeyboardComponent zoneKeyboard_;

    ZoneEditorPanel zoneEditor_;
    int selectedZone_ = -1;
    std::string selectedSampleId_;
    bool syncingSelection_ = false;

    /** One row per zone (keyboard order) in the ZONES list. */
    struct ZoneRow
    {
        int zoneIndex = -1;
        juce::String name, root, keys, vel, rr;
        bool missing = false, edited = false, overlap = false;
    };
    std::vector<ZoneRow> zoneRows_;
    /** Column x offsets shared by the list rows and the header captions. */
    static std::array<int, 5> zoneColumns (int width);
    struct ZoneListModel : public juce::ListBoxModel
    {
        std::vector<ZoneRow>* rows = nullptr;
        std::function<void (int row)> onSelect;
        int getNumRows() override { return rows != nullptr ? (int) rows->size() : 0; }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
        void selectedRowsChanged (int lastRowSelected) override { if (onSelect) onSelect (lastRowSelected); }
    } zoneModel_;
    juce::ListBox sampleList_;
    juce::Rectangle<int> listHeaderArea_;

    juce::Slider vol_, atk_, dec_, sus_, rel_, cut_, res_, env_;
    juce::Label volL_, atkL_, decL_, susL_, relL_, cutL_, resL_, envL_;
    juce::ComboBox filterBox_;
    juce::Label filterLabel_;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SliderAtt> volA_, atkA_, decA_, susA_, relA_, cutA_, resA_, envA_;
    std::unique_ptr<ComboAtt> filterA_;

    bool dragHighlight_ = false;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<juce::FileChooser> patchChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};

} // namespace looper
