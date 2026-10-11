// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

// MeterScope WebView page logic. Kept out of index.html because JUCE's
// WebView2 resource responses get cut off at roughly 144 KB, which left the
// page black once the inline script pushed index.html past that size.
"use strict";

import * as Juce from "./juce/index.js";

/* ======================================================================
   Disable web behaviors (REQUIRED — ui-design-rules.md Rule 3)
   ====================================================================== */
document.addEventListener("contextmenu", function (e) { e.preventDefault(); return false; });
document.addEventListener("dragstart", function (e) { e.preventDefault(); });

if (!window.__JUCE__) {
    console.error("JUCE backend not available");
}

/* ======================================================================
   HELPERS (unchanged from the v1-v7 mockup)
   ====================================================================== */
function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
function round1(v) { return Math.round(v * 10) / 10; }

function fmtDb(v) {
    if (v <= -72) return "—";
    var s = v >= 0 ? "+" : "";
    return s + v.toFixed(1);
}
function fmtLra(v) { return v.toFixed(1); }
function fmtClip(n) { return n > 999 ? ">999" : String(n); }

function heightFrac(db, yScale) {
    var d = clamp(db, -72, 0);
    return Math.pow(d / 72 + 1, yScale);
}
function dbFromFrac(frac, yScale) {
    frac = clamp(frac, 0, 1);
    return round1((Math.pow(frac, 1 / yScale) - 1) * 72);
}

function logParam(id, value) { console.log("[param] " + id + " = " + value); }

function el(tag, cls) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    return e;
}

// v8: thin-line SVG icons live in the <symbol> sprite at the top of index.html.
function icon(name, cls) {
    var s = el("span", "ico-wrap");
    s.innerHTML = '<svg class="ico' + (cls ? " " + cls : "") + '"><use href="#i-' + name + '"/></svg>';
    return s;
}

/* ======================================================================
   JUCE PARAMETER BRIDGE
   ======================================================================
   The 40 settings in parameter-spec.md are exposed as APVTS parameters on
   THIS plugin instance (automation disabled -- see parameter-spec.md
   "General Rules"). Per parameter-spec.md's Notes ("a redundant copy in
   every instance's state, REAPER project extended state, or a hybrid"),
   these 40 relay-backed parameters are ASSUMED to represent, at all times,
   "this instance's own currently-effective configuration for its own HOST
   track" -- i.e. exactly what v1-v7's effConfigFor(hostTrack) /
   effIndicatorsFor(hostTrack) returned: the global (linked) values when the
   host track is linked, or the host track's own copy when unlinked.

   Phase 3.3: keeping every linked instance's copy of the shared global
   configuration consistent (and publishing an instance's own copy when it
   unlinks) is done NATIVELY (Source/Host/SharedConfigSync), against the
   project-partitioned shared store. This WebView layer only ever reads/writes
   ITS OWN 40 parameters through the relays, plus:
     - editing the Global panel while THIS instance's host track is UNLINKED,
       and editing an UNLINKED non-host track's own settings, go through
       setConfigParam(targetId, paramId, rawValue) (see makeRemoteConfig()).

   ALL OTHER tracks' data (name, order, linked, mono, group, own config, live
   meter values) and every truly shared, NON-parameter value (groups,
   groupFilter, soloTrack, hiddenTracks, trackGroup) are project-wide state
   that cannot live in this instance's own APVTS. They are read via native
   function calls at boot and kept live by native events: meterUpdate (one per
   track of THIS instance's own project), trackListUpdate, trackConfigUpdate,
   globalConfigUpdate, sharedViewStateUpdate, projectScopeUpdate. User edits go
   back through setSharedViewState / setTrackLinked / setTrackMono /
   resetTrackMeterMax / resetTrackAll / setConfigParam. Everything is scoped to
   the viewing instance's own project; other projects never appear.
   ====================================================================== */

function callNative(name, args) {
    try {
        var fn = Juce.getNativeFunction(name);
        return fn.apply(null, args || []);
    } catch (e) {
        console.warn("[JUCE bridge] native function \"" + name + "\" not available yet", e);
        return Promise.resolve(null);
    }
}

/* Keyboard focus hand-back. After any click WebView2 keeps the keyboard focus,
   and its keys never reach the host, so Space/Enter stop driving REAPER's
   transport. Outside text fields, focus goes back to the native editor, which
   forwards every key it doesn't use to the host (like any native plugin). Text
   fields keep focus while editing (Enter confirms, Escape cancels) and hand it
   back when they lose it. */
var TEXT_INPUT_TYPES = ["text", "number", "search", "email", "url", "tel", "password"];

function isTextEntry(node) {
    if (!node || !node.tagName) return false;
    if (node.isContentEditable || node.tagName === "TEXTAREA") return true;
    return node.tagName === "INPUT" && TEXT_INPUT_TYPES.indexOf((node.type || "text").toLowerCase()) !== -1;
}

// Deferred so a click that opens and focuses a text field keeps that focus.
function returnFocusToHostSoon() {
    setTimeout(function () {
        if (!isTextEntry(document.activeElement)) callNative("returnKeyboardFocusToHost", []);
    }, 0);
}

document.addEventListener("pointerup", returnFocusToHostSoon, true);
document.addEventListener("focusout", function (e) { if (isTextEntry(e.target)) returnFocusToHostSoon(); }, true);

// Fallback when the page still has focus (e.g. right after the editor opens):
// pass the key itself on to the host instead of swallowing it.
["keydown", "keyup"].forEach(function (type) {
    document.addEventListener(type, function (e) {
        if (e.defaultPrevented || isTextEntry(e.target)) return;
        e.preventDefault();
        callNative("forwardKeyToHost", [e.keyCode, type === "keydown"]);
    });
});

// requestEditorWidth: the ONE native call used by every automatic window
// resize (view switch, SHOW ALL re-fit, view-switch shrink). The plugin
// cannot resize its own window from JS -- see v7-PluginEditor.cpp, which
// performs the real juce::Component::setSize(), clamped against the actual
// screen's userArea and the active resize limits.
function requestEditorWidthNative(width, height) {
    callNative("requestEditorWidth", [Math.round(width), Math.round(height)]);
}

// ---- Choice value tables (order MUST match parameter-spec.md exactly) ----
var PEAK_MODE_VALUES = ["off", "tp", "tpclips", "peak", "peakclips"];
var LUFS_M_MODE_VALUES = ["off", "on", "hist"];
var IND_MODE_VALUES = ["off", "threshold", "range"];

// ComboBoxState (real JUCE 8 frontend API -- verified against
// C:/JUCE/modules/juce_gui_extra/native/typescript/webview-interop/dist/index.js)
// exposes getChoiceIndex()/setChoiceIndex(index), NOT a normalised value --
// unlike SliderState/ToggleState. values[] order MUST match the
// AudioParameterChoice's choice order defined in Stage 1 (Foundation),
// which MUST match parameter-spec.md exactly.
function comboBinding(paramId, values) {
    var cb = Juce.getComboBoxState(paramId);
    return {
        get: function () { return values[clamp(cb.getChoiceIndex(), 0, values.length - 1)]; },
        set: function (v) {
            var idx = values.indexOf(v);
            if (idx < 0) idx = 0;
            cb.setChoiceIndex(idx);
        },
        onChange: function (fn) { cb.valueChangedEvent.addListener(fn); }
    };
}
function toggleBinding(paramId) {
    var tg = Juce.getToggleState(paramId);
    return {
        get: function () { return !!tg.getValue(); },
        set: function (v) { tg.setValue(!!v); },
        onChange: function (fn) { tg.valueChangedEvent.addListener(fn); }
    };
}
// SliderState.getScaledValue() returns the REAL (un-normalised) value in the
// same units as the backend AudioParameterFloat/Int's NormalisableRange
// (parameter-spec.md's stated dB / yScale / textSize ranges), so no local
// lo/hi bookkeeping is needed for reads. There is no setScaledValue(): to
// write, we normalise using the SAME start/end/skew the backend reported
// via sl.properties (populated asynchronously on construction, via a
// propertiesChanged round-trip) -- this exactly inverts getNormalisedValue()
// regardless of skew. KNOWN LIMITATION: sl.properties still holds its
// {start:0,end:1,skew:1} default until that first round-trip completes, so
// a set() called before then would be mis-scaled -- in practice this only
// risks the very first synchronous reads/writes during proxy construction
// below, before boot() ever calls back into the page.
function sliderBinding(paramId) {
    var sl = Juce.getSliderState(paramId);
    return {
        get: function () { return sl.getScaledValue(); },
        set: function (v) {
            var p = sl.properties;
            var norm = Math.pow((v - p.start) / (p.end - p.start), p.skew || 1);
            sl.setNormalisedValue(clamp(norm, 0, 1));
        },
        onChange: function (fn) { sl.valueChangedEvent.addListener(fn); }
    };
}

var linkedBinding = toggleBinding("linked");
var monoModeBinding = comboBinding("monoMode", [false, true]); // Stereo, Mono
var resetOnPlayBinding = toggleBinding("resetOnPlay");
var yScaleBinding = sliderBinding("yScale");
var textSizeBinding = sliderBinding("textSize"); // Int -- rounded on read

var peakModeBinding = comboBinding("peakMode", PEAK_MODE_VALUES);
var lufsMModeBinding = comboBinding("lufsMMode", LUFS_M_MODE_VALUES);
var showRmsMBinding = toggleBinding("showRmsM");
var showRmsIBinding = toggleBinding("showRmsI");
var showLufsSBinding = toggleBinding("showLufsS");
var showLraBinding = toggleBinding("showLra");
var showLufsIBinding = toggleBinding("showLufsI");

function indicatorFieldBindings(m) {
    return {
        mode: comboBinding("ind" + m + "Mode", IND_MODE_VALUES),
        threshold: sliderBinding("ind" + m + "Threshold"),
        min: sliderBinding("ind" + m + "RangeMin"),
        max: sliderBinding("ind" + m + "RangeMax")
    };
}

/* Proxy objects below let EVERY existing render/gear-panel call site in
   this file (cfg.mode = v, cfg[prop] = v, etc. -- unchanged since v1-v7)
   transparently read/write the real relay-backed parameter, with zero
   changes needed at those call sites. */
function makeIndicatorProxy(m) {
    var b = indicatorFieldBindings(m);
    var target = { mode: b.mode.get(), threshold: b.threshold.get(), min: b.min.get(), max: b.max.get() };
    var proxy = new Proxy(target, {
        set: function (obj, key, value) {
            obj[key] = value;
            if (b[key]) b[key].set(value);
            return true;
        }
    });
    // While a drag handle is held, the backend's echo of each move must not
    // rebuild the view: that would detach the handle being dragged. The
    // drag's own pointerup (onCommit) re-renders once the gesture ends.
    b.mode.onChange(function () { target.mode = b.mode.get(); refreshAllAndGear(); });
    b.threshold.onChange(function () { target.threshold = b.threshold.get(); if (!isDragging) refreshAll(); });
    b.min.onChange(function () { target.min = b.min.get(); if (!isDragging) refreshAll(); });
    b.max.onChange(function () { target.max = b.max.get(); if (!isDragging) refreshAll(); });
    return proxy;
}

/* ---- Phase 3.3: configs that are NOT backed by this instance's own relays ----
   The global configuration (while THIS instance's host track is unlinked) and
   an UNLINKED non-host track's own configuration live in the project's shared
   store / in the other instance's APVTS. They are READ from native events
   (globalConfigUpdate / trackConfigUpdate) and WRITTEN through the single
   native function setConfigParam(targetId, paramId, rawValue). The objects
   below keep the exact shape the render/gear code has always used
   (cfg.peakMode = "tp", ind.Peak.threshold = -12, ...), so no render call site
   changed: a Proxy turns every property write into a native write. */
// Declared here (not with the gear panel below): makeRemoteConfig() runs at module load.
var MEASUREMENTS = ["TruePeak", "Peak", "RmsM", "RmsI", "LufsM", "LufsS", "LufsI"];
var CONFIG_SHOW_KEYS = ["showRmsM", "showRmsI", "showLufsS", "showLra", "showLufsI"];
var IND_FIELD_SUFFIX = { mode: "Mode", threshold: "Threshold", min: "RangeMin", max: "RangeMax" };

function normalizeConfig(src, prev) {
    var out = {
        peakMode: src.peakMode, lufsMMode: src.lufsMMode,
        showRmsM: !!src.showRmsM, showRmsI: !!src.showRmsI, showLufsS: !!src.showLufsS,
        showLra: !!src.showLra, showLufsI: !!src.showLufsI
    };
    out._lastPeakMode = out.peakMode !== "off" ? out.peakMode : ((prev && prev._lastPeakMode) || "peakclips");
    out._lastLufsMMode = out.lufsMMode !== "off" ? out.lufsMMode : ((prev && prev._lastLufsMMode) || "hist");
    return out;
}
function encodeConfigValue(key, value) {
    if (key === "peakMode") return Math.max(0, PEAK_MODE_VALUES.indexOf(value));
    if (key === "lufsMMode") return Math.max(0, LUFS_M_MODE_VALUES.indexOf(value));
    return value ? 1 : 0;
}
function makeRemoteConfig(targetId, initialConfig, initialIndicators) {
    var cfgTarget = normalizeConfig(initialConfig, null);
    var indTarget = {};
    MEASUREMENTS.forEach(function (m) { indTarget[m] = Object.assign({}, initialIndicators[m]); });

    var cfgProxy = new Proxy(cfgTarget, {
        set: function (obj, key, value) {
            obj[key] = value;
            if (key === "peakMode" || key === "lufsMMode" || CONFIG_SHOW_KEYS.indexOf(key) >= 0) {
                callNative("setConfigParam", [targetId, key, encodeConfigValue(key, value)]);
            }
            return true; // _last* memory stays UI-local
        }
    });
    var indProxy = {};
    MEASUREMENTS.forEach(function (m) {
        indProxy[m] = new Proxy(indTarget[m], {
            set: function (obj, key, value) {
                obj[key] = value;
                if (IND_FIELD_SUFFIX[key]) {
                    var raw = key === "mode" ? Math.max(0, IND_MODE_VALUES.indexOf(value)) : value;
                    callNative("setConfigParam", [targetId, "ind" + m + IND_FIELD_SUFFIX[key], raw]);
                }
                return true;
            }
        });
    });

    return {
        config: cfgProxy,
        indicators: indProxy,
        // Applies a native update WITHOUT echoing it back as a write.
        apply: function (cfg, ind) {
            if (cfg) {
                var n = normalizeConfig(cfg, cfgTarget);
                Object.keys(n).forEach(function (k) { cfgTarget[k] = n[k]; });
            }
            if (ind) {
                MEASUREMENTS.forEach(function (m) { if (ind[m]) Object.assign(indTarget[m], ind[m]); });
            }
        }
    };
}

