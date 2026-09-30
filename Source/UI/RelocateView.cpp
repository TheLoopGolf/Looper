#include "RelocateView.h"
#include "../Plugin/PluginProcessor.h"

#include <algorithm>

namespace looper {

namespace {
juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
juce::String ellipsis() { return juce::String::fromUTF8 ("\xe2\x80\xa6"); }

/** Draw a path, trimming from the left ("…/Kit/Snares/s1.wav") so the tail stays visible. */
void drawPathTail (juce::Graphics& g, const juce::String& text, int x, int w, int h)
{
    const auto font = g.getCurrentFont();
    juce::String shown = text;
    if (juce::GlyphArrangement::getStringWidth (font, shown) > (float) w)
    {
        int start = 0;
        while (start < text.length()
               && juce::GlyphArrangement::getStringWidth (font, ellipsis() + text.substring (start)) > (float) w)
            ++start;
        shown = ellipsis() + text.substring (start);
    }
    g.drawText (shown, x, 0, w, h, juce::Justification::centredLeft, false);
}
} // namespace

RelocateView::RelocateView (LooperAudioProcessor& processor) : processor_ (processor)
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

    title_.setText (juce::String::fromUTF8 ("Relocate missing samples \xe2\x80\x94 relink moved files, then play on."),
                    juce::dontSendNotification);
    title_.setColour (juce::Label::textColourId, Palette::sand());
    addAndMakeVisible (title_);

    summary_.setColour (juce::Label::textColourId, Palette::text());
    addAndMakeVisible (summary_);

    // Primary action (LooperLookAndFeel renders bright fills as the fairway primary style)
    searchBtn_.setColour (juce::TextButton::buttonColourId, Palette::fairway());
    searchBtn_.setColour (juce::TextButton::textColourOffId, Palette::bg());
    searchBtn_.setTooltip ("Pick a folder: every missing file is searched for by name (recursively).");
    for (auto* b : { &locateBtn_, &skipBtn_ })
    {
        b->setColour (juce::TextButton::buttonColourId, Palette::bgRaised());
        b->setColour (juce::TextButton::textColourOffId, Palette::text());
    }
    locateBtn_.setTooltip ("Pick the selected file by hand; the rest are then tried from the same place.");
    skipBtn_.setTooltip ("Leave the selected sample offline (its zones stay silent).");
    closeBtn_.setColour (juce::TextButton::buttonColourId, Palette::bgRaised());
    closeBtn_.setColour (juce::TextButton::textColourOffId, Palette::text());

    searchBtn_.onClick = [this] { searchFolder(); };
    locateBtn_.onClick = [this] { locateSelected(); };
    skipBtn_.onClick = [this] { skipSelected(); };
    closeBtn_.onClick = [this] {
        cancel_->store (true);
        if (onClose_) onClose_();
    };
    addAndMakeVisible (searchBtn_);
    addAndMakeVisible (locateBtn_);
    addAndMakeVisible (skipBtn_);
    addAndMakeVisible (closeBtn_);

    auto& header = table_.getHeader();
    header.addColumn ("Sample", 1, 170, 80);
    header.addColumn ("Original path", 2, 330, 120);
    header.addColumn ("Status", 3, 190, 100);
    header.addColumn ("New location", 4, 250, 120);
    header.setStretchToFitActive (true);
    table_.setColour (juce::ListBox::backgroundColourId, Palette::bgSunken());
    table_.setColour (juce::ListBox::outlineColourId, Palette::border());
    table_.setRowHeight (26);
    addAndMakeVisible (table_);

    footer_.setColour (juce::Label::textColourId, Palette::muted());
    addAndMakeVisible (footer_);
}

RelocateView::~RelocateView()
{
    cancel_->store (true);
    searchPool_.removeAllJobs (true, 5000);
    table_.setModel (nullptr);
    setLookAndFeel (nullptr);
}

