// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#include "RegistryDumpLogger.h"

#if METERSCOPE_REGISTRY_DUMP_LOG

#include "SharedMeterRegistry.h"

#include <cstdint>
#include <map>
#include <vector>

namespace meterscope
{
    namespace
    {
        juce::File getDumpLogFile()
        {
            return juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("MeterScope_registry.log");
        }

        juce::CriticalSection& getDumpLogLock()
        {
            static juce::CriticalSection lock;
            return lock;
        }

        juce::String partitionLabel (const ProjectKey& key)
        {
            switch (key.scope)
            {
                case ProjectKey::Scope::Identified:
                    return "Identified project=0x" + juce::String::toHexString ((juce::pointer_sized_int) key.reaProject);
                case ProjectKey::Scope::Unidentified:
                    return "Unidentified (shared fallback partition)";
                case ProjectKey::Scope::Pending:
                default:
                    // Each Pending key is unique to its own instance (see
                    // ProjectKey.h) - shown here only for debug visibility;
                    // it is not a real cross-instance partition.
                    return "Pending (single instance) key=0x" + juce::String::toHexString ((juce::pointer_sized_int) key.reaProject);
            }
        }

        // ------------------------------------------------------------------
        // Part C: config-summary dump (task item 1) + the debug command-file
        // mechanism (task item 2), both gated by the same
        // METERSCOPE_REGISTRY_DUMP_LOG flag as the rest of this file, since
        // the WebView editor (Stage 3) that will eventually replace both
        // does not exist yet. See plan.md's Phase 2.2 T2/T4 test
        // instructions for the exact command syntax.
        // ------------------------------------------------------------------

        juce::String configSummary (const SharedProjectConfig& cfg)
        {
            const auto lufsSIt = cfg.globalLinkedConfig.values.find ("showLufsS");
            const bool lufsSOn = (lufsSIt == cfg.globalLinkedConfig.values.end()) || (lufsSIt->second > 0.5f); // APVTS default is true

            juce::String groupNames;
            for (size_t i = 0; i < cfg.groups.size(); ++i)
                groupNames << (i > 0 ? "," : "") << cfg.groups[i].name;

            juce::String out;
            out << "version=" << cfg.version
                << " globalLufsS=" << (lufsSOn ? "on" : "off")
                << " groups=[" << groupNames << "]"
                << " hiddenTracks=" << (int) cfg.hiddenTracks.size()
                << " soloTrack=\"" << cfg.soloTrack << "\"";
            return out;
        }

        juce::File getCmdFile()
        {
            return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MeterScope_cmd.txt");
        }

        // Searches EVERY partition (unlike production Editor Bridge code,
        // which always goes through a ProjectKey - see
        // SharedMeterRegistry::getAllActiveEntriesForDebugDump()'s own doc
        // comment): the command file addresses tracks by name only, so the
        // ISSUING track must first be found globally before its own
        // ProjectKey can be used to scope everything else the command does.
        const SharedMeterRegistry::Entry* findEntryByTrackNameAnyPartition (const juce::String& name)
        {
            for (auto* e : SharedMeterRegistry::getInstance().getAllActiveEntriesForDebugDump())
                if (e->trackName == name)
                    return e;
            return nullptr;
        }

        // Once the issuing track's own ProjectKey is known, every TARGET
        // track a command names (e.g. "hide A-Vox") is resolved only within
        // that SAME partition - this is deliberate: it is what T2 tests
        // ("a config change made from A... changes only A's config").
        const SharedMeterRegistry::Entry* findEntryByTrackNameInPartition (const ProjectKey& key, const juce::String& name)
        {
            for (auto* e : SharedMeterRegistry::getInstance().getAllActiveEntriesForDebugDump())
                if (e->projectKey == key && e->trackName == name)
                    return e;
            return nullptr;
        }