var hostIndicators = {
    TruePeak: makeIndicatorProxy("TruePeak"),
    Peak: makeIndicatorProxy("Peak"),
    RmsM: makeIndicatorProxy("RmsM"),
    RmsI: makeIndicatorProxy("RmsI"),
    LufsM: makeIndicatorProxy("LufsM"),
    LufsS: makeIndicatorProxy("LufsS"),
    LufsI: makeIndicatorProxy("LufsI")
};

var hostConfig = new Proxy({
    peakMode: peakModeBinding.get(),
    lufsMMode: lufsMModeBinding.get(),
    showRmsM: showRmsMBinding.get(),
    showRmsI: showRmsIBinding.get(),
    showLufsS: showLufsSBinding.get(),
    showLra: showLraBinding.get(),
    showLufsI: showLufsIBinding.get(),
    // Local UI-only memory (which non-"off" mode to restore when
    // re-enabled) -- never sent to C++, not a parameter.
    _lastPeakMode: peakModeBinding.get() !== "off" ? peakModeBinding.get() : "peakclips",
    _lastLufsMMode: lufsMModeBinding.get() !== "off" ? lufsMModeBinding.get() : "hist"
}, {
    set: function (obj, key, value) {
        obj[key] = value;
        if (key === "peakMode") peakModeBinding.set(value);
        else if (key === "lufsMMode") lufsMModeBinding.set(value);
        else if (key === "showRmsM") showRmsMBinding.set(value);
        else if (key === "showRmsI") showRmsIBinding.set(value);
        else if (key === "showLufsS") showLufsSBinding.set(value);
        else if (key === "showLra") showLraBinding.set(value);
        else if (key === "showLufsI") showLufsIBinding.set(value);
        return true;
    }
});
peakModeBinding.onChange(function () { hostConfig.peakMode = peakModeBinding.get(); refreshAllAndGear(); });
lufsMModeBinding.onChange(function () { hostConfig.lufsMMode = lufsMModeBinding.get(); refreshAllAndGear(); });
showRmsMBinding.onChange(function () { hostConfig.showRmsM = showRmsMBinding.get(); refreshAllAndGear(); });
showRmsIBinding.onChange(function () { hostConfig.showRmsI = showRmsIBinding.get(); refreshAllAndGear(); });
showLufsSBinding.onChange(function () { hostConfig.showLufsS = showLufsSBinding.get(); refreshAllAndGear(); });
showLraBinding.onChange(function () { hostConfig.showLra = showLraBinding.get(); refreshAllAndGear(); });
showLufsIBinding.onChange(function () { hostConfig.showLufsI = showLufsIBinding.get(); refreshAllAndGear(); });

function defaultIndicators() {
    return {
        TruePeak: { mode: "off", threshold: -1.0, min: -3.0, max: -1.0 },
        Peak:     { mode: "off", threshold: 0.0,  min: -6.0, max: 0.0 },
        RmsM:     { mode: "off", threshold: -12.0, min: -20.0, max: -12.0 },
        RmsI:     { mode: "off", threshold: -14.0, min: -20.0, max: -14.0 },
        LufsM:    { mode: "off", threshold: -10.0, min: -18.0, max: -10.0 },
        LufsS:    { mode: "off", threshold: -12.0, min: -18.0, max: -12.0 },
        LufsI:    { mode: "off", threshold: -14.0, min: -24.0, max: -22.0 }
    };
}
function defaultConfig(peakMode, lufsMMode) {
    return {
        peakMode: peakMode, lufsMMode: lufsMMode,
        showRmsM: false, showRmsI: false, showLufsS: true, showLra: true, showLufsI: true,
        _lastPeakMode: peakMode !== "off" ? peakMode : "peakclips",
        _lastLufsMMode: lufsMMode !== "off" ? lufsMMode : "hist"
    };
}

// nativeGlobalMirror: the shared GLOBAL (linked tracks) configuration as the
// project's shared store holds it, kept live by "globalConfigUpdate". When THIS
// instance's own host track is linked, hostConfig/hostIndicators already ARE
// that same global data (via this instance's own relays, which the native side
// keeps in sync), so the mirror is what renders OTHER tracks and what the
// Global panel edits while this instance's own host is UNLINKED (writes go
// through setConfigParam("global", ...) -- the resolution of the old
// "unlinked-instance known gap").
var nativeGlobalMirror = makeRemoteConfig("global", defaultConfig("peakclips", "hist"), defaultIndicators());

// The global configuration as THIS instance should show/edit it right now.
function currentGlobalConfig() { return linkedBinding.get() ? hostConfig : nativeGlobalMirror.config; }
function currentGlobalIndicators() { return linkedBinding.get() ? hostIndicators : nativeGlobalMirror.indicators; }

var PROJECT = new Proxy({
    resetOnPlay: resetOnPlayBinding.get(),
    yScale: yScaleBinding.get(),
    textSize: Math.round(textSizeBinding.get()),
    // ---- Shared View State (NOT parameters, see parameter-spec.md) ----
    // Initial values fetched in boot() via getSharedViewState, kept live via
    // onSharedViewStateUpdate() -- see the header comment.
    groups: [],
    groupFilter: new Set(["no-group"]),
    soloTrack: null,
    hiddenTracks: {}
}, {
    set: function (obj, key, value) {
        obj[key] = value;
        if (key === "resetOnPlay") resetOnPlayBinding.set(value);
        else if (key === "yScale") yScaleBinding.set(value);
        else if (key === "textSize") textSizeBinding.set(value);
        // groups / groupFilter / soloTrack / hiddenTracks are not
        // parameters -- callers push them via pushSharedViewState() below
        // at each mutation site instead of through this trap.
        return true;
    }
});
resetOnPlayBinding.onChange(function () { PROJECT.resetOnPlay = resetOnPlayBinding.get(); });
yScaleBinding.onChange(function () { PROJECT.yScale = yScaleBinding.get(); refreshAll(); });
textSizeBinding.onChange(function () {
    PROJECT.textSize = Math.round(textSizeBinding.get());
    document.getElementById("pluginFrame").style.setProperty("--text-offset", PROJECT.textSize + "px");
});

// Phase 3.3: pushes the shared, non-parameter view state to the project-wide
// store (projectConfigs[ownKey]) -- groups, groupFilter, soloTrack,
// hiddenTracks and every known track's group (null = no group). Saved with
// the project; the same call from any instance of the project updates every
// other open editor through "sharedViewStateUpdate".
function pushSharedViewState() {
    var trackGroup = {};
    TRACKS.forEach(function (t) { trackGroup[t.id] = t.group || null; });
    callNative("setSharedViewState", [{
        groups: PROJECT.groups,
        groupFilter: Array.from(PROJECT.groupFilter),
        soloTrack: PROJECT.soloTrack,
        hiddenTracks: PROJECT.hiddenTracks,
        trackGroup: trackGroup
    }]);
}
function onSharedViewStateUpdate(data) {
    if (!data) return;
    if (data.groups) PROJECT.groups = data.groups;
    // An EMPTY groupFilter is a legal user choice (every chip off) and must be
    // taken literally; the all-visible default is decided natively.
    if (data.groupFilter) PROJECT.groupFilter = new Set(data.groupFilter);
    if (data.soloTrack !== undefined) PROJECT.soloTrack = data.soloTrack;
    if (data.hiddenTracks) PROJECT.hiddenTracks = data.hiddenTracks;
    if (data.trackGroup) TRACKS.forEach(function (t) { t.group = data.trackGroup[t.id] || null; });
    refreshAll();
}
// The shared GLOBAL configuration changed (edited from any instance of this
// project).
function onGlobalConfigUpdate(data) {
    if (!data) return;
    // While an indicator handle is being dragged the page's own value is ahead
    // of any echo; applying (or re-rendering for) a slightly stale update would
    // fight the drag and detach the handle (the Phase 3.1 drag bug), so it is
    // skipped -- the drag's own commit re-renders, and the next update catches up.
    if (isDragging) return;
    nativeGlobalMirror.apply(data.config, data.indicators);
    // While this instance is linked the mirror is not what is displayed (the
    // host's own relay-backed parameters are, and the native side keeps those
    // in step), so nothing needs re-rendering.
    if (linkedBinding.get()) return;
    if (gearPanel.classList.contains("open") && panelMode === "global") buildGearBody();
    refreshAll();
}
// Another track's linked / mono / group changed, or (config / indicators) its
// OWN configuration changed while it is unlinked. Names and track
// additions/removals arrive as a full "trackListUpdate" instead.
function onTrackConfigUpdate(data) {
    if (!data || !data.trackId) return;
    var t = trackById(data.trackId);
    if (!t) return;
    var isHost = t.id === hostTrackId; // the host's linked/mono come from its own relays
    if (data.linked !== undefined && !isHost) t.linked = data.linked;
    if (data.mono !== undefined && !isHost) t.mono = data.mono;
    if (data.group !== undefined) t.group = data.group;
    if (data.name !== undefined) t.name = data.name;
    if (!isHost) {
        if (!t.linked) ensureOwnCopy(t);
        if (t.remote && (data.config || data.indicators) && !isDragging) t.remote.apply(data.config, data.indicators);
    }
    if (isDragging) return;
    if (t.id === viewingTrackId) syncTopBarToViewingTrack();
    if (gearPanel.classList.contains("open") && panelMode === "track" && gearScopeTrackId === t.id) {
        if (t.linked) gearPanel.classList.remove("open"); else buildGearBody();
    }
    refreshAll();
}
// Project scope: the "Project could not be identified" banner in ALL TRACKS is
// visible iff scope === "unidentified" (never for "identified" or "pending").
function applyProjectScope(scope) {
    var banner = document.getElementById("scopeBanner");
    if (banner) banner.classList.toggle("show", scope === "unidentified");
}
function onProjectScopeUpdate(data) {
    if (data) applyProjectScope(data.scope);
}
// The set / order / names of this project's tracks changed (an instance was
// added, removed, moved or renamed), or this instance's own host id resolved.
function onTrackListUpdate(data) {
    if (!data || !data.tracks) return;
    applyTrackList(data.tracks, data.hostTrackId);
    syncTopBarToViewingTrack();
    if (gearPanel.classList.contains("open")) buildGearBody();
    refreshAll();
}
// Phase 3.2: real for this instance's own host track (emitted from
// PluginEditor.cpp's emitMeterUpdate(), reading Phase 2.1's engine via the
// real SharedMeterSnapshot). Additional tracks' events (THIS-TRACK
// navigation / ALL TRACKS) remain Phase 3.3 scope. Values are raw
// (pre-mono-compensation, pre-display-smoothing) -- monoDb()/frameUpdate()
// below still apply exactly as in the v1-v7 mockup. Fields: trackId, peakL,
// peakR, tpL, tpR, peakLHeld, peakRHeld, tpLHeld, tpRHeld, peakLClips,
// peakRClips, tpLClips, tpRClips, rmsM, rmsMHeld, rmsI, lufsM, lufsMHeld,
// lufsMHist (75-bin, [0,1]-normalised, bin i == db (i-70) -- see
// renderHistogramBars()), lufsS, lufsSHeld, lufsI, lraLo, lraHi.
function onMeterUpdate(payload) {
    if (!payload || !payload.trackId) return;
    var t = trackById(payload.trackId);
    if (!t) return;
    var live = t.live;
    ["peakL", "peakR", "tpL", "tpR", "peakLHeld", "peakRHeld", "tpLHeld", "tpRHeld",
     "peakLClips", "peakRClips", "tpLClips", "tpRClips", "rmsM", "rmsMHeld", "rmsI",
     "lufsM", "lufsMHeld", "lufsMHist", "lufsS", "lufsSHeld", "lufsI", "lraLo", "lraHi"
    ].forEach(function (k) { if (payload[k] !== undefined) live[k] = payload[k]; });
}

/* ======================================================================
   STATE — TRACKS, host/viewing identity
   ====================================================================== */
var GRID_MARKS = [0, -3, -6, -12, -18, -24, -30, -36, -42, -48, -60];

function ensureOwnCopy(t) {
    if (t.id === hostTrackId) return; // hostConfig/hostIndicators are already relay-backed and always current
    if (!t.remote) {
        // Starting point = the current global values ("unlinking copies the
        // current global values into the instance's own copy"); the owning
        // instance publishes the authoritative copy, which then arrives as a
        // trackConfigUpdate and replaces this. Writes go to that instance
        // through setConfigParam(trackId, ...).
        t.remote = makeRemoteConfig(t.id,
            JSON.parse(JSON.stringify(currentGlobalConfig())),
            JSON.parse(JSON.stringify(currentGlobalIndicators())));
        t.ownConfig = t.remote.config;
        t.ownIndicators = t.remote.indicators;
    }
}
function toggleTrackLinked(t) {
    var next = !t.linked;
    if (t.id === hostTrackId) {
        linkedBinding.set(next); // t.linked updates via linkedBinding.onChange in boot()
    } else {
        if (!next) ensureOwnCopy(t);
        callNative("setTrackLinked", [t.id, next]);
        t.linked = next; // optimistic local update until a trackConfigUpdate event confirms it
    }
    logParam("linked(" + t.name + ")", next);
}
function toggleTrackMono(t) {
    var next = !t.mono;
    if (t.id === hostTrackId) {
        monoModeBinding.set(next);
    } else {
        callNative("setTrackMono", [t.id, next]);
        t.mono = next; // optimistic, confirmed by trackConfigUpdate
    }
    logParam("monoMode(" + t.name + ")", next ? "Mono" : "Stereo");
}
function effConfigFor(t) {
    if (t.id === hostTrackId) return hostConfig;
    return t.linked ? currentGlobalConfig() : t.ownConfig;
}
function effIndicatorsFor(t) {
    if (t.id === hostTrackId) return hostIndicators;
    return t.linked ? currentGlobalIndicators() : t.ownIndicators;
}

/* ---- live measurement data: silence until the first onMeterUpdate() ---- */
function newLive() {
    return {
        peakL: -72, peakR: -72, tpL: -72, tpR: -72,
        peakLHeld: -72, peakRHeld: -72, tpLHeld: -72, tpRHeld: -72,
        peakLClips: 0, peakRClips: 0, tpLClips: 0, tpRClips: 0,
        rmsM: -72, rmsMHeld: -72, rmsI: -72,
        lufsM: -72, lufsMHeld: -72, lufsMHist: null,
        lufsS: -72, lufsSHeld: -72,
        lraLo: -72, lraHi: -72,
        lufsI: -72,
        dispPeakL: -72, dispPeakR: -72, dispTpL: -72, dispTpR: -72,
        dispRmsM: -72, dispRmsI: -72, dispLufsM: -72, dispLufsS: -72, dispLufsI: -72,
        dispLraLo: -72, dispLraHi: -72, dispLraValue: 0
    };
}

