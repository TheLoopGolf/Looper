#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PatchStore/SampleRelocator.h"
#include "LooperLookAndFeel.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class LooperAudioProcessor;

namespace looper {

/**
 * "Relocate missing samples" screen (golf theme, same chrome as Review / Settings).
 * Lists offline samples and offers Search folder... / Locate... / Skip. Every file that
 * is found is relinked immediately (audio reloads live, patch marked dirty).
 * Folder scans run on a background thread; results are applied on the message thread.
 */
class RelocateView : public juce::Component, private juce::TableListBoxModel
{
public:
    explicit RelocateView (LooperAudioProcessor& processor);
    ~RelocateView() override;

    /** Rebuild the list from the processor's current missing samples. */
    void open();
    void setCloseCallback (std::function<void()> fn) { onClose_ = std::move (fn); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Status { Missing, Unreadable, Relinked, Ambiguous, Skipped, Failed };
    struct Row
    {
        std::string id;
        juce::String name;
        std::string originalPath;
        std::string newPath;
        Status status = Status::Missing;
        juce::String detail;
        std::vector<std::string> alternatives;
    };

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellDoubleClicked (int row, int col, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    juce::String getCellTooltip (int row, int col) override;

    void searchFolder();
    void locateSelected();
    void skipSelected();
    void runSearch (const std::string& folder);
    void runCascade (const std::string& foundOriginal, const std::string& foundNew);
    void applyMatches (const std::vector<RelocationMatch>& matches, const juce::String& context);
    bool relinkRow (Row& row, const std::string& newPath);
    std::vector<MissingSample> unresolvedTargets (bool includeAmbiguous, const std::string& excludeId = {}) const;
    juce::File suggestedStartFolder() const;
    void setBusy (bool busy, const juce::String& message = {});
    void updateSummary();
    static juce::String statusText (const Row& r);
    static juce::Colour statusColour (const Row& r);

    LooperAudioProcessor& processor_;
    LooperLookAndFeel lookAndFeel_;

    juce::Label brand_, brandSub_, title_, summary_, footer_;
    juce::TextButton searchBtn_; // "Search folder..." / "Locate..." (ellipsis glyph set in the constructor)
    juce::TextButton locateBtn_;
    juce::TextButton skipBtn_ { "Skip" };
    juce::TextButton closeBtn_ { "Close" };
    juce::TableListBox table_ { {}, this };
    juce::TooltipWindow tooltips_ { this, 500 };

    std::vector<Row> rows_;
    std::function<void()> onClose_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::File lastFolder_;
    bool busy_ = false;

    juce::ThreadPool searchPool_ { juce::ThreadPoolOptions{}.withThreadName ("Looper relocate").withNumberOfThreads (1) };
    std::shared_ptr<std::atomic<bool>> cancel_ = std::make_shared<std::atomic<bool>> (false);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RelocateView)
};

} // namespace looper
