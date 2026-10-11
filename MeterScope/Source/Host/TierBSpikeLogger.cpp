// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#include "TierBSpikeLogger.h"

#if METERSCOPE_TIER_B_SPIKE_LOG

#include "../PluginProcessor.h"

namespace meterscope
{
    namespace
    {
        // Opaque - REAPER's GUID struct is bit-compatible with the Win32
        // GUID, but this spike never dereferences it: it is only ever passed
        // as a pointer straight from GetTrackGUID's return value into
        // guidToString's argument. Forward-declared only, deliberately kept
        // out of the reaperhost namespace (it is not part of the vendored
        // VST3-interface header - it is a REAPER extension-API type, fetched
        // via getReaperApi(), not something JUCE or the VST3 SDK declares).
        struct OpaqueReaperGuid;

        // REAPER extension API function-pointer signatures (architecture.md's
        // Host Integration -> Tier B: "GetSetMediaTrackInfo_String
        // (myTrack, "GUID", ...)", "GetMediaTrackInfo_Value (myTrack,
        // "IP_TRACKNUMBER")"; plan.md's Step 1 spike additionally asks for
        // GetTrackGUID+guidToString and GetProjectName/EnumProjects). These
        // are REAPER's own SDK signatures (reaper_plugin_functions.h), not
        // part of the JUCE/VST3 SDK, so they cannot be verified against
        // C:\JUCE - only empirically, by this spike, against a running
        // REAPER instance (plan.md's checks (a)-(d)).
        using GetTrackGUID_t                = OpaqueReaperGuid* (*) (void* /*MediaTrack*/);
        using guidToString_t                = void (*) (const OpaqueReaperGuid*, char*);
        using GetMediaTrackInfo_Value_t     = double (*) (void* /*MediaTrack*/, const char*);
        using GetSetMediaTrackInfo_String_t = bool (*) (void* /*MediaTrack*/, const char*, char*, bool);
        using GetProjectName_t              = void (*) (void* /*ReaProject*/, char*, int);
        using EnumProjects_t                = void* /*ReaProject, or nullptr past the last index*/ (*) (int, char*, int);

        template <typename FnPtr>
        FnPtr getReaperFn (reaperhost::IReaperHostApplication& api, const char* name)
        {
            return reinterpret_cast<FnPtr> (api.getReaperApi (name));
        }

        juce::CriticalSection& getSpikeLogLock()
        {
            static juce::CriticalSection lock;
            return lock;
        }

        juce::File getSpikeLogFile()
        {
            return juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("MeterScope_TierB_spike.log");
        }

        juce::String pointerToString (const void* ptr)
        {
            return ptr == nullptr ? juce::String ("null")
                                   : "0x" + juce::String::toHexString ((juce::pointer_sized_int) ptr);
        }
    }

    TierBSpikeLogger::TierBSpikeLogger (MeterScopeAudioProcessor& ownerProcessor)
        : owner (ownerProcessor)
    {
        // Short, process-unique identifier for this instance (distinguishes
        // lines from different simultaneously-open instances in the shared
        // log file - plan.md's test criterion "at least 2 simultaneously-open
        // instances on 2 different tracks").
        instanceId = "0x" + juce::String::toHexString ((juce::pointer_sized_int) &owner);

        // "a processor-owned juce::Timer" (plan.md), "~2Hz"/"~2s" cadence,
        // running even when no editor is open (started here, from the
        // processor's own construction, not from createEditor()).
        startTimer (2000);
    }

    TierBSpikeLogger::~TierBSpikeLogger()
    {
        stopTimer();
    }

