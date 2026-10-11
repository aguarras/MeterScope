// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#pragma once

// Stage 3 Phase 3.3. Keeps ONE instance's Linked/Project-scope APVTS
// parameters consistent with the project-partitioned SharedProjectConfig
// (architecture.md -> Multi-Instance Shared Store, parameter-spec.md
// "Scope: Linked" / "Scope: Project"):
//
//   * Linked-scope parameters (7 measurement-selection + 28 level-indicator
//     settings): while this instance is LINKED they follow, and write to,
//     globalLinkedConfig; while UNLINKED they are this track's own copy,
//     mirrored into unlinkedTrackConfigs[trackId] so every other instance's
//     combined view can render the track correctly. Unlinking publishes the
//     current (== global) values as the starting point; relinking adopts the
//     global values again (parameter-spec.md: "Unlinking copies the current
//     global values into the instance's own copy").
//     globalLinkedConfig is kept COMPLETE: a linked instance seeds every id
//     the store lacks with its own value (first reconcile and relink; never
//     overwrites). Otherwise an id nobody edited from the Global panel has no
//     global value, and relinking would keep that id's unlinked value.
//   * Project-scope parameters (resetOnPlay, yScale, textSize): shared by every
//     instance of the project regardless of linked state. They are stored in
//     the same globalLinkedConfig map (the map stores any parameter id; unknown
//     ids are forward-compatible) - a documented extension, since
//     architecture.md's projectConfigs list does not enumerate them.
//
// Design: POLLING, no APVTS listeners. reconcile() runs on the message thread
// from (a) the editor's existing 30Hz timer when an editor is open and (b)
// ProjectResolver's existing ~2Hz tick when it is not. It is cheap when nothing
// changed (38 float compares + one version read). Polling avoids the thread
// questions of listener callbacks and cannot feed back on itself: values it
// applies from the store become the new baseline, so they are never mistaken
// for a local edit. All of it is message-thread-only; the audio thread never
// touches this class, the registry's configs, or any lock.
//
// Every store access is keyed by this instance's own ProjectKey (never 0 /
// "current project").

#include "SharedMeterRegistry.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <vector>

namespace meterscope
{
    class SharedConfigSync
    {
    public:
        SharedConfigSync (juce::AudioProcessorValueTreeState& apvts, SharedMeterRegistry::Entry& entry);

        // The 35 Linked-scope ids and the 3 Project-scope ids.
        static const std::vector<juce::String>& linkedParamIds();
        static const std::vector<juce::String>& projectParamIds();
        static bool isLinkedParamId (const juce::String& id);
        static bool isProjectParamId (const juce::String& id);

        // Message thread. Returns true if this call WROTE to the shared store
        // (the caller should then schedule a ProjExtState flush).
        bool reconcile();

        // Message thread. Sets one of THIS instance's own Linked-scope
        // parameters to a raw (denormalised) value, e.g. on behalf of another
        // instance's settings panel (Entry::remoteSetParam). Rejects ids that
        // are not Linked-scope. The change is then picked up by reconcile()
        // like any local edit and mirrored into the right store.
        bool setOwnLinkedParam (const juce::String& id, float rawValue);

        // This instance's parameter default, as a raw value (for payloads of a
        // config whose store map lacks an id).
        float getDefaultRaw (const juce::String& id) const;

        bool isOwnLinked() const noexcept;

    private:
        struct Slot
        {
            juce::String id;
            juce::RangedAudioParameter* param = nullptr;
            const std::atomic<float>* raw = nullptr;
            bool isProject = false;
            float baseline = 0.0f;
        };

        void applyFromStore (const SharedProjectConfig& cfg, bool linked);
        bool seedMissingGlobal (bool linked);
        void applyIfDiffers (Slot& slot, float value);
        void snapshotBaseline();
        std::map<juce::String, float> currentLinkedValues() const;

        juce::AudioProcessorValueTreeState& apvts;
        SharedMeterRegistry::Entry& entry;
        const std::atomic<float>* linkedRaw = nullptr;

        std::vector<Slot> slots;

        bool initialised = false;
        bool lastLinked = true;
        ProjectKey syncedKey;
        juce::String syncedTrackId;
        juce::int64 lastVersion = -1;

        JUCE_DECLARE_NON_COPYABLE (SharedConfigSync)
    };
}