var TRACKS = [];

/* ---- instance identity ----
   hostTrackId: the track THIS plugin instance is inserted on (sourced via
   getHostTrackId() in boot(); changes once, from the pending placeholder to
   the resolved id, through trackListUpdate -- see applyTrackList()).
   viewingTrackId: which track's meters the own view currently shows
   (transient editor state, steppable via prev/next/back). They start
   equal, as a freshly opened instance always does. */
var hostTrackId = null;
var viewingTrackId = null;
var gearScopeTrackId = null; // which track a "track"-mode gear panel is scoped to
var panelMode = "global";    // "global" | "track"
var currentView = "own";     // own | combined

/* ======================================================================
   WINDOW RESIZE ON VIEW SWITCH -- see v7-ui.yaml -> window_resize_on_view_
   switch for the full resolved semantics. THIS TRACK -> ALL TRACKS grows
   automatically to fit every currently VISIBLE track box (after solo/hide/
   group filters) at its natural width; HEIGHT is carried forward exactly as
   set in THIS TRACK. ALL TRACKS -> THIS TRACK restores the exact saved
   single-track size. Clicking SHOW ALL re-fits WIDTH only, exactly like a
   fresh entry, whenever it actually changed something (v7, NEW). The real
   screen-cap / oversized-height clamps and the min/max resize LIMITS are
   all owned by C++ (see requestEditorWidthNative() above and
   v7-PluginEditor.cpp) -- this file only ever proposes a candidate size.
   ====================================================================== */
var pluginFrame = document.getElementById("pluginFrame");
// .plugin-frame has CSS zoom 1.1 (index.html). getBoundingClientRect() and
// pointer coordinates are in window pixels (zoomed); computed styles and
// style.* lengths are in the frame's own CSS pixels. Convert with UI_ZOOM.
var UI_ZOOM = 1.1;
// Window pixels: the 360x420 CSS-px layout floor at UI_ZOOM. Must match the
// C++ resize limits (PluginEditor.cpp).
var MIN_FRAME_WIDTH = 396, MIN_FRAME_HEIGHT = 462;

// The single-track size to restore when leaving ALL TRACKS. Conceptually
// this mirrors the instance's persisted non-parameter editor state (like
// any normal JUCE plugin editor size) -- see v7-PluginEditor.cpp's
// getEditorStateSize()/setEditorStateSize() TODO hooks. Starts at the
// default frame size until boot() reads the real persisted size.
var savedSingleTrackSize = { width: 528, height: 572 };

/* Measures the width actually needed to show every currently visible track
   box (after solo/hide/group filters -- renderCombinedView() must have
   already run) side by side with NO horizontal scrolling. Reads real
   computed styles + each visible box's own intrinsic rendered width. Sums
   each visible box's own getBoundingClientRect().width (each .track-box is
   flex:0 0 auto, so its own rendered width is always intrinsic/content-
   driven, independent of the row's current width) rather than the row's
   scrollWidth, so it stays correct even when called from INSIDE an
   already-wide ALL TRACKS frame (see refitWidthOnShowAll below). */
function measureAllTracksContentWidth() {
    var trackRow = document.getElementById("trackRow");
    var combinedBody = document.querySelector(".combined-body");
    var cbStyle = getComputedStyle(combinedBody);
    var trStyle = getComputedStyle(trackRow);
    var padLeft = (parseFloat(cbStyle.paddingLeft) || 0) * UI_ZOOM;
    var padRight = (parseFloat(cbStyle.paddingRight) || 0) * UI_ZOOM;
    var rowGap = (parseFloat(trStyle.columnGap || trStyle.gap) || 0) * UI_ZOOM; // gap BETWEEN individual track boxes
    var visibleBoxes = Array.prototype.filter.call(trackRow.children, function (b) {
        return getComputedStyle(b).display !== "none"; // excludes .hidden-box (display:none); .dimmed boxes still occupy width
    });
    var trackRowContentW = 0;
    visibleBoxes.forEach(function (b) { trackRowContentW += b.getBoundingClientRect().width; });
    if (visibleBoxes.length > 1) trackRowContentW += rowGap * (visibleBoxes.length - 1);
    return Math.ceil(trackRowContentW + padLeft + padRight);
}

// Authoritative write point for savedSingleTrackSize. Called (a) live, from
// the ResizeObserver below, on every resize while currentView === "own"
// (covers the user dragging the real host window's edge), and (b)
// defensively, synchronously, the instant before switching away from THIS
// TRACK.
function captureSingleTrackSizeIfOwn() {
    if (currentView !== "own") return;
    var r = pluginFrame.getBoundingClientRect();
    savedSingleTrackSize = { width: Math.round(r.width), height: Math.round(r.height) };
    callNative("setEditorStateSize", [savedSingleTrackSize.width, savedSingleTrackSize.height]); // Phase 3.2: persisted as non-parameter <EditorState> XML
}

function applyFrameSize(width, height) {
    requestEditorWidthNative(width, height);
}

// THIS TRACK -> ALL TRACKS. Must run AFTER renderCombinedView() so the
// width measurement reflects the CURRENTLY visible boxes, and AFTER the
// caller's captureSingleTrackSizeIfOwn() (called immediately before the
// view switch, while still in THIS TRACK) so savedSingleTrackSize.height
// already holds the exact THIS TRACK height to carry forward. HORIZONTAL-
// ONLY: only WIDTH adapts; HEIGHT is left exactly as it was in THIS TRACK
// (C++ applies the real screen-width cap and the oversized-height edge-case
// clamp -- see requestEditorWidth in v7-PluginEditor.cpp).
function growToAllTracks() {
    var neededW = measureAllTracksContentWidth();
    var targetW = Math.max(MIN_FRAME_WIDTH, neededW);
    var targetH = Math.max(MIN_FRAME_HEIGHT, savedSingleTrackSize.height);
    applyFrameSize(targetW, targetH);
}

// ALL TRACKS -> THIS TRACK.
function shrinkToSingleTrack() {
    var targetW = Math.max(MIN_FRAME_WIDTH, savedSingleTrackSize.width);
    var targetH = Math.max(MIN_FRAME_HEIGHT, savedSingleTrackSize.height);
    applyFrameSize(targetW, targetH);
}

// v7: "If I'm in ALL TRACKS and have hidden instances, clicking SHOW ALL
// should also automatically re-fit the horizontal size." Called ONLY from
// the btnShowAll handler below, and ONLY when SHOW ALL actually changed
// something (unhid at least one track and/or cleared an active solo) -- see
// v7-ui.yaml -> show_all_refit. Reuses the EXACT SAME width formula as
// growToAllTracks() but deliberately does NOT touch height: it reads the
// frame's CURRENT height straight off the DOM and passes it straight back
// through unchanged.
function refitWidthOnShowAll() {
    var neededW = measureAllTracksContentWidth();
    var targetW = Math.max(MIN_FRAME_WIDTH, neededW);
    var currentH = Math.round(pluginFrame.getBoundingClientRect().height);
    applyFrameSize(targetW, currentH);
}

function currentViewingTrack() { return TRACKS.filter(function (t) { return t.id === viewingTrackId; })[0]; }
function trackById(id) { return TRACKS.filter(function (t) { return t.id === id; })[0]; }

/* ======================================================================
   INDICATOR EVALUATION
   ====================================================================== */
function evalIndicator(cfg, displayedValue) {
    if (!cfg || cfg.mode === "off") return "normal";
    if (cfg.mode === "threshold") return displayedValue > cfg.threshold ? "red" : "normal";
    if (cfg.mode === "range") {
        if (displayedValue > cfg.max) return "red";
        if (displayedValue >= cfg.min && displayedValue <= cfg.max) return "green";
        return "normal";
    }
    return "normal";
}

/* Indicator colour stabilisation (display only; measurements are untouched).
   A meter hovering around a threshold used to swap the whole bar's colour on
   every frame, which is distracting and a photosensitivity risk (WCAG 2.3.1:
   at most 3 flashes per second). Two rules keep the shown state calm:
   - hysteresis: a bar leaves red/green only once the value is
     IND_HYSTERESIS_DB past the edge it crossed on the way in;
   - minimum hold: a shown state lasts at least IND_MIN_HOLD_MS, so a bar
     changes colour at most ~3 times per second (~1.4 flashes/s, under half
     the WCAG limit). Keep it >= 250 ms.
   The colour change itself fades over 100 ms in CSS (.bar-fill). */
var IND_HYSTERESIS_DB = 0.5;
var IND_MIN_HOLD_MS = 350;

/* How an active indicator colours the bar (Settings > Project):
   "zone" (default) - only the part of the bar past the threshold / inside the
            range is coloured (red above the threshold or range max, green
            inside the range); the rest keeps the measurement's own colour;
   "bar"  - the whole bar changes colour (stabilised, see above).
   A per-machine viewing preference (localStorage), not a project setting. */
var INDICATOR_STYLE = (function () {
    try { return localStorage.getItem("meterscope.indicatorStyle") === "bar" ? "bar" : "zone"; }
    catch (e) { return "zone"; }
})();

function setIndicatorStyle(style) {
    INDICATOR_STYLE = style;
    try { localStorage.setItem("meterscope.indicatorStyle", style); } catch (e) {}
}

function setBarZone(zone, loFrac, hiFrac) {
    var h = Math.max(0, hiFrac - loFrac);
    zone.style.display = h > 0 ? "" : "none";
    zone.style.bottom = (loFrac * 100) + "%";
    zone.style.height = (h * 100) + "%";
}

// "zone" style: colours [edge, current level] of the bar. cfg null hides both.
function updateIndicatorZones(zoneGreen, zoneRed, cfg, fillFrac, yScale) {
    if (!cfg || cfg.mode === "off") {
        setBarZone(zoneGreen, 0, 0);
        setBarZone(zoneRed, 0, 0);
        return;
    }
    if (cfg.mode === "threshold") {
        setBarZone(zoneGreen, 0, 0);
        setBarZone(zoneRed, heightFrac(cfg.threshold, yScale), fillFrac);
        return;
    }
    var lo = heightFrac(cfg.min, yScale), hi = heightFrac(cfg.max, yScale);
    setBarZone(zoneGreen, lo, Math.min(fillFrac, hi));
    zoneGreen.classList.toggle("tops-fill", fillFrac <= hi);
    setBarZone(zoneRed, hi, fillFrac);
}

function evalIndicatorWithHysteresis(cfg, displayedValue, shownState) {
    var raw = evalIndicator(cfg, displayedValue);
    if (raw === shownState || !cfg || cfg.mode === "off") return raw;
    var h = IND_HYSTERESIS_DB;
    if (shownState === "red") {
        var edge = cfg.mode === "threshold" ? cfg.threshold : cfg.max;
        if (displayedValue > edge - h) return "red";
    } else if (shownState === "green" && cfg.mode === "range" && raw === "normal") {
        if (displayedValue >= cfg.min - h) return "green";
    }
    return raw;
}

function makeIndicatorStabilizer(initialState) {
    var shown = initialState || "normal";
    var since = -Infinity; // the first change is never delayed
    return function (info) {
        if (!info.cfg) return info.state;
        var target = evalIndicatorWithHysteresis(info.cfg, info.value, shown);
        var now = performance.now();
        if (target !== shown && now - since >= IND_MIN_HOLD_MS) {
            shown = target;
            since = now;
        }
        return shown;
    };
}

/* monoDb() applies the JSFX-semantics -3dB SHOWN-value compensation
   (parameter-spec.md "monoMode") to loudness/RMS measurements only -- never
   Peak/True Peak, whose mono behaviour is channel-selection only. All
   RMS-M/RMS-I/LUFS-M/LUFS-S/LUFS-I current+held values and the LRA lo/hi
   drawing pass through this before being displayed, formatted or compared
   by evalIndicator; the LRA VALUE itself (hi-lo) is deliberately left
   unadjusted. isHeldWarn() is the fixed, indicator-independent >=0dB
   held-max rule (creative-brief.md Deliberate Differences #4), judged on
   the SAME already-mono-adjusted displayed value. */
function monoDb(v, mono) { return mono ? v - 3 : v; }
function isHeldWarn(displayedHeldDb) { return displayedHeldDb >= 0.0; }

/* ======================================================================
   DOM BUILDERS — range zones / drag handles
   ====================================================================== */
function buildRangeZones(cfg, yScale) {
    var wrap = el("div", "range-zones");
    if (!cfg || cfg.mode === "off") return wrap;
    if (cfg.mode === "threshold") {
        var tFrac = heightFrac(cfg.threshold, yScale);
        var topPct = (1 - tFrac) * 100;
        var red = el("div", "zone");
        red.style.top = "0%"; red.style.height = topPct + "%";
        red.style.background = "var(--ind-fill-red)";
        wrap.appendChild(red);
    } else if (cfg.mode === "range") {
        var maxFrac = heightFrac(cfg.max, yScale);
        var minFrac = heightFrac(cfg.min, yScale);
        var topMax = (1 - maxFrac) * 100;
        var topMin = (1 - minFrac) * 100;
        var zRed = el("div", "zone");
        zRed.style.top = "0%"; zRed.style.height = topMax + "%";
        zRed.style.background = "var(--ind-fill-red)";
        var zGreen = el("div", "zone");
        zGreen.style.top = topMax + "%"; zGreen.style.height = (topMin - topMax) + "%";
        zGreen.style.background = "var(--ind-fill-green)";
        var zNorm = el("div", "zone");
        zNorm.style.top = topMin + "%"; zNorm.style.bottom = "0%";
        zNorm.style.background = "var(--ind-fill-normal)";
        wrap.appendChild(zRed); wrap.appendChild(zGreen); wrap.appendChild(zNorm);
    }
    return wrap;
}

function buildDragHandles(cfg, yScale, indicatorKey, onCommit, zonesWrap) {
    var frag = document.createDocumentFragment();
    if (!cfg || cfg.mode === "off") return frag;
    if (cfg.mode === "threshold") {
        var h = el("div", "drag-handle no-solo");
        h.style.top = ((1 - heightFrac(cfg.threshold, yScale)) * 100) + "%";
        attachDrag(h, cfg, "threshold", yScale, onCommit, indicatorKey, zonesWrap);
        frag.appendChild(h);
    } else if (cfg.mode === "range") {
        var hMax = el("div", "drag-handle edge no-solo");
        hMax.style.top = ((1 - heightFrac(cfg.max, yScale)) * 100) + "%";
        attachDrag(hMax, cfg, "max", yScale, onCommit, indicatorKey, zonesWrap);
        var hMin = el("div", "drag-handle edge no-solo");
        hMin.style.top = ((1 - heightFrac(cfg.min, yScale)) * 100) + "%";
        attachDrag(hMin, cfg, "min", yScale, onCommit, indicatorKey, zonesWrap);
        frag.appendChild(hMax); frag.appendChild(hMin);
    }
    return frag;
}

