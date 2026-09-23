#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../AutoMapper/AutoMapper.h"
#include "../InstrumentMap/InstrumentMap.h"
#include "LooperLookAndFeel.h"
#include <functional>
#include <vector>

namespace looper {

class ReviewMapView : public juce::Component, private juce::TableListBoxModel
{
public:
    ReviewMapView();
    ~ReviewMapView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void setResult (const AutoMapResult& result, const std::vector<SampleRef>& refs);
    void clear();
    void setAcceptCallback (std::function<void()> fn) { onAccept_ = std::move (fn); }
    void setBackCallback (std::function<void()> fn) { onBack_ = std::move (fn); }

    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool sel) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool sel) override;

private:
    struct Row {
        juce::String sample, source, root, vel, rr, keySpan, confidence;
        bool warning = false;
    };
    LooperLookAndFeel lookAndFeel_;
    juce::Label brand_, brandSub_, subtitle_, summary_, footer_;
    juce::TextButton acceptBtn_ { "Accept map" }, backBtn_ { "Back to play" };
    juce::TableListBox table_ { {}, this };
    std::vector<Row> rows_;
    std::function<void()> onAccept_, onBack_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReviewMapView)
};

} // namespace looper
