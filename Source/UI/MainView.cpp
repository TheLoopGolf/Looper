#include "MainView.h"
#include "../Plugin/PluginProcessor.h"
#include "../Import/ImportController.h"
#include <algorithm>

namespace looper {

MainView::MainView (LooperAudioProcessor& processor) : processor_ (processor)
{
    setLookAndFeel (&lookAndFeel_);

    brand_.setText ("LOOPER", juce::dontSendNotification);
    brand_.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    brand_.setColour (juce::Label::textColourId, Palette::text());
    addAndMakeVisible (brand_);

    brandSub_.setText ("Loop Audio Lab", juce::dontSendNotification);
    brandSub_.setFont (juce::Font (juce::FontOptions (11.0f)));
    brandSub_.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (brandSub_);

    subtitle_.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (subtitle_);

    status_.setColour (juce::Label::textColourId, Palette::fairway());
    status_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (status_);

    styleSecondaryButton (reviewBtn_);
    styleSecondaryButton (addBtn_);
    styleBrassButton (openBtn_);
    styleBrassButton (saveBtn_);
    styleSecondaryButton (settingsBtn_);
    settingsBtn_.setTooltip ("Settings / preferences");
    reviewBtn_.onClick = [this] { if (onReview_) onReview_(); };
    addBtn_.onClick = [this] { openChooser(); };
    openBtn_.onClick = [this] { openPatchChooser(); };
    saveBtn_.onClick = [this] { savePatchChooser(); };
    settingsBtn_.onClick = [this] { if (onSettings_) onSettings_(); };
    addAndMakeVisible (reviewBtn_);
    addAndMakeVisible (addBtn_);
    addAndMakeVisible (openBtn_);
    addAndMakeVisible (saveBtn_);
    addAndMakeVisible (settingsBtn_);

    dropHint_.setJustificationType (juce::Justification::centred);
    dropHint_.setColour (juce::Label::textColourId, Palette::text());
    dropHint_.setFont (juce::Font (juce::FontOptions (16.0f)));
    addAndMakeVisible (dropHint_);

    addAndMakeVisible (zoneKeyboard_);

    samplesTitle_.setText ("SAMPLES", juce::dontSendNotification);
    samplesTitle_.setFont (juce::Font (juce::FontOptions (11.0f)));
    samplesTitle_.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (samplesTitle_);

    sampleModel_.names = &sampleNames_;
    sampleList_.setModel (&sampleModel_);
    sampleList_.setColour (juce::ListBox::backgroundColourId, Palette::bgSunken());
    sampleList_.setColour (juce::ListBox::outlineColourId, Palette::border());
    addAndMakeVisible (sampleList_);

    ampTitle_.setText ("AMP", juce::dontSendNotification);
    ampTitle_.setFont (juce::Font (juce::FontOptions (11.0f)));
    ampTitle_.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (ampTitle_);

    filterTitle_.setText ("FILTER", juce::dontSendNotification);
    filterTitle_.setFont (juce::Font (juce::FontOptions (11.0f)));
    filterTitle_.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (filterTitle_);

    styleKnob (vol_, volL_, "VOL");
    styleKnob (atk_, atkL_, "ATK");
    styleKnob (dec_, decL_, "DEC");
    styleKnob (sus_, susL_, "SUS");
    styleKnob (rel_, relL_, "REL");
    styleKnob (cut_, cutL_, "CUT");
    styleKnob (res_, resL_, "RES");
    styleKnob (env_, envL_, "ENV");

    auto& ap = processor_.apvts();
    volA_ = std::make_unique<SliderAtt> (ap, "volume", vol_);
    atkA_ = std::make_unique<SliderAtt> (ap, "attack", atk_);
    decA_ = std::make_unique<SliderAtt> (ap, "decay", dec_);
    susA_ = std::make_unique<SliderAtt> (ap, "sustain", sus_);
    relA_ = std::make_unique<SliderAtt> (ap, "release", rel_);
    cutA_ = std::make_unique<SliderAtt> (ap, "cutoff", cut_);
    resA_ = std::make_unique<SliderAtt> (ap, "resonance", res_);
    envA_ = std::make_unique<SliderAtt> (ap, "filterEnvAmt", env_);

    filterLabel_.setText ("MODE", juce::dontSendNotification);
    filterLabel_.setColour (juce::Label::textColourId, Palette::muted());
    filterLabel_.setFont (juce::Font (juce::FontOptions (10.0f)));
    addAndMakeVisible (filterLabel_);
    filterBox_.addItemList ({ "LP", "HP", "BP" }, 1);
    addAndMakeVisible (filterBox_);
    filterA_ = std::make_unique<ComboAtt> (ap, "filterType", filterBox_);

    refreshFromProcessor();
}

MainView::~MainView()
{
    sampleList_.setModel (nullptr);
    setLookAndFeel (nullptr);
}

void MainView::styleSecondaryButton (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, Palette::bgRaised());
    b.setColour (juce::TextButton::textColourOffId, Palette::text());
}