var tooltipEl = document.getElementById("dragTooltip");
var isDragging = false;
var frameUpdaters = [];

function repositionZones(zonesWrap, cfg, yScale) {
    if (!zonesWrap) return;
    var zones = zonesWrap.children;
    if (cfg.mode === "threshold" && zones.length >= 1) {
        var topPct = (1 - heightFrac(cfg.threshold, yScale)) * 100;
        zones[0].style.height = topPct + "%";
    } else if (cfg.mode === "range" && zones.length >= 3) {
        var topMax = (1 - heightFrac(cfg.max, yScale)) * 100;
        var topMin = (1 - heightFrac(cfg.min, yScale)) * 100;
        zones[0].style.height = topMax + "%";
        zones[1].style.top = topMax + "%";
        zones[1].style.height = (topMin - topMax) + "%";
        zones[2].style.top = topMin + "%";
    }
}

// Param ID for the relay/attachment this handle drives -- MUST match the
// "ind" + indicatorKey + ("Threshold"|"RangeMax"|"RangeMin") convention used
// both by logParam() below and by PluginEditor.cpp's relay/attachment
// construction (parameter-spec.md's 28 Level Indicator parameters).
function indicatorParamId(indicatorKey, edge) {
    return "ind" + indicatorKey + (edge === "threshold" ? "Threshold" : (edge === "max" ? "RangeMax" : "RangeMin"));
}

function attachDrag(handleEl, cfg, edge, yScale, onCommit, indicatorKey, zonesWrap) {
    var paramId = indicatorParamId(indicatorKey, edge);
    handleEl.addEventListener("pointerdown", function (e) {
        e.stopPropagation();
        e.preventDefault();
        var track = handleEl.parentElement;
        handleEl.setPointerCapture(e.pointerId);
        isDragging = true;
        // One drag gesture = one undo step (Phase 3.2) -- must bracket every
        // value change made by move() below between these two calls.
        Juce.getSliderState(paramId).sliderDragStarted();
        var lastDb = null;

        function move(ev) {
            var rect = track.getBoundingClientRect();
            var frac = 1 - (ev.clientY - rect.top) / rect.height;
            var db = dbFromFrac(frac, yScale);
            if (edge === "threshold") {
                cfg.threshold = db;
            } else if (edge === "max") {
                cfg.max = Math.max(db, cfg.min);
            } else if (edge === "min") {
                cfg.min = Math.min(db, cfg.max);
            }
            lastDb = db;

            handleEl.style.top = ((1 - heightFrac(edge === "threshold" ? cfg.threshold : (edge === "max" ? cfg.max : cfg.min), yScale)) * 100) + "%";
            repositionZones(zonesWrap, cfg, yScale);

            tooltipEl.style.display = "block";
            tooltipEl.style.left = ev.clientX + "px";
            tooltipEl.style.top = ev.clientY + "px";
            tooltipEl.textContent = (edge === "threshold" ? "Threshold " : (edge === "max" ? "Max " : "Min ")) + fmtDb(db) + " dB";
        }
        function up(ev) {
            document.removeEventListener("pointermove", move);
            document.removeEventListener("pointerup", up);
            tooltipEl.style.display = "none";
            isDragging = false;
            Juce.getSliderState(paramId).sliderDragEnded();
            if (lastDb !== null) {
                logParam(paramId, lastDb);
            }
            onCommit();
        }
        document.addEventListener("pointermove", move);
        document.addEventListener("pointerup", up);
    });
}

/**
 * Builds one held-max / bar / current-value stack (no label).
 */
function buildStack(opts) {
    var stack = el("div", "meter-stack");

    var held = el("div", "held-max-slot");
    var heldChip = null, heldMarker = null, heldMarkerState = "normal", heldWarnGlyph = null;
    if (opts.heldMode === "dash") {
        var chipDash = el("span", "value-chip");
        chipDash.textContent = "—";
        held.appendChild(chipDash);
    } else {
        held.classList.add("clickable", "no-solo");
        held.title = "Click to reset max";
        heldWarnGlyph = el("span", "held-warn-glyph");
        heldWarnGlyph.textContent = "!";
        held.appendChild(heldWarnGlyph);
        heldChip = el("span", "value-chip");
        heldChip.textContent = fmtDb(opts.heldValue);
        held.appendChild(heldChip);
        if (opts.heldWarnActive) {
            heldWarnGlyph.classList.add("active");
            heldChip.classList.add("held-warn");
            held.title = "≥ 0 dB reached — click to reset max";
        }
        if (opts.heldColorState === "green") {
            heldMarker = el("span", "marker-dot"); held.appendChild(heldMarker); heldMarkerState = "green";
        } else if (opts.heldColorState === "red") {
            heldMarker = el("span", "marker-tri"); held.appendChild(heldMarker); heldMarkerState = "red";
        }
        held.addEventListener("click", function (e) { e.stopPropagation(); opts.onHeldClick && opts.onHeldClick(); });
    }
    stack.appendChild(held);

    var track = el("div", "bar-track");
    var zonesWrap = buildRangeZones(opts.cfg, opts.yScale);
    track.appendChild(zonesWrap);

    var fill = null, capLo = null, capHi = null, lraBar = null, histWrap = null, lastHistArr = null;
    var zoneGreen = null, zoneRed = null;
    if (opts.lra) {
        var loFrac = heightFrac(opts.lra.lo, opts.yScale);
        var hiFrac = heightFrac(opts.lra.hi, opts.yScale);
        capLo = el("div", "lra-cap"); capLo.style.bottom = (loFrac * 100) + "%";
        capHi = el("div", "lra-cap"); capHi.style.bottom = (hiFrac * 100) + "%";
        lraBar = el("div", "lra-bar");
        lraBar.style.bottom = (loFrac * 100) + "%";
        lraBar.style.height = ((hiFrac - loFrac) * 100) + "%";
        track.appendChild(lraBar); track.appendChild(capLo); track.appendChild(capHi);
    } else {
        fill = el("div", "bar-fill " + (opts.family || "family-peak"));
        if (opts.barColorState === "green") fill.classList.add("state-green");
        if (opts.barColorState === "red") fill.classList.add("state-red");
        fill.style.height = (opts.fillFrac * 100) + "%";
        track.appendChild(fill);

        if (opts.cfg) {
            zoneGreen = el("div", "bar-zone zone-green");
            zoneRed = el("div", "bar-zone zone-red");
            setBarZone(zoneGreen, 0, 0);
            setBarZone(zoneRed, 0, 0);
            track.appendChild(zoneGreen);
            track.appendChild(zoneRed);
        }

        if (opts.histogram) {
            histWrap = el("div", "hist-wrap");
            track.appendChild(histWrap);
            renderHistogramBars(histWrap, opts.histogram.bins, opts.histogram.yScale);
            lastHistArr = opts.histogram.bins;
        }
    }

    if (opts.indicatorKey && opts.cfg) {
        track.appendChild(buildDragHandles(opts.cfg, opts.yScale, opts.indicatorKey, opts.onDrag || function () {}, zonesWrap));
    }
    stack.appendChild(track);

    var cur = el("div", "current-value-slot");
    if (opts.curColorState === "green") cur.classList.add("state-green");
    if (opts.curColorState === "red") cur.classList.add("state-red");
    var curChip = el("span", "value-chip");
    curChip.textContent = opts.curValue;
    cur.appendChild(curChip);
    stack.appendChild(cur);

    if (opts.updaters && opts.dynamic) {
        var d = opts.dynamic;
        var stableState = makeIndicatorStabilizer(opts.curColorState);
        opts.updaters.push(function () {
            var fillFrac = fill ? d.getFillFrac() : 0;
            if (fill) {
                fill.style.height = (fillFrac * 100) + "%";
            }
            if (capLo) {
                var lohi = d.getLoHi();
                var loF = heightFrac(lohi.lo, opts.yScale), hiF = heightFrac(lohi.hi, opts.yScale);
                capLo.style.bottom = (loF * 100) + "%";
                capHi.style.bottom = (hiF * 100) + "%";
                lraBar.style.bottom = (loF * 100) + "%";
                lraBar.style.height = ((hiF - loF) * 100) + "%";
            }
            var curInfo = d.getCur();
            var shownState = stableState(curInfo);
            curChip.textContent = curInfo.text;
            cur.classList.toggle("state-green", shownState === "green");
            cur.classList.toggle("state-red", shownState === "red");
            if (fill) {
                var zoneStyle = INDICATOR_STYLE === "zone" && !!curInfo.cfg;
                fill.classList.toggle("state-green", !zoneStyle && shownState === "green");
                fill.classList.toggle("state-red", !zoneStyle && shownState === "red");
                if (zoneGreen) updateIndicatorZones(zoneGreen, zoneRed, zoneStyle ? curInfo.cfg : null, fillFrac, opts.yScale);
            }
            if (heldChip && d.getHeld) {
                var heldInfo = d.getHeld();
                heldChip.textContent = heldInfo.text;
                var warnNow = !!heldInfo.warn;
                heldChip.classList.toggle("held-warn", warnNow);
                if (heldWarnGlyph) heldWarnGlyph.classList.toggle("active", warnNow);
                held.title = warnNow ? "≥ 0 dB reached — click to reset max" : "Click to reset max";
                if (heldInfo.state !== heldMarkerState) {
                    heldMarkerState = heldInfo.state;
                    if (heldMarker) { heldMarker.remove(); heldMarker = null; }
                    if (heldInfo.state === "green") { heldMarker = el("span", "marker-dot"); held.appendChild(heldMarker); }
                    else if (heldInfo.state === "red") { heldMarker = el("span", "marker-tri"); held.appendChild(heldMarker); }
                }
            }
            if (histWrap && d.getHistogram) {
                var hb = d.getHistogram();
                if (hb !== lastHistArr) {
                    lastHistArr = hb;
                    renderHistogramBars(histWrap, hb, opts.histogram.yScale);
                }
            }
        });
    }

    return stack;
}

// bins[i] is the LUFS-M measurement DENSITY in the 1 dB-wide bin
// [i-70, i-70+1) dB -- i.e. the JSFX's own db_hist[75] domain (-70..+4dB,
// loudness_meter_cockos.jsfx lines 728-741: `db = d-70`), pre-normalised by
// the C++ side (Phase 3.2) to a [0,1] fraction of that histogram's own peak
// bin, matching the JSFX's own `tw = cnt/db_hist_max*maxtw` bar-width scaling
// (line 732). Positioning each bin at its REAL dB value (not an
// index-relative -72..0 approximation of the array's length) through the
// SAME heightFrac() used for every other bar/indicator/marker in this file
// (heightFrac is itself an exact port of the JSFX's own scale_db1(), which
// this histogram loop also feeds real dB values into) keeps histogram bar
// Y-positions numerically consistent with everything else on the meter.
function renderHistogramBars(histWrap, bins, yScale) {
    histWrap.innerHTML = "";
    for (var i = 0; i < bins.length; i++) {
        var binDb = i - 70;
        var frac = heightFrac(binDb, yScale);
        var top = (1 - frac) * 100;
        var w = Math.round(bins[i] * 12);
        if (w > 0) {
            var left = el("div", "hist-bar");
            left.style.top = top + "%";
            left.style.right = "calc(50% + 3px)";
            left.style.width = w + "px";
            var right = el("div", "hist-bar");
            right.style.top = top + "%";
            right.style.left = "calc(50% + 3px)";
            right.style.width = w + "px";
            histWrap.appendChild(left); histWrap.appendChild(right);
        }
    }
}

// Placeholder (all-zero) histogram used only until the first onMeterUpdate()
// with lufsMHist data arrives for a track. 75 bins to match the real
// db_hist[75] domain (-70..+4dB) -- irrelevant while every value is 0 (no
// bars render either way), kept in sync purely for clarity.
function silentHistogram() {
    var arr = [];
    for (var i = 0; i < 75; i++) arr.push(0);
    return arr;
}

/**
 * Renders a full meter cluster (own view OR one combined-view box).
 * monoDb() mono -3dB compensation is applied to every loudness/RMS reading
 * (current, held, LRA lo/hi, histogram basis -- never the LRA value itself,
 * and never Peak/True Peak). The fixed 0dB held-max warning
 * (heldWarnActive / getHeld().warn) is applied to every measurement that
 * has a held max. One function, reused identically by the own view AND
 * every combined-view box.
 */
// The button under a meter's label cycles its indicator: none ("+IND") ->
// Threshold -> Range -> none. It writes the same mode parameter as the
// settings panel, so a linked track edits the global indicator.
var IND_CYCLE_NEXT = { off: "threshold", threshold: "range", range: "off" };
function indicatorCycleButton(indicators, key, onChange) {
    var mode = indicators[key].mode;
    var b = el("button", "ind-cycle no-solo" + (mode !== "off" ? " on" : ""));
    if (mode === "threshold") { b.appendChild(icon("ind-thr")); b.title = "Indicator: Threshold (click for Range)"; }
    else if (mode === "range") { b.appendChild(icon("ind-rng")); b.title = "Indicator: Range (click to remove)"; }
    else { b.textContent = "+IND"; b.title = "Add a Threshold indicator"; }
    b.addEventListener("click", function (e) {
        e.stopPropagation();
        var next = IND_CYCLE_NEXT[indicators[key].mode] || "threshold";
        indicators[key].mode = next; logParam("ind" + key + "Mode", next);
        onChange();
    });
    return b;
}

