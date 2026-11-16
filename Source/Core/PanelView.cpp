// PanelView.cpp - editor layout and behaviour.
#include "PanelView.h"
#include "PanelTheme.h"
#include "Recorder.h"
#include "SynthEngine.h"

#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <cmath>
#include <cstdio>

using namespace Gdiplus;
using namespace params;

namespace
{
constexpr UINT_PTR kTimerId = 1;
const wchar_t kClassName[] = L"Drone2PanelView";
const wchar_t kPromptClass[] = L"Drone2NamePrompt";

// GDI+ is started by the first panel and shut down with the last one, so the
// plugin DLL never touches it from DllMain.
int g_gdiplusUsers = 0;
ULONG_PTR g_gdiplusToken = 0;

bool AcquireGdiplus()
{
    if (g_gdiplusUsers == 0)
    {
        GdiplusStartupInput input;
        if (GdiplusStartup(&g_gdiplusToken, &input, nullptr) != Ok)
            return false;
    }
    ++g_gdiplusUsers;
    return true;
}

void ReleaseGdiplus()
{
    if (--g_gdiplusUsers == 0)
        GdiplusShutdown(g_gdiplusToken);
}

// The module (exe or plugin DLL) this code is linked into.
HINSTANCE ModuleInstance()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ModuleInstance), &module);
    return module;
}

// Tracker-style layout on the bottom letter row: Z = C, S = C#, X = D ...
int KeyToSemitone(UINT vk)
{
    switch (vk)
    {
    case 'Z': return 0;  case 'S': return 1;  case 'X': return 2;  case 'D': return 3;
    case 'C': return 4;  case 'V': return 5;  case 'G': return 6;  case 'B': return 7;
    case 'H': return 8;  case 'N': return 9;  case 'J': return 10; case 'M': return 11;
    case VK_OEM_COMMA: return 12; case 'L': return 13; case VK_OEM_PERIOD: return 14;
    default: return -1;
    }
}

// A small modal naming box with its own message loop. A plain child edit control
// would not work in every DAW, because many hosts swallow keystrokes; here we
// own the loop, so typing always reaches the box.
struct PromptState
{
    HWND edit = nullptr;
    bool done = false, accepted = false;
    std::wstring text;
    HBRUSH bgBrush = nullptr, editBrush = nullptr;
    HFONT font = nullptr;
};

LRESULT CALLBACK PromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg)
    {
    case WM_COMMAND:
        if (state && (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL))
        {
            state->accepted = LOWORD(wp) == IDOK;
            if (state->accepted)
            {
                wchar_t buffer[256] = {};
                GetWindowTextW(state->edit, buffer, 256);
                state->text = buffer;
            }
            state->done = true;
            return 0;
        }
        break;
    case WM_CLOSE:
        if (state)
            state->done = true;
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        if (state)
        {
            const theme::Palette& p = theme::P();
            const bool isEdit = msg == WM_CTLCOLOREDIT;
            const Color text = p.text, back = isEdit ? p.well : p.card;
            SetTextColor(reinterpret_cast<HDC>(wp), RGB(text.GetR(), text.GetG(), text.GetB()));
            SetBkColor(reinterpret_cast<HDC>(wp), RGB(back.GetR(), back.GetG(), back.GetB()));
            return reinterpret_cast<LRESULT>(isEdit ? state->editBrush : state->bgBrush);
        }
        break;
    case WM_ERASEBKGND:
        if (state)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, state->bgBrush);
            return 1;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool RunNamePrompt(HWND owner, const wchar_t* title, const wchar_t* prompt, std::wstring& name, float scale)
{
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = PromptProc;
        wc.hInstance = ModuleInstance();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kPromptClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    const theme::Palette& p = theme::P();
    PromptState state;
    state.bgBrush = CreateSolidBrush(RGB(p.card.GetR(), p.card.GetG(), p.card.GetB()));
    state.editBrush = CreateSolidBrush(RGB(p.well.GetR(), p.well.GetG(), p.well.GetB()));
    auto px = [scale](int v) { return static_cast<int>(v * scale); };
    state.font = CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    RECT ownerRect;
    GetWindowRect(owner, &ownerRect);
    const int w = px(380), h = px(150);
    const int x = (ownerRect.left + ownerRect.right - w) / 2, y = (ownerRect.top + ownerRect.bottom - h) / 2;
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT frame = { 0, 0, w, h };
    AdjustWindowRectEx(&frame, style, FALSE, WS_EX_DLGMODALFRAME);
    HWND wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kPromptClass, title, style, x, y, frame.right - frame.left, frame.bottom - frame.top,
                               owner, nullptr, ModuleInstance(), nullptr);
    SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    auto child = [&](const wchar_t* cls, const wchar_t* text, DWORD st, int cx, int cy, int cw, int ch, int id, DWORD ex = 0)
    {
        HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | st, px(cx), px(cy), px(cw), px(ch), wnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ModuleInstance(), nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
        return c;
    };
    child(L"STATIC", prompt, 0, 18, 14, 344, 22, 0);
    state.edit = child(L"EDIT", name.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 18, 42, 344, 28, 100, WS_EX_CLIENTEDGE);
    SendMessageW(state.edit, EM_SETLIMITTEXT, 100, 0);
    child(L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 182, 92, 86, 32, IDOK);
    child(L"BUTTON", L"Cancel", WS_TABSTOP, 276, 92, 86, 32, IDCANCEL);

    EnableWindow(owner, FALSE);
    ShowWindow(wnd, SW_SHOW);
    SetFocus(state.edit);
    SendMessageW(state.edit, EM_SETSEL, 0, -1);

    MSG msg;
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
        {
            state.done = true;
            continue;
        }
        if (!IsDialogMessageW(wnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    DestroyWindow(wnd);
    DeleteObject(state.bgBrush);
    DeleteObject(state.editBrush);
    DeleteObject(state.font);
    if (state.accepted)
        name = state.text;
    return state.accepted;
}
} // namespace

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------
PanelView::PanelView(PanelHost& host) : m_host(host)
{
    theme::LoadSaved();
    BuildLayout();
    m_bank.Refresh();
    m_presetName = m_host.PatchName();
    if (m_presetName.empty())
        m_presetName = L"Init Drone";
    m_presetIndex = m_bank.IndexOfName(m_presetName);
    if (m_presetIndex >= 0)
        m_presetPath = m_bank.Get(m_presetIndex).path;
    m_randomSeed = GetTickCount();
}

