#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "LooperLookAndFeel.h"
#include "ZoneKeyboardComponent.h"
#include <functional>

class LooperAudioProcessor;

namespace looper {

class MainView : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    using ImportFn = std::function<void(const juce::Array<juce::File>&)>;
    using ReviewFn = std::function<void()>;
    using SettingsFn = std::function<void()>;

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
    void refreshFromProcessor();

private:
    void styleKnob (juce::Slider& s, juce::Label& label, const juce::String& name);
    void styleSecondaryButton (juce::TextButton& b);
    void styleBrassButton (juce::TextButton& b);
    void openChooser();
    void openPatchChooser();
    void savePatchChooser();
    void showMissingSamplesAlert (const juce::StringArray& missing);
    void syncZoneKeyboard();

    LooperAudioProcessor& processor_;
    ImportFn onImport_;
    ReviewFn onReview_;
    SettingsFn onSettings_;

    LooperLookAndFeel lookAndFeel_;

    juce::Label brand_, brandSub_, subtitle_, status_, dropHint_, samplesTitle_;
    juce::Label ampTitle_, filterTitle_;
    juce::TextButton reviewBtn_ { "Review map" };
    juce::TextButton addBtn_ { "+ Samples" };
    juce::TextButton openBtn_ { "Open…" };
    juce::TextButton saveBtn_ { "Save" };
    juce::TextButton settingsBtn_ { "⚙" };

    ZoneKeyboardComponent zoneKeyboard_;

    juce::StringArray sampleNames_;
    struct SampleModel : public juce::ListBoxModel
    {
        juce::StringArray* names = nullptr;
        int getNumRows() override { return names != nullptr ? names->size() : 0; }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (names == nullptr || ! juce::isPositiveAndBelow (row, names->size()))
                return;
            if (selected)
                g.fillAll (Palette::fairwayDim().withAlpha (0.45f));
            else if (row % 2 == 0)
                g.fillAll (Palette::bgRaised().withAlpha (0.35f));
            g.setColour (Palette::text());
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText ((*names)[row], 8, 0, w - 12, h, juce::Justification::centredLeft, true);
        }
    } sampleModel_;
    juce::ListBox sampleList_;

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
