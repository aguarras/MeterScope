// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#pragma once

// Phase 2.2 Part C. Primary persistence path for SharedProjectConfig
// (architecture.md -> State Persistence: "SetProjExtState(myProject,
// 'MeterScope', 'SharedConfig', json)" / GetProjExtState, "always with the
// resolved ReaProject*, never 0"). Only ever meaningful for Identified
// partitions - architecture.md's persistence table: "Only for Identified
// partitions" (Unidentified has no project to write to).
//
// REAPER extension API function-pointer signatures below (SetProjExtState/
// GetProjExtState) are transcribed from REAPER's own documented
// reaper_plugin_functions.h; that header is NOT part of the JUCE/VST3 SDK
// vendored into this repo (Source/Host/reaper_vst3_interfaces.h only
// declares IReaperHostApplication itself), so these signatures cannot be
// verified against C:\JUCE the way ReaperHostBridge's own types can - same
// documented limitation already accepted in ProjectResolver.cpp for
// GetSetMediaTrackInfo_String et al. Flagged as an open question in this
// phase's report; recommend a real-REAPER smoke test of T4 before relying on
// this path in production.
//
// Chunking: REAPER's per-value ProjExtState length limit is not confirmed
// locally, so the serialized config is defensively split into fixed-size
// chunks under numbered sub-keys ("SharedConfig_0", "SharedConfig_1", ...)
// plus a "SharedConfig_count" key - safe regardless of the real per-value
// limit. A later, smaller write simply lowers "SharedConfig_count"; any
// higher-numbered chunk keys from a previous larger write become orphaned
// (never read again, harmless, reused if a future write needs that many
// chunks again).

#include "ProjectKey.h"
#include "ReaperHostBridge.h"
#include "SharedMeterRegistry.h"
#include "SharedProjectConfig.h"

namespace meterscope
{
    // Writes `cfg` to ProjExtState("MeterScope", "SharedConfig_*") on
    // `reaProject`. Never call with reaProject == nullptr (architecture.md's
    // explicit prohibition on passing "the current project"). Returns false
    // (no-op) if the host doesn't expose SetProjExtState.
    bool writeSharedConfigToProjExtState (reaperhost::IReaperHostApplication& api,
                                          void* reaProject,
                                          const SharedProjectConfig& cfg);

    // Reads it back. Returns false (leaves `outCfg` untouched) if
    // unavailable, or the key was never set for this project.
    bool readSharedConfigFromProjExtState (reaperhost::IReaperHostApplication& api,
                                           void* reaProject,
                                           SharedProjectConfig& outCfg);

    // Called once per instance, each time that instance's own ProjectKey
    // transitions into a partition (Identified or Unidentified) it has not
    // yet entered this session (architecture.md's "Version-wins merge rule":
    // "The merge runs at project-key resolution time... First instance to
    // resolve into a given partition seeds that partition's SharedProjectConfig
    // in memory from its own embedded redundant copy... Every instance
    // resolving into the same partition afterwards compares ITS OWN embedded
    // copy's (version, timestamp)... If it is strictly newer, it overwrites").
    // See ProjectResolver.cpp for the call site and "new partition" tracking.
    // Phase 3.3: an UNIDENTIFIED key never seeds from or merges with
    // `ownRedundantCopy` (user decision 2026-10-09) - it only ensures the
    // partition's default config exists. See the .cpp.
    // `api` is nullptr when Tier B is unavailable (ProjExtState step is then
    // skipped entirely, matching "Only for Identified partitions" AND "when
    // ProjExtState is unavailable... seeds from its own embedded copy").
    void mergeOrSeedProjectConfigOnPartitionEntry (SharedMeterRegistry& registry,
                                                    const ProjectKey& key,
                                                    reaperhost::IReaperHostApplication* api,
                                                    const SharedProjectConfig& ownRedundantCopy);
}
