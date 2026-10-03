#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LooperLookAndFeel.h"
#include "../ZoneEdit/ZoneEditor.h"
#include "../ZoneEdit/ZoneSelection.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>

class LooperAudioProcessor;

namespace looper {

/**
 * Main-screen zone editor: root / key range / velocity range / fine tune / gain / RR for the
 * selected zone(s), plus "Use detected pitch", "Reset to auto", Audition, Shift keys, Undo
 * and Redo.
 *
 * With several zones selected, fields whose values differ show "mixed"; dragging or scrolling
 * root / key edges / fine tune / gain offsets every zone from its own value, velocity and RR
 * fields are set absolutely, typing "+3" / "-3" offsets and a plain value (or "=-6") sets all.
 * Each action is one undo step (LooperAudioProcessor::performZoneEdits). Value rules live in
 * ZoneEditor / ZoneSelection (JUCE-free, unit-tested).
 */
class ZoneEditorPanel : public juce::Component
{
public:
    explicit ZoneEditorPanel (LooperAudioProcessor& processor);
    ~ZoneEditorPanel() override;

    /** Zones being edited (indices into the processor's map). */
    void setSelection (const ZoneSelection& selection);
    const ZoneSelection& selection() const noexcept { return selection_; }
    /** Single-zone convenience: zone index, or -1 for none. */
    void setSelectedZone (int zoneIndex);
    /** Primary selected zone (audition target), or -1. */
    int selectedZone() const noexcept { return selection_.primary(); }

    /** Re-read the selected zones (after undo/redo, settings change, etc.). */
    void refresh();

    void undo();
    void redo();
    /** Shift the selected zones' key ranges and roots by `semis` (one undo step). */
    void shiftSelectedKeys (int semis);

    /** Called after this panel changed the map (MainView refreshes list / strip / status). */
    std::function<void()> onEdited;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Field
    {
        ZoneField field;
        juce::Label caption;
        juce::Slider slider;
        bool mixed = false;
        double minValue = 0.0, maxValue = 0.0;
        double lastValue = 0.0;   // slider value after the last refresh / edit (scroll deltas)
    };
    struct TypedEdit
    {
        bool relative = false;
        double value = 0.0;       // offset when relative
    };

    void setupField (Field& f, ZoneField field, const juce::String& caption);
    void applyField (Field& f);
    bool applyChanges (const std::vector<ZoneChange>& changes, const juce::String& actionName, bool newTransaction);
    juce::String valueText (ZoneField field, double v) const;
    juce::String offsetText (ZoneField field, double delta) const;
    double parseValue (ZoneField field, const juce::String& text) const;
    juce::String noteText (int midi) const;
    juce::String zoneName (const Zone& zone) const;
    bool middleCIsC4() const;
    bool isMulti() const noexcept { return selection_.isMulti(); }
    void updateUndoButtons();

    LooperAudioProcessor& processor_;
    ZoneSelection selection_;
    bool refreshing_ = false;
    bool auditionDown_ = false;

    // Slider drag in progress: edits are computed from the zones as they were at drag start
    Field* dragField_ = nullptr;
    InstrumentMap dragOrigin_;
    double dragStartValue_ = 0.0;
    bool dragTransactionOpen_ = false;
    std::optional<TypedEdit> typed_;

    juce::Label title_, hint_, detectInfo_, warning_, shiftCaption_;
    std::array<std::unique_ptr<Field>, 9> fields_;
    juce::TextButton useDetectedBtn_ { "Use detected pitch" };
    juce::TextButton resetBtn_ { "Reset to auto" };
    juce::TextButton auditionBtn_ { "Audition" };
    juce::TextButton undoBtn_ { "Undo" };
    juce::TextButton redoBtn_ { "Redo" };
    std::array<juce::TextButton, 4> shiftBtns_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZoneEditorPanel)
};

} // namespace looper
