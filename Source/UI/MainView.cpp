#include "MainView.h"
#include "../Plugin/PluginProcessor.h"
#include "../Import/ImportController.h"
#include "Glyphs.h"
#include "../AutoMapper/FilenameTokens.h"
#include "../ZoneEdit/ZoneEditor.h"
#include "../SamplePool/MemoryFormat.h"
#include <algorithm>

namespace looper {

MainView::MainView (LooperAudioProcessor& processor) : processor_ (processor), zoneEditor_ (processor)
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

    openBtn_.setButtonText ("Open" + glyph::ellipsis());
    styleSecondaryButton (reviewBtn_);
    styleSecondaryButton (addBtn_);
    styleBrassButton (openBtn_);
    styleBrassButton (saveBtn_);
    settingsBtn_.setColour (juce::TextButton::buttonColourId, Palette::bgRaised());
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

    // Missing-samples banner: sand "bunker" chip that opens the Relocate screen
    // Dark brass tone -> LooperLookAndFeel draws a raised chip with a brass outline
    missingBanner_.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3212));
    missingBanner_.setColour (juce::TextButton::textColourOffId, Palette::sand());
    missingBanner_.setTooltip ("Some sample files were not found. Click to relocate them.");
    missingBanner_.onClick = [this] { if (onRelocate_) onRelocate_(); };
    addChildComponent (missingBanner_);

    memoryChip_.onClick = [this] { if (onMemory_) onMemory_(); else if (onSettings_) onSettings_(); };
    addChildComponent (memoryChip_);

    dropHint_.setJustificationType (juce::Justification::centred);
    dropHint_.setColour (juce::Label::textColourId, Palette::text());
    dropHint_.setFont (juce::Font (juce::FontOptions (16.0f)));
    addAndMakeVisible (dropHint_);

    addAndMakeVisible (zoneKeyboard_);
    zoneKeyboard_.onZoneClicked = [this] (int zoneIndex, ClickModifier mod) {
        if (zoneIndex < 0)
            return;
        auto next = selection_;
        applySelectionClick (next, (size_t) zoneIndex, mod, displayOrder());
        setSelection (next);
    };
    zoneKeyboard_.onAudition = [this] (int zoneIndex, bool down) { processor_.auditionZone (zoneIndex, down); };
    zoneKeyboard_.onDragStart = [this] (StripPart part, int zoneIndex, int anchorKey) { return beginStripDrag (part, zoneIndex, anchorKey); };
    zoneKeyboard_.onDragMove = [this] (int key) { return moveStripDrag (key); };
    zoneKeyboard_.onDragEnd = [this] { endStripDrag(); };

    addAndMakeVisible (zoneEditor_);
    zoneEditor_.onEdited = [this] { refreshZoneViews(); };
    processor_.undoManager().addChangeListener (this);
    setWantsKeyboardFocus (true);

    rrLabel_.setText ("ROUND ROBIN", juce::dontSendNotification);
    rrLabel_.setFont (juce::Font (juce::FontOptions (10.0f)));
    rrLabel_.setColour (juce::Label::textColourId, Palette::muted());
    rrLabel_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (rrLabel_);
    if (auto* rrParam = processor_.apvts().getParameter ("rrMode"))
        rrToggle_.attachToParameter (*rrParam);
    rrToggle_.setTitle ("Round robin mode");
    addAndMakeVisible (rrToggle_);

    samplesTitle_.setText ("ZONES", juce::dontSendNotification);
    samplesTitle_.setFont (juce::Font (juce::FontOptions (11.0f)));
    samplesTitle_.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (samplesTitle_);

    zoneModel_.rows = &zoneRows_;
    zoneModel_.onSelect = [this] (int row) { onListSelectionChanged (row); };
    sampleList_.setModel (&zoneModel_);
    sampleList_.setMultipleSelectionEnabled (true);   // Shift / Cmd(Ctrl)-click, Cmd/Ctrl+A when focused
    sampleList_.setWantsKeyboardFocus (true);
    sampleList_.setRowHeight (22);
    sampleList_.setTitle ("Zones");
    sampleList_.setColour (juce::ListBox::backgroundColourId, Palette::bgSunken());
    sampleList_.setColour (juce::ListBox::outlineColourId, Palette::border());
    addAndMakeVisible (sampleList_);

    ampTitle_.setText ("AMP", juce::dontSendNotification);
    ampTitle_.setFont (juce::Font (juce::FontOptions (11.0f)));
    ampTitle_.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (ampTitle_);

    for (auto* t : { &filterTitle_, &velTitle_, &fenvTitle_, &bendTitle_ })
    {
        t->setFont (juce::Font (juce::FontOptions (11.0f)));
        t->setColour (juce::Label::textColourId, Palette::muted());
        addAndMakeVisible (*t);
    }
    filterTitle_.setText ("FILTER", juce::dontSendNotification);
    velTitle_.setText ("VELOCITY", juce::dontSendNotification);
    fenvTitle_.setText ("FILTER ENV", juce::dontSendNotification);
    bendTitle_.setText ("BEND", juce::dontSendNotification);

    const auto pm = glyph::plusMinus();
    styleKnob (vol_, volL_, "VOL");
    styleKnob (atk_, atkL_, "ATK");
    styleKnob (dec_, decL_, "DEC");
    styleKnob (sus_, susL_, "SUS");
    styleKnob (rel_, relL_, "REL");
    styleKnob (cut_, cutL_, "CUT", "Filter cutoff (Hz). Changes glide over 20 ms while notes sound.");
    styleKnob (res_, resL_, "RES", "Filter resonance");
    styleKnob (key_, keyL_, "KEY",
               "Key tracking: the cutoff follows the note you play. 100% = one octave per octave; "
               "C4 (MIDI 60) is the pivot and sounds at the CUT value.");
    styleKnob (velAmp_, velAmpL_, "AMP",
               "Velocity to loudness. 100% = full velocity range (classic), 0% = every note at full level.",
               false, true);
    styleKnob (velCut_, velCutL_, "CUT",
               "Velocity to cutoff (semitones). Full velocity plays the CUT value; softer notes move the "
               "cutoff down by up to this amount (negative: softer notes are brighter).",
               true, true);
    styleKnob (velAtk_, velAtkL_, "ATK",
               "Velocity to attack time: harder = faster. Full velocity uses the amp ATK value; softer notes "
               "attack up to 16x slower at 100%.",
               false, true);
    styleKnob (fAtk_, fAtkL_, "ATK", "Filter envelope attack", false, true);
    styleKnob (fDec_, fDecL_, "DEC", "Filter envelope decay", false, true);
    styleKnob (fSus_, fSusL_, "SUS", "Filter envelope sustain level", false, true);
    styleKnob (fRel_, fRelL_, "REL", "Filter envelope release", false, true);
    styleKnob (fAmt_, fAmtL_, "AMT",
               "Filter envelope depth in semitones (" + pm + "60 = " + pm + "5 octaves at the envelope peak).",
               true, true);
    styleKnob (bendUp_, bendUpL_, "UP", "Pitch-bend range up (semitones)");
    styleKnob (bendDown_, bendDownL_, "DOWN", "Pitch-bend range down (semitones)");

    auto& ap = processor_.apvts();
    const std::pair<juce::Slider*, const char*> bindings[] = {
        { &vol_, "volume" }, { &atk_, "attack" }, { &dec_, "decay" }, { &sus_, "sustain" }, { &rel_, "release" },
        { &cut_, "cutoff" }, { &res_, "resonance" }, { &key_, "keyTrack" },
        { &velAmp_, "velAmp" }, { &velCut_, "velCutoff" }, { &velAtk_, "velAttack" },
        { &fAtk_, "fenvAttack" }, { &fDec_, "fenvDecay" }, { &fSus_, "fenvSustain" }, { &fRel_, "fenvRelease" },
        { &fAmt_, "fenvAmount" }, { &bendUp_, "bendUp" }, { &bendDown_, "bendDown" },
    };
    for (const auto& [slider, id] : bindings)
        knobAttachments_.push_back (std::make_unique<SliderAtt> (ap, id, *slider));
    // Double-click resets to the parameter default
    for (const auto& [slider, id] : bindings)
        if (auto* param = ap.getParameter (id))
            slider->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    filterTypeL_.setText ("TYPE", juce::dontSendNotification);
    filterTypeL_.setJustificationType (juce::Justification::centred);
    filterTypeL_.setFont (juce::Font (juce::FontOptions (10.0f)));
    filterTypeL_.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (filterTypeL_);
    filterType_.setSegmentValues ({ 0, 3, 2, 1 });   // LP12, LP24, BP, HP -> choice indices
    if (auto* ft = ap.getParameter ("filterType"))
        filterType_.attachToParameter (*ft);
    filterType_.setTitle ("Filter type");
    filterType_.setTooltip ("Filter type: low-pass 12 dB/oct, low-pass 24 dB/oct, band-pass, high-pass");
    addAndMakeVisible (filterType_);

    legacyEnvChip_.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3212));
    legacyEnvChip_.setColour (juce::TextButton::textColourOffId, Palette::sand());
    legacyEnvChip_.onClick = [this] {
        processor_.convertLegacyFilterEnv();
        updateLegacyEnvChip();
        updateStatus();
    };
    addChildComponent (legacyEnvChip_);
    updateLegacyEnvChip();

    refreshFromProcessor();
}

