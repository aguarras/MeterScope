// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#include "PluginEditor.h"
#include "BinaryData.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <set>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

//==============================================================================
// Constructor — CRITICAL: Initialize in correct order (Relays -> WebView ->
// Attachments). See troubleshooting/patterns/juce8-critical-patterns.md #11.
//==============================================================================

MeterScopeAudioProcessorEditor::MeterScopeAudioProcessorEditor(MeterScopeAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    // ------------------------------------------------------------------------
    // STEP 1: CREATE RELAYS (before WebView!) — parameter IDs match
    // parameter-spec.md exactly.
    // ------------------------------------------------------------------------
    linkedRelay = std::make_unique<juce::WebToggleButtonRelay>("linked");
    monoModeRelay = std::make_unique<juce::WebComboBoxRelay>("monoMode");

    peakModeRelay = std::make_unique<juce::WebComboBoxRelay>("peakMode");
    lufsMModeRelay = std::make_unique<juce::WebComboBoxRelay>("lufsMMode");
    showRmsMRelay = std::make_unique<juce::WebToggleButtonRelay>("showRmsM");
    showRmsIRelay = std::make_unique<juce::WebToggleButtonRelay>("showRmsI");
    showLufsSRelay = std::make_unique<juce::WebToggleButtonRelay>("showLufsS");
    showLraRelay = std::make_unique<juce::WebToggleButtonRelay>("showLra");
    showLufsIRelay = std::make_unique<juce::WebToggleButtonRelay>("showLufsI");

    resetOnPlayRelay = std::make_unique<juce::WebToggleButtonRelay>("resetOnPlay");
    yScaleRelay = std::make_unique<juce::WebSliderRelay>("yScale");
    textSizeRelay = std::make_unique<juce::WebSliderRelay>("textSize");

    indTruePeakModeRelay = std::make_unique<juce::WebComboBoxRelay>("indTruePeakMode");
    indTruePeakThresholdRelay = std::make_unique<juce::WebSliderRelay>("indTruePeakThreshold");
    indTruePeakRangeMinRelay = std::make_unique<juce::WebSliderRelay>("indTruePeakRangeMin");
    indTruePeakRangeMaxRelay = std::make_unique<juce::WebSliderRelay>("indTruePeakRangeMax");

    indPeakModeRelay = std::make_unique<juce::WebComboBoxRelay>("indPeakMode");
    indPeakThresholdRelay = std::make_unique<juce::WebSliderRelay>("indPeakThreshold");
    indPeakRangeMinRelay = std::make_unique<juce::WebSliderRelay>("indPeakRangeMin");
    indPeakRangeMaxRelay = std::make_unique<juce::WebSliderRelay>("indPeakRangeMax");

    indRmsMModeRelay = std::make_unique<juce::WebComboBoxRelay>("indRmsMMode");
    indRmsMThresholdRelay = std::make_unique<juce::WebSliderRelay>("indRmsMThreshold");
    indRmsMRangeMinRelay = std::make_unique<juce::WebSliderRelay>("indRmsMRangeMin");
    indRmsMRangeMaxRelay = std::make_unique<juce::WebSliderRelay>("indRmsMRangeMax");

    indRmsIModeRelay = std::make_unique<juce::WebComboBoxRelay>("indRmsIMode");
    indRmsIThresholdRelay = std::make_unique<juce::WebSliderRelay>("indRmsIThreshold");
    indRmsIRangeMinRelay = std::make_unique<juce::WebSliderRelay>("indRmsIRangeMin");
    indRmsIRangeMaxRelay = std::make_unique<juce::WebSliderRelay>("indRmsIRangeMax");

    indLufsMModeRelay = std::make_unique<juce::WebComboBoxRelay>("indLufsMMode");
    indLufsMThresholdRelay = std::make_unique<juce::WebSliderRelay>("indLufsMThreshold");
    indLufsMRangeMinRelay = std::make_unique<juce::WebSliderRelay>("indLufsMRangeMin");
    indLufsMRangeMaxRelay = std::make_unique<juce::WebSliderRelay>("indLufsMRangeMax");

    indLufsSModeRelay = std::make_unique<juce::WebComboBoxRelay>("indLufsSMode");
    indLufsSThresholdRelay = std::make_unique<juce::WebSliderRelay>("indLufsSThreshold");
    indLufsSRangeMinRelay = std::make_unique<juce::WebSliderRelay>("indLufsSRangeMin");
    indLufsSRangeMaxRelay = std::make_unique<juce::WebSliderRelay>("indLufsSRangeMax");

    indLufsIModeRelay = std::make_unique<juce::WebComboBoxRelay>("indLufsIMode");
    indLufsIThresholdRelay = std::make_unique<juce::WebSliderRelay>("indLufsIThreshold");
    indLufsIRangeMinRelay = std::make_unique<juce::WebSliderRelay>("indLufsIRangeMin");
    indLufsIRangeMaxRelay = std::make_unique<juce::WebSliderRelay>("indLufsIRangeMax");

    // ------------------------------------------------------------------------
    // STEP 2: CREATE WEBVIEW (with relay options + native functions)
    // ------------------------------------------------------------------------
    webView = std::make_unique<juce::WebBrowserComponent>(
        juce::WebBrowserComponent::Options{}
           #if JUCE_WINDOWS
            // REQUIRED on Windows: Options::Backend::defaultBackend resolves
            // to legacy Internet Explorer, which does not support
            // withResourceProvider() and cancels navigation to the
            // resource-provider root. See
            // troubleshooting/patterns/juce8-critical-patterns.md #23 and
            // plugins/SimpleGain/Source/PluginEditor.cpp for the working
            // reference implementation this scaffold follows.
            .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
            .withWinWebView2Options(
                juce::WebBrowserComponent::Options::WinWebView2{}
                    // REQUIRED: plugin host processes are often denied write
                    // access to WebView2's default user data location.
                    .withUserDataFolder(juce::File::getSpecialLocation(
                        juce::File::SpecialLocationType::tempDirectory))
                    // Paint WebView2's surface in the page's own background
                    // colour until the page renders (no white flash).
                    .withBackgroundColour(juce::Colour(0xff0A0B0C))
            )
           #endif

            // REQUIRED: Enable JUCE frontend library
            .withNativeIntegrationEnabled()

            // REQUIRED: Resource provider for embedded files (Pattern #8)
            .withResourceProvider([this](const auto& url) { return getResource(url); })

            // OPTIONAL: FL Studio fix (prevents blank screen on focus loss)
            .withKeepPageLoadedWhenBrowserIsHidden()

            // REQUIRED: Register each relay with WebView
            .withOptionsFrom(*linkedRelay)
            .withOptionsFrom(*monoModeRelay)
            .withOptionsFrom(*peakModeRelay)
            .withOptionsFrom(*lufsMModeRelay)
            .withOptionsFrom(*showRmsMRelay)
            .withOptionsFrom(*showRmsIRelay)
            .withOptionsFrom(*showLufsSRelay)
            .withOptionsFrom(*showLraRelay)
            .withOptionsFrom(*showLufsIRelay)
            .withOptionsFrom(*resetOnPlayRelay)
            .withOptionsFrom(*yScaleRelay)
            .withOptionsFrom(*textSizeRelay)
            .withOptionsFrom(*indTruePeakModeRelay)
            .withOptionsFrom(*indTruePeakThresholdRelay)
            .withOptionsFrom(*indTruePeakRangeMinRelay)
            .withOptionsFrom(*indTruePeakRangeMaxRelay)
            .withOptionsFrom(*indPeakModeRelay)
            .withOptionsFrom(*indPeakThresholdRelay)
            .withOptionsFrom(*indPeakRangeMinRelay)
            .withOptionsFrom(*indPeakRangeMaxRelay)
            .withOptionsFrom(*indRmsMModeRelay)
            .withOptionsFrom(*indRmsMThresholdRelay)
            .withOptionsFrom(*indRmsMRangeMinRelay)
            .withOptionsFrom(*indRmsMRangeMaxRelay)
            .withOptionsFrom(*indRmsIModeRelay)
            .withOptionsFrom(*indRmsIThresholdRelay)
            .withOptionsFrom(*indRmsIRangeMinRelay)
            .withOptionsFrom(*indRmsIRangeMaxRelay)
            .withOptionsFrom(*indLufsMModeRelay)
            .withOptionsFrom(*indLufsMThresholdRelay)
            .withOptionsFrom(*indLufsMRangeMinRelay)
            .withOptionsFrom(*indLufsMRangeMaxRelay)
            .withOptionsFrom(*indLufsSModeRelay)
            .withOptionsFrom(*indLufsSThresholdRelay)
            .withOptionsFrom(*indLufsSRangeMinRelay)
            .withOptionsFrom(*indLufsSRangeMaxRelay)
            .withOptionsFrom(*indLufsIModeRelay)
            .withOptionsFrom(*indLufsIThresholdRelay)
            .withOptionsFrom(*indLufsIRangeMinRelay)
            .withOptionsFrom(*indLufsIRangeMaxRelay)

            // Native functions (see the .h file for the full TODO(native)
            // rationale on every one of these -- placeholders, not a
            // finalised API).
            // Keyboard focus hand-back (see app.js): outside text fields the
            // page returns focus to this native editor, whose unused keys JUCE
            // forwards to the host, so Space/Enter keep driving the transport.
            .withNativeFunction("returnKeyboardFocusToHost",
                [this](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    returnKeyboardFocusToHost();
                    completion(juce::var());
                })
            .withNativeFunction("forwardKeyToHost",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    returnKeyboardFocusToHost();
                    if (args.size() >= 2)
                        forwardKeyToHost((int) args[0], (bool) args[1]);
                    completion(juce::var());
                })
            .withNativeFunction("requestEditorWidth",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    handleRequestEditorWidth(args, std::move(completion));
                })
            .withNativeFunction("getHostTrackId",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    handleGetHostTrackId(args, std::move(completion));
                })
            .withNativeFunction("getTrackList",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    handleGetTrackList(args, std::move(completion));
                })
            .withNativeFunction("getSharedViewState",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    handleGetSharedViewState(args, std::move(completion));
                })
            .withNativeFunction("setSharedViewState",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    handleSetSharedViewState(args, std::move(completion));
                })
            .withNativeFunction("setTrackLinked",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    handleSetTrackLinked(args, std::move(completion));
                })
            .withNativeFunction("setTrackMono",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    handleSetTrackMono(args, std::move(completion));
                })
            .withNativeFunction("resetTrackMeterMax",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    handleResetTrackMeterMax(args, std::move(completion));
                })
            .withNativeFunction("resetTrackAll",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    handleResetTrackAll(args, std::move(completion));
                })
            .withNativeFunction("getProjectScope",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    handleGetProjectScope(args, std::move(completion));
                })
            .withNativeFunction("setConfigParam",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    handleSetConfigParam(args, std::move(completion));
                })
            .withNativeFunction("getEditorStateSize",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    handleGetEditorStateSize(args, std::move(completion));
                })
            .withNativeFunction("setEditorStateSize",
                [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                    handleSetEditorStateSize(args, std::move(completion));
                })
    );

    // ------------------------------------------------------------------------
    // STEP 3: CREATE PARAMETER ATTACHMENTS (after WebView!)
    // JUCE 8: three-parameter constructor (parameter, relay, undoManager) --
    // see troubleshooting/patterns/juce8-critical-patterns.md #12.
    // TODO(Stage 1): confirm processorRef.parameters is the actual APVTS
    // member name/type Foundation generates.
    // ------------------------------------------------------------------------
    linkedAttachment = std::make_unique<juce::WebToggleButtonParameterAttachment>(
        *processorRef.parameters.getParameter("linked"), *linkedRelay, &processorRef.getUndoManager());
    monoModeAttachment = std::make_unique<juce::WebComboBoxParameterAttachment>(
        *processorRef.parameters.getParameter("monoMode"), *monoModeRelay, &processorRef.getUndoManager());

    peakModeAttachment = std::make_unique<juce::WebComboBoxParameterAttachment>(
        *processorRef.parameters.getParameter("peakMode"), *peakModeRelay, &processorRef.getUndoManager());
    lufsMModeAttachment = std::make_unique<juce::WebComboBoxParameterAttachment>(
        *processorRef.parameters.getParameter("lufsMMode"), *lufsMModeRelay, &processorRef.getUndoManager());
    showRmsMAttachment = std::make_unique<juce::WebToggleButtonParameterAttachment>(
        *processorRef.parameters.getParameter("showRmsM"), *showRmsMRelay, &processorRef.getUndoManager());
    showRmsIAttachment = std::make_unique<juce::WebToggleButtonParameterAttachment>(
        *processorRef.parameters.getParameter("showRmsI"), *showRmsIRelay, &processorRef.getUndoManager());
    showLufsSAttachment = std::make_unique<juce::WebToggleButtonParameterAttachment>(
        *processorRef.parameters.getParameter("showLufsS"), *showLufsSRelay, &processorRef.getUndoManager());
    showLraAttachment = std::make_unique<juce::WebToggleButtonParameterAttachment>(
        *processorRef.parameters.getParameter("showLra"), *showLraRelay, &processorRef.getUndoManager());
    showLufsIAttachment = std::make_unique<juce::WebToggleButtonParameterAttachment>(
        *processorRef.parameters.getParameter("showLufsI"), *showLufsIRelay, &processorRef.getUndoManager());

    resetOnPlayAttachment = std::make_unique<juce::WebToggleButtonParameterAttachment>(
        *processorRef.parameters.getParameter("resetOnPlay"), *resetOnPlayRelay, &processorRef.getUndoManager());
    yScaleAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("yScale"), *yScaleRelay, &processorRef.getUndoManager());
    textSizeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("textSize"), *textSizeRelay, &processorRef.getUndoManager());

    indTruePeakModeAttachment = std::make_unique<juce::WebComboBoxParameterAttachment>(
        *processorRef.parameters.getParameter("indTruePeakMode"), *indTruePeakModeRelay, &processorRef.getUndoManager());
    indTruePeakThresholdAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indTruePeakThreshold"), *indTruePeakThresholdRelay, &processorRef.getUndoManager());
    indTruePeakRangeMinAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indTruePeakRangeMin"), *indTruePeakRangeMinRelay, &processorRef.getUndoManager());
    indTruePeakRangeMaxAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indTruePeakRangeMax"), *indTruePeakRangeMaxRelay, &processorRef.getUndoManager());

    indPeakModeAttachment = std::make_unique<juce::WebComboBoxParameterAttachment>(
        *processorRef.parameters.getParameter("indPeakMode"), *indPeakModeRelay, &processorRef.getUndoManager());
    indPeakThresholdAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indPeakThreshold"), *indPeakThresholdRelay, &processorRef.getUndoManager());
    indPeakRangeMinAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indPeakRangeMin"), *indPeakRangeMinRelay, &processorRef.getUndoManager());
    indPeakRangeMaxAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indPeakRangeMax"), *indPeakRangeMaxRelay, &processorRef.getUndoManager());

    indRmsMModeAttachment = std::make_unique<juce::WebComboBoxParameterAttachment>(
        *processorRef.parameters.getParameter("indRmsMMode"), *indRmsMModeRelay, &processorRef.getUndoManager());
    indRmsMThresholdAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indRmsMThreshold"), *indRmsMThresholdRelay, &processorRef.getUndoManager());
    indRmsMRangeMinAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indRmsMRangeMin"), *indRmsMRangeMinRelay, &processorRef.getUndoManager());
    indRmsMRangeMaxAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indRmsMRangeMax"), *indRmsMRangeMaxRelay, &processorRef.getUndoManager());

    indRmsIModeAttachment = std::make_unique<juce::WebComboBoxParameterAttachment>(
        *processorRef.parameters.getParameter("indRmsIMode"), *indRmsIModeRelay, &processorRef.getUndoManager());
    indRmsIThresholdAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indRmsIThreshold"), *indRmsIThresholdRelay, &processorRef.getUndoManager());
    indRmsIRangeMinAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indRmsIRangeMin"), *indRmsIRangeMinRelay, &processorRef.getUndoManager());
    indRmsIRangeMaxAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indRmsIRangeMax"), *indRmsIRangeMaxRelay, &processorRef.getUndoManager());

    indLufsMModeAttachment = std::make_unique<juce::WebComboBoxParameterAttachment>(
        *processorRef.parameters.getParameter("indLufsMMode"), *indLufsMModeRelay, &processorRef.getUndoManager());
    indLufsMThresholdAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indLufsMThreshold"), *indLufsMThresholdRelay, &processorRef.getUndoManager());
    indLufsMRangeMinAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indLufsMRangeMin"), *indLufsMRangeMinRelay, &processorRef.getUndoManager());
    indLufsMRangeMaxAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indLufsMRangeMax"), *indLufsMRangeMaxRelay, &processorRef.getUndoManager());

    indLufsSModeAttachment = std::make_unique<juce::WebComboBoxParameterAttachment>(
        *processorRef.parameters.getParameter("indLufsSMode"), *indLufsSModeRelay, &processorRef.getUndoManager());
    indLufsSThresholdAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indLufsSThreshold"), *indLufsSThresholdRelay, &processorRef.getUndoManager());
    indLufsSRangeMinAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indLufsSRangeMin"), *indLufsSRangeMinRelay, &processorRef.getUndoManager());
    indLufsSRangeMaxAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indLufsSRangeMax"), *indLufsSRangeMaxRelay, &processorRef.getUndoManager());

    indLufsIModeAttachment = std::make_unique<juce::WebComboBoxParameterAttachment>(
        *processorRef.parameters.getParameter("indLufsIMode"), *indLufsIModeRelay, &processorRef.getUndoManager());
    indLufsIThresholdAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indLufsIThreshold"), *indLufsIThresholdRelay, &processorRef.getUndoManager());
    indLufsIRangeMinAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indLufsIRangeMin"), *indLufsIRangeMinRelay, &processorRef.getUndoManager());
    indLufsIRangeMaxAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *processorRef.parameters.getParameter("indLufsIRangeMax"), *indLufsIRangeMaxRelay, &processorRef.getUndoManager());

    // ------------------------------------------------------------------------
    // WEBVIEW SETUP
    // ------------------------------------------------------------------------
    webView->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
    addAndMakeVisible(*webView);

    // ------------------------------------------------------------------------
    // WINDOW SIZE (from v7-ui.yaml -> window: width=480, height=520,
    // resizable=true, min 360x420). The upper bound is deliberately
    // generous/permissive here (the real, meaningful cap on automatic
    // grow/re-fit is computed explicitly inside handleRequestEditorWidth()
    // against the actual screen -- see v7-ui.yaml ->
    // window_resize_on_view_switch); ALL TRACKS' natural size can legitimately
    // need to be much larger than max_width_this_track/max_height_this_track
    // (1400x1000), which only bound MANUAL resize while in THIS TRACK
    // per the yaml -- TODO(plan/gui-agent): decide whether to enforce that
    // THIS-TRACK-only manual ceiling with a view-aware constrainer, or accept
    // the simpler single generous ceiling used here.
    //
    // Phase 3.2: seed from this instance's own persisted <EditorState> size
    // (Phase 2.2 Part C) if one was ever saved, so the single-track window
    // size survives a plugin close/reopen and a REAPER project save/reload,
    // not just an editor-open session -- falls back to the v7 default
    // (480x520) when either dimension is 0 (no size saved yet, e.g. a fresh
    // instance or an old pre-Part-C state). The v7 JS layer's own
    // ResizeObserver (captureSingleTrackSizeIfOwn() in index.html) already
    // keeps this persisted value current on every THIS-TRACK resize -- see
    // this phase's own status note in plan.md for why handleRequestEditorWidth
    // itself needed no corresponding change.
    // ------------------------------------------------------------------------
    savedEditorWidth = processorRef.getEditorStateWidth() > 0 ? processorRef.getEditorStateWidth() : 528;
    savedEditorHeight = processorRef.getEditorStateHeight() > 0 ? processorRef.getEditorStateHeight() : 572;
    setWantsKeyboardFocus(true); // receives focus back from the WebView (see returnKeyboardFocusToHost)
    setResizable(true, true);
    // UI zoom 1.1 (index.html .plugin-frame): the 360x420 CSS-px layout
    // floor and the 480x520 default become 396x462 and 528x572 window px.
    setResizeLimits(396, 462, 6000, 4000);
    setSize(savedEditorWidth, savedEditorHeight);

    // Publishes this track's live meter snapshot to the WebView. Suggested
    // rate: 30Hz (comfortably faster than the JSFX's own ~100ms raw
    // measurement tick; the WebView's own 60fps rAF loop supplies the smooth
    // peak-decay/loudness-smoothing display dynamics between updates).
    startTimerHz(30);
}