void MainView::styleBrassButton (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, Palette::brass().withAlpha (0.25f));
    b.setColour (juce::TextButton::textColourOffId, Palette::text());
}

void MainView::styleKnob (juce::Slider& s, juce::Label& label, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
    s.setColour (juce::Slider::rotarySliderFillColourId, Palette::fairway());
    s.setColour (juce::Slider::rotarySliderOutlineColourId, Palette::border());
    s.setColour (juce::Slider::textBoxTextColourId, Palette::text());
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (s);
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (10.0f)));
    label.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (label);
}

void MainView::syncZoneKeyboard()
{
    if (processor_.hasUserInstrument())
        zoneKeyboard_.setZones (processor_.getZoneKeySpans());
    else
        zoneKeyboard_.clearZones();
}

void MainView::refreshFromProcessor()
{
    const bool loaded = processor_.hasUserInstrument();
    if (loaded)
    {
        subtitle_.setText ("Play immediately. Open Review map when auto-map needs a fix.",
                           juce::dontSendNotification);
        dropHint_.setVisible (false);
        reviewBtn_.setVisible (true);
        addBtn_.setVisible (true);
        openBtn_.setVisible (true);
        saveBtn_.setVisible (true);
        saveBtn_.setEnabled (true);
        samplesTitle_.setVisible (true);
        sampleList_.setVisible (true);
        juce::String st;
        st << processor_.patchName() << " · " << processor_.zoneCount() << " zones · "
           << processor_.rootCount() << " roots";
        const int rr = processor_.rrDepth();
        if (rr > 0)
            st << " · RR x " << rr;
        if (! processor_.offlineSampleIds().empty())
            st << " · " << (int) processor_.offlineSampleIds().size() << " offline";
        status_.setText (st, juce::dontSendNotification);
        sampleNames_.clear();
        for (const auto& ref : processor_.userSampleRefs())
        {
            juce::String name = ref.displayName.empty() ? ref.path : ref.displayName;
            const bool offline = std::find (processor_.offlineSampleIds().begin(),
                                            processor_.offlineSampleIds().end(),
                                            ref.id) != processor_.offlineSampleIds().end();
            if (offline)
                name = "[offline] " + name;
            sampleNames_.add (name);
        }
        sampleList_.updateContent();
    }
    else
    {
        subtitle_.setText ("Drop samples to build an instrument. Mapping stays secondary.",
                           juce::dontSendNotification);
        dropHint_.setText ("Drop a sample folder — AutoMapper builds the map.\nClick to browse · WAV / AIFF / FLAC",
                           juce::dontSendNotification);
        dropHint_.setVisible (true);
        reviewBtn_.setVisible (false);
        addBtn_.setVisible (false);
        openBtn_.setVisible (true);
        saveBtn_.setVisible (false);
        samplesTitle_.setVisible (false);
        sampleList_.setVisible (false);
        status_.setText ({}, juce::dontSendNotification);
        sampleNames_.clear();
        sampleList_.updateContent();
    }
    syncZoneKeyboard();
    resized();
    repaint();
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::bg());
    auto area = getLocalBounds().reduced (16);

    // Header wordmark + flagstick + brass accent
    auto header = area.removeFromTop (48);
    const float flagX = 16.0f + 6.0f;
    drawFlagstick (g, juce::Rectangle<float> (flagX, (float) header.getY() + 4.0f, 16.0f, 22.0f));

    juce::ColourGradient accent (Palette::fairway(), (float) header.getX(), (float) header.getBottom(),
                                 Palette::fairway().withAlpha (0.0f), (float) header.getRight(),
                                 (float) header.getBottom(), false);
    g.setGradientFill (accent);
    g.fillRect ((float) header.getX(), (float) header.getBottom() - 1.5f,
                (float) header.getWidth() * 0.55f, 1.5f);
    g.setColour (Palette::brass().withAlpha (0.7f));
    g.fillRect ((float) header.getX(), (float) header.getBottom() - 1.0f, 72.0f, 1.0f);

    area.removeFromTop (8);

    const int perfH = 168;
    auto mid = area;
    mid.removeFromBottom (perfH + 8);

    if (! processor_.hasUserInstrument())
    {
        auto dz = mid.removeFromTop (juce::jmax (130, getHeight() / 3)).toFloat();
        g.setColour (dragHighlight_ ? Palette::fairway().withAlpha (0.12f) : Palette::bgSunken());
        g.fillRoundedRectangle (dz, 12.0f);

        g.setColour (dragHighlight_ ? Palette::fairway() : Palette::fairwayDim());
        const float dash[] = { 6.0f, 4.0f };
        juce::Path border;
        border.addRoundedRectangle (dz.reduced (1.0f), 12.0f);
        juce::PathStrokeType stroke (dragHighlight_ ? 2.4f : 1.6f);
        stroke.createDashedStroke (border, border, dash, 2);
        g.strokePath (border, stroke);

        if (dragHighlight_)
        {
            g.setColour (Palette::fairway().withAlpha (0.18f));
            g.fillRoundedRectangle (dz.reduced (4.0f), 10.0f);
        }

        // Centered flagstick icon above drop copy
        drawFlagstick (g, juce::Rectangle<float> (dz.getCentreX() - 10.0f, dz.getY() + 18.0f, 20.0f, 28.0f));
    }
    else
    {
        auto bar = mid.removeFromTop (40).toFloat();
        g.setColour (Palette::bgRaised());
        g.fillRoundedRectangle (bar, 10.0f);
        g.setColour (Palette::border());
        g.drawRoundedRectangle (bar, 10.0f, 1.0f);
    }

    // Zone keyboard card backdrop (component draws its own keys)
    mid.removeFromTop (8);
    auto kb = mid.removeFromTop (88).toFloat();
    g.setColour (Palette::bgRaised());
    g.fillRoundedRectangle (kb, 10.0f);
    g.setColour (Palette::border());
    g.drawRoundedRectangle (kb, 10.0f, 1.0f);

    if (processor_.hasUserInstrument())
    {
        auto samplesCard = mid.toFloat().reduced (0.0f, 4.0f);
        g.setColour (Palette::bgRaised());
        g.fillRoundedRectangle (samplesCard, 10.0f);
        g.setColour (Palette::border());
        g.drawRoundedRectangle (samplesCard, 10.0f, 1.0f);
    }

    // Performance deck card
    auto perf = getLocalBounds().reduced (16).removeFromBottom (perfH).toFloat();
    g.setColour (Palette::bgRaised());
    g.fillRoundedRectangle (perf, 12.0f);
    g.setColour (Palette::border());
    g.drawRoundedRectangle (perf, 12.0f, 1.0f);

    // Thin divider between AMP and FILTER groups (after 5 knobs / before filter)
    const float divX = perf.getX() + perf.getWidth() * (5.0f / 8.0f);
    g.setColour (Palette::border());
    g.drawLine (divX, perf.getY() + 28.0f, divX, perf.getBottom() - 12.0f, 1.0f);
}

