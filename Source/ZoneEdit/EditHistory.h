#pragma once

#include <cstdint>

namespace looper {

/**
 * "Unsaved" tracking that follows undo / redo (JUCE-free, unit-tested).
 *
 * Every undoable zone action owns two state ids: the state it starts from (`before`) and a
 * fresh id for the state it produces (`after`). perform() / redo() move the tracker to
 * `after`, undo() moves it back to `before`, so walking the undo history back to the state
 * that was saved lands on the saved id again and the patch reads as clean. Changes that are
 * not undoable (accepting an import, relinking a missing sample) set a sticky flag that only a
 * save clears.
 */
class EditStateTracker
{
public:
    EditStateTracker() noexcept { reset (false); }

    /** Id of the state the instrument is in right now. */
    uint64_t current() const noexcept { return current_; }
    /** Id of the state that was last written to (or read from) a patch file. */
    uint64_t saved() const noexcept { return saved_; }

    /** A fresh, never-used state id (for an action's `after`). */
    uint64_t allocate() noexcept { return ++lastId_; }

    /** An undoable action moved the instrument to `id` (perform / redo / undo). */
    void moveTo (uint64_t id) noexcept { current_ = id; }

    /** The current state was saved (or loaded): clean until the next change. */
    void markSaved() noexcept
    {
        saved_ = current_;
        externalDirty_ = false;
    }

    /** A change that undo cannot revert (import accepted, sample relinked): dirty until saved. */
    void markExternalChange() noexcept { externalDirty_ = true; }

    /** New instrument (import / patch load): fresh history, clean or dirty as given. */
    void reset (bool dirty) noexcept
    {
        current_ = saved_ = allocate();
        externalDirty_ = dirty;
    }

    bool isDirty() const noexcept { return externalDirty_ || current_ != saved_; }
    /** True when undo/redo put the zones back exactly where the last save left them. */
    bool atSavedState() const noexcept { return current_ == saved_; }

private:
    uint64_t lastId_ = 0;
    uint64_t current_ = 0;
    uint64_t saved_ = 0;
    bool externalDirty_ = false;
};

} // namespace looper
