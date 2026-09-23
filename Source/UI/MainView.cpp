#include "MainView.h"
#include "../Plugin/PluginProcessor.h"
#include "../Import/ImportController.h"
#include <algorithm>

namespace looper {
namespace {
constexpr uint32_t kBg = 0xff1c1c20;
constexpr uint32_t kPanel = 0xff24242a;
constexpr uint32_t kBorder = 0xff3a3a48;
constexpr uint32_t kAccent = 0xff6a8cff;
constexpr uint32_t kText = 0xffe8e8ee;
constexpr uint32_t kMuted = 0xff9a9aa8;
}

MainView::MainView(LooperAudioProcessor& processor) : processor_(processor)
{
    brand_.setText("LOOPER  Loop Audio Lab", juce::dontSendNotification);
    brand_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    brand_.setColour(juce::Label::textColourId, juce::Colour(kText));
    addAndMakeVisible(brand_);

    subtitle_.setColour(juce::Label::textColourId, juce::Colour(kMuted));
    addAndMakeVisible(subtitle_);

    status_.setColour(juce::Label::textColourId, juce::Colour(kAccent));
    status_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(status_);

    auto styleBtn = [](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(kPanel));
        b.setColour(juce::TextButton::textColourOffId, juce::Colour(kText));
    };
    styleBtn(reviewBtn_);
    styleBtn(addBtn_);
    styleBtn(openBtn_);
    styleBtn(saveBtn_);
    styleBtn(settingsBtn_);
    settingsBtn_.setTooltip("Settings / preferences");
    reviewBtn_.onClick = [this] { if (onReview_) onReview_(); };
    addBtn_.onClick = [this] { openChooser(); };
    openBtn_.onClick = [this] { openPatchChooser(); };
    saveBtn_.onClick = [this] { savePatchChooser(); };
    settingsBtn_.onClick = [this] { if (onSettings_) onSettings_(); };
    addAndMakeVisible(reviewBtn_);
    addAndMakeVisible(addBtn_);
    addAndMakeVisible(openBtn_);
    addAndMakeVisible(saveBtn_);
    addAndMakeVisible(settingsBtn_);

    dropHint_.setJustificationType(juce::Justification::centred);
    dropHint_.setColour(juce::Label::textColourId, juce::Colour(kText));
    dropHint_.setFont(juce::FontOptions(18.0f));
    addAndMakeVisible(dropHint_);

    keyboardHint_.setJustificationType(juce::Justification::centred);
    keyboardHint_.setColour(juce::Label::textColourId, juce::Colour(kMuted));
    keyboardHint_.setText("Keyboard / zone strip (viz TODO)", juce::dontSendNotification);
    addAndMakeVisible(keyboardHint_);

    samplesTitle_.setText("Samples", juce::dontSendNotification);
    samplesTitle_.setColour(juce::Label::textColourId, juce::Colour(kMuted));
    addAndMakeVisible(samplesTitle_);

    sampleModel_.names = &sampleNames_;
    sampleList_.setModel(&sampleModel_);
    sampleList_.setColour(juce::ListBox::backgroundColourId, juce::Colour(kPanel));
    addAndMakeVisible(sampleList_);

    perfTitle_.setText("Performance", juce::dontSendNotification);
    perfTitle_.setColour(juce::Label::textColourId, juce::Colour(kMuted));
    addAndMakeVisible(perfTitle_);

    styleKnob(vol_, volL_, "Vol");
    styleKnob(atk_, atkL_, "Atk");
    styleKnob(dec_, decL_, "Dec");
    styleKnob(sus_, susL_, "Sus");
    styleKnob(rel_, relL_, "Rel");
    styleKnob(cut_, cutL_, "Cut");
    styleKnob(res_, resL_, "Res");
    styleKnob(env_, envL_, "Env");