void MainView::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto header = r.removeFromTop (48);
    // Leave room for flagstick (~24px)
    brand_.setBounds (header.getX() + 26, header.getY() + 2, 120, 22);
    brandSub_.setBounds (header.getX() + 26, header.getY() + 24, 160, 16);
    subtitle_.setBounds (header.getX() + 200, header.getY() + 8,
                         juce::jmax (120, header.getWidth() - 360), 28);

    settingsBtn_.setBounds (getWidth() - 16 - 36, 18, 36, 28);

    const int perfH = 168;
    auto perf = getLocalBounds().reduced (16).removeFromBottom (perfH).reduced (10, 8);
    auto titleRow = perf.removeFromTop (18);
    ampTitle_.setBounds (titleRow.removeFromLeft (titleRow.getWidth() * 5 / 8));
    filterTitle_.setBounds (titleRow);

    auto modeRow = perf.removeFromTop (22);
    modeRow.removeFromLeft (modeRow.getWidth() * 5 / 8);
    filterLabel_.setBounds (modeRow.removeFromLeft (44));
    filterBox_.setBounds (modeRow.removeFromLeft (72).reduced (0, 1));

    juce::Slider* knobs[] = { &vol_, &atk_, &dec_, &sus_, &rel_, &cut_, &res_, &env_ };
    juce::Label* labels[] = { &volL_, &atkL_, &decL_, &susL_, &relL_, &cutL_, &resL_, &envL_ };
    const int kw = perf.getWidth() / 8;
    for (int i = 0; i < 8; ++i)
    {
        auto cell = perf.withX (perf.getX() + i * kw).withWidth (kw).reduced (4);
        labels[i]->setBounds (cell.removeFromTop (14));
        knobs[i]->setBounds (cell);
    }

    auto mid = getLocalBounds().reduced (16);
    mid.removeFromTop (56);
    mid.removeFromBottom (perfH + 8);

    if (! processor_.hasUserInstrument())
    {
        auto dz = mid.removeFromTop (juce::jmax (130, getHeight() / 3));
        dropHint_.setBounds (dz.withTrimmedTop (48).reduced (20, 8));
        mid.removeFromTop (8);
        zoneKeyboard_.setBounds (mid.removeFromTop (88).reduced (8));
        reviewBtn_.setBounds ({});
        addBtn_.setBounds ({});
        openBtn_.setBounds (getWidth() - 16 - 80 - 44, 18, 80, 28);
        saveBtn_.setBounds ({});
        status_.setBounds ({});
        samplesTitle_.setBounds ({});
        sampleList_.setBounds ({});
    }
    else
    {
        auto bar = mid.removeFromTop (40).reduced (8, 6);
        reviewBtn_.setBounds (bar.removeFromLeft (110));
        bar.removeFromLeft (8);
        addBtn_.setBounds (bar.removeFromLeft (100));
        bar.removeFromLeft (8);
        openBtn_.setBounds (bar.removeFromLeft (72));
        bar.removeFromLeft (8);
        saveBtn_.setBounds (bar.removeFromLeft (72));
        status_.setBounds (bar);
        mid.removeFromTop (8);
        zoneKeyboard_.setBounds (mid.removeFromTop (88).reduced (8));
        samplesTitle_.setBounds (mid.removeFromTop (22).reduced (10, 0));
        sampleList_.setBounds (mid.reduced (10, 6));
        dropHint_.setBounds ({});
    }
}