//==============================================================================
// Destructor
//==============================================================================

MeterScopeAudioProcessorEditor::~MeterScopeAudioProcessorEditor()
{
    // Members are automatically destroyed in reverse order of declaration:
    // 1. Attachments destroyed first (stop calling evaluateJavascript)
    // 2. WebView destroyed second (safe, attachments are gone)
    // 3. Relays destroyed last (safe, nothing using them)
}

//==============================================================================
// AudioProcessorEditor Overrides
//==============================================================================

void MeterScopeAudioProcessorEditor::returnKeyboardFocusToHost()
{
    grabKeyboardFocus();

   #if JUCE_WINDOWS
    // JUCE skips the native SetFocus when it already thinks this component is
    // focused, but the real Win32 focus is inside WebView2's child window.
    if (auto* peer = getPeer())
        ::SetFocus(static_cast<HWND>(peer->getNativeHandle()));
   #endif
}

void MeterScopeAudioProcessorEditor::forwardKeyToHost(int keyCode, bool isKeyDown)
{
   #if JUCE_WINDOWS
    auto* peer = getPeer();
    if (peer == nullptr || processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        return;

    const auto editorWindow = static_cast<HWND>(peer->getNativeHandle());

    // Same path JUCE uses for keys a native editor doesn't consume: post the
    // key message to the host window that contains the editor. Enter is the
    // exception: REAPER's FX window is a dialog, which turns Enter into its
    // default "OK" button and never runs the shortcut, so Enter goes to the
    // host's main window (the FX window's owner) instead.
    const auto target = keyCode == VK_RETURN ? ::GetAncestor(editorWindow, GA_ROOTOWNER)
                                             : ::GetParent(editorWindow);
    if (target == nullptr || target == editorWindow)
        return;

    const auto scanCode = (LPARAM) ::MapVirtualKeyW((UINT) keyCode, MAPVK_VK_TO_VSC);
    const auto lParam = 1 | (scanCode << 16) | (isKeyDown ? 0 : ((LPARAM) 3 << 30));
    ::PostMessageW(target, isKeyDown ? WM_KEYDOWN : WM_KEYUP, (WPARAM) keyCode, lParam);
   #else
    juce::ignoreUnused(keyCode, isKeyDown);
   #endif
}

bool MeterScopeAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    // Every other unused key reaches the host through JUCE's own forwarding;
    // Enter needs the detour described in forwardKeyToHost().
   #if JUCE_WINDOWS
    if (key.getKeyCode() == juce::KeyPress::returnKey
        && processorRef.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
    {
        forwardKeyToHost(VK_RETURN, true);
        forwardKeyToHost(VK_RETURN, false);
        return true;
    }
   #endif
    juce::ignoreUnused(key);
    return false;
}

void MeterScopeAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Shown instantly while WebView2 starts up; matches the page's body
    // background (#0A0B0C) so the UI appears over the same dark surface.
    g.fillAll(juce::Colour(0xff0A0B0C));
}