void RelocateView::open()
{
    cancel_->store (false);
    rows_.clear();
    for (const auto& m : processor_.missingSamples())
    {
        Row r;
        r.id = m.sampleId;
        r.originalPath = m.originalPath;
        for (const auto& ref : processor_.userSampleRefs())
            if (ref.id == m.sampleId)
                r.name = u8 (ref.displayName.empty() ? SampleRelocator::fileNameOf (ref.path) : ref.displayName);
        if (r.name.isEmpty())
            r.name = u8 (SampleRelocator::fileNameOf (m.originalPath));
        r.status = SampleRelocator::fileExists (m.originalPath) ? Status::Unreadable : Status::Missing;
        rows_.push_back (std::move (r));
    }
    footer_.setText ("Search a folder to find files by name (case-insensitive). When one file turns up, "
                     "its neighbours are tried from the same place automatically.",
                     juce::dontSendNotification);
    setBusy (false);
    table_.updateContent();
    if (! rows_.empty())
        table_.selectRow (0);
    updateSummary();
    repaint();
}

// --- Table model -----------------------------------------------------------

int RelocateView::getNumRows() { return (int) rows_.size(); }

void RelocateView::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) rows_.size())) return;
    if (selected)
        g.fillAll (Palette::fairwayDim().withAlpha (0.40f));
    else if (rows_[(size_t) row].status == Status::Ambiguous)
        g.fillAll (Palette::sand().withAlpha (0.10f));
    else if (row % 2 == 0)
        g.fillAll (Palette::bg().withAlpha (0.55f));
}

void RelocateView::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (! juce::isPositiveAndBelow (row, (int) rows_.size())) return;
    const auto& r = rows_[(size_t) row];
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    switch (col)
    {
        case 1:
            g.setColour (r.status == Status::Skipped ? Palette::muted() : Palette::text());
            g.drawText (r.name, 6, 0, w - 10, h, juce::Justification::centredLeft, true);
            break;
        case 2:
            g.setColour (Palette::muted());
            drawPathTail (g, u8 (r.originalPath), 6, w - 10, h);
            break;
        case 3:
            g.setColour (statusColour (r));
            g.drawText (statusText (r), 6, 0, w - 10, h, juce::Justification::centredLeft, true);
            break;
        case 4:
            g.setColour (Palette::text());
            if (! r.newPath.empty())
                drawPathTail (g, u8 (r.newPath), 6, w - 10, h);
            break;
        default: break;
    }
}

void RelocateView::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    table_.selectRow (row);
    locateSelected();
}

void RelocateView::selectedRowsChanged (int)
{
    setBusy (busy_); // refresh button enablement
}

juce::String RelocateView::getCellTooltip (int row, int)
{
    if (! juce::isPositiveAndBelow (row, (int) rows_.size())) return {};
    const auto& r = rows_[(size_t) row];
    juce::String tip;
    tip << "Original: " << u8 (r.originalPath);
    if (! r.newPath.empty())
        tip << "\nNow: " << u8 (r.newPath);
    if (r.detail.isNotEmpty())
        tip << "\n" << r.detail;
    if (! r.alternatives.empty())
    {
        tip << "\nOther candidates:";
        for (const auto& a : r.alternatives)
            tip << "\n  " << u8 (a);
        tip << juce::String::fromUTF8 ("\nUse Locate\xe2\x80\xa6 to pick a different one.");
    }
    return tip;
}

juce::String RelocateView::statusText (const Row& r)
{
    switch (r.status)
    {
        case Status::Missing:    return r.detail.isNotEmpty() ? "Missing - " + r.detail : juce::String ("Missing");
        case Status::Unreadable: return "Unreadable file";
        case Status::Relinked:   return r.detail.isNotEmpty() ? "Relinked - " + r.detail : juce::String ("Relinked");
        case Status::Ambiguous:  return "Relinked - ambiguous (" + juce::String ((int) r.alternatives.size() + 1) + " matches)";
        case Status::Skipped:    return "Skipped (silent)";
        case Status::Failed:     return "Load failed - " + r.detail;
    }
    return {};
}

