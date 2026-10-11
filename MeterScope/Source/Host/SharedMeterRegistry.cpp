// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#include "SharedMeterRegistry.h"

#include <algorithm>

// Thread-safety argument (also summarised in this phase's JSON report):
//
// 1. registerInstance()/unregisterInstance() are the only two operations that
//    mutate the fixed array's STRUCTURE (claim/release a slot). Both run only
//    on the message thread (juce::AudioProcessor construction/destruction is
//    message-thread work in JUCE - architecture.md's own Host Integration
//    Threading note) and are serialised by `registrationLock`
//    (juce::CriticalSection), matching architecture.md's literal
//    specification ("guarded by a plain std::mutex").
//
// 2. getActiveEntriesSortedByOrder()/findEntry() (the read path used by every
//    Editor Bridge native-function handler, all message-thread-only per
//    architecture.md's Thread Boundaries) do NOT take registrationLock. This
//    matches architecture.md's explicit design ("no lock of any kind on the
//    snapshot path"). Safety instead comes from a publish/observe pattern on
//    each Entry's `active` flag: registerInstance() writes every other Entry
//    field FIRST, then publishes `active.store(true, memory_order_release)`
//    LAST; every reader checks `active.load(memory_order_acquire)` FIRST,
//    before reading any other field. The release/acquire pair guarantees a
//    reader that observes `active == true` also sees every field write that
//    happened-before it, even under a hypothetical multi-threaded host bridge
//    - in REAPER's actual (and only supported) in-process configuration, the
//    message thread is a single OS thread shared by every loaded instance, so
//    there is no true concurrency here at all: registerInstance() on
//    instance A and getActiveEntriesSortedByOrder() on instance B's own timer
//    are always sequenced, never simultaneous.
//
// 3. Entry::projectKey/trackId/trackName/trackOrder are written every ~2Hz
//    tick by that Entry's OWNING instance's own ProjectResolver (message
//    thread) and read by every OTHER instance's own message-thread code
//    (Editor Bridge natives, the debug dump). Per point 2's single-message-
//    thread argument, these writes and reads are never simultaneous, so the
//    plain (non-atomic) juce::String fields are safe without a lock; trackOrder
//    is additionally kept as std::atomic<int> per architecture.md's literal
//    Entry struct, which costs nothing and adds defence-in-depth.
//
// 4. Entry::snapshot points at that instance's own MeteringEngine::snapshot,
//    which the AUDIO thread writes via relaxed atomic stores (unchanged since
//    Phase 2.1 - see MeteringEngine::publishSnapshot()). This registry never
//    writes through that pointer and never touches it from the audio thread;
//    it only hands the pointer to message-thread readers, who perform their
//    own relaxed atomic loads on the individual SharedMeterSnapshot fields
//    (exactly as Phase 2.1 already does for this instance's own editor).
//
// 5. Cross-instance WRITE callbacks (remoteSetLinked/remoteSetMono/
//    remoteResetOne/remoteResetAll) are std::function closures over their
//    OWNING instance's own `this`, registered once at construction (message
//    thread) and invoked only by another instance's message-thread code
//    after a same-partition findEntry() lookup - message-thread-to-message-
//    thread, per point 2's argument, never concurrent with anything.

namespace meterscope
{
    SharedMeterRegistry& SharedMeterRegistry::getInstance()
    {
        static SharedMeterRegistry instance; // function-local static, thread-safe init (C++11)
        return instance;
    }

    SharedMeterRegistry::Entry* SharedMeterRegistry::registerInstance (const void* instanceKey,
                                                                        const SharedMeterSnapshot* snapshot)
    {
        const juce::ScopedLock sl (registrationLock);

        for (auto& e : entries)
        {
            if (e.active.load (std::memory_order_relaxed))
                continue;

            // Reset to a fresh, no-callbacks, Pending-partition state before
            // publishing. The caller (PluginProcessor) is responsible for
            // immediately setting e.projectKey to a self-unique Pending key
            // (see ProjectKey.h's field-semantics doc) and for registering
            // the remote* callbacks right after this call returns, per
            // architecture.md ("wired at AudioProcessor construction time,
            // before any editor for any instance can safely call them").
            e.instanceKey = instanceKey;
            e.snapshot = snapshot;
            e.projectKey = ProjectKey {};
            e.trackId.clear();
            e.trackName.clear();
            e.trackOrder.store (-1, std::memory_order_relaxed);
            e.remoteSetLinked = nullptr;
            e.remoteSetMono = nullptr;
            e.remoteResetOne = nullptr;
            e.remoteResetAll = nullptr;
            e.remoteFlushConfigToProjExtState = nullptr;
            e.linkedRaw = nullptr;
            e.monoRaw = nullptr;
            e.remoteSetParam = nullptr;

            e.active.store (true, std::memory_order_release); // publish LAST - see thread-safety argument above
            return &e;
        }

        // kMaxInstances exhausted - should never happen in any realistic
        // REAPER session (architecture.md). Fail safe: no crash, caller gets
        // nullptr and simply has no registry presence (its own THIS-TRACK
        // measurement is entirely unaffected, since that never depends on
        // this registry - architecture.md's Multi-Instance Shared Store risk
        // note).
        jassertfalse;
        return nullptr;
    }

