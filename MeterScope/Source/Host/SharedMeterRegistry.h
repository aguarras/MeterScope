// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#pragma once

// Phase 2.2 Part B. See plugins/MeterScope/.ideas/architecture.md ->
// "Multi-Instance Shared Store" (SharedMeterRegistry) and this phase's task
// instructions. Process-wide Meyer's singleton, partitioned by ProjectKey -
// there is deliberately NO unfiltered accessor in the public API (only
// getActiveEntriesSortedByOrder(key)/findEntry(key, trackId), both of which
// require a ProjectKey), matching architecture.md's explicit requirement.
//
// Audio-thread discipline (see this class's own .cpp for the full argument):
// the ONLY audio-thread interaction anywhere in this file's design is that
// each instance's MeteringEngine writes its own SharedMeterSnapshot fields
// via relaxed atomic stores at the end of processBlock (unchanged from Phase
// 2.1 - see MeteringEngine.h's publishSnapshot()). Entry::snapshot below is a
// REFERENCE (const pointer) to that already-existing, already-atomic
// snapshot - registering/unregistering/reading it never touches the audio
// thread, never locks it, and never allocates from it.

#include "ProjectKey.h"
#include "SharedProjectConfig.h"
#include "../DSP/SharedMeterSnapshot.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace meterscope
{
    class SharedMeterRegistry
    {
    public:
        // Comfortably exceeds any realistic REAPER track/take-FX count
        // (architecture.md: "kMaxInstances comfortably exceeds any realistic
        // REAPER track count").
        static constexpr int kMaxInstances = 128;

        // trackOrder encoding (architecture.md's Entry::trackOrder is
        // std::atomic<int>, but the Tier B spike's user-approved decisions
        // require take FX to sort "immediately after its parent track" and
        // Master to sort last - neither fits a plain per-track integer without
        // an encoding). Resolution (documented deviation from a literal
        // 1:1 IP_TRACKNUMBER mapping, see this phase's report): every normal
        // track's order is IP_TRACKNUMBER*2; a take FX's order is its parent
        // track's IP_TRACKNUMBER*2 + 1, which sorts immediately after the
        // parent (before the next track's *2) and can never collide with a
        // track-FX instance's own order. Master and the rare
        // API-lookup-failed fallbacks use large sentinels below Master's,
        // which is always last.
        static constexpr int kMasterOrder       = 0x7FFFFFFF; // always sorts last
        static constexpr int kUnknownOrder      = 0x7FFFFFFE; // Identified track, but IP_TRACKNUMBER unavailable
        static constexpr int kTakeFallbackOrder = 0x7FFFFFFD; // take FX whose parent track number is unavailable

        struct Entry
        {
            // Process-unique, stable for the object's lifetime (this
            // AudioProcessor's `this` pointer) - never dereferenced.
            const void* instanceKey = nullptr;

            // false once unregistered / before first registration - array
            // slot becomes reusable. Set last (release) at the end of
            // registration, checked first (acquire) by every scan - see the
            // .cpp for why this ordering is safe without a scan-side lock.
            std::atomic<bool> active { false };

            // Message-thread-only (architecture.md). Written every ~2Hz tick
            // by this instance's own ProjectResolver; read by every
            // instance's own message-thread code (Editor Bridge native
            // functions, the debug registry dump). Never touched by the
            // audio thread. See this class's .cpp for the single-message-
            // thread argument for why this needs no additional locking
            // beyond the `active` publish/observe above.
            ProjectKey projectKey;
            juce::String trackId;
            juce::String trackName;
            std::atomic<int> trackOrder { -1 };

            // References (never owns) the registering instance's own
            // MeteringEngine::snapshot (MeteringEngine::getSnapshotPointer()),
            // per this phase's explicit instruction not to duplicate or move
            // the Phase 2.1 snapshot. Audio thread continues writing it
            // exactly as before (relaxed atomic stores); this pointer is only
            // ever read (relaxed atomic loads) by message-thread code.
            const SharedMeterSnapshot* snapshot = nullptr;

            // Cross-instance WRITE callbacks (architecture.md's "Cross-
            // Instance Writes"). Each is registered by the OWNING instance at
            // construction time (closures over that instance's own `this`)
            // and invoked only by OTHER instances, only on the message
            // thread, only after a same-partition findEntry() lookup.
            std::function<void (bool)> remoteSetLinked;
            std::function<void (bool)> remoteSetMono;
            std::function<void (const juce::String&)> remoteResetOne; // arg = measurement key (currently ignored - see PluginProcessor.cpp)
            std::function<void()> remoteResetAll;

            // Part C addition: invoked (no args) to ask THIS entry's owning
            // instance to flush its own project partition's current
            // SharedProjectConfig to ProjExtState through its own
            // ReaperHostBridge (see ProjectConfigPersistence.h). Registered
            // by the owning instance at construction, same pattern as the
            // four callbacks above. Used by the debug command-file mechanism
            // (Source/Host/RegistryDumpLogger.cpp, METERSCOPE_REGISTRY_DUMP_LOG
            // only) so a config edit issued "from" a given track's instance
            // persists through that same instance's own bridge - the same
            // code path a future Editor Bridge write handler (Stage 3 Phase
            // 3.3) would use. A no-op if the entry's partition is not
            // Identified or Tier B is unavailable (see the owning
            // instance's own lambda in PluginProcessor.cpp).
            std::function<void()> remoteFlushConfigToProjExtState;

            // Phase 3.3 additions.
            //
            // Cross-instance-READABLE view of this instance's two Track-scope
            // parameters (linked, monoMode), for getTrackList()/trackConfigUpdate.
            // These point at the owning instance's own APVTS raw-value atomics
            // (AudioProcessorValueTreeState::getRawParameterValue): lock-free
            // relaxed reads, always current, never a second copy to keep in
            // sync (this supersedes the trackLinked/trackMono mirrors in
            // SharedProjectConfig, which stay as-is but unused). Valid for the
            // registering instance's lifetime; cleared on unregister.
            const std::atomic<float>* linkedRaw = nullptr;
            const std::atomic<float>* monoRaw = nullptr;

            // Cross-instance write of ONE Linked-scope APVTS parameter on this
            // instance (raw/denormalised value). Same pattern, thread rules and
            // lookup rule as the four callbacks above; used by the combined
            // view's per-track settings panel for an unlinked track. The
            // owner whitelists parameter ids.
            std::function<void (const juce::String&, float)> remoteSetParam;
        };

        static SharedMeterRegistry& getInstance();

        // Message-thread only. Claims a free slot, resets it to a fresh
        // Pending-partition, no-callbacks state, and returns a pointer valid
        // for the registering instance's entire lifetime (until its matching
        // unregisterInstance() call). Returns nullptr (and asserts) only if
        // kMaxInstances is exhausted.
        Entry* registerInstance (const void* instanceKey, const SharedMeterSnapshot* snapshot);

        // Message-thread only. Safe to call even if instanceKey was never
        // registered (no-op).
        void unregisterInstance (const void* instanceKey);

        // Iteration for the combined view / instance navigation - lock-free
        // (only an atomic `active` check per slot; see .cpp). Sorted
        // ascending by trackOrder. Deliberately the ONLY two read accessors -
        // both require a ProjectKey; there is no way to enumerate every
        // instance regardless of partition through this public API.
        std::vector<Entry*> getActiveEntriesSortedByOrder (const ProjectKey& project);
        Entry* findEntry (const ProjectKey& project, const juce::String& trackId);

        // Fallback tier (architecture.md's REAPER Host Integration ->
        // Fallback): "a monotonically increasing 'discovery order' counter,
        // incremented each time a new instance registers". In this
        // implementation the counter is drawn the first time an instance
        // actually resolves into the Unidentified partition (see
        // ProjectResolver::becomeUnidentified()), not at registerInstance()
        // itself - operationally equivalent for the Unidentified pool's own
        // ordering purposes, and avoids burning counter values on instances
        // that resolve to Identified within their first tick. Process-wide,
        // monotonic, never reused even as slots free up.
        static int nextDiscoveryOrder() noexcept;

       #if METERSCOPE_REGISTRY_DUMP_LOG
        // Debug-only escape hatch behind METERSCOPE_REGISTRY_DUMP_LOG (this
        // phase's task item 5). NEVER used by production Editor Bridge code -
        // that code always goes through getActiveEntriesSortedByOrder()/
        // findEntry(), both of which require a ProjectKey. This one
        // deliberately returns every active entry across every partition, so
        // RegistryDumpLogger can print one line per partition.
        std::vector<const Entry*> getAllActiveEntriesForDebugDump() const;
       #endif

        // ------------------------------------------------------------------
        // Part C: projectConfigs (architecture.md's `std::map<ProjectKey,
        // SharedProjectConfig> projectConfigs`). Message-thread only,
        // mutex-guarded (`projectConfigLock`, separate from
        // `registrationLock` - see architecture.md's Architecture Decisions
        // -> "Threading Model for SharedProjectConfig: Mutex vs. Fully
        // Lock-Free"), never touched by the audio thread. No unfiltered
        // accessor exists - same rule as the Entry registry above. `key`
        // must be Identified or Unidentified; Pending keys are per-instance-
        // unique (ProjectKey.h) and are never a real partition, so calling
        // these with a Pending key would silently create a config no other
        // instance could ever reach - callers assert this.
        //
        // Storage note (documented deviation from architecture.md's own
        // illustrative `std::map<ProjectKey, SharedProjectConfig>` snippet,
        // same kind of deviation already recorded for Part B's ProjectKey.h
        // placement): implemented as a linear-scan `std::vector`, not a
        // `std::map`, because ProjectKey (deliberately, per Part B) only
        // defines `operator==`/`!=`, not `operator<` - adding ordering to a
        // key whose only documented semantic is equality felt like a bigger
        // surface change than justified here. The number of live project
        // partitions is always tiny (REAPER project-tab count, realistically
        // single digits) versus `kMaxInstances=128` Entries, and this is not
        // a per-audio-block hot path (RegistryDumpLogger's own O(n^2)
        // partition grouping already accepts the same trade-off) - so a
        // linear scan is a correct, simple, and cheap choice.
        //
        // Pointer-escape correctness fix (orchestrator review, post-Part-C):
        // `projectConfigs` is a `std::vector`, so `emplace_back()` (a newly
        // seen partition) or `erase()` (eraseProjectConfigIfOrphaned() on tab
        // close) can reallocate/relocate its elements at any time. The
        // original Part C API returned `SharedProjectConfig&`/`*` directly
        // into that vector with the lock already released on return - a
        // dangling-pointer/data-race risk, made concrete by
        // getStateInformation() (which the VST3 host may call off the
        // message thread, concurrently with message-thread mutation). FIX:
        // no pointer or reference into `projectConfigs` is ever allowed to
        // escape `projectConfigLock` any more. `getOrCreateProjectConfig()`
        // is now PRIVATE (only ever called from within an already-locked
        // member function). External code must use one of:
        //   - `getProjectConfigCopy()` - a safe read-only COPY, or
        //   - `withProjectConfig()` - a locked read/mutate callback, for
        //     code (e.g. ProjectConfigPersistence.cpp's merge/seed) that
        //     needs to inspect-then-mutate atomically.
        // ------------------------------------------------------------------

        // Phase 3.3: cheap change detector for the editors' 30Hz poll. Returns
        // the config's current version counter (no copy), or std::nullopt if
        // `key` has no config.
        std::optional<juce::int64> getProjectConfigVersion (const ProjectKey& key) const;

        // Safe read: a copy of the config taken while holding
        // projectConfigLock, or std::nullopt if `key` has no config yet.
        // Never returns a pointer/reference into projectConfigs. `const` (and
        // therefore callable off the message thread, e.g. from
        // getStateInformation() - the lock itself makes this safe; nothing
        // in `SharedProjectConfig`'s copy constructor touches the audio
        // thread or blocks unboundedly).
        std::optional<SharedProjectConfig> getProjectConfigCopy (const ProjectKey& key) const;

        // Safe read-modify-write: runs `fn(SharedProjectConfig&)` exactly
        // once, synchronously, while holding projectConfigLock - the ONLY
        // way to mutate a config from OUTSIDE this class (the 8 dedicated
        // mutators below already do their own locked read-modify-write and
        // don't need this). `fn` must not itself call back into this
        // registry's projectConfigLock-guarded API (the lock is not
        // recursive-safe across a callback that re-enters conceptually
        // different logic - it IS reentrant for plain nested calls, but
        // `fn` should still just read/mutate `cfg` and copy out whatever it
        // needs before returning, per ProjectConfigPersistence.cpp's usage).
        // If `createIfMissing` is false and `key` has no config, `fn` is
        // never called. Message-thread only (same rule as the mutators).
        template <typename Fn>
        void withProjectConfig (const ProjectKey& key, bool createIfMissing, Fn&& fn)
        {
            const juce::ScopedLock sl (projectConfigLock);

            if (createIfMissing)
            {
                fn (getOrCreateProjectConfig (key)); // reentrant lock - safe, see projectConfigLock's doc comment
                return;
            }

            for (auto& kv : projectConfigs)
            {
                if (kv.first == key)
                {
                    fn (kv.second);
                    return;
                }
            }
        }

        // Erases a partition's config (architecture.md: "A partition's
        // config is erased when its last instance unregisters (tab close)").
        // Called internally by unregisterInstance(); public only so it is
        // independently exercisable. Safe no-op if `key` has no config.
        void eraseProjectConfigIfOrphaned (const ProjectKey& key);

        // Part C mutation entry points - the SAME in-memory code path a
        // future Editor Bridge write handler (Stage 3 Phase 3.3's
        // setSharedViewState()/setTrackLinked() etc.) will call. Exposed now
        // only so the debug command-file mechanism (RegistryDumpLogger.cpp,
        // METERSCOPE_REGISTRY_DUMP_LOG-only) can exercise plan.md's T2/T4
        // before the WebView editor exists. Each bumps `version`/
        // `timestampMs`. Deliberately REAPER-API-agnostic (no
        // ProjExtState write here) - see ProjectConfigPersistence.h and
        // Entry::remoteFlushConfigToProjExtState for that separate step.
        void setGlobalLinkedParam (const ProjectKey& key, const juce::String& paramId, float rawValue);
        void setUnlinkedTrackParam (const ProjectKey& key, const juce::String& trackId, const juce::String& paramId, float rawValue);
        juce::String createGroup (const ProjectKey& key, const juce::String& name); // returns the new group's id
        void setTrackGroup (const ProjectKey& key, const juce::String& trackId, const juce::String& groupId);
        void setTrackHidden (const ProjectKey& key, const juce::String& trackId, bool hidden);
        void setSoloTrack (const ProjectKey& key, const juce::String& trackId); // "" clears solo
        void setTrackMonoMirror (const ProjectKey& key, const juce::String& trackId, bool mono);
        void setTrackLinkedMirror (const ProjectKey& key, const juce::String& trackId, bool linked);

        // Phase 3.3 write entry points (message thread, same locking rules).
        // setSharedViewState: replaces the Shared View State (groups,
        // groupFilter - which becomes explicit -, soloTrack, hiddenTracks and
        // the supplied trackGroup entries) in one locked step. Bumps the
        // version only if something actually changed; returns true in that
        // case. `key` must not be Pending.
        bool setSharedViewState (const ProjectKey& key, const SharedViewStateUpdate& update);

        // seedGlobalLinkedParams: inserts each id of `values` into
        // globalLinkedConfig ONLY if the store has no value for it yet (never
        // overwrites). One version bump if anything was added; returns true in
        // that case. Keeps the global map complete, so relinking can adopt
        // every id. `key` must not be Pending.
        bool seedGlobalLinkedParams (const ProjectKey& key, const std::map<juce::String, float>& values);

        // setUnlinkedTrackConfig: replaces the whole own-config copy of ONE
        // unlinked track (bulk, so unlinking costs a single version bump).
        // Bumps only if the stored copy actually changes.
        void setUnlinkedTrackConfig (const ProjectKey& key, const juce::String& trackId,
                                     const std::map<juce::String, float>& values);

    private:
        SharedMeterRegistry() = default;

        // Guards ONLY the register/unregister slot-claim/slot-release
        // operations (architecture.md: "guarded by a plain std::mutex").
        // Never touched by the audio thread. Read accessors below do not
        // take this lock (see .cpp).
        juce::CriticalSection registrationLock;

        std::array<Entry, kMaxInstances> entries;

        // Part C: guards projectConfigs only (see the public accessors'
        // doc comment above for why this is a separate, reentrant
        // (juce::CriticalSection) lock from registrationLock). `mutable` so
        // the const getProjectConfigCopy() (safe to call off the message
        // thread - see its own doc comment) can still lock it.
        mutable juce::CriticalSection projectConfigLock;
        std::vector<std::pair<ProjectKey, SharedProjectConfig>> projectConfigs;

        // PRIVATE (post-review fix): returns a reference into `projectConfigs`
        // - only ever safe to call from within a function that ALREADY holds
        // projectConfigLock for the reference's entire remaining lifetime
        // (every one of the 8 setX() mutators below, and withProjectConfig()
        // above). Never let this reference escape a locked scope - that was
        // exactly the bug this review fixed. External callers must use
        // getProjectConfigCopy() or withProjectConfig() instead.
        SharedProjectConfig& getOrCreateProjectConfig (const ProjectKey& key);

        JUCE_DECLARE_NON_COPYABLE (SharedMeterRegistry)
    };

    // Phase 3.3: the track id string the WebView uses for an Entry. Identical to
    // Entry::trackId once resolved; while the entry is still Pending (trackId
    // empty) a placeholder derived from the instance key, so the instance's own
    // editor always has a stable, non-empty id to work with until resolution
    // (the editor then reports the id change via a "trackListUpdate" event).
    inline juce::String uiTrackIdFor (const SharedMeterRegistry::Entry& e)
    {
        return e.trackId.isNotEmpty()
                   ? e.trackId
                   : juce::String ("pending_") + juce::String::toHexString ((juce::pointer_sized_int) e.instanceKey);
    }
}