MainView::~MainView()
{
    processor_.undoManager().removeChangeListener (this);
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

void MainView::updateLegacyEnvChip()
{
    float amt = 0.0f, fenv = 0.0f;
    if (auto* raw = processor_.apvts().getRawParameterValue ("filterEnvAmt"))
        amt = raw->load();
    if (auto* raw = processor_.apvts().getRawParameterValue ("fenvAmount"))
        fenv = raw->load();
    const bool show = ! juce::exactlyEqual (amt, 0.0f);
    const bool fenvUnused = juce::exactlyEqual (fenv, 0.0f);
    if (show)
    {
        const auto text = glyph::spaced ("Amp env " + glyph::arrowRight() + " cutoff "
                                             + (amt > 0.0f ? "+" : "") + juce::String (amt, 2) + " oct",
                                         glyph::middleDot(), fenvUnused ? "Move to filter env" : "Remove");
        if (text != legacyEnvChip_.getButtonText())
            legacyEnvChip_.setButtonText (text);
        legacyEnvChip_.setTooltip (fenvUnused
            ? "This patch was made before the filter envelope existed: the amp envelope moves the cutoff. "
              "Click to copy that onto FILTER ENV (same sound) and switch the old modulation off."
            : "This patch also uses the old amp-envelope cutoff modulation. Click to switch it off "
              "(the FILTER ENV settings stay).");
    }
    if (show != legacyEnvChip_.isVisible() || ! juce::exactlyEqual (amt, legacyEnvShown_))
    {
        legacyEnvShown_ = amt;
        legacyEnvChip_.setVisible (show);
        resized();
    }
}

MainView::PerfLayout MainView::perfLayout() const
{
    PerfLayout l;
    l.card = getLocalBounds().reduced (16).removeFromBottom (kPerfH);
    auto inner = l.card.reduced (10, 6);
    const int rowH = (inner.getHeight() - 4) / 2;
    for (int row = 0; row < 2; ++row)
    {
        auto band = inner.removeFromTop (rowH);
        if (row == 0)
            inner.removeFromTop (4);
        l.rowTitle[row] = band.removeFromTop (16);
        l.rowCells[row] = band;
    }
    l.cellW = inner.getWidth() / 10;
    return l;
}

void MainView::styleKnob (juce::Slider& s, juce::Label& label, const juce::String& name,
                          const juce::String& tooltip, bool bipolar, bool brass)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 15);
    s.setColour (juce::Slider::rotarySliderFillColourId, brass ? Palette::brass() : Palette::fairway());
    if (bipolar)
        s.getProperties().set ("bipolar", true);
    if (tooltip.isNotEmpty())
        s.setTooltip (tooltip);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, Palette::border());
    s.setColour (juce::Slider::textBoxTextColourId, Palette::text());
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (s);
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (10.0f)));
    label.setColour (juce::Label::textColourId, Palette::muted());
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
}