void MeterScopeAudioProcessorEditor::resized()
{
    webView->setBounds(getLocalBounds());
}

//==============================================================================
// Phase 3.3 helpers
//==============================================================================

namespace
{
    // Choice tables: order MUST match parameter-spec.md / the Stage 1 choice
    // lists AND index.html's PEAK_MODE_VALUES / LUFS_M_MODE_VALUES /
    // IND_MODE_VALUES.
    constexpr const char* kPeakModeNames[] = { "off", "tp", "tpclips", "peak", "peakclips" };
    constexpr const char* kLufsMModeNames[] = { "off", "on", "hist" };
    constexpr const char* kIndModeNames[] = { "off", "threshold", "range" };
    constexpr const char* kMeasurements[] = { "TruePeak", "Peak", "RmsM", "RmsI", "LufsM", "LufsS", "LufsI" };

    juce::String toJson(const juce::var& v)
    {
        return juce::JSON::toString(v, true);
    }

    // dB / scale values are stored as float; widening a value such as -3.3f to
    // double prints as -3.29999995231628 -- round to a tidy 4 decimals.
    double tidy(float v)
    {
        return std::round((double) v * 10000.0) / 10000.0;
    }

    juce::var makeObjectVar(const std::function<void(juce::DynamicObject&)>& fill)
    {
        auto* obj = new juce::DynamicObject();
        fill(*obj);
        return juce::var(obj);
    }
}

