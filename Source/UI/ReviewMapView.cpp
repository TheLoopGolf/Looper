#include "ReviewMapView.h"
#include "../AutoMapper/FilenameTokens.h"
#include "Glyphs.h"

namespace looper {

namespace {
juce::String pitchLabel (PitchSource s)
{
    switch (s) {
        case PitchSource::Filename: return "Filename";
        case PitchSource::Detected: return "Detected";
        case PitchSource::Unpitched: return "Unpitched";
    }
    return "?";
}
juce::Colour pitchTagColour (PitchSource s)
{
    switch (s) {
        case PitchSource::Filename: return Palette::fairway();
        case PitchSource::Detected: return Palette::text();
        case PitchSource::Unpitched: return Palette::brass();
    }
    return Palette::muted();
}
const Zone* findZone (const InstrumentMap& map, const std::string& id)
{
    for (const auto& z : map.zones)
        if (z.sampleId == id) return &z;
    return nullptr;
}
} // namespace

/** Compact fairway "Use detected" action living (inset) in the "Fix" column of mismatch rows. */
class ReviewMapView::UseDetectedButton : public juce::Component
{
public:
    explicit UseDetectedButton (ReviewMapView& owner) : owner_ (owner)
    {
        button_.setColour (juce::TextButton::buttonColourId, Palette::fairwayDim().withAlpha (0.55f));
        button_.setColour (juce::TextButton::textColourOffId, Palette::text());
        button_.setTooltip ("Replace the filename note with the detected note for this sample");
        // Deferred: the re-map rebuilds the table, which deletes this very component.
        button_.onClick = [this] {
            juce::Component::SafePointer<ReviewMapView> safe (&owner_);
            juce::MessageManager::callAsync ([safe, id = sampleId] {
                if (safe != nullptr && safe->onUseDetected_) safe->onUseDetected_ (id);
            });
        };
        addAndMakeVisible (button_);
    }
    void resized() override { button_.setBounds (getLocalBounds().reduced (6, 2)); }
    std::string sampleId;

private:
    ReviewMapView& owner_;
    juce::TextButton button_ { "Use detected" };
};

ReviewMapView::ReviewMapView()
{
    setLookAndFeel (&lookAndFeel_);

    brand_.setText ("LOOPER", juce::dontSendNotification);
    brand_.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    brand_.setColour (juce::Label::textColourId, Palette::text());
    addAndMakeVisible (brand_);

    brandSub_.setText ("Loop Audio Lab", juce::dontSendNotification);
    brandSub_.setFont (juce::Font (juce::FontOptions (11.0f)));
    brandSub_.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (brandSub_);

    subtitle_.setText (glyph::spaced ("Review map", glyph::emDash(), "fix auto-map, then return to play."),
                       juce::dontSendNotification);
    subtitle_.setColour (juce::Label::textColourId, Palette::fairway());
    addAndMakeVisible (subtitle_);

    summary_.setColour (juce::Label::textColourId, Palette::text());
    addAndMakeVisible (summary_);

    // Accept = primary fairway fill
    acceptBtn_.setColour (juce::TextButton::buttonColourId, Palette::fairway());
    acceptBtn_.setColour (juce::TextButton::textColourOffId, Palette::bg());
    // Back = ghost
    backBtn_.setColour (juce::TextButton::buttonColourId, Palette::bgRaised());
    backBtn_.setColour (juce::TextButton::textColourOffId, Palette::text());

    acceptBtn_.onClick = [this] { if (onAccept_) onAccept_(); };
    backBtn_.onClick = [this] { if (onBack_) onBack_(); };
    addAndMakeVisible (acceptBtn_);
    addAndMakeVisible (backBtn_);

    auto& header = table_.getHeader();
    header.addColumn ("Sample", colSample, 168);
    header.addColumn ("Source", colSource, 88);
    header.addColumn ("Root", colRoot, 52);
    header.addColumn ("Vel", colVel, 60);
    header.addColumn ("RR", colRr, 34);
    header.addColumn ("Key span", colSpan, 84);
    header.addColumn ("Conf.", colConf, 56);
    header.addColumn ("Pitch", colPitch, 300);
    header.addColumn ("Fix", colAction, 96);
    table_.setRowHeight (26);
    table_.setColour (juce::ListBox::backgroundColourId, Palette::bgSunken());
    table_.setColour (juce::ListBox::outlineColourId, Palette::border());
    addAndMakeVisible (table_);

    footer_.setColour (juce::Label::textColourId, Palette::muted());
    footer_.setText (glyph::spaced ("Filename note wins (audio mismatches are flagged)", glyph::middleDot(),
                                    "else YIN detection") + " " + glyph::middleDot()
                         + " Unpitched: Settings " + glyph::arrowRight() + " Mapping " + glyph::arrowRight()
                         + " No clear pitch (default: own drum key each).",
                     juce::dontSendNotification);
    addAndMakeVisible (footer_);
}

ReviewMapView::~ReviewMapView()
{
    table_.setModel (nullptr);
    setLookAndFeel (nullptr);
}

void ReviewMapView::clear()
{
    rows_.clear();
    summary_.setText ({}, juce::dontSendNotification);
    table_.updateContent();
}

void ReviewMapView::setResult (const AutoMapResult& result, const std::vector<SampleRef>& refs)
{
    rows_.clear();
    const bool c4 = result.middleCIsC4;
    auto note = [c4] (int n) { return juce::String (midiToNoteName (n, c4)); };
    auto nameFor = [&] (const std::string& id) -> juce::String {
        for (const auto& r : refs)
            if (r.id == id)
                return glyph::utf8 ((r.displayName.empty() ? r.path : r.displayName).c_str());
        return glyph::utf8 (id.c_str());
    };
    int warns = 0, unpitched = 0, mismatches = 0;
    int unpitchedLo = 128, unpitchedHi = -1;
    for (const auto& rev : result.reviews)
    {
        Row row;
        row.sampleId = rev.sampleId;
        row.sample = nameFor (rev.sampleId);
        row.kind = rev.source;
        row.source = pitchLabel (rev.source);
        row.mismatch = rev.mismatch.has_value();
        row.canUseDetected = AutoMapper::canUseDetected (rev);
        if (rev.source == PitchSource::Unpitched)
            row.confidence = "-";
        else
            row.confidence = rev.pitch && rev.pitch->analysed ? juce::String (rev.pitch->confidence, 2)
                                                              : juce::String (rev.confidence, 2);
        // AutoMapper text is UTF-8 (arrows, minus) -> decode via glyph::utf8
        if (row.mismatch && rev.source == PitchSource::Filename)
            row.pitch = glyph::utf8 (rev.mismatch->text.c_str()); // detection % is in Conf.
        else
        {
            row.pitch = glyph::utf8 (describePitch (rev, c4).c_str());
            if (! rev.warnings.empty())
                row.pitch << "  " << glyph::emDash() << " " << glyph::utf8 (rev.warnings.front().c_str());
        }
        if (const Zone* z = findZone (result.map, rev.sampleId))
        {
            row.root = note (z->rootKey);
            row.vel = juce::String (z->velLow) + "-" + juce::String (z->velHigh);
            row.rr = z->rrIndex > 0 ? juce::String (z->rrIndex) : "-";
            row.keySpan = z->keyLow == z->keyHigh ? note (z->keyLow)
                                                  : note (z->keyLow) + glyph::enDash() + note (z->keyHigh);
        }
        else
        {
            row.root = "?"; row.vel = "-"; row.rr = "-"; row.keySpan = "-";
        }
        if (rev.source == PitchSource::Unpitched)
        {
            ++unpitched;
            unpitchedLo = juce::jmin (unpitchedLo, rev.rootKey);
            unpitchedHi = juce::jmax (unpitchedHi, rev.rootKey);
        }
        if (row.mismatch && rev.source == PitchSource::Filename) ++mismatches;
        row.warning = ! rev.warnings.empty()
                      || (rev.source == PitchSource::Detected && rev.confidence < 0.8f);
        if (row.warning) ++warns;
        row.sortKey = rev.rootKey;
        if (const Zone* z = findZone (result.map, rev.sampleId))
            row.sortKey = z->rootKey * 100000 + z->velLow * 256 + z->rrIndex;
        else
            row.sortKey *= 100000;
        rows_.push_back (std::move (row));
    }
    // Keyboard order (root, velocity layer, RR), then natural filename order
    std::stable_sort (rows_.begin(), rows_.end(), [] (const Row& a, const Row& b) {
        if (a.sortKey != b.sortKey) return a.sortKey < b.sortKey;
        return a.sample.compareNatural (b.sample) < 0;
    });
    juce::String sum;
    const auto dot = glyph::dotSep();
    sum << "Auto-map review" << dot << (int) rows_.size() << " samples";
    if (unpitched > 0)
    {
        sum << dot << unpitched << " unpitched " << glyph::arrowRight() << " " << note (unpitchedLo);
        if (unpitchedHi > unpitchedLo) sum << glyph::enDash() << note (unpitchedHi);
    }
    if (mismatches > 0) sum << dot << mismatches << " mismatch" << (mismatches == 1 ? "" : "es");
    if (warns > 0) sum << dot << warns << " warning" << (warns == 1 ? "" : "s");
    summary_.setText (sum, juce::dontSendNotification);
    table_.updateContent();
    repaint();
}

void ReviewMapView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::bg());
    auto header = getLocalBounds().reduced (16).removeFromTop (48);
    drawFlagstick (g, juce::Rectangle<float> ((float) header.getX() + 2.0f,
                                              (float) header.getY() + 4.0f, 16.0f, 22.0f));
    g.setColour (Palette::brass().withAlpha (0.7f));
    g.fillRect ((float) header.getX(), (float) header.getBottom() - 1.0f, 72.0f, 1.0f);

    auto panel = getLocalBounds().reduced (16);
    panel.removeFromTop (56);
    g.setColour (Palette::bgRaised());
    g.fillRoundedRectangle (panel.toFloat(), 12.0f);
    g.setColour (Palette::border());
    g.drawRoundedRectangle (panel.toFloat(), 12.0f, 1.2f);
}

