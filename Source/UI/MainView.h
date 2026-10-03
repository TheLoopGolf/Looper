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
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    using ImportFn = std::function<void(const juce::Array<juce::File>&)>;
    using ReviewFn = std::function<void()>;
    using SettingsFn = std::function<void()>;
    using RelocateFn = std::function<void()>;
    using MemoryFn = std::function<void()>;

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
    /** Memory chip click (opens Settings > Memory). */
    void setMemoryCallback (MemoryFn fn) { onMemory_ = std::move (fn); }
    /** Refresh the "RAM 42 MB . Streaming" chip (also runs on a 4 Hz timer while visible). */
    void updateMemoryChip();
    void refreshFromProcessor();

    /** Select a zone (index into the processor's map; -1 = none) in list, strip and editor. */
    void selectZone (int zoneIndex);
    /** Primary selected zone, or -1. */
    int selectedZone() const noexcept { return selection_.primary(); }

    /** Multi-selection (list Shift/Cmd-click, strip Shift/Cmd-click, Cmd/Ctrl+A in the list). */
    void setSelection (const ZoneSelection& selection);
    const ZoneSelection& selection() const noexcept { return selection_; }

    /** Cmd/Ctrl+Z undo, Shift+Cmd/Ctrl+Z (or Ctrl+Y) redo for zone edits. */
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override { updateMemoryChip(); updateLegacyEnvChip(); }
    void visibilityChanged() override;
    /** Rebuild zone rows, strip, status and editor after an edit / undo / redo. */
    void refreshZoneViews();
    void rebuildZoneRows();
    void updateStatus();
    juce::String noteName (int midi) const;
    void styleKnob (juce::Slider& s, juce::Label& label, const juce::String& name,
                    const juce::String& tooltip = {}, bool bipolar = false, bool brass = false);
    /** Show the "amp env > cutoff (legacy)" chip only while an old patch uses that modulation. */
    void updateLegacyEnvChip();

    /** Sound deck (bottom card): two rows of ten cells, shared by paint() and resized(). */
    static constexpr int kPerfH = 190;
    struct PerfLayout
    {
        juce::Rectangle<int> card;
        juce::Rectangle<int> rowTitle[2];   // section caption strips
        juce::Rectangle<int> rowCells[2];   // knob cells area
        int cellW = 0;
        juce::Rectangle<int> cell (int row, int index, int span = 1) const
        {
            const auto& r = rowCells[row];
            return { r.getX() + index * cellW, r.getY(), cellW * span, r.getHeight() };
        }
    };
    PerfLayout perfLayout() const;
    void styleSecondaryButton (juce::TextButton& b);
    void styleBrassButton (juce::TextButton& b);
    void openChooser();
    void openPatchChooser();
    void savePatchChooser();
    void syncZoneKeyboard();
    /** Push selection_ to strip, list and editor (pruned to the current map). */
    void applySelection();
    /** Zone indices in list (keyboard) order, for Shift-click ranges. */
    std::vector<size_t> displayOrder() const;
    void onListSelectionChanged (int lastRowSelected);
    // Keyboard strip drags (one undo step each)
    bool beginStripDrag (StripPart part, int zoneIndex, int anchorKey);
    juce::String moveStripDrag (int key);
    void endStripDrag();

    LooperAudioProcessor& processor_;
    ImportFn onImport_;
    ReviewFn onReview_;
    SettingsFn onSettings_;
    RelocateFn onRelocate_;
    MemoryFn onMemory_;

    LooperLookAndFeel lookAndFeel_;

    juce::Label brand_, brandSub_, subtitle_, status_, dropHint_, samplesTitle_;
    juce::Label ampTitle_, filterTitle_, velTitle_, fenvTitle_, bendTitle_;
    juce::TextButton reviewBtn_ { "Review map" };
    juce::TextButton addBtn_ { "+ Samples" };
    juce::TextButton openBtn_;   // "Open..." (ellipsis glyph set in the constructor)
    juce::TextButton saveBtn_ { "Save" };
    GearButton settingsBtn_;     // vector gear: no font glyph needed
    juce::TextButton missingBanner_;
    MemoryChip memoryChip_;      // header: RAM use + streaming state

    // Round-robin mode (APVTS "rrMode", saved per patch) in the SAMPLES card header
    juce::Label rrLabel_;
    SegmentedChoice rrToggle_ { juce::StringArray { "Cycle", "Random" } };
    juce::TooltipWindow tooltips_ { this, 600 };

    ZoneKeyboardComponent zoneKeyboard_;

    ZoneEditorPanel zoneEditor_;
    ZoneSelection selection_;
    std::string selectedSampleId_;   // primary's sample: keeps the selection across map swaps
    bool syncingSelection_ = false;

    // Strip drag in progress
    StripPart dragPart_ = StripPart::None;
    int dragAnchorKey_ = -1;
    InstrumentMap dragOrigin_;
    bool dragTransactionOpen_ = false;

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
        void deleteKeyPressed (int) override {}
    } zoneModel_;
    juce::ListBox sampleList_;
    juce::Rectangle<int> listHeaderArea_;

    // Row 1: AMP | FILTER (type + cutoff / resonance / key tracking)
    juce::Slider vol_, atk_, dec_, sus_, rel_, cut_, res_, key_;
    juce::Label volL_, atkL_, decL_, susL_, relL_, cutL_, resL_, keyL_;
    // Row 2: VELOCITY | FILTER ENV | BEND
    juce::Slider velAmp_, velCut_, velAtk_, fAtk_, fDec_, fSus_, fRel_, fAmt_, bendUp_, bendDown_;
    juce::Label velAmpL_, velCutL_, velAtkL_, fAtkL_, fDecL_, fSusL_, fRelL_, fAmtL_, bendUpL_, bendDownL_;
    // Filter type: LP12 | LP24 | BP | HP over the "filterType" choice (stored LP12, HP, BP, LP24)
    SegmentedChoice filterType_ { juce::StringArray { "LP12", "LP24", "BP", "HP" } };
    juce::Label filterTypeL_;
    // v1 patches only: amp envelope -> cutoff ("filterEnvAmt"); click moves it to the filter env
    juce::TextButton legacyEnvChip_;
    float legacyEnvShown_ = 0.0f;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::vector<std::unique_ptr<SliderAtt>> knobAttachments_;

    bool dragHighlight_ = false;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<juce::FileChooser> patchChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};

} // namespace looper