void MainView::syncZoneKeyboard()
{
    if (processor_.hasUserInstrument())
    {
        zoneKeyboard_.setZones (processor_.getZoneKeySpans());
        if (! zoneKeyboard_.isDragging())   // never rescale the strip under the pointer
            zoneKeyboard_.fitKeyRangeToRoots();
        zoneKeyboard_.setSelection (selection_);
    }
    else
        zoneKeyboard_.clearZones();
}

juce::String MainView::noteName (int midi) const
{
    return juce::String (midiToNoteName (midi, processor_.sessionPrefs().middleCIsC4));
}

std::array<int, 5> MainView::zoneColumns (int width)
{
    // name | root | keys | vel | rr  (x offsets); name takes the slack
    const int rr = width - 34;
    const int vel = rr - 58;
    const int keys = vel - 84;
    const int root = keys - 46;
    return { 8, root, keys, vel, rr };
}

void MainView::ZoneListModel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (rows == nullptr || ! juce::isPositiveAndBelow (row, (int) rows->size()))
        return;
    const auto& r = (*rows)[(size_t) row];
    if (selected)
        g.fillAll (Palette::fairwayDim().withAlpha (0.45f));
    else if (row % 2 == 0)
        g.fillAll (Palette::bgRaised().withAlpha (0.35f));
    const auto cols = zoneColumns (w);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    int nameX = cols[0];
    if (r.edited)
    {
        g.setColour (Palette::sand());
        g.fillEllipse ((float) nameX, (float) h * 0.5f - 3.0f, 6.0f, 6.0f);
    }
    nameX += 10;
    g.setColour (r.missing ? Palette::danger() : Palette::text());
    const int nameRight = cols[1] - (r.overlap ? 18 : 6);
    g.drawText ((r.missing ? "[missing] " : "") + r.name, nameX, 0, nameRight - nameX, h,
                juce::Justification::centredLeft, true);
    if (r.overlap)
    {
        g.setColour (Palette::sand());
        g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
        g.drawText ("!", cols[1] - 16, 0, 10, h, juce::Justification::centred, false);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
    }
    g.setColour (Palette::text());
    g.drawText (r.root, cols[1], 0, cols[2] - cols[1] - 4, h, juce::Justification::centredLeft, false);
    g.setColour (Palette::muted());
    g.drawText (r.keys, cols[2], 0, cols[3] - cols[2] - 4, h, juce::Justification::centredLeft, true);
    g.drawText (r.vel, cols[3], 0, cols[4] - cols[3] - 4, h, juce::Justification::centredLeft, false);
    g.drawText (r.rr, cols[4], 0, w - cols[4] - 4, h, juce::Justification::centredLeft, false);
}

