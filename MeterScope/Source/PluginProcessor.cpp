// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Partial held-max reset: maps a measurement key (as sent by the UI) to a
// HeldMaxTarget. Peak and TruePeak share ONE register (errata #7), so either
// key clears both displays. Unknown key: deliberately a no-op.
void MeterScopeAudioProcessor::requestHeldMaxReset(const juce::String& measurementKey)
{
    using meterscope::HeldMaxTarget;

    if (measurementKey == "Peak" || measurementKey == "TruePeak")
        meteringEngine.requestHeldMaxReset(HeldMaxTarget::PeakRegister);
    else if (measurementKey == "RmsM")
        meteringEngine.requestHeldMaxReset(HeldMaxTarget::RmsMMax);
    else if (measurementKey == "LufsM")
        meteringEngine.requestHeldMaxReset(HeldMaxTarget::LufsMMax);
    else if (measurementKey == "LufsS")
        meteringEngine.requestHeldMaxReset(HeldMaxTarget::LufsSMax);
}

//==============================================================================
// Parameter layout - 40 parameters total, per plugins/MeterScope/.ideas/parameter-spec.md
// (Total Parameter Count: 1 link switch + 7 measurement selection + 1 mono/stereo
// mode + 1 reset-on-playback-start + 2 display settings + 28 level-indicator
// settings = 40). Automation is disabled on every parameter, per parameter-spec.md's
// General Rules ("none of these settings need to be automatable"). The manual
// `reset` action is NOT a parameter (it is dispatched via the Editor Bridge in
// Stage 3, per architecture.md's Reset & Transport Controller).
juce::AudioProcessorValueTreeState::ParameterLayout MeterScopeAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    //==========================================================================
    // linked (Bool, default true, Scope: Track)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "linked", 1 },
        "Linked",
        true,
        juce::AudioParameterBoolAttributes().withAutomatable(false)
    ));

    //==========================================================================
    // Measurement selection (Scope: Linked)

    // peakMode (Choice, default "Peak clips" = index 4)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "peakMode", 1 },
        "Peak Mode",
        juce::StringArray { "Off", "True Peak", "True Peak clips", "Peak", "Peak clips" },
        4,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)
    ));

    // lufsMMode (Choice, default "On + histogram" = index 2)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "lufsMMode", 1 },
        "LUFS-M Mode",
        juce::StringArray { "Off", "On", "On + histogram" },
        2,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)
    ));

    // showRmsM (Bool, default false)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "showRmsM", 1 },
        "Show RMS-M",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable(false)
    ));

    // showRmsI (Bool, default false)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "showRmsI", 1 },
        "Show RMS-I",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable(false)
    ));

    // showLufsS (Bool, default true)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "showLufsS", 1 },
        "Show LUFS-S",
        true,
        juce::AudioParameterBoolAttributes().withAutomatable(false)
    ));

    // showLra (Bool, default true)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "showLra", 1 },
        "Show LRA",
        true,
        juce::AudioParameterBoolAttributes().withAutomatable(false)
    ));

    // showLufsI (Bool, default true)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "showLufsI", 1 },
        "Show LUFS-I",
        true,
        juce::AudioParameterBoolAttributes().withAutomatable(false)
    ));

    //==========================================================================
    // monoMode (Choice, default Stereo = index 0, Scope: Track)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "monoMode", 1 },
        "Mono/Stereo",
        juce::StringArray { "Stereo", "Mono" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)
    ));

    //==========================================================================
    // resetOnPlay (Bool, default true, Scope: Project)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "resetOnPlay", 1 },
        "Reset On Playback Start",
        true,
        juce::AudioParameterBoolAttributes().withAutomatable(false)
    ));

    //==========================================================================
    // yScale (Float, 0.5 to 4.0, default 1.8, Scope: Project)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "yScale", 1 },
        "Y-Axis Scale",
        juce::NormalisableRange<float>(0.5f, 4.0f, 0.01f),
        1.8f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));

    // textSize (Int, -2 to 8, default 0, Scope: Project)
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "textSize", 1 },
        "Text Size",
        -2, 8, 0,
        juce::AudioParameterIntAttributes().withAutomatable(false)
    ));

    //==========================================================================
    // Level Indicators (Scope: Linked) - 7 measurements x 4 settings = 28 parameters.
    // Each measurement <m> has: ind<m>Mode (Choice: Off/Threshold/Range),
    // ind<m>Threshold, ind<m>RangeMin, ind<m>RangeMax (Float, -72.0 to 0.0 dB, step 0.1).
    // Defaults per parameter-spec.md's "Defaults" table (all modes Off).

    // --- TruePeak ---
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "indTruePeakMode", 1 },
        "True Peak Indicator Mode",
        juce::StringArray { "Off", "Threshold", "Range" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indTruePeakThreshold", 1 },
        "True Peak Indicator Threshold",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -1.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indTruePeakRangeMin", 1 },
        "True Peak Indicator Range Min",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -3.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indTruePeakRangeMax", 1 },
        "True Peak Indicator Range Max",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -1.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));

    // --- Peak ---
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "indPeakMode", 1 },
        "Peak Indicator Mode",
        juce::StringArray { "Off", "Threshold", "Range" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indPeakThreshold", 1 },
        "Peak Indicator Threshold",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indPeakRangeMin", 1 },
        "Peak Indicator Range Min",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -6.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indPeakRangeMax", 1 },
        "Peak Indicator Range Max",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));

    // --- RmsM ---
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "indRmsMMode", 1 },
        "RMS-M Indicator Mode",
        juce::StringArray { "Off", "Threshold", "Range" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indRmsMThreshold", 1 },
        "RMS-M Indicator Threshold",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -12.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indRmsMRangeMin", 1 },
        "RMS-M Indicator Range Min",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -20.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indRmsMRangeMax", 1 },
        "RMS-M Indicator Range Max",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -12.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));

    // --- RmsI ---
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "indRmsIMode", 1 },
        "RMS-I Indicator Mode",
        juce::StringArray { "Off", "Threshold", "Range" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indRmsIThreshold", 1 },
        "RMS-I Indicator Threshold",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -14.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indRmsIRangeMin", 1 },
        "RMS-I Indicator Range Min",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -20.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indRmsIRangeMax", 1 },
        "RMS-I Indicator Range Max",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -14.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));

    // --- LufsM ---
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "indLufsMMode", 1 },
        "LUFS-M Indicator Mode",
        juce::StringArray { "Off", "Threshold", "Range" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indLufsMThreshold", 1 },
        "LUFS-M Indicator Threshold",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -10.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indLufsMRangeMin", 1 },
        "LUFS-M Indicator Range Min",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -18.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indLufsMRangeMax", 1 },
        "LUFS-M Indicator Range Max",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -10.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));

    // --- LufsS ---
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "indLufsSMode", 1 },
        "LUFS-S Indicator Mode",
        juce::StringArray { "Off", "Threshold", "Range" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indLufsSThreshold", 1 },
        "LUFS-S Indicator Threshold",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -12.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indLufsSRangeMin", 1 },
        "LUFS-S Indicator Range Min",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -18.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indLufsSRangeMax", 1 },
        "LUFS-S Indicator Range Max",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -12.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));

    // --- LufsI ---
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "indLufsIMode", 1 },
        "LUFS-I Indicator Mode",
        juce::StringArray { "Off", "Threshold", "Range" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indLufsIThreshold", 1 },
        "LUFS-I Indicator Threshold",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -14.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indLufsIRangeMin", 1 },
        "LUFS-I Indicator Range Min",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -24.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "indLufsIRangeMax", 1 },
        "LUFS-I Indicator Range Max",
        juce::NormalisableRange<float>(-72.0f, 0.0f, 0.1f),
        -22.0f,
        juce::AudioParameterFloatAttributes().withAutomatable(false)
    ));

    // Total: 1 (linked) + 7 (measurement selection) + 1 (monoMode) + 1 (resetOnPlay)
    //      + 2 (yScale, textSize) + 28 (7 measurements x 4 indicator settings) = 40.
    return layout;
}

