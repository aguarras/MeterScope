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
// "Project Scoping" (#1, #5) and "REAPER Host Integration" -> Tier B spike
// results/decisions (2026-09-26), and this phase's task instructions.
//
// ProjectResolver is the processor-owned ~2Hz message-thread resolution timer
// that moves this instance's own SharedMeterRegistry::Entry through
// Pending -> Identified/Unidentified, and keeps re-resolving forever
// afterwards (never caches track/take/project/trackId/trackOrder/name - the
// Tier B spike showed an FX moved to another track re-resolves within one
// tick). Runs even when no editor is open (started at construction, owned by
// MeterScopeAudioProcessor, not by the editor).

#include "ProjectConfigPersistence.h"
#include "ReaperHostBridge.h"
#include "SharedMeterRegistry.h"
#include "SharedProjectConfig.h"

#include <juce_events/juce_events.h>

#include <functional>

namespace meterscope
{
    class ProjectResolver final : private juce::Timer
    {
    public:
        // `entry` must already be registered (SharedMeterRegistry::registerInstance()
        // already called) and must outlive this object - MeterScopeAudioProcessor
        // owns both and destroys this object (see its destructor) before
        // unregistering the entry.
        //
        // `getHostTrackName` reads Tier A's updateTrackProperties() name
        // (message thread only, matches architecture.md's Parameter Mapping /
        // Host Integration -> Tier B: "Optionally... P_NAME... if
        // updateTrackProperties has not arrived yet").
        //
        // `forceUnidentifiedProject` gates the CMake option
        // METERSCOPE_FORCE_UNIDENTIFIED_PROJECT (originally this phase's task
        // item 4; reworked in the Part B follow-up because REAPER loads a
        // single plugin module per process, so an all-or-nothing switch
        // cannot force only SOME instances Unidentified while others stay
        // Identified in the same process - which plan.md's T9 requires).
        // When true, every tick checks this instance's own resolved track
        // display name (the same name that is about to be written into
        // entry.trackName: the normal-track host/updateTrackProperties()/
        // P_NAME name, or the parent track's own name for a take FX) for the
        // case-insensitive tag "[UNID]" - see resolveOnce(). Only an instance
        // whose name currently contains that tag is forced to Unidentified
        // that tick; renaming a track to add or remove the tag re-resolves
        // within about one tick (~0.5s), in either direction. Master and
        // monitoring-FX instances are unaffected: Master's name is always the
        // literal "MASTER" and can never contain the tag, and monitoring FX
        // already resolves to Unidentified unconditionally before any tag
        // check runs. When false (METERSCOPE_FORCE_UNIDENTIFIED_PROJECT=OFF,
        // the default), this mechanism is inert and resolveOnce() behaves
        // exactly as it does with the option compiled out.
        // Part C additions:
        //   `fallbackTrackIdRef` - reference to the owning
        //   MeterScopeAudioProcessor's own persisted fallback-identity
        //   string (State Persistence's `<FallbackTrackId uuid="…"/>`
        //   sibling XML, populated by setStateInformation() before this
        //   object's timer ever fires - see the removed-synchronous-resolve
        //   note below). becomeUnidentified() seeds a first-time-minted
        //   trackId from it when non-empty (so an Unidentified instance
        //   keeps its identity across a session reload), and mints+writes
        //   it back through this reference when empty.
        //
        //   `getOwnRedundantConfigCopy` - reads the owning processor's own
        //   embedded SharedProjectConfig copy (populated the same way, from
        //   its `<SharedConfig>` sibling XML). Used only at the moment this
        //   instance first enters a NEW partition this session, to seed/
        //   version-wins-merge that partition's in-memory SharedProjectConfig
        //   (architecture.md's State Persistence -> "Version-wins merge
        //   rule"; see ProjectConfigPersistence.h's
        //   mergeOrSeedProjectConfigOnPartitionEntry()).
        ProjectResolver (ReaperHostBridge& bridge,
                          SharedMeterRegistry::Entry& entry,
                          std::function<juce::String()> getHostTrackName,
                          bool forceUnidentifiedProject,
                          juce::String& fallbackTrackIdRef,
                          std::function<const SharedProjectConfig&()> getOwnRedundantConfigCopy);

        ~ProjectResolver() override;

        // Message-thread only. True once this instance has left Pending
        // (Identified or Unidentified) - a minimal, forward-compatible hook
        // for Stage 3's getProjectScope()/"projectScopeUpdate" (not wired up
        // in this Part B pass; those are Stage 3 Phase 3.3 scope).
        bool hasResolved() const noexcept
        {
            return entry.projectKey.scope != ProjectKey::Scope::Pending;
        }

        // Phase 3.3: optional hook invoked (message thread) at the end of every
        // resolution tick. Set once by the owning processor right after
        // construction. Not a new timer - it rides this existing one.
        std::function<void()> onTick;

    private:
        void timerCallback() override;
        void resolveOnce();
        void becomeUnidentified();

        ReaperHostBridge& reaperHostBridge;
        SharedMeterRegistry::Entry& entry;
        std::function<juce::String()> hostTrackNameProvider;
        bool forceUnidentifiedProject;
        juce::Time constructionTime;

        // Part C additions - see the constructor's doc comment above.
        juce::String& fallbackTrackIdRef;
        std::function<const SharedProjectConfig&()> getOwnRedundantConfigCopy;

        // Tracks the last ProjectKey this instance actually triggered
        // mergeOrSeedProjectConfigOnPartitionEntry() for, so that function
        // runs exactly once per NEW partition this instance enters this
        // session (not on every ~2Hz tick) - see resolveOnce()/
        // becomeUnidentified()'s call sites.
        ProjectKey lastConfigMergeKey;

        JUCE_DECLARE_NON_COPYABLE (ProjectResolver)
    };
}