void ReviewMapView::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto header = r.removeFromTop (48);
    brand_.setBounds (header.getX() + 26, header.getY() + 2, 120, 22);
    brandSub_.setBounds (header.getX() + 26, header.getY() + 24, 160, 16);
    subtitle_.setBounds (header.getX() + 200, header.getY() + 10,
                         juce::jmax (120, header.getWidth() - 220), 28);
    r.removeFromTop (8);
    auto bar = r.removeFromTop (36);
    summary_.setBounds (bar.removeFromLeft (juce::jmax (200, bar.getWidth() - 280)));
    acceptBtn_.setBounds (bar.removeFromRight (120).reduced (4));
    backBtn_.setBounds (bar.removeFromRight (130).reduced (4));
    footer_.setBounds (r.removeFromBottom (28));
    table_.setBounds (r.reduced (10));
}

int ReviewMapView::getNumRows() { return (int) rows_.size(); }

void ReviewMapView::paintRowBackground (juce::Graphics& g, int row, int, int, bool sel)
{
    if (! juce::isPositiveAndBelow (row, (int) rows_.size())) return;
    if (rows_[(size_t) row].warning) g.fillAll (Palette::sand().withAlpha (0.12f));
    else if (sel) g.fillAll (Palette::fairwayDim().withAlpha (0.4f));
    else if (row % 2 == 0) g.fillAll (Palette::bg().withAlpha (0.55f));
}

