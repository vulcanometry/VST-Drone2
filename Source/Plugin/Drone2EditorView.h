// Drone2EditorView.h - VST3 editor: embeds the shared PanelView in the host's window.
#pragma once

#include "PanelView.h"

#include "public.sdk/source/common/pluginview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

#include <memory>

namespace Drone2Plugin
{
class Drone2Controller;

class Drone2EditorView : public Steinberg::CPluginView, public Steinberg::IPlugViewContentScaleSupport
{
public:
    explicit Drone2EditorView(Drone2Controller* controller);
    ~Drone2EditorView() SMTG_OVERRIDE;

    void Refresh();

    // IPlugView
    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* size) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16 key, Steinberg::int16 keyCode, Steinberg::int16 modifiers) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onKeyUp(Steinberg::char16 key, Steinberg::int16 keyCode, Steinberg::int16 modifiers) SMTG_OVERRIDE;

    // IPlugViewContentScaleSupport
    Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) SMTG_OVERRIDE;

    OBJ_METHODS(Drone2EditorView, CPluginView)
    DEFINE_INTERFACES
        DEF_INTERFACE(IPlugViewContentScaleSupport)
    END_DEFINE_INTERFACES(CPluginView)
    REFCOUNT_METHODS(CPluginView)

private:
    void ApplyScale(float scale);

    Steinberg::IPtr<Drone2Controller> m_controller;
    std::unique_ptr<PanelView> m_panel;
    float m_scale = 1.0f;
    bool  m_hostSetScale = false;
};
} // namespace Drone2Plugin
