#pragma once

#include "binaryninjaapi.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

// Shared pane state updated by BifrostContainer / BifrostDiffView and read by
// BifrostSidebarWidget. Decouples components without direct references.
struct BifrostPaneState
{
    BinaryNinja::Ref<BinaryNinja::BinaryView> leftData;
    BinaryNinja::Ref<BinaryNinja::BinaryView> rightData;

    // Navigation callbacks registered by the active BifrostContainer or
    // BifrostDiffView so other components can drive the panes.
    std::function<void(uint64_t)> navigateLeft;
    std::function<void(uint64_t)> navigateRight;

    // Registered by BifrostDiffView: navigate BOTH panes to a diff entry and
    // apply block/instruction highlighting for it. When set, the sidebar uses
    // this instead of navigateLeft/navigateRight so highlights are applied.
    // Args: leftAddr, rightAddr, status ("identical"|"changed"|"added"|"removed").
    std::function<void(uint64_t, uint64_t, const std::string&)> highlightEntry;

    // Active diff metadata — set by BifrostDiffView, read by sidebar.
    BinaryNinja::Ref<BinaryNinja::Metadata> activeDiffData;

    // Owners of the nav callbacks and the active diff, so each is cleared only
    // by whoever set it (a destroyed view must not wipe another view's state).
    void* navOwner = nullptr;
    void* diffOwner = nullptr;

    // Set/clear the nav callbacks (owner = the driving BifrostContainer or
    // BifrostDiffView). clearNav is a no-op if someone else is now active.
    void setNav(void* owner,
                std::function<void(uint64_t)> navLeft,
                std::function<void(uint64_t)> navRight,
                std::function<void(uint64_t, uint64_t, const std::string&)> highlight)
    {
        navOwner       = owner;
        navigateLeft   = std::move(navLeft);
        navigateRight  = std::move(navRight);
        highlightEntry = std::move(highlight);
    }
    void clearNav(void* owner)
    {
        if (navOwner != owner) return;
        navOwner       = nullptr;
        navigateLeft   = nullptr;
        navigateRight  = nullptr;
        highlightEntry = nullptr;
    }

    // Set/clear the active diff (owner = the BifrostDiffView showing it).
    void setDiff(void* owner, BinaryNinja::Ref<BinaryNinja::Metadata> data)
    {
        diffOwner      = owner;
        activeDiffData = data;
    }
    void clearDiff(void* owner)
    {
        if (diffOwner != owner) return;
        diffOwner      = nullptr;
        activeDiffData = nullptr;
    }

    // Observers notified when activeDiffData changes, each keyed by its owner
    // (the widget pointer) so it can be removed precisely on destruction — a
    // stale observer firing on a freed widget is a use-after-free crash.
    std::vector<std::pair<void*, std::function<void()>>> diffObservers;

    void addDiffObserver(void* owner, std::function<void()> fn)
    {
        diffObservers.emplace_back(owner, std::move(fn));
    }

    void removeDiffObserver(void* owner)
    {
        diffObservers.erase(
            std::remove_if(diffObservers.begin(), diffObservers.end(),
                           [owner](const auto& p) { return p.first == owner; }),
            diffObservers.end());
    }

    void notifyDiffChanged()
    {
        // Copy first: an observer may add/remove observers (e.g. open a diff
        // view) while we iterate.
        auto snapshot = diffObservers;
        for (auto& [owner, fn] : snapshot)
            if (fn) fn();
    }

    static BifrostPaneState& instance()
    {
        // Deliberately leaked, never destroyed. This singleton holds Binary
        // Ninja Ref<> objects (leftData/rightData/activeDiffData); a static
        // destructor would release them from __cxa_finalize at process exit —
        // i.e. AFTER BN's core has torn itself down — and BNFreeBinaryView
        // then aborts inside an already-finalized core subsystem. Letting the
        // OS reclaim this at exit is the standard fix for the static
        // destruction order problem across dynamic libraries.
        static BifrostPaneState* s = new BifrostPaneState();
        return *s;
    }

    void set(BinaryNinja::Ref<BinaryNinja::BinaryView> left,
             BinaryNinja::Ref<BinaryNinja::BinaryView> right)
    {
        leftData  = left;
        rightData = right;
    }

private:
    BifrostPaneState() = default;
};