function renderMeterCluster(containerEl, trackId, config, indicators, live, mono, colW, colHalfW, yScale, onChange, updaters) {
    containerEl.innerHTML = "";

    function col(spec) {
        var c = el("div", "meter-col");
        c.style.width = spec.width + "px";
        var row = el("div", "stacks-row");
        spec.stacks.forEach(function (s) { row.appendChild(s); });
        c.appendChild(row);
        var label = el("div", "meter-label-row");
        var lt = document.createTextNode(spec.label);
        label.appendChild(lt);
        c.appendChild(label);
        // Every column gets the row (LRA's stays empty) so labels stay aligned.
        var indRow = el("div", "ind-cycle-row");
        if (spec.indKey) indRow.appendChild(indicatorCycleButton(indicators, spec.indKey, onChange));
        c.appendChild(indRow);
        containerEl.appendChild(c);
    }

    // ---- Peak / True Peak group ----
    var mode = config.peakMode;
    if (mode !== "off") {
        var isTP = (mode === "tp" || mode === "tpclips");
        var isClip = (mode === "tpclips" || mode === "peakclips");
        var indKey = isTP ? "TruePeak" : "Peak";
        var cfg = indicators[indKey];
        var famClass = "family-peak";
        var label = isTP ? "True Peak" : "Peak";

        function channelStack(ch) {
            function curDb() { return isTP ? (ch === "L" ? live.dispTpL : live.dispTpR) : (ch === "L" ? live.dispPeakL : live.dispPeakR); }
            function heldDb() { return isTP ? (ch === "L" ? live.tpLHeld : live.tpRHeld) : (ch === "L" ? live.peakLHeld : live.peakRHeld); }
            function clips() { return isTP ? (ch === "L" ? live.tpLClips : live.tpRClips) : (ch === "L" ? live.peakLClips : live.peakRClips); }
            var initCur = curDb(), initHeld = heldDb();
            var barState = evalIndicator(cfg, initCur);
            var heldState = evalIndicator(cfg, initHeld);
            return buildStack({
                heldMode: "value", heldValue: initHeld, heldColorState: heldState, heldWarnActive: isHeldWarn(initHeld),
                onHeldClick: function () {
                    live.peakLHeld = -72; live.peakRHeld = -72; live.tpLHeld = -72; live.tpRHeld = -72;
                    callNative("resetTrackMeterMax", [trackId, isTP ? "TruePeak" : "Peak"]); // partial: clears only this measurement's held max + clips (see PluginEditor.cpp); any track of this project
                    logParam((isTP ? "TruePeak" : "Peak") + "HeldMaxReset(" + ch + "+both)", "reset");
                    onChange();
                },
                curValue: isClip ? fmtClip(clips()) : fmtDb(initCur),
                curColorState: barState,
                family: famClass, fillFrac: heightFrac(initCur, yScale),
                barColorState: barState,
                cfg: cfg, yScale: yScale, indicatorKey: indKey, onDrag: onChange,
                updaters: updaters,
                dynamic: {
                    getFillFrac: function () { return heightFrac(curDb(), yScale); },
                    getCur: function () { var c = curDb(); return { text: isClip ? fmtClip(clips()) : fmtDb(c), state: evalIndicator(cfg, c), cfg: cfg, value: c }; },
                    getHeld: function () { var h = heldDb(); return { text: fmtDb(h), state: evalIndicator(cfg, h), warn: isHeldWarn(h) }; }
                }
            });
        }

        var stacks = mono ? [channelStack("L")] : [channelStack("L"), channelStack("R")];
        col({ label: label, width: mono ? colW + 22 : (colW * 2 + 4), stacks: stacks, indKey: indKey });
    }

    // ---- RMS-M ----
    if (config.showRmsM) {
        var rmsMCur0 = monoDb(live.dispRmsM, mono);
        var rmsMHeld0 = monoDb(live.rmsMHeld, mono);
        var st = evalIndicator(indicators.RmsM, rmsMCur0);
        var hst = evalIndicator(indicators.RmsM, rmsMHeld0);
        col({ label: "RMS-M", indKey: "RmsM", width: colW, stacks: [buildStack({
            heldMode: "value", heldValue: rmsMHeld0, heldColorState: hst, heldWarnActive: isHeldWarn(rmsMHeld0),
            onHeldClick: function () { live.rmsMHeld = -72; callNative("resetTrackMeterMax", [trackId, "RmsM"]); logParam("RmsMHeldMaxReset", "reset"); onChange(); },
            curValue: fmtDb(rmsMCur0), curColorState: st,
            family: "family-rms", fillFrac: heightFrac(rmsMCur0, yScale), barColorState: st,
            cfg: indicators.RmsM, yScale: yScale, indicatorKey: "RmsM", onDrag: onChange,
            updaters: updaters,
            dynamic: {
                getFillFrac: function () { return heightFrac(monoDb(live.dispRmsM, mono), yScale); },
                getCur: function () { var v = monoDb(live.dispRmsM, mono); return { text: fmtDb(v), state: evalIndicator(indicators.RmsM, v), cfg: indicators.RmsM, value: v }; },
                getHeld: function () { var v = monoDb(live.rmsMHeld, mono); return { text: fmtDb(v), state: evalIndicator(indicators.RmsM, v), warn: isHeldWarn(v) }; }
            }
        })] });
    }

    // ---- RMS-I ----
    if (config.showRmsI) {
        var rmsICur0 = monoDb(live.dispRmsI, mono);
        var stI = evalIndicator(indicators.RmsI, rmsICur0);
        col({ label: "RMS-I", indKey: "RmsI", width: colW, stacks: [buildStack({
            heldMode: "dash",
            curValue: fmtDb(rmsICur0), curColorState: stI,
            family: "family-rms", fillFrac: heightFrac(rmsICur0, yScale), barColorState: stI,
            cfg: indicators.RmsI, yScale: yScale, indicatorKey: "RmsI", onDrag: onChange,
            updaters: updaters,
            dynamic: {
                getFillFrac: function () { return heightFrac(monoDb(live.dispRmsI, mono), yScale); },
                getCur: function () { var v = monoDb(live.dispRmsI, mono); return { text: fmtDb(v), state: evalIndicator(indicators.RmsI, v), cfg: indicators.RmsI, value: v }; }
            }
        })] });
    }

    // ---- LUFS-M ----
    if (config.lufsMMode !== "off") {
        var hasHist = config.lufsMMode === "hist";
        if (!live.lufsMHist) { live.lufsMHist = silentHistogram(); }
        var lufsMCur0 = monoDb(live.dispLufsM, mono);
        var lufsMHeld0 = monoDb(live.lufsMHeld, mono);
        var stM = evalIndicator(indicators.LufsM, lufsMCur0);
        var hstM = evalIndicator(indicators.LufsM, lufsMHeld0);
        col({ label: "LUFS-M", indKey: "LufsM", width: colW + (hasHist ? 22 : 0), stacks: [buildStack({
            heldMode: "value", heldValue: lufsMHeld0, heldColorState: hstM, heldWarnActive: isHeldWarn(lufsMHeld0),
            onHeldClick: function () { live.lufsMHeld = -72; callNative("resetTrackMeterMax", [trackId, "LufsM"]); logParam("LufsMHeldMaxReset", "reset"); onChange(); },
            curValue: fmtDb(lufsMCur0), curColorState: stM,
            family: "family-lufs", fillFrac: heightFrac(lufsMCur0, yScale), barColorState: stM,
            cfg: indicators.LufsM, yScale: yScale, indicatorKey: "LufsM", onDrag: onChange,
            histogram: hasHist ? { bins: live.lufsMHist, yScale: yScale } : null,
            updaters: updaters,
            dynamic: {
                getFillFrac: function () { return heightFrac(monoDb(live.dispLufsM, mono), yScale); },
                getCur: function () { var v = monoDb(live.dispLufsM, mono); return { text: fmtDb(v), state: evalIndicator(indicators.LufsM, v), cfg: indicators.LufsM, value: v }; },
                getHeld: function () { var v = monoDb(live.lufsMHeld, mono); return { text: fmtDb(v), state: evalIndicator(indicators.LufsM, v), warn: isHeldWarn(v) }; },
                getHistogram: hasHist ? function () { return live.lufsMHist; } : null
            }
        })] });
    }

    // ---- LUFS-S ----
    if (config.showLufsS) {
        var lufsSCur0 = monoDb(live.dispLufsS, mono);
        var lufsSHeld0 = monoDb(live.lufsSHeld, mono);
        var stS = evalIndicator(indicators.LufsS, lufsSCur0);
        var hstS = evalIndicator(indicators.LufsS, lufsSHeld0);
        col({ label: "LUFS-S", indKey: "LufsS", width: colW, stacks: [buildStack({
            heldMode: "value", heldValue: lufsSHeld0, heldColorState: hstS, heldWarnActive: isHeldWarn(lufsSHeld0),
            onHeldClick: function () { live.lufsSHeld = -72; callNative("resetTrackMeterMax", [trackId, "LufsS"]); logParam("LufsSHeldMaxReset", "reset"); onChange(); },
            curValue: fmtDb(lufsSCur0), curColorState: stS,
            family: "family-lufs", fillFrac: heightFrac(lufsSCur0, yScale), barColorState: stS,
            cfg: indicators.LufsS, yScale: yScale, indicatorKey: "LufsS", onDrag: onChange,
            updaters: updaters,
            dynamic: {
                getFillFrac: function () { return heightFrac(monoDb(live.dispLufsS, mono), yScale); },
                getCur: function () { var v = monoDb(live.dispLufsS, mono); return { text: fmtDb(v), state: evalIndicator(indicators.LufsS, v), cfg: indicators.LufsS, value: v }; },
                getHeld: function () { var v = monoDb(live.lufsSHeld, mono); return { text: fmtDb(v), state: evalIndicator(indicators.LufsS, v), warn: isHeldWarn(v) }; }
            }
        })] });
    }

    // ---- LRA (no indicator) ----
    if (config.showLra) {
        col({ label: "LRA", width: colHalfW, stacks: [buildStack({
            heldMode: "dash",
            curValue: fmtLra(live.dispLraValue), curColorState: "normal",
            lra: { lo: monoDb(live.dispLraLo, mono), hi: monoDb(live.dispLraHi, mono) }, yScale: yScale,
            updaters: updaters,
            dynamic: {
                getLoHi: function () { return { lo: monoDb(live.dispLraLo, mono), hi: monoDb(live.dispLraHi, mono) }; },
                getCur: function () { return { text: fmtLra(live.dispLraValue), state: "normal" }; }
            }
        })] });
    }

    // ---- LUFS-I ----
    if (config.showLufsI) {
        var lufsICur0 = monoDb(live.dispLufsI, mono);
        var stII = evalIndicator(indicators.LufsI, lufsICur0);
        col({ label: "LUFS-I", indKey: "LufsI", width: colW, stacks: [buildStack({
            heldMode: "dash",
            curValue: fmtDb(lufsICur0), curColorState: stII,
            family: "family-lufs", fillFrac: heightFrac(lufsICur0, yScale), barColorState: stII,
            cfg: indicators.LufsI, yScale: yScale, indicatorKey: "LufsI", onDrag: onChange,
            updaters: updaters,
            dynamic: {
                getFillFrac: function () { return heightFrac(monoDb(live.dispLufsI, mono), yScale); },
                getCur: function () { var v = monoDb(live.dispLufsI, mono); return { text: fmtDb(v), state: evalIndicator(indicators.LufsI, v), cfg: indicators.LufsI, value: v }; }
            }
        })] });
    }
}

/* ======================================================================
   SHARED SCALE
   ====================================================================== */
function renderScale(trackEl, yScale, marks) {
    trackEl.innerHTML = "";
    (marks || GRID_MARKS).forEach(function (db) {
        var t = el("div", "scale-tick");
        t.style.top = ((1 - heightFrac(db, yScale)) * 100) + "%";
        t.dataset.db = db; t.textContent = db === 0 ? "0" : String(db);
        trackEl.appendChild(t);
    });
}

/* v9: ticks are positioned as a % of a scale-track whose rect is copied from the
   real first .bar-track in its scope (alignScale), so the dB->position mapping is
   the bar fill's own (heightFrac, % of bar height) and cannot drift with header /
   padding / label heights. compact=true also culls ticks closer than 11px
   (priority order) so small boxes stay legible. Re-run on every ResizeObserver hit. */
var COMPACT_PRIORITY = [0, -60, -12, -24, -48, -6, -36, -18, -3, -30, -42];
function alignScale(scaleCol, scopeEl, compact) {
    var tr = scaleCol.querySelector(".scale-track"), bar = scopeEl.querySelector(".bar-track");
    if (!tr || !bar) return;
    var cr = scaleCol.getBoundingClientRect(), br = bar.getBoundingClientRect();
    if (br.height < 1) return;
    tr.style.position = "absolute"; tr.style.left = "0"; tr.style.right = "0"; tr.style.margin = "0";
    tr.style.top = (br.top - cr.top) / UI_ZOOM + "px";
    tr.style.height = br.height / UI_ZOOM + "px";
    if (compact) {
        var kept = [];
        var ticks = {};
        Array.prototype.forEach.call(tr.children, function (t) { ticks[t.dataset.db] = t; });
        COMPACT_PRIORITY.forEach(function (db) {
            var t = ticks[db]; if (!t) return;
            var y = (1 - heightFrac(db, PROJECT.yScale)) * br.height;
            var ok = kept.every(function (k) { return Math.abs(k - y) >= 11; });
            t.classList.toggle("cull", !ok);
            if (ok) kept.push(y);
        });
    }
}
var scaleRO = window.ResizeObserver ? new ResizeObserver(function () { alignAllScales(); }) : null;
function alignAllScales() {
    var o = document.getElementById("ownScaleCol");
    if (o && currentView === "own") alignScale(o, document.getElementById("ownCluster"), false);
    if (currentView === "combined") {
        Array.prototype.forEach.call(document.querySelectorAll(".track-box"), function (b) {
            var c = b.querySelector(".scale-col"); if (c) alignScale(c, b, true);
        });
    }
}
function observeScales(scopes) {
    if (!scaleRO) return;
    scaleRO.disconnect();
    scopes.forEach(function (s) { var bt = s.querySelector(".bar-track"); if (bt) scaleRO.observe(bt); });
}

/* ======================================================================
   OWN VIEW RENDER (reads whichever track viewingTrackId currently points at)
   ====================================================================== */
function renderOwnView() {
    var t = currentViewingTrack();
    if (!t) return;
    renderScale(document.getElementById("ownScaleTrack"), PROJECT.yScale);
    frameUpdaters = [];
    renderMeterCluster(
        document.getElementById("ownCluster"), t.id,
        effConfigFor(t), effIndicatorsFor(t), t.live, t.mono,
        34, 18, PROJECT.yScale, renderOwnView, frameUpdaters
    );
    observeScales([document.getElementById("ownCluster")]);
    alignAllScales();
}

/* ======================================================================
   GROUPS — filter bar, popover, manager
   ====================================================================== */
function groupNameFor(t) {
    if (!t.group) return "—";
    var g = PROJECT.groups.filter(function (x) { return x.id === t.group; })[0];
    return g ? g.name : "—";
}

function renderGroupsBar() {
    var bar = document.getElementById("groupsBar");
    bar.innerHTML = "";
    var lbl = el("div", "gb-label"); lbl.textContent = "Groups"; bar.appendChild(lbl);

    PROJECT.groups.forEach(function (g) {
        var chip = document.createElement("button");
        chip.className = "group-filter-chip" + (PROJECT.groupFilter.has(g.id) ? " active" : "");
        chip.textContent = g.name;
        chip.addEventListener("click", function () {
            if (PROJECT.groupFilter.has(g.id)) PROJECT.groupFilter.delete(g.id); else PROJECT.groupFilter.add(g.id);
            logParam("groupFilter", Array.from(PROJECT.groupFilter).join(","));
            pushSharedViewState();
            renderCombinedView();
        });
        bar.appendChild(chip);
    });

    var noGroupChip = document.createElement("button");
    noGroupChip.className = "group-filter-chip" + (PROJECT.groupFilter.has("no-group") ? " active" : "");
    noGroupChip.textContent = "No group";
    noGroupChip.addEventListener("click", function () {
        if (PROJECT.groupFilter.has("no-group")) PROJECT.groupFilter.delete("no-group"); else PROJECT.groupFilter.add("no-group");
        logParam("groupFilter", Array.from(PROJECT.groupFilter).join(","));
        pushSharedViewState();
        renderCombinedView();
    });
    bar.appendChild(noGroupChip);

    var manageBtn = document.createElement("button");
    manageBtn.className = "manage-link";
    manageBtn.textContent = "Manage groups…";
    manageBtn.addEventListener("click", function () { openGroupsManager(); });
    bar.appendChild(manageBtn);
}