    auto& ap = processor_.apvts();
    volA_ = std::make_unique<SliderAtt>(ap, "volume", vol_);
    atkA_ = std::make_unique<SliderAtt>(ap, "attack", atk_);
    decA_ = std::make_unique<SliderAtt>(ap, "decay", dec_);
    susA_ = std::make_unique<SliderAtt>(ap, "sustain", sus_);
    relA_ = std::make_unique<SliderAtt>(ap, "release", rel_);
    cutA_ = std::make_unique<SliderAtt>(ap, "cutoff", cut_);
    resA_ = std::make_unique<SliderAtt>(ap, "resonance", res_);
    envA_ = std::make_unique<SliderAtt>(ap, "filterEnvAmt", env_);

    filterLabel_.setText("Filter", juce::dontSendNotification);
    filterLabel_.setColour(juce::Label::textColourId, juce::Colour(kMuted));
    addAndMakeVisible(filterLabel_);
    filterBox_.addItemList({ "LP", "HP", "BP" }, 1);
    addAndMakeVisible(filterBox_);
    filterA_ = std::make_unique<ComboAtt>(ap, "filterType", filterBox_);

    refreshFromProcessor();
}

MainView::~MainView() { sampleList_.setModel(nullptr); }

void MainView::styleKnob(juce::Slider& s, juce::Label& label, const juce::String& name)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(kAccent));
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(kBorder));
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colour(kText));
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(s);
    label.setText(name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(kMuted));
    addAndMakeVisible(label);
}

void MainView::refreshFromProcessor()
{
    const bool loaded = processor_.hasUserInstrument();
    if (loaded)
    {
        subtitle_.setText("Play immediately. Open Review map when auto-map needs a fix.",
                          juce::dontSendNotification);
        dropHint_.setVisible(false);
        reviewBtn_.setVisible(true);
        addBtn_.setVisible(true);
        openBtn_.setVisible(true);
        saveBtn_.setVisible(true);
        saveBtn_.setEnabled(true);
        samplesTitle_.setVisible(true);
        sampleList_.setVisible(true);
        juce::String st;
        st << processor_.patchName() << " · " << processor_.zoneCount() << " zones · "
           << processor_.rootCount() << " roots";
        const int rr = processor_.rrDepth();
        if (rr > 0)
            st << " · RR x " << rr;
        if (! processor_.offlineSampleIds().empty())
            st << " · " << (int) processor_.offlineSampleIds().size() << " offline";
        status_.setText(st, juce::dontSendNotification);
        sampleNames_.clear();
        for (const auto& ref : processor_.userSampleRefs())
        {
            juce::String name = ref.displayName.empty() ? ref.path : ref.displayName;
            const bool offline = std::find(processor_.offlineSampleIds().begin(),
                                           processor_.offlineSampleIds().end(),
                                           ref.id) != processor_.offlineSampleIds().end();
            if (offline)
                name = "[offline] " + name;
            sampleNames_.add(name);
        }
        sampleList_.updateContent();
    }
    else
    {
        subtitle_.setText("Drop samples to build an instrument. Mapping stays secondary.",
                          juce::dontSendNotification);
        dropHint_.setText("Drop WAV / AIFF folder here\nor click to browse · auto-map on import",
                          juce::dontSendNotification);
        dropHint_.setVisible(true);
        reviewBtn_.setVisible(false);
        addBtn_.setVisible(false);
        openBtn_.setVisible(true);  // allow Open from empty state
        saveBtn_.setVisible(false);
        samplesTitle_.setVisible(false);
        sampleList_.setVisible(false);
        status_.setText({}, juce::dontSendNotification);
        sampleNames_.clear();
        sampleList_.updateContent();
    }
    resized();
    repaint();
}