juce::Colour RelocateView::statusColour (const Row& r)
{
    switch (r.status)
    {
        case Status::Relinked:  return Palette::fairway();
        case Status::Ambiguous: return Palette::sand();
        case Status::Skipped:   return Palette::muted();
        case Status::Missing:
        case Status::Unreadable:
        case Status::Failed:    return Palette::danger();
    }
    return Palette::text();
}

// --- Actions -----------------------------------------------------------------

juce::File RelocateView::suggestedStartFolder() const
{
    if (lastFolder_.isDirectory())
        return lastFolder_;
    if (processor_.lastPatchPath().isNotEmpty())
    {
        const auto dir = juce::File (processor_.lastPatchPath()).getParentDirectory();
        if (dir.isDirectory())
            return dir;
    }
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory);
}

std::vector<MissingSample> RelocateView::unresolvedTargets (bool includeAmbiguous, const std::string& excludeId) const
{
    std::vector<MissingSample> out;
    for (const auto& r : rows_)
    {
        if (r.id == excludeId)
            continue;
        const bool unresolved = r.status == Status::Missing || r.status == Status::Unreadable || r.status == Status::Failed
                          || (includeAmbiguous && r.status == Status::Ambiguous);
        if (unresolved)
            out.push_back ({ r.id, r.originalPath });
    }
    return out;
}

void RelocateView::searchFolder()
{
    if (busy_) return;
    chooser_ = std::make_unique<juce::FileChooser> ("Search folder for missing samples", suggestedStartFolder());
    constexpr auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    juce::Component::SafePointer<RelocateView> safe (this);
    chooser_->launchAsync (chooserFlags, [safe] (const juce::FileChooser& fc) {
        auto* self = safe.getComponent();
        const auto dir = fc.getResult();
        if (self == nullptr || ! dir.isDirectory())
            return;
        self->lastFolder_ = dir;
        self->runSearch (dir.getFullPathName().toStdString());
    });
}

void RelocateView::runSearch (const std::string& folder)
{
    auto targets = unresolvedTargets (true);
    if (targets.empty())
    {
        footer_.setText ("Nothing left to search for.", juce::dontSendNotification);
        return;
    }
    setBusy (true, juce::String::fromUTF8 ("Searching ") + u8 (folder) + ellipsis());
    cancel_->store (false);
    auto cancel = cancel_;
    juce::Component::SafePointer<RelocateView> safe (this);
    searchPool_.addJob ([safe, cancel, jobTargets = std::move (targets), folder] {
        RelocatorOptions options;
        options.cancel = cancel.get();
        auto results = SampleRelocator::searchFolder (jobTargets, folder, options);
        if (cancel->load())
            return;
        juce::MessageManager::callAsync ([safe, matches = std::move (results), folder] {
            if (auto* self = safe.getComponent())
            {
                self->setBusy (false);
                self->applyMatches (matches, "in " + u8 (folder));
            }
        });
    });
}

void RelocateView::runCascade (const std::string& foundOriginal, const std::string& foundNew)
{
    auto targets = unresolvedTargets (false);
    if (targets.empty())
        return;
    setBusy (true, juce::String::fromUTF8 ("Trying the same folder for the rest") + ellipsis());
    cancel_->store (false);
    auto cancel = cancel_;
    juce::Component::SafePointer<RelocateView> safe (this);
    searchPool_.addJob ([safe, cancel, jobTargets = std::move (targets), foundOriginal, foundNew] {
        RelocatorOptions options;
        options.cancel = cancel.get();
        auto results = SampleRelocator::cascadeFrom (foundOriginal, foundNew, jobTargets, options);
        if (cancel->load())
            return;
        juce::MessageManager::callAsync ([safe, matches = std::move (results), foundNew] {
            if (auto* self = safe.getComponent())
            {
                self->setBusy (false);
                self->applyMatches (matches, "near " + u8 (SampleRelocator::parentOf (foundNew)));
            }
        });
    });
}

