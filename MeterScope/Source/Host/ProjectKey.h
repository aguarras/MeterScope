// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#pragma once

// Phase 2.2 Part B. See plugins/MeterScope/.ideas/architecture.md ->
// "Multi-Instance Shared Store" (SharedMeterRegistry::ProjectKey) and
// "Project Scoping". Deliberately framework-free (no JUCE includes), like
// Source/DSP/*, so it can be reasoned about/unit-tested in isolation.
//
// Field semantics per scope (all three share the same two fields; `reaProject`
// is repurposed per scope - documented explicitly below, since this is a
// deliberate interpretation this task's contracts left implicit):
//
//   - Identified: `reaProject` is the REAPER `ReaProject*` returned by
//     `IReaperHostApplication::getReaperParent(3)`, used ONLY as an opaque
//     partition key (architecture.md: "never dereferenced by MeterScope").
//     Two Identified keys are the same partition iff their `reaProject`
//     pointers are equal.
//
//   - Unidentified: `reaProject` is always nullptr. Every Unidentified
//     instance, in any project or non-REAPER host, pools into this single
//     shared partition (architecture.md: "Unidentified instances are pooled
//     with each other ... but never with Identified instances").
//
//   - Pending: `reaProject` holds THIS INSTANCE'S OWN identity (in practice,
//     the owning AudioProcessor's `this` pointer, cast to `const void*`) -
//     never a real ReaProject*, and never dereferenced (same "opaque key"
//     discipline as the Identified case). This is how "a Pending instance's
//     own editor shows its own THIS-TRACK data... its ALL TRACKS view shows
//     only itself" (architecture.md, Project Scoping -> Fallback) falls out
//     of plain ProjectKey equality with no extra machinery: every Pending
//     instance mints a key that is structurally unique to itself, so
//     `getActiveEntriesSortedByOrder`/`findEntry` called with a Pending key
//     can only ever match that same instance's own Entry - never another
//     Pending instance's Entry, and never an Identified/Unidentified one.
//     See Source/Host/ProjectResolver.cpp for where this is set.
struct ProjectKey
{
    enum class Scope
    {
        Pending,      // Not resolved yet - see field semantics above.
        Identified,   // reaProject == a real (opaque) ReaProject*.
        Unidentified  // reaProject == nullptr; single shared fallback partition.
    };

    Scope scope = Scope::Pending;
    const void* reaProject = nullptr;

    // Two keys denote the same partition iff both fields match. This single
    // definition correctly implements all three scopes' pooling rules given
    // the field semantics documented above - no scope-specific special-casing
    // needed here.
    bool operator== (const ProjectKey& other) const noexcept
    {
        return scope == other.scope && reaProject == other.reaProject;
    }

    bool operator!= (const ProjectKey& other) const noexcept
    {
        return ! (*this == other);
    }
};