void MeterScopeAudioProcessorEditor::emitEvent(const char* eventId, const juce::var& payload)
{
    if (webView != nullptr)
        webView->emitEventIfBrowserIsVisible(eventId, payload);
}

std::vector<meterscope::SharedMeterRegistry::Entry*> MeterScopeAudioProcessorEditor::getPartitionEntries() const
{
    // Always filtered by THIS instance's own ProjectKey (there is no
    // unfiltered accessor). A Pending instance's key is unique to itself, so
    // this returns just itself until it is resolved.
    if (auto* own = processorRef.getRegistryEntry())
        return meterscope::SharedMeterRegistry::getInstance().getActiveEntriesSortedByOrder(own->projectKey);

    return {};
}

juce::String MeterScopeAudioProcessorEditor::scopeString() const
{
    auto* own = processorRef.getRegistryEntry();

    // No registry presence at all (registry exhausted, never expected): the
    // project is, by definition, not identified.
    if (own == nullptr)
        return "unidentified";

    switch (own->projectKey.scope)
    {
        case ProjectKey::Scope::Identified:   return "identified";
        case ProjectKey::Scope::Unidentified: return "unidentified";
        case ProjectKey::Scope::Pending:      break;
    }

    return "pending";
}

const meterscope::SharedProjectConfig* MeterScopeAudioProcessorEditor::refreshConfigCache()
{
    auto* own = processorRef.getRegistryEntry();

    if (own == nullptr)
    {
        cachedConfig.reset();
        return nullptr;
    }

    const auto key = own->projectKey;
    auto& registry = meterscope::SharedMeterRegistry::getInstance();
    const auto version = registry.getProjectConfigVersion(key);

    if (! version.has_value())
    {
        if (cachedConfig.has_value())
            configChangedThisTick = true;

        cachedConfig.reset();
        cachedConfigVersion = -1;
        return nullptr;
    }

    if (! cachedConfig.has_value() || key != cachedConfigKey || *version != cachedConfigVersion)
    {
        cachedConfig = registry.getProjectConfigCopy(key);
        cachedConfigKey = key;
        cachedConfigVersion = *version;
        configChangedThisTick = true;
    }

    return cachedConfig.has_value() ? &*cachedConfig : nullptr;
}

juce::var MeterScopeAudioProcessorEditor::buildTrackSpec(const meterscope::SharedMeterRegistry::Entry& entry, int index,
                                                         const meterscope::SharedProjectConfig* cfg) const
{
    const auto id = meterscope::uiTrackIdFor(entry);
    const bool isOwn = (&entry == processorRef.getRegistryEntry());

    auto name = entry.trackName;
    if (name.isEmpty())
        name = isOwn ? "This Track" : "Track";

    // Track-scope state is read lock-free off the owning instance's own APVTS.
    const bool linked = entry.linkedRaw == nullptr || entry.linkedRaw->load(std::memory_order_relaxed) > 0.5f;
    const bool mono = entry.monoRaw != nullptr && entry.monoRaw->load(std::memory_order_relaxed) > 0.5f;

    // Group comes from the project's Shared View State (never per-instance);
    // an assignment to a group that no longer exists reads as "no group".
    juce::var group;
    if (cfg != nullptr)
    {
        auto it = cfg->trackGroup.find(id);
        if (it != cfg->trackGroup.end())
        {
            for (auto& g : cfg->groups)
                if (g.id == it->second)
                    group = g.id;
        }
    }

    return makeObjectVar([&](juce::DynamicObject& o)
    {
        o.setProperty("id", id);
        o.setProperty("name", name);
        o.setProperty("order", index);
        o.setProperty("group", group);
        o.setProperty("linked", linked);
        o.setProperty("mono", mono);
    });
}

juce::var MeterScopeAudioProcessorEditor::buildTrackList(const std::vector<meterscope::SharedMeterRegistry::Entry*>& entries,
                                                         const meterscope::SharedProjectConfig* cfg) const
{
    juce::Array<juce::var> list;
    int index = 0;

    for (auto* e : entries)
        list.add(buildTrackSpec(*e, index++, cfg));

    return list;
}

juce::var MeterScopeAudioProcessorEditor::buildViewStateVar(const meterscope::SharedProjectConfig* cfg) const
{
    const meterscope::SharedProjectConfig fresh; // fresh/missing config -> the all-visible default
    const auto& c = cfg != nullptr ? *cfg : fresh;

    juce::Array<juce::var> groups;
    std::set<juce::String> groupIds;
    for (auto& g : c.groups)
    {
        groupIds.insert(g.id);
        groups.add(makeObjectVar([&](juce::DynamicObject& o) { o.setProperty("id", g.id); o.setProperty("name", g.name); }));
    }

    juce::Array<juce::var> filter;
    for (auto& f : c.resolvedGroupFilter())
        filter.add(f);

    auto* hidden = new juce::DynamicObject();
    for (auto& t : c.hiddenTracks)
        hidden->setProperty(t, true);

    auto* trackGroup = new juce::DynamicObject();
    for (auto& kv : c.trackGroup)
        if (groupIds.count(kv.second) > 0)
            trackGroup->setProperty(kv.first, kv.second);

    return makeObjectVar([&](juce::DynamicObject& o)
    {
        o.setProperty("groups", groups);
        o.setProperty("groupFilter", filter);
        o.setProperty("soloTrack", c.soloTrack.isEmpty() ? juce::var() : juce::var(c.soloTrack));
        o.setProperty("hiddenTracks", juce::var(hidden));
        o.setProperty("trackGroup", juce::var(trackGroup));
    });
}