void MainView::rebuildZoneRows()
{
    zoneRows_.clear();
    if (! processor_.hasUserInstrument())
        return;
    const auto map = processor_.copyInstrumentMap();
    const auto& offline = processor_.offlineSampleIds();
    for (size_t i = 0; i < map.zones.size(); ++i)
    {
        const auto& z = map.zones[i];
        ZoneRow row;
        row.zoneIndex = (int) i;
        const auto* ref = processor_.findSampleRef (z.sampleId);
        const std::string& label = ref != nullptr ? (ref->displayName.empty() ? ref->path : ref->displayName) : z.sampleId;
        row.name = glyph::utf8 (label.c_str());
        row.root = noteName (z.rootKey);
        row.keys = z.keyLow == z.keyHigh ? noteName (z.keyLow) : noteName (z.keyLow) + glyph::enDash() + noteName (z.keyHigh);
        row.vel = juce::String (z.velLow) + glyph::enDash() + juce::String (z.velHigh);
        row.rr = z.rrGroup > 0 ? "#" + juce::String (z.rrIndex) : juce::String ("-");
        row.missing = std::find (offline.begin(), offline.end(), z.sampleId) != offline.end();
        row.edited = isZoneEditedFromAuto (z);
        row.overlap = ! findOverlaps (map, i).empty();
        zoneRows_.push_back (std::move (row));
    }
    // Keyboard order: low key, velocity layer, RR alternate, then map order
    std::stable_sort (zoneRows_.begin(), zoneRows_.end(), [&map] (const ZoneRow& a, const ZoneRow& b) {
        const auto& za = map.zones[(size_t) a.zoneIndex];
        const auto& zb = map.zones[(size_t) b.zoneIndex];
        if (za.keyLow != zb.keyLow) return za.keyLow < zb.keyLow;
        if (za.velLow != zb.velLow) return za.velLow < zb.velLow;
        return za.rrIndex < zb.rrIndex;
    });
}

void MainView::selectZone (int zoneIndex)
{
    ZoneSelection next;
    if (zoneIndex >= 0)
        next.selectOnly ((size_t) zoneIndex);
    setSelection (next);
}

void MainView::setSelection (const ZoneSelection& selection)
{
    selection_ = selection;
    applySelection();
}

std::vector<size_t> MainView::displayOrder() const
{
    std::vector<size_t> order;
    order.reserve (zoneRows_.size());
    for (const auto& r : zoneRows_)
        order.push_back ((size_t) r.zoneIndex);
    return order;
}

