#include "ZoneEditorPanel.h"
#include "../Plugin/PluginProcessor.h"
#include "../AutoMapper/FilenameTokens.h"
#include "Glyphs.h"

#include <cmath>

namespace looper {

namespace {

juce::String shortcutText (bool redo)
{
   #if JUCE_MAC
    return redo ? "Shift+Cmd+Z" : "Cmd+Z";
   #else
    return redo ? "Shift+Ctrl+Z / Ctrl+Y" : "Ctrl+Z";
   #endif
}

juce::String signedText (double v, int decimals, const juce::String& unit)
{
    const double scale = std::pow (10.0, decimals);
    const auto steps = (long long) std::llround (v * scale);   // integer: no float == compare
    if (steps == 0)
        return juce::String (0.0, decimals) + " " + unit;
    return (steps > 0 ? "+" : "") + juce::String ((double) steps / scale, decimals) + " " + unit;
}

bool isNoteField (ZoneField f)
{
    return f == ZoneField::RootKey || f == ZoneField::KeyLow || f == ZoneField::KeyHigh;
}

bool isFractional (ZoneField f)
{
    return f == ZoneField::TuneCents || f == ZoneField::GainDb;
}

} // namespace

ZoneEditorPanel::ZoneEditorPanel (LooperAudioProcessor& processor) : processor_ (processor)
{
    title_.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    title_.setColour (juce::Label::textColourId, Palette::text());
    title_.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (title_);

    hint_.setText ("Select a zone on the keyboard strip or in the list to edit it.\n"
                   "Drag its edges, root dot or body on the strip; Cmd/Ctrl or Shift-click to select several.",
                   juce::dontSendNotification);
    hint_.setJustificationType (juce::Justification::centred);
    hint_.setColour (juce::Label::textColourId, Palette::muted());
    hint_.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (hint_);

    detectInfo_.setFont (juce::Font (juce::FontOptions (11.5f)));
    detectInfo_.setColour (juce::Label::textColourId, Palette::muted());
    detectInfo_.setMinimumHorizontalScale (0.75f);
    addAndMakeVisible (detectInfo_);

    warning_.setFont (juce::Font (juce::FontOptions (11.5f)));
    warning_.setColour (juce::Label::textColourId, Palette::sand());
    warning_.setMinimumHorizontalScale (0.75f);
    addAndMakeVisible (warning_);

    shiftCaption_.setText ("SHIFT KEYS", juce::dontSendNotification);
    shiftCaption_.setFont (juce::Font (juce::FontOptions (10.0f)));
    shiftCaption_.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (shiftCaption_);

    const std::array<std::pair<ZoneField, const char*>, 9> layout { {
        { ZoneField::RootKey,   "ROOT KEY" },
        { ZoneField::TuneCents, "FINE TUNE" },
        { ZoneField::GainDb,    "GAIN" },
        { ZoneField::KeyLow,    "LOW KEY" },
        { ZoneField::KeyHigh,   "HIGH KEY" },
        { ZoneField::RrGroup,   "RR GROUP" },
        { ZoneField::VelLow,    "LOW VEL" },
        { ZoneField::VelHigh,   "HIGH VEL" },
        { ZoneField::RrIndex,   "RR ALT" },
    } };
    for (size_t i = 0; i < layout.size(); ++i)
    {
        fields_[i] = std::make_unique<Field>();
        setupField (*fields_[i], layout[i].first, layout[i].second);
    }

    auto styleButton = [this] (juce::TextButton& b, bool brass) {
        b.setColour (juce::TextButton::buttonColourId, brass ? Palette::brass().withAlpha (0.25f) : Palette::bgSunken());
        b.setColour (juce::TextButton::textColourOffId, Palette::text());
        addAndMakeVisible (b);
    };
    styleButton (useDetectedBtn_, true);
    styleButton (resetBtn_, false);
    styleButton (auditionBtn_, false);
    styleButton (undoBtn_, false);
    styleButton (redoBtn_, false);

    const std::array<int, 4> shifts { -12, -1, 1, 12 };
    for (size_t i = 0; i < shiftBtns_.size(); ++i)
    {
        auto& b = shiftBtns_[i];
        const int n = shifts[i];
        b.setButtonText ((n > 0 ? "+" : "") + juce::String (n));
        b.setTooltip ("Move the selected zone(s) " + juce::String (std::abs (n)) + (std::abs (n) == 1 ? " semitone " : " semitones ")
                      + (n > 0 ? "up" : "down") + ": key range and root together, widths kept.");
        styleButton (b, false);
        b.onClick = [this, n] { shiftSelectedKeys (n); };
    }

    useDetectedBtn_.onClick = [this] {
        const auto map = processor_.copyInstrumentMap();
        auto changes = mapSelected (map, selection_.indices(), [this] (const Zone& zone) {
            if (const auto* ref = processor_.findSampleRef (zone.sampleId))
                if (const auto p = detectedPitchFor (*ref))
                    return withDetectedPitch (zone, *p);
            return zone;
        });
        applyChanges (changes, "Use detected pitch", true);
    };
    resetBtn_.onClick = [this] {
        const auto map = processor_.copyInstrumentMap();
        applyChanges (mapSelected (map, selection_.indices(), [] (const Zone& z) { return resetZoneToAuto (z); }),
                      "Reset to auto", true);
    };
    auditionBtn_.onStateChange = [this] {
        const bool down = auditionBtn_.isDown();
        if (down != auditionDown_)
            processor_.auditionZone (selection_.primary(), down);
        auditionDown_ = down;
    };
    undoBtn_.onClick = [this] { undo(); };
    redoBtn_.onClick = [this] { redo(); };

    refresh();
}

ZoneEditorPanel::~ZoneEditorPanel()
{
    processor_.auditionZone (-1, false);
}

juce::String ZoneEditorPanel::valueText (ZoneField field, double v) const
{
    switch (field)
    {
        case ZoneField::RootKey:
        case ZoneField::KeyLow:
        case ZoneField::KeyHigh:   return noteText ((int) std::lround (v));
        case ZoneField::VelLow:
        case ZoneField::VelHigh:
        case ZoneField::RrIndex:   return juce::String ((int) std::lround (v));
        case ZoneField::TuneCents: return signedText (v, 1, "ct");
        case ZoneField::GainDb:    return signedText (v, 1, "dB");
        case ZoneField::RrGroup:   { const int g = (int) std::lround (v); return g == 0 ? juce::String ("off") : juce::String (g); }
    }
    return juce::String (v);
}

juce::String ZoneEditorPanel::offsetText (ZoneField field, double delta) const
{
    if (isNoteField (field))
    {
        const int d = (int) std::lround (delta);
        return (d > 0 ? "+" : "") + juce::String (d) + " st" + glyph::dotSep() + "each zone";
    }
    return signedText (delta, 1, field == ZoneField::GainDb ? "dB" : "ct") + glyph::dotSep() + "each zone";
}

double ZoneEditorPanel::parseValue (ZoneField field, const juce::String& text) const
{
    const auto t = text.trim();
    switch (field)
    {
        case ZoneField::RootKey:
        case ZoneField::KeyLow:
        case ZoneField::KeyHigh:
        {
            const auto noteToken = t.upToFirstOccurrenceOf (" ", false, false);
            if (auto midi = noteNameToMidi (noteToken.toStdString(), middleCIsC4()))
                return (double) *midi;
            return t.getDoubleValue();
        }
        case ZoneField::TuneCents:
        case ZoneField::GainDb:
            return t.retainCharacters ("+-.0123456789").getDoubleValue();
        case ZoneField::RrGroup:
            return t.equalsIgnoreCase ("off") ? 0.0 : t.getDoubleValue();
        case ZoneField::VelLow:
        case ZoneField::VelHigh:
        case ZoneField::RrIndex:
            return t.getDoubleValue();
    }
    return t.getDoubleValue();
}

void ZoneEditorPanel::setupField (Field& f, ZoneField field, const juce::String& caption)
{
    f.field = field;
    f.caption.setText (caption, juce::dontSendNotification);
    f.caption.setFont (juce::Font (juce::FontOptions (10.0f)));
    f.caption.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (f.caption);

    auto& s = f.slider;
    double lo = 0.0, hi = 0.0;
    zoneFieldRange (field, lo, hi);
    s.setSliderStyle (juce::Slider::LinearBar);
    s.setRange (lo, hi, isFractional (field) ? 0.1 : 1.0);
    s.setSliderSnapsToMousePosition (false);   // relative drags: a click never jumps the value
    s.setTextBoxIsEditable (true);
    s.setScrollWheelEnabled (true);
    s.setColour (juce::Slider::backgroundColourId, Palette::bgSunken());
    s.setColour (juce::Slider::trackColourId, Palette::fairwayDim().withAlpha (0.55f));
    s.setColour (juce::Slider::textBoxTextColourId, Palette::text());
    s.setColour (juce::Slider::textBoxOutlineColourId, Palette::border());
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxHighlightColourId, Palette::fairway().withAlpha (0.4f));
    s.setTitle (juce::String (zoneFieldName (field)));