bool RelocateView::relinkRow (Row& row, const std::string& newPath)
{
    juce::String error;
    if (processor_.relocateSample (row.id, juce::File (u8 (newPath)), &error))
    {
        row.newPath = newPath;
        row.status = Status::Relinked;
        row.alternatives.clear();
        return true;
    }
    row.status = Status::Failed;
    row.detail = error;
    return false;
}

void RelocateView::applyMatches (const std::vector<RelocationMatch>& matches, const juce::String& context)
{
    int found = 0, ambiguous = 0, notFound = 0, failed = 0;
    for (const auto& m : matches)
    {
        auto it = std::find_if (rows_.begin(), rows_.end(), [&] (const Row& r) { return r.id == m.sampleId; });
        if (it == rows_.end())
            continue;
        if (! m.found())
        {
            if (it->status != Status::Ambiguous) // keep an earlier best guess
            {
                it->detail = "not found " + context;
                ++notFound;
            }
            continue;
        }
        if (! relinkRow (*it, m.newPath))
        {
            ++failed;
            continue;
        }
        ++found;
        if (m.ambiguous)
        {
            ++ambiguous;
            it->status = Status::Ambiguous;
            it->alternatives = m.alternatives;
            it->detail = "best path match of " + juce::String (m.candidateCount) + " files with this name";
        }
        else
        {
            it->detail = m.viaCascade ? juce::String ("via folder structure") : juce::String ("found by name");
        }
    }

    juce::String msg;
    msg << "Found " << found << " of " << (int) matches.size() << " " << context << ".";
    if (ambiguous > 0)
        msg << " " << ambiguous << juce::String::fromUTF8 (" ambiguous \xe2\x80\x94 hover to see candidates, Locate\xe2\x80\xa6 to override.");
    if (failed > 0)
        msg << " " << failed << " could not be decoded.";
    if (notFound > 0 && found == 0)
        msg << " Try a different folder or Locate" << ellipsis() << " a file by hand.";
    footer_.setText (msg, juce::dontSendNotification);
    table_.updateContent();
    table_.repaint();
    updateSummary();
}

void RelocateView::locateSelected()
{
    if (busy_) return;
    const int sel = table_.getSelectedRow();
    if (! juce::isPositiveAndBelow (sel, (int) rows_.size()))
        return;
    const auto& row = rows_[(size_t) sel];
    const auto id = row.id;
    const auto wantName = u8 (SampleRelocator::fileNameOf (row.originalPath));

    chooser_ = std::make_unique<juce::FileChooser> ("Locate " + wantName, suggestedStartFolder(),
                                                    "*.wav;*.aif;*.aiff;*.flac;" + wantName);
    constexpr auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    juce::Component::SafePointer<RelocateView> safe (this);
    chooser_->launchAsync (chooserFlags, [safe, id] (const juce::FileChooser& fc) {
        auto* self = safe.getComponent();
        const auto file = fc.getResult();
        if (self == nullptr || ! file.existsAsFile())
            return;
        auto it = std::find_if (self->rows_.begin(), self->rows_.end(), [&] (const Row& r) { return r.id == id; });
        if (it == self->rows_.end())
            return;
        self->lastFolder_ = file.getParentDirectory();
        const auto newPath = file.getFullPathName().toStdString();
        const auto original = it->originalPath;
        if (self->relinkRow (*it, newPath))
        {
            it->detail = "located by hand";
            self->footer_.setText ("Relinked " + file.getFileName() + ".", juce::dontSendNotification);
            self->table_.updateContent();
            self->table_.repaint();
            self->updateSummary();
            self->runCascade (original, newPath);
        }
        else
        {
            self->footer_.setText ("Could not load " + file.getFileName() + ": " + it->detail, juce::dontSendNotification);
            self->table_.repaint();
            self->updateSummary();
        }
    });
}

