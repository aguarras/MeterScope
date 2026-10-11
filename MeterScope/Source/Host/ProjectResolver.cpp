// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#include "ProjectResolver.h"

namespace meterscope
{
    namespace
    {
        // REAPER extension API function-pointer signatures, exactly as given
        // by this task (REAPER's own reaper_plugin_functions.h - not part of
        // the JUCE/VST3 SDK, so these cannot be verified against C:\JUCE; see
        // this phase's report). Every pointer obtained through getReaperApi()
        // is null-checked at every call site below before use.
        using GetMediaTrackInfo_Value_t        = double (*) (void* /*MediaTrack*/, const char*);
        using GetSetMediaTrackInfo_String_t    = bool (*) (void* /*MediaTrack*/, const char*, char*, bool);
        using GetSetMediaItemTakeInfo_String_t = bool (*) (void* /*MediaItem_Take*/, const char*, char*, bool);
        using GetMediaItemTake_Track_t         = void* /*MediaTrack*/ (*) (void* /*MediaItem_Take*/);

        template <typename FnPtr>
        FnPtr getReaperFn (reaperhost::IReaperHostApplication& api, const char* name)
        {
            return reinterpret_cast<FnPtr> (api.getReaperApi (name));
        }

        juce::String pointerToHex (const void* ptr)
        {
            return juce::String::toHexString ((juce::pointer_sized_int) ptr);
        }

        // Part B follow-up: the per-instance METERSCOPE_FORCE_UNIDENTIFIED_PROJECT
        // tag check (see ProjectResolver.h's doc comment on the constructor's
        // `forceUnidentifiedProject` parameter).
        bool containsForceUnidentifiedTag (const juce::String& resolvedName)
        {
            return resolvedName.containsIgnoreCase ("[UNID]");
        }
    }

    ProjectResolver::ProjectResolver (ReaperHostBridge& bridge,
                                       SharedMeterRegistry::Entry& entryToResolve,
                                       std::function<juce::String()> getHostTrackName,
                                       bool forceUnidentifiedProject_,
                                       juce::String& fallbackTrackIdRef_,
                                       std::function<const SharedProjectConfig&()> getOwnRedundantConfigCopy_)
        : reaperHostBridge (bridge)
        , entry (entryToResolve)
        , hostTrackNameProvider (std::move (getHostTrackName))
        , forceUnidentifiedProject (forceUnidentifiedProject_)
        , constructionTime (juce::Time::getCurrentTime())
        , fallbackTrackIdRef (fallbackTrackIdRef_)
        , getOwnRedundantConfigCopy (std::move (getOwnRedundantConfigCopy_))
    {
        // Pending key, unique to THIS instance (see ProjectKey.h's field-
        // semantics doc: `reaProject` holds the owning instance's own
        // identity while Pending, never a real ReaProject*, never
        // dereferenced) - implements "a Pending instance's own... ALL TRACKS
        // view shows only itself" by plain ProjectKey equality, no extra
        // machinery needed.
        entry.projectKey = ProjectKey { ProjectKey::Scope::Pending, entry.instanceKey };
        lastConfigMergeKey = entry.projectKey; // Pending is never merged - see resolveOnce()/becomeUnidentified()

        // "running even when no editor is open" (plan.md) - started here,
        // from the owning processor's own construction, not from
        // createEditor(). ~2Hz per this phase's task ("about 2Hz").
        startTimer (500);

        // Part C change (deliberate deviation from Part B): the previous
        // code here called resolveOnce() synchronously, before this
        // constructor returns, "so an instance that never has processBlock
        // called still gets its first resolution attempt promptly". That is
        // now REMOVED: MeterScopeAudioProcessor::setStateInformation() (which
        // populates fallbackTrackIdRef/getOwnRedundantConfigCopy's data) is
        // called by the host strictly AFTER this constructor already
        // returned, so an immediate synchronous resolve here could reach
        // becomeUnidentified() (monitoring FX resolves to Unidentified on its
        // very first check, with no Pending timeout at all - see below) and
        // mint a brand-new fallback identity before the persisted one has
        // had any chance to load, breaking cross-session identity
        // persistence for exactly the instances this feature exists for.
        // Waiting for the timer's own first ~500ms tick instead gives a
        // host's typical synchronous "construct, then restore state"
        // sequence time to complete first; the ~2s Pending timeout below
        // easily absorbs this extra delay, and no already-passed Part B test
        // (T1/T5/T6/T8/T9) depends on sub-500ms resolution timing.
    }

