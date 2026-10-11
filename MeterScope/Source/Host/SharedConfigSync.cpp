// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#include "SharedConfigSync.h"

#include <algorithm>
#include <cmath>

namespace meterscope
{
    const std::vector<juce::String>& SharedConfigSync::linkedParamIds()
    {
        static const std::vector<juce::String> ids = []
        {
            std::vector<juce::String> v { "peakMode", "lufsMMode", "showRmsM", "showRmsI",
                                          "showLufsS", "showLra", "showLufsI" };

            for (const char* m : { "TruePeak", "Peak", "RmsM", "RmsI", "LufsM", "LufsS", "LufsI" })
                for (const char* suffix : { "Mode", "Threshold", "RangeMin", "RangeMax" })
                    v.push_back (juce::String ("ind") + m + suffix);

            return v; // 7 + 28 = 35
        }();

        return ids;
    }

    const std::vector<juce::String>& SharedConfigSync::projectParamIds()
    {
        static const std::vector<juce::String> ids { "resetOnPlay", "yScale", "textSize" };
        return ids;
    }

    bool SharedConfigSync::isLinkedParamId (const juce::String& id)
    {
        const auto& ids = linkedParamIds();
        return std::find (ids.begin(), ids.end(), id) != ids.end();
    }

    bool SharedConfigSync::isProjectParamId (const juce::String& id)
    {
        const auto& ids = projectParamIds();
        return std::find (ids.begin(), ids.end(), id) != ids.end();
    }

    SharedConfigSync::SharedConfigSync (juce::AudioProcessorValueTreeState& apvtsToUse,
                                        SharedMeterRegistry::Entry& entryToUse)
        : apvts (apvtsToUse), entry (entryToUse)
    {
        linkedRaw = apvts.getRawParameterValue ("linked");
        jassert (linkedRaw != nullptr);

        auto addSlots = [this] (const std::vector<juce::String>& ids, bool isProject)
        {
            for (auto& id : ids)
            {
                Slot s;
                s.id = id;
                s.param = apvts.getParameter (id);
                s.raw = apvts.getRawParameterValue (id);
                s.isProject = isProject;
                jassert (s.param != nullptr && s.raw != nullptr);

                if (s.param != nullptr && s.raw != nullptr)
                {
                    s.baseline = s.raw->load();
                    slots.push_back (std::move (s));
                }
            }
        };

        addSlots (linkedParamIds(), false);
        addSlots (projectParamIds(), true);
    }

    bool SharedConfigSync::isOwnLinked() const noexcept
    {
        return linkedRaw == nullptr || linkedRaw->load() > 0.5f;
    }

    float SharedConfigSync::getDefaultRaw (const juce::String& id) const
    {
        if (auto* p = apvts.getParameter (id))
            return p->convertFrom0to1 (p->getDefaultValue());

        return 0.0f;
    }

    void SharedConfigSync::applyIfDiffers (Slot& slot, float value)
    {
        // Tolerance covers the parameters' own step quantisation (0.1 dB /
        // 0.01 / integers) so an echo of a value we just wrote never
        // re-triggers a write.
        if (std::abs (slot.raw->load() - value) > 1.0e-3f)
            slot.param->setValueNotifyingHost (slot.param->convertTo0to1 (value));
    }

    void SharedConfigSync::snapshotBaseline()
    {
        for (auto& s : slots)
            s.baseline = s.raw->load();
    }

    std::map<juce::String, float> SharedConfigSync::currentLinkedValues() const
    {
        std::map<juce::String, float> values;

        for (auto& s : slots)
            if (! s.isProject)
                values[s.id] = s.raw->load();

        return values;
    }

    void SharedConfigSync::applyFromStore (const SharedProjectConfig& cfg, bool linked)
    {
        for (auto& s : slots)
        {
            // Project-scope ids follow the store always; Linked-scope ids only
            // while this instance is linked (an unlinked instance owns its copy).
            if (! (s.isProject || linked))
                continue;

            auto it = cfg.globalLinkedConfig.values.find (s.id);

            if (it == cfg.globalLinkedConfig.values.end())
                continue; // never written for this project: keep own value

            applyIfDiffers (s, it->second);
            s.baseline = s.raw->load();
        }
    }