    void SharedMeterRegistry::unregisterInstance (const void* instanceKey)
    {
        ProjectKey removedKey;
        bool removed = false;

        {
            const juce::ScopedLock sl (registrationLock);

            for (auto& e : entries)
            {
                if (e.active.load (std::memory_order_relaxed) && e.instanceKey == instanceKey)
                {
                    removedKey = e.projectKey;
                    removed = true;

                    // Publish inactive FIRST (release), so any reader that later
                    // observes active==false never looks at the fields we are
                    // about to clear below - readers that already observed
                    // active==true before this store may still be mid-read of a
                    // valid snapshot, which is fine (single-message-thread: that
                    // can only happen if we ourselves are that reader).
                    e.active.store (false, std::memory_order_release);
                    e.instanceKey = nullptr;
                    e.snapshot = nullptr;
                    e.remoteSetLinked = nullptr;
                    e.remoteSetMono = nullptr;
                    e.remoteResetOne = nullptr;
                    e.remoteResetAll = nullptr;
                    e.remoteFlushConfigToProjExtState = nullptr;
                    e.linkedRaw = nullptr;
                    e.monoRaw = nullptr;
                    e.remoteSetParam = nullptr;
                    break;
                }
            }

            if (removed)
            {
                // Part C: "A partition's config is erased when its last
                // instance unregisters (tab close)" - check while still
                // holding registrationLock (cheap, kMaxInstances=128 max)
                // whether any OTHER active entry still shares removedKey's
                // partition before erasing its config.
                bool anyRemaining = false;

                for (auto& e : entries)
                {
                    if (e.active.load (std::memory_order_relaxed) && e.projectKey == removedKey)
                    {
                        anyRemaining = true;
                        break;
                    }
                }

                if (! anyRemaining)
                    eraseProjectConfigIfOrphaned (removedKey); // takes projectConfigLock, a different mutex - no deadlock risk
            }
        }
    }

    std::vector<SharedMeterRegistry::Entry*> SharedMeterRegistry::getActiveEntriesSortedByOrder (const ProjectKey& project)
    {
        std::vector<Entry*> result;
        result.reserve (kMaxInstances);

        for (auto& e : entries)
        {
            if (! e.active.load (std::memory_order_acquire))
                continue;

            if (e.projectKey == project)
                result.push_back (&e);
        }

        std::sort (result.begin(), result.end(), [] (const Entry* a, const Entry* b)
        {
            return a->trackOrder.load (std::memory_order_relaxed) < b->trackOrder.load (std::memory_order_relaxed);
        });

        return result;
    }

    SharedMeterRegistry::Entry* SharedMeterRegistry::findEntry (const ProjectKey& project, const juce::String& trackId)
    {
        for (auto& e : entries)
        {
            if (! e.active.load (std::memory_order_acquire))
                continue;

            if (e.projectKey == project && e.trackId == trackId)
                return &e;
        }

        return nullptr;
    }