        // Maps the command file's short aliases (plan.md's illustrative
        // syntax, e.g. "lufsS") onto the real APVTS "Scope: Linked"
        // parameter IDs (Stage 1's PluginProcessor.cpp::createParameterLayout(),
        // parameter-spec.md's Measurement selection table) - documented once
        // here so it can never silently drift from Stage 1's actual IDs. Any
        // alias not in this table is passed through unchanged, so the
        // command file can also address any of the 28 ind<m>* indicator IDs
        // (or any Linked-scope ID) directly by its real name.
        juce::String resolveGlobalFieldAlias (const juce::String& alias)
        {
            static const std::map<juce::String, juce::String> aliases
            {
                { "peakMode",  "peakMode" },
                { "lufsMMode", "lufsMMode" },
                { "rmsM",      "showRmsM" },
                { "rmsI",      "showRmsI" },
                { "lufsS",     "showLufsS" },
                { "lra",       "showLra" },
                { "lufsI",     "showLufsI" },
            };

            const auto it = aliases.find (alias);
            return it != aliases.end() ? it->second : alias;
        }

        float parseCommandValue (const juce::String& token)
        {
            if (token.equalsIgnoreCase ("on") || token.equalsIgnoreCase ("true"))   return 1.0f;
            if (token.equalsIgnoreCase ("off") || token.equalsIgnoreCase ("false")) return 0.0f;
            return token.getFloatValue();
        }

        void logCommandOutcome (const juce::String& line, const juce::String& outcome)
        {
            juce::String out;
            out << "  [cmd] \"" << line << "\" -> " << outcome << "\n";
            const juce::ScopedLock sl (getDumpLogLock());
            getDumpLogFile().appendText (out);
        }

        // Applies one command line through SharedMeterRegistry's Part C
        // mutation methods - the SAME in-memory code path a future Editor
        // Bridge write handler (Stage 3 Phase 3.3) will use - then flushes
        // to ProjExtState through the ISSUING track's own instance (via
        // Entry::remoteFlushConfigToProjExtState, see
        // SharedMeterRegistry.h/PluginProcessor.cpp).
        void applyCommandLine (const juce::String& rawLine)
        {
            const auto line = rawLine.trim();
            if (line.isEmpty() || line.startsWith ("#"))
                return;

            const int colon = line.indexOfChar (':');
            if (colon < 0)
            {
                logCommandOutcome (line, "malformed (expected \"TrackName: command args...\")");
                return;
            }

            const auto issuerName = line.substring (0, colon).trim();
            const auto rest = line.substring (colon + 1).trim();

            auto* issuer = findEntryByTrackNameAnyPartition (issuerName);
            if (issuer == nullptr)
            {
                logCommandOutcome (line, "unknown issuing track \"" + issuerName + "\"");
                return;
            }

            if (issuer->projectKey.scope == ProjectKey::Scope::Pending)
            {
                logCommandOutcome (line, "issuing track \"" + issuerName + "\" still Pending - retry next tick");
                return;
            }

            const auto key = issuer->projectKey;
            const auto tokens = juce::StringArray::fromTokens (rest, " ", "");
            auto& registry = SharedMeterRegistry::getInstance();
            bool applied = false;

            if (tokens.size() >= 3 && tokens[0].equalsIgnoreCase ("global"))
            {
                const auto paramId = resolveGlobalFieldAlias (tokens[1]);
                const auto value = parseCommandValue (tokens[2]);
                registry.setGlobalLinkedParam (key, paramId, value);
                logCommandOutcome (line, "OK (global " + paramId + " = " + juce::String (value) + ")");
                applied = true;
            }
            else if (tokens.size() >= 3 && tokens[0].equalsIgnoreCase ("group") && tokens[1].equalsIgnoreCase ("add"))
            {
                const auto name = tokens.joinIntoString (" ", 2);
                const auto id = registry.createGroup (key, name);
                logCommandOutcome (line, "OK (group \"" + name + "\" id=" + id + ")");
                applied = true;
            }
            else if (tokens.size() >= 2 && (tokens[0].equalsIgnoreCase ("hide") || tokens[0].equalsIgnoreCase ("show")))
            {
                const auto targetName = tokens.joinIntoString (" ", 1);
                auto* target = findEntryByTrackNameInPartition (key, targetName);

                if (target == nullptr)
                {
                    logCommandOutcome (line, "unknown target track \"" + targetName + "\" in issuer's own partition");
                    return;
                }

                registry.setTrackHidden (key, target->trackId, tokens[0].equalsIgnoreCase ("hide"));
                logCommandOutcome (line, juce::String ("OK (") + tokens[0] + " " + targetName + ")");
                applied = true;
            }
            else if (tokens.size() >= 2 && tokens[0].equalsIgnoreCase ("solo"))
            {
                const auto targetName = tokens.joinIntoString (" ", 1);
                auto* target = findEntryByTrackNameInPartition (key, targetName);

                if (target == nullptr)
                {
                    logCommandOutcome (line, "unknown target track \"" + targetName + "\" in issuer's own partition");
                    return;
                }

                registry.setSoloTrack (key, target->trackId);
                logCommandOutcome (line, "OK (solo " + targetName + ")");
                applied = true;
            }
            else if (tokens.size() == 1 && tokens[0].equalsIgnoreCase ("unsolo"))
            {
                registry.setSoloTrack (key, {});
                logCommandOutcome (line, "OK (unsolo)");
                applied = true;
            }
            else
            {
                logCommandOutcome (line, "unrecognized command");
                return;
            }

            if (applied && issuer->remoteFlushConfigToProjExtState)
                issuer->remoteFlushConfigToProjExtState();
        }

