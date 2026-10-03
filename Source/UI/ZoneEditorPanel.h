#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LooperLookAndFeel.h"
#include "../ZoneEdit/ZoneEditor.h"

#include <array>
#include <functional>
#include <memory>

class LooperAudioProcessor;

namespace looper {

/**
 * Main-screen zone editor: root / key range / velocity range / fine tune / gain / RR for the
 * selected zone, plus "Use detected pitch", "Reset to auto", Audition, Undo and Redo.
 * Every change goes through LooperAudioProcessor::performZoneEdit (undoable, live, marks the
 * patch unsaved). Value rules live in ZoneEditor (JUCE-free, unit-tested).
 */
class ZoneEditorPanel : public juce::Component
{
public:
    explicit ZoneEditorPanel (LooperAudioProcessor& processor);
    ~ZoneEditorPanel() override;

    /** Zone index into the processor's map, or -1 for none. */
    void setSelectedZone (int zoneIndex);
    int selectedZone() const noexcept { return selected_; }

    /** Re-read the selected zone (after undo/redo, settings change, etc.). */
    void refresh();

    void undo();
    void redo();

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
    };

    void setupField (Field& f, ZoneField field, const juce::String& caption);
    void applyField (ZoneField field, double value);
    void applyWholeZone (const Zone& proposed, const juce::String& actionName);
    juce::String noteText (int midi) const;
    bool middleCIsC4() const;
    void updateUndoButtons();

    LooperAudioProcessor& processor_;
    int selected_ = -1;
    bool refreshing_ = false;
    bool dragging_ = false;
    bool auditionDown_ = false;

    juce::Label title_, hint_, detectInfo_, warning_;
    std::array<std::unique_ptr<Field>, 9> fields_;
    juce::TextButton useDetectedBtn_ { "Use detected pitch" };
    juce::TextButton resetBtn_ { "Reset to auto" };
    juce::TextButton auditionBtn_ { "Audition" };
    juce::TextButton undoBtn_ { "Undo" };
    juce::TextButton redoBtn_ { "Redo" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZoneEditorPanel)
};

} // namespace looper