juce::var MeterScopeAudioProcessorEditor::buildConfigVar(const std::map<juce::String, float>* values) const
{
    // Same shape as index.html's defaultConfig()/defaultIndicators() (minus the
    // UI-only _last* memory fields). An id the store never recorded falls back
    // to the parameter's own APVTS default (SharedProjectConfig contract).
    auto val = [&](const juce::String& id) -> float
    {
        if (values != nullptr)
        {
            auto it = values->find(id);
            if (it != values->end())
                return it->second;
        }
        return processorRef.getParameterDefaultRaw(id);
    };

    auto index = [&](const juce::String& id, int count) { return juce::jlimit(0, count - 1, juce::roundToInt(val(id))); };

    auto* cfg = new juce::DynamicObject();
    cfg->setProperty("peakMode", kPeakModeNames[index("peakMode", 5)]);
    cfg->setProperty("lufsMMode", kLufsMModeNames[index("lufsMMode", 3)]);
    for (const char* id : { "showRmsM", "showRmsI", "showLufsS", "showLra", "showLufsI" })
        cfg->setProperty(id, val(id) > 0.5f);

    auto* ind = new juce::DynamicObject();
    for (const char* m : kMeasurements)
    {
        const juce::String prefix = juce::String("ind") + m;
        ind->setProperty(m, makeObjectVar([&](juce::DynamicObject& o)
        {
            o.setProperty("mode", kIndModeNames[index(prefix + "Mode", 3)]);
            o.setProperty("threshold", tidy(val(prefix + "Threshold")));
            o.setProperty("min", tidy(val(prefix + "RangeMin")));
            o.setProperty("max", tidy(val(prefix + "RangeMax")));
        }));
    }

    return makeObjectVar([&](juce::DynamicObject& o)
    {
        o.setProperty("config", juce::var(cfg));
        o.setProperty("indicators", juce::var(ind));
    });
}

//==============================================================================
// 30Hz timer: the ONLY periodic work of the editor (no other timer exists).
// Order: reconcile this instance's shared-config parameters, then poll the
// registry/config for changes and emit events only for real differences, then
// publish meter data for every track of this instance's own project partition.
//==============================================================================

void MeterScopeAudioProcessorEditor::timerCallback()
{
    // Keep this instance's Linked/Project-scope parameters in step with the
    // project's shared config (also runs from the processor's ~2Hz tick when no
    // editor is open). Cheap when nothing changed.
    processorRef.syncSharedConfig();

    if (webView == nullptr)
        return;

    // emitEventIfBrowserIsVisible() silently drops events while the browser is
    // not visible, so the "what was last sent" bookkeeping is only trusted
    // while visible; on becoming visible (or when armed by getProjectScope(),
    // the last call of the JS boot sequence) everything is re-sent once.
    const bool visible = webView->isVisible();
    const bool force = visible && (resendAll || ! wasVisible);

    if (force)
        resendAll = false;

    wasVisible = visible;

    const auto* cfg = refreshConfigCache();

    if (visible)
    {
        pollProjectScope(force);
        const bool trackAttrsChanged = pollTrackList(force, cfg);
        pollSharedConfig(force, cfg, trackAttrsChanged);
    }

    emitMeterUpdates(force);
    configChangedThisTick = false;
}

void MeterScopeAudioProcessorEditor::pollProjectScope(bool force)
{
    const auto scope = scopeString();

    if (! force && scope == lastScope)
        return;

    lastScope = scope;
    emitEvent("projectScopeUpdate", makeObjectVar([&](juce::DynamicObject& o) { o.setProperty("scope", scope); }));
}

bool MeterScopeAudioProcessorEditor::pollTrackList(bool force, const meterscope::SharedProjectConfig* cfg)
{
    const auto entries = getPartitionEntries();

    if (entries.empty())
        return false;

    const auto hostId = processorRef.getUiTrackId();

    // Structure = which tracks exist, in which order, under which names (and
    // which one is the host). Any change re-sends the whole list; attribute-only
    // changes (linked / mono / group) go out as per-track "trackConfigUpdate".
    juce::String signature = hostId;
    for (auto* e : entries)
        signature << "\n" << meterscope::uiTrackIdFor(*e) << "|" << e->trackName;

    juce::Array<juce::var> specs;
    int index = 0;
    for (auto* e : entries)
        specs.add(buildTrackSpec(*e, index++, cfg));

    bool attrsChanged = false;

    if (force || signature != lastTrackStructureSig)
    {
        lastTrackStructureSig = signature;
        lastTrackAttrs.clear();

        for (auto& spec : specs)
        {
            auto* o = spec.getDynamicObject();
            lastTrackAttrs[o->getProperty("id").toString()] =
                toJson(o->getProperty("linked")) + toJson(o->getProperty("mono")) + toJson(o->getProperty("group"));
        }

        attrsChanged = true;
        emitEvent("trackListUpdate", makeObjectVar([&](juce::DynamicObject& o)
        {
            o.setProperty("hostTrackId", hostId);
            o.setProperty("tracks", specs);
        }));
        return attrsChanged;
    }

    for (auto& spec : specs)
    {
        auto* o = spec.getDynamicObject();
        const auto id = o->getProperty("id").toString();
        const auto attrs = toJson(o->getProperty("linked")) + toJson(o->getProperty("mono")) + toJson(o->getProperty("group"));

        if (lastTrackAttrs[id] == attrs)
            continue;

        lastTrackAttrs[id] = attrs;
        attrsChanged = true;

        emitEvent("trackConfigUpdate", makeObjectVar([&](juce::DynamicObject& u)
        {
            u.setProperty("trackId", id);
            u.setProperty("linked", o->getProperty("linked"));
            u.setProperty("mono", o->getProperty("mono"));
            u.setProperty("group", o->getProperty("group"));
        }));
    }

    return attrsChanged;
}

void MeterScopeAudioProcessorEditor::pollSharedConfig(bool force, const meterscope::SharedProjectConfig* cfg,
                                                       bool trackAttrsChanged)
{
    if (! (force || configChangedThisTick || trackAttrsChanged))
        return;

    // Shared View State (groups, groupFilter, soloTrack, hiddenTracks,
    // trackGroup): changed by ANY editor of this project.
    const auto viewJson = toJson(buildViewStateVar(cfg));
    if (force || viewJson != lastViewStateJson)
    {
        lastViewStateJson = viewJson;
        emitEvent("sharedViewStateUpdate", buildViewStateVar(cfg));
    }

    // Shared GLOBAL configuration (used by every linked track).
    const auto globalVar = buildConfigVar(cfg != nullptr ? &cfg->globalLinkedConfig.values : nullptr);
    const auto globalJson = toJson(globalVar);
    if (force || globalJson != lastGlobalConfigJson)
    {
        lastGlobalConfigJson = globalJson;
        emitEvent("globalConfigUpdate", globalVar);
    }

    // Own configuration of every UNLINKED track of this partition (other than
    // this instance's own host track, which the page reads from its relays).
    if (cfg == nullptr)
        return;

    auto* own = processorRef.getRegistryEntry();
    std::set<juce::String> live;

    for (auto* e : getPartitionEntries())
    {
        const bool linked = e->linkedRaw == nullptr || e->linkedRaw->load(std::memory_order_relaxed) > 0.5f;
        const auto id = meterscope::uiTrackIdFor(*e);

        if (e == own || linked)
            continue;

        auto it = cfg->unlinkedTrackConfigs.find(id);
        if (it == cfg->unlinkedTrackConfigs.end())
            continue; // owner has not published yet; the page keeps its optimistic copy

        live.insert(id);
        auto configVar = buildConfigVar(&it->second.values);
        const auto json = toJson(configVar);

        if (! force && lastOwnConfigJson[id] == json)
            continue;

        lastOwnConfigJson[id] = json;

        auto* o = configVar.getDynamicObject();
        emitEvent("trackConfigUpdate", makeObjectVar([&](juce::DynamicObject& u)
        {
            u.setProperty("trackId", id);
            u.setProperty("config", o->getProperty("config"));
            u.setProperty("indicators", o->getProperty("indicators"));
        }));
    }

    for (auto it = lastOwnConfigJson.begin(); it != lastOwnConfigJson.end();)
        it = live.count(it->first) > 0 ? std::next(it) : lastOwnConfigJson.erase(it);
}