    void TierBSpikeLogger::timerCallback()
    {
        juce::String line;
        line << juce::Time::getCurrentTime().toISO8601 (true) << " instance=" << instanceId;

        auto* app = owner.getReaperHostBridgeForSpikeLog().getReaperHostApplication();

        if (app == nullptr)
        {
            line << " reaperHostApplication=null (Tier B unavailable: non-REAPER host, or "
                    "setIHostApplication() not called/not yet called)";
        }
        else
        {
            void* trackPtr   = app->getReaperParent (1);
            void* takePtr    = app->getReaperParent (2);
            void* projectPtr = app->getReaperParent (3);

            line << " track="   << pointerToString (trackPtr)
                 << " take="    << pointerToString (takePtr)
                 << " project=" << pointerToString (projectPtr);

            // Project name/path - architecture.md: "GetProjectName/EnumProjects
            // from getReaperApi, so the user can tell the tabs apart". Read-only
            // for this spike; never calls with `0`/nullptr in place of a real
            // ReaProject*.
            if (projectPtr == nullptr)
            {
                line << " projectName=(no project - Tier B did not resolve one)";
            }
            else if (auto* getProjectName = getReaperFn<GetProjectName_t> (*app, "GetProjectName"))
            {
                char nameBuf[2048] = {};
                getProjectName (projectPtr, nameBuf, (int) sizeof (nameBuf));
                juce::String projectName (nameBuf);

                if (projectName.isEmpty())
                {
                    // GetProjectName() returns an empty buffer for an unsaved
                    // project; fall back to matching this ReaProject* against
                    // EnumProjects()'s own pointer, which also yields the path
                    // for saved projects (used here only as a cross-check /
                    // fallback, per plan.md's explicit mention of both APIs).
                    if (auto* enumProjects = getReaperFn<EnumProjects_t> (*app, "EnumProjects"))
                    {
                        for (int i = 0; ; ++i)
                        {
                            char pathBuf[2048] = {};
                            void* p = enumProjects (i, pathBuf, (int) sizeof (pathBuf));

                            if (p == nullptr)
                                break;

                            if (p == projectPtr)
                            {
                                projectName = juce::String (pathBuf);
                                break;
                            }
                        }
                    }
                }

                line << " projectName=\""
                     << (projectName.isEmpty() ? juce::String ("(unsaved project, no path available)") : projectName)
                     << "\"";
            }
            else
            {
                line << " projectName=(GetProjectName unavailable via getReaperApi)";
            }

            // Track GUID / order / name.
            if (trackPtr == nullptr)
            {
                line << " trackGUID=(no track) ipTrackNumber=(n/a) isMaster=(n/a) pName=(n/a)";
            }
            else
            {
                auto* getTrackGUID   = getReaperFn<GetTrackGUID_t> (*app, "GetTrackGUID");
                auto* guidToStringFn = getReaperFn<guidToString_t> (*app, "guidToString");

                if (getTrackGUID != nullptr && guidToStringFn != nullptr)
                {
                    if (auto* guid = getTrackGUID (trackPtr))
                    {
                        char guidBuf[64] = {};
                        guidToStringFn (guid, guidBuf);
                        line << " trackGUID=" << juce::String (guidBuf);
                    }
                    else
                    {
                        line << " trackGUID=(GetTrackGUID returned null)";
                    }
                }
                else
                {
                    line << " trackGUID=(GetTrackGUID/guidToString unavailable via getReaperApi)";
                }

                if (auto* getValue = getReaperFn<GetMediaTrackInfo_Value_t> (*app, "GetMediaTrackInfo_Value"))
                {
                    const double trackNumber = getValue (trackPtr, "IP_TRACKNUMBER");
                    line << " ipTrackNumber=" << trackNumber
                         << " isMaster=" << (trackNumber == -1.0 ? "true" : "false");
                }
                else
                {
                    line << " ipTrackNumber=(GetMediaTrackInfo_Value unavailable) isMaster=(n/a)";
                }

                if (auto* getSetString = getReaperFn<GetSetMediaTrackInfo_String_t> (*app, "GetSetMediaTrackInfo_String"))
                {
                    char nameBuf[512] = {};
                    getSetString (trackPtr, "P_NAME", nameBuf, false);
                    line << " pName=\"" << juce::String (nameBuf) << "\"";
                }
                else
                {
                    line << " pName=(GetSetMediaTrackInfo_String unavailable)";
                }
            }
        }

        line << " updateTrackPropertiesName=\"" << owner.getHostTrackNameForSpikeLog() << "\""
             << " isPlaying=" << (owner.getLastKnownIsPlayingForSpikeLog() ? "true" : "false");

        const juce::ScopedLock sl (getSpikeLogLock());
        getSpikeLogFile().appendText (line + "\n");
    }
}

#endif // METERSCOPE_TIER_B_SPIKE_LOG