void MainView::applySelection()
{
    const auto map = processor_.copyInstrumentMap();
    if (! processor_.hasUserInstrument())
        selection_.clear();
    selection_.prune (map.zones.size());
    const int primary = selection_.primary();
    selectedSampleId_ = primary >= 0 ? map.zones[(size_t) primary].sampleId : std::string();
    zoneKeyboard_.setSelection (selection_);
    zoneEditor_.setSelection (selection_);

    const juce::ScopedValueSetter<bool> guard (syncingSelection_, true);
    juce::SparseSet<int> rows;
    int primaryRow = -1;
    for (size_t r = 0; r < zoneRows_.size(); ++r)
    {
        const auto zi = (size_t) zoneRows_[r].zoneIndex;
        if (selection_.contains (zi))
            rows.addRange ({ (int) r, (int) r + 1 });
        if ((int) zi == primary)
            primaryRow = (int) r;
    }
    if (rows.isEmpty())
        sampleList_.deselectAllRows();
    else
    {
        sampleList_.setSelectedRows (rows, juce::dontSendNotification);
        if (primaryRow >= 0)
            sampleList_.scrollToEnsureRowIsOnscreen (primaryRow);
    }
    sampleList_.repaint();
}

void MainView::onListSelectionChanged (int lastRowSelected)
{
    if (syncingSelection_)
        return;
    const auto rows = sampleList_.getSelectedRows();
    std::vector<size_t> indices;
    for (int k = 0; k < rows.size(); ++k)
    {
        const int r = rows[k];
        if (juce::isPositiveAndBelow (r, (int) zoneRows_.size()))
            indices.push_back ((size_t) zoneRows_[(size_t) r].zoneIndex);
    }
    int primary = -1;
    if (juce::isPositiveAndBelow (lastRowSelected, (int) zoneRows_.size()) && rows.contains (lastRowSelected))
        primary = zoneRows_[(size_t) lastRowSelected].zoneIndex;
    else if (selection_.primary() >= 0
             && std::find (indices.begin(), indices.end(), (size_t) selection_.primary()) != indices.end())
        primary = selection_.primary();
    ZoneSelection next;
    next.set (std::move (indices), primary);
    setSelection (next);
}

bool MainView::beginStripDrag (StripPart part, int zoneIndex, int anchorKey)
{
    if (! processor_.hasUserInstrument())
        return false;
    if (zoneIndex >= 0 && ! selection_.contains ((size_t) zoneIndex))
        return false;
    if (zoneIndex >= 0)
    {
        selection_.setPrimary ((size_t) zoneIndex);   // the dragged zone leads; others follow by the same keys
        applySelection();
    }
    if (selection_.empty())
        return false;
    dragPart_ = part;
    dragAnchorKey_ = anchorKey;
    dragOrigin_ = processor_.copyInstrumentMap();
    dragTransactionOpen_ = false;
    return true;
}

juce::String MainView::moveStripDrag (int key)
{
    if (dragPart_ == StripPart::None)
        return {};
    const auto changes = computeStripDrag (dragOrigin_, selection_, dragPart_, dragAnchorKey_, key);
    // First change opens the transaction; the rest of the drag coalesces into it (one undo step)
    if (processor_.performZoneEdits (changes, stripDragActionName (dragPart_, selection_.isMulti()),
                                     ! dragTransactionOpen_))
        dragTransactionOpen_ = true;
    refreshZoneViews();
    const auto map = processor_.copyInstrumentMap();
    const int primary = selection_.primary();
    if (! juce::isPositiveAndBelow (primary, (int) map.zones.size()))
        return {};
    return juce::String (juce::CharPointer_UTF8 (stripDragLabel (dragPart_, map.zones[(size_t) primary],
                                                                  processor_.sessionPrefs().middleCIsC4,
                                                                  selection_.size()).c_str()));
}

void MainView::endStripDrag()
{
    dragPart_ = StripPart::None;
    dragTransactionOpen_ = false;
    dragOrigin_ = {};
    refreshZoneViews();
}