    s.textFromValueFunction = [this, &f] (double v) {
        if (isMulti() && dragField_ == &f && fieldEditsRelative (f.field))
            return offsetText (f.field, v - dragStartValue_);
        if (isMulti() && f.mixed)
        {
            // Compact range: note names without MIDI numbers so it fits the field
            const auto shortText = [this, &f] (double x) {
                return isNoteField (f.field) ? juce::String (midiToNoteName ((int) std::lround (x), middleCIsC4()))
                                             : valueText (f.field, x);
            };
            return "mixed  " + shortText (f.minValue) + glyph::enDash() + shortText (f.maxValue);
        }
        return valueText (f.field, v);
    };
    s.valueFromTextFunction = [this, &f] (const juce::String& text) {
        if (! isMulti())
            return parseValue (f.field, text);
        const auto c = classifyMultiEditText (text.toStdString());
        TypedEdit t;
        double shown = 0.0;
        if (c.relative && fieldEditsRelative (f.field))
        {
            const auto body = juce::String (c.body);
            t.relative = true;
            t.value = isFractional (f.field) ? body.retainCharacters ("+-.0123456789").getDoubleValue() : body.getDoubleValue();
            shown = f.lastValue + t.value;
        }
        else
        {
            t.value = parseValue (f.field, juce::String (c.body));
            shown = t.value;
        }
        typed_ = t;
        shown = juce::jlimit (f.slider.getMinimum(), f.slider.getMaximum(), shown);
        if (std::abs (shown - f.slider.getValue()) < 1.0e-9)
        {
            // The slider will not report a change (e.g. "=-3" when the primary already is -3):
            // apply the typed edit to the rest of the selection ourselves.
            juce::Component::SafePointer<ZoneEditorPanel> safe (this);
            juce::MessageManager::callAsync ([safe, &f] { if (safe != nullptr && safe->typed_) safe->applyField (f); });
        }
        return shown;
    };