    ProjectResolver::~ProjectResolver()
    {
        stopTimer();
    }

    void ProjectResolver::timerCallback()
    {
        resolveOnce();

        // Phase 3.3: lets the owning processor piggy-back its shared-config
        // reconcile/flush on this existing ~2Hz message-thread tick (no new
        // timer) so linked instances stay in sync even with no editor open.
        if (onTick)
            onTick();
    }

    void ProjectResolver::resolveOnce()
    {
        auto* app = reaperHostBridge.getReaperHostApplication();

        if (app == nullptr)
        {
            // Tier B unavailable - non-REAPER host, sandboxed-process bridge
            // that doesn't forward it, or setIHostApplication() not (yet)
            // called (architecture.md: "may not be called at all").
            if (juce::Time::getCurrentTime() - constructionTime > juce::RelativeTime::seconds (2.0))
                becomeUnidentified();
            return;
        }

        void* trackPtr   = app->getReaperParent (1);
        void* takePtr    = app->getReaperParent (2);
        void* projectPtr = app->getReaperParent (3);

        // Monitoring-FX rule - spike-confirmed correction to architecture.md
        // (2026-09-26): track and take both null -> Unidentified, whatever
        // (3) returns (it returns the currently-active tab and is therefore
        // not trustworthy in this case).
        if (trackPtr == nullptr && takePtr == nullptr)
        {
            becomeUnidentified();
            return;
        }

        if (projectPtr == nullptr)
        {
            // Defensive: a track/take resolved but no project did. Not
            // observed in the spike, but architecture.md's general Fallback
            // rule ("getReaperParent(3) returns nullptr... placed in the
            // Unidentified partition") covers it. Apply the same Pending
            // timeout rather than flipping immediately, in case this is a
            // transient state during host startup.
            if (juce::Time::getCurrentTime() - constructionTime > juce::RelativeTime::seconds (2.0))
                becomeUnidentified();
            return;
        }

        auto* getSetTrackString = getReaperFn<GetSetMediaTrackInfo_String_t> (*app, "GetSetMediaTrackInfo_String");
        auto* getTrackValue     = getReaperFn<GetMediaTrackInfo_Value_t> (*app, "GetMediaTrackInfo_Value");
        auto* getSetTakeString  = getReaperFn<GetSetMediaItemTakeInfo_String_t> (*app, "GetSetMediaItemTakeInfo_String");
        auto* getTakeTrack      = getReaperFn<GetMediaItemTake_Track_t> (*app, "GetMediaItemTake_Track");

        juce::String trackId;
        juce::String displayName;
        int order = SharedMeterRegistry::kUnknownOrder;

        if (takePtr != nullptr)
        {
            // Take FX (spike-confirmed: track is null, take is non-null, the
            // project resolves and stays stable). User decision: Identified,
            // trackId = the take's own GUID, name = "<parent track> (take)",
            // sorts immediately after the parent track.
            if (getSetTakeString != nullptr)
            {
                char guidBuf[64] = {};
                getSetTakeString (takePtr, "GUID", guidBuf, false);
                trackId = juce::String (guidBuf);
            }

            void* parentTrack = (getTakeTrack != nullptr) ? getTakeTrack (takePtr) : nullptr;
            juce::String parentName;
            int parentTrackNumber = -1;

            if (parentTrack != nullptr)
            {
                if (getTrackValue != nullptr)
                    parentTrackNumber = (int) getTrackValue (parentTrack, "IP_TRACKNUMBER");

                if (getSetTrackString != nullptr)
                {
                    char nameBuf[512] = {};
                    getSetTrackString (parentTrack, "P_NAME", nameBuf, false);
                    parentName = juce::String (nameBuf);
                }
            }

            displayName = (parentName.isNotEmpty() ? parentName : juce::String ("Track")) + " (take)";

            // See SharedMeterRegistry.h's trackOrder-encoding doc: parent's
            // own order is parentTrackNumber*2; +1 sorts this take
            // immediately after it, before the next track's *2.
            order = parentTrackNumber >= 0 ? (parentTrackNumber * 2 + 1) : SharedMeterRegistry::kTakeFallbackOrder;

            if (trackId.isEmpty())
                trackId = "take:" + pointerToHex (takePtr); // defensive fallback if GUID lookup unavailable

            // Part B follow-up: the tag check is against the parent track's
            // own (un-suffixed) name, per ProjectResolver.h's doc comment -
            // not against displayName's "(take)"-suffixed form.
            if (forceUnidentifiedProject && containsForceUnidentifiedTag (parentName))
            {
                becomeUnidentified();
                return;
            }
        }
        else
        {
            // Normal track FX or Master (trackPtr != nullptr, takePtr == nullptr).
            int trackNumber = -2; // sentinel: "unknown", distinct from -1 (Master)

            if (getTrackValue != nullptr)
                trackNumber = (int) getTrackValue (trackPtr, "IP_TRACKNUMBER");

            const bool isMaster = (trackNumber == -1);

            if (getSetTrackString != nullptr)
            {
                char guidBuf[64] = {};
                getSetTrackString (trackPtr, "GUID", guidBuf, false);
                trackId = juce::String (guidBuf);
            }

            if (trackId.isEmpty())
                trackId = "track:" + pointerToHex (trackPtr); // defensive fallback

            if (isMaster)
            {
                // User decision: "MASTER" unconditionally (P_NAME is empty
                // for the master track), sorts last, REAPER-only detection.
                // Unaffected by the Part B follow-up's tag check by
                // construction - "MASTER" can never contain "[UNID]" - so no
                // check is added in this branch (see ProjectResolver.h).
                displayName = "MASTER";
                order = SharedMeterRegistry::kMasterOrder;

                // Phase 3.3: Master's REAPER GUID is NOT stable across project
                // loads (observed 3 times on 2026-09-28), and every persisted
                // per-track value in SharedProjectConfig (trackGroup,
                // hiddenTracks, soloTrack, unlinked copies) is keyed on
                // trackId. Master's persisted state must therefore never be
                // keyed on that GUID. A project has exactly one master track
                // and ids are only unique per project partition, so the
                // literal "master" is a stable, collision-free identity.
                trackId = "master";
            }
            else
            {
                const auto hostName = hostTrackNameProvider ? hostTrackNameProvider() : juce::String();

                if (hostName.isNotEmpty())
                {
                    displayName = hostName;
                }
                else if (getSetTrackString != nullptr)
                {
                    char nameBuf[512] = {};
                    getSetTrackString (trackPtr, "P_NAME", nameBuf, false);
                    displayName = juce::String (nameBuf);
                }

                order = trackNumber >= 0 ? (trackNumber * 2) : SharedMeterRegistry::kUnknownOrder;

                // Part B follow-up: per-instance force-Unidentified tag check
                // (see ProjectResolver.h's doc comment).
                if (forceUnidentifiedProject && containsForceUnidentifiedTag (displayName))
                {
                    becomeUnidentified();
                    return;
                }
            }
        }

        // Never cached: every tick recomputes and overwrites these fields
        // unconditionally, so an FX moved to another track, a manual track
        // reorder, or a rename all re-resolve within about one tick
        // (~0.5s) - matches the Tier B spike result exactly. This same
        // unconditional overwrite is also what correctly completes a Part-B-
        // follow-up Unidentified(tag) -> Identified transition (the "[UNID]"
        // tag removed): the tick after removal simply never takes either
        // early-return above, so projectKey/trackId/trackName/trackOrder are
        // all freshly recomputed here from REAPER's own current state,
        // replacing whatever the Unidentified partition had stored - no
        // separate "coming back from Unidentified" code path is needed.
        entry.projectKey = ProjectKey { ProjectKey::Scope::Identified, projectPtr };
        entry.trackId = trackId;
        entry.trackName = displayName;
        entry.trackOrder.store (order, std::memory_order_relaxed);

        // Part C: seed/version-wins-merge this partition's SharedProjectConfig
        // the first time THIS instance ever resolves into it this session -
        // see this method's own doc comment and ProjectConfigPersistence.h.
        // entry.projectKey is reassigned unconditionally every tick above, so
        // this equality check is what limits the merge to actual partition
        // CHANGES (first resolution, or the rare case of the project pointer
        // itself changing).
        if (entry.projectKey != lastConfigMergeKey)
        {
            mergeOrSeedProjectConfigOnPartitionEntry (
                SharedMeterRegistry::getInstance(), entry.projectKey,
                reaperHostBridge.getReaperHostApplication(),
                getOwnRedundantConfigCopy ? getOwnRedundantConfigCopy() : SharedProjectConfig{});

            lastConfigMergeKey = entry.projectKey;
        }
    }