//==============================================================================
MeterScopeAudioProcessor::MeterScopeAudioProcessor()
    : AudioProcessor(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , parameters(*this, &undoManager, "Parameters", createParameterLayout())
{
    // Cache raw-parameter-value pointers (Task 4): no string lookups in
    // processBlock(). Only these two parameters are read by the native
    // engine at all (architecture.md's Parameter Mapping).
    peakModeParam = parameters.getRawParameterValue("peakMode");
    resetOnPlayParam = parameters.getRawParameterValue("resetOnPlay");

    jassert(peakModeParam != nullptr);
    jassert(resetOnPlayParam != nullptr);

    // Phase 2.2 Part B: register this instance with the process-wide
    // SharedMeterRegistry singleton BEFORE constructing anything that depends
    // on the returned Entry. meteringEngine already exists at this point
    // (member declared/initialized before this constructor body runs), so
    // getSnapshotPointer() is already valid.
    registryEntry = meterscope::SharedMeterRegistry::getInstance().registerInstance(
        this, meteringEngine.getSnapshotPointer());
    jassert(registryEntry != nullptr);

    if (registryEntry != nullptr)
    {
        // Cross-instance WRITE callbacks (architecture.md's "Cross-Instance
        // Writes"): closures over this instance's own `this`, invoked only by
        // ANOTHER instance's message-thread code after a same-partition
        // findEntry() lookup - wired here, at construction time, before any
        // editor for any instance can safely call them (architecture.md's
        // ordering requirement).
        registryEntry->remoteSetLinked = [this](bool linked)
        {
            if (auto* p = parameters.getParameter("linked"))
                p->setValueNotifyingHost(linked ? 1.0f : 0.0f);
        };

        registryEntry->remoteSetMono = [this](bool mono)
        {
            // monoMode is a 2-choice AudioParameterChoice (Stereo=0, Mono=1);
            // for a 2-choice parameter the normalised value IS the index
            // (0.0 or 1.0), so this needs no extra range math.
            if (auto* p = parameters.getParameter("monoMode"))
                p->setValueNotifyingHost(mono ? 1.0f : 0.0f);
        };

        registryEntry->remoteResetOne = [this](const juce::String& measurementKey)
        {
            // Partial held-max reset of just this measurement (see
            // requestHeldMaxReset()); remoteResetAll stays a full reset.
            requestHeldMaxReset(measurementKey);
        };

        registryEntry->remoteResetAll = [this]()
        {
            requestReset();
        };

        // Part C: flush THIS instance's own project partition's current
        // SharedProjectConfig to ProjExtState through this instance's own
        // ReaperHostBridge - see SharedMeterRegistry.h's Entry::
        // remoteFlushConfigToProjExtState doc comment and
        // ProjectConfigPersistence.h. A no-op if this instance's own
        // partition isn't Identified, or Tier B is unavailable, or the
        // partition has no config yet (nothing to flush).
        registryEntry->remoteFlushConfigToProjExtState = [this]()
        {
            flushSharedConfig();
        };

        // Phase 3.3: lock-free, cross-instance-readable Track-scope state
        // (getTrackList's linked/mono) and the Linked-scope parameter write
        // callback used by another instance's per-track settings panel.
        registryEntry->linkedRaw = parameters.getRawParameterValue("linked");
        registryEntry->monoRaw = parameters.getRawParameterValue("monoMode");
        registryEntry->remoteSetParam = [this](const juce::String& paramId, float rawValue)
        {
            setOwnLinkedParam(paramId, rawValue);
        };

        sharedConfigSync = std::make_unique<meterscope::SharedConfigSync>(parameters, *registryEntry);
    }

    // Phase 2.2 Part B: the processor-owned ~2Hz message-thread project/track
    // resolution timer - runs even when no editor is open (started here, not
    // from createEditor()). METERSCOPE_FORCE_UNIDENTIFIED_PROJECT (Part B
    // follow-up - see ProjectResolver.h's doc comment on the constructor's
    // `forceUnidentifiedProject` parameter) enables a per-instance, per-tick
    // check inside ProjectResolver::resolveOnce() that forces ONLY the
    // instance(s) whose resolved track name contains the case-insensitive tag
    // "[UNID]" into the Unidentified partition - not every instance in the
    // process - so plan.md's T9 can force some instances Unidentified while
    // others stay Identified, within the same REAPER process/module.
    if (registryEntry != nullptr)
    {
        projectResolver = std::make_unique<meterscope::ProjectResolver>(
            reaperHostBridge,
            *registryEntry,
            [this] { return hostTrackName; },
           #if METERSCOPE_FORCE_UNIDENTIFIED_PROJECT
            true,
           #else
            false,
           #endif
            fallbackTrackId,
            [this]() -> const meterscope::SharedProjectConfig& { return ownRedundantConfigCopy; }
        );

        // Phase 3.3: ride the resolver's existing ~2Hz tick for the shared
        // config reconcile/flush (no new timer), so linked instances stay in
        // sync even while their editor is closed.
        projectResolver->onTick = [this]()
        {
            syncSharedConfig();

            if (configFlushPending)
                flushSharedConfig();
        };
    }
}

MeterScopeAudioProcessor::~MeterScopeAudioProcessor()
{
    // Stop the resolution timer BEFORE invalidating the registry slot it
    // writes into (order matters even though both only ever run on the
    // single message thread - see SharedMeterRegistry.cpp's thread-safety
    // argument).
    projectResolver.reset();
    sharedConfigSync.reset();
    meterscope::SharedMeterRegistry::getInstance().unregisterInstance(this);
}

//==============================================================================
// Stage 3 Phase 3.3: Multi-Instance Bridge accessors
juce::String MeterScopeAudioProcessor::getUiTrackId() const
{
    if (registryEntry != nullptr)
        return meterscope::uiTrackIdFor(*registryEntry);

    // Registry full (never expected): a self-derived placeholder keeps THIS
    // TRACK working on its own.
    return juce::String("pending_") + juce::String::toHexString((juce::pointer_sized_int) this);
}

void MeterScopeAudioProcessor::syncSharedConfig()
{
    if (sharedConfigSync != nullptr && sharedConfigSync->reconcile())
        configFlushPending = true;
}

void MeterScopeAudioProcessor::flushSharedConfig()
{
    configFlushPending = false;

    // Part C's path: only Identified partitions have a project to write to,
    // always with the resolved ReaProject*, never 0.
    if (registryEntry == nullptr
        || registryEntry->projectKey.scope != ProjectKey::Scope::Identified)
        return;

    auto* api = reaperHostBridge.getReaperHostApplication();
    if (api == nullptr)
        return;

    // getProjectConfigCopy() takes a COPY under the registry's own lock
    // (post-review fix - see SharedMeterRegistry.h's doc comment); the
    // ProjExtState write below then runs on that copy, never while any
    // registry lock is held.
    if (auto cfg = meterscope::SharedMeterRegistry::getInstance().getProjectConfigCopy(registryEntry->projectKey))
        meterscope::writeSharedConfigToProjExtState(
            *api, const_cast<void*>(registryEntry->projectKey.reaProject), *cfg);
}

bool MeterScopeAudioProcessor::setOwnLinkedParam(const juce::String& paramId, float rawValue)
{
    return sharedConfigSync != nullptr && sharedConfigSync->setOwnLinkedParam(paramId, rawValue);
}

float MeterScopeAudioProcessor::getParameterDefaultRaw(const juce::String& paramId) const
{
    if (auto* p = parameters.getParameter(paramId))
        return p->convertFrom0to1(p->getDefaultValue());

    return 0.0f;
}

bool MeterScopeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // v1 supports mono and stereo only (creative-brief.md "Channels"), and
    // input must match output (MeterScope is an analysis-only passthrough:
    // it never changes channel count).
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void MeterScopeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    // All allocation/sizing happens here (real-time safety) - meteringEngine.prepare()
    // performs a full Reset() (architecture.md Component 6), matching the JSFX's
    // own unconditional `Reset();` call at @init.
    meteringEngine.prepare(sampleRate, getTotalNumOutputChannels());

   #if METERSCOPE_VALIDATION_DUMP
    // Task 7: validation-only build. Path comes from the environment variable
    // METERSCOPE_DUMP_CSV, read here (prepareToPlay), per the task's explicit
    // instruction. Compiled out entirely when METERSCOPE_VALIDATION_DUMP is OFF.
    const auto csvPath = juce::SystemStats::getEnvironmentVariable("METERSCOPE_DUMP_CSV", {});
    if (csvPath.isNotEmpty())
        meteringEngine.openValidationDump(csvPath.toStdString());

    // METERSCOPE_FORCE_PLAYING=1: forces the isPlayingOrRecording integrate
    // gate on for offline renders with no host transport (e.g. pedalboard).
    // Read once here and cached as a plain bool - never re-read per block.
    forcePlayingForValidation =
        juce::SystemStats::getEnvironmentVariable("METERSCOPE_FORCE_PLAYING", {}) == "1";
   #endif
}

