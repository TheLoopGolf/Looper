#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "EditHistory.h"
#include "ZoneEditor.h"

#include <vector>

namespace looper {

/**
 * juce::UndoableAction for one zone edit or a group of them (multi-selection, strip drags).
 * perform() writes every `after`, undo() every `before`, through ZoneEditTarget::replaceZones
 * (one live map update for the whole group).
 *
 * Consecutive actions on the same zones inside one UndoManager transaction (a slider or strip
 * drag) coalesce into a single action: first `before`, last `after`.
 *
 * With an EditStateTracker the action also records which history state it leads from / to,
 * so undoing back to the saved state clears the patch's "unsaved" marker (EditHistory.h).
 */
class ZoneEditAction : public juce::UndoableAction
{
public:
    ZoneEditAction (ZoneEditTarget& target, ZoneEdit edit, EditStateTracker* tracker = nullptr)
        : ZoneEditAction (target, std::vector<ZoneEdit> { std::move (edit) }, tracker) {}

    ZoneEditAction (ZoneEditTarget& target, std::vector<ZoneEdit> edits, EditStateTracker* tracker = nullptr)
        : target_ (target), edits_ (std::move (edits)), tracker_ (tracker)
    {
        if (tracker_ != nullptr)
        {
            beforeState_ = tracker_->current();
            afterState_ = tracker_->allocate();
        }
    }

    bool perform() override
    {
        if (! target_.replaceZones (edits_, true))
            return false;
        if (tracker_ != nullptr)
            tracker_->moveTo (afterState_);
        return true;
    }

    bool undo() override
    {
        if (! target_.replaceZones (edits_, false))
            return false;
        if (tracker_ != nullptr)
            tracker_->moveTo (beforeState_);
        return true;
    }

    int getSizeInUnits() override
    {
        return static_cast<int> (sizeof (*this) + edits_.size() * sizeof (ZoneEdit));
    }

    juce::UndoableAction* createCoalescedAction (juce::UndoableAction* nextAction) override
    {
        auto* next = dynamic_cast<ZoneEditAction*> (nextAction);
        if (next == nullptr || &next->target_ != &target_ || next->tracker_ != tracker_
            || next->edits_.size() != edits_.size())
            return nullptr;
        for (size_t i = 0; i < edits_.size(); ++i)
            if (next->edits_[i].index != edits_[i].index || next->edits_[i].sampleId != edits_[i].sampleId)
                return nullptr;
        std::vector<ZoneEdit> merged = edits_;
        for (size_t i = 0; i < merged.size(); ++i)
            merged[i].after = next->edits_[i].after;
        auto* m = new ZoneEditAction (target_, std::move (merged), nullptr);
        m->tracker_ = tracker_;
        m->beforeState_ = beforeState_;
        m->afterState_ = next->afterState_;
        return m;
    }

    /** First edit (single-zone actions). */
    const ZoneEdit& edit() const noexcept { return edits_.front(); }
    const std::vector<ZoneEdit>& edits() const noexcept { return edits_; }

private:
    ZoneEditTarget& target_;
    std::vector<ZoneEdit> edits_;
    EditStateTracker* tracker_ = nullptr;
    uint64_t beforeState_ = 0;
    uint64_t afterState_ = 0;
};

} // namespace looper
