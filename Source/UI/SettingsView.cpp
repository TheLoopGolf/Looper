#include "SettingsView.h"
#include "LooperLookAndFeel.h"
#include "../Plugin/PluginProcessor.h"

namespace looper {
namespace {
juce::Colour kBg()      { return Palette::bg(); }
juce::Colour kPanel()   { return Palette::bgRaised(); }
juce::Colour kBorder()  { return Palette::border(); }
juce::Colour kAccent()  { return Palette::fairway(); }
juce::Colour kText()    { return Palette::text(); }
juce::Colour kMuted()   { return Palette::muted(); }
juce::Colour kNavSel()  { return Palette::fairwayDim().withAlpha (0.35f); }
} // namespace

void SettingsView::styleCombo(juce::ComboBox& c)
{
    c.setColour(juce::ComboBox::backgroundColourId, Palette::bgSunken());
    c.setColour(juce::ComboBox::outlineColourId, Palette::border());
    c.setColour(juce::ComboBox::textColourId, Palette::text());
    c.setColour(juce::ComboBox::arrowColourId, Palette::muted());
}

SettingsView::SettingsView(LooperAudioProcessor& processor) : processor_(processor)
{
    setLookAndFeel (&lookAndFeel_);

    brand_.setText("LOOPER", juce::dontSendNotification);
    brand_.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    brand_.setColour(juce::Label::textColourId, kText());
    addAndMakeVisible(brand_);

    title_.setText("Settings / preferences", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, kAccent());
    addAndMakeVisible(title_);

    subtitle_.setText("Engine defaults and library behavior. Patch-specific knobs stay on the main view.",
                      juce::dontSendNotification);
    subtitle_.setColour(juce::Label::textColourId, kMuted());
    addAndMakeVisible(subtitle_);

    chromeHint_.setText("Preferences · applies to new maps & this session", juce::dontSendNotification);
    chromeHint_.setColour(juce::Label::textColourId, kMuted());
    addAndMakeVisible(chromeHint_);

    backBtn_.setColour(juce::TextButton::buttonColourId, Palette::bgRaised());
    backBtn_.setColour(juce::TextButton::textColourOffId, Palette::text());
    backBtn_.onClick = [this] { if (onBack_) onBack_(); };
    addAndMakeVisible(backBtn_);

    auto wireNav = [this](juce::TextButton& b, Tab t) {
        b.setColour(juce::TextButton::textColourOffId, kText());
        b.onClick = [this, t] { setTab(t); };
        addAndMakeVisible(b);
    };
    wireNav(navEngine_, Tab::Engine);
    wireNav(navMapping_, Tab::Mapping);
    wireNav(navMidi_, Tab::Midi);
    wireNav(navFiles_, Tab::Files);
    wireNav(navAbout_, Tab::About);

    sectionTitle_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    sectionTitle_.setColour(juce::Label::textColourId, kText());
    addAndMakeVisible(sectionTitle_);
    sectionSub_.setColour(juce::Label::textColourId, kMuted());
    addAndMakeVisible(sectionSub_);

    auto initRow = [this](PrefRow& row, const juce::String& t, const juce::String& d, juce::ComboBox* box) {
        row.title.setText(t, juce::dontSendNotification);
        row.title.setColour(juce::Label::textColourId, kText());
        row.title.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        row.desc.setText(d, juce::dontSendNotification);
        row.desc.setColour(juce::Label::textColourId, kMuted());
        row.desc.setFont(juce::FontOptions(12.0f));
        row.combo = box;
        addAndMakeVisible(row.title);
        addAndMakeVisible(row.desc);
        if (box)
        {
            styleCombo(*box);
            addAndMakeVisible(*box);
        }
    };

    // --- Engine ---
    for (int n : { 1, 2, 4, 8, 16, 24, 32, 48, 64, 96, 128 })
        polyBox_.addItem(juce::String(n), n);
    polyBox_.onChange = [this] { applyEngineFromUi(); };

    interpBox_.addItem("Hermite", 1);
    interpBox_.addItem("Sinc (later)", 2);
    interpBox_.setItemEnabled(2, false);
    interpBox_.setSelectedId(1, juce::dontSendNotification);

    glideBox_.addItem("Off · 0 ms", 1);
    glideBox_.addItem("10 ms", 2);
    glideBox_.addItem("25 ms", 3);
    glideBox_.addItem("50 ms", 4);
    glideBox_.addItem("100 ms", 5);
    glideBox_.addItem("200 ms", 6);
    glideBox_.onChange = [this] { applyEngineFromUi(); };

    softClipBox_.addItem("Off", 1);
    softClipBox_.addItem("On", 2);
    softClipBox_.onChange = [this] { applyEngineFromUi(); };

    filterBox_.addItem("Low-pass", 1);
    filterBox_.addItem("High-pass", 2);
    filterBox_.addItem("Band-pass", 3);
    filterBox_.onChange = [this] { applyEngineFromUi(); };

    initRow(engineRows_[0], "Polyphony", "Voice steal: quietest releasing, then oldest", &polyBox_);
    initRow(engineRows_[1], "Interpolation", "v1 default — higher quality sinc later", &interpBox_);
    initRow(engineRows_[2], "Glide", "Legato portamento when enabled", &glideBox_);
    initRow(engineRows_[3], "Master soft-clip", "Optional ceiling when stacking many voices", &softClipBox_);
    initRow(engineRows_[4], "Default filter type", "Per-patch override still on main view", &filterBox_);

    // --- Mapping ---
    midCBox_.addItem("C4 = 60", 1);
    midCBox_.setSelectedId(1, juce::dontSendNotification);
    // locked for v1

    spanBox_.addItem("Full keyboard 0–127", 1);
    spanBox_.addItem("Natural span only", 2);
    spanBox_.onChange = [this] { applyMappingFromUi(); };

    rrBox_.addItem("Cycle", 1);
    rrBox_.addItem("Random (later)", 2);
    rrBox_.setItemEnabled(2, false);
    rrBox_.onChange = [this] { applyMappingFromUi(); };

    velBox_.addItem("Linear", 1);
    velBox_.addItem("Soft", 2);
    velBox_.addItem("Hard", 3);
    velBox_.onChange = [this] { applyMappingFromUi(); };

    unpitchedBox_.addItem("Equal spread + warn", 1);
    unpitchedBox_.setSelectedId(1, juce::dontSendNotification);

    reviewBox_.addItem("On", 1);
    reviewBox_.addItem("Off", 2);
    reviewBox_.onChange = [this] { applyMappingFromUi(); };

    initRow(mappingRows_[0], "Middle C convention", "Scientific pitch; matches most modern packs", &midCBox_);
    initRow(mappingRows_[1], "Key span", "Or natural span only (roots' neighborhood)", &spanBox_);
    initRow(mappingRows_[2], "Round-robin mode", "Cycle or random within RR group", &rrBox_);
    initRow(mappingRows_[3], "Velocity curve", "Soft / linear / hard — global feel", &velBox_);
    initRow(mappingRows_[4], "Unpitched fallback", "When filename + detect both fail", &unpitchedBox_);
    initRow(mappingRows_[5], "Open review after import", "If any warning/confidence < 0.8", &reviewBox_);

    // --- MIDI ---
    bendBox_.addItem("±2 semitones", 1);
    bendBox_.setSelectedId(1, juce::dontSendNotification);
    // VoiceEngine / MidiRouter fixed ±2 for v1 — store preference

    modBox_.addItem("FilterCutoff", 1);
    modBox_.addItem("Volume", 2);
    modBox_.onChange = [this] { applyMidiFromUi(); };

    initRow(midiRows_[0], "Pitch bend range", "14-bit bend; VoiceEngine applies ±range", &bendBox_);
    initRow(midiRows_[1], "Mod wheel target", "CC1 → filter cutoff or volume", &modBox_);
    initRow(midiRows_[2], "Sustain pedal", "CC64", nullptr);
    sustainNote_.setText("CC64 active — note-off deferred while pedal down (VoiceEngine)",
                         juce::dontSendNotification);
    sustainNote_.setColour(juce::Label::textColourId, kMuted());
    addAndMakeVisible(sustainNote_);
    midiRows_[2].extra = &sustainNote_;

    clearLearnBtn_.setColour(juce::TextButton::buttonColourId, kPanel());
    clearLearnBtn_.setColour(juce::TextButton::textColourOffId, kText());
    clearLearnBtn_.onClick = [] {
        // MIDI learn not implemented in v1 — stub.
    };
    addAndMakeVisible(clearLearnBtn_);

    // --- Files ---
    initRow(filesRows_[0], "Last patch path", "Most recent Open / Save location", nullptr);
    patchPathValue_.setColour(juce::Label::textColourId, kText());
    patchPathValue_.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(patchPathValue_);
    filesRows_[0].extra = &patchPathValue_;

    initRow(filesRows_[1], "Missing-file policy", "Offline list on load; relocate UI later", nullptr);
    missingPolicy_.setText("Missing samples stay offline (zones kept). Relocate UI TODO.",
                           juce::dontSendNotification);
    missingPolicy_.setColour(juce::Label::textColourId, kMuted());
    addAndMakeVisible(missingPolicy_);
    filesRows_[1].extra = &missingPolicy_;

    revealFolderBtn_.setColour(juce::TextButton::buttonColourId, kPanel());
    revealFolderBtn_.setColour(juce::TextButton::textColourOffId, kText());
    revealFolderBtn_.onClick = [this] { revealLastPatchFolder(); };
    addAndMakeVisible(revealFolderBtn_);

    // --- About ---
    aboutName_.setText("Looper", juce::dontSendNotification);
    aboutName_.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    aboutName_.setColour(juce::Label::textColourId, kText());
    addAndMakeVisible(aboutName_);

    aboutCompany_.setText("Loop Audio Lab", juce::dontSendNotification);
    aboutCompany_.setColour(juce::Label::textColourId, kMuted());
    addAndMakeVisible(aboutCompany_);

    aboutVersion_.setColour(juce::Label::textColourId, kText());
    addAndMakeVisible(aboutVersion_);

    aboutLink_.setText("https://github.com/TheLoopGolf/Looper", juce::dontSendNotification);
    aboutLink_.setColour(juce::Label::textColourId, kAccent());
    aboutLink_.setFont(juce::FontOptions(13.0f, juce::Font::underlined));
    addAndMakeVisible(aboutLink_);

    footerNote_.setColour(juce::Label::textColourId, kMuted());
    footerNote_.setFont(juce::FontOptions(11.0f));
    footerNote_.setText("Settings are global/session · performance knobs remain on main view",
                        juce::dontSendNotification);
    addAndMakeVisible(footerNote_);

    setTab(Tab::Engine);
    refreshFromProcessor();
}

SettingsView::~SettingsView()
{
    setLookAndFeel (nullptr);
}

void SettingsView::setTab(Tab t)
{
    tab_ = t;
    switch (tab_)
    {
        case Tab::Engine:
            sectionTitle_.setText("Engine", juce::dontSendNotification);
            sectionSub_.setText("Voice + quality defaults for playback.", juce::dontSendNotification);
            break;
        case Tab::Mapping:
            sectionTitle_.setText("Mapping", juce::dontSendNotification);
            sectionSub_.setText("Defaults for AutoMapper + zone building.", juce::dontSendNotification);
            break;
        case Tab::Midi:
            sectionTitle_.setText("MIDI", juce::dontSendNotification);
            sectionSub_.setText("Bend, mod target, sustain, learn.", juce::dontSendNotification);
            break;
        case Tab::Files:
            sectionTitle_.setText("Files", juce::dontSendNotification);
            sectionSub_.setText("Patch paths and missing-sample policy.", juce::dontSendNotification);
            break;
        case Tab::About:
            sectionTitle_.setText("About", juce::dontSendNotification);
            sectionSub_.setText("Looper · Loop Audio Lab", juce::dontSendNotification);
            break;
    }
    updateNavStyles();
    updateVisibility();
    resized();
    repaint();
}

void SettingsView::updateNavStyles()
{
    auto paintNav = [](juce::TextButton& b, bool sel) {
        b.setColour(juce::TextButton::buttonColourId,
                    sel ? Palette::fairwayDim().withAlpha (0.45f) : Palette::bgSunken());
        b.setColour(juce::TextButton::textColourOffId,
                    sel ? Palette::fairway() : Palette::text());
    };
    paintNav(navEngine_, tab_ == Tab::Engine);
    paintNav(navMapping_, tab_ == Tab::Mapping);
    paintNav(navMidi_, tab_ == Tab::Midi);
    paintNav(navFiles_, tab_ == Tab::Files);
    paintNav(navAbout_, tab_ == Tab::About);
}

void SettingsView::updateVisibility()
{
    auto setEngine = tab_ == Tab::Engine;
    auto setMapping = tab_ == Tab::Mapping;
    auto setMidi = tab_ == Tab::Midi;
    auto setFiles = tab_ == Tab::Files;
    auto setAbout = tab_ == Tab::About;

    for (auto& r : engineRows_)
    {
        r.title.setVisible(setEngine);
        r.desc.setVisible(setEngine);
        if (r.combo) r.combo->setVisible(setEngine);
    }
    for (auto& r : mappingRows_)
    {
        r.title.setVisible(setMapping);
        r.desc.setVisible(setMapping);
        if (r.combo) r.combo->setVisible(setMapping);
    }
    for (auto& r : midiRows_)
    {
        r.title.setVisible(setMidi);
        r.desc.setVisible(setMidi);
        if (r.combo) r.combo->setVisible(setMidi);
        if (r.extra) r.extra->setVisible(setMidi);
    }
    clearLearnBtn_.setVisible(setMidi);

    for (auto& r : filesRows_)
    {
        r.title.setVisible(setFiles);
        r.desc.setVisible(setFiles);
        if (r.extra) r.extra->setVisible(setFiles);
    }
    revealFolderBtn_.setVisible(setFiles);

    aboutName_.setVisible(setAbout);
    aboutCompany_.setVisible(setAbout);
    aboutVersion_.setVisible(setAbout);
    aboutLink_.setVisible(setAbout);
}

void SettingsView::refreshFromProcessor()
{
    const auto& p = processor_.sessionPrefs();

    polyBox_.setSelectedId(p.polyphony, juce::dontSendNotification);
    if (polyBox_.getSelectedId() == 0)
    {
        // custom value not in list — add temporarily
        polyBox_.addItem(juce::String(p.polyphony), p.polyphony);
        polyBox_.setSelectedId(p.polyphony, juce::dontSendNotification);
    }
    interpBox_.setSelectedId(1, juce::dontSendNotification);

    int glideId = 1;
    if (p.glideMs >= 150.0f) glideId = 6;
    else if (p.glideMs >= 75.0f) glideId = 5;
    else if (p.glideMs >= 40.0f) glideId = 4;
    else if (p.glideMs >= 20.0f) glideId = 3;
    else if (p.glideMs >= 5.0f) glideId = 2;
    glideBox_.setSelectedId(glideId, juce::dontSendNotification);

    softClipBox_.setSelectedId(p.masterSoftClip ? 2 : 1, juce::dontSendNotification);
    filterBox_.setSelectedId(p.defaultFilterType + 1, juce::dontSendNotification);

    midCBox_.setSelectedId(1, juce::dontSendNotification);
    spanBox_.setSelectedId(p.preferFullKeyboardSpan ? 1 : 2, juce::dontSendNotification);
    rrBox_.setSelectedId(p.cycleRrDefault ? 1 : 1, juce::dontSendNotification);
    switch (p.velCurve)
    {
        case VelCurve::Soft: velBox_.setSelectedId(2, juce::dontSendNotification); break;
        case VelCurve::Hard: velBox_.setSelectedId(3, juce::dontSendNotification); break;
        case VelCurve::Linear: velBox_.setSelectedId(1, juce::dontSendNotification); break;
    }
    unpitchedBox_.setSelectedId(1, juce::dontSendNotification);
    reviewBox_.setSelectedId(p.openReviewAfterImport ? 1 : 2, juce::dontSendNotification);

    bendBox_.setSelectedId(1, juce::dontSendNotification);
    modBox_.setSelectedId(p.modWheelTarget == ModWheelTarget::Volume ? 2 : 1, juce::dontSendNotification);

    const auto& path = processor_.lastPatchPath();
    patchPathValue_.setText(path.isNotEmpty() ? path : "(none yet)", juce::dontSendNotification);
    revealFolderBtn_.setEnabled(path.isNotEmpty());

#if defined(JucePlugin_VersionString)
    aboutVersion_.setText(juce::String("Version ") + JucePlugin_VersionString, juce::dontSendNotification);
#else
    aboutVersion_.setText("Version 0.1.0", juce::dontSendNotification);
#endif

    updateVisibility();
}

void SettingsView::applyEngineFromUi()
{
    auto prefs = processor_.sessionPrefs();
    prefs.polyphony = polyBox_.getSelectedId() > 0 ? polyBox_.getSelectedId() : 64;
    static const float kGlideMs[] = { 0.f, 0.f, 10.f, 25.f, 50.f, 100.f, 200.f };
    const int gid = glideBox_.getSelectedId();
    prefs.glideMs = (gid >= 1 && gid <= 6) ? kGlideMs[gid] : 0.f;
    prefs.masterSoftClip = softClipBox_.getSelectedId() == 2;
    prefs.defaultFilterType = juce::jlimit(0, 2, filterBox_.getSelectedId() - 1);
    processor_.applySessionPrefs(prefs);
}

void SettingsView::applyMappingFromUi()
{
    auto prefs = processor_.sessionPrefs();
    prefs.middleCIsC4 = true;
    prefs.preferFullKeyboardSpan = spanBox_.getSelectedId() != 2;
    prefs.cycleRrDefault = true; // Random disabled
    switch (velBox_.getSelectedId())
    {
        case 2: prefs.velCurve = VelCurve::Soft; break;
        case 3: prefs.velCurve = VelCurve::Hard; break;
        default: prefs.velCurve = VelCurve::Linear; break;
    }
    prefs.openReviewAfterImport = reviewBox_.getSelectedId() != 2;
    processor_.applySessionPrefs(prefs);
}

void SettingsView::applyMidiFromUi()
{
    auto prefs = processor_.sessionPrefs();
    prefs.pitchBendRangeSemis = 2.0f;
    prefs.modWheelTarget = (modBox_.getSelectedId() == 2) ? ModWheelTarget::Volume
                                                          : ModWheelTarget::FilterCutoff;
    processor_.applySessionPrefs(prefs);
}

void SettingsView::revealLastPatchFolder()
{
    const auto& path = processor_.lastPatchPath();
    if (path.isEmpty()) return;
    juce::File f(path);
    if (f.existsAsFile())
        f.getParentDirectory().revealToUser();
    else if (f.getParentDirectory().isDirectory())
        f.getParentDirectory().revealToUser();
}

void SettingsView::paint(juce::Graphics& g)
{
    g.fillAll(Palette::bg());
    auto header = getLocalBounds().reduced(16).removeFromTop(48);
    drawFlagstick(g, juce::Rectangle<float>((float) header.getX() + 2.0f,
                                            (float) header.getY() + 4.0f, 16.0f, 22.0f));
    g.setColour(Palette::brass().withAlpha(0.7f));
    g.fillRect((float) header.getX(), (float) header.getBottom() - 1.0f, 72.0f, 1.0f);

    auto r = getLocalBounds().reduced(16);
    r.removeFromTop(70);
    auto panel = r.toFloat();
    g.setColour(Palette::bgRaised());
    g.fillRoundedRectangle(panel, 12.0f);
    g.setColour(Palette::border());
    g.drawRoundedRectangle(panel, 12.0f, 1.2f);

    // left nav rail
    auto body = r.reduced(8);
    body.removeFromTop(40);
    auto nav = body.removeFromLeft(140).toFloat();
    g.setColour(Palette::bgSunken());
    g.fillRoundedRectangle(nav, 8.0f);

    // Fairway underline cue under selected nav (approx via tab order heights)
    const int navH = 32;
    int selIndex = 0;
    switch (tab_)
    {
        case Tab::Engine: selIndex = 0; break;
        case Tab::Mapping: selIndex = 1; break;
        case Tab::Midi: selIndex = 2; break;
        case Tab::Files: selIndex = 3; break;
        case Tab::About: selIndex = 4; break;
    }
    const float uy = nav.getY() + 4.0f + (float) selIndex * (float) navH + (float) navH - 4.0f;
    g.setColour(Palette::fairway());
    g.fillRoundedRectangle(nav.getX() + 8.0f, uy, nav.getWidth() - 16.0f, 2.0f, 1.0f);
}

void SettingsView::layoutRows(juce::Rectangle<int> area, PrefRow* rows, int count)
{
    const int rowH = 52;
    for (int i = 0; i < count; ++i)
    {
        auto row = area.removeFromTop(rowH);
        auto left = row.removeFromLeft(juce::jmax(200, row.getWidth() - 180));
        rows[i].title.setBounds(left.removeFromTop(22));
        rows[i].desc.setBounds(left);
        auto right = row.reduced(4, 10);
        if (rows[i].combo)
            rows[i].combo->setBounds(right.removeFromRight(160));
        else if (rows[i].extra)
            rows[i].extra->setBounds(right);
    }
}

void SettingsView::resized()
{
    auto r = getLocalBounds().reduced(16);
    auto header = r.removeFromTop(48);
    brand_.setBounds(header.getX() + 26, header.getY() + 6, 140, 24);
    title_.setBounds(r.removeFromTop(24));
    subtitle_.setBounds(r.removeFromTop(20));
    r.removeFromTop(6);

    auto chrome = r.removeFromTop(36);
    chromeHint_.setBounds(chrome.removeFromLeft(juce::jmax(200, chrome.getWidth() - 160)));
    backBtn_.setBounds(chrome.removeFromRight(140).reduced(2));

    auto body = r.reduced(8);
    body.removeFromTop(4);
    auto navCol = body.removeFromLeft(140);
    const int navH = 32;
    navEngine_.setBounds(navCol.removeFromTop(navH).reduced(4, 2));
    navMapping_.setBounds(navCol.removeFromTop(navH).reduced(4, 2));
    navMidi_.setBounds(navCol.removeFromTop(navH).reduced(4, 2));
    navFiles_.setBounds(navCol.removeFromTop(navH).reduced(4, 2));
    navAbout_.setBounds(navCol.removeFromTop(navH).reduced(4, 2));

    body.removeFromLeft(12);
    footerNote_.setBounds(body.removeFromBottom(24));
    sectionTitle_.setBounds(body.removeFromTop(22));
    sectionSub_.setBounds(body.removeFromTop(20));
    body.removeFromTop(8);

    // Hide all content bounds first by zeroing unused
    clearLearnBtn_.setBounds({});
    revealFolderBtn_.setBounds({});
    aboutName_.setBounds({});
    aboutCompany_.setBounds({});
    aboutVersion_.setBounds({});
    aboutLink_.setBounds({});

    switch (tab_)
    {
        case Tab::Engine:
            layoutRows(body, engineRows_, 5);
            break;
        case Tab::Mapping:
            layoutRows(body, mappingRows_, 6);
            break;
        case Tab::Midi:
            layoutRows(body, midiRows_, 3);
            clearLearnBtn_.setBounds(body.removeFromTop(36).removeFromLeft(180).reduced(0, 4));
            break;
        case Tab::Files:
            layoutRows(body, filesRows_, 2);
            revealFolderBtn_.setBounds(body.removeFromTop(36).removeFromLeft(220).reduced(0, 4));
            break;
        case Tab::About:
            aboutName_.setBounds(body.removeFromTop(28));
            aboutCompany_.setBounds(body.removeFromTop(22));
            aboutVersion_.setBounds(body.removeFromTop(22));
            body.removeFromTop(8);
            aboutLink_.setBounds(body.removeFromTop(24));
            break;
    }
}

} // namespace looper
