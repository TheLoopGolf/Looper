#include "ReviewMapView.h"
#include "../AutoMapper/FilenameTokens.h"

namespace looper {

namespace {
juce::String pitchLabel(PitchSource s)
{
    switch (s) {
        case PitchSource::Filename: return "filename";
        case PitchSource::Detected: return "detected";
        case PitchSource::Spread: return "spread";
    }
    return "?";
}
const Zone* findZone(const InstrumentMap& map, const std::string& id)
{
    for (const auto& z : map.zones)
        if (z.sampleId == id) return &z;
    return nullptr;
}
} // namespace

ReviewMapView::ReviewMapView()
{
    brand_.setText("LOOPER  Loop Audio Lab", juce::dontSendNotification);
    brand_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    brand_.setColour(juce::Label::textColourId, juce::Colour(0xffe8e8ee));
    addAndMakeVisible(brand_);
    subtitle_.setText("Review map — fix auto-map, then return to play.", juce::dontSendNotification);
    subtitle_.setColour(juce::Label::textColourId, juce::Colour(0xff6a8cff));
    addAndMakeVisible(subtitle_);
    summary_.setColour(juce::Label::textColourId, juce::Colour(0xffe8e8ee));
    addAndMakeVisible(summary_);
    acceptBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d5a3d));
    backBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff24242a));
    acceptBtn_.onClick = [this] { if (onAccept_) onAccept_(); };
    backBtn_.onClick = [this] { if (onBack_) onBack_(); };
    addAndMakeVisible(acceptBtn_);
    addAndMakeVisible(backBtn_);
    table_.getHeader().addColumn("Sample", 1, 180);
    table_.getHeader().addColumn("Source", 2, 80);
    table_.getHeader().addColumn("Root", 3, 60);
    table_.getHeader().addColumn("Vel", 4, 70);
    table_.getHeader().addColumn("RR", 5, 40);
    table_.getHeader().addColumn("Key span", 6, 100);
    table_.getHeader().addColumn("Confidence", 7, 90);
    table_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff24242a));
    addAndMakeVisible(table_);
    footer_.setColour(juce::Label::textColourId, juce::Colour(0xff9a9aa8));
    footer_.setText("Warnings highlight low-confidence / spread rows.", juce::dontSendNotification);
    addAndMakeVisible(footer_);
}

ReviewMapView::~ReviewMapView() { table_.setModel(nullptr); }

void ReviewMapView::clear()
{
    rows_.clear();
    summary_.setText({}, juce::dontSendNotification);
    table_.updateContent();
}

void ReviewMapView::setResult(const AutoMapResult& result, const std::vector<SampleRef>& refs)
{
    rows_.clear();
    auto nameFor = [&](const std::string& id) -> juce::String {
        for (const auto& r : refs)
            if (r.id == id)
                return r.displayName.empty() ? r.path : r.displayName;
        return id;
    };
    int warns = 0;
    for (const auto& rev : result.reviews)
    {
        Row row;
        row.sample = nameFor(rev.sampleId);
        row.source = pitchLabel(rev.source);
        row.confidence = juce::String(rev.confidence, 2);
        if (const Zone* z = findZone(result.map, rev.sampleId))
        {
            row.root = midiToNoteName(z->rootKey);
            row.vel = juce::String(z->velLow) + "-" + juce::String(z->velHigh);
            row.rr = z->rrIndex > 0 ? juce::String(z->rrIndex) : "-";
            row.keySpan = midiToNoteName(z->keyLow) + "-" + midiToNoteName(z->keyHigh);
        }
        else
        {
            row.root = "?"; row.vel = "-"; row.rr = "-"; row.keySpan = "-";
        }
        row.warning = rev.confidence < 0.7f || !rev.warnings.empty()
                      || rev.source == PitchSource::Spread;
        if (row.warning) ++warns;
        rows_.push_back(std::move(row));
    }
    juce::String sum;
    sum << "Auto-map review · " << (int) rows_.size() << " samples";
    if (warns > 0) sum << " · " << warns << " warning" << (warns == 1 ? "" : "s");
    summary_.setText(sum, juce::dontSendNotification);
    table_.updateContent();
    repaint();
}

void ReviewMapView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1c1c20));
    auto panel = getLocalBounds().reduced(16);
    panel.removeFromTop(56);
    g.setColour(juce::Colour(0xff24242a));
    g.fillRoundedRectangle(panel.toFloat(), 8.0f);
    g.setColour(juce::Colour(0xff3a3a48));
    g.drawRoundedRectangle(panel.toFloat(), 8.0f, 1.2f);
}

void ReviewMapView::resized()
{
    auto r = getLocalBounds().reduced(16);
    brand_.setBounds(r.removeFromTop(22));
    subtitle_.setBounds(r.removeFromTop(22));
    r.removeFromTop(8);
    auto bar = r.removeFromTop(36);
    summary_.setBounds(bar.removeFromLeft(juce::jmax(200, bar.getWidth() - 280)));
    acceptBtn_.setBounds(bar.removeFromRight(120).reduced(4));
    backBtn_.setBounds(bar.removeFromRight(130).reduced(4));
    footer_.setBounds(r.removeFromBottom(28));
    table_.setBounds(r.reduced(8));
}

int ReviewMapView::getNumRows() { return (int) rows_.size(); }

void ReviewMapView::paintRowBackground(juce::Graphics& g, int row, int, int, bool sel)
{
    if (!juce::isPositiveAndBelow(row, (int) rows_.size())) return;
    if (rows_[(size_t) row].warning) g.fillAll(juce::Colour(0xff3a3220));
    else if (sel) g.fillAll(juce::Colour(0xff2e2e38));
    else if (row % 2 == 0) g.fillAll(juce::Colour(0xff202028));
}

void ReviewMapView::paintCell(juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (!juce::isPositiveAndBelow(row, (int) rows_.size())) return;
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
    g.setColour(juce::Colour(r.warning ? 0xffffc857 : 0xffe8e8ee));
    g.setFont(13.0f);
    g.drawText(text, 6, 0, w - 10, h, juce::Justification::centredLeft, true);
}

} // namespace looper
