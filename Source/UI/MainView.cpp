#include "MainView.h"
#include "../Plugin/PluginProcessor.h"
#include "../Import/ImportController.h"
#include "Glyphs.h"
#include "../AutoMapper/FilenameTokens.h"
#include "../ZoneEdit/ZoneEditor.h"
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