PanelView::~PanelView()
{
    Destroy();
    m_static.reset();
    m_back.reset();
    m_fontLabel.reset();
    m_fontValue.reset();
    m_fontTitle.reset();
    m_fontName.reset();
    m_fontLogo.reset();
    m_fontSmall.reset();
    if (m_gdiplus)
        ReleaseGdiplus();
}

SIZE PanelView::PixelSize(float scale)
{
    return { static_cast<LONG>(lroundf(kPanelW * scale)), static_cast<LONG>(lroundf(kPanelH * scale)) };
}

bool PanelView::Create(HWND parent, int x, int y, float scale)
{
    if (!m_gdiplus)
    {
        if (!AcquireGdiplus())
            return false;
        m_gdiplus = true;
    }
    m_scale = scale;

    static bool registered = false;
    if (!registered)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = ModuleInstance();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
    const SIZE size = PixelSize(scale);
    m_hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, x, y, size.cx, size.cy,
                             parent, nullptr, ModuleInstance(), this);
    if (!m_hwnd)
        return false;
    SetTimer(m_hwnd, kTimerId, 33, nullptr);
    return true;
}

void PanelView::Destroy()
{
    if (m_hwnd)
    {
        ReleaseKeyboardNotes();
        KillTimer(m_hwnd, kTimerId);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void PanelView::SetScale(float scale)
{
    if (scale == m_scale)
        return;
    m_scale = scale;
    m_static.reset();
    m_back.reset();
    m_fontLabel.reset();
    m_staticDirty = true;
    if (m_hwnd)
    {
        const SIZE size = PixelSize(scale);
        SetWindowPos(m_hwnd, nullptr, 0, 0, size.cx, size.cy, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void PanelView::Refresh()
{
    if (m_hwnd)
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

void PanelView::SetStatus(const std::wstring& text)
{
    m_status = text;
    m_statusUntil = GetTickCount64() + 5000;
}

void PanelView::OnPowerLost(const std::wstring& reason)
{
    SetStatus(reason);
    m_staticDirty = true;
    Refresh();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void PanelView::AddKnob(int id, float cx, float cy, int page, int accent, float r)
{
    Control c{ Kind::Knob, id, RectF(cx - r, cy - r, r * 2.0f, r * 2.0f), page, accent };
    m_controls.push_back(c);
}

void PanelView::AddDropdown(int id, const RectF& rc, int page, int accent)
{
    Control c{ Kind::Dropdown, id, rc, page, accent };
    m_controls.push_back(c);
}

void PanelView::AddButton(int action, const RectF& rc, const wchar_t* text, const wchar_t* tip)
{
    Control c{ Kind::Button, action, rc, -1, -1, text, tip };
    m_controls.push_back(c);
}

void PanelView::BuildLayout()
{
    // Header.
    float x = 142.0f;
    const float by = 14.0f, bh = 30.0f;
    AddButton(A_PATCH_PREV, RectF(x, by, 26, bh), L"‹", L"Previous patch");  x += 28;
    m_patchRect = RectF(x, by, 220, bh);
    AddButton(A_PATCH_MENU, m_patchRect, nullptr, L"Choose a patch");             x += 222;
    AddButton(A_PATCH_NEXT, RectF(x, by, 26, bh), L"›", L"Next patch");      x += 32;
    struct B { int action; const wchar_t* text; float w; const wchar_t* tip; };
    const B buttons[] = {
        { A_SAVE, L"SAVE", 50, L"Save over the current patch" },
        { A_SAVE_AS, L"SAVE AS", 66, L"Save with a new name" },
        { A_RENAME, L"RENAME", 62, L"Rename this user patch" },
        { A_DELETE, L"DELETE", 58, L"Delete this user patch" },
        { A_LOAD, L"LOAD", 50, L"Load a patch file from disk" },
        { A_RANDOM, L"RANDOM", 66, L"Generate a random drone" },
        { A_INIT, L"INIT", 42, L"Back to the initial patch" },
    };
    for (const B& b : buttons)
    {
        AddButton(b.action, RectF(x, by, b.w, bh), b.text, b.tip);
        x += b.w + 3.0f;
    }
    float rx = kPanelW - 12.0f;
    rx -= 62;  AddButton(A_POWER, RectF(rx, by, 62, bh), L"POWER", L"Audio on / off (Space)");
    rx -= 4 + 18; m_meterRect = RectF(rx, by, 18, bh);
    rx -= 4 + 22; AddButton(A_REC_FOLDER, RectF(rx, by, 22, bh), L"…", L"Open the recordings folder");
    rx -= 2 + 28; AddButton(A_REC_AS, RectF(rx, by, 28, bh), L"AS", L"Record to a file you choose");
    rx -= 2 + 54; AddButton(A_REC, RectF(rx, by, 54, bh), L"REC", L"Record to a 24-bit WAV file");
    rx -= 4 + 72; AddButton(A_THEME, RectF(rx, by, 72, bh), L"THEME", L"Colour theme");
    m_statusRect = RectF(142.0f, 44.0f, kPanelW - 154.0f, 16.0f);

    // Layer cards.
    const float margin = 10.0f, gap = 8.0f;
    const float cardW = (kPanelW - margin * 2.0f - gap * 3.0f) / 4.0f;
    const float layerY = 64.0f, layerH = 304.0f;
    static const wchar_t* const titles[kNumLayers] = { L"A", L"B", L"C", L"D" };
    for (int l = 0; l < kNumLayers; ++l)
    {
        const float cx = margin + l * (cardW + gap);
        m_cards.push_back({ RectF(cx, layerY, cardW, layerH), titles[l], -1, l });

        Control on{ Kind::LayerToggle, LayerId(l, LP_ON), RectF(cx + 8, layerY + 7, 22, 22), -1, l };
        m_controls.push_back(on);
        AddDropdown(LayerId(l, LP_ALGO), RectF(cx + 36, layerY + 7, 150, 22), -1, l);
        AddDropdown(LayerId(l, LP_TABLE), RectF(cx + 190, layerY + 7, cardW - 198, 22), -1, l);
        m_previewRect[l] = RectF(cx + 8, layerY + 36, cardW - 16, 46);

        const float col = (cardW - 12.0f) / 5.0f;
        const float k0 = cx + 6.0f + col * 0.5f;
        const float rows[3] = { layerY + 112.0f, layerY + 182.0f, layerY + 252.0f };
        const LayerParam grid[3][5] = {
            { LP_MORPH, LP_SCAN_DEPTH, LP_SCAN_RATE, LP_TIMBRE, LP_COLOR },
            { LP_OCT, LP_SEMI, LP_FINE, LP_LEVEL, LP_PAN },
            { LP_VOICES, LP_DETUNE, LP_WIDTH, LP_DRIFT, LP_COUNT },
        };
        for (int r = 0; r < 3; ++r)
            for (int i = 0; i < 5; ++i)
                if (grid[r][i] != LP_COUNT)
                    AddKnob(LayerId(l, grid[r][i]), k0 + i * col, rows[r], -1, l);
    }

    // Middle row: vector, pitch and amp, filter, displays.
    const float midY = layerY + layerH + gap, midH = 178.0f;
    m_cards.push_back({ RectF(margin, midY, 306, midH), L"VECTOR MIX", -1, -1 });
    m_padRect = RectF(margin + 10, midY + 28, 142, 142);
    {
        Control pad{ Kind::Pad, P_VEC_X, m_padRect, -1, -1 };
        m_controls.push_back(pad);
        AddKnob(P_ORBIT_DEPTH, margin + 192, midY + 58, -1);
        AddKnob(P_ORBIT_RATE, margin + 262, midY + 58, -1);
        AddDropdown(P_ORBIT_MODE, RectF(margin + 164, midY + 118, 132, 24), -1);
    }

    const float pitchX = margin + 306 + gap;
    m_cards.push_back({ RectF(pitchX, midY, 306, midH), L"PITCH & AMP", -1, -1 });
    {
        const float col = 72.0f, x0 = pitchX + 44.0f;
        AddKnob(P_ROOT, x0, midY + 58, -1);
        AddKnob(P_TUNE, x0 + col, midY + 58, -1);
        AddKnob(P_GLIDE, x0 + col * 2, midY + 58, -1);
        AddKnob(P_ATTACK, x0, midY + 130, -1);
        AddKnob(P_RELEASE, x0 + col, midY + 130, -1);
        AddDropdown(P_TEMPERAMENT, RectF(pitchX + 190, midY + 108, 106, 24), -1);
        Control hold{ Kind::Toggle, P_HOLD, RectF(pitchX + 190, midY + 138, 106, 24), -1, -1, L"HOLD" };
        m_controls.push_back(hold);
        AddButton(A_MIDI, RectF(pitchX + 248, midY + 34, 48, 24), L"MIDI", L"Choose the MIDI input");
    }

    const float filterX = pitchX + 306 + gap;
    m_cards.push_back({ RectF(filterX, midY, 196, midH), L"FILTER", -1, -1 });
    AddDropdown(P_FLT_MODE, RectF(filterX + 76, midY + 4, 112, 21), -1);
    AddKnob(P_FLT_CUTOFF, filterX + 54, midY + 62, -1, -1, 21.0f);
    AddKnob(P_FLT_RES, filterX + 142, midY + 62, -1);
    AddKnob(P_FLT_DRIVE, filterX + 54, midY + 136, -1);
    AddKnob(P_FLT_KEY, filterX + 142, midY + 136, -1);

    const float dispX = filterX + 196 + gap;
    const float dispW = kPanelW - margin - dispX;
    m_cards.push_back({ RectF(dispX, midY, dispW, midH), L"OUTPUT", -1, -1 });
    m_scopeRect = RectF(dispX + 8, midY + 28, dispW - 16, 66);
    m_specRect = RectF(dispX + 8, midY + 98, dispW - 16, 72);

    // Bottom row: effects and modulation pages.
    const float botY = midY + midH + gap;
    Control fx{ Kind::Tab, A_TAB_FX, RectF(margin, botY, 110, 24), -1, -1, L"EFFECTS" };
    Control mod{ Kind::Tab, A_TAB_MOD, RectF(margin + 114, botY, 110, 24), -1, -1, L"MODULATION" };
    m_controls.push_back(fx);
    m_controls.push_back(mod);

    const float pageY = botY + 28.0f;
    const float pageH = kPanelH - pageY - margin;
    const float row1 = pageY + 66.0f, row2 = pageY + 156.0f;
    const float unit = (kPanelW - margin * 2.0f - gap * 4.0f) / 14.0f;
    float fxX = margin;
    auto fxCard = [&](const wchar_t* title, int cols)
    {
        const float w = unit * cols;
        m_cards.push_back({ RectF(fxX, pageY, w, pageH), title, 0, -1 });
        const float start = fxX;
        fxX += w + gap;
        return start;
    };
    auto place = [&](float start, const int* ids, int count, int cols)
    {
        for (int i = 0; i < count; ++i)
            AddKnob(ids[i], start + unit * ((i % cols) + 0.5f), i < cols ? row1 : row2, 0);
    };
    {
        const int ids[] = { P_ENS_RATE, P_ENS_DEPTH, P_ENS_MIX };
        place(fxCard(L"ENSEMBLE", 2), ids, 3, 2);
    }
    {
        const int ids[] = { P_PHS_RATE, P_PHS_DEPTH, P_PHS_FB, P_PHS_MIX };
        place(fxCard(L"PHASER", 2), ids, 4, 2);
    }
    {
        const int ids[] = { P_DLY_TIME, P_DLY_FB, P_DLY_MIX, P_DLY_TONE, P_DLY_PING, P_DLY_MOD };
        place(fxCard(L"TAPE DELAY", 3), ids, 6, 3);
    }
    {
        const float start = fxCard(L"FDN REVERB", 4);
        const int ids[] = { P_REV_SIZE, P_REV_DECAY, P_REV_DAMP, P_REV_MIX, P_REV_DIFF, P_REV_MOD, P_REV_SHIMMER };
        place(start, ids, 7, 4);
        const float cx = start + unit * 3.5f;
        Control freeze{ Kind::Toggle, P_REV_FREEZE, RectF(cx - 32, row2 - 12, 64, 24), 0, -1, L"FREEZE" };
        m_controls.push_back(freeze);
    }
    {
        const int ids[] = { P_MST_VOLUME, P_MST_WIDTH, P_MST_LOWCUT, P_MST_TILT };
        place(fxCard(L"MASTER", 3), ids, 4, 3);
    }

    // Modulation page.
    const float lfoW = 118.0f;
    for (int i = 0; i < kNumLfos; ++i)
    {
        static const wchar_t* const names[kNumLfos] = { L"LFO 1", L"LFO 2", L"LFO 3", L"LFO 4" };
        const float lx = margin + i * (lfoW + 6.0f);
        m_cards.push_back({ RectF(lx, pageY, lfoW, pageH), names[i], 1, -1 });
        AddDropdown(LfoId(i, LFOP_SHAPE), RectF(lx + 8, pageY + 30, lfoW - 16, 24), 1);
        AddKnob(LfoId(i, LFOP_RATE), lx + lfoW * 0.5f, pageY + 88, 1, -1, 20.0f);
        m_lfoBar[i] = RectF(lx + 12, pageY + 142, lfoW - 24, 10);
    }
    const float matrixX = margin + kNumLfos * (lfoW + 6.0f) + 2.0f;
    const float matrixW = kPanelW - margin - matrixX;
    m_cards.push_back({ RectF(matrixX, pageY, matrixW, pageH), L"MOD MATRIX", 1, -1 });
    const float slotW = (matrixW - 24.0f) / 2.0f;
    const float slotH = (pageH - 34.0f) / 4.0f;
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const float sx = matrixX + 8 + (s / 4) * (slotW + 8.0f);
        const float sy = pageY + 30 + (s % 4) * slotH;
        const float h = slotH - 6.0f;
        AddDropdown(ModId(s, MODP_SRC), RectF(sx, sy, 92, h), 1);
        AddDropdown(ModId(s, MODP_DST), RectF(sx + 96, sy, 106, h), 1);
        Control amount{ Kind::Slider, ModId(s, MODP_AMT), RectF(sx + 206, sy, slotW - 206, h), 1 };
        m_controls.push_back(amount);
    }
}

int PanelView::LayerOf(int id) const
{
    return id >= P_LAYER_BASE && id < P_FLT_MODE ? (id - P_LAYER_BASE) / LP_COUNT : -1;
}

bool PanelView::ControlEnabled(const Control& c)
{
    if (c.kind == Kind::Button)
    {
        if (c.id == A_RENAME || c.id == A_DELETE)
            return !m_presetPath.empty();
        return true;
    }
    const int layer = LayerOf(c.id);
    if (layer < 0)
        return true;
    if (c.id == LayerId(layer, LP_ON))
        return true;
    if (m_host.GetParam(LayerId(layer, LP_ON)) < 0.5f)
        return false;
    if (c.id == LayerId(layer, LP_TABLE))
        return AlgorithmUsesTable(ToStep(ALG_COUNT, m_host.GetParam(LayerId(layer, LP_ALGO))));
    return true;
}

int PanelView::HitTest(POINT pt) const
{
    const PointF p = ToPanel(pt);
    // Latest added first, so small controls win over large ones under them.
    for (int i = static_cast<int>(m_controls.size()) - 1; i >= 0; --i)
    {
        const Control& c = m_controls[i];
        if (!Visible(c))
            continue;
        RectF rc = c.rc;
        if (c.kind == Kind::Knob)
            rc.Inflate(6.0f, 8.0f);
        if (rc.Contains(p))
            return i;
    }
    return -1;
}

PointF PanelView::ToPanel(POINT pt) const
{
    return PointF(pt.x / m_scale, pt.y / m_scale);
}

HWND PanelView::DialogOwner() const
{
    HWND root = GetAncestor(m_hwnd, GA_ROOT);
    return root ? root : m_hwnd;
}

// ---------------------------------------------------------------------------
// Parameters and actions
// ---------------------------------------------------------------------------
void PanelView::EditParam(int id, float value)
{
    m_host.SetParam(id, value);
    m_presetDirty = true;
    m_staticDirty = true;
}

ParamValues PanelView::CurrentValues()
{
    ParamValues values{};
    for (int id = 0; id < P_COUNT; ++id)
        values[id] = m_host.GetParam(id);
    return values;
}

void PanelView::ApplyValues(const ParamValues& values)
{
    for (int id = 0; id < P_COUNT; ++id)
    {
        m_host.BeginEdit(id);
        m_host.SetParam(id, values[id]);
        m_host.EndEdit(id);
    }
    m_staticDirty = true;
}

void PanelView::SetCurrentPatch(const std::wstring& name, const std::wstring& path)
{
    m_presetName = name;
    m_presetPath = path;
    m_presetIndex = m_bank.IndexOfName(name);
    m_presetDirty = false;
    m_host.SetPatchName(name);
    m_staticDirty = true;
}

void PanelView::ApplyPreset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_bank.Count()))
        return;
    ParamValues values;
    if (!m_bank.Load(static_cast<size_t>(index), values))
    {
        SetStatus(L"Could not read patch: " + m_bank.Get(index).name);
        return;
    }
    ApplyValues(values);
    SetCurrentPatch(m_bank.Get(index).name, m_bank.Get(index).path);
}

void PanelView::StepPreset(int delta)
{
    m_bank.Refresh();
    const int count = static_cast<int>(m_bank.Count());
    if (count > 0)
        ApplyPreset((m_presetIndex + delta + count) % count);
}

bool PanelView::PromptForName(const wchar_t* title, std::wstring& name)
{
    for (;;)
    {
        if (!RunNamePrompt(DialogOwner(), title, L"Patch name:", name, m_scale))
            return false;
        if (PresetBank::IsValidName(name))
            return true;
        MessageBoxW(DialogOwner(), L"A patch name can't be empty, start or end with a space, end with a dot, or contain any of  < > : \" / \\ | ? *",
                    L"Drone 2", MB_OK | MB_ICONWARNING);
    }
}

bool PanelView::WritePatch(const std::wstring& path)
{
    if (PresetBank::WriteTextFile(path, PresetBank::Serialize(CurrentValues())))
        return true;
    SetStatus(L"Could not write " + path);
    return false;
}

void PanelView::SavePatch(bool forceName)
{
    if (!forceName && !m_presetPath.empty())
    {
        if (WritePatch(m_presetPath))
        {
            m_presetDirty = false;
            m_staticDirty = true;
            SetStatus(L"Saved " + m_presetName);
        }
        return;
    }
    std::wstring name = m_presetName;
    if (!PromptForName(L"Save Patch", name))
        return;
    const std::wstring path = PresetBank::PathForName(name);
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && _wcsicmp(path.c_str(), m_presetPath.c_str()) != 0)
    {
        if (MessageBoxW(DialogOwner(), (L"A patch called \"" + name + L"\" already exists. Replace it?").c_str(), L"Drone 2",
                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return;
    }
    if (WritePatch(path))
    {
        m_bank.Refresh();
        SetCurrentPatch(name, path);
        SetStatus(L"Saved " + name);
    }
}

void PanelView::RenamePatch()
{
    if (m_presetPath.empty())
    {
        SetStatus(L"Factory patches can't be renamed. Use SAVE AS to make your own copy.");
        return;
    }
    std::wstring name = m_presetName;
    if (!PromptForName(L"Rename Patch", name) || name == m_presetName)
        return;
    const std::wstring target = PresetBank::PathForName(name);
    const bool caseOnly = _wcsicmp(target.c_str(), m_presetPath.c_str()) == 0;
    if (!caseOnly && GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        MessageBoxW(DialogOwner(), (L"A patch called \"" + name + L"\" already exists.").c_str(), L"Drone 2", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!MoveFileW(m_presetPath.c_str(), target.c_str()))
    {
        SetStatus(L"Could not rename the patch file");
        return;
    }
    const bool dirty = m_presetDirty;
    m_bank.Refresh();
    SetCurrentPatch(name, target);
    m_presetDirty = dirty;
    SetStatus(L"Renamed to " + name);
}

void PanelView::DeletePatch()
{
    if (m_presetPath.empty())
    {
        SetStatus(L"Factory patches can't be deleted");
        return;
    }
    if (MessageBoxW(DialogOwner(), (L"Delete the patch \"" + m_presetName + L"\"? This can't be undone.").c_str(), L"Drone 2",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    if (!DeleteFileW(m_presetPath.c_str()))
    {
        SetStatus(L"Could not delete the patch file");
        return;
    }
    const std::wstring gone = m_presetName;
    m_bank.Refresh();
    // The sound keeps playing; it just no longer belongs to a saved patch.
    SetCurrentPatch(gone, L"");
    m_presetIndex = -1;
    m_presetDirty = true;
    SetStatus(L"Deleted " + gone);
}

void PanelView::LoadPatchFile()
{
    wchar_t file[MAX_PATH] = {};
    const std::wstring folder = PresetBank::UserFolder();
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = DialogOwner();
    ofn.lpstrFilter = L"Drone 2 patches (*.d2patch)\0*.d2patch\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = folder.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn))
        return;
    std::string text;
    if (!PresetBank::ReadTextFile(file, text))
    {
        SetStatus(L"Could not read that file");
        return;
    }
    ParamValues values;
    PresetBank::Parse(text, values);
    ApplyValues(values);
    m_bank.Refresh();
    SetCurrentPatch(PresetBank::NameFromPath(file), file);
    SetStatus(L"Loaded " + m_presetName);
}

void PanelView::ShowPatchMenu()
{
    m_bank.Refresh();
    HMENU menu = CreatePopupMenu();
    std::vector<std::pair<std::wstring, HMENU>> categories;
    constexpr UINT kFirst = 1000, kFolder = 900;
    for (size_t i = 0; i < m_bank.Count(); ++i)
    {
        const Preset& p = m_bank.Get(i);
        const UINT flags = MF_STRING | (static_cast<int>(i) == m_presetIndex ? MF_CHECKED : 0);
        if (p.path.empty())
        {
            HMENU sub = nullptr;
            for (auto& c : categories)
                if (c.first == p.category)
                    sub = c.second;
            if (!sub)
            {
                sub = CreatePopupMenu();
                categories.emplace_back(p.category, sub);
                AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), p.category.c_str());
            }
            AppendMenuW(sub, flags, kFirst + i, p.name.c_str());
        }
        else
        {
            if (i == m_bank.FactoryCount())
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, flags, kFirst + i, p.name.c_str());
        }
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kFolder, L"Open Patches Folder");

    POINT pt = { static_cast<LONG>(m_patchRect.X * m_scale), static_cast<LONG>(m_patchRect.GetBottom() * m_scale) };
    ClientToScreen(m_hwnd, &pt);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == kFolder)
        ShellExecuteW(nullptr, L"open", PresetBank::UserFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else if (cmd >= kFirst)
        ApplyPreset(static_cast<int>(cmd - kFirst));
}

void PanelView::ShowThemeMenu()
{
    HMENU menu = CreatePopupMenu();
    for (int i = 0; i < theme::Count(); ++i)
        AppendMenuW(menu, MF_STRING | (i == theme::Current() ? MF_CHECKED : 0), 1 + i, theme::Name(i));
    POINT pt;
    GetCursorPos(&pt);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd > 0)
    {
        theme::Apply(static_cast<int>(cmd) - 1);
        m_staticDirty = true;
        Refresh();
    }
}

void PanelView::ShowDropdown(const Control& c)
{
    const int steps = Info(c.id).steps;
    const int current = ToStep(steps, m_host.GetParam(c.id));
    HMENU menu = CreatePopupMenu();
    for (int i = 0; i < steps; ++i)
    {
        // Long lists wrap into columns rather than scroll.
        const UINT breakFlag = (i > 0 && i % 14 == 0) ? MF_MENUBARBREAK : 0;
        AppendMenuW(menu, MF_STRING | breakFlag | (i == current ? MF_CHECKED : 0), 1 + i, Format(c.id, FromStep(steps, i)).c_str());
    }
    POINT pt = { static_cast<LONG>(c.rc.X * m_scale), static_cast<LONG>(c.rc.GetBottom() * m_scale) };
    ClientToScreen(m_hwnd, &pt);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd > 0)
    {
        m_host.BeginEdit(c.id);
        EditParam(c.id, FromStep(steps, static_cast<int>(cmd) - 1));
        m_host.EndEdit(c.id);
    }
}

void PanelView::RunAction(int action)
{
    switch (action)
    {
    case A_PATCH_PREV: StepPreset(-1); break;
    case A_PATCH_NEXT: StepPreset(1); break;
    case A_PATCH_MENU: ShowPatchMenu(); break;
    case A_SAVE:       SavePatch(false); break;
    case A_SAVE_AS:    SavePatch(true); break;
    case A_RENAME:     RenamePatch(); break;
    case A_DELETE:     DeletePatch(); break;
    case A_LOAD:       LoadPatchFile(); break;
    case A_RANDOM:
    {
        ApplyValues(PresetBank::Randomize(m_randomSeed++));
        SetCurrentPatch(L"Random", L"");
        m_presetIndex = -1;
        m_presetDirty = true;
        break;
    }
    case A_INIT:
        ApplyValues(PresetBank::Defaults());
        SetCurrentPatch(L"Init Drone", L"");
        if (SynthEngine* engine = m_host.Engine())
            engine->ResetPitch();
        break;
    case A_THEME:      ShowThemeMenu(); break;
    case A_MIDI:       SetStatus(m_host.CycleMidiInput()); break;
    case A_REC:        ToggleRecording(false); break;
    case A_REC_AS:     ToggleRecording(true); break;
    case A_REC_FOLDER: ShellExecuteW(nullptr, L"open", Recorder::DefaultFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case A_POWER:      m_host.SetPowered(!m_host.IsPowered()); m_staticDirty = true; break;
    case A_TAB_FX:     m_page = 0; m_staticDirty = true; break;
    case A_TAB_MOD:    m_page = 1; m_staticDirty = true; break;
    default: break;
    }
    Refresh();
}

void PanelView::ResetControl(const Control& c)
{
    m_host.BeginEdit(c.id);
    EditParam(c.id, Info(c.id).defaultValue);
    m_host.EndEdit(c.id);
}

void PanelView::SetPadFromPoint(const Control& c, PointF p)
{
    const float x = std::clamp((p.X - c.rc.X) / c.rc.Width, 0.0f, 1.0f);
    const float y = std::clamp((p.Y - c.rc.Y) / c.rc.Height, 0.0f, 1.0f);
    EditParam(P_VEC_X, x);
    EditParam(P_VEC_Y, y);
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------
bool PanelView::Recording()
{
    Recorder* r = m_host.GetRecorder();
    return r && r->IsRecording();
}

void PanelView::ToggleRecording(bool chooseFile)
{
    Recorder* recorder = m_host.GetRecorder();
    if (!recorder)
    {
        SetStatus(L"Recording is not available until the host connects the plug-in");
        return;
    }
    if (recorder->IsRecording())
    {
        const bool ok = recorder->Stop();
        const std::wstring path = recorder->Path();
        if (!ok)
            SetStatus(L"Recording failed: could not write " + path);
        else if (recorder->DroppedFrames() > 0)
            SetStatus(L"Saved " + path + L" (the disk could not keep up: some audio was lost)");
        else
            SetStatus(L"Saved " + path);
        m_staticDirty = true;
        return;
    }

    std::wstring path = Recorder::DefaultFolder() + L"\\" + Recorder::MakeFileName(m_presetName);
    if (chooseFile)
    {
        wchar_t file[MAX_PATH] = {};
        wcsncpy_s(file, Recorder::MakeFileName(m_presetName).c_str(), _TRUNCATE);
        const std::wstring folder = Recorder::DefaultFolder();
        OPENFILENAMEW ofn = { sizeof(ofn) };
        ofn.hwndOwner = DialogOwner();
        ofn.lpstrFilter = L"WAV audio (*.wav)\0*.wav\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrInitialDir = folder.c_str();
        ofn.lpstrDefExt = L"wav";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&ofn))
            return;
        path = file;
    }
    if (!m_host.IsPowered())
        m_host.SetPowered(true);
    recorder->Start(path);
    SetStatus(L"Recording to " + path);
    m_staticDirty = true;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
void PanelView::ReleaseKeyboardNotes()
{
    if (SynthEngine* engine = m_host.Engine())
        for (const auto& k : m_keyNotes)
            engine->NoteOff(k.second);
    m_keyNotes.clear();
}

void PanelView::HandleMidiMessage(unsigned message)
{
    const unsigned status = message & 0xF0;
    const int d1 = static_cast<int>((message >> 8) & 0x7F);
    const int d2 = static_cast<int>((message >> 16) & 0x7F);
    SynthEngine* engine = m_host.Engine();
    if (!engine)
        return;
    if (status == 0x90 && d2 > 0)
        engine->NoteOn(d1, d2 / 127.0f);
    else if (status == 0x80 || status == 0x90)
        engine->NoteOff(d1);
    else if (status == 0xB0 && d1 == 1)
        engine->SetModWheel(d2 / 127.0f);
    else if (status == 0xB0 && (d1 == 120 || d1 == 123))
        engine->AllNotesOff();
}

bool PanelView::HandleKey(UINT vk, bool down)
{
    SynthEngine* engine = m_host.Engine();
    const int semi = KeyToSemitone(vk);
    if (semi >= 0)
    {
        auto it = std::find_if(m_keyNotes.begin(), m_keyNotes.end(), [vk](const auto& k) { return k.first == vk; });
        if (down && it == m_keyNotes.end())
        {
            const int note = (m_octave + 1) * 12 + semi;
            m_keyNotes.emplace_back(vk, note);
            if (engine) engine->NoteOn(note);
        }
        else if (!down && it != m_keyNotes.end())
        {
            if (engine) engine->NoteOff(it->second);
            m_keyNotes.erase(it);
        }
        return true;
    }
    if (!down)
        return vk == VK_SPACE || vk == VK_BACK || vk == VK_OEM_MINUS || vk == VK_OEM_PLUS || vk == VK_LEFT || vk == VK_RIGHT;
    switch (vk)
    {
    case VK_SPACE:     RunAction(A_POWER); return true;
    case VK_BACK:
        ReleaseKeyboardNotes();
        if (engine) engine->ResetPitch();
        SetStatus(L"Pitch back to ROOT");
        return true;
    case VK_LEFT:      StepPreset(-1); return true;
    case VK_RIGHT:     StepPreset(1); return true;
    case VK_OEM_MINUS: m_octave = (std::max)(m_octave - 1, 0); SetStatus(L"Keyboard octave " + std::to_wstring(m_octave)); return true;
    case VK_OEM_PLUS:  m_octave = (std::min)(m_octave + 1, 8); SetStatus(L"Keyboard octave " + std::to_wstring(m_octave)); return true;
    }
    return false;
}

void PanelView::OnLButtonDown(POINT pt, bool doubleClick)
{
    SetFocus(m_hwnd);
    const int hit = HitTest(pt);
    if (hit < 0)
        return;
    const Control& c = m_controls[hit];
    if (!ControlEnabled(c))
        return;

    if (doubleClick && (c.kind == Kind::Knob || c.kind == Kind::Slider))
    {
        ResetControl(c);
        return;
    }
    if (doubleClick && c.kind == Kind::Pad)
    {
        m_host.BeginEdit(P_VEC_X);
        m_host.BeginEdit(P_VEC_Y);
        EditParam(P_VEC_X, 0.5f);
        EditParam(P_VEC_Y, 0.5f);
        m_host.EndEdit(P_VEC_X);
        m_host.EndEdit(P_VEC_Y);
        return;
    }

    switch (c.kind)
    {
    case Kind::Knob:
    case Kind::Slider:
        m_host.BeginEdit(c.id);
        m_drag = hit;
        m_dragValue = m_host.GetParam(c.id);
        m_lastPt = pt;
        SetCapture(m_hwnd);
        m_staticDirty = true;
        break;
    case Kind::Pad:
        m_host.BeginEdit(P_VEC_X);
        m_host.BeginEdit(P_VEC_Y);
        m_drag = hit;
        SetPadFromPoint(c, ToPanel(pt));
        SetCapture(m_hwnd);
        break;
    case Kind::Dropdown:
        ShowDropdown(c);
        break;
    case Kind::Toggle:
    case Kind::LayerToggle:
        m_host.BeginEdit(c.id);
        EditParam(c.id, m_host.GetParam(c.id) >= 0.5f ? 0.0f : 1.0f);
        m_host.EndEdit(c.id);
        break;
    case Kind::Button:
    case Kind::Tab:
        m_pressed = hit;
        m_staticDirty = true;
        SetCapture(m_hwnd);
        break;
    }
}

void PanelView::OnLButtonUp(POINT pt)
{
    if (m_drag >= 0)
    {
        const Control& c = m_controls[m_drag];
        if (c.kind == Kind::Pad)
        {
            m_host.EndEdit(P_VEC_X);
            m_host.EndEdit(P_VEC_Y);
        }
        else
        {
            m_host.EndEdit(c.id);
        }
        m_drag = -1;
        m_staticDirty = true;
        ReleaseCapture();
    }
    else if (m_pressed >= 0)
    {
        const int pressed = m_pressed;
        m_pressed = -1;
        m_staticDirty = true;
        ReleaseCapture();
        if (HitTest(pt) == pressed && ControlEnabled(m_controls[pressed]))
            RunAction(m_controls[pressed].id);
    }
}

void PanelView::OnMouseMove(POINT pt, WPARAM keys)
{
    if (m_drag >= 0)
    {
        const Control& c = m_controls[m_drag];
        if (c.kind == Kind::Pad)
        {
            SetPadFromPoint(c, ToPanel(pt));
        }
        else
        {
            const float fine = (keys & MK_SHIFT) ? 0.2f : 1.0f;
            const float delta = c.kind == Kind::Slider
                ? (pt.x - m_lastPt.x) / (m_scale * c.rc.Width)
                : (m_lastPt.y - pt.y) / (m_scale * 180.0f);
            m_dragValue = std::clamp(m_dragValue + delta * fine, 0.0f, 1.0f);
            m_lastPt = pt;
            EditParam(c.id, Quantize(c.id, m_dragValue));
        }
        Refresh();
        return;
    }
    const int hot = HitTest(pt);
    if (hot != m_hot)
    {
        m_hot = hot;
        m_staticDirty = true;
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);
        Refresh();
    }
}

bool PanelView::OnMouseWheel(POINT screenPt, short delta, WPARAM keys)
{
    POINT pt = screenPt;
    ScreenToClient(m_hwnd, &pt);
    const int hit = HitTest(pt);
    if (hit < 0)
        return false;
    const Control& c = m_controls[hit];
    if (!ControlEnabled(c) || (c.kind != Kind::Knob && c.kind != Kind::Slider && c.kind != Kind::Dropdown))
        return false;
    const int steps = Info(c.id).steps;
    const float notch = delta > 0 ? 1.0f : -1.0f;
    const float amount = steps > 1 ? notch / (steps - 1) : notch * ((keys & MK_SHIFT) ? 0.002f : 0.015f);
    m_host.BeginEdit(c.id);
    EditParam(c.id, std::clamp(m_host.GetParam(c.id) + amount, 0.0f, 1.0f));
    m_host.EndEdit(c.id);
    Refresh();
    return true;
}

void PanelView::OnTimer()
{
    // Values can change from outside (automation, host presets): redraw when they do.
    if (m_lastValues.size() != P_COUNT)
        m_lastValues.assign(P_COUNT, -1.0f);
    for (int id = 0; id < P_COUNT; ++id)
    {
        const float v = m_host.GetParam(id);
        if (v != m_lastValues[id])
        {
            m_lastValues[id] = v;
            m_staticDirty = true;
        }
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

LRESULT CALLBACK PanelView::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PanelView* self;
    if (msg == WM_NCCREATE)
    {
        self = static_cast<PanelView*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<PanelView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT PanelView::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    const POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    switch (msg)
    {
    case WM_PAINT:         OnPaint(); return 0;
    case WM_ERASEBKGND:    return 1;
    case WM_TIMER:         OnTimer(); return 0;
    case WM_LBUTTONDOWN:   OnLButtonDown(pt, false); Refresh(); return 0;
    case WM_LBUTTONDBLCLK: OnLButtonDown(pt, true); Refresh(); return 0;
    case WM_LBUTTONUP:     OnLButtonUp(pt); Refresh(); return 0;
    case WM_MOUSEMOVE:     OnMouseMove(pt, wp); return 0;
    case WM_MOUSELEAVE:    m_hot = -1; m_staticDirty = true; return 0;
    case WM_MOUSEWHEEL:
        if (OnMouseWheel(pt, GET_WHEEL_DELTA_WPARAM(wp), GET_KEYSTATE_WPARAM(wp)))
            return 0;
        break;
    case WM_GETDLGCODE:    return DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (!(lp & (1 << 30)) || KeyToSemitone(static_cast<UINT>(wp)) < 0)
            if (HandleKey(static_cast<UINT>(wp), true))
                return 0;
        if (KeyToSemitone(static_cast<UINT>(wp)) >= 0)
            return 0;
        break;
    case WM_KEYUP:
        if (HandleKey(static_cast<UINT>(wp), false))
            return 0;
        break;
    case WM_KILLFOCUS:
        ReleaseKeyboardNotes();
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