    int SharedMeterRegistry::nextDiscoveryOrder() noexcept
    {
        static std::atomic<int> counter { 0 }; // function-local static, thread-safe init; process-wide, never reset
        return counter.fetch_add (1, std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------
    // Part C: projectConfigs
    // ------------------------------------------------------------------

    namespace
    {
        void bumpVersion (SharedProjectConfig& cfg)
        {
            ++cfg.version;
            cfg.timestampMs = juce::Time::currentTimeMillis();
        }
    }

    SharedProjectConfig& SharedMeterRegistry::getOrCreateProjectConfig (const ProjectKey& key)
    {
        jassert (key.scope != ProjectKey::Scope::Pending); // Pending keys are per-instance-unique, never a real partition - see ProjectKey.h

        const juce::ScopedLock sl (projectConfigLock);

        for (auto& kv : projectConfigs)
            if (kv.first == key)
                return kv.second;

        projectConfigs.emplace_back (key, SharedProjectConfig{});
        return projectConfigs.back().second;
    }

    std::optional<SharedProjectConfig> SharedMeterRegistry::getProjectConfigCopy (const ProjectKey& key) const
    {
        const juce::ScopedLock sl (projectConfigLock);

        for (auto& kv : projectConfigs)
            if (kv.first == key)
                return kv.second; // copy - never a pointer/reference into projectConfigs

        return std::nullopt;
    }

    std::optional<juce::int64> SharedMeterRegistry::getProjectConfigVersion (const ProjectKey& key) const
    {
        const juce::ScopedLock sl (projectConfigLock);

        for (auto& kv : projectConfigs)
            if (kv.first == key)
                return kv.second.version;

        return std::nullopt;
    }

    void SharedMeterRegistry::eraseProjectConfigIfOrphaned (const ProjectKey& key)
    {
        const juce::ScopedLock sl (projectConfigLock);

        for (auto it = projectConfigs.begin(); it != projectConfigs.end(); ++it)
        {
            if (it->first == key)
            {
                projectConfigs.erase (it);
                return;
            }
        }
    }

    void SharedMeterRegistry::setGlobalLinkedParam (const ProjectKey& key, const juce::String& paramId, float rawValue)
    {
        const juce::ScopedLock sl (projectConfigLock); // reentrant - getOrCreateProjectConfig() below takes it again
        auto& cfg = getOrCreateProjectConfig (key);
        cfg.globalLinkedConfig.values[paramId] = rawValue;
        bumpVersion (cfg);
    }

    void SharedMeterRegistry::setUnlinkedTrackParam (const ProjectKey& key, const juce::String& trackId, const juce::String& paramId, float rawValue)
    {
        const juce::ScopedLock sl (projectConfigLock);
        auto& cfg = getOrCreateProjectConfig (key);
        cfg.unlinkedTrackConfigs[trackId].values[paramId] = rawValue;
        bumpVersion (cfg);
    }

    juce::String SharedMeterRegistry::createGroup (const ProjectKey& key, const juce::String& name)
    {
        const juce::ScopedLock sl (projectConfigLock);
        auto& cfg = getOrCreateProjectConfig (key);

        // juce::Uuid guarantees uniqueness trivially (same identity-minting
        // tool already used elsewhere in Part B's Fallback tier) - no
        // separate per-config counter/collision handling needed.
        const auto id = juce::Uuid().toString().substring (0, 8);
        cfg.groups.push_back ({ id, name });
        bumpVersion (cfg);
        return id;
    }

    void SharedMeterRegistry::setTrackGroup (const ProjectKey& key, const juce::String& trackId, const juce::String& groupId)
    {
        const juce::ScopedLock sl (projectConfigLock);
        auto& cfg = getOrCreateProjectConfig (key);

        if (groupId.isEmpty())
            cfg.trackGroup.erase (trackId);
        else
            cfg.trackGroup[trackId] = groupId;

        bumpVersion (cfg);
    }

    void SharedMeterRegistry::setTrackHidden (const ProjectKey& key, const juce::String& trackId, bool hidden)
    {
        const juce::ScopedLock sl (projectConfigLock);
        auto& cfg = getOrCreateProjectConfig (key);

        if (hidden)
            cfg.hiddenTracks.insert (trackId);
        else
            cfg.hiddenTracks.erase (trackId);

        bumpVersion (cfg);
    }

    void SharedMeterRegistry::setSoloTrack (const ProjectKey& key, const juce::String& trackId)
    {
        const juce::ScopedLock sl (projectConfigLock);
        auto& cfg = getOrCreateProjectConfig (key);
        cfg.soloTrack = trackId; // "" clears
        bumpVersion (cfg);
    }

    void SharedMeterRegistry::setTrackMonoMirror (const ProjectKey& key, const juce::String& trackId, bool mono)
    {
        const juce::ScopedLock sl (projectConfigLock);
        auto& cfg = getOrCreateProjectConfig (key);
        cfg.trackMono[trackId] = mono;
        bumpVersion (cfg);
    }

    void SharedMeterRegistry::setTrackLinkedMirror (const ProjectKey& key, const juce::String& trackId, bool linked)
    {
        const juce::ScopedLock sl (projectConfigLock);
        auto& cfg = getOrCreateProjectConfig (key);
        cfg.trackLinked[trackId] = linked;
        bumpVersion (cfg);
    }

    bool SharedMeterRegistry::setSharedViewState (const ProjectKey& key, const SharedViewStateUpdate& update)
    {
        jassert (key.scope != ProjectKey::Scope::Pending);

        if (key.scope == ProjectKey::Scope::Pending)
            return false;

        const juce::ScopedLock sl (projectConfigLock);
        auto& cfg = getOrCreateProjectConfig (key);

        // Build the would-be new state first, compare, and only then commit,
        // so an echo of an unchanged state never bumps the version.
        std::vector<SharedProjectConfig::Group> newGroups;
        std::set<juce::String> groupIds;

        for (auto& g : update.groups)
        {
            if (g.id.isEmpty() || groupIds.count (g.id) > 0)
                continue;

            groupIds.insert (g.id);
            newGroups.push_back ({ g.id, g.name.trim() });
        }

        // trackGroup: apply the UI's entries ("" = clear), keep the entries of
        // tracks the UI did not mention, then drop any whose group is gone.
        auto newTrackGroup = cfg.trackGroup;

        for (auto& kv : update.trackGroup)
        {
            if (kv.second.isEmpty())
                newTrackGroup.erase (kv.first);
            else
                newTrackGroup[kv.first] = kv.second;
        }

        for (auto it = newTrackGroup.begin(); it != newTrackGroup.end();)
        {
            if (groupIds.count (it->second) == 0)
                it = newTrackGroup.erase (it);
            else
                ++it;
        }

        std::set<juce::String> newFilter;

        for (auto& f : update.groupFilter)
            if (f == "no-group" || groupIds.count (f) > 0)
                newFilter.insert (f);

        bool groupsSame = newGroups.size() == cfg.groups.size();

        if (groupsSame)
            for (size_t i = 0; i < newGroups.size(); ++i)
                if (newGroups[i].id != cfg.groups[i].id || newGroups[i].name != cfg.groups[i].name)
                    groupsSame = false;

        const bool unchanged = groupsSame
                               && cfg.groupFilterExplicit
                               && newFilter == cfg.groupFilter
                               && newTrackGroup == cfg.trackGroup
                               && update.soloTrack == cfg.soloTrack
                               && update.hiddenTracks == cfg.hiddenTracks;

        if (unchanged)
            return false;

        cfg.groups = std::move (newGroups);
        cfg.groupFilter = std::move (newFilter);
        cfg.groupFilterExplicit = true;
        cfg.trackGroup = std::move (newTrackGroup);
        cfg.soloTrack = update.soloTrack;
        cfg.hiddenTracks = update.hiddenTracks;
        bumpVersion (cfg);
        return true;
    }

    bool SharedMeterRegistry::seedGlobalLinkedParams (const ProjectKey& key, const std::map<juce::String, float>& values)
    {
        jassert (key.scope != ProjectKey::Scope::Pending);

        if (key.scope == ProjectKey::Scope::Pending)
            return false;

        const juce::ScopedLock sl (projectConfigLock);
        auto& cfg = getOrCreateProjectConfig (key);
        bool added = false;

        for (auto& [id, value] : values)
            added = cfg.globalLinkedConfig.values.emplace (id, value).second || added;

        if (added)
            bumpVersion (cfg);

        return added;
    }

    void SharedMeterRegistry::setUnlinkedTrackConfig (const ProjectKey& key, const juce::String& trackId,
                                                      const std::map<juce::String, float>& values)
    {
        jassert (key.scope != ProjectKey::Scope::Pending);

        if (key.scope == ProjectKey::Scope::Pending)
            return;

        const juce::ScopedLock sl (projectConfigLock);
        auto& cfg = getOrCreateProjectConfig (key);
        auto it = cfg.unlinkedTrackConfigs.find (trackId);

        if (it != cfg.unlinkedTrackConfigs.end() && it->second.values == values)
            return;

        cfg.unlinkedTrackConfigs[trackId].values = values;
        bumpVersion (cfg);
    }

   #if METERSCOPE_REGISTRY_DUMP_LOG
    std::vector<const SharedMeterRegistry::Entry*> SharedMeterRegistry::getAllActiveEntriesForDebugDump() const
    {
        std::vector<const Entry*> result;
        result.reserve (kMaxInstances);

        for (auto& e : entries)
            if (e.active.load (std::memory_order_acquire))
                result.push_back (&e);

        return result;
    }
   #endif
}