void MeterScopeAudioProcessorEditor::emitMeterUpdates(bool force)
{
    if (webView == nullptr)
        return;

    if (force)
        lastEmittedVersions.clear();

    // The page needs live data for its own host track (always), the track
    // being viewed through instance navigation, and every visible ALL TRACKS
    // box. It does not tell the native side which, so every track of THIS
    // instance's project partition is published; each one is skipped while its
    // snapshot is unchanged (cheap version check, so a stopped transport costs
    // nothing). Other projects' instances are never in the list.
    const auto entries = getPartitionEntries();

    if (entries.empty())
    {
        // Registry exhausted (never expected): THIS TRACK keeps working alone.
        const auto id = processorRef.getUiTrackId();
        const auto& snapshot = processorRef.getMeterSnapshot();
        const auto version = snapshot.version.load(std::memory_order_relaxed);
        auto it = lastEmittedVersions.find(id);
        if (it == lastEmittedVersions.end() || it->second != version)
        {
            lastEmittedVersions[id] = version;
            emitMeterUpdateFor(snapshot, id);
        }
        return;
    }

    std::set<juce::String> live;

    for (auto* e : entries)
    {
        if (e->snapshot == nullptr)
            continue;

        const auto id = meterscope::uiTrackIdFor(*e);
        live.insert(id);

        // Cheap staleness skip: `version` is bumped exactly once per
        // processBlock() (relaxed atomic, audio thread -- see
        // MeteringEngine::publishSnapshot()). If it hasn't moved since the last
        // emit, every field is unchanged too. This never freezes the visible
        // meters: the page's own 60fps rAF loop keeps easing toward the last
        // published values regardless of how often this emits.
        const auto version = e->snapshot->version.load(std::memory_order_relaxed);
        auto it = lastEmittedVersions.find(id);

        if (it != lastEmittedVersions.end() && it->second == version)
            continue;

        lastEmittedVersions[id] = version;
        emitMeterUpdateFor(*e->snapshot, id);
    }

    for (auto it = lastEmittedVersions.begin(); it != lastEmittedVersions.end();)
        it = live.count(it->first) > 0 ? std::next(it) : lastEmittedVersions.erase(it);
}

void MeterScopeAudioProcessorEditor::emitMeterUpdateFor(const meterscope::SharedMeterSnapshot& snapshot,
                                                        const juce::String& trackId)
{
    if (webView == nullptr)
        return;

    constexpr auto relaxed = std::memory_order_relaxed;

    // Values below are RAW (pre-mono-compensation, pre-display-smoothing) --
    // index.html's monoDb()/frameUpdate() apply the -3dB mono compensation
    // and the 150ms/75ms JSFX display dynamics client-side, unchanged since
    // v1 (architecture.md Components 8/9's finding: native never applies
    // either).
    auto* obj = new juce::DynamicObject();
    obj->setProperty("trackId", trackId);

    // Component 2's shared peak/TP register (architecture.md's "Peak/True-
    // Peak Payload Correction"): both the peakX and tpX field sets below are
    // ALWAYS populated from the SAME snapshot fields -- natively there is
    // only one register per channel, never reset on a peakMode switch; the
    // WebView only ever renders whichever column matches the current
    // peakMode, so it never notices the "other" field set is just a mirror.
    obj->setProperty("peakL", snapshot.peakL.load(relaxed));
    obj->setProperty("peakR", snapshot.peakR.load(relaxed));
    obj->setProperty("tpL", snapshot.tpL.load(relaxed));
    obj->setProperty("tpR", snapshot.tpR.load(relaxed));
    obj->setProperty("peakLHeld", snapshot.peakLHeld.load(relaxed));
    obj->setProperty("peakRHeld", snapshot.peakRHeld.load(relaxed));
    obj->setProperty("tpLHeld", snapshot.tpLHeld.load(relaxed));
    obj->setProperty("tpRHeld", snapshot.tpRHeld.load(relaxed));
    obj->setProperty("peakLClips", (int) snapshot.peakLClips.load(relaxed));
    obj->setProperty("peakRClips", (int) snapshot.peakRClips.load(relaxed));
    obj->setProperty("tpLClips", (int) snapshot.tpLClips.load(relaxed));
    obj->setProperty("tpRClips", (int) snapshot.tpRClips.load(relaxed));

    obj->setProperty("rmsM", snapshot.rmsM.load(relaxed));
    obj->setProperty("rmsMHeld", snapshot.rmsMHeld.load(relaxed));
    obj->setProperty("rmsI", snapshot.rmsI.load(relaxed));

    obj->setProperty("lufsM", snapshot.lufsM.load(relaxed));
    obj->setProperty("lufsMHeld", snapshot.lufsMHeld.load(relaxed));

    // 75-bin, 1dB-resolution LUFS-M display histogram (JSFX's db_hist[75];
    // architecture.md/SharedMeterSnapshot: bin i == db (i - 70), i.e. the
    // JSFX's own -70..+4dB domain -- loudness_meter_cockos.jsfx lines
    // 728-741). Normalised here to a [0,1] fraction of the histogram's own
    // peak bin, matching the JSFX's own `tw = cnt/db_hist_max*maxtw` display
    // scaling (line 732): SharedMeterSnapshot only publishes raw per-bin
    // counts (no db_hist_max field, by design -- Source/DSP is out of scope
    // for this phase), so the max is recomputed here from the same published
    // array every tick. This is O(75) message-thread work, not an
    // audio-thread or Source/DSP change. See this phase's plan.md status
    // note for the matching index.html renderHistogramBars() domain fix
    // (-70..+4, not the old index-relative -72..0 approximation).
    std::array<std::uint32_t, meterscope::SharedMeterSnapshot::kLufsMHistBins> rawBins {};
    std::uint32_t histMax = 0;
    for (int i = 0; i < meterscope::SharedMeterSnapshot::kLufsMHistBins; ++i)
    {
        rawBins[(size_t) i] = snapshot.lufsMHistBins[(size_t) i].load(relaxed);
        histMax = juce::jmax(histMax, rawBins[(size_t) i]);
    }
    juce::Array<juce::var> histArray;
    histArray.ensureStorageAllocated(meterscope::SharedMeterSnapshot::kLufsMHistBins);
    for (int i = 0; i < meterscope::SharedMeterSnapshot::kLufsMHistBins; ++i)
        histArray.add(histMax > 0 ? (double) rawBins[(size_t) i] / (double) histMax : 0.0);
    obj->setProperty("lufsMHist", histArray);

    obj->setProperty("lufsS", snapshot.lufsS.load(relaxed));
    obj->setProperty("lufsSHeld", snapshot.lufsSHeld.load(relaxed));
    obj->setProperty("lufsI", snapshot.lufsI.load(relaxed));
    obj->setProperty("lraLo", snapshot.lraLo.load(relaxed));
    obj->setProperty("lraHi", snapshot.lraHi.load(relaxed));

    webView->emitEventIfBrowserIsVisible("meterUpdate", juce::var(obj));
}

//==============================================================================
// Native function handlers
// See v7-PluginEditor.h for the full TODO(native) rationale on every one of
// these -- placeholder implementations, not a finalised API.
//==============================================================================

