// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "DSP/MeteringEngine.h"
#include "Host/ProjectConfigPersistence.h"
#include "Host/ReaperHostBridge.h"
#include "Host/SharedMeterRegistry.h"
#include "Host/SharedProjectConfig.h"
#include "Host/ProjectResolver.h"
#include "Host/SharedConfigSync.h"

#if METERSCOPE_TIER_B_SPIKE_LOG
 #include "Host/TierBSpikeLogger.h"
#endif

#if METERSCOPE_REGISTRY_DUMP_LOG
 #include "Host/RegistryDumpLogger.h"
#endif

// Stage 1 (Foundation + Shell): build system + all 40 APVTS parameters from
// parameter-spec.md, automation disabled on all of them.
// Stage 2 Phase 2.1: the 9 Core Components from architecture.md are
// implemented in Source/DSP/ and driven from processBlock() below. Audio
// remains a pure, bit-identical passthrough - MeterScope is analysis-only and
// NEVER writes to the buffer.
// Stage 2 Phase 2.2, Step 1: the Tier B REAPER host-integration spike -
// ReaperHostBridge (juce::VST3ClientExtensions subclass capturing
// IReaperHostApplication, see Source/Host/ReaperHostBridge.h) and
// updateTrackProperties() (Tier A, track name only). Spike PASSED
// (2026-09-26) - see plan.md's Phase 2.2 "Spike result".
// Stage 2 Phase 2.2, Part B: the project/track resolution timer
// (Source/Host/ProjectResolver.h), the process-wide partitioned
// SharedMeterRegistry singleton (Source/Host/SharedMeterRegistry.h),
// cross-instance write callbacks (remoteSetLinked/remoteSetMono/
// remoteResetOne/remoteResetAll, registered below into this instance's own
// registry Entry), the Fallback tier (discovery order + minted juce::Uuid,
// forced via METERSCOPE_FORCE_UNIDENTIFIED_PROJECT), and the optional debug
// registry dump (Source/Host/RegistryDumpLogger.h).
// Stage 2 Phase 2.2, Part C (this file's current state): projectConfigs/
// SharedProjectConfig (Source/Host/SharedProjectConfig.h) with per-project
// ProjExtState persistence (Source/Host/ProjectConfigPersistence.h) and a
// version-wins redundant copy embedded in getStateInformation(); the
// <EditorState>/<FallbackTrackId> non-APVTS XML siblings; and a
// processor-owned juce::UndoManager passed to the APVTS (used starting Stage
// 3 Phase 3.1's Web*ParameterAttachment constructors - see getUndoManager()).
// No WebView UI yet (Stage 3).
class MeterScopeAudioProcessor : public juce::AudioProcessor
{
public:
    MeterScopeAudioProcessor();
    ~MeterScopeAudioProcessor() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Message-thread API: requests a full measurement reset, consumed
    // (exchange) at the top of this instance's own next processBlock -
    // mirrors the JSFX's own `want_reset` flag (architecture.md's Reset &
    // Transport Controller). remoteResetAll (registry callback) calls this
    // full-reset entry point.
    void requestReset() { meteringEngine.requestReset(); }

    // Message-thread API: PARTIAL reset of ONE measurement's held max (+ clip
    // counters for Peak/TruePeak - they share one register), both channels.
    // Keys: "Peak"/"TruePeak" -> shared peak register, "RmsM", "LufsM",
    // "LufsS". Unknown key: no-op (never falls back to a full reset). Used by
    // remoteResetOne and the editor's resetTrackMeterMax. DELIBERATE
    // DIFFERENCE FROM THE JSFX (any click there runs the full Reset()).
    void requestHeldMaxReset(const juce::String& measurementKey);