function closeGroupPopover() {
    var existing = document.querySelector(".group-popover");
    if (existing) existing.remove();
    document.removeEventListener("click", closeGroupPopoverOnOutsideClick, true);
}
function closeGroupPopoverOnOutsideClick(e) {
    var pop = document.querySelector(".group-popover");
    if (pop && !pop.contains(e.target)) closeGroupPopover();
}
function openGroupPopover(anchorEl, t) {
    closeGroupPopover();
    var pop = el("div", "group-popover");

    PROJECT.groups.forEach(function (g) {
        var b = document.createElement("button");
        b.textContent = g.name;
        if (t.group === g.id) b.classList.add("active");
        b.addEventListener("click", function (e) {
            e.stopPropagation();
            t.group = g.id;
            logParam("trackGroup(" + t.name + ")", g.id);
            pushSharedViewState();
            closeGroupPopover();
            renderCombinedView();
        });
        pop.appendChild(b);
    });

    var bNo = document.createElement("button");
    bNo.textContent = "No group";
    if (!t.group) bNo.classList.add("active");
    bNo.addEventListener("click", function (e) {
        e.stopPropagation();
        t.group = null;
        logParam("trackGroup(" + t.name + ")", "none");
        pushSharedViewState();
        closeGroupPopover();
        renderCombinedView();
    });
    pop.appendChild(bNo);

    var manageBtn = document.createElement("button");
    manageBtn.className = "gp-manage";
    manageBtn.textContent = "Manage groups…";
    manageBtn.addEventListener("click", function (e) {
        e.stopPropagation();
        closeGroupPopover();
        openGroupsManager();
    });
    pop.appendChild(manageBtn);

    var box = anchorEl.closest(".track-box");
    box.appendChild(pop);
    pop.style.top = (anchorEl.offsetTop + anchorEl.offsetHeight + 2) + "px";
    pop.style.left = anchorEl.offsetLeft + "px";

    setTimeout(function () { document.addEventListener("click", closeGroupPopoverOnOutsideClick, true); }, 0);
}

var GROUP_ID_COUNTER = 1;

function buildGroupsManagerBody() {
    var body = document.getElementById("gmBody");
    body.innerHTML = "";

    if (PROJECT.groups.length === 0) {
        var empty = el("div", "gm-empty"); empty.textContent = "No groups yet. Add one below.";
        body.appendChild(empty);
    }

    PROJECT.groups.forEach(function (g) {
        var row = el("div", "gm-row");
        var input = document.createElement("input");
        input.type = "text"; input.value = g.name;
        input.addEventListener("keydown", function (e) {
            if (e.key === "Enter") { e.preventDefault(); input.blur(); }
            else if (e.key === "Escape") { e.preventDefault(); input.value = g.name; input.blur(); }
        });
        input.addEventListener("change", function () {
            g.name = input.value.trim() || g.name;
            input.value = g.name;
            logParam("groupRename", g.id + "->" + g.name);
            pushSharedViewState();
            renderGroupsBar();
            renderCombinedView();
        });
        var count = TRACKS.filter(function (t) { return t.group === g.id; }).length;
        var countEl = el("div", "gm-count"); countEl.textContent = count + " track" + (count === 1 ? "" : "s");
        var del = document.createElement("button"); del.className = "gm-del"; del.appendChild(icon("close", "sm"));
        del.title = "Delete group";
        del.addEventListener("click", function () {
            TRACKS.forEach(function (t) { if (t.group === g.id) t.group = null; });
            PROJECT.groups = PROJECT.groups.filter(function (x) { return x.id !== g.id; });
            PROJECT.groupFilter.delete(g.id);
            logParam("groupDelete", g.id);
            pushSharedViewState();
            buildGroupsManagerBody();
            renderGroupsBar();
            renderCombinedView();
        });
        row.appendChild(input); row.appendChild(countEl); row.appendChild(del);
        body.appendChild(row);
    });

    var addRow = el("div", "gm-add-row");
    var addInput = document.createElement("input"); addInput.type = "text"; addInput.placeholder = "New group name";
    var addBtn = document.createElement("button"); addBtn.textContent = "Add";
    function doAdd() {
        var nm = addInput.value.trim();
        if (!nm) return;
        // Unique across sessions and instances (ids are persisted in the
        // project's shared view state): a bare counter would restart at 1 in
        // every editor and collide with saved groups.
        var id = "grp_" + Date.now().toString(36) + "_" + (GROUP_ID_COUNTER++);
        PROJECT.groups.push({ id: id, name: nm });
        PROJECT.groupFilter.add(id);
        addInput.value = "";
        logParam("groupCreate", id + ":" + nm);
        pushSharedViewState();
        buildGroupsManagerBody();
        renderGroupsBar();
        renderCombinedView();
    }
    addBtn.addEventListener("click", doAdd);
    addInput.addEventListener("keydown", function (e) {
        if (e.key === "Enter") doAdd();
        else if (e.key === "Escape") { e.preventDefault(); addInput.value = ""; addInput.blur(); }
    });
    addRow.appendChild(addInput); addRow.appendChild(addBtn);
    body.appendChild(addRow);
}

function openGroupsManager() {
    buildGroupsManagerBody();
    document.getElementById("groupsManager").classList.add("open");
}

/* ======================================================================
   ICON GLYPH BUILDERS — shared by the top bar (Linked/Mono quick toggles)
   and every combined-view box's bottom-right icon cluster.
   ====================================================================== */
function buildMonoStereoGlyph(mono) {
    return icon(mono ? "mono" : "stereo", "wide");
}
function buildLinkGlyph(linked) {
    return icon(linked ? "linked" : "unlinked", "wide");
}

/* ======================================================================
   COMBINED VIEW RENDER
   Box header keeps only name/tags/group-chip/reset/hide; THIS/OWN tags are
   joined by a VIEWING tag (host vs. viewed-track are distinct concepts); a
   bottom-right icon cluster carries Mono/Stereo, Link/Unlink and the
   per-track gear (unlinked only).
   ====================================================================== */
function renderCombinedView() {
    renderGroupsBar();
    var row = document.getElementById("trackRow");
    row.innerHTML = "";
    frameUpdaters = [];

    var visible = TRACKS.filter(function (t) {
        var gid = t.group || "no-group";
        return PROJECT.groupFilter.has(gid);
    });

    visible.forEach(function (t) {
        var box = el("div", "track-box" + (t.id === hostTrackId ? " this-track" : ""));
        if (PROJECT.hiddenTracks[t.id]) box.classList.add("hidden-box");
        if (PROJECT.soloTrack && PROJECT.soloTrack !== t.id) box.classList.add("dimmed");
        box.dataset.track = t.id;

        var header = el("div", "box-header");
        var name = el("div", "name");
        name.textContent = t.name;
        if (t.id === hostTrackId) {
            var tag = el("span", "this-tag"); tag.title = "The track this plugin instance is inserted on";
            tag.textContent = "THIS"; name.appendChild(tag);
        } else if (t.id === viewingTrackId) {
            var vtag = el("span", "viewing-tag"); vtag.title = "Currently shown in THIS TRACK (a different instance's track)";
            vtag.textContent = "VIEWING"; name.appendChild(vtag);
        }
        if (!t.linked) { var otag = el("span", "own-tag"); otag.textContent = "OWN"; name.appendChild(otag); }
        header.appendChild(name);

        var groupChip = document.createElement("button");
        groupChip.className = "group-chip";
        groupChip.textContent = groupNameFor(t);
        groupChip.title = "Change group";
        groupChip.addEventListener("click", function (e) {
            e.stopPropagation();
            openGroupPopover(groupChip, t);
        });
        header.appendChild(groupChip);

        var btnGroup = el("div", "btn-group");

        var resetBtn = el("button", "icon-btn no-solo");
        resetBtn.appendChild(icon("reset", "sm"));
        resetBtn.title = "Reset this track";
        resetBtn.addEventListener("click", function (e) {
            e.stopPropagation();
            resetTrack(t);
            renderCombinedView();
        });
        btnGroup.appendChild(resetBtn);

        var hideBtn = el("button", "icon-btn no-solo");
        hideBtn.appendChild(icon("close", "sm"));
        hideBtn.title = "Hide";
        hideBtn.addEventListener("click", function (e) {
            e.stopPropagation();
            PROJECT.hiddenTracks[t.id] = true;
            logParam("hide(" + t.name + ")", true);
            pushSharedViewState();
            renderCombinedView();
        });
        btnGroup.appendChild(hideBtn);

        header.appendChild(btnGroup);
        box.appendChild(header);

        var body = el("div", "box-body");
        var sCol = el("div", "scale-col"); var sTr = el("div", "scale-track");
        sCol.appendChild(sTr); body.appendChild(sCol);
        renderScale(sTr, PROJECT.yScale, COMPACT_PRIORITY);
        var clusterHost = el("div", "box-cluster"); clusterHost.style.cssText = "display:flex;flex-direction:row;gap:inherit;flex:1 1 auto;min-height:0";
        renderMeterCluster(clusterHost, t.id, effConfigFor(t), effIndicatorsFor(t), t.live, t.mono, 26, 13, PROJECT.yScale, renderCombinedView, frameUpdaters);
        body.appendChild(clusterHost);
        box.appendChild(body);

        // ---- bottom-right icon cluster ----
        var cluster = el("div", "box-icon-cluster no-solo");

        var monoBtn = document.createElement("button");
        monoBtn.className = "mono-icon-btn no-solo";
        monoBtn.title = t.mono ? "Mono" : "Stereo";
        monoBtn.appendChild(buildMonoStereoGlyph(t.mono));
        monoBtn.addEventListener("click", function (e) {
            e.stopPropagation();
            toggleTrackMono(t);
            if (t.id === viewingTrackId) syncTopBarToViewingTrack();
            renderCombinedView();
        });
        cluster.appendChild(monoBtn);

        var linkBtn = document.createElement("button");
        linkBtn.className = "link-icon-btn no-solo";
        linkBtn.title = t.linked ? "Linked — uses global settings" : "Unlinked — own settings";
        linkBtn.appendChild(buildLinkGlyph(t.linked));
        linkBtn.addEventListener("click", function (e) {
            e.stopPropagation();
            var wasLinked = t.linked;
            toggleTrackLinked(t);
            if (t.id === viewingTrackId) syncTopBarToViewingTrack();
            if (wasLinked === false && t.linked === true && gearPanel.classList.contains("open") && panelMode === "track" && gearScopeTrackId === t.id) {
                gearPanel.classList.remove("open"); // relinked -> its own-config panel is no longer meaningful
            }
            renderCombinedView();
        });
        cluster.appendChild(linkBtn);

        if (!t.linked) {
            var gearBtn = document.createElement("button");
            gearBtn.className = "icon-btn no-solo";
            gearBtn.appendChild(icon("pencil", "sm"));
            gearBtn.title = "Settings — " + t.name;
            gearBtn.addEventListener("click", function (e) {
                e.stopPropagation();
                openTrackGearPanel(t.id);
            });
            cluster.appendChild(gearBtn);
        }

        box.appendChild(cluster);

        box.addEventListener("click", function (e) {
            if (e.target.closest(".no-solo")) return;
            PROJECT.soloTrack = (PROJECT.soloTrack === t.id) ? null : t.id;
            logParam("soloTrack", PROJECT.soloTrack || "none");
            pushSharedViewState();
            renderCombinedView();
        });

        row.appendChild(box);
    });
    observeScales(Array.prototype.slice.call(row.children));
    alignAllScales();
}

function resetLive(live) {
    live.peakLHeld = -72; live.peakRHeld = -72; live.tpLHeld = -72; live.tpRHeld = -72;
    live.peakLClips = 0; live.peakRClips = 0; live.tpLClips = 0; live.tpRClips = 0;
    live.rmsMHeld = -72; live.lufsMHeld = -72; live.lufsSHeld = -72;
    live.rmsI = -72; live.dispRmsI = -72;
    live.lufsI = -72; live.dispLufsI = -72;
    live.lraLo = -72; live.lraHi = -72; live.dispLraLo = -72; live.dispLraHi = -72; live.dispLraValue = 0;
}
// "reset" action (parameter-spec.md "Actions"): clears peaks, held maxima,
// clips, windows, histograms, integrated and LRA. The authoritative reset
// happens in DSP; the local resetLive() call above is optimistic UI
// feedback until the next meterUpdate confirms it.
function resetTrack(t) {
    resetLive(t.live);
    callNative("resetTrackAll", [t.id]); // this instance's own track locally, any other track of the project through its instance
    logParam("reset(" + t.name + ")", "reset");
}

/* ======================================================================
   GEAR PANEL
   ====================================================================== */
var MEASUREMENT_LABELS = { TruePeak: "True Peak", Peak: "Peak", RmsM: "RMS-M", RmsI: "RMS-I", LufsM: "LUFS-M", LufsS: "LUFS-S", LufsI: "LUFS-I" };

function heading(text) { var h = el("div", "g-heading"); h.textContent = text; return h; }

function segmented(options, getVal, setVal) {
    var wrap = el("div", "segmented");
    options.forEach(function (opt) {
        var b = document.createElement("button");
        b.textContent = opt[1];
        if (getVal() === opt[0]) b.classList.add("active");
        b.addEventListener("click", function () {
            setVal(opt[0]);
            wrap.querySelectorAll("button").forEach(function (x) { x.classList.remove("active"); });
            b.classList.add("active");
        });
        wrap.appendChild(b);
    });
    return wrap;
}

function toggleRow(label, getVal, setVal) {
    var row = el("div", "toggle-row");
    var l = document.createElement("div"); l.textContent = label;
    var sw = el("div", "switch" + (getVal() ? " on" : ""));
    sw.appendChild(el("div", "knob"));
    sw.addEventListener("click", function () {
        var nv = !getVal();
        setVal(nv);
        sw.classList.toggle("on", nv);
    });
    row.appendChild(l); row.appendChild(sw);
    return row;
}

