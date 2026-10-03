#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "ZoneEditor.h"

namespace looper {

/**
 * juce::UndoableAction for one zone edit. perform() writes `after`, undo() writes `before`
 * through ZoneEditTarget::replaceZone (which pushes the change to playback).
 * Consecutive edits of the same zone inside one UndoManager transaction (a slider drag)
 * coalesce into a single action: first `before`, last `after`.
 */
class ZoneEditAction : public juce::UndoableAction
{
public:
    ZoneEditAction (ZoneEditTarget& target, ZoneEdit edit) : target_ (target), edit_ (std::move (edit)) {}

    bool perform() override { return target_.replaceZone (edit_.index, edit_.sampleId, edit_.after); }
    bool undo() override    { return target_.replaceZone (edit_.index, edit_.sampleId, edit_.before); }

    int getSizeInUnits() override { return static_cast<int> (sizeof (*this)); }

    juce::UndoableAction* createCoalescedAction (juce::UndoableAction* nextAction) override
    {
        auto* next = dynamic_cast<ZoneEditAction*> (nextAction);
        if (next == nullptr || &next->target_ != &target_
            || next->edit_.index != edit_.index || next->edit_.sampleId != edit_.sampleId)
            return nullptr;
        ZoneEdit merged = edit_;
        merged.after = next->edit_.after;
        return new ZoneEditAction (target_, std::move (merged));
    }

    const ZoneEdit& edit() const noexcept { return edit_; }

private:
    ZoneEditTarget& target_;
    ZoneEdit edit_;
};

} // namespace looper
