// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#include "ReaperHostBridge.h"

namespace meterscope::reaperhost
{
    // Exactly one definition of IReaperHostApplication::iid across the whole
    // link, per JUCE's own examples/Plugins/ReaperEmbeddedViewPluginDemo.h
    // ("These should live in a file which is guaranteed to be compiled only
    // once (i.e. a .cpp file, normally)"). IReaperUIEmbedInterface is declared
    // by the vendored header but never used by this plugin (Phase 2.2 Step 1
    // only needs IReaperHostApplication), so it deliberately has no
    // DEF_CLASS_IID here - its `iid` member is never ODR-used, so leaving it
    // undefined causes no link error.
    DEF_CLASS_IID (IReaperHostApplication)
}

namespace meterscope
{
    void ReaperHostBridge::setIHostApplication (Steinberg::FUnknown* hostContext)
    {
        if (hostContext == nullptr)
            return;

        void* obj = nullptr;

        if (hostContext->queryInterface (reaperhost::IReaperHostApplication::iid, &obj) == Steinberg::kResultOk
            && obj != nullptr)
        {
            reaperHost = Steinberg::IPtr<reaperhost::IReaperHostApplication>::adopt (
                static_cast<reaperhost::IReaperHostApplication*> (obj));
        }
    }
}