    bool SharedConfigSync::seedMissingGlobal (bool linked)
    {
        // Project-scope ids are shared regardless of linked state; Linked-scope
        // ids only describe the global config while this instance is linked.
        std::map<juce::String, float> values;

        for (auto& s : slots)
            if (s.isProject || linked)
                values[s.id] = s.raw->load();

        return SharedMeterRegistry::getInstance().seedGlobalLinkedParams (entry.projectKey, values);
    }

    bool SharedConfigSync::setOwnLinkedParam (const juce::String& id, float rawValue)
    {
        if (! isLinkedParamId (id))
            return false;

        for (auto& s : slots)
        {
            if (s.id == id)
            {
                applyIfDiffers (s, rawValue);
                return true;
            }
        }

        return false;
    }

    bool SharedConfigSync::reconcile()
    {
        auto& registry = SharedMeterRegistry::getInstance();
        const ProjectKey key = entry.projectKey;

        // Pending has no partition (and no config); nothing to sync until the
        // resolver places this instance.
        if (key.scope == ProjectKey::Scope::Pending)
        {
            initialised = false;
            return false;
        }

        const juce::String trackId = entry.trackId;

        if (trackId.isEmpty())
            return false;

        if (! registry.getProjectConfigVersion (key).has_value())
            return false; // merge/seed has not created this partition's config yet

        const bool linked = isOwnLinked();
        bool wroteStore = false;

        // ---- first reconcile in this partition / under this trackId --------
        if (! initialised || key != syncedKey || trackId != syncedTrackId)
        {
            if (auto cfg = registry.getProjectConfigCopy (key))
                applyFromStore (*cfg, linked);

            wroteStore = seedMissingGlobal (linked);

            if (! linked)
            {
                registry.setUnlinkedTrackConfig (key, trackId, currentLinkedValues());
                wroteStore = true;
            }

            snapshotBaseline();
            lastLinked = linked;
            syncedKey = key;
            syncedTrackId = trackId;
            initialised = true;
            lastVersion = registry.getProjectConfigVersion (key).value_or (0);
            return wroteStore;
        }

        // ---- linked <-> unlinked transition ---------------------------------
        if (linked != lastLinked)
        {
            if (! linked)
            {
                // Unlinking: current values (== global while linked) become
                // this track's own starting copy.
                registry.setUnlinkedTrackConfig (key, trackId, currentLinkedValues());
                wroteStore = true;
            }
            else if (auto cfg = registry.getProjectConfigCopy (key))
            {
                // Relinking: adopt the global values again. Ids the store
                // still lacks (no linked instance has seeded them) take this
                // instance's value, so everyone renders the same global config.
                applyFromStore (*cfg, true);
                wroteStore = seedMissingGlobal (true);
            }

            snapshotBaseline();
            lastLinked = linked;
            lastVersion = registry.getProjectConfigVersion (key).value_or (0);
            return wroteStore;
        }

        // ---- local edits -> store -------------------------------------------
        for (auto& s : slots)
        {
            const float current = s.raw->load();

            if (current == s.baseline)
                continue;

            if (s.isProject || linked)
                registry.setGlobalLinkedParam (key, s.id, current);
            else
                registry.setUnlinkedTrackParam (key, trackId, s.id, current);

            s.baseline = current;
            wroteStore = true;
        }

        // ---- store -> this instance (other instances' edits) ----------------
        const auto version = registry.getProjectConfigVersion (key).value_or (0);

        if (version != lastVersion)
        {
            if (auto cfg = registry.getProjectConfigCopy (key))
                applyFromStore (*cfg, linked);

            lastVersion = registry.getProjectConfigVersion (key).value_or (version);
        }

        return wroteStore;
    }
}