void MainView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBg));
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(52);
    if (! processor_.hasUserInstrument())
    {
        auto dz = area.removeFromTop(juce::jmax(120, getHeight() / 3)).toFloat();
        g.setColour(juce::Colour(dragHighlight_ ? 0xff2a3350 : kPanel));
        g.fillRoundedRectangle(dz, 8.0f);
        g.setColour(juce::Colour(dragHighlight_ ? kAccent : kBorder));
        g.drawRoundedRectangle(dz, 8.0f, dragHighlight_ ? 2.0f : 1.2f);
    }
    else
    {
        auto bar = area.removeFromTop(36).toFloat();
        g.setColour(juce::Colour(kPanel));
        g.fillRoundedRectangle(bar, 6.0f);
        g.setColour(juce::Colour(kBorder));
        g.drawRoundedRectangle(bar, 6.0f, 1.0f);
    }
    auto kb = area.removeFromTop(72).toFloat();
    g.setColour(juce::Colour(kPanel));
    g.fillRoundedRectangle(kb, 6.0f);
    g.setColour(juce::Colour(kBorder));
    g.drawRoundedRectangle(kb, 6.0f, 1.0f);
    auto perf = getLocalBounds().reduced(16).removeFromBottom(150).toFloat();
    g.setColour(juce::Colour(kPanel));
    g.fillRoundedRectangle(perf, 6.0f);
    g.setColour(juce::Colour(kBorder));
    g.drawRoundedRectangle(perf, 6.0f, 1.0f);
}

void MainView::resized()
{
    auto r = getLocalBounds().reduced(16);
    brand_.setBounds(r.removeFromTop(22));
    subtitle_.setBounds(r.removeFromTop(20));
    r.removeFromTop(8);

    auto perf = getLocalBounds().reduced(16).removeFromBottom(150);
    perfTitle_.setBounds(perf.removeFromTop(20).reduced(8, 0));
    filterLabel_.setBounds(perf.getRight() - 140, perf.getY() - 20, 50, 18);
    filterBox_.setBounds(perf.getRight() - 88, perf.getY() - 22, 72, 22);

    juce::Slider* knobs[] = { &vol_, &atk_, &dec_, &sus_, &rel_, &cut_, &res_, &env_ };
    juce::Label* labels[] = { &volL_, &atkL_, &decL_, &susL_, &relL_, &cutL_, &resL_, &envL_ };
    const int kw = perf.getWidth() / 8;
    for (int i = 0; i < 8; ++i)
    {
        auto cell = perf.withX(perf.getX() + i * kw).withWidth(kw).reduced(4);
        labels[i]->setBounds(cell.removeFromTop(16));
        knobs[i]->setBounds(cell);
    }

    auto mid = getLocalBounds().reduced(16);
    mid.removeFromTop(52);
    mid.removeFromBottom(158);

    // Settings gear always top-right
    settingsBtn_.setBounds(getWidth() - 16 - 36, 14, 36, 28);

    if (! processor_.hasUserInstrument())
    {
        auto dz = mid.removeFromTop(juce::jmax(120, getHeight() / 3));
        dropHint_.setBounds(dz.reduced(16));
        keyboardHint_.setBounds(mid.removeFromTop(72).reduced(8));
        reviewBtn_.setBounds({});
        addBtn_.setBounds({});
        openBtn_.setBounds(getWidth() - 16 - 80 - 44, 16, 80, 28);
        saveBtn_.setBounds({});
        status_.setBounds({});
        samplesTitle_.setBounds({});
        sampleList_.setBounds({});
    }
    else
    {
        auto bar = mid.removeFromTop(36).reduced(6, 4);
        reviewBtn_.setBounds(bar.removeFromLeft(110));
        bar.removeFromLeft(8);
        addBtn_.setBounds(bar.removeFromLeft(100));
        bar.removeFromLeft(8);
        openBtn_.setBounds(bar.removeFromLeft(72));
        bar.removeFromLeft(8);
        saveBtn_.setBounds(bar.removeFromLeft(72));
        status_.setBounds(bar);
        keyboardHint_.setBounds(mid.removeFromTop(72).reduced(8));
        samplesTitle_.setBounds(mid.removeFromTop(22).reduced(8, 0));
        sampleList_.setBounds(mid.reduced(6));
        dropHint_.setBounds({});
    }
}

