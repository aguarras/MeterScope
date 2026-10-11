// Vendored verbatim from JUCE 9.0.2's own
// C:\JUCE\examples\Plugins\extern\reaper_vst3_interfaces.h (checked 2026-09-26,
// byte-identical to the JUCE checkout at C:\JUCE - see
// plugins/MeterScope/.ideas/plan.md, Phase 2.2 "Step 1: Tier B spike (do
// first)", and architecture.md's "REAPER Host Integration" section).
//
// This is REAPER's own VST3 extension interface declaration (Cockos), not a
// JUCE file - JUCE simply ships it under examples/Plugins/extern/ as the
// canonical way to declare IReaperHostApplication for a JUCE VST3 plugin. It
// is included from Source/Host/ReaperHostBridge.h inside an enclosing
// `meterscope::reaperhost` namespace that supplies the `uint32`/`FUnknown`
// declarations it expects to already be in scope - exactly the pattern used
// by JUCE's own examples/Plugins/ReaperEmbeddedViewPluginDemo.h (see that
// file's `namespace reaper { using namespace Steinberg; ... }` wrapper).
//
// Do not edit the interface declarations below - they must stay byte-for-byte
// identical to the vendored source so the IID matches REAPER's own binary.

#ifndef _REAPER_VST3_INTERFACES_H_
#define _REAPER_VST3_INTERFACES_H_

class IReaperHostApplication : public FUnknown // available from IHostApplication in REAPER v5.02+
{
public:
  // Gets a REAPER Extension API function by name, returns NULL is failed
  virtual void* PLUGIN_API getReaperApi(CStringA funcname) = 0;

  virtual void* PLUGIN_API getReaperParent(uint32 w) = 0; // get parent track(=1), take(=2), project(=3), fxdsp(=4), trackchan(=5)

  // Multi-purpose function, returns NULL if unsupported
  virtual void* PLUGIN_API reaperExtended(uint32 call, void *parm1, void *parm2, void *parm3) = 0;

  static const FUID iid;
};

DECLARE_CLASS_IID (IReaperHostApplication, 0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C)

class IReaperUIEmbedInterface : public FUnknown // supported by REAPER v6.24+, queried from plug-in IController
{
  public:
    // note: VST2 uses CanDo "hasCockosEmbeddedUI"==0xbeef0000, then opcode=effVendorSpecific, index=effEditDraw, opt=(float)msg, value=parm2, ptr=parm3
    // see reaper_plugin_fx_embed.h
  virtual Steinberg::TPtrInt embed_message(int msg, Steinberg::TPtrInt parm2, Steinberg::TPtrInt parm3) = 0;

  static const FUID iid;
};

DECLARE_CLASS_IID (IReaperUIEmbedInterface, 0x049bf9e7, 0xbc74ead0, 0xc4101e86, 0x7f725981)
#endif
