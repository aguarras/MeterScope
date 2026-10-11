cmake_minimum_required(VERSION 3.15)

# Plugin formats: VST3, Standalone (AU is macOS-only, this system targets Windows).
# Target framework: JUCE 9.0.2 (C:\JUCE) - API surface used here is unchanged
# from JUCE 8; see plugins/MeterScope/.ideas/architecture.md's
# "JUCE 9.0.2 API Verification".
#
# Stage 1 (Foundation + Shell): build system + all 40 APVTS parameters from
# parameter-spec.md (automation disabled on all 40).
#
# Stage 2 Phase 2.1 (this file's current state): Core Measurement Engine
# (K-weighting, True-Peak sinc, sliding windows, LUFS-I/LRA histograms,
# reset/transport, peak decay - architecture.md Components 1-7) implemented in
# Source/DSP/. Audio remains a pure, bit-identical passthrough - MeterScope
# never writes to the buffer. Phase 2.2 (not yet implemented): Multi-Instance
# Shared Store + REAPER Host Integration + Per-Project Isolation.
#
# Stage 2 Phase 2.2, Step 1 (spike, PASSED 2026-09-26): the Tier B REAPER
# host-integration spike (Source/Host/) - vendored reaper_vst3_interfaces.h,
# ReaperHostBridge (juce::VST3ClientExtensions subclass), and the optional
# TierBSpikeLogger debug-log Timer, gated by METERSCOPE_TIER_B_SPIKE_LOG
# (default OFF, see below).
#
# Stage 2 Phase 2.2, Part B: identity resolution (Source/Host/ProjectResolver)
# + the per-project-partitioned SharedMeterRegistry singleton
# (Source/Host/SharedMeterRegistry), cross-instance write callbacks, the
# Fallback tier (discovery order + minted juce::Uuid, forceable via
# METERSCOPE_FORCE_UNIDENTIFIED_PROJECT), and the optional debug registry
# dump (Source/Host/RegistryDumpLogger, gated by METERSCOPE_REGISTRY_DUMP_LOG).
#
# Stage 2 Phase 2.2, Part C (this file's current state): projectConfigs/
# SharedProjectConfig (Source/Host/SharedProjectConfig) with per-project
# ProjExtState persistence (Source/Host/ProjectConfigPersistence) and a
# version-wins redundant copy embedded in getStateInformation(); the
# <EditorState>/<FallbackTrackId> non-APVTS XML siblings; a processor-owned
# juce::UndoManager passed to the APVTS; and a debug command-file mechanism
# added to RegistryDumpLogger (still gated by METERSCOPE_REGISTRY_DUMP_LOG)
# so plan.md's T2/T4 can be exercised before the WebView editor exists.
#
# Stage 3 (GUI, Phase 3.1, this file's current state): the finalized v7
# WebView mockup is folded in here - NEEDS_WEB_BROWSER TRUE, NEEDS_WEBVIEW2
# TRUE, juce_add_binary_data(), the JUCE_USE_WIN_WEBVIEW2* defines - see
# plugins/MeterScope/.ideas/mockups/v7-CMakeLists.txt (the snippet this was
# folded from) and troubleshooting/patterns/juce8-critical-patterns.md
# #9/#23. PLUGIN_CODE stays "Mesc" (already unique in this repo, chosen at
# Stage 1) rather than the v7 snippet's placeholder "Mtsc".
juce_add_plugin(MeterScope
    COMPANY_NAME "TES Lab"
    PLUGIN_MANUFACTURER_CODE Agus
    PLUGIN_CODE Mesc
    FORMATS VST3 Standalone
    PRODUCT_NAME "MeterScope"
    VERSION 1.0.1
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
)

# Stage 3 Phase 3.1: WebView UI resources (finalized v7 mockup, copied to
# Source/ui/public/ - see v7-integration-checklist.md step 1).
juce_add_binary_data(MeterScope_UIResources
    SOURCES
        Source/ui/public/index.html
        Source/ui/public/js/app.js
        Source/ui/public/js/juce/index.js
        Source/ui/public/js/juce/check_native_interop.js
)

