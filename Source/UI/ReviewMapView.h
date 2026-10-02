#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../AutoMapper/AutoMapper.h"
#include "../InstrumentMap/InstrumentMap.h"
#include "LooperLookAndFeel.h"
#include <functional>
#include <string>
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
    /** Row action "Use detected" (filename/audio mismatch): sample id of the clicked row. */
    void setUseDetectedCallback (std::function<void (const std::string&)> fn) { onUseDetected_ = std::move (fn); }

    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool sel) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool sel) override;
    juce::Component* refreshComponentForCell (int row, int col, bool sel, juce::Component* existing) override;

private:
    enum Column { colSample = 1, colSource, colRoot, colVel, colRr, colSpan, colConf, colPitch, colAction };

    struct Row {
        std::string sampleId;
        juce::String sample, source, root, vel, rr, keySpan, confidence, pitch;
        PitchSource kind = PitchSource::Unpitched;
        bool warning = false;
        bool mismatch = false;
        bool canUseDetected = false;
        int sortKey = 0;
    };
    class UseDetectedButton;

    LooperLookAndFeel lookAndFeel_;
    juce::Label brand_, brandSub_, subtitle_, summary_, footer_;
    juce::TextButton acceptBtn_ { "Accept map" }, backBtn_ { "Back to play" };
    juce::TableListBox table_ { {}, this };
    std::vector<Row> rows_;
    std::function<void()> onAccept_, onBack_;
    std::function<void (const std::string&)> onUseDetected_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReviewMapView)
};

} // namespace looper
