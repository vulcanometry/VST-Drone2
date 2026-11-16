// PanelView.h - the editor: a custom-drawn Win32 child window (GDI+), shared by
// the standalone app and the VST3 plugin.
#pragma once

#include "PlatformWin.h"
#include "PanelHost.h"
#include "Presets.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

class PanelView
{
public:
    static constexpr float kPanelW = 1280.0f;
    static constexpr float kPanelH = 824.0f;

    explicit PanelView(PanelHost& host);
    ~PanelView();
    PanelView(const PanelView&) = delete;
    PanelView& operator=(const PanelView&) = delete;

    bool Create(HWND parent, int x, int y, float scale);
    void Destroy();
    HWND Hwnd() const { return m_hwnd; }

    static SIZE PixelSize(float scale);
    float Scale() const { return m_scale; }
    void SetScale(float scale);   // resizes the window

    void Refresh();                              // repaint; safe from any thread
    void SetStatus(const std::wstring& text);    // transient message in the header
    void OnPowerLost(const std::wstring& reason);
    void HandleMidiMessage(unsigned message);    // raw WinMM short message (app only)
    bool HandleKey(UINT vk, bool down);          // keys forwarded by a plugin host

    // Renders the editor to a PNG; used by the render tool to check the layout.
    bool SaveScreenshot(const std::wstring& path, int page);

private:
    enum class Kind { Knob, Dropdown, Toggle, LayerToggle, Slider, Pad, Button, Tab };
    enum Action
    {
        A_PATCH_PREV, A_PATCH_NEXT, A_PATCH_MENU, A_SAVE, A_SAVE_AS, A_RENAME, A_DELETE, A_LOAD, A_RANDOM, A_INIT,
        A_THEME, A_MIDI, A_REC, A_REC_AS, A_REC_FOLDER, A_POWER, A_TAB_FX, A_TAB_MOD
    };

    struct Control
    {
        Kind kind;
        int id;              // ParamId, or Action for Button and Tab
        Gdiplus::RectF rc;   // panel coordinates
        int page = -1;       // tab it lives on; -1 = always visible
        int accent = -1;     // layer whose colour it uses; -1 = the theme accent
        const wchar_t* text = nullptr;   // caption override
        const wchar_t* tip = nullptr;
    };

    struct Card
    {
        Gdiplus::RectF rc;
        const wchar_t* title;
        int page;
        int accent;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    // PanelView.cpp: layout and behaviour.
    void BuildLayout();
    void AddKnob(int id, float cx, float cy, int page, int accent = -1, float r = 18.0f);
    void AddDropdown(int id, const Gdiplus::RectF& rc, int page, int accent = -1);
    void AddButton(int action, const Gdiplus::RectF& rc, const wchar_t* text, const wchar_t* tip);
    bool Visible(const Control& c) const { return c.page < 0 || c.page == m_page; }
    int  LayerOf(int id) const;
    bool ControlEnabled(const Control& c);
    int  HitTest(POINT pt) const;
    Gdiplus::PointF ToPanel(POINT pt) const;
    HWND DialogOwner() const;

    void EditParam(int id, float value);
    void ApplyValues(const ParamValues& values);
    ParamValues CurrentValues();
    void RunAction(int action);
    void ShowDropdown(const Control& c);
    void ResetControl(const Control& c);
    void SetPadFromPoint(const Control& c, Gdiplus::PointF p);

    void ShowPatchMenu();
    void ShowThemeMenu();
    void ApplyPreset(int index);
    void StepPreset(int delta);
    void SavePatch(bool forceName);
    void RenamePatch();
    void DeletePatch();
    void LoadPatchFile();
    void SetCurrentPatch(const std::wstring& name, const std::wstring& path);
    bool WritePatch(const std::wstring& path);
    bool PromptForName(const wchar_t* title, std::wstring& name);

    void ToggleRecording(bool chooseFile);
    bool Recording();
    void ReleaseKeyboardNotes();

    void OnLButtonDown(POINT pt, bool doubleClick);
    void OnLButtonUp(POINT pt);
    void OnMouseMove(POINT pt, WPARAM keys);
    bool OnMouseWheel(POINT screenPt, short delta, WPARAM keys);
    void OnTimer();
    void OnPaint();

    // PanelDraw.cpp: rendering.
    void EnsureFonts();
    void RenderStatic(int width, int height);
    void RenderFrame(Gdiplus::Graphics& g);
    void DrawCard(Gdiplus::Graphics& g, const Card& card);
    void DrawControl(Gdiplus::Graphics& g, const Control& c, int index);
    void DrawKnob(Gdiplus::Graphics& g, const Control& c, int index);
    void DrawDropdown(Gdiplus::Graphics& g, const Control& c, int index);
    void DrawToggle(Gdiplus::Graphics& g, const Control& c, int index);
    void DrawLayerToggle(Gdiplus::Graphics& g, const Control& c, int index);
    void DrawSlider(Gdiplus::Graphics& g, const Control& c, int index);
    void DrawButton(Gdiplus::Graphics& g, const Control& c, int index);
    void DrawLogo(Gdiplus::Graphics& g);
    void DrawDynamic(Gdiplus::Graphics& g);
    void DrawLayerPreview(Gdiplus::Graphics& g, int layer);
    void DrawPad(Gdiplus::Graphics& g);
    void DrawScope(Gdiplus::Graphics& g);
    void DrawSpectrum(Gdiplus::Graphics& g);
    void DrawMeters(Gdiplus::Graphics& g);
    void DrawLfoBars(Gdiplus::Graphics& g);
    void DrawHeaderText(Gdiplus::Graphics& g);
    void Text(Gdiplus::Graphics& g, const std::wstring& s, Gdiplus::Font* font, const Gdiplus::RectF& rc,
              const Gdiplus::Color& color, Gdiplus::StringAlignment align = Gdiplus::StringAlignmentCenter);
    Gdiplus::Color AccentOf(int accent) const;
    std::wstring ValueText(int id);
    std::wstring CaptionFor(const Control& c);

    PanelHost& m_host;
    HWND m_hwnd = nullptr;
    bool m_gdiplus = false;
    float m_scale = 1.0f;

    PresetBank m_bank;
    int m_presetIndex = -1;
    std::wstring m_presetName, m_presetPath;
    bool m_presetDirty = false;
    unsigned m_randomSeed = 1;

    std::vector<Control> m_controls;
    std::vector<Card> m_cards;
    int m_page = 0;
    Gdiplus::RectF m_previewRect[4], m_padRect, m_scopeRect, m_specRect, m_meterRect, m_lfoBar[4], m_statusRect, m_patchRect;

    int m_drag = -1, m_pressed = -1, m_hot = -1;
    POINT m_lastPt{};
    float m_dragValue = 0.0f;

    std::vector<std::pair<UINT, int>> m_keyNotes;
    int m_octave = 4;

    std::wstring m_status;
    ULONGLONG m_statusUntil = 0;
    ULONGLONG m_lastPeakFall = 0;
    float m_meterL = 0.0f, m_meterR = 0.0f;

    std::vector<float> m_lastValues;
    bool m_staticDirty = true;
    std::vector<float> m_scopeData, m_spectrum;
    std::unique_ptr<Gdiplus::Bitmap> m_static, m_back;
    std::unique_ptr<Gdiplus::Font> m_fontLabel, m_fontValue, m_fontTitle, m_fontName, m_fontLogo, m_fontSmall;
};
