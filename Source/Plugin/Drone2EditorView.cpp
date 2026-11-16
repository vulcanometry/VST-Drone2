// Drone2EditorView.cpp - VST3 editor: embeds the shared PanelView in the host's window.
#include "Drone2EditorView.h"
#include "Drone2Controller.h"

#include "pluginterfaces/base/keycodes.h"

using namespace Steinberg;

namespace Drone2Plugin
{
namespace
{
// Translates a VST3 key event into the Windows virtual key the panel uses.
UINT ToVirtualKey(char16 key, int16 keyCode)
{
    if (key >= 'a' && key <= 'z') return static_cast<UINT>(key - 'a' + 'A');
    if (key >= 'A' && key <= 'Z') return static_cast<UINT>(key);
    switch (key)
    {
    case ',': return VK_OEM_COMMA;
    case '.': return VK_OEM_PERIOD;
    case '-': return VK_OEM_MINUS;
    case '=': return VK_OEM_PLUS;
    case ' ': return VK_SPACE;
    }
    switch (keyCode)
    {
    case KEY_SPACE: return VK_SPACE;
    case KEY_LEFT:  return VK_LEFT;
    case KEY_RIGHT: return VK_RIGHT;
    case KEY_BACK:  return VK_BACK;
    }
    return 0;
}

float SystemScale()
{
    HDC screen = GetDC(nullptr);
    const float scale = static_cast<float>(GetDeviceCaps(screen, LOGPIXELSX)) / 96.0f;
    ReleaseDC(nullptr, screen);
    return scale;
}
} // namespace

Drone2EditorView::Drone2EditorView(Drone2Controller* controller) : m_controller(controller)
{
    m_scale = SystemScale();
    const SIZE size = PanelView::PixelSize(m_scale);
    ViewRect r(0, 0, size.cx, size.cy);
    setRect(r);
}

Drone2EditorView::~Drone2EditorView()
{
    if (m_controller)
        m_controller->SetEditor(nullptr);
}

void Drone2EditorView::Refresh()
{
    if (m_panel)
        m_panel->Refresh();
}

tresult PLUGIN_API Drone2EditorView::isPlatformTypeSupported(FIDString type)
{
    return FIDStringsEqual(type, kPlatformTypeHWND) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Drone2EditorView::attached(void* parent, FIDString type)
{
    if (!parent || !FIDStringsEqual(type, kPlatformTypeHWND))
        return kResultFalse;

    // Hosts that do not send a content scale factor: follow the parent window's DPI.
    if (!m_hostSetScale)
        m_scale = static_cast<float>(GetDpiForWindow(static_cast<HWND>(parent))) / 96.0f;

    m_panel = std::make_unique<PanelView>(*m_controller);
    if (!m_panel->Create(static_cast<HWND>(parent), 0, 0, m_scale))
    {
        m_panel.reset();
        return kResultFalse;
    }
    m_controller->SetEditor(this);
    const tresult result = CPluginView::attached(parent, type);
    ApplyScale(m_scale);
    return result;
}

tresult PLUGIN_API Drone2EditorView::removed()
{
    m_controller->SetEditor(nullptr);
    m_panel.reset();
    return CPluginView::removed();
}

tresult PLUGIN_API Drone2EditorView::getSize(ViewRect* size)
{
    if (!size)
        return kInvalidArgument;
    const SIZE s = PanelView::PixelSize(m_scale);
    *size = ViewRect(0, 0, s.cx, s.cy);
    return kResultTrue;
}

tresult PLUGIN_API Drone2EditorView::onKeyDown(char16 key, int16 keyCode, int16)
{
    const UINT vk = ToVirtualKey(key, keyCode);
    return vk && m_panel && m_panel->HandleKey(vk, true) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Drone2EditorView::onKeyUp(char16 key, int16 keyCode, int16)
{
    const UINT vk = ToVirtualKey(key, keyCode);
    return vk && m_panel && m_panel->HandleKey(vk, false) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Drone2EditorView::setContentScaleFactor(ScaleFactor factor)
{
    if (factor <= 0.0f)
        return kInvalidArgument;
    m_hostSetScale = true;
    ApplyScale(factor);
    return kResultTrue;
}

void Drone2EditorView::ApplyScale(float scale)
{
    const bool changed = scale != m_scale || (m_panel && m_panel->Scale() != scale);
    m_scale = scale;
    const SIZE s = PanelView::PixelSize(scale);
    ViewRect r(0, 0, s.cx, s.cy);
    setRect(r);
    if (m_panel && changed)
    {
        m_panel->SetScale(scale);
        if (plugFrame)
            plugFrame->resizeView(this, &r);
    }
}
} // namespace Drone2Plugin