void ReviewMapView::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (! juce::isPositiveAndBelow (row, (int) rows_.size())) return;
    const auto& r = rows_[(size_t) row];
    const juce::Font font (juce::FontOptions (13.0f));

    if (col == colSource)
    {
        // Pill tag: fairway = filename, cream = detected, brass = unpitched
        const auto c = pitchTagColour (r.kind);
        juce::GlyphArrangement ga;
        ga.addLineOfText (font, r.source, 0.0f, 0.0f);
        const float tw = ga.getBoundingBox (0, -1, true).getWidth();
        auto pill = juce::Rectangle<float> (6.0f, 4.0f, juce::jmin ((float) w - 10.0f, tw + 16.0f), (float) h - 8.0f);
        g.setColour (c.withAlpha (0.14f));
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setColour (c.withAlpha (0.55f));
        g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, 1.0f);
        g.setColour (c);
        g.setFont (font);
        g.drawText (r.source, pill.toNearestInt(), juce::Justification::centred, true);
        return;
    }

    juce::String text;
    switch (col) {
        case colSample: text = r.sample; break;
        case colRoot: text = r.root; break;
        case colVel: text = r.vel; break;
        case colRr: text = r.rr; break;
        case colSpan: text = r.keySpan; break;
        case colConf: text = r.confidence + (r.warning ? " !" : ""); break;
        case colPitch: text = r.pitch; break;
        case colAction: text = r.mismatch && ! r.canUseDetected ? juce::String ("applied") : juce::String(); break;
        default: break;
    }
    auto colour = r.warning ? Palette::sand() : Palette::text();
    if (col == colRoot && r.kind == PitchSource::Unpitched) colour = Palette::brass();
    if (col == colAction) colour = Palette::muted();
    g.setColour (colour);
    g.setFont (font);
    g.drawText (text, 6, 0, w - 10, h, juce::Justification::centredLeft, true);
}

juce::Component* ReviewMapView::refreshComponentForCell (int row, int col, bool, juce::Component* existing)
{
    const bool wanted = col == colAction && juce::isPositiveAndBelow (row, (int) rows_.size())
                        && rows_[(size_t) row].canUseDetected;
    if (! wanted)
    {
        delete existing; // JUCE contract: we own the stale component when returning nullptr
        return nullptr;
    }
    auto* btn = dynamic_cast<UseDetectedButton*> (existing);
    if (btn == nullptr)
    {
        delete existing;
        btn = new UseDetectedButton (*this);
    }
    btn->sampleId = rows_[(size_t) row].sampleId;
    return btn;
}

} // namespace looper
