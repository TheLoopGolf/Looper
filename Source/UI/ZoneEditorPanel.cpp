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

} // namespace

ZoneEditorPanel::ZoneEditorPanel (LooperAudioProcessor& processor) : processor_ (processor)
{
    title_.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    title_.setColour (juce::Label::textColourId, Palette::text());
    title_.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (title_);

    hint_.setText ("Select a zone on the keyboard strip or in the list to edit it.\n"
                   "Click a zone's root key (dot) to hear it.",
                   juce::dontSendNotification);
    hint_.setJustificationType (juce::Justification::centred);
    hint_.setColour (juce::Label::textColourId, Palette::muted());
    hint_.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (hint_);

    detectInfo_.setFont (juce::Font (juce::FontOptions (11.5f)));
    detectInfo_.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (detectInfo_);

    warning_.setFont (juce::Font (juce::FontOptions (11.5f)));
    warning_.setColour (juce::Label::textColourId, Palette::sand());
    addAndMakeVisible (warning_);

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

    useDetectedBtn_.onClick = [this] {
        const auto map = processor_.copyInstrumentMap();
        if (! juce::isPositiveAndBelow (selected_, (int) map.zones.size()))
            return;
        const auto& zone = map.zones[(size_t) selected_];
        if (const auto* ref = processor_.findSampleRef (zone.sampleId))
            if (const auto p = detectedPitchFor (*ref))
                applyWholeZone (withDetectedPitch (zone, *p), "Use detected pitch");
    };
    resetBtn_.onClick = [this] {
        const auto map = processor_.copyInstrumentMap();
        if (juce::isPositiveAndBelow (selected_, (int) map.zones.size()))
            applyWholeZone (resetZoneToAuto (map.zones[(size_t) selected_]), "Reset to auto");
    };
    auditionBtn_.setTooltip ("Hold to hear this zone at its root key");
    auditionBtn_.onStateChange = [this] {
        const bool down = auditionBtn_.isDown();
        if (down != auditionDown_)
            processor_.auditionZone (selected_, down);
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
    const bool fractional = field == ZoneField::TuneCents || field == ZoneField::GainDb;
    s.setSliderStyle (juce::Slider::LinearBar);
    s.setRange (lo, hi, fractional ? 0.1 : 1.0);
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

    switch (field)
    {
        case ZoneField::RootKey:
        case ZoneField::KeyLow:
        case ZoneField::KeyHigh:
            s.textFromValueFunction = [this] (double v) { return noteText ((int) std::lround (v)); };
            s.valueFromTextFunction = [this] (const juce::String& text) {
                const auto t = text.trim();
                const auto noteToken = t.upToFirstOccurrenceOf (" ", false, false);
                if (auto midi = noteNameToMidi (noteToken.toStdString(), middleCIsC4()))
                    return (double) *midi;
                return t.getDoubleValue();
            };
            s.setTooltip (field == ZoneField::RootKey
                ? "Note at which the sample plays back unpitched. Drag, scroll or type a note (E3) or MIDI number."
                : "Key range edge. Ranges never invert: moving one edge past the other drags it along.");
            break;
        case ZoneField::VelLow:
        case ZoneField::VelHigh:
            s.textFromValueFunction = [] (double v) { return juce::String ((int) std::lround (v)); };
            s.setTooltip ("Velocity layer edge (1-127).");
            break;
        case ZoneField::TuneCents:
            s.textFromValueFunction = [] (double v) { return signedText (v, 1, "ct"); };
            s.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("+-.0123456789").getDoubleValue(); };
            s.setTooltip ("Fine tune in cents (" + glyph::plusMinus() + "100).");
            break;
        case ZoneField::GainDb:
            s.textFromValueFunction = [] (double v) { return signedText (v, 1, "dB"); };
            s.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("+-.0123456789").getDoubleValue(); };
            s.setTooltip ("Zone gain in dB (-48 to +24).");
            break;
        case ZoneField::RrGroup:
            s.textFromValueFunction = [] (double v) { const int g = (int) std::lround (v); return g == 0 ? juce::String ("off") : juce::String (g); };
            s.valueFromTextFunction = [] (const juce::String& t) { return t.trim().equalsIgnoreCase ("off") ? 0.0 : t.getDoubleValue(); };
            s.setTooltip ("Round-robin group. Zones sharing a group and key/velocity range alternate. 0 = off.");
            break;
        case ZoneField::RrIndex:
            s.textFromValueFunction = [] (double v) { return juce::String ((int) std::lround (v)); };
            s.setTooltip ("Order of this alternate within its round-robin group (Cycle mode).");
            break;
    }

    s.onDragStart = [this, field] {
        dragging_ = true;
        processor_.undoManager().beginNewTransaction (juce::String (zoneFieldName (field)));
    };
    s.onDragEnd = [this] { dragging_ = false; };
    s.onValueChange = [this, &f] {
        if (! refreshing_)
            applyField (f.field, f.slider.getValue());
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

void ZoneEditorPanel::setSelectedZone (int zoneIndex)
{
    if (zoneIndex != selected_)
        processor_.auditionZone (-1, false);
    selected_ = zoneIndex;
    refresh();
}

void ZoneEditorPanel::applyField (ZoneField field, double value)
{
    const auto map = processor_.copyInstrumentMap();
    if (! juce::isPositiveAndBelow (selected_, (int) map.zones.size()))
        return;
    const Zone proposed = withZoneField (map.zones[(size_t) selected_], field, value);
    processor_.performZoneEdit ((size_t) selected_, proposed, juce::String (zoneFieldName (field)), ! dragging_);
    refresh(); // shows clamped values and partner bounds that were pushed along
    if (onEdited)
        onEdited();
}

void ZoneEditorPanel::applyWholeZone (const Zone& proposed, const juce::String& actionName)
{
    if (selected_ < 0)
        return;
    processor_.performZoneEdit ((size_t) selected_, proposed, actionName, true);
    refresh();
    if (onEdited)
        onEdited();
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
    const bool has = processor_.hasUserInstrument()
                     && juce::isPositiveAndBelow (selected_, (int) map.zones.size());

    hint_.setVisible (! has);
    for (auto& f : fields_)
    {
        f->caption.setVisible (has);
        f->slider.setVisible (has);
    }
    title_.setVisible (has);
    detectInfo_.setVisible (has);
    warning_.setVisible (has);
    useDetectedBtn_.setVisible (has);
    resetBtn_.setVisible (has);
    auditionBtn_.setVisible (has);
    updateUndoButtons();

    if (! has)
    {
        if (selected_ >= (int) map.zones.size())
            selected_ = -1;
        repaint();
        return;
    }

    const Zone& zone = map.zones[(size_t) selected_];
    const auto* ref = processor_.findSampleRef (zone.sampleId);
    const juce::String name = ref != nullptr && ! ref->displayName.empty() ? glyph::utf8 (ref->displayName.c_str())
                                                                           : glyph::utf8 (zone.sampleId.c_str());
    const bool edited = isZoneEditedFromAuto (zone);
    title_.setText (name + (edited ? glyph::dotSep() + "edited" : juce::String()), juce::dontSendNotification);
    title_.setColour (juce::Label::textColourId, edited ? Palette::sand() : Palette::text());

    for (auto& f : fields_)
    {
        f->slider.setValue (zoneFieldValue (zone, f->field), juce::dontSendNotification);
        f->slider.updateText(); // note labels follow the C4/C3 = 60 setting
    }

    // Detected pitch line + "Use detected pitch"
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

    resetBtn_.setEnabled (zone.autoValues.has_value() && edited);
    resetBtn_.setTooltip (! zone.autoValues ? juce::String ("No auto-map values stored (patch saved before the zone editor existed).")
                                            : edited ? juce::String ("Restore every field to what AutoMapper chose at import.")
                                                     : juce::String ("Already matches the auto-map."));

    // Overlap warning (never blocks)
    const auto overlaps = findOverlaps (map, (size_t) selected_);
    if (overlaps.empty())
        warning_.setText ({}, juce::dontSendNotification);
    else
    {
        juce::StringArray names;
        for (size_t k = 0; k < overlaps.size() && k < 2; ++k)
        {
            const auto& other = map.zones[overlaps[k]];
            const auto* oref = processor_.findSampleRef (other.sampleId);
            names.add (oref != nullptr && ! oref->displayName.empty() ? glyph::utf8 (oref->displayName.c_str())
                                                                      : glyph::utf8 (other.sampleId.c_str()));
        }
        juce::String w;
        w << "! Overlaps " << (int) overlaps.size() << (overlaps.size() == 1 ? " zone: " : " zones: ")
          << names.joinIntoString (", ") << (overlaps.size() > 2 ? glyph::ellipsis() : juce::String())
          << glyph::dotSep() << "first match plays";
        warning_.setText (w, juce::dontSendNotification);
    }
    repaint();
}

void ZoneEditorPanel::paint (juce::Graphics& g)
{
    g.setColour (Palette::border());
    g.drawLine (0.5f, 6.0f, 0.5f, (float) getHeight() - 6.0f, 1.0f);
    if (selected_ >= 0 && title_.isVisible())
    {
        g.setColour (Palette::muted());
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("ZONE", 12, 6, 40, 20, juce::Justification::centredLeft, false);
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
    top.removeFromLeft (40); // "ZONE" caption
    title_.setBounds (top);
    r.removeFromTop (4);

    const int colGap = 10;
    const int colW = (r.getWidth() - 2 * colGap) / 3;
    for (int row = 0; row < 3; ++row)
    {
        auto line = r.removeFromTop (38);
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
}

} // namespace looper