void RelocateView::skipSelected()
{
    if (busy_) return;
    const int sel = table_.getSelectedRow();
    if (! juce::isPositiveAndBelow (sel, (int) rows_.size()))
        return;
    auto& row = rows_[(size_t) sel];
    if (row.status == Status::Relinked || row.status == Status::Ambiguous)
        return;
    row.status = Status::Skipped;
    row.detail = {};
    if (sel + 1 < (int) rows_.size())
        table_.selectRow (sel + 1);
    table_.repaint();
    updateSummary();
}

void RelocateView::setBusy (bool busy, const juce::String& message)
{
    busy_ = busy;
    const int sel = table_.getSelectedRow();
    const bool hasSel = juce::isPositiveAndBelow (sel, (int) rows_.size());
    const bool selOpen = hasSel && rows_[(size_t) sel].status != Status::Relinked
                         && rows_[(size_t) sel].status != Status::Ambiguous
                         && rows_[(size_t) sel].status != Status::Skipped;
    searchBtn_.setEnabled (! busy && ! unresolvedTargets (true).empty());
    locateBtn_.setEnabled (! busy && hasSel);
    skipBtn_.setEnabled (! busy && selOpen);
    if (message.isNotEmpty())
        footer_.setText (message, juce::dontSendNotification);
}

void RelocateView::updateSummary()
{
    int relinked = 0, ambiguous = 0, skipped = 0, stillMissing = 0;
    for (const auto& r : rows_)
    {
        switch (r.status)
        {
            case Status::Relinked:  ++relinked; break;
            case Status::Ambiguous: ++ambiguous; ++relinked; break;
            case Status::Skipped:   ++skipped; break;
            case Status::Missing:
            case Status::Unreadable:
            case Status::Failed:    ++stillMissing; break;
        }
    }
    juce::String s;
    if (rows_.empty())
        s = "No missing samples - every zone is on the green.";
    else
    {
        s << (int) rows_.size() << " missing at load  |  " << relinked << " relinked";
        if (ambiguous > 0) s << " (" << ambiguous << " ambiguous)";
        s << "  |  " << stillMissing << " still missing";
        if (skipped > 0) s << "  |  " << skipped << " skipped";
    }
    summary_.setText (s, juce::dontSendNotification);
    closeBtn_.setButtonText (stillMissing == 0 ? "Done" : "Close");
    setBusy (busy_);
}

// --- Layout ------------------------------------------------------------------

void RelocateView::paint (juce::Graphics& g)
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

    // Sand "bunker" accent under the action bar while anything is still missing
    bool anyOpen = false;
    for (const auto& r : rows_)
        anyOpen = anyOpen || r.status == Status::Missing || r.status == Status::Unreadable || r.status == Status::Failed;
    g.setColour ((anyOpen ? Palette::sand() : Palette::fairway()).withAlpha (0.55f));
    g.fillRect ((float) panel.getX() + 12.0f, (float) panel.getY() + 46.0f, 96.0f, 1.5f);
}

void RelocateView::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto header = r.removeFromTop (48);
    brand_.setBounds (header.getX() + 26, header.getY() + 2, 120, 22);
    brandSub_.setBounds (header.getX() + 26, header.getY() + 24, 160, 16);
    title_.setBounds (header.getX() + 200, header.getY() + 10, juce::jmax (120, header.getWidth() - 220), 28);
    r.removeFromTop (8);

    auto panel = r.reduced (10, 6);
    auto bar = panel.removeFromTop (36);
    closeBtn_.setBounds (bar.removeFromRight (96).reduced (3));
    skipBtn_.setBounds (bar.removeFromRight (72).reduced (3));
    locateBtn_.setBounds (bar.removeFromRight (96).reduced (3));
    searchBtn_.setBounds (bar.removeFromRight (136).reduced (3));
    summary_.setBounds (bar);
    panel.removeFromTop (8);
    footer_.setBounds (panel.removeFromBottom (28));
    table_.setBounds (panel.reduced (0, 4));
}

} // namespace looper