void MainView::updateStatus()
{
    if (! processor_.hasUserInstrument())
    {
        status_.setText ({}, juce::dontSendNotification);
        return;
    }
    juce::String st;
    const auto dot = glyph::dotSep();
    st << processor_.patchName() << dot << processor_.zoneCount() << " zones" << dot
       << processor_.rootCount() << " roots";
    const int rr = processor_.rrDepth();
    if (rr > 0)
        st << dot << "RR x " << rr;
    if (processor_.isPatchDirty())
        st << dot << "unsaved";
    status_.setText (st, juce::dontSendNotification);
}

void MainView::visibilityChanged()
{
    if (isVisible())
    {
        updateMemoryChip();
        startTimerHz (4);
    }
    else
        stopTimer();
}

void MainView::updateMemoryChip()
{
    const bool show = processor_.hasUserInstrument();
    if (show != memoryChip_.isVisible())
    {
        memoryChip_.setVisible (show);
        resized();
    }
    if (! show)
        return;
    const auto st = processor_.memoryStatus();
    const auto d = describeMemory (st.ramBytes, st.streamingSamples, st.underruns);
    const auto tone = d.tone == MemoryTone::Warning   ? MemoryChip::Tone::Warning
                    : d.tone == MemoryTone::Streaming ? MemoryChip::Tone::Streaming
                                                      : MemoryChip::Tone::InRam;
    const auto text = juce::String (d.ram) + glyph::dotSep() + juce::String (d.state);
    const int before = memoryChip_.idealWidth();
    memoryChip_.setStatus (text, tone, st.activeStreams > 0);
    if (memoryChip_.idealWidth() != before)
        resized();

    juce::String tip;
    tip << st.samples << " samples" << glyph::dotSep();
    if (st.loadIntoRam)
        tip << "Load fully into RAM is on for this patch";
    else
        tip << st.streamingSamples << " stream from disk (preload "
            << juce::String (formatPreloadFrames (st.preloadFrames)) << ")";
    tip << "\nSample audio " << juce::String (formatBytes (st.sampleBytes))
        << " of " << juce::String (formatBytes (st.fullBytes)) << " fully loaded";
    if (st.ringBytes > 0)
        tip << glyph::dotSep() << "stream buffers " << juce::String (formatBytes (st.ringBytes));
    tip << "\nStreaming voices now: " << st.activeStreams << glyph::dotSep() << "dropouts: "
        << juce::String ((juce::int64) st.underruns) << "\nClick for memory settings";
    memoryChip_.setTooltip (tip);
}

void MainView::refreshZoneViews()
{
    rebuildZoneRows();
    sampleList_.updateContent();
    sampleList_.repaint();
    syncZoneKeyboard();
    updateStatus();
    applySelection();
}

void MainView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // UndoManager changed (perform / undo / redo / history cleared)
    refreshZoneViews();
}