void MainView::mouseDown(const juce::MouseEvent& e)
{
    if (processor_.hasUserInstrument())
        return;
    auto mid = getLocalBounds().reduced(16);
    mid.removeFromTop(52);
    if (mid.removeFromTop(juce::jmax(120, getHeight() / 3)).contains(e.getPosition()))
        openChooser();
}

bool MainView::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& p : files)
    {
        const juce::File f(p);
        if (f.isDirectory())
            return true;
        if (ImportController::isSupportedAudioExtension(f.getFileExtension()))
            return true;
    }
    return false;
}

void MainView::fileDragEnter(const juce::StringArray&, int, int)
{
    dragHighlight_ = true;
    repaint();
}

void MainView::fileDragExit(const juce::StringArray&)
{
    dragHighlight_ = false;
    repaint();
}

void MainView::filesDropped(const juce::StringArray& files, int, int)
{
    dragHighlight_ = false;
    repaint();
    juce::Array<juce::File> arr;
    for (const auto& p : files)
        arr.add(juce::File(p));
    if (onImport_)
        onImport_(arr);
}

void MainView::openChooser()
{
    chooser_ = std::make_unique<juce::FileChooser>(
        "Import samples", juce::File{}, "*.wav;*.aiff;*.aif;*.flac", true);
    const auto chooserFlags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::canSelectDirectories
                     | juce::FileBrowserComponent::canSelectMultipleItems;
    chooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
        auto results = fc.getResults();
        if (! results.isEmpty() && onImport_)
            onImport_(results);
    });
}


void MainView::showMissingSamplesAlert(const juce::StringArray& missing)
{
    if (missing.isEmpty())
        return;
    juce::String body = "Some sample files could not be loaded (offline). Zones are kept; relocate UI comes later.\n\n";
    const int n = juce::jmin(12, missing.size());
    for (int i = 0; i < n; ++i)
        body << "• " << missing[i] << "\n";
    if (missing.size() > n)
        body << "… and " << (missing.size() - n) << " more";
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                           "Missing samples", body);
}

void MainView::openPatchChooser()
{
    patchChooser_ = std::make_unique<juce::FileChooser>(
        "Open Looper patch",
        processor_.lastPatchPath().isNotEmpty() ? juce::File(processor_.lastPatchPath()) : juce::File{},
        "*.looper.json;*.json", true);
    constexpr auto chooserFlags = juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles;
    patchChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (!f.existsAsFile())
            return;
        juce::StringArray missing;
        if (!processor_.loadPatchFromFile(f, &missing))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Open patch", "Could not parse patch file.");
            return;
        }
        refreshFromProcessor();
        showMissingSamplesAlert(missing);
    });
}

void MainView::savePatchChooser()
{
    if (!processor_.hasUserInstrument())
        return;
    juce::File suggest;
    if (processor_.lastPatchPath().isNotEmpty())
        suggest = juce::File(processor_.lastPatchPath());
    else
        suggest = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                      .getChildFile(processor_.patchName() + ".looper.json");

    patchChooser_ = std::make_unique<juce::FileChooser>(
        "Save Looper patch", suggest, "*.looper.json;*.json", true);
    constexpr auto chooserFlags = juce::FileBrowserComponent::saveMode
                         | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting;
    patchChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f.getFullPathName().isEmpty())
            return;
        if (!f.getFileExtension().equalsIgnoreCase(".json")
            && !f.getFileName().endsWithIgnoreCase(".looper.json"))
            f = f.withFileExtension(".looper.json");
        if (!processor_.savePatchToFile(f))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Save patch", "Failed to write patch file.");
            return;
        }
        refreshFromProcessor();
    });
}

} // namespace looper