function buildMiniSegmented(options, cfg, key, lastKey, onChange) {
    var wrap = el("div", "mini-segmented" + (cfg[key] === "off" ? " dim" : ""));
    options.forEach(function (opt) {
        var b = document.createElement("button");
        b.textContent = opt[1];
        if (cfg[key] === opt[0]) b.classList.add("active");
        b.addEventListener("click", function () {
            cfg[key] = opt[0]; cfg[lastKey] = opt[0];
            logParam(key, opt[0]);
            onChange();
        });
        wrap.appendChild(b);
    });
    return wrap;
}

function buildMeasurementRow(label, isOn, onToggle, trailingEl, indEl) {
    var row = el("div", "measurement-row");
    var l = el("div", "row-label"); l.textContent = label; row.appendChild(l);
    var sw = el("div", "switch" + (isOn ? " on" : ""));
    sw.appendChild(el("div", "knob"));
    sw.addEventListener("click", onToggle);
    row.appendChild(sw);
    var trailing = el("div", "row-trailing");
    if (trailingEl) trailing.appendChild(trailingEl);
    row.appendChild(trailing);
    if (indEl) row.appendChild(indEl);
    return row;
}

/* Level indicators live INSIDE their measurement's row: an enabled
   measurement with no indicator shows "+ IND" right after its row's last
   control; it adds a Threshold indicator, whose editor under the row shows
   the mode (Threshold/Range), its fields and a remove (×). A disabled measurement
   shows nothing, but keeps its indicator parameters, so turning it back on
   brings the indicator back as it was. LRA has no indicator. */


// The indicator a measurement row drives. The Peak row's TruePeak and Peak
// indicators are separate parameter sets; the row shows the one matching
// its active mode and keeps the other.
function rowIndicatorKey(rowKey, cfg) {
    if (rowKey === "Peak") return (cfg.peakMode === "tp" || cfg.peakMode === "tpclips") ? "TruePeak" : "Peak";
    return rowKey === "Lra" ? null : rowKey;
}

// "+ IND" adds a Threshold indicator straight away; the editor that replaces
// it offers Range and the remove (×), so no separate choice step is needed.
function indicatorAddButton(m, ind, onChange) {
    var b = el("button", "ind-add");
    b.textContent = "+ IND";
    b.title = "Add a level indicator";
    b.addEventListener("click", function () {
        ind[m].mode = "threshold"; logParam("ind" + m + "Mode", "threshold");
        onChange();
    });
    return b;
}

function indicatorEditor(m, ind, onChange) {
    var cfg = function () { return ind[m]; };
    var wrap = el("div", "ind-sub");
    var line = el("div", "ind-sub-line");
    var lbl = el("div", "ind-sub-label"); lbl.textContent = "Indicator";
    line.appendChild(lbl);
    var seg = el("div", "mini-segmented");
    [["threshold", "Threshold"], ["range", "Range"]].forEach(function (opt) {
        var b = document.createElement("button");
        b.textContent = opt[1];
        if (cfg().mode === opt[0]) b.classList.add("active");
        b.addEventListener("click", function () {
            if (cfg().mode === opt[0]) return;
            cfg().mode = opt[0]; logParam("ind" + m + "Mode", opt[0]);
            onChange();
        });
        seg.appendChild(b);
    });
    line.appendChild(seg);
    var remove = el("button", "ind-remove");
    remove.appendChild(icon("close", "sm")); remove.title = "Remove indicator";
    remove.addEventListener("click", function () {
        cfg().mode = "off"; logParam("ind" + m + "Mode", "off");
        onChange();
    });
    line.appendChild(remove);
    wrap.appendChild(line);

    var fields = el("div", "ind-fields");
    if (cfg().mode === "threshold") {
        fields.appendChild(numField("threshold", "Threshold", cfg, "threshold", m, onChange));
    } else {
        fields.appendChild(numField("min", "Min", cfg, "min", m, onChange));
        fields.appendChild(numField("max", "Max", cfg, "max", m, onChange));
    }
    wrap.appendChild(fields);
    return wrap;
}

function numField(key, label, cfg, prop, m, onChange) {
    var f = el("div", "field");
    f.dataset.f = key;
    var l = document.createElement("label"); l.textContent = label;
    var i = document.createElement("input");
    // Text, not number: ArrowUp/ArrowDown step the shown value by 0.1 dB
    // (held keys auto-repeat), and only Enter or leaving the field applies
    // it. Escape restores the applied value.
    i.type = "text"; i.inputMode = "decimal"; i.spellcheck = false;
    i.value = cfg()[prop].toFixed(1);
    function limit(v) {
        var c = cfg();
        v = clamp(v, -72, 0);
        if (prop === "min" && v > c.max) v = c.max;
        if (prop === "max" && v < c.min) v = c.min;
        return Math.round(v * 10) / 10;
    }
    function commit() {
        var parsed = parseFloat(i.value.replace(",", "."));
        var c = cfg();
        var v = isNaN(parsed) ? c[prop] : limit(parsed);
        i.value = v.toFixed(1);
        if (v === c[prop]) return;
        c[prop] = v;
        logParam("ind" + m + (prop === "threshold" ? "Threshold" : (prop === "max" ? "RangeMax" : "RangeMin")), v);
        onChange();
    }
    i.addEventListener("keydown", function (e) {
        if (e.key === "ArrowUp" || e.key === "ArrowDown") {
            e.preventDefault();
            var cur = parseFloat(i.value.replace(",", "."));
            if (isNaN(cur)) cur = cfg()[prop];
            i.value = limit(cur + (e.key === "ArrowUp" ? 0.1 : -0.1)).toFixed(1);
        } else if (e.key === "Enter") {
            e.preventDefault(); i.blur();
        } else if (e.key === "Escape") {
            e.preventDefault(); i.value = cfg()[prop].toFixed(1); i.blur();
        }
    });
    i.addEventListener("blur", commit);
    f.appendChild(l); f.appendChild(i);
    return f;
}

/**
 * Builds the shared "measurement selection + level indicators" block,
 * reused for the Global section AND a track's own-configuration panel.
 */
function buildMeasurementBlock(cfg, ind, onChange, scopeText) {
    var frag = document.createDocumentFragment();
    var scope = el("div", "g-scope"); scope.textContent = scopeText; frag.appendChild(scope);

    // A measurement row plus, under it, its indicator chooser/editor.
    function addRow(rowKey, label, isOn, onToggle, trailingEl) {
        var m = isOn ? rowIndicatorKey(rowKey, cfg) : null;
        var active = m && ind[m].mode !== "off";
        var addBtn = (m && !active) ? indicatorAddButton(m, ind, onChange) : null;
        var item = el("div", "m-item");
        item.appendChild(buildMeasurementRow(label, isOn, onToggle, trailingEl, addBtn));
        if (active) item.appendChild(indicatorEditor(m, ind, onChange));
        frag.appendChild(item);
    }

    addRow("Peak", "Peak", cfg.peakMode !== "off", function () {
        if (cfg.peakMode === "off") { cfg.peakMode = cfg._lastPeakMode || "peakclips"; }
        else { cfg._lastPeakMode = cfg.peakMode; cfg.peakMode = "off"; }
        logParam("peakMode", cfg.peakMode);
        onChange();
    }, buildMiniSegmented([["tp", "True Peak"], ["tpclips", "TP clips"], ["peak", "Peak"], ["peakclips", "Peak clips"]], cfg, "peakMode", "_lastPeakMode", onChange));

    function addToggleRow(r) {
        addRow(r[2], r[1], cfg[r[0]], function () {
            cfg[r[0]] = !cfg[r[0]];
            logParam(r[0], cfg[r[0]]);
            onChange();
        }, null);
    }

    // Same order as the meters: Peak, RMS-M, RMS-I, LUFS-M, LUFS-S, LRA, LUFS-I.
    [["showRmsM", "RMS-M", "RmsM"], ["showRmsI", "RMS-I", "RmsI"]].forEach(addToggleRow);

    addRow("LufsM", "LUFS-M", cfg.lufsMMode !== "off", function () {
        if (cfg.lufsMMode === "off") { cfg.lufsMMode = cfg._lastLufsMMode || "hist"; }
        else { cfg._lastLufsMMode = cfg.lufsMMode; cfg.lufsMMode = "off"; }
        logParam("lufsMMode", cfg.lufsMMode);
        onChange();
    }, buildMiniSegmented([["on", "Plain"], ["hist", "+Histogram"]], cfg, "lufsMMode", "_lastLufsMMode", onChange));

    [["showLufsS", "LUFS-S", "LufsS"], ["showLra", "LRA", "Lra"], ["showLufsI", "LUFS-I", "LufsI"]].forEach(addToggleRow);

    return frag;
}

function refreshAllAndGear() {
    buildGearBody();
    refreshAll();
}

/**
 * The gear panel has exactly two mutually exclusive modes:
 *  - "global": Global (linked tracks) + Project. Always reachable from the
 *    top-bar gear, in BOTH views. Never shows any one track's own config.
 *  - "track": ONE unlinked track's own configuration only (no Global
 *    section, no Project section). Reachable only from that track's own
 *    gear icon. Titled with the track's name and visually tinted so it
 *    cannot be mistaken for Global.
 */
function buildGearBody() {
    var body = document.getElementById("gearBody");
    var header = document.getElementById("gearHeader");
    var titleEl = document.getElementById("gearTitle");
    var banner = document.getElementById("linkedBanner");
    body.innerHTML = "";

    if (panelMode === "track") {
        var t = trackById(gearScopeTrackId);
        if (!t) return;
        ensureOwnCopy(t);
        header.classList.add("track-scoped");
        titleEl.innerHTML = "";
        var badge = el("span", "track-scope-badge"); badge.textContent = "TRACK";
        titleEl.appendChild(badge);
        titleEl.appendChild(document.createTextNode(" Settings — " + t.name));
        banner.style.display = "none";

        var sT = el("div", "g-section");
        sT.appendChild(buildMeasurementBlock(
            t.ownConfig, t.ownIndicators, refreshAllAndGear,
            "This track's own configuration — affects only " + t.name + ". The Global section is not shown here."
        ));
        body.appendChild(sT);
        return;
    }

    // ---- panelMode === "global" ----
    header.classList.remove("track-scoped");
    titleEl.textContent = "Settings — Global (linked tracks)";
    banner.style.display = "";
    banner.textContent = "Edits the shared configuration used by every linked track, from wherever it's opened.";

    // Phase 3.3: always edits the SHARED global configuration. While this
    // instance's host track is linked that is hostConfig/hostIndicators (its
    // own relay-backed parameters, which the native side mirrors into the
    // shared store); while it is UNLINKED those parameters are the host's OWN
    // copy, so the panel edits nativeGlobalMirror instead, which writes
    // straight to the shared store (setConfigParam("global", ...)).
    var sG = el("div", "g-section");
    sG.appendChild(heading("Global (linked tracks)"));
    sG.appendChild(buildMeasurementBlock(currentGlobalConfig(), currentGlobalIndicators(), refreshAllAndGear, "Used by every linked track"));
    body.appendChild(sG);

    var sP = el("div", "g-section divider-above");
    sP.appendChild(heading("Project"));
    var scopeP = el("div", "g-scope"); scopeP.textContent = "Applies to every instance in this project"; sP.appendChild(scopeP);
    sP.appendChild(toggleRow("Reset on playback start", function () { return PROJECT.resetOnPlay; }, function (v) {
        PROJECT.resetOnPlay = v; logParam("resetOnPlay", v);
    }));

    // Indicator colouring style, see INDICATOR_STYLE.
    var styleRow = el("div", "toggle-row");
    var styleLabel = document.createElement("div"); styleLabel.textContent = "Indicator style";
    styleRow.appendChild(styleLabel);
    styleRow.appendChild(segmented([["zone", "Zone"], ["bar", "Whole bar"]],
        function () { return INDICATOR_STYLE; }, function (v) { setIndicatorStyle(v); logParam("indicatorStyle", v); }));
    sP.appendChild(styleRow);

    var sliderRow = el("div", "slider-row");
    var sLabel = el("div"); sLabel.style.fontSize = "10px"; sLabel.style.width = "50px"; sLabel.textContent = "Y-scale";
    var range = document.createElement("input");
    range.type = "range"; range.min = "0.5"; range.max = "4"; range.step = "0.1"; range.value = String(PROJECT.yScale);
    var sVal = el("div", "val"); sVal.textContent = PROJECT.yScale.toFixed(1);
    range.addEventListener("input", function () {
        PROJECT.yScale = parseFloat(range.value);
        sVal.textContent = PROJECT.yScale.toFixed(1);
        logParam("yScale", PROJECT.yScale);
        refreshAll();
    });
    sliderRow.appendChild(sLabel); sliderRow.appendChild(range); sliderRow.appendChild(sVal);
    sP.appendChild(sliderRow);

    var stepRow = el("div", "slider-row");
    var tLabel = el("div"); tLabel.style.fontSize = "10px"; tLabel.style.width = "50px"; tLabel.textContent = "Text size";
    var stepper = el("div", "stepper");
    var minus = document.createElement("button"); minus.appendChild(icon("minus", "sm"));
    var tVal = el("div", "val"); tVal.textContent = String(PROJECT.textSize);
    var plus = document.createElement("button"); plus.appendChild(icon("plus", "sm"));
    minus.addEventListener("click", function () { setTextSize(PROJECT.textSize - 1); tVal.textContent = String(PROJECT.textSize); });
    plus.addEventListener("click", function () { setTextSize(PROJECT.textSize + 1); tVal.textContent = String(PROJECT.textSize); });
    stepper.appendChild(minus); stepper.appendChild(tVal); stepper.appendChild(plus);
    stepRow.appendChild(tLabel); stepRow.appendChild(stepper);
    sP.appendChild(stepRow);

    body.appendChild(sP);
}

function openGlobalGearPanel() {
    panelMode = "global";
    buildGearBody();
    gearPanel.classList.add("open");
}
function openTrackGearPanel(trackId) {
    panelMode = "track";
    gearScopeTrackId = trackId;
    buildGearBody();
    gearPanel.classList.add("open");
}

function setTextSize(v) {
    PROJECT.textSize = clamp(v, -2, 8);
    logParam("textSize", PROJECT.textSize);
}

function refreshAll() {
    if (currentView === "own") renderOwnView(); else renderCombinedView();
}

/* ======================================================================
   TOP BAR WIRING
   ====================================================================== */
var viewNav = document.getElementById("viewNav");
var btnPrevTrack = document.getElementById("btnPrevTrack");
var btnViewToggle = document.getElementById("btnViewToggle");
var btnNextTrack = document.getElementById("btnNextTrack");
var btnBackToHost = document.getElementById("btnBackToHost");
var btnReset = document.getElementById("btnReset");
var btnResetAll = document.getElementById("btnResetAll");
var btnShowAll = document.getElementById("btnShowAll");
var btnLinked = document.getElementById("btnLinked");
var btnMono = document.getElementById("btnMono");
var btnTrackGear = document.getElementById("btnTrackGear");
var btnGear = document.getElementById("btnGear");
var gearPanel = document.getElementById("gearPanel");
var gearClose = document.getElementById("gearClose");
var groupsManagerEl = document.getElementById("groupsManager");
var gmClose = document.getElementById("gmClose");
var viewOwn = document.getElementById("viewOwn");
var viewCombined = document.getElementById("viewCombined");