    switch (field)
    {
        case ZoneField::RootKey:
            s.setTooltip ("Note at which the sample plays back unpitched. Drag, scroll or type a note (E3) or MIDI number. "
                          "Several zones: drag offsets each; type +12 / -12 to offset, a note to set all.");
            break;
        case ZoneField::KeyLow:
        case ZoneField::KeyHigh:
            s.setTooltip ("Key range edge (or drag the zone edge on the keyboard strip). Ranges never invert: moving one "
                          "edge past the other drags it along.");
            break;
        case ZoneField::VelLow:
        case ZoneField::VelHigh:
            s.setTooltip ("Velocity layer edge (1-127). Several zones: sets every selected zone.");
            break;
        case ZoneField::TuneCents:
            s.setTooltip ("Fine tune in cents (" + glyph::plusMinus() + "100). Several zones: drag or +5 / -5 offsets each, "
                          "a plain value (or =-5) sets all.");
            break;
        case ZoneField::GainDb:
            s.setTooltip ("Zone gain in dB (-48 to +24); glides on sounding notes. Several zones: drag or +2 / -2 offsets "
                          "each, a plain value (or =-6) sets all.");
            break;
        case ZoneField::RrGroup:
            s.setTooltip ("Round-robin group. Zones sharing a group and key/velocity range alternate. 0 = off.");
            break;
        case ZoneField::RrIndex:
            s.setTooltip ("Order of this alternate within its round-robin group (Cycle mode).");
            break;
    }

    s.onDragStart = [this, &f] {
        dragField_ = &f;
        dragOrigin_ = processor_.copyInstrumentMap();
        dragStartValue_ = f.slider.getValue();
        dragTransactionOpen_ = false;
    };
    s.onDragEnd = [this] {
        dragField_ = nullptr;
        dragTransactionOpen_ = false;
        refresh();
    };
    s.onValueChange = [this, &f] {
        if (! refreshing_)
            applyField (f);
    };
    addAndMakeVisible (s);
}