void MeterScopeAudioProcessor::releaseResources()
{
   #if METERSCOPE_VALIDATION_DUMP
    meteringEngine.closeValidationDump();
   #endif
}

void MeterScopeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    // MeterScope is analysis-only: audio must remain a bit-identical
    // passthrough (creative-brief.md, architecture.md's Processing Chain) -
    // the metering side-chain below only READS `buffer`, it is never written.
    // Since input and output buses always match (see isBusesLayoutSupported),
    // the host-provided buffer already IS the output buffer, untouched.

    bool isPlayingOrRecording = false;
    bool hasPlayPosition = false;
    double playPositionSeconds = 0.0;

    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto position = hostPlayHead->getPosition())
        {
            // Reset & Transport Controller's "playing or recording" gate
            // (architecture.md Component 6 - maps play_state&1 to
            // getIsPlaying() || getIsRecording()).
            isPlayingOrRecording = position->getIsPlaying() || position->getIsRecording();

            if (const auto seconds = position->getTimeInSeconds())
            {
                playPositionSeconds = *seconds;
                hasPlayPosition = true;
            }
        }
    }

   #if METERSCOPE_VALIDATION_DUMP
    // Validation-only override - see the member's declaration comment. Does
    // NOT touch hasPlayPosition/playPositionSeconds, so resetOnPlay's
    // position-jump trigger is unaffected.
    if (forcePlayingForValidation)
        isPlayingOrRecording = true;
   #endif

   #if METERSCOPE_TIER_B_SPIKE_LOG
    // Phase 2.2 Step 1 spike log: relaxed atomic store only (real-time safe),
    // read by the message-thread TierBSpikeLogger - see its class doc for why
    // AudioPlayHead itself is never touched from that timer.
    lastKnownIsPlayingForSpikeLog.store (isPlayingOrRecording, std::memory_order_relaxed);
   #endif

    meterscope::MeteringEngine::BlockInput input;
    input.numChannels = buffer.getNumChannels();
    input.numSamples = buffer.getNumSamples();
    input.channelData = buffer.getArrayOfReadPointers();
    input.peakMode = (int) peakModeParam->load();
    input.resetOnPlayEnabled = resetOnPlayParam->load() > 0.5f;
    input.isPlayingOrRecording = isPlayingOrRecording;
    input.hasPlayPosition = hasPlayPosition;
    input.playPositionSeconds = playPositionSeconds;
    input.sampleRate = getSampleRate();

    meteringEngine.processBlock(input);
}