    // Read-only accessor for the raw (uncompensated) measurement snapshot
    // this instance's audio thread publishes at the end of every
    // processBlock (architecture.md's SharedMeterSnapshot, Phase 2.1 scope -
    // no multi-instance registry yet). Safe to call from the message thread;
    // every field is a relaxed-order atomic.
    const meterscope::SharedMeterSnapshot& getMeterSnapshot() const noexcept
    {
        return meteringEngine.getSnapshot();
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Phase 2.2 Step 1 (Tier B spike): returns this instance's ReaperHostBridge
    // so JUCE's VST3 wrapper can call setIHostApplication() on it. A clean
    // no-op pointer in every non-VST3 / non-REAPER host - see
    // ReaperHostBridge's own class documentation.
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &reaperHostBridge; }

    // Phase 2.2 Step 1 (Tier A, track name only): the host calls this on the
    // message thread whenever the track's name/colour changes. Stored for
    // later Phase 2.2 use (Entry::trackName) and for the spike log. Message
    // thread only, per juce_AudioProcessor.h's own documentation of this
    // method.
    void updateTrackProperties (const TrackProperties& properties) override
    {
        if (properties.name.has_value())
            hostTrackName = *properties.name;
    }

   #if METERSCOPE_TIER_B_SPIKE_LOG
    // Spike-log-only accessors (compiled out entirely when
    // METERSCOPE_TIER_B_SPIKE_LOG is OFF). Message-thread only.
    meterscope::ReaperHostBridge& getReaperHostBridgeForSpikeLog() noexcept { return reaperHostBridge; }
    juce::String getHostTrackNameForSpikeLog() const { return hostTrackName; }
    bool getLastKnownIsPlayingForSpikeLog() const noexcept
    {
        return lastKnownIsPlayingForSpikeLog.load (std::memory_order_relaxed);
    }
   #endif