bool MainView::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (! mods.isCommandDown() || ! processor_.hasUserInstrument())
        return false;
    const auto code = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
    if (code == 'Z')
    {
        if (mods.isShiftDown())
            zoneEditor_.redo();
        else
            zoneEditor_.undo();
        return true;
    }
    if (code == 'Y')
    {
        zoneEditor_.redo();
        return true;
    }
    return false;
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
        updateStatus();
        const auto dot = glyph::dotSep();
        const int rr = processor_.rrDepth();
        const int missing = (int) processor_.offlineSampleIds().size();
        missingBanner_.setVisible (missing > 0);
        missingBanner_.setButtonText (juce::String (missing) + (missing == 1 ? " sample missing" : " samples missing")
                                      + dot + "Relocate" + glyph::ellipsis());
        rebuildZoneRows();
        sampleList_.updateContent();
        zoneEditor_.setVisible (true);

        rrLabel_.setVisible (true);
        rrToggle_.setVisible (true);
        const bool hasRr = rr > 0;
        rrToggle_.setSubdued (! hasRr);
        rrToggle_.setTooltip (juce::String ("Round-robin: Cycle plays alternates in order; Random picks a different "
                                            "one each hit (never the same twice in a row). Saved with the patch, "
                                            "host-automatable.")
                              + (hasRr ? juce::String() : juce::String (" This patch has no round-robin alternates yet.")));
    }
    else
    {
        subtitle_.setText ("Drop samples to build an instrument. Mapping stays secondary.",
                           juce::dontSendNotification);
        dropHint_.setText (glyph::spaced ("Drop a sample folder", glyph::emDash(), "AutoMapper builds the map.\n")
                               + glyph::spaced ("Click to browse", glyph::middleDot(), "WAV / AIFF / FLAC"),
                           juce::dontSendNotification);
        dropHint_.setVisible (true);
        reviewBtn_.setVisible (false);
        addBtn_.setVisible (false);
        openBtn_.setVisible (true);
        saveBtn_.setVisible (false);
        samplesTitle_.setVisible (false);
        sampleList_.setVisible (false);
        status_.setText ({}, juce::dontSendNotification);
        missingBanner_.setVisible (false);
        rrLabel_.setVisible (false);
        rrToggle_.setVisible (false);
        zoneEditor_.setVisible (false);
        zoneRows_.clear();
        sampleList_.updateContent();
    }
    // Keep the selection when the map is unchanged in shape; after an import / patch load only
    // the primary's sample survives (found by sample id).
    if (loaded && ! selectedSampleId_.empty())
    {
        const auto map = processor_.copyInstrumentMap();
        const int primary = selection_.primary();
        const bool same = juce::isPositiveAndBelow (primary, (int) map.zones.size())
                          && map.zones[(size_t) primary].sampleId == selectedSampleId_;
        if (! same)
        {
            int keep = -1;
            for (size_t i = 0; i < map.zones.size() && keep < 0; ++i)
                if (map.zones[i].sampleId == selectedSampleId_)
                    keep = (int) i;
            selection_.clear();
            if (keep >= 0)
                selection_.selectOnly ((size_t) keep);
        }
    }
    else
        selection_.clear();
    syncZoneKeyboard();
    applySelection();
    resized();
    repaint();
    updateMemoryChip();
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

    const int perfH = kPerfH;
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

        if (! listHeaderArea_.isEmpty())
        {
            const auto cols = zoneColumns (listHeaderArea_.getWidth());
            g.setColour (Palette::muted());
            g.setFont (juce::Font (juce::FontOptions (9.5f)));
            const char* captions[] = { "SAMPLE", "ROOT", "KEYS", "VEL", "RR" };
            for (size_t c = 0; c < cols.size(); ++c)
            {
                const int x0 = listHeaderArea_.getX() + cols[c] + (c == 0 ? 10 : 0);
                const int x1 = c + 1 < cols.size() ? listHeaderArea_.getX() + cols[c + 1] : listHeaderArea_.getRight();
                g.drawText (captions[c], x0, listHeaderArea_.getY(), x1 - x0, listHeaderArea_.getHeight(),
                            juce::Justification::centredLeft, false);
            }
        }
    }

    // Sound deck card: AMP | FILTER over VELOCITY | FILTER ENV | BEND
    const auto pl = perfLayout();
    const auto perf = pl.card.toFloat();
    g.setColour (Palette::bgRaised());
    g.fillRoundedRectangle (perf, 12.0f);
    g.setColour (Palette::border());
    g.drawRoundedRectangle (perf, 12.0f, 1.0f);

    // Hairline between the rows (brass tick at the left, like a yardage marker)
    const float rowSplit = (float) pl.rowTitle[1].getY() - 3.0f;
    g.setColour (Palette::border());
    g.drawLine (perf.getX() + 12.0f, rowSplit, perf.getRight() - 12.0f, rowSplit, 1.0f);
    g.setColour (Palette::brass().withAlpha (0.6f));
    g.fillRect (perf.getX() + 12.0f, rowSplit - 0.5f, 28.0f, 1.0f);

    // Section dividers: row 1 after AMP (5 cells); row 2 after VELOCITY (3) and FILTER ENV (8)
    auto divider = [&] (int row, int afterCells) {
        const float x = (float) pl.rowCells[row].getX() + (float) (afterCells * pl.cellW);
        g.drawLine (x, (float) pl.rowTitle[row].getY() + 2.0f, x, (float) pl.rowCells[row].getBottom() - 4.0f, 1.0f);
    };
    g.setColour (Palette::border());
    divider (0, 5);
    divider (1, 3);
    divider (1, 8);
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
    if (memoryChip_.isVisible())
    {
        const int w = juce::jlimit (120, 260, memoryChip_.idealWidth());
        memoryChip_.setBounds (settingsBtn_.getX() - 8 - w, 20, w, 24);
        subtitle_.setBounds (subtitle_.getBounds().withRight (juce::jmin (subtitle_.getRight(), memoryChip_.getX() - 8)));
    }
    else
        memoryChip_.setBounds ({});

    const int perfH = kPerfH;
    {
        const auto pl = perfLayout();
        auto title = [&] (juce::Label& lbl, int row, int cell, int span) {
            const auto& t = pl.rowTitle[row];
            lbl.setBounds (t.getX() + cell * pl.cellW + 6, t.getY(), span * pl.cellW - 12, t.getHeight());
        };
        title (ampTitle_, 0, 0, 5);
        title (filterTitle_, 0, 5, 2);
        title (velTitle_, 1, 0, 3);
        title (fenvTitle_, 1, 3, 5);
        title (bendTitle_, 1, 8, 2);
        if (legacyEnvChip_.isVisible())
        {
            const auto& t = pl.rowTitle[0];
            const int w = juce::jmin (3 * pl.cellW + 40,
                                      legacyEnvChip_.getBestWidthForHeight (15) + 24);
            // Inside the caption strip only, so it never touches the knob labels below.
            legacyEnvChip_.setBounds (t.getRight() - w, t.getY(), w, t.getHeight() - 2);
        }
        else
            legacyEnvChip_.setBounds ({});

        auto place = [&] (juce::Slider& s, juce::Label& lbl, int row, int index) {
            auto cell = pl.cell (row, index).reduced (3, 0);
            lbl.setBounds (cell.removeFromTop (12));
            s.setBounds (cell);
        };
        juce::Slider* row0[] = { &vol_, &atk_, &dec_, &sus_, &rel_ };
        juce::Label* row0L[] = { &volL_, &atkL_, &decL_, &susL_, &relL_ };
        for (int i = 0; i < 5; ++i)
            place (*row0[i], *row0L[i], 0, i);
        {
            auto typeCell = pl.cell (0, 5, 2).reduced (10, 0);
            filterTypeL_.setBounds (typeCell.removeFromTop (12));
            filterType_.setBounds (typeCell.withSizeKeepingCentre (typeCell.getWidth(), 24).translated (0, -6));
        }
        place (cut_, cutL_, 0, 7);
        place (res_, resL_, 0, 8);
        place (key_, keyL_, 0, 9);

        juce::Slider* row1[] = { &velAmp_, &velCut_, &velAtk_, &fAtk_, &fDec_, &fSus_, &fRel_, &fAmt_, &bendUp_, &bendDown_ };
        juce::Label* row1L[] = { &velAmpL_, &velCutL_, &velAtkL_, &fAtkL_, &fDecL_, &fSusL_, &fRelL_, &fAmtL_, &bendUpL_, &bendDownL_ };
        for (int i = 0; i < 10; ++i)
            place (*row1[i], *row1L[i], 1, i);
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
        missingBanner_.setBounds ({});
        status_.setBounds ({});
        samplesTitle_.setBounds ({});
        sampleList_.setBounds ({});
        rrLabel_.setBounds ({});
        rrToggle_.setBounds ({});
        zoneEditor_.setBounds ({});
        listHeaderArea_ = {};
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
        bar.removeFromLeft (8);
        if (missingBanner_.isVisible())
        {
            missingBanner_.setBounds (bar.removeFromLeft (220));
            bar.removeFromLeft (8);
        }
        else
            missingBanner_.setBounds ({});
        status_.setBounds (bar);
        mid.removeFromTop (8);
        zoneKeyboard_.setBounds (mid.removeFromTop (88).reduced (8));
        // SAMPLES card header: title left, round-robin segmented control right
        // Left: ZONES list (+ RR mode); right: zone editor
        auto card = mid.reduced (0, 4);
        auto editorArea = card.removeFromRight (juce::jmax (420, card.getWidth() * 54 / 100));
        zoneEditor_.setBounds (editorArea.reduced (0, 4));
        auto samplesHeader = card.removeFromTop (30).withTrimmedTop (6).reduced (12, 1);
        rrToggle_.setBounds (samplesHeader.removeFromRight (130));
        samplesHeader.removeFromRight (6);
        rrLabel_.setBounds (samplesHeader.removeFromRight (82));
        samplesTitle_.setBounds (samplesHeader);
        auto listArea = card.reduced (10, 4);
        listHeaderArea_ = listArea.removeFromTop (14);
        sampleList_.setBounds (listArea);
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
        // Patch loaded anyway (missing zones stay silent) - go straight to Relocate.
        if (! missing.isEmpty() && onRelocate_)
            onRelocate_();
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