# Source files
target_sources(MeterScope
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        # Stage 2 Phase 2.1: Core Measurement Engine (architecture.md Components 1-7),
        # ported from references/loudness-meter/loudness_meter_cockos.jsfx. Kept
        # framework-free (no JUCE includes) so each is independently unit-testable -
        # see architecture.md's Mitigation strategy #1.
        Source/DSP/KWeighting.cpp
        Source/DSP/TruePeakInterpolator.cpp
        Source/DSP/PeakMeter.cpp
        Source/DSP/BlockAccumulator.cpp
        Source/DSP/LoudnessHistogram.cpp
        Source/DSP/LraEngine.cpp
        Source/DSP/ResetController.cpp
        Source/DSP/MeteringEngine.cpp
        # Stage 2 Phase 2.2 Step 1 (Tier B spike): REAPER host integration.
        # reaper_vst3_interfaces.h is a vendored header only (no .cpp of its
        # own - it is #include'd by ReaperHostBridge.h/.cpp).
        Source/Host/ReaperHostBridge.cpp
        Source/Host/TierBSpikeLogger.cpp
        # Stage 2 Phase 2.2 Part B: project/track identity resolution + the
        # process-wide, per-project-partitioned shared meter registry.
        # ProjectKey.h is header-only (no .cpp of its own).
        Source/Host/SharedMeterRegistry.cpp
        Source/Host/ProjectResolver.cpp
        Source/Host/RegistryDumpLogger.cpp
        # Stage 2 Phase 2.2 Part C: per-project shared configuration
        # (projectConfigs/SharedProjectConfig) with ProjExtState persistence
        # + the redundant version-wins copy.
        Source/Host/SharedProjectConfig.cpp
        Source/Host/ProjectConfigPersistence.cpp
        # Stage 3 Phase 3.3: Linked/Project-scope APVTS parameters <-> the
        # project's shared config (propagation between linked instances).
        Source/Host/SharedConfigSync.cpp
)

# Task 7 (Phase 2.1 validation hook): OFF by default. Declared here (option()
# has no target-ordering constraint); the resulting compile definition is
# added below, alongside the rest of target_compile_definitions(), per
# juce8-critical-patterns.md #1's ordering rule (after target_link_libraries()
# and juce_generate_juce_header()).
option(METERSCOPE_VALIDATION_DUMP "Enable Phase 2.1 numeric-validation CSV dump" OFF)

# Phase 2.2 Step 1 (Tier B spike): OFF by default. When ON,
# Source/Host/TierBSpikeLogger.cpp compiles in a processor-owned ~2Hz
# message-thread juce::Timer that appends REAPER host-identity findings to
# %TEMP%\MeterScope_TierB_spike.log (see plan.md's Phase 2.2 "Step 1: Tier B
# spike (do first)"). ReaperHostBridge itself (the VST3ClientExtensions
# subclass that captures IReaperHostApplication) is NOT gated by this option -
# it is always compiled in, since later Phase 2.2 work depends on it
# unconditionally; only the debug logging is optional.
option(METERSCOPE_TIER_B_SPIKE_LOG "Enable Phase 2.2 Step 1 Tier B REAPER host-integration spike log" OFF)

# Phase 2.2 Part B, task item 4 (reworked in the Part B follow-up below): OFF
# by default. When ON, every instance's ProjectResolver (Source/Host/
# ProjectResolver.cpp's resolveOnce()) checks, every tick, whether its own
# resolved track display name contains the case-insensitive tag "[UNID]"; only
# an instance whose name currently contains that tag is forced into the
# Unidentified partition that tick (Master and monitoring FX are unaffected -
# see ProjectResolver.h's doc comment). This is per-instance and re-evaluated
# every ~0.5s tick (so renaming a track to add/remove the tag takes effect in
# either direction), unlike the original all-or-nothing "every instance forced
# Unidentified" design - REAPER loads a single plugin module per process, so
# an all-or-nothing switch cannot force only SOME instances Unidentified while
# others stay Identified in the same process, which plan.md's Phase 2.2 T9
# requires. Lets T9 be tested in a real REAPER instance, in both project tabs
# at once, without needing a non-REAPER host to force the condition.
option(METERSCOPE_FORCE_UNIDENTIFIED_PROJECT "Enable per-instance [UNID]-tag forcing into the Unidentified project partition (Phase 2.2 Part B fallback-tier testing; put \"[UNID]\" in a track's name to force that instance)" OFF)

