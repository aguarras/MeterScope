// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <vector>

/**
 * MeterScopeAudioProcessorEditor — WebView UI scaffolding generated from the
 * finalized v7 mockup (plugins/MeterScope/.ideas/mockups/v7-ui.yaml /
 * v7-ui-test.html / v7-ui.html).
 *
 * ============================================================================
 * IMPORTANT — READ BEFORE ADAPTING IN STAGE 3
 * ============================================================================
 * This file is a Phase B scaffold, not a drop-in final implementation. It
 * assumes a PluginProcessor with:
 *   - a public `juce::AudioProcessorValueTreeState parameters;` member
 *     exposing exactly the 40 parameter IDs below (see parameter-spec.md),
 *   - automation DISABLED on all 40 (per parameter-spec.md "General Rules").
 * Adjust the processor accessor name/type to match whatever Stage 1
 * (Foundation) actually generates.
 *
 * This plugin's design (parameter-spec.md, creative-brief.md) is a shared,
 * multi-instance, project-wide meter: every instance shows/edits the SAME
 * shared configuration and can navigate to view OTHER tracks' published
 * meter data. That shared, cross-instance behaviour is NOT something
 * AudioProcessorValueTreeState or WebView relays can express on their own —
 * it requires a lock-free, multi-instance store that Stage 0 (/plan) has not
 * yet designed (see parameter-spec.md "Notes" and "Shared View State").
 *
 * The design decision this scaffold makes (see v7-ui.html's JUCE PARAMETER
 * BRIDGE header comment for the full rationale):
 *   - The 40 relays/attachments below always represent THIS instance's own
 *     currently-effective configuration for the track it is inserted on
 *     (the "host" track) — global values while linked, this track's own
 *     copy while unlinked.
 *   - Keeping every linked instance's mirror of the shared global
 *     configuration consistent (and correctly re-seeding an instance's own
 *     copy on unlink) is a native/backend synchronisation job — TODO(native)
 *     for /plan + gui-agent, NOT implemented here.
 *   - Every OTHER track's data (name, order, linked, mono, effective config,
 *     live meter values) and every non-parameter shared value (groups,
 *     groupFilter, soloTrack, hiddenTracks, trackGroup[track], the fixed 0dB
 *     held-max warning STATE is derived client-side so no extra storage is
 *     needed there) are exposed via native functions / events, marked
 *     TODO(native) throughout this file and v7-PluginEditor.cpp. Names and
 *     payload shapes below are PLACEHOLDERS, not a finalised API — do not
 *     treat them as authoritative without a Stage 0 (/plan) design pass.
 *
 * See v7-integration-checklist.md for the full list of follow-up work.
 * ============================================================================
 */
class MeterScopeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit MeterScopeAudioProcessorEditor(MeterScopeAudioProcessor&);
    ~MeterScopeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    // Timer callback (30Hz, the ONLY periodic work of the editor; no other
    // timer exists): reconciles this instance's shared-config parameters, then
    // diff-polls the registry / project config and emits only real changes
    // ("trackListUpdate", "trackConfigUpdate", "sharedViewStateUpdate",
    // "globalConfigUpdate", "projectScopeUpdate"), then publishes
    // "meterUpdate" for every track of this instance's own project partition
    // (Phase 3.3; Phase 3.2 published this instance's own track only). The
    // WebView's own 60fps rAF loop supplies the smooth peak-decay/loudness-
    // smoothing display dynamics between updates, unchanged from v1-v7.
    void timerCallback() override;

    // Resource provider (JUCE 8 required pattern — explicit URL mapping,
    // see troubleshooting/patterns/juce8-critical-patterns.md #8).
    std::optional<juce::WebBrowserComponent::Resource> getResource(const juce::String& url);

    // ------------------------------------------------------------------------
    // NATIVE FUNCTIONS registered via Options::withNativeFunction() in the
    // constructor. Phase 3.3: every one is now real (no TODO(native) left);
    // 13 functions in total (the 11 scaffold ones + getProjectScope and
    // setConfigParam).
    // ------------------------------------------------------------------------

    // requestEditorWidth(width, height): the ONE call the WebView UI uses for
    // every automatic window resize (view switch grow/shrink, SHOW ALL
    // re-fit). Real, host-driven resize: performs the actual
    // juce::Component::setSize(), clamped against
    // juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds())->userArea
    // and the active resize limits (see resized()/constructor). See
    // v7-ui.yaml -> window_resize_on_view_switch for the full semantics this
    // must implement (horizontal-only grow-on-entry, oversized-height
    // edge-case clamp, SHOW ALL re-fit).
    void returnKeyboardFocusToHost();
    void forwardKeyToHost(int keyCode, bool isKeyDown);

    void handleRequestEditorWidth(const juce::Array<juce::var>& args,
                                   juce::WebBrowserComponent::NativeFunctionCompletion completion);

    // getHostTrackId() / getTrackList() (Phase 3.3): real registry reads,
    // filtered by this instance's own ProjectKey -- REAPER track GUID, real
    // track names, REAPER track order (discovery order for Unidentified).
    void handleGetHostTrackId(const juce::Array<juce::var>& args,
                               juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void handleGetTrackList(const juce::Array<juce::var>& args,
                             juce::WebBrowserComponent::NativeFunctionCompletion completion);

    // getSharedViewState() / setSharedViewState(state) (Phase 3.3): groups,
    // groupFilter, soloTrack, hiddenTracks, trackGroup (parameter-spec.md
    // "Shared View State", explicitly NOT parameters) -- projectConfigs[ownKey].
    void handleGetSharedViewState(const juce::Array<juce::var>& args,
                                   juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void handleSetSharedViewState(const juce::Array<juce::var>& args,
                                   juce::WebBrowserComponent::NativeFunctionCompletion completion);

    // setTrackLinked(trackId, linked) / setTrackMono(trackId, mono)
    // (Phase 3.3): looked up by (own projectKey, trackId); this instance's own
    // entry is written locally, any other through its registry callback
    // (every processor stays the sole writer of its own APVTS).
    void handleSetTrackLinked(const juce::Array<juce::var>& args,
                               juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void handleSetTrackMono(const juce::Array<juce::var>& args,
                             juce::WebBrowserComponent::NativeFunctionCompletion completion);

    // resetTrackMeterMax(trackId, measurementKey) / resetTrackAll(trackId)
    // (Phase 3.3): local for this instance's own track, otherwise through the
    // target instance's remoteResetOne / remoteResetAll callback, which sets
    // bits in THAT instance's lock-free pending-reset mask (architecture.md
    // Component 6). measurementKey is one of "TruePeak","Peak","RmsM","RmsI",
    // "LufsM","LufsS","LufsI" (unknown / not-held-max keys are a no-op).
    void handleResetTrackMeterMax(const juce::Array<juce::var>& args,
                                   juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void handleResetTrackAll(const juce::Array<juce::var>& args,
                              juce::WebBrowserComponent::NativeFunctionCompletion completion);

    // getEditorStateSize() / setEditorStateSize(w, h): the single-track
    // window size, persisted as this instance's own NON-PARAMETER editor
    // state (like any normal JUCE editor's remembered size) — see
    // v7-ui.yaml -> window_resize_on_view_switch.persistence. Suggested
    // storage: a dedicated (non-APVTS) ValueTree/XML block alongside
    // getStateInformation()/setStateInformation(), or APVTS state XML with a
    // clearly-separated non-parameter child element.
    void handleGetEditorStateSize(const juce::Array<juce::var>& args,
                                   juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void handleSetEditorStateSize(const juce::Array<juce::var>& args,
                                   juce::WebBrowserComponent::NativeFunctionCompletion completion);

    // ------------------------------------------------------------------------
    // Stage 3 Phase 3.3 -- Multi-Instance Bridge.
    //
    // getProjectScope(): { scope: "identified" | "unidentified" | "pending" }
    // for THIS instance (drives the ALL TRACKS "Project could not be
    // identified" banner). Also the LAST native call of the JS boot sequence,
    // so it arms a one-shot full re-send of every event below once the page
    // has registered its listeners and built its track list.
    void handleGetProjectScope(const juce::Array<juce::var>& args,
                                juce::WebBrowserComponent::NativeFunctionCompletion completion);

    // setConfigParam(targetId, paramId, rawValue): writes ONE Linked-scope
    // setting of the configuration shown in a settings panel that is NOT
    // backed by this instance's own relays: targetId == "global" edits the
    // shared global configuration (the Global panel opened from an UNLINKED
    // instance -- the "known gap"), any other targetId is an UNLINKED track's
    // own configuration, routed to that instance through
    // Entry::remoteSetParam. rawValue is the denormalised APVTS value.
    void handleSetConfigParam(const juce::Array<juce::var>& args,
                               juce::WebBrowserComponent::NativeFunctionCompletion completion);

    // 30Hz timer work (see timerCallback). Each poll compares against what it
    // last SENT and emits only on a real change; `force` re-sends everything.
    void pollProjectScope(bool force);
    bool pollTrackList(bool force, const meterscope::SharedProjectConfig* cfg); // true if any track attribute changed
    void pollSharedConfig(bool force, const meterscope::SharedProjectConfig* cfg, bool trackAttrsChanged);
    void emitMeterUpdates(bool force);
    void emitEvent(const char* eventId, const juce::var& payload);
    std::vector<meterscope::SharedMeterRegistry::Entry*> getPartitionEntries() const;

    // Publishes one track's live meter snapshot to the WebView via
    // webView->emitEventIfBrowserIsVisible("meterUpdate", ...). `trackId` is
    // the id the page knows that track by. Phase 3.2 payload, unchanged; Phase
    // 3.3 calls it once per track of this instance's own project partition.
    void emitMeterUpdateFor(const meterscope::SharedMeterSnapshot& snapshot, const juce::String& trackId);

    // Registry-backed payload builders (message thread).
    const meterscope::SharedProjectConfig* refreshConfigCache();
    juce::var buildTrackSpec(const meterscope::SharedMeterRegistry::Entry& entry, int index,
                              const meterscope::SharedProjectConfig* cfg) const;
    juce::var buildTrackList(const std::vector<meterscope::SharedMeterRegistry::Entry*>& entries,
                              const meterscope::SharedProjectConfig* cfg) const;
    juce::var buildViewStateVar(const meterscope::SharedProjectConfig* cfg) const;
    juce::var buildConfigVar(const std::map<juce::String, float>* values) const;
    juce::String scopeString() const;

    // Reference to processor
    MeterScopeAudioProcessor& processorRef;

    // Phase 3.3 poll state (message thread only). Everything here is "what the
    // WebView was last told", so the 30Hz poll only emits real changes.
    //
    // lastEmittedVersions: SharedMeterSnapshot::version last emitted per
    // track id (cheap staleness skip, generalising Phase 3.2's single value; a
    // missing entry means "never emitted", so the first tick always emits).
    std::map<juce::String, std::uint64_t> lastEmittedVersions;
    juce::String lastScope;
    juce::String lastTrackStructureSig;
    std::map<juce::String, juce::String> lastTrackAttrs;
    juce::String lastViewStateJson;
    juce::String lastGlobalConfigJson;
    std::map<juce::String, juce::String> lastOwnConfigJson;
    bool resendAll = false;       // armed by getProjectScope(); one-shot
    bool wasVisible = false;

    // Single per-editor copy of this partition's SharedProjectConfig, refreshed
    // only when the cheap version counter moved (see refreshConfigCache()).
    std::optional<meterscope::SharedProjectConfig> cachedConfig;
    ProjectKey cachedConfigKey;
    juce::int64 cachedConfigVersion = -1;
    bool configChangedThisTick = false;

    // ========================================================================
    // ⚠️ CRITICAL MEMBER DECLARATION ORDER ⚠️
    // Order: Relays → WebView → Attachments
    // Members destroyed in REVERSE order (attachments first, then webView,
    // then relays) — see troubleshooting/patterns/juce8-critical-patterns.md
    // #11. DO NOT REORDER.
    // ========================================================================

    // ------------------------------------------------------------------------
    // 1) RELAYS FIRST — one per parameter-spec.md setting (40 total).
    // Parameter IDs below MUST match parameter-spec.md exactly.
    // ------------------------------------------------------------------------

    // Track-scope
    std::unique_ptr<juce::WebToggleButtonRelay> linkedRelay;
    std::unique_ptr<juce::WebComboBoxRelay> monoModeRelay; // Stereo, Mono

    // Measurement selection (Scope: Linked)
    std::unique_ptr<juce::WebComboBoxRelay> peakModeRelay; // Off, True Peak, True Peak clips, Peak, Peak clips
    std::unique_ptr<juce::WebComboBoxRelay> lufsMModeRelay; // Off, On, On + histogram
    std::unique_ptr<juce::WebToggleButtonRelay> showRmsMRelay;
    std::unique_ptr<juce::WebToggleButtonRelay> showRmsIRelay;
    std::unique_ptr<juce::WebToggleButtonRelay> showLufsSRelay;
    std::unique_ptr<juce::WebToggleButtonRelay> showLraRelay;
    std::unique_ptr<juce::WebToggleButtonRelay> showLufsIRelay;

    // Project scope
    std::unique_ptr<juce::WebToggleButtonRelay> resetOnPlayRelay;
    std::unique_ptr<juce::WebSliderRelay> yScaleRelay;
    std::unique_ptr<juce::WebSliderRelay> textSizeRelay;

    // Level indicators (Scope: Linked) — 7 measurements x 4 settings = 28
    std::unique_ptr<juce::WebComboBoxRelay> indTruePeakModeRelay;
    std::unique_ptr<juce::WebSliderRelay> indTruePeakThresholdRelay;
    std::unique_ptr<juce::WebSliderRelay> indTruePeakRangeMinRelay;
    std::unique_ptr<juce::WebSliderRelay> indTruePeakRangeMaxRelay;

    std::unique_ptr<juce::WebComboBoxRelay> indPeakModeRelay;
    std::unique_ptr<juce::WebSliderRelay> indPeakThresholdRelay;
    std::unique_ptr<juce::WebSliderRelay> indPeakRangeMinRelay;
    std::unique_ptr<juce::WebSliderRelay> indPeakRangeMaxRelay;

    std::unique_ptr<juce::WebComboBoxRelay> indRmsMModeRelay;
    std::unique_ptr<juce::WebSliderRelay> indRmsMThresholdRelay;
    std::unique_ptr<juce::WebSliderRelay> indRmsMRangeMinRelay;
    std::unique_ptr<juce::WebSliderRelay> indRmsMRangeMaxRelay;

    std::unique_ptr<juce::WebComboBoxRelay> indRmsIModeRelay;
    std::unique_ptr<juce::WebSliderRelay> indRmsIThresholdRelay;
    std::unique_ptr<juce::WebSliderRelay> indRmsIRangeMinRelay;
    std::unique_ptr<juce::WebSliderRelay> indRmsIRangeMaxRelay;

    std::unique_ptr<juce::WebComboBoxRelay> indLufsMModeRelay;
    std::unique_ptr<juce::WebSliderRelay> indLufsMThresholdRelay;
    std::unique_ptr<juce::WebSliderRelay> indLufsMRangeMinRelay;
    std::unique_ptr<juce::WebSliderRelay> indLufsMRangeMaxRelay;

    std::unique_ptr<juce::WebComboBoxRelay> indLufsSModeRelay;
    std::unique_ptr<juce::WebSliderRelay> indLufsSThresholdRelay;
    std::unique_ptr<juce::WebSliderRelay> indLufsSRangeMinRelay;
    std::unique_ptr<juce::WebSliderRelay> indLufsSRangeMaxRelay;

    std::unique_ptr<juce::WebComboBoxRelay> indLufsIModeRelay;
    std::unique_ptr<juce::WebSliderRelay> indLufsIThresholdRelay;
    std::unique_ptr<juce::WebSliderRelay> indLufsIRangeMinRelay;
    std::unique_ptr<juce::WebSliderRelay> indLufsIRangeMaxRelay;

    // ------------------------------------------------------------------------
    // 2) WEBVIEW SECOND (depends on relays via withOptionsFrom, and on the
    //    native function handlers above via withNativeFunction)
    // ------------------------------------------------------------------------
    std::unique_ptr<juce::WebBrowserComponent> webView;

    // ------------------------------------------------------------------------
    // 3) ATTACHMENTS LAST (depend on both relays and webView) — one per relay
    // ------------------------------------------------------------------------
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> linkedAttachment;
    std::unique_ptr<juce::WebComboBoxParameterAttachment> monoModeAttachment;

    std::unique_ptr<juce::WebComboBoxParameterAttachment> peakModeAttachment;
    std::unique_ptr<juce::WebComboBoxParameterAttachment> lufsMModeAttachment;
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> showRmsMAttachment;
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> showRmsIAttachment;
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> showLufsSAttachment;
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> showLraAttachment;
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> showLufsIAttachment;

    std::unique_ptr<juce::WebToggleButtonParameterAttachment> resetOnPlayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> yScaleAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> textSizeAttachment;

    std::unique_ptr<juce::WebComboBoxParameterAttachment> indTruePeakModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indTruePeakThresholdAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indTruePeakRangeMinAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indTruePeakRangeMaxAttachment;

    std::unique_ptr<juce::WebComboBoxParameterAttachment> indPeakModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indPeakThresholdAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indPeakRangeMinAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indPeakRangeMaxAttachment;

    std::unique_ptr<juce::WebComboBoxParameterAttachment> indRmsMModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indRmsMThresholdAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indRmsMRangeMinAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indRmsMRangeMaxAttachment;

    std::unique_ptr<juce::WebComboBoxParameterAttachment> indRmsIModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indRmsIThresholdAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indRmsIRangeMinAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indRmsIRangeMaxAttachment;

    std::unique_ptr<juce::WebComboBoxParameterAttachment> indLufsMModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indLufsMThresholdAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indLufsMRangeMinAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indLufsMRangeMaxAttachment;

    std::unique_ptr<juce::WebComboBoxParameterAttachment> indLufsSModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indLufsSThresholdAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indLufsSRangeMinAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indLufsSRangeMaxAttachment;

    std::unique_ptr<juce::WebComboBoxParameterAttachment> indLufsIModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indLufsIThresholdAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indLufsIRangeMinAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> indLufsIRangeMaxAttachment;

    // Phase 3.2: this instance's own in-memory copy of the single-track
    // window size, seeded from MeterScopeAudioProcessor::getEditorStateWidth
    // ()/getEditorStateHeight() in the constructor (falling back to 480x520
    // when neither was ever saved) and kept in sync with the processor's
    // real persisted <EditorState> XML by handleSetEditorStateSize() below --
    // see handleGetEditorStateSize/handleSetEditorStateSize.
    int savedEditorWidth = 528;
    int savedEditorHeight = 572;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeterScopeAudioProcessorEditor)
};
