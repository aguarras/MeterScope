// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#include "ProjectConfigPersistence.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace meterscope
{
    namespace
    {
        // See this file's header doc: signatures transcribed from REAPER's
        // documented reaper_plugin_functions.h, not verifiable against
        // C:\JUCE. int SetProjExtState(ReaProject*, extname, key, value) ->
        // nonzero on success. int GetProjExtState(ReaProject*, extname, key,
        // valOutBuf, valOutBufSz) -> length written (0 if the key/extname
        // pair was never set).
        using SetProjExtState_t = int (*) (void* /*ReaProject*/, const char*, const char*, const char*);
        using GetProjExtState_t = int (*) (void* /*ReaProject*/, const char*, const char*, char*, int);

        constexpr int kChunkSize = 3800;
        constexpr const char* kExtName = "MeterScope";
        constexpr const char* kKeyPrefix = "SharedConfig";

        template <typename FnPtr>
        FnPtr getReaperFn (reaperhost::IReaperHostApplication& api, const char* name)
        {
            return reinterpret_cast<FnPtr> (api.getReaperApi (name));
        }

        juce::String chunkKey (int index)
        {
            return juce::String (kKeyPrefix) + "_" + juce::String (index);
        }

        juce::String countKey()
        {
            return juce::String (kKeyPrefix) + "_count";
        }

        // XmlElement::toString() includes a header comment and newlines;
        // REAPER's ExtState value strings are stored as single text lines in
        // the .rpp state chunk, so newlines are stripped before storage
        // (harmless for XML parsing - whitespace between tags is
        // insignificant). Avoids depending on the exact availability/name of
        // XmlElement::TextFormat's single-line helpers in this JUCE version.
        juce::String singleLineXml (const juce::XmlElement& xml)
        {
            return xml.toString()
                      .replace ("\r\n", " ")
                      .replace ("\n", " ")
                      .replace ("\t", " ");
        }
    }

    bool writeSharedConfigToProjExtState (reaperhost::IReaperHostApplication& api, void* reaProject, const SharedProjectConfig& cfg)
    {
        jassert (reaProject != nullptr); // never 0/"current project" - architecture.md's explicit prohibition

        if (reaProject == nullptr)
            return false;

        auto* setFn = getReaperFn<SetProjExtState_t> (api, "SetProjExtState");
        if (setFn == nullptr)
            return false;

        auto xml = cfg.toXml();
        if (xml == nullptr)
            return false;

        const auto text = singleLineXml (*xml);
        const int numChunks = juce::jmax (1, (text.length() + kChunkSize - 1) / kChunkSize);

        for (int i = 0; i < numChunks; ++i)
        {
            const auto chunk = text.substring (i * kChunkSize, (i + 1) * kChunkSize);
            setFn (reaProject, kExtName, chunkKey (i).toRawUTF8(), chunk.toRawUTF8());
        }

        setFn (reaProject, kExtName, countKey().toRawUTF8(), juce::String (numChunks).toRawUTF8());
        return true;
    }

    bool readSharedConfigFromProjExtState (reaperhost::IReaperHostApplication& api, void* reaProject, SharedProjectConfig& outCfg)
    {
        jassert (reaProject != nullptr);

        if (reaProject == nullptr)
            return false;

        auto* getFn = getReaperFn<GetProjExtState_t> (api, "GetProjExtState");
        if (getFn == nullptr)
            return false;

        char countBuf[32] = {};
        if (getFn (reaProject, kExtName, countKey().toRawUTF8(), countBuf, (int) sizeof (countBuf)) == 0)
            return false; // never written for this project

        const int numChunks = juce::String (countBuf).getIntValue();
        if (numChunks <= 0)
            return false;

        juce::String text;
        std::vector<char> chunkBuf (kChunkSize + 64, '\0');

        for (int i = 0; i < numChunks; ++i)
        {
            std::fill (chunkBuf.begin(), chunkBuf.end(), '\0');
            getFn (reaProject, kExtName, chunkKey (i).toRawUTF8(), chunkBuf.data(), (int) chunkBuf.size());
            text << chunkBuf.data();
        }

        auto xml = juce::parseXML (text);
        if (xml == nullptr)
            return false;

        outCfg = SharedProjectConfig::fromXml (*xml);
        return true;
    }

    void mergeOrSeedProjectConfigOnPartitionEntry (SharedMeterRegistry& registry, const ProjectKey& key,
                                                    reaperhost::IReaperHostApplication* api,
                                                    const SharedProjectConfig& ownRedundantCopy)
    {
        jassert (key.scope == ProjectKey::Scope::Identified || key.scope == ProjectKey::Scope::Unidentified);

        // Phase 3.3 DECISION (user, 2026-10-09): the Unidentified partition
        // IGNORES redundant per-instance config copies. Its pool can hold
        // instances from several projects (or monitoring FX, non-REAPER hosts),
        // so seeding from / version-wins-merging those copies could apply one
        // project's config to another. It therefore starts from the defaults
        // (a fresh SharedProjectConfig, created here if missing) or from
        // whatever is later edited in that partition, and never from a copy.
        // Identified partitions keep the version-wins merge below unchanged.
        if (key.scope == ProjectKey::Scope::Unidentified)
        {
            registry.withProjectConfig (key, /*createIfMissing*/ true, [] (SharedProjectConfig&) {});
            return;
        }

        // Post-review correctness fix: the previous implementation held a
        // `SharedProjectConfig&` obtained from the registry and mutated it
        // AFTER the registry's internal lock had already been released on
        // return - a dangling-pointer/data-race risk once `projectConfigs`
        // (a std::vector) reallocates or erases concurrently (e.g.
        // getStateInformation(), which the VST3 host may call off the
        // message thread). Fixed by never holding a pointer/reference into
        // the registry outside a locked call: step 1 below is a plain,
        // lock-free read-only COPY check (registry.getProjectConfigCopy());
        // step 2 does the ENTIRE seed/merge decision AND mutation inside a
        // single registry.withProjectConfig() callback, which runs while the
        // registry's own lock is held; step 3 (the I/O ProjExtState
        // write-back) runs afterwards, on a plain SharedProjectConfig COPY
        // taken while still inside that locked callback - never while any
        // registry lock is held.
        //
        // Step 1's own existence check and step 2's create-if-missing check
        // are technically two separate registry accesses (a benign TOCTOU
        // window), which is safe under this codebase's established
        // single-message-thread argument (see SharedMeterRegistry.cpp's own
        // thread-safety note) - this function itself is only ever called
        // from ProjectResolver's message-thread timer callback, never
        // concurrently with itself for the same instance, and REAPER's
        // in-process VST3 hosting means every OTHER instance's own message-
        // thread work is also serialised through the same single OS thread.
        const bool partitionAlreadyExisted = registry.getProjectConfigCopy (key).has_value();

        // "Only for Identified partitions" (this phase's task) - an
        // Unidentified partition has no project to read from. Pure I/O, no
        // registry lock held while this runs.
        std::optional<SharedProjectConfig> diskConfig;

        if (! partitionAlreadyExisted && key.scope == ProjectKey::Scope::Identified && api != nullptr)
        {
            SharedProjectConfig fromDisk;
            if (readSharedConfigFromProjExtState (*api, const_cast<void*> (key.reaProject), fromDisk))
                diskConfig = fromDisk;
        }

        SharedProjectConfig finalCopy;   // populated inside the locked callback below
        bool needsWriteBack = false;

        registry.withProjectConfig (key, /*createIfMissing*/ true, [&] (SharedProjectConfig& cfg)
        {
            if (! partitionAlreadyExisted)
            {
                // architecture.md's Fallback seed: ProjExtState if it
                // resolved above, else "own embedded redundant copy".
                cfg = diskConfig.has_value() ? *diskConfig : ownRedundantCopy;
            }

            // Version-wins comparison, unconditionally (architecture.md:
            // "Every instance resolving into the same partition afterwards
            // compares... If it is strictly newer, it overwrites"). For the
            // just-seeded-from-ownRedundantCopy branch above this is a no-op
            // (cfg IS ownRedundantCopy already). Tie-break on timestamp when
            // versions are equal - not specified verbatim in the contracts,
            // a documented interpretation of this phase.
            const bool ownIsStrictlyNewer =
                ownRedundantCopy.version > cfg.version
                || (ownRedundantCopy.version == cfg.version && ownRedundantCopy.timestampMs > cfg.timestampMs);

            if (ownIsStrictlyNewer)
            {
                cfg = ownRedundantCopy;

                // Repair-on-read: if ProjExtState is available for this
                // (Identified) partition, write the now-corrected config
                // back - but not from here (still holding the lock); just
                // flag it and take a copy below for the caller to use once
                // the lock has been released.
                needsWriteBack = (key.scope == ProjectKey::Scope::Identified && api != nullptr);
            }

            finalCopy = cfg; // copy taken WHILE STILL LOCKED - never dereferences `cfg` after this callback returns
        });

        if (needsWriteBack)
            writeSharedConfigToProjExtState (*api, const_cast<void*> (key.reaProject), finalCopy);
    }
}