# Phase 2.2 Part B, task item 5: OFF by default. When ON, compiles in
# RegistryDumpLogger (Source/Host/RegistryDumpLogger.h/.cpp), a single
# process-wide (see juce::SharedResourcePointer in PluginProcessor.h) ~2s
# debug Timer that appends every SharedMeterRegistry partition and its
# entries to %TEMP%\MeterScope_registry.log. Replaces plan.md's original
# DBG-based dump idea, which would be silent in Release builds.
option(METERSCOPE_REGISTRY_DUMP_LOG "Enable Phase 2.2 Part B debug registry dump (%TEMP%/MeterScope_registry.log)" OFF)

# Include paths
target_include_directories(MeterScope
    PRIVATE
        Source
)

# Required JUCE modules.
# juce_gui_extra is required by juce_audio_processors itself in JUCE 9.0.2
# (see juce_audio_processors.h's module dependencies) and is also what
# provides juce::WebBrowserComponent, used by Stage 3's WebView editor.
# MeterScope_UIResources (juce_add_binary_data target above) is linked so
# PluginEditor.cpp's getResource() can serve the embedded UI files.
target_link_libraries(MeterScope
    PRIVATE
        MeterScope_UIResources
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# CRITICAL: Generate JuceHeader.h (JUCE 8/9 requirement)
# Must come after target_link_libraries(), before target_compile_definitions()
juce_generate_juce_header(MeterScope)

# Compile definitions
# Stage 3 Phase 3.1 additions (see v7-CMakeLists.txt and
# troubleshooting/patterns/juce8-critical-patterns.md #9/#23):
#   JUCE_WEB_BROWSER=1                          - enable WebView
#   JUCE_USE_CURL=0                              - not needed for local HTML
#   JUCE_USE_WIN_WEBVIEW2=1                      - Windows WebView2 backend API
#   JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1  - REQUIRED alongside
#     NEEDS_WEBVIEW2 TRUE above (statically-linked WebView2LoaderStatic.lib):
#     without this, JUCE takes the dynamic LoadLibraryA("WebView2Loader.dll")
#     branch, which fails silently since no such DLL exists in this build
#     (pattern #23 - "navigation was cancelled" / zero msedgewebview2.exe
#     child processes).
target_compile_definitions(MeterScope
    PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_USE_WIN_WEBVIEW2=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
)

# Task 7 (Phase 2.1 validation hook): when ON, MeteringEngine dumps every
# 100ms block's raw values to a CSV file (path from the METERSCOPE_DUMP_CSV
# environment variable, read in prepareToPlay) so the orchestrator can
# numerically compare the built VST3 against the JSFX reference (pedalboard
# venv, per project memory). Writing from the audio thread is explicitly
# accepted for this validation-only build. When OFF (default), every
# validation-dump code path is compiled out entirely (see
# Source/DSP/MeteringEngine.h/.cpp's `#if METERSCOPE_VALIDATION_DUMP` guards).
if (METERSCOPE_VALIDATION_DUMP)
    target_compile_definitions(MeterScope PUBLIC METERSCOPE_VALIDATION_DUMP=1)
endif()

# Phase 2.2 Step 1 (Tier B spike): when ON, compiles in TierBSpikeLogger's
# ~2Hz debug-log Timer (see Source/Host/TierBSpikeLogger.h/.cpp). When OFF
# (default), every spike-log code path is compiled out entirely (`#if
# METERSCOPE_TIER_B_SPIKE_LOG` guards in PluginProcessor.h/.cpp and
# TierBSpikeLogger.h/.cpp) - ReaperHostBridge itself is unaffected by this
# option and is always built in.
if (METERSCOPE_TIER_B_SPIKE_LOG)
    target_compile_definitions(MeterScope PUBLIC METERSCOPE_TIER_B_SPIKE_LOG=1)
endif()

# Phase 2.2 Part B: see the option() declarations above for what each of
# these gates. Both compile down to nothing (an empty macro condition, or a
# `false` literal for METERSCOPE_FORCE_UNIDENTIFIED_PROJECT's PluginProcessor.cpp
# use) when OFF (default).
if (METERSCOPE_FORCE_UNIDENTIFIED_PROJECT)
    target_compile_definitions(MeterScope PUBLIC METERSCOPE_FORCE_UNIDENTIFIED_PROJECT=1)
endif()

if (METERSCOPE_REGISTRY_DUMP_LOG)
    target_compile_definitions(MeterScope PUBLIC METERSCOPE_REGISTRY_DUMP_LOG=1)
endif()