bool ZoneEditorPanel::middleCIsC4() const
{
    return processor_.sessionPrefs().middleCIsC4;
}

juce::String ZoneEditorPanel::noteText (int midi) const
{
    return juce::String (midiToNoteName (midi, middleCIsC4())) + "  (" + juce::String (midi) + ")";
}

juce::String ZoneEditorPanel::zoneName (const Zone& zone) const
{
    const auto* ref = processor_.findSampleRef (zone.sampleId);
    return ref != nullptr && ! ref->displayName.empty() ? glyph::utf8 (ref->displayName.c_str())
                                                        : glyph::utf8 (zone.sampleId.c_str());
}

void ZoneEditorPanel::setSelection (const ZoneSelection& selection)
{
    if (selection.primary() != selection_.primary())
        processor_.auditionZone (-1, false);
    selection_ = selection;
    refresh();
}

void ZoneEditorPanel::setSelectedZone (int zoneIndex)
{
    ZoneSelection s;
    if (zoneIndex >= 0)
        s.selectOnly ((size_t) zoneIndex);
    setSelection (s);
}

void ZoneEditorPanel::applyField (Field& f)
{
    if (refreshing_ || selection_.empty())
        return;
    const bool dragging = dragField_ == &f;
    const InstrumentMap origin = dragging ? dragOrigin_ : processor_.copyInstrumentMap();
    const double ref = dragging ? dragStartValue_ : f.lastValue;
    const double v = f.slider.getValue();
    const auto& sel = selection_.indices();

    std::vector<ZoneChange> changes;
    if (typed_)
    {
        changes = typed_->relative ? offsetField (origin, sel, f.field, typed_->value)
                                   : setFieldAll (origin, sel, f.field, typed_->value);
        typed_.reset();
    }
    else if (isMulti() && fieldEditsRelative (f.field))
        changes = offsetField (origin, sel, f.field, v - ref);   // each zone keeps its own spread
    else
        changes = setFieldAll (origin, sel, f.field, v);

    const bool newTransaction = ! dragging || ! dragTransactionOpen_;
    if (applyChanges (changes, juce::String (zoneFieldName (f.field)), newTransaction) && dragging)
        dragTransactionOpen_ = true;
}

bool ZoneEditorPanel::applyChanges (const std::vector<ZoneChange>& changes, const juce::String& actionName,
                                    bool newTransaction)
{
    const bool ok = ! changes.empty()
                    && processor_.performZoneEdits (changes, actionName, newTransaction);
    refresh(); // shows clamped values and partner bounds that were pushed along
    if (onEdited)
        onEdited();
    return ok;
}

void ZoneEditorPanel::shiftSelectedKeys (int semis)
{
    if (selection_.empty())
        return;
    const auto map = processor_.copyInstrumentMap();
    applyChanges (shiftKeys (map, selection_.indices(), semis), "Shift keys", true);
}

void ZoneEditorPanel::undo()
{
    auto& um = processor_.undoManager();
    if (um.canUndo())
        um.undo();
    refresh();
    if (onEdited)
        onEdited();
}

void ZoneEditorPanel::redo()
{
    auto& um = processor_.undoManager();
    if (um.canRedo())
        um.redo();
    refresh();
    if (onEdited)
        onEdited();
}

void ZoneEditorPanel::updateUndoButtons()
{
    auto& um = processor_.undoManager();
    undoBtn_.setEnabled (um.canUndo());
    redoBtn_.setEnabled (um.canRedo());
    const auto undoName = um.getUndoDescription();
    const auto redoName = um.getRedoDescription();
    undoBtn_.setTooltip ((um.canUndo() ? "Undo " + undoName : juce::String ("Nothing to undo")) + " (" + shortcutText (false) + ")");
    redoBtn_.setTooltip ((um.canRedo() ? "Redo " + redoName : juce::String ("Nothing to redo")) + " (" + shortcutText (true) + ")");
}

void ZoneEditorPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard (refreshing_, true);
    const auto map = processor_.copyInstrumentMap();
    if (! processor_.hasUserInstrument())
        selection_.clear();
    selection_.prune (map.zones.size());
    const bool has = ! selection_.empty() && selection_.primary() >= 0;

    hint_.setVisible (! has);
    for (auto& f : fields_)
    {
        f->caption.setVisible (has);
        f->slider.setVisible (has);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &title_, &detectInfo_, &warning_, &useDetectedBtn_,
                                                             &resetBtn_, &auditionBtn_, &shiftCaption_ })
        c->setVisible (has);
    for (auto& b : shiftBtns_)
        b.setVisible (has);
    updateUndoButtons();

    if (! has)
    {
        repaint();
        return;
    }

    const auto& sel = selection_.indices();
    const size_t primaryIndex = (size_t) selection_.primary();
    const Zone& zone = map.zones[primaryIndex];
    const bool multi = sel.size() > 1;

    int editedCount = 0;
    for (size_t i : sel)
        if (isZoneEditedFromAuto (map.zones[i]))
            ++editedCount;

    if (multi)
    {
        juce::String t;
        t << (int) sel.size() << " zones selected";
        if (editedCount > 0)
            t << glyph::dotSep() << editedCount << " edited";
        title_.setText (t, juce::dontSendNotification);
        title_.setColour (juce::Label::textColourId, Palette::fairway());
    }
    else
    {
        const bool edited = editedCount > 0;
        title_.setText (zoneName (zone) + (edited ? glyph::dotSep() + "edited" : juce::String()), juce::dontSendNotification);
        title_.setColour (juce::Label::textColourId, edited ? Palette::sand() : Palette::text());
    }

    for (auto& f : fields_)
    {
        const auto s = summarizeField (map, sel, f->field, selection_.primary());
        f->mixed = s.mixed;
        f->minValue = s.minValue;
        f->maxValue = s.maxValue;
        f->lastValue = s.first;
        f->slider.setValue (s.first, juce::dontSendNotification);
        f->slider.setColour (juce::Slider::textBoxTextColourId, s.mixed ? Palette::sand() : Palette::text());
        f->slider.updateText(); // note labels follow the C4/C3 = 60 setting; "mixed" for differing values
    }

    // Detected pitch line + "Use detected pitch"
    if (multi)
    {
        int withPitch = 0, notInUse = 0;
        for (size_t i : sel)
            if (const auto* ref = processor_.findSampleRef (map.zones[i].sampleId))
                if (const auto p = detectedPitchFor (*ref))
                {
                    ++withPitch;
                    if (! zoneUsesDetectedPitch (map.zones[i], *p))
                        ++notInUse;
                }
        detectInfo_.setText ("Drag/scroll offsets each zone" + glyph::dotSep() + "type +3 / -3 to offset, a value to set all",
                             juce::dontSendNotification);
        useDetectedBtn_.setEnabled (notInUse > 0);
        useDetectedBtn_.setTooltip (withPitch == 0 ? juce::String ("None of the selected samples has a detected pitch.")
                                    : notInUse == 0 ? juce::String ("Every selected zone already uses its detected pitch.")
                                                    : "Apply the detected root and tuning to " + juce::String (notInUse)
                                                          + (notInUse == 1 ? " zone." : " zones."));
    }
    else
    {
        const auto* ref = processor_.findSampleRef (zone.sampleId);
        std::optional<DetectedPitchInfo> detected;
        if (ref != nullptr)
            detected = detectedPitchFor (*ref);
        if (detected)
        {
            const bool inUse = zoneUsesDetectedPitch (zone, *detected);
            juce::String info;
            info << "Detected " << midiToNoteName (detected->rootKey, middleCIsC4()) << " ("
                 << signedText (detected->cents, 0, "ct") << ")" << glyph::dotSep()
                 << juce::roundToInt (detected->confidence * 100.0f) << "% confidence";
            if (inUse)
                info << glyph::dotSep() << "in use";
            detectInfo_.setText (info, juce::dontSendNotification);
            useDetectedBtn_.setEnabled (! inUse);
            useDetectedBtn_.setTooltip (inUse ? juce::String ("This zone already uses the detected root and tuning.")
                                              : "Set root to " + juce::String (midiToNoteName (detected->rootKey, middleCIsC4()))
                                                    + " and fine tune to " + signedText (-detected->cents, 1, "ct") + ".");
        }
        else
        {
            const bool unpitched = ref != nullptr && ref->pitchSource && *ref->pitchSource == PitchSource::Unpitched;
            detectInfo_.setText (unpitched ? "No clear pitch detected (unpitched sample)"
                                           : "No pitch-detection data for this sample",
                                 juce::dontSendNotification);
            useDetectedBtn_.setEnabled (false);
            useDetectedBtn_.setTooltip ("No detected pitch is stored for this sample.");
        }
    }

    bool anyAuto = false;
    for (size_t i : sel)
        anyAuto = anyAuto || map.zones[i].autoValues.has_value();
    resetBtn_.setEnabled (editedCount > 0);
    resetBtn_.setTooltip (! anyAuto ? juce::String ("No auto-map values stored (patch saved before the zone editor existed).")
                          : editedCount > 0 ? juce::String ("Restore every field to what AutoMapper chose at import.")
                                            : juce::String ("Already matches the auto-map."));
    auditionBtn_.setTooltip ("Hold to hear " + zoneName (zone) + " exactly (root key, ignores overlaps and round-robin)"
                             + (multi ? juce::String (" - the zone you clicked last") : juce::String()));

    // Overlap warning (never blocks)
    if (multi)
    {
        int overlapping = 0;
        for (size_t i : sel)
            if (! findOverlaps (map, i).empty())
                ++overlapping;
        if (overlapping == 0)
            warning_.setText ({}, juce::dontSendNotification);
        else
            warning_.setText ("! " + juce::String (overlapping) + " of " + juce::String ((int) sel.size())
                                  + " selected zones overlap other zones" + glyph::dotSep() + "first match plays",
                              juce::dontSendNotification);
    }
    else
    {
        const auto overlaps = findOverlaps (map, primaryIndex);
        if (overlaps.empty())
            warning_.setText ({}, juce::dontSendNotification);
        else
        {
            juce::StringArray names;
            for (size_t k = 0; k < overlaps.size() && k < 2; ++k)
                names.add (zoneName (map.zones[overlaps[k]]));
            juce::String w;
            w << "! Overlaps " << (int) overlaps.size() << (overlaps.size() == 1 ? " zone: " : " zones: ")
              << names.joinIntoString (", ") << (overlaps.size() > 2 ? glyph::ellipsis() : juce::String())
              << glyph::dotSep() << "first match plays";
            warning_.setText (w, juce::dontSendNotification);
        }
    }
    repaint();
}

