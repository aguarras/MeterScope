// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#pragma once

// Phase 2.2 Part C. See plugins/MeterScope/.ideas/architecture.md ->
// "Multi-Instance Shared Store" (`projectConfigs`) and "State Persistence".
// Framework-light (juce_core only, no REAPER/VST3 dependency) - the
// REAPER-specific ProjExtState read/write path lives separately in
// ProjectConfigPersistence.h, so this struct and SharedMeterRegistry's
// in-memory mutation methods (see SharedMeterRegistry.h's Part C additions)
// have zero REAPER API dependency and can be reasoned about/unit-tested in
// isolation, matching this codebase's DSP/ style.
//
// Deliberate deviation from architecture.md's own field-list wording (its
// `projectConfigs` bullet lists "Mono" alongside the Linked-scope config):
// each instance's own APVTS `monoMode`/`linked` parameters (Stage 1) remain
// the AUTHORITATIVE, REAPER-project-saved store for those two Track-scope
// settings - architecture.md's own State Persistence table says exactly this
// for "The 40 APVTS parameters" row ("None needed [for a second on-disk
// copy]: the 'shared' meaning of these values across instances is a runtime
// concept implemented by the registry, not a second on-disk copy"). Part B
// already lets any instance WRITE another track's Mono/Linked value directly
// through that authoritative store via the `remoteSetMono`/`remoteSetLinked`
// Entry callbacks. `trackMono`/`trackLinked` below exist only as a
// cross-instance-READABLE MIRROR, so a future getTrackList() (Stage 3 Phase
// 3.3) can render OTHER instances' Mono/Linked state without reaching into
// another processor's APVTS - never a second authoritative copy, and never
// read by this instance's own measurement/display logic. Flagged as an open
// question for the user in this phase's report: Stage 3 may find it simpler
// to read this straight off a new Entry field instead, once a concrete UI
// call site exists to prove out the real access pattern.

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <set>
#include <vector>

namespace meterscope
{
    struct SharedProjectConfig
    {
        // The 7 measurement-selection + 28 level-indicator APVTS parameter
        // IDs from parameter-spec.md's "Scope: Linked" set (Stage 1's
        // PluginProcessor.cpp::createParameterLayout() is the single source
        // of truth for the exact IDs - this struct stores whatever subset a
        // caller writes, by ID, and treats any ID never written as "use that
        // parameter's own APVTS default". That is what keeps an old saved
        // config forward-compatible with any Linked-scope parameter a future
        // revision might add.
        struct LinkedConfig
        {
            std::map<juce::String, float> values; // paramId -> raw (denormalised) value
        };

        struct Group
        {
            juce::String id, name;
        };

        // Monotonic config version + wall-clock timestamp (architecture.md's
        // State Persistence -> "Version-wins merge rule": "a monotonically
        // increasing (version, wallClockTimestamp) pair").
        juce::int64 version = 0;
        juce::int64 timestampMs = 0;

        // Global measurement selection + indicators (used while a track is
        // linked), and each unlinked track's own copy (parameter-spec.md:
        // "Unlinking copies the current global values into the instance's
        // own copy as its starting point"), keyed by trackId.
        LinkedConfig globalLinkedConfig;
        std::map<juce::String, LinkedConfig> unlinkedTrackConfigs;

        // Cross-instance-readable mirrors - see this file's header doc above
        // for why these are mirrors, not the authoritative store.
        std::map<juce::String, bool> trackMono;
        std::map<juce::String, bool> trackLinked;

        // Shared View State (parameter-spec.md, "not parameters"; "never
        // affects measurements or linking"; saved with the project).
        std::vector<Group> groups;
        std::map<juce::String, juce::String> trackGroup; // trackId -> group id, "" = no group
        // Group ids plus the UI token "no-group" (for tracks without a group)
        // currently visible in ALL TRACKS.
        //
        // Phase 3.3 fix (groupFilter default bug, found 2026-10-09 in REAPER):
        // the Part C encoding "EMPTY set == all visible" is gone, because an
        // empty set is a LEGAL user choice (every chip off) and must not be
        // confused with the default. `groupFilterExplicit` says whether the
        // user (UI) ever set the filter. While it is false (a fresh or missing
        // config) the effective filter is the all-visible default computed by
        // resolvedGroupFilter(): "no-group" plus every existing group id (so a
        // group created later is visible too). Once explicit, `groupFilter` is
        // taken literally, including when empty.
        std::set<juce::String> groupFilter;
        bool groupFilterExplicit = false;
        juce::String soloTrack;    // "" = none
        std::set<juce::String> hiddenTracks;

        // The groupFilter the UI should apply (see groupFilterExplicit above).
        std::set<juce::String> resolvedGroupFilter() const
        {
            if (groupFilterExplicit)
                return groupFilter;

            std::set<juce::String> all { "no-group" };
            for (auto& g : groups)
                all.insert (g.id);
            return all;
        }

        std::unique_ptr<juce::XmlElement> toXml() const;

        // Unknown/malformed input (including a missing/wrong root tag, e.g.
        // an old Stage-1/Part-B state that never had a <SharedConfig>
        // sibling at all) returns a default-constructed (version 0) config -
        // the backward-compatibility contract this phase's task requires.
        static SharedProjectConfig fromXml (const juce::XmlElement& xml);
    };

    // Phase 3.3: one atomic write of the Shared View State sub-object (what the
    // editor's setSharedViewState() sends). `trackGroup` carries only the
    // tracks the UI knows about; "" means "no group" (erase). Tracks the UI
    // does not know (instances that were closed) keep their saved group unless
    // that group no longer exists. See SharedMeterRegistry::setSharedViewState.
    struct SharedViewStateUpdate
    {
        std::vector<SharedProjectConfig::Group> groups;
        std::set<juce::String> groupFilter;
        juce::String soloTrack;
        std::set<juce::String> hiddenTracks;
        std::map<juce::String, juce::String> trackGroup;
    };
}