/* View button label: the viewed track's name in THIS TRACK, or the literal
   "ALL TRACKS" in the combined view. */
function updateViewButtonLabel() {
    var t = currentViewingTrack();
    btnViewToggle.textContent = currentView === "own" ? (t ? t.name : "…") : "ALL TRACKS";
}

/* Instance-navigation UI state: prev/next/back are only meaningful in THIS
   TRACK; the back-affordance and the view-button's neutral dashed marker
   only appear while viewing a track other than the host. */
function updateInstanceNavUI() {
    var isOwn = currentView === "own";
    var viewingElsewhere = viewingTrackId !== hostTrackId;
    btnPrevTrack.style.display = isOwn ? "" : "none";
    btnNextTrack.style.display = isOwn ? "" : "none";
    btnBackToHost.style.display = (isOwn && viewingElsewhere) ? "" : "none";
    btnViewToggle.classList.toggle("viewing-other", isOwn && viewingElsewhere);
}

function syncTopBarToViewingTrack() {
    var t = currentViewingTrack();
    if (!t) return;
    btnLinked.innerHTML = "";
    btnLinked.appendChild(buildLinkGlyph(t.linked));
    btnLinked.title = t.linked ? "Linked — uses global settings" : "Unlinked — own settings";
    btnMono.innerHTML = "";
    btnMono.appendChild(buildMonoStereoGlyph(t.mono));
    btnMono.title = t.mono ? "Mono" : "Stereo";
    btnTrackGear.style.display = (currentView === "own" && !t.linked) ? "" : "none";
    btnTrackGear.title = "Settings — " + t.name;
    updateViewButtonLabel();
    updateInstanceNavUI();
}

function afterViewingTrackChange() {
    // Navigating (arrows / back-to-host) invalidates an open TRACK-scoped
    // gear panel (it was scoped to whatever was being viewed before); the
    // GLOBAL panel stays valid and open regardless, since it never depends
    // on which track is being viewed.
    if (gearPanel.classList.contains("open") && panelMode === "track") {
        gearPanel.classList.remove("open");
    }
    syncTopBarToViewingTrack();
    logParam("viewingTrack", viewingTrackId);
    if (currentView === "own") renderOwnView(); else renderCombinedView();
}

function stepInstance(direction) {
    if (TRACKS.length === 0) return;
    var idx = TRACKS.findIndex(function (t) { return t.id === viewingTrackId; });
    var nextIdx = (idx + direction + TRACKS.length) % TRACKS.length;
    viewingTrackId = TRACKS[nextIdx].id;
    afterViewingTrackChange();
}
btnPrevTrack.addEventListener("click", function () { stepInstance(-1); });
btnNextTrack.addEventListener("click", function () { stepInstance(1); });
btnBackToHost.addEventListener("click", function () {
    viewingTrackId = hostTrackId;
    afterViewingTrackChange();
});

btnViewToggle.addEventListener("click", function () {
    var enteringCombined = currentView === "own";
    if (enteringCombined) captureSingleTrackSizeIfOwn(); // defensive save before leaving THIS TRACK (also kept live by the ResizeObserver during any manual resize)
    currentView = enteringCombined ? "combined" : "own";
    var isCombined = currentView === "combined";
    viewOwn.style.display = isCombined ? "none" : "flex";
    viewCombined.style.display = isCombined ? "flex" : "none";
    btnReset.style.display = isCombined ? "none" : "";
    btnResetAll.style.display = isCombined ? "" : "none";
    btnShowAll.style.display = isCombined ? "" : "none";
    btnLinked.style.display = isCombined ? "none" : "";
    btnMono.style.display = isCombined ? "none" : "";
    // btnGear (Global Settings) is intentionally NEVER hidden here -- it is
    // available in both views.
    logParam("view", currentView);
    syncTopBarToViewingTrack();
    if (isCombined) {
        renderCombinedView();
        growToAllTracks(); // automatic WIDTH-only grow-on-entry (height carried over from THIS TRACK)
    } else {
        renderOwnView();
        shrinkToSingleTrack(); // automatic shrink-on-exit
    }
});

btnReset.addEventListener("click", function () {
    var t = currentViewingTrack();
    if (!t) return;
    resetTrack(t);
    renderOwnView();
});
btnResetAll.addEventListener("click", function () {
    TRACKS.forEach(function (t) { resetTrack(t); });
    logParam("resetAll", "reset");
    renderCombinedView();
});
btnShowAll.addEventListener("click", function () {
    // v7: track whether SHOW ALL actually changes anything (unhides a
    // group-filter-visible track, and/or clears an active solo) so the
    // width re-fit below only ever runs when it is meaningful -- a genuine
    // no-op click (nothing hidden, nothing soloed) must not resize the
    // window at all, per show_all_refit in v7-ui.yaml.
    var changedVisibility = false;
    TRACKS.forEach(function (t) {
        var gid = t.group || "no-group";
        if (PROJECT.groupFilter.has(gid) && PROJECT.hiddenTracks[t.id]) {
            PROJECT.hiddenTracks[t.id] = false;
            changedVisibility = true;
        }
    });
    if (PROJECT.soloTrack !== null) changedVisibility = true;
    PROJECT.soloTrack = null;
    logParam("showAll", true);
    pushSharedViewState();
    renderCombinedView();
    if (changedVisibility) {
        refitWidthOnShowAll(); // v7: re-fit WIDTH only
    }
});

btnLinked.addEventListener("click", function () {
    var t = currentViewingTrack();
    if (!t) return;
    var wasLinked = t.linked;
    toggleTrackLinked(t);
    syncTopBarToViewingTrack();
    if (wasLinked === false && t.linked === true && gearPanel.classList.contains("open") && panelMode === "track" && gearScopeTrackId === t.id) {
        gearPanel.classList.remove("open");
    }
    renderOwnView();
});

btnMono.addEventListener("click", function () {
    var t = currentViewingTrack();
    if (!t) return;
    toggleTrackMono(t);
    syncTopBarToViewingTrack();
    renderOwnView();
});

btnTrackGear.addEventListener("click", function () { openTrackGearPanel(viewingTrackId); });
btnGear.addEventListener("click", openGlobalGearPanel);
gearClose.addEventListener("click", function () { gearPanel.classList.remove("open"); });
gmClose.addEventListener("click", function () { groupsManagerEl.classList.remove("open"); });

/* ======================================================================
   ANIMATION LOOP — 60fps rAF display dynamics (JSFX peak decay 150ms
   half-life instant-attack, loudness smoothing 75ms half-life ease),
   decoupled from however often onMeterUpdate() actually arrives from C++.
   The RAW values it eases toward come exclusively from onMeterUpdate()
   (see the JUCE PARAMETER BRIDGE section above) -- there is no local
   signal simulation in this file.
   ====================================================================== */
function frameUpdate(live, dt) {
    // Peak values are in dB. The JSFX's 150 ms half-life on linear amplitude
    // is a fall of 20*log10(2) = 6.0206 dB per 150 ms in the dB domain.
    // (Multiplying a negative dB value by a factor < 1 would instead climb
    // toward 0 dB.)
    var peakFallDb = 6.0206 * dt / 0.150;
    live.dispPeakL = Math.max(live.peakL, live.dispPeakL - peakFallDb);
    live.dispPeakR = Math.max(live.peakR, live.dispPeakR - peakFallDb);
    live.dispTpL = Math.max(live.tpL, live.dispTpL - peakFallDb);
    live.dispTpR = Math.max(live.tpR, live.dispTpR - peakFallDb);

    var speed = 1 - Math.pow(0.5, dt / 0.075);
    live.dispRmsM += (live.rmsM - live.dispRmsM) * speed;
    live.dispRmsI += (live.rmsI - live.dispRmsI) * speed;
    live.dispLufsM += (live.lufsM - live.dispLufsM) * speed;
    live.dispLufsS += (live.lufsS - live.dispLufsS) * speed;
    live.dispLufsI += (live.lufsI - live.dispLufsI) * speed;
    live.dispLraLo += (live.lraLo - live.dispLraLo) * speed;
    live.dispLraHi += (live.lraHi - live.dispLraHi) * speed;
    live.dispLraValue = clamp(live.dispLraHi - live.dispLraLo, 0, 20);
}

var lastFrameTime = 0;
function frame(ts) {
    if (lastFrameTime === 0) lastFrameTime = ts;
    var dt = (ts - lastFrameTime) / 1000;
    if (dt > 0.25) dt = 0.25;
    lastFrameTime = ts;

    TRACKS.forEach(function (t) { frameUpdate(t.live, dt); });

    for (var i = 0; i < frameUpdaters.length; i++) frameUpdaters[i]();

    requestAnimationFrame(frame);
}

/* ======================================================================
   BOOT
   ====================================================================== */
/* Builds / reconciles TRACKS from the native track list (REAPER track order,
   this instance's own project only). Existing track objects are kept so their
   live meter state, own-config proxies and any open view survive a refresh.
   `newHostId`, when it differs from the current host id, is this instance's
   Pending -> resolved identity change: the host's track object is carried over
   under the new id. */
function applyTrackList(specs, newHostId) {
    if (newHostId && newHostId !== hostTrackId) {
        var oldHost = trackById(hostTrackId);
        if (oldHost) oldHost.id = newHostId;
        if (viewingTrackId === hostTrackId) viewingTrackId = newHostId;
        if (gearScopeTrackId === hostTrackId) gearScopeTrackId = newHostId;
        hostTrackId = newHostId;
    }

    TRACKS = specs.map(function (spec) {
        var isHost = spec.id === hostTrackId;
        var t = trackById(spec.id);
        if (!t) {
            t = {
                id: spec.id, name: spec.name, group: spec.group || null, order: spec.order || 0,
                linked: isHost ? linkedBinding.get() : spec.linked !== false,
                mono: isHost ? monoModeBinding.get() : !!spec.mono,
                live: newLive(),
                ownConfig: isHost ? hostConfig : null,
                ownIndicators: isHost ? hostIndicators : null,
                remote: null
            };
        } else {
            t.name = spec.name; t.order = spec.order || 0; t.group = spec.group || null;
            if (!isHost) { t.linked = spec.linked !== false; t.mono = !!spec.mono; }
        }
        return t;
    });
    TRACKS.forEach(function (t) { if (t.id !== hostTrackId && !t.linked) ensureOwnCopy(t); });

    // Anything the page was pointing at that no longer exists falls back to host.
    if (!trackById(viewingTrackId)) viewingTrackId = hostTrackId;
    if (!trackById(gearScopeTrackId)) {
        gearScopeTrackId = hostTrackId;
        if (panelMode === "track") gearPanel.classList.remove("open");
    }
}

async function boot() {
    // Phase 3.3: every native call below is real. Event listeners are
    // registered FIRST so nothing that arrives while the awaits below are in
    // flight is lost; getProjectScope is deliberately the LAST native call --
    // it makes the native side re-send every event once, by which time the
    // track list and view state are in place.
    if (window.__JUCE__ && window.__JUCE__.backend && window.__JUCE__.backend.addEventListener) {
        window.__JUCE__.backend.addEventListener("meterUpdate", onMeterUpdate);
        window.__JUCE__.backend.addEventListener("sharedViewStateUpdate", onSharedViewStateUpdate);
        window.__JUCE__.backend.addEventListener("trackConfigUpdate", onTrackConfigUpdate);
        window.__JUCE__.backend.addEventListener("globalConfigUpdate", onGlobalConfigUpdate);
        window.__JUCE__.backend.addEventListener("trackListUpdate", onTrackListUpdate);
        window.__JUCE__.backend.addEventListener("projectScopeUpdate", onProjectScopeUpdate);
    } else {
        console.warn("[JUCE bridge] window.__JUCE__.backend.addEventListener unavailable -- live meter/shared-state updates will not arrive");
    }

    hostTrackId = (await callNative("getHostTrackId", [])) || "host";

    var trackList = await callNative("getTrackList", []);
    if (!trackList || !trackList.length) {
        console.warn("[JUCE bridge] getTrackList unavailable -- falling back to a single-track placeholder");
        trackList = [{ id: hostTrackId, name: "This Track", group: null, order: 0 }];
    }
    applyTrackList(trackList, null);

    viewingTrackId = hostTrackId;
    gearScopeTrackId = hostTrackId;

    var savedSize = await callNative("getEditorStateSize", []); // persisted non-parameter editor size
    if (savedSize && savedSize.width && savedSize.height) {
        savedSingleTrackSize = { width: savedSize.width, height: savedSize.height };
    }

    var initialViewState = await callNative("getSharedViewState", []);
    if (initialViewState) {
        PROJECT.groups = initialViewState.groups || [];
        // The native side already applies the all-visible default; an empty
        // array here is the user's own explicit "nothing visible" and must
        // NOT be replaced by a default.
        PROJECT.groupFilter = new Set(initialViewState.groupFilter || ["no-group"]);
        PROJECT.soloTrack = initialViewState.soloTrack || null;
        PROJECT.hiddenTracks = initialViewState.hiddenTracks || {};
        if (initialViewState.trackGroup) TRACKS.forEach(function (t) { t.group = initialViewState.trackGroup[t.id] || null; });
    }

    linkedBinding.onChange(function () {
        var t = trackById(hostTrackId);
        if (t) t.linked = linkedBinding.get();
        if (viewingTrackId === hostTrackId) syncTopBarToViewingTrack();
        // The Global panel edits hostConfig while linked and the shared-store
        // mirror while unlinked: rebuild it if it is open.
        if (gearPanel.classList.contains("open") && panelMode === "global") buildGearBody();
        refreshAll();
    });
    monoModeBinding.onChange(function () {
        var t = trackById(hostTrackId);
        if (t) t.mono = monoModeBinding.get();
        if (viewingTrackId === hostTrackId) syncTopBarToViewingTrack();
        refreshAll();
    });

    if (window.ResizeObserver) {
        new ResizeObserver(function () { captureSingleTrackSizeIfOwn(); }).observe(pluginFrame);
    }

    syncTopBarToViewingTrack();
    buildGearBody();
    renderOwnView();
    requestAnimationFrame(frame);

    // LAST: project scope (banner) + arms the native one-shot full event re-send.
    var scopeInfo = await callNative("getProjectScope", []);
    applyProjectScope(scopeInfo && scopeInfo.scope);
}
boot();

window.addEventListener("error", function (e) { console.error("UI error:", e.message); });
