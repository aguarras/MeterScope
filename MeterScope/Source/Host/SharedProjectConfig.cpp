// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#include "SharedProjectConfig.h"

namespace meterscope
{
    namespace
    {
        // Manual getFirstChildElement()/getNextElement() iteration is used
        // throughout instead of XmlElement::getChildWithTagNameIterator(),
        // since the latter's exact availability/name could not be verified
        // against C:\JUCE in this session - the classic API used here has
        // been stable since early JUCE and is the same style already used by
        // Source/Host/ProjectResolver.cpp for XmlElement-free REAPER string
        // buffers, keeping this file's JUCE API surface minimal and safe.
        template <typename Fn>
        void forEachChildWithTag (const juce::XmlElement& parent, const char* tag, Fn&& fn)
        {
            for (auto* child = parent.getFirstChildElement(); child != nullptr; child = child->getNextElement())
                if (child->hasTagName (tag))
                    fn (*child);
        }
    }

    std::unique_ptr<juce::XmlElement> SharedProjectConfig::toXml() const
    {
        auto root = std::make_unique<juce::XmlElement> ("SharedConfig");
        root->setAttribute ("version", juce::String (version));
        root->setAttribute ("timestampMs", juce::String (timestampMs));

        auto* global = root->createNewChildElement ("GlobalLinkedConfig");
        for (auto& kv : globalLinkedConfig.values)
            global->setAttribute (kv.first, (double) kv.second);

        for (auto& kv : unlinkedTrackConfigs)
        {
            auto* trackEl = root->createNewChildElement ("UnlinkedTrackConfig");
            trackEl->setAttribute ("trackId", kv.first);

            for (auto& p : kv.second.values)
                trackEl->setAttribute (p.first, (double) p.second);
        }

        for (auto& kv : trackMono)
        {
            auto* e = root->createNewChildElement ("TrackMono");
            e->setAttribute ("trackId", kv.first);
            e->setAttribute ("mono", kv.second);
        }

        for (auto& kv : trackLinked)
        {
            auto* e = root->createNewChildElement ("TrackLinked");
            e->setAttribute ("trackId", kv.first);
            e->setAttribute ("linked", kv.second);
        }

        auto* groupsEl = root->createNewChildElement ("Groups");
        for (auto& g : groups)
        {
            auto* e = groupsEl->createNewChildElement ("Group");
            e->setAttribute ("id", g.id);
            e->setAttribute ("name", g.name);
        }

        for (auto& kv : trackGroup)
        {
            auto* e = root->createNewChildElement ("TrackGroup");
            e->setAttribute ("trackId", kv.first);
            e->setAttribute ("groupId", kv.second);
        }

        auto* filterEl = root->createNewChildElement ("GroupFilter");
        filterEl->setAttribute ("explicit", (int) groupFilterExplicit);
        for (auto& g : groupFilter)
        {
            auto* e = filterEl->createNewChildElement ("Item");
            e->setAttribute ("id", g);
        }

        if (soloTrack.isNotEmpty())
            root->setAttribute ("soloTrack", soloTrack);

        auto* hiddenEl = root->createNewChildElement ("HiddenTracks");
        for (auto& t : hiddenTracks)
        {
            auto* e = hiddenEl->createNewChildElement ("Item");
            e->setAttribute ("trackId", t);
        }

        return root;
    }

    SharedProjectConfig SharedProjectConfig::fromXml (const juce::XmlElement& xml)
    {
        SharedProjectConfig cfg; // defaults (version 0) - backward-compat fallback

        if (! xml.hasTagName ("SharedConfig"))
            return cfg;

        cfg.version = xml.getStringAttribute ("version").getLargeIntValue();
        cfg.timestampMs = xml.getStringAttribute ("timestampMs").getLargeIntValue();

        if (auto* global = xml.getChildByName ("GlobalLinkedConfig"))
            for (int i = 0; i < global->getNumAttributes(); ++i)
            {
                const auto name = global->getAttributeName (i);
                cfg.globalLinkedConfig.values[name] = (float) global->getDoubleAttribute (name);
            }

        forEachChildWithTag (xml, "UnlinkedTrackConfig", [&] (const juce::XmlElement& trackEl)
        {
            const auto trackId = trackEl.getStringAttribute ("trackId");
            LinkedConfig lc;

            for (int i = 0; i < trackEl.getNumAttributes(); ++i)
            {
                const auto name = trackEl.getAttributeName (i);
                if (name == "trackId")
                    continue;

                lc.values[name] = (float) trackEl.getDoubleAttribute (name);
            }

            cfg.unlinkedTrackConfigs[trackId] = std::move (lc);
        });

        forEachChildWithTag (xml, "TrackMono", [&] (const juce::XmlElement& e)
        {
            cfg.trackMono[e.getStringAttribute ("trackId")] = e.getBoolAttribute ("mono");
        });

        forEachChildWithTag (xml, "TrackLinked", [&] (const juce::XmlElement& e)
        {
            cfg.trackLinked[e.getStringAttribute ("trackId")] = e.getBoolAttribute ("linked");
        });

        if (auto* groupsEl = xml.getChildByName ("Groups"))
            forEachChildWithTag (*groupsEl, "Group", [&] (const juce::XmlElement& e)
            {
                cfg.groups.push_back ({ e.getStringAttribute ("id"), e.getStringAttribute ("name") });
            });

        forEachChildWithTag (xml, "TrackGroup", [&] (const juce::XmlElement& e)
        {
            cfg.trackGroup[e.getStringAttribute ("trackId")] = e.getStringAttribute ("groupId");
        });

        if (auto* filterEl = xml.getChildByName ("GroupFilter"))
        {
            forEachChildWithTag (*filterEl, "Item", [&] (const juce::XmlElement& e)
            {
                cfg.groupFilter.insert (e.getStringAttribute ("id"));
            });

            // Backward compatibility (Part C wrote no "explicit" attribute and
            // an empty filter meant "default"): a missing attribute is taken
            // as explicit only if the saved filter actually has items.
            cfg.groupFilterExplicit = filterEl->hasAttribute ("explicit")
                ? filterEl->getBoolAttribute ("explicit")
                : ! cfg.groupFilter.empty();
        }

        cfg.soloTrack = xml.getStringAttribute ("soloTrack");

        if (auto* hiddenEl = xml.getChildByName ("HiddenTracks"))
            forEachChildWithTag (*hiddenEl, "Item", [&] (const juce::XmlElement& e)
            {
                cfg.hiddenTracks.insert (e.getStringAttribute ("trackId"));
            });

        return cfg;
    }
}