juce::AudioProcessorEditor* MeterScopeAudioProcessor::createEditor()
{
    return new MeterScopeAudioProcessorEditor(*this);
}

void MeterScopeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Part C: refresh this instance's own embedded redundant SharedProjectConfig
    // copy from this partition's CURRENT in-memory config (if resolved)
    // immediately before serializing - architecture.md's State Persistence:
    // "captured into every instance's own redundant copy the next time THAT
    // instance's getStateInformation() runs (i.e. on the next project
    // save)". If unresolved (Pending) or the partition has no config yet,
    // the previously-loaded/default copy is kept as-is. getProjectConfigCopy()
    // takes its copy under the registry's own lock and never returns a
    // pointer/reference into the registry (post-review fix) - safe even
    // though the VST3 host may call getStateInformation() off the message
    // thread, concurrently with message-thread registry mutation.
    // Phase 3.3: refreshed from IDENTIFIED partitions only. An Unidentified
    // partition's config is never persisted into the per-instance copy: its
    // pool can span projects, and that copy is also what this instance would
    // later contribute to an Identified project's version-wins merge (user
    // decision 2026-10-09: the Unidentified partition is isolated from the
    // redundant copies in both directions).
    if (registryEntry != nullptr && registryEntry->projectKey.scope == ProjectKey::Scope::Identified)
    {
        if (auto cfg = meterscope::SharedMeterRegistry::getInstance().getProjectConfigCopy (registryEntry->projectKey))
            ownRedundantConfigCopy = *cfg;
    }

    // Sibling elements are appended to a fresh COPY of the APVTS ValueTree
    // (parameters.copyState() already deep-copies), never to the live
    // parameters.state itself - keeps APVTS's own internal tree untouched by
    // this instance's own non-parameter state.
    auto state = parameters.copyState();

    // <EditorState> (Part C: persistence plumbing only - Stage 3 Phase 3.2
    // will read/write real width/height via getEditorStateSize()/
    // setEditorStateSize()). Always written (even 0/0) so a value is present
    // once any real size has been recorded.
    juce::ValueTree editorState ("EditorState");
    editorState.setProperty ("width", editorStateWidth, nullptr);
    editorState.setProperty ("height", editorStateHeight, nullptr);
    state.appendChild (editorState, nullptr);

    // <FallbackTrackId> - only written once this instance actually has one
    // (architecture.md: only meaningful for the Fallback/Unidentified tier).
    if (fallbackTrackId.isNotEmpty())
    {
        juce::ValueTree fallbackTree ("FallbackTrackId");
        fallbackTree.setProperty ("uuid", fallbackTrackId, nullptr);
        state.appendChild (fallbackTree, nullptr);
    }

    // <SharedConfig> - the redundant version-wins copy (see above).
    if (auto configXml = ownRedundantConfigCopy.toXml())
        state.appendChild (juce::ValueTree::fromXml (*configXml), nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MeterScopeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml(*xmlState);

        // Part C: read the non-APVTS sibling children BEFORE stripping them
        // (below) and handing the tree to APVTS. A missing element (an old
        // Stage-1/Part-B-saved state, which never had any of these three)
        // simply leaves each member at its default - the backward-
        // compatibility contract this phase's task requires.
        if (auto editorState = tree.getChildWithName ("EditorState"); editorState.isValid())
        {
            editorStateWidth  = (int) editorState.getProperty ("width", 0);
            editorStateHeight = (int) editorState.getProperty ("height", 0);
        }
        else
        {
            editorStateWidth = 0;
            editorStateHeight = 0;
        }

        if (auto fallbackTree = tree.getChildWithName ("FallbackTrackId"); fallbackTree.isValid())
            fallbackTrackId = fallbackTree.getProperty ("uuid", juce::String()).toString();
        else
            fallbackTrackId.clear();

        if (auto sharedConfigTree = tree.getChildWithName ("SharedConfig"); sharedConfigTree.isValid())
        {
            std::unique_ptr<juce::XmlElement> configXml (sharedConfigTree.createXml());
            ownRedundantConfigCopy = configXml != nullptr
                ? meterscope::SharedProjectConfig::fromXml (*configXml)
                : meterscope::SharedProjectConfig{};
        }
        else
        {
            ownRedundantConfigCopy = meterscope::SharedProjectConfig{}; // defaults (version 0)
        }

        // Strip the sibling children before handing the tree to APVTS -
        // AudioProcessorValueTreeState::replaceState() only expects its own
        // "PARAM" children structure; keeping these out is defensive
        // cleanliness (ValueTree itself tolerates unknown children fine).
        for (const char* childName : { "EditorState", "FallbackTrackId", "SharedConfig" })
        {
            auto child = tree.getChildWithName (childName);
            if (child.isValid())
                tree.removeChild (child, nullptr);
        }

        parameters.replaceState(tree);
        enforceIndicatorRangeInvariant();
    }
}

// Task 8 (pending Stage 1 item): parameter-spec.md's Level Indicators
// invariant - "RangeMin <= RangeMax. The UI does not let the two cross. If a
// loaded state has them crossed, they are swapped." Enforced here for all 7
// ind<m> measurements, since a state loaded from disk/a DAW project bypasses
// any UI-side guard.
void MeterScopeAudioProcessor::enforceIndicatorRangeInvariant()
{
    static const char* const measurements[] =
    {
        "TruePeak", "Peak", "RmsM", "RmsI", "LufsM", "LufsS", "LufsI"
    };

    for (const auto* measurement : measurements)
    {
        const juce::String minId = juce::String("ind") + measurement + "RangeMin";
        const juce::String maxId = juce::String("ind") + measurement + "RangeMax";

        auto* minParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter(minId));
        auto* maxParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter(maxId));

        jassert(minParam != nullptr && maxParam != nullptr);

        if (minParam != nullptr && maxParam != nullptr)
        {
            const float minValue = minParam->get();
            const float maxValue = maxParam->get();

            if (minValue > maxValue)
            {
                *minParam = maxValue;
                *maxParam = minValue;
            }
        }
    }
}

// Factory function
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MeterScopeAudioProcessor();
}