    void ProjectResolver::becomeUnidentified()
    {
        const bool alreadyUnidentified = (entry.projectKey.scope == ProjectKey::Scope::Unidentified);

        // Tier A's updateTrackProperties() name can arrive at any time
        // (independently of Tier B) - keep refreshing it every tick even once
        // Unidentified, unlike the stable fallback identity below.
        const auto hostName = hostTrackNameProvider ? hostTrackNameProvider() : juce::String();

        if (hostName.isNotEmpty())
            entry.trackName = hostName;
        else if (entry.trackName.isEmpty())
            entry.trackName = "Unidentified Track";

        if (alreadyUnidentified)
            return; // fallback identity (trackId/trackOrder) already minted - don't re-mint every tick

        // Fallback tier (architecture.md's REAPER Host Integration ->
        // Fallback): discovery-order counter + minted juce::Uuid, re-minted
        // only on a collision within the same (Unidentified) partition.
        // trackOrder is ALWAYS freshly re-minted below on every transition
        // into Unidentified (never reused), so it can never collide/interleave
        // with another Unidentified entry's order. trackId is only minted
        // when currently empty: for an instance arriving here from Pending
        // (Tier B never available / monitoring FX) that is always true. For
        // the Part B follow-up's tag-forced path, an instance can arrive here
        // from Identified, where entry.trackId already holds that track's own
        // real REAPER GUID - this branch deliberately keeps that GUID as a
        // stable, already-unique Unidentified-partition identity rather than
        // discarding it for a freshly minted UUID; the collision-remint loop
        // below still covers it exactly like a minted UUID would be.
        //
        // Part C addition: when a fresh mint IS needed, prefer the
        // persisted fallbackTrackIdRef (architecture.md's State Persistence
        // -> "<FallbackTrackId uuid="…"/> ... lets an Unidentified instance
        // keep its identity across sessions") over a brand-new juce::Uuid,
        // so a monitoring-FX/non-REAPER-host instance keeps the SAME
        // trackId every time its project is reloaded. If no persisted value
        // exists yet (first time ever), mint one and write it back through
        // the reference so the owning processor's next getStateInformation()
        // saves it.
        if (entry.trackId.isEmpty())
        {
            if (fallbackTrackIdRef.isNotEmpty())
                entry.trackId = fallbackTrackIdRef;
            else
            {
                entry.trackId = juce::Uuid().toString();
                fallbackTrackIdRef = entry.trackId;
            }
        }

        entry.trackOrder.store (SharedMeterRegistry::nextDiscoveryOrder(), std::memory_order_relaxed);

        entry.projectKey = ProjectKey { ProjectKey::Scope::Unidentified, nullptr };

        // Collision re-mint, evaluated only within the Unidentified partition
        // (architecture.md: "This covers track duplication... detecting the
        // collision... and minting a fresh UUID"). Bounded guard - a
        // collision on every one of several random UUID draws in a row is
        // astronomically unlikely; the guard exists only to make the loop
        // provably terminating. A re-mint here (a genuine collision, not the
        // common case) also refreshes fallbackTrackIdRef so the NEW identity
        // - not the collided one - is what gets persisted.
        auto& registry = SharedMeterRegistry::getInstance();

        for (int guard = 0; guard < 8; ++guard)
        {
            auto* existing = registry.findEntry (entry.projectKey, entry.trackId);

            if (existing == nullptr || existing == &entry)
                break;

            entry.trackId = juce::Uuid().toString();
            fallbackTrackIdRef = entry.trackId;
        }

        // Part C: same one-time-per-new-partition seed/version-wins-merge as
        // the Identified path in resolveOnce() (see that method's doc
        // comment) - Unidentified is also a real partition with its own
        // SharedProjectConfig (architecture.md: "one config per project,
        // including one for the Unidentified bucket"). `api` is always
        // nullptr here by construction (Unidentified means Tier B is either
        // unavailable or deliberately not consulted for this instance), so
        // mergeOrSeedProjectConfigOnPartitionEntry() always takes its
        // "seed from own embedded copy" branch, never the ProjExtState one.
        if (entry.projectKey != lastConfigMergeKey)
        {
            mergeOrSeedProjectConfigOnPartitionEntry (
                registry, entry.projectKey, nullptr,
                getOwnRedundantConfigCopy ? getOwnRedundantConfigCopy() : SharedProjectConfig{});

            lastConfigMergeKey = entry.projectKey;
        }
    }
}