void ZoneEditorPanel::paint (juce::Graphics& g)
{
    g.setColour (Palette::border());
    g.drawLine (0.5f, 6.0f, 0.5f, (float) getHeight() - 6.0f, 1.0f);
    if (! selection_.empty() && title_.isVisible())
    {
        g.setColour (Palette::muted());
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (selection_.isMulti() ? "ZONES" : "ZONE", 12, 6, 40, 20, juce::Justification::centredLeft, false);
    }
}

void ZoneEditorPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 4);
    hint_.setBounds (r);

    auto top = r.removeFromTop (22);
    redoBtn_.setBounds (top.removeFromRight (54).reduced (0, 1));
    top.removeFromRight (6);
    undoBtn_.setBounds (top.removeFromRight (54).reduced (0, 1));
    top.removeFromLeft (44); // "ZONE(S)" caption
    title_.setBounds (top);
    r.removeFromTop (4);

    const int colGap = 10;
    const int colW = (r.getWidth() - 2 * colGap) / 3;
    for (int row = 0; row < 3; ++row)
    {
        auto line = r.removeFromTop (37);
        for (int col = 0; col < 3; ++col)
        {
            auto& f = *fields_[(size_t) (row * 3 + col)];
            auto cell = line.withX (line.getX() + col * (colW + colGap)).withWidth (colW);
            f.caption.setBounds (cell.removeFromTop (13));
            f.slider.setBounds (cell.removeFromTop (22));
        }
    }
    r.removeFromTop (2);
    detectInfo_.setBounds (r.removeFromTop (16));
    warning_.setBounds (r.removeFromTop (16));
    r.removeFromTop (4);
    auto buttons = r.removeFromTop (26);
    useDetectedBtn_.setBounds (buttons.removeFromLeft (150));
    buttons.removeFromLeft (8);
    resetBtn_.setBounds (buttons.removeFromLeft (118));
    buttons.removeFromLeft (8);
    auditionBtn_.setBounds (buttons.removeFromLeft (90));

    r.removeFromTop (4);
    auto shiftRow = r.removeFromTop (24);
    shiftCaption_.setBounds (shiftRow.removeFromLeft (74));
    for (auto& b : shiftBtns_)
    {
        b.setBounds (shiftRow.removeFromLeft (40).reduced (0, 1));
        shiftRow.removeFromLeft (6);
    }
}

} // namespace looper
