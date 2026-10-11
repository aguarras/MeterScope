// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#pragma once

// Phase 2.2 Step 1 (Tier B spike). See plugins/MeterScope/.ideas/plan.md,
// Phase 2.2 "Step 1: Tier B spike (do first)", and
// plugins/MeterScope/.ideas/architecture.md -> "REAPER Host Integration ->
// Tier B - REAPER host API".
//
// Vendors JUCE 9.0.2's own examples/Plugins/extern/reaper_vst3_interfaces.h
// (Source/Host/reaper_vst3_interfaces.h, byte-identical) and wires it up
// exactly the way JUCE's own examples/Plugins/ReaperEmbeddedViewPluginDemo.h
// does it (VST3Extensions::setIHostApplication there): only
// juce::VST3ClientExtensions::setIHostApplication() is overridden.
//
// Deliberately NOT overriding queryIEditController(): architecture.md is
// explicit that a custom IInfoListener implementation there would collide
// with JUCE's own IInfoListener (used internally by
// AudioProcessor::updateTrackProperties()'s VST3 forwarding), tripping
// extractResult()'s jassertfalse ("you've provided a custom implementation of
// an interface that JUCE implements already") and silently disabling
// updateTrackProperties(). See juce_VST3ClientExtensions.h's own
// documentation of setIHostApplication/queryIEditController
// (juce_audio_processors_headless/utilities/juce_VST3ClientExtensions.h,
// checked against C:\JUCE 2026-09-26).

#include <juce_audio_processors/juce_audio_processors.h>

// juce_VST3ClientExtensions.h (juce_audio_processors_headless/utilities/
// juce_VST3ClientExtensions.h, checked against C:\JUCE 2026-09-26) only
// forward-declares `Steinberg::FUnknown`/`Steinberg::TUID` ("Forward
// declaration to avoid leaking implementation details"), so FUnknown is
// still an incomplete type at this point - not enough to derive
// IReaperHostApplication from it or to call queryInterface() on it. Pull in
// the real definition directly, exactly as JUCE's own
// examples/Plugins/ReaperEmbeddedViewPluginDemo.h does
// (`#include <pluginterfaces/base/funknown.h>` there, alongside ftypes.h/
// ivsthostapplication.h/aeffect.h which this plugin doesn't need - Phase 2.2
// Step 1 only calls queryInterface() on the FUnknown JUCE hands us, it never
// implements IHostApplication or anything VST2). This header is available
// unprefixed because JUCE's CMake plugin-target machinery adds the VST3 SDK
// include path (juce_audio_processors_headless/format_types/VST3_SDK/) to
// the whole target whenever FORMATS includes VST3 - confirmed by
// juce_audio_plugin_client_VST3.cpp's own unprefixed
// `#include <pluginterfaces/base/fplatform.h>`, and by JUCE's demo including
// this same header directly from ordinary plugin source, not just from the
// wrapper .cpp.
#include <pluginterfaces/base/funknown.h>

namespace meterscope::reaperhost
{
    using namespace Steinberg;
    using uint32 = Steinberg::uint32;

    // Note: JUCE's own demo also declares `using INT_PTR = pointer_sized_int;`
    // here, but that alias is only used by reaper_plugin_fx_embed.h (the
    // embedded-UI interface), which this spike deliberately does not vendor
    // (Phase 2.2 Step 1 needs IReaperHostApplication only, not
    // IReaperUIEmbedInterface's embedding support) - reaper_vst3_interfaces.h
    // itself never references INT_PTR, so it is omitted here.
    #include "reaper_vst3_interfaces.h"
}

namespace meterscope
{
    /** Minimal juce::VST3ClientExtensions override for Phase 2.2 Step 1:
        captures the host's IReaperHostApplication if the host provides one
        (REAPER >= 5.02, via IHostApplication in initialize()).

        Clean no-op in every other host: setIHostApplication() may simply
        never be called at all (juce_VST3ClientExtensions.h's own doc comment:
        "you should not make any assumptions about how and when this will be
        called - this function may not be called at all!" - true for
        pluginval, the Standalone format, and an offline pedalboard-hosted
        render), or may be called with an IHostApplication that doesn't answer
        IReaperHostApplication::iid (a non-REAPER VST3 host). In both cases
        getReaperHostApplication() stays nullptr, and every Tier B code path
        in this plugin treats that as "Tier B unavailable"
        (architecture.md's Fallback / Unidentified partition).
    */
    class ReaperHostBridge final : public juce::VST3ClientExtensions
    {
    public:
        // Called by JUCE's VST3 wrapper from installAudioProcessor(), on the
        // message thread, with the IHostApplication it received in
        // IEditController::initialize() (architecture.md's Host Integration:
        // "every getReaperParent/getReaperApi function call runs on the
        // message thread"). May not be called at all - see class doc above.
        void setIHostApplication (Steinberg::FUnknown* hostContext) override;

        // nullptr in every non-REAPER host, or before setIHostApplication()
        // has been called (or if it is never called). Message-thread only -
        // never call from processBlock() (architecture.md: "None runs on the
        // audio thread").
        reaperhost::IReaperHostApplication* getReaperHostApplication() const noexcept
        {
            return reaperHost.get();
        }

    private:
        // hostContext->queryInterface() already adds a reference for us
        // (COM/VST3 convention) - held here via IPtr::adopt() (addRef=false)
        // so we neither double-add nor leak it. (JUCE's own
        // ReaperEmbeddedViewPluginDemo.h leaks this reference; plan.md's
        // Phase 2.2 Step 1 explicitly requires MeterScope not to.)
        Steinberg::IPtr<reaperhost::IReaperHostApplication> reaperHost;
    };
}