    const juce::String getName() const override { return "MeterScope"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Part C: processor-owned juce::UndoManager (architecture.md's State
    // Persistence -> Undo: "pass a real juce::UndoManager* (owned by the
    // processor) as the third argument to every Web*ParameterAttachment
    // constructor in Stage 3"). MUST be declared (and therefore constructed)
    // BEFORE `parameters` below, since its constructor takes &undoManager -
    // C++ constructs members in declaration order regardless of access
    // specifiers, matching this repo's general "DSP components declare
    // BEFORE APVTS" ordering convention. The 40 Web*ParameterAttachment call
    // sites do not exist yet (WebView editor arrives Stage 3 Phase 3.1) -
    // this member only wires the plumbing and exposes getUndoManager() for
    // that stage; see plan.md's Part C implementation status note.
    juce::UndoManager undoManager;

    // Public so the editor (Stage 3 WebView) can bind Web*ParameterAttachment
    // objects directly, following this repo's SimpleGain/Retumbo convention.
    juce::AudioProcessorValueTreeState parameters;

    // Stage 3 Phase 3.1: pass to every Web*ParameterAttachment's 3rd
    // constructor argument (currently nullptr in the v7 scaffold - see
    // architecture.md's Undo decision and plan.md's Phase 3.1 components).
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    // Stage 3 Phase 3.2: this instance's own persisted <EditorState>
    // width/height (Phase 2.2 Part C's XML sibling - see
    // getStateInformation()/setStateInformation()). Message-thread only,
    // exactly like every other Editor Bridge native-function handler and
    // Part C's own editorStateWidth/Height fields - the editor reads these
    // once at construction (falling back to its own 480x520 default when
    // either is 0, i.e. "no size saved yet") and writes through
    // setEditorStateSize() on every persisted resize
    // (handleSetEditorStateSize in PluginEditor.cpp). No audio-thread
    // involvement whatsoever.
    int getEditorStateWidth() const noexcept { return editorStateWidth; }
    int getEditorStateHeight() const noexcept { return editorStateHeight; }
    void setEditorStateSize(int width, int height) noexcept
    {
        editorStateWidth = width;
        editorStateHeight = height;
    }

    // ------------------------------------------------------------------
    // Stage 3 Phase 3.3: Multi-Instance Bridge accessors (all message-thread
    // only, like every Editor Bridge handler).
    // ------------------------------------------------------------------

    // This instance's own registry Entry (nullptr only if the registry was
    // full at construction). Its projectKey/trackId/trackName are kept current
    // by ProjectResolver; the editor reads them every tick, never caches.
    meterscope::SharedMeterRegistry::Entry* getRegistryEntry() const noexcept { return registryEntry; }

    // The id string the WebView uses for this instance's own host track
    // (meterscope::uiTrackIdFor).
    juce::String getUiTrackId() const;

    // Reconciles this instance's Linked/Project-scope parameters with the
    // project's shared config (see Host/SharedConfigSync.h). Called by the
    // editor's 30Hz timer and by ProjectResolver's ~2Hz tick.
    void syncSharedConfig();

    // Writes this instance's partition config to ProjExtState now (Identified
    // partitions with Tier B only; otherwise a no-op). Editor handlers call it
    // after a user-driven shared-config write; sync-driven writes are flushed
    // from the ~2Hz tick.
    void flushSharedConfig();

    // Cross-instance-writable Linked-scope parameter of THIS instance (the
    // body of Entry::remoteSetParam), also used for the editor's own-target case.
    bool setOwnLinkedParam(const juce::String& paramId, float rawValue);

    // Raw default of a parameter, for payloads of a config that lacks an id.
    float getParameterDefaultRaw(const juce::String& paramId) const;

private:
    // Parameter layout creation - all 40 parameters from
    // plugins/MeterScope/.ideas/parameter-spec.md, zero drift, automation
    // disabled on every one (parameter-spec.md "General Rules": "none of
    // these settings need to be automatable").
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Task 8 (pending Stage 1 item): parameter-spec.md's invariant
    // `RangeMin <= RangeMax` for each of the 7 ind<m> pairs - if a loaded
    // state has them crossed, swap them. Called from setStateInformation().
    void enforceIndicatorRangeInvariant();

    // Phase 2.1 Core Measurement Engine (architecture.md Components 1-7).
    // Default-constructed here; real initialization (sample rate, channel
    // count, full Reset()) happens in prepareToPlay() via meteringEngine.prepare().
    // Independent of APVTS construction order (this class caches its raw
    // parameter pointers from `parameters` in the constructor body, not via
    // any member-initializer-list dependency on meteringEngine).
    meterscope::MeteringEngine meteringEngine;

    // Cached raw-parameter-value pointers (architecture.md's Parameter
    // Mapping: only `peakMode` and `resetOnPlay` are read by the native
    // engine - monoMode, the 28 indicator parameters, yScale and textSize are
    // client-side/display-only and are never read here). Cached in the
    // constructor so processBlock() never does a string lookup.
    std::atomic<float>* peakModeParam = nullptr;
    std::atomic<float>* resetOnPlayParam = nullptr;

   #if METERSCOPE_VALIDATION_DUMP
    // Validation-only override (env var METERSCOPE_FORCE_PLAYING=1, read and
    // cached in prepareToPlay): forces the isPlayingOrRecording integrate gate
    // to true so RMS-I/LUFS-I/LRA can be validated from an offline render
    // (e.g. the pedalboard venv) where no host transport is playing. Does NOT
    // fake a play position - hasPlayPosition/playPositionSeconds stay exactly
    // what the host provides, so the resetOnPlay position-jump trigger is
    // unaffected. Compiles out completely when METERSCOPE_VALIDATION_DUMP is OFF.
    bool forcePlayingForValidation = false;
   #endif

    // Phase 2.2 Step 1 (Tier B spike). reaperHostBridge captures the host's
    // IReaperHostApplication (nullptr in non-REAPER hosts). hostTrackName is
    // message-thread-only state set by updateTrackProperties() (Tier A).
    meterscope::ReaperHostBridge reaperHostBridge;
    juce::String hostTrackName;

    // Phase 2.2 Part B: this instance's own registry presence. registryEntry
    // is a stable pointer into SharedMeterRegistry's fixed array (registered
    // in the constructor below, unregistered in the destructor) - valid for
    // this object's entire lifetime. projectResolver owns the ~2Hz
    // message-thread project/track resolution timer that keeps
    // registryEntry->projectKey/trackId/trackName/trackOrder continuously
    // up to date (never cached - see ProjectResolver.h/.cpp).
    meterscope::SharedMeterRegistry::Entry* registryEntry = nullptr;
    std::unique_ptr<meterscope::ProjectResolver> projectResolver;

    // Phase 3.3: Linked/Project-scope parameter <-> shared config sync (see
    // Host/SharedConfigSync.h). Declared after `parameters` and registryEntry
    // (it holds references to both), so it is destroyed before either.
    std::unique_ptr<meterscope::SharedConfigSync> sharedConfigSync;
    bool configFlushPending = false;

    // Part C: this instance's own embedded redundant copy of
    // SharedProjectConfig (architecture.md's State Persistence -> "A
    // redundant JSON copy embedded in every instance's own
    // getStateInformation()"). Populated from the <SharedConfig> sibling XML
    // in setStateInformation(); refreshed from the CURRENT in-memory
    // projectConfigs entry (if resolved) immediately before every
    // getStateInformation() call - see both methods' bodies. Read by
    // ProjectResolver (via the getOwnRedundantConfigCopy lambda passed to
    // its constructor) exactly once per new partition this instance enters.
    meterscope::SharedProjectConfig ownRedundantConfigCopy;

    // Part C: this instance's own persisted fallback-identity UUID
    // (architecture.md's REAPER Host Integration -> Fallback: "Track
    // identity is a UUID minted once per instance... persisted in that
    // instance's own non-parameter editor state"). Empty until either a
    // previous session minted one (loaded via setStateInformation()'s
    // <FallbackTrackId> sibling) or this session's own ProjectResolver mints
    // a fresh one on first becoming Unidentified (see ProjectResolver.cpp's
    // becomeUnidentified(), which writes back through the reference passed
    // to its constructor). Only ever meaningful for an Unidentified-scope
    // instance; ignored otherwise.
    juce::String fallbackTrackId;

    // Part C: <EditorState width="…" height="…"/> persistence. Stage 3
    // Phase 3.2 wires real read/write through the public
    // getEditorStateWidth()/getEditorStateHeight()/setEditorStateSize()
    // accessors above (architecture.md's Editor Bridge table). 0 means "no
    // size saved yet" (a fresh instance/an old Stage-1/Part-B state with no
    // <EditorState> sibling at all).
    int editorStateWidth = 0;
    int editorStateHeight = 0;

   #if METERSCOPE_REGISTRY_DUMP_LOG
    // Phase 2.2 Part B, task item 5: one process-wide debug dump Timer,
    // shared across every loaded instance via juce::SharedResourcePointer
    // (verified: juce_core/memory/juce_SharedResourcePointer.h) - see
    // RegistryDumpLogger.h's class doc for the "once per process, not once
    // per instance" lifetime argument.
    juce::SharedResourcePointer<meterscope::RegistryDumpLogger> registryDumpLogger;
   #endif

   #if METERSCOPE_TIER_B_SPIKE_LOG
    // Set (relaxed atomic store) at the end of every processBlock() below, so
    // the message-thread TierBSpikeLogger can log "isPlaying" without ever
    // calling AudioPlayHead from outside the audio thread (its contract
    // restricts it to processBlock() only - see TierBSpikeLogger.h).
    std::atomic<bool> lastKnownIsPlayingForSpikeLog { false };

    // Declared last: its constructor only stores a reference to `*this` and
    // starts a Timer (see TierBSpikeLogger.cpp) - it never reads any other
    // member during construction, but ordering it last keeps every other
    // member it will later read (reaperHostBridge, hostTrackName,
    // lastKnownIsPlayingForSpikeLog) already fully constructed regardless.
    meterscope::TierBSpikeLogger tierBSpikeLogger { *this };
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeterScopeAudioProcessor)
};