        void processCommandFileIfPresent()
        {
            auto file = getCmdFile();
            if (! file.existsAsFile())
                return;

            const auto lines = juce::StringArray::fromLines (file.loadFileAsString());

            for (const auto& line : lines)
                applyCommandLine (line);

            const auto processedName = file.getFileName()
                + ".processed-" + juce::String ((juce::int64) juce::Time::getCurrentTime().toMilliseconds());
            file.moveFileTo (file.getSiblingFile (processedName));
        }
    }

    RegistryDumpLogger::RegistryDumpLogger()
    {
        // "~2 s" per this phase's task item 5.
        startTimer (2000);
    }

    RegistryDumpLogger::~RegistryDumpLogger()
    {
        stopTimer();
    }

    void RegistryDumpLogger::timerCallback()
    {
        // Part C: apply any pending debug commands FIRST, so this same
        // tick's dump below already reflects their effect.
        processCommandFileIfPresent();

        const auto entries = SharedMeterRegistry::getInstance().getAllActiveEntriesForDebugDump();

        // Group by partition (ProjectKey has no ordering, only equality - see
        // ProjectKey.h - so this is a simple O(n^2) grouping over at most
        // kMaxInstances=128 entries, which is more than acceptable for a ~2s
        // debug-only timer).
        std::vector<ProjectKey> partitions;

        for (const auto* e : entries)
        {
            bool found = false;

            for (const auto& p : partitions)
            {
                if (p == e->projectKey)
                {
                    found = true;
                    break;
                }
            }

            if (! found)
                partitions.push_back (e->projectKey);
        }

        juce::String out;
        out << "=== " << juce::Time::getCurrentTime().toISO8601 (true) << " ("
            << (int) partitions.size() << " partition(s), " << (int) entries.size() << " entrie(s)) ===\n";

        for (const auto& key : partitions)
        {
            out << "  " << partitionLabel (key) << "\n";

            // Part C, task item 1: per-partition config version + compact
            // summary. Pending keys never have a config (getOrCreateProjectConfig
            // asserts non-Pending - see SharedMeterRegistry.h), so
            // getProjectConfigCopy() correctly returns std::nullopt for those
            // and this line is skipped for them. getProjectConfigCopy() takes
            // its copy under the registry's own lock and never returns a
            // pointer/reference into the registry (post-review fix).
            if (auto cfg = SharedMeterRegistry::getInstance().getProjectConfigCopy (key))
                out << "      config " << configSummary (*cfg) << "\n";

            for (const auto* e : entries)
            {
                if (! (e->projectKey == key))
                    continue;

                const double lufsI = e->snapshot != nullptr
                                          ? e->snapshot->lufsI.load (std::memory_order_relaxed)
                                          : -100.0;
                const std::uint64_t version = e->snapshot != nullptr
                                                  ? e->snapshot->version.load (std::memory_order_relaxed)
                                                  : 0;

                out << "      trackId=" << e->trackId
                    << " order=" << e->trackOrder.load (std::memory_order_relaxed)
                    << " name=\"" << e->trackName << "\""
                    << " snapshotVersion=" << (juce::int64) version
                    << " lufsI=" << juce::String (lufsI, 2) << "\n";
            }
        }

        const juce::ScopedLock sl (getDumpLogLock());
        getDumpLogFile().appendText (out);
    }
}

#endif // METERSCOPE_REGISTRY_DUMP_LOG
