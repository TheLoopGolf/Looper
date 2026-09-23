#include "ReviewMapView.h"
#include "../AutoMapper/FilenameTokens.h"

namespace looper {

namespace {
juce::String pitchLabel (PitchSource s)
{
    switch (s) {
        case PitchSource::Filename: return "filename";
        case PitchSource::Detected: return "detected";
        case PitchSource::Spread: return "spread";
    }
    return "?";
}
const Zone* findZone (const InstrumentMap& map, const std::string& id)
{
    for (const auto& z : map.zones)
        if (z.sampleId == id) return &z;
    return nullptr;
}
} // namespace

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

    subtitle_.setText ("Review map — fix auto-map, then return to play.", juce::dontSendNotification);
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

    table_.getHeader().addColumn ("Sample", 1, 180);
    table_.getHeader().addColumn ("Source", 2, 80);
    table_.getHeader().addColumn ("Root", 3, 60);
    table_.getHeader().addColumn ("Vel", 4, 70);
    table_.getHeader().addColumn ("RR", 5, 40);
    table_.getHeader().addColumn ("Key span", 6, 100);
    table_.getHeader().addColumn ("Confidence", 7, 90);
    table_.setColour (juce::ListBox::backgroundColourId, Palette::bgSunken());
    table_.setColour (juce::ListBox::outlineColourId, Palette::border());
    addAndMakeVisible (table_);

    footer_.setColour (juce::Label::textColourId, Palette::muted());
    footer_.setText ("Warnings highlight low-confidence / spread rows.", juce::dontSendNotification);
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
    auto nameFor = [&] (const std::string& id) -> juce::String {
        for (const auto& r : refs)
            if (r.id == id)
                return r.displayName.empty() ? r.path : r.displayName;
        return id;
    };
    int warns = 0;
    for (const auto& rev : result.reviews)
    {
        Row row;
        row.sample = nameFor (rev.sampleId);
        row.source = pitchLabel (rev.source);
        row.confidence = juce::String (rev.confidence, 2);
        if (const Zone* z = findZone (result.map, rev.sampleId))
        {
            row.root = midiToNoteName (z->rootKey);
            row.vel = juce::String (z->velLow) + "-" + juce::String (z->velHigh);
            row.rr = z->rrIndex > 0 ? juce::String (z->rrIndex) : "-";
            row.keySpan = midiToNoteName (z->keyLow) + "-" + midiToNoteName (z->keyHigh);
        }
        else
        {
            row.root = "?"; row.vel = "-"; row.rr = "-"; row.keySpan = "-";
        }
        row.warning = rev.confidence < 0.7f || ! rev.warnings.empty()
                      || rev.source == PitchSource::Spread;
        if (row.warning) ++warns;
        rows_.push_back (std::move (row));
    }
    juce::String sum;
    sum << "Auto-map review · " << (int) rows_.size() << " samples";
    if (warns > 0) sum << " · " << warns << " warning" << (warns == 1 ? "" : "s");
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
    juce::String text;
    switch (col) {
        case 1: text = r.sample; break;
        case 2: text = r.source; break;
        case 3: text = r.root; break;
        case 4: text = r.vel; break;
        case 5: text = r.rr; break;
        case 6: text = r.keySpan; break;
        case 7: text = r.confidence + (r.warning ? " !" : ""); break;
        default: break;
    }
    g.setColour (r.warning ? Palette::sand() : Palette::text());
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (text, 6, 0, w - 10, h, juce::Justification::centredLeft, true);
}

} // namespace looper