void MeterScopeAudioProcessorEditor::handleRequestEditorWidth(
    const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    // args[0] = candidate width (px), args[1] = candidate height (px) -- see
    // requestEditorWidthNative() in v7-ui.html. This is the ONE place that
    // actually resizes the editor: the WebView cannot resize its own window
    // from JS.
    if (args.size() < 2)
    {
        completion(juce::var());
        return;
    }

    auto requestedWidth = (int) args[0];
    auto requestedHeight = (int) args[1];

    // Real screen cap -- see v7-ui.yaml -> window_resize_on_view_switch and
    // v6-ui.yaml -> horizontal_only_resize.for_plan_note. TODO(plan): confirm
    // getDisplayForRect(getScreenBounds()) behaves correctly before the
    // editor has ever been shown/positioned by the host (may need a
    // fallback to the primary display in that case, as below).
    auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());
    auto userArea = display != nullptr
                         ? display->userBounds.toNearestInt()
                         : juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();

    // TODO(plan): confirm/tune these against a real host. Values mirror the
    // mockup's simulated reserve for host window chrome (title bar/borders)
    // not already excluded by userArea -- see v5-ui.yaml
    // window_resize_on_view_switch.screen_cap_definition.
    constexpr int chromeMarginWidth = 60;
    constexpr int chromeMarginHeight = 100;
    juce::ignoreUnused(chromeMarginHeight); // reserved for width only, per v6's horizontal-only rule -- see below

    auto cappedWidth = juce::jmin(requestedWidth, userArea.getWidth() - chromeMarginWidth);

    // Oversized-height edge case (v6): clamp DOWN only, against the RAW
    // usable height, never the smaller chrome-reserving margin used for
    // width -- see v6-ui.yaml -> horizontal_only_resize.edge_case_oversized_height.
    auto cappedHeight = juce::jmin(requestedHeight, userArea.getHeight());

    constexpr int minWidth = 396, minHeight = 462; // matches setResizeLimits (UI zoom 1.1)
    cappedWidth = juce::jmax(minWidth, cappedWidth);
    cappedHeight = juce::jmax(minHeight, cappedHeight);

    setSize(cappedWidth, cappedHeight);
    completion(juce::var());
}

void MeterScopeAudioProcessorEditor::handleGetHostTrackId(
    const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    // Phase 3.3: the REAL track identity from the Phase 2.2 registry (REAPER
    // track/take GUID, "master" for Master, or the minted fallback UUID). While
    // this instance is still Pending (the resolver needs up to ~2s after
    // construction) it is a placeholder derived from the instance; the page is
    // told about the switch to the real id by a "trackListUpdate" event.
    completion(processorRef.getUiTrackId());
}

void MeterScopeAudioProcessorEditor::handleGetTrackList(
    const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    // Phase 3.3: one {id, name, order, group, linked, mono} object per
    // instance of THIS instance's own project partition, in REAPER track order
    // (discovery order for Unidentified). Other projects are never included.
    // A Pending caller gets only itself.
    const auto entries = getPartitionEntries();
    const auto* cfg = refreshConfigCache();

    if (entries.empty())
    {
        // Registry exhausted (never expected): a single stand-in for this track.
        juce::Array<juce::var> list;
        list.add(makeObjectVar([&](juce::DynamicObject& o)
        {
            o.setProperty("id", processorRef.getUiTrackId());
            o.setProperty("name", "This Track");
            o.setProperty("order", 0);
            o.setProperty("group", juce::var());
            o.setProperty("linked", true);
            o.setProperty("mono", false);
        }));
        completion(list);
        return;
    }

    completion(buildTrackList(entries, cfg));
}

void MeterScopeAudioProcessorEditor::handleGetSharedViewState(
    const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    // Phase 3.3: the project's Shared View State (projectConfigs[ownKey]),
    // never per-instance. A fresh/missing config -- and any config whose
    // groupFilter the user never set -- yields the all-visible default
    // ("no-group" plus every existing group id), NOT an empty filter; an empty
    // filter is only ever returned when the user explicitly turned every chip
    // off (SharedProjectConfig::resolvedGroupFilter()).
    completion(buildViewStateVar(refreshConfigCache()));
}

void MeterScopeAudioProcessorEditor::handleSetSharedViewState(
    const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    auto* own = processorRef.getRegistryEntry();

    // A Pending instance has no project partition to write to yet (the
    // resolver places it within ~2s); the page keeps its optimistic state and
    // re-syncs from the first sharedViewStateUpdate after resolution.
    if (own == nullptr || own->projectKey.scope == ProjectKey::Scope::Pending
        || args.isEmpty() || args[0].getDynamicObject() == nullptr)
    {
        juce::Logger::writeToLog("MeterScope: setSharedViewState ignored (project not resolved yet)");
        completion(juce::var());
        return;
    }

    auto* obj = args[0].getDynamicObject();
    meterscope::SharedViewStateUpdate update;

    if (auto* groups = obj->getProperty("groups").getArray())
        for (auto& g : *groups)
            update.groups.push_back({ g.getProperty("id", {}).toString(), g.getProperty("name", {}).toString() });

    if (auto* filter = obj->getProperty("groupFilter").getArray())
        for (auto& f : *filter)
            update.groupFilter.insert(f.toString());

    update.soloTrack = obj->getProperty("soloTrack").toString(); // null -> ""

    if (auto* hidden = obj->getProperty("hiddenTracks").getDynamicObject())
        for (auto& nv : hidden->getProperties())
            if ((bool) nv.value)
                update.hiddenTracks.insert(nv.name.toString());

    if (auto* trackGroup = obj->getProperty("trackGroup").getDynamicObject())
        for (auto& nv : trackGroup->getProperties())
            update.trackGroup[nv.name.toString()] = nv.value.toString(); // null -> "" (= no group)

    if (meterscope::SharedMeterRegistry::getInstance().setSharedViewState(own->projectKey, update))
    {
        processorRef.flushSharedConfig();

        // The page already holds this state (optimistic update), so do not
        // echo it back to the editor that made the change: record it as sent.
        lastViewStateJson = toJson(buildViewStateVar(refreshConfigCache()));
    }

    completion(juce::var());
}

namespace
{
    // Finds the Entry for `trackId` inside the caller's OWN project partition
    // (every lookup is keyed by (projectKey, trackId); never across projects).
    meterscope::SharedMeterRegistry::Entry* findOwnPartitionEntry(MeterScopeAudioProcessor& processor,
                                                                   const juce::String& trackId)
    {
        auto* own = processor.getRegistryEntry();

        if (own == nullptr)
            return nullptr;

        return meterscope::SharedMeterRegistry::getInstance().findEntry(own->projectKey, trackId);
    }
}

void MeterScopeAudioProcessorEditor::handleSetTrackLinked(
    const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    // args[0] = trackId, args[1] = linked (bool). Lookup by (own projectKey,
    // trackId); this instance's own entry is written locally, any other
    // instance through its registered cross-instance callback (which writes
    // that instance's OWN APVTS -- no processor ever writes another's).
    // Unknown trackId (instance closed since the list was fetched): harmless
    // logged no-op; the page re-syncs from the next trackListUpdate.
    if (args.size() >= 2)
    {
        const auto trackId = args[0].toString();
        const bool linked = (bool) args[1];

        if (auto* entry = findOwnPartitionEntry(processorRef, trackId))
        {
            if (entry == processorRef.getRegistryEntry())
            {
                if (auto* p = processorRef.parameters.getParameter("linked"))
                    p->setValueNotifyingHost(linked ? 1.0f : 0.0f);
            }
            else if (entry->remoteSetLinked)
            {
                entry->remoteSetLinked(linked);
            }
        }
        else
        {
            juce::Logger::writeToLog("MeterScope: setTrackLinked: no such track in this project: " + trackId);
        }
    }

    completion(juce::var());
}

void MeterScopeAudioProcessorEditor::handleSetTrackMono(
    const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    // See handleSetTrackLinked above. args[1] = mono (bool).
    if (args.size() >= 2)
    {
        const auto trackId = args[0].toString();
        const bool mono = (bool) args[1];

        if (auto* entry = findOwnPartitionEntry(processorRef, trackId))
        {
            if (entry == processorRef.getRegistryEntry())
            {
                if (auto* p = processorRef.parameters.getParameter("monoMode"))
                    p->setValueNotifyingHost(mono ? 1.0f : 0.0f);
            }
            else if (entry->remoteSetMono)
            {
                entry->remoteSetMono(mono);
            }
        }
        else
        {
            juce::Logger::writeToLog("MeterScope: setTrackMono: no such track in this project: " + trackId);
        }
    }

    completion(juce::var());
}