void MainView::mouseDown (const juce::MouseEvent& e)
{
    if (processor_.hasUserInstrument())
        return;
    auto mid = getLocalBounds().reduced (16);
    mid.removeFromTop (56);
    if (mid.removeFromTop (juce::jmax (130, getHeight() / 3)).contains (e.getPosition()))
        openChooser();
}

bool MainView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& p : files)
    {
        const juce::File f (p);
        if (f.isDirectory())
            return true;
        if (ImportController::isSupportedAudioExtension (f.getFileExtension()))
            return true;
    }
    return false;
}

void MainView::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHighlight_ = true;
    repaint();
}

void MainView::fileDragExit (const juce::StringArray&)
{
    dragHighlight_ = false;
    repaint();
}

void MainView::filesDropped (const juce::StringArray& files, int, int)
{
    dragHighlight_ = false;
    repaint();
    juce::Array<juce::File> arr;
    for (const auto& p : files)
        arr.add (juce::File (p));
    if (onImport_)
        onImport_ (arr);
}

void MainView::openChooser()
{
    chooser_ = std::make_unique<juce::FileChooser> (
        "Import samples", juce::File{}, "*.wav;*.aiff;*.aif;*.flac", true);
    const auto chooserFlags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::canSelectDirectories
                     | juce::FileBrowserComponent::canSelectMultipleItems;
    chooser_->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc) {
        auto results = fc.getResults();
        if (! results.isEmpty() && onImport_)
            onImport_ (results);
    });
}

void MainView::showMissingSamplesAlert (const juce::StringArray& missing)
{
    if (missing.isEmpty())
        return;
    juce::String body = "Some sample files could not be loaded (offline). Zones are kept; relocate UI comes later.\n\n";
    const int n = juce::jmin (12, missing.size());
    for (int i = 0; i < n; ++i)
        body << "• " << missing[i] << "\n";
    if (missing.size() > n)
        body << "… and " << (missing.size() - n) << " more";
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                            "Missing samples", body);
}

void MainView::openPatchChooser()
{
    patchChooser_ = std::make_unique<juce::FileChooser> (
        "Open Looper patch",
        processor_.lastPatchPath().isNotEmpty() ? juce::File (processor_.lastPatchPath()) : juce::File{},
        "*.looper.json;*.json", true);
    constexpr auto chooserFlags = juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles;
    patchChooser_->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (! f.existsAsFile())
            return;
        juce::StringArray missing;
        if (! processor_.loadPatchFromFile (f, &missing))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                "Open patch", "Could not parse patch file.");
            return;
        }
        refreshFromProcessor();
        showMissingSamplesAlert (missing);
    });
}

void MainView::savePatchChooser()
{
    if (! processor_.hasUserInstrument())
        return;
    juce::File suggest;
    if (processor_.lastPatchPath().isNotEmpty())
        suggest = juce::File (processor_.lastPatchPath());
    else
        suggest = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                      .getChildFile (processor_.patchName() + ".looper.json");

    patchChooser_ = std::make_unique<juce::FileChooser> (
        "Save Looper patch", suggest, "*.looper.json;*.json", true);
    constexpr auto chooserFlags = juce::FileBrowserComponent::saveMode
                         | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting;
    patchChooser_->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f.getFullPathName().isEmpty())
            return;
        if (! f.getFileExtension().equalsIgnoreCase (".json")
            && ! f.getFileName().endsWithIgnoreCase (".looper.json"))
            f = f.withFileExtension (".looper.json");
        if (! processor_.savePatchToFile (f))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                "Save patch", "Failed to write patch file.");
            return;
        }
        refreshFromProcessor();
    });
}

} // namespace looper