void MeterScopeAudioProcessorEditor::handleResetTrackMeterMax(
    const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    // args[0] = trackId, args[1] = measurement key. PARTIAL held-max reset:
    // only that measurement's held max (plus clip counters for Peak/TruePeak,
    // which share one register), both channels -- see
    // MeterScopeAudioProcessor::requestHeldMaxReset() (unknown keys are a
    // no-op). DELIBERATE DIFFERENCE FROM THE JSFX, whose any-click Reset() is
    // full. Phase 3.3: this instance's own track resets locally; any other
    // track of the same project goes through its registry remoteResetOne
    // callback, which sets the same bit in THAT instance's own lock-free
    // pending-reset mask (consumed at the top of its next processBlock).
    if (args.size() >= 2)
    {
        const auto trackId = args[0].toString();
        const auto key = args[1].toString();

        if (auto* entry = findOwnPartitionEntry(processorRef, trackId))
        {
            if (entry == processorRef.getRegistryEntry())
                processorRef.requestHeldMaxReset(key);
            else if (entry->remoteResetOne)
                entry->remoteResetOne(key);
        }
    }

    completion(juce::var());
}

void MeterScopeAudioProcessorEditor::handleResetTrackAll(
    const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    // args[0] = trackId. "reset" action (parameter-spec.md "Actions"): clears
    // peaks, held maxima, clips, windows, histograms, integrated and LRA via
    // the same full ResetController reset. Local for this instance's own
    // track, remoteResetAll for another instance of the same project.
    if (args.size() >= 1)
    {
        if (auto* entry = findOwnPartitionEntry(processorRef, args[0].toString()))
        {
            if (entry == processorRef.getRegistryEntry())
                processorRef.requestReset();
            else if (entry->remoteResetAll)
                entry->remoteResetAll();
        }
    }

    completion(juce::var());
}

void MeterScopeAudioProcessorEditor::handleGetProjectScope(
    const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    // Phase 3.3 (new): the ALL TRACKS banner is shown iff scope == "unidentified".
    // This is the last native call of the page's boot sequence (listeners and
    // the track list are in place by then), so it also arms a one-shot
    // re-send of every event on the next timer tick.
    resendAll = true;

    completion(makeObjectVar([&](juce::DynamicObject& o) { o.setProperty("scope", scopeString()); }));
}

void MeterScopeAudioProcessorEditor::handleSetConfigParam(
    const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    // args[0] = targetId ("global" | an unlinked track's id), args[1] = Linked-
    // scope parameter id, args[2] = raw (denormalised) value.
    auto* own = processorRef.getRegistryEntry();

    if (args.size() >= 3 && own != nullptr && own->projectKey.scope != ProjectKey::Scope::Pending)
    {
        const auto targetId = args[0].toString();
        const auto paramId = args[1].toString();
        const auto rawValue = (float) (double) args[2];

        if (! meterscope::SharedConfigSync::isLinkedParamId(paramId))
        {
            juce::Logger::writeToLog("MeterScope: setConfigParam: not a Linked-scope parameter: " + paramId);
        }
        else if (targetId == "global")
        {
            // Any instance may write the shared global configuration at any
            // time, whatever its own linked state (architecture.md: this is the
            // "known gap" resolution). Every LINKED instance -- this one
            // included, if linked -- adopts the new value on its next reconcile.
            meterscope::SharedMeterRegistry::getInstance().setGlobalLinkedParam(own->projectKey, paramId, rawValue);
            processorRef.flushSharedConfig();

            // The page already holds this value (optimistic update): record the
            // new global config as sent so it is not echoed back to this editor.
            if (const auto* cfg = refreshConfigCache())
                lastGlobalConfigJson = toJson(buildConfigVar(&cfg->globalLinkedConfig.values));
        }
        else if (auto* entry = findOwnPartitionEntry(processorRef, targetId))
        {
            // Another instance's OWN configuration only makes sense while that
            // track is unlinked (a linked track edits the global one).
            const bool targetLinked = entry->linkedRaw == nullptr || entry->linkedRaw->load(std::memory_order_relaxed) > 0.5f;

            if (targetLinked)
                juce::Logger::writeToLog("MeterScope: setConfigParam: target track is linked, ignored: " + targetId);
            else if (entry == own)
                processorRef.setOwnLinkedParam(paramId, rawValue);
            else if (entry->remoteSetParam)
                entry->remoteSetParam(paramId, rawValue);
        }
    }

    completion(juce::var());
}

void MeterScopeAudioProcessorEditor::handleGetEditorStateSize(
    const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    // Phase 3.2: savedEditorWidth/Height are seeded from the processor's
    // real persisted <EditorState> size in the constructor (Phase 2.2 Part
    // C, via getEditorStateWidth()/getEditorStateHeight()) and kept current
    // by handleSetEditorStateSize() below, so this simply reflects that
    // already-persisted value back to the WebView JS layer (used there only
    // to seed savedSingleTrackSize for the ALL-TRACKS-view shrink-back
    // target -- see index.html's boot()).
    auto* obj = new juce::DynamicObject();
    obj->setProperty("width", savedEditorWidth);
    obj->setProperty("height", savedEditorHeight);
    completion(juce::var(obj));
}

void MeterScopeAudioProcessorEditor::handleSetEditorStateSize(
    const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    if (args.size() >= 2)
    {
        savedEditorWidth = (int) args[0];
        savedEditorHeight = (int) args[1];

        // Phase 3.2: persist through to the processor's real <EditorState>
        // XML (Phase 2.2 Part C's getStateInformation()/setStateInformation()
        // sibling) so the size survives an editor close/reopen and a
        // REAPER project save/reload, not just the current editor-open
        // session. Message-thread only (native-function handlers dispatch
        // on the message thread) -- no audio-thread involvement.
        processorRef.setEditorStateSize(savedEditorWidth, savedEditorHeight);
    }
    completion(juce::var());
}

//==============================================================================
// Resource Provider (JUCE 8 Required Pattern — explicit URL mapping)
//==============================================================================

std::optional<juce::WebBrowserComponent::Resource> MeterScopeAudioProcessorEditor::getResource(
    const juce::String& url)
{
    auto makeVector = [](const char* data, int size) {
        return std::vector<std::byte>(
            reinterpret_cast<const std::byte*>(data),
            reinterpret_cast<const std::byte*>(data) + size);
    };

    if (url == "/" || url == "/index.html")
    {
        return juce::WebBrowserComponent::Resource {
            makeVector(BinaryData::index_html, BinaryData::index_htmlSize),
            juce::String("text/html")
        };
    }

    // The page logic lives in its own file: WebView2 cuts resource responses
    // off at roughly 144 KB, so index.html must stay well below that.
    if (url == "/js/app.js")
    {
        return juce::WebBrowserComponent::Resource {
            makeVector(BinaryData::app_js, BinaryData::app_jsSize),
            juce::String("text/javascript")
        };
    }

    if (url == "/js/juce/index.js")
    {
        return juce::WebBrowserComponent::Resource {
            makeVector(BinaryData::index_js, BinaryData::index_jsSize),
            juce::String("text/javascript")
        };
    }

    // REQUIRED — see troubleshooting/patterns/juce8-critical-patterns.md #13
    if (url == "/js/juce/check_native_interop.js")
    {
        return juce::WebBrowserComponent::Resource {
            makeVector(BinaryData::check_native_interop_js, BinaryData::check_native_interop_jsSize),
            juce::String("text/javascript")
        };
    }

    juce::Logger::writeToLog("MeterScope WebView: resource not found: " + url);
    return std::nullopt;
}
