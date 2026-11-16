// PanelDraw.cpp - editor rendering. Cards and controls go into a cached bitmap that
// is only redrawn when something changes; the live displays are drawn over it.
#include "PanelView.h"
#include "PanelTheme.h"
#include "Recorder.h"
#include "SynthEngine.h"
#include "Wavetables.h"

#include <cmath>
#include <cstdio>

using namespace Gdiplus;
using namespace params;
using theme::Alpha;
using theme::FillRound;
using theme::Mix;
using theme::StrokeRound;

namespace
{
constexpr float kPi = 3.14159265358979f;
inline PointF Polar(float cx, float cy, float r, float deg)
{
    const float a = deg * kPi / 180.0f;
    return PointF(cx + sinf(a) * r, cy - cosf(a) * r);
}
} // namespace

void PanelView::EnsureFonts()
{
    if (m_fontLabel)
        return;
    const FontFamily family(L"Segoe UI");
    auto make = [&family](float size, int style) { return std::make_unique<Font>(&family, size, style, UnitPixel); };
    m_fontLabel = make(10.0f, FontStyleBold);
    m_fontValue = make(10.5f, FontStyleRegular);
    m_fontTitle = make(11.5f, FontStyleBold);
    m_fontName = make(14.0f, FontStyleBold);
    m_fontLogo = make(23.0f, FontStyleBold);
    m_fontSmall = make(10.5f, FontStyleRegular);
}

Color PanelView::AccentOf(int accent) const
{
    return accent >= 0 ? theme::P().layer[accent] : theme::P().accent;
}

void PanelView::Text(Graphics& g, const std::wstring& s, Font* font, const RectF& rc, const Color& color, StringAlignment align)
{
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush brush(color);
    g.DrawString(s.c_str(), -1, font, rc, &format, &brush);
}

// Values whose meaning depends on the layer's algorithm.
std::wstring PanelView::ValueText(int id)
{
    const float n = m_host.GetParam(id);
    const int layer = LayerOf(id);
    if (layer >= 0)
    {
        const int algo = ToStep(ALG_COUNT, m_host.GetParam(LayerId(layer, LP_ALGO)));
        const bool timbre = id == LayerId(layer, LP_TIMBRE), color = id == LayerId(layer, LP_COLOR);
        if (algo == ALG_MORPH && (timbre || color))
            return wavetables::ClassicWaveName(static_cast<int>(n * (wavetables::kClassicWaves - 1) + 0.5f));
        if (algo == ALG_FM && timbre)
        {
            static const float ratios[] = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f, 6.0f, 7.0f, 9.0f };
            wchar_t text[16];
            swprintf_s(text, L"x%.1f", ratios[static_cast<int>(n * 11.0f + 0.5f)]);
            return text;
        }
    }
    return Format(id, n);
}

std::wstring PanelView::CaptionFor(const Control& c)
{
    if (c.text)
        return c.text;
    const int layer = LayerOf(c.id);
    if (layer >= 0)
    {
        const wchar_t* morph; const wchar_t* timbre; const wchar_t* color;
        AlgorithmLabels(ToStep(ALG_COUNT, m_host.GetParam(LayerId(layer, LP_ALGO))), morph, timbre, color);
        if (c.id == LayerId(layer, LP_MORPH)) return morph;
        if (c.id == LayerId(layer, LP_TIMBRE)) return timbre;
        if (c.id == LayerId(layer, LP_COLOR)) return color;
    }
    return Info(c.id).label;
}

// ---------------------------------------------------------------------------
// Static layer
// ---------------------------------------------------------------------------
void PanelView::DrawCard(Graphics& g, const Card& card)
{
    const theme::Palette& p = theme::P();
    FillRound(g, card.rc, 9.0f, p.card);
    StrokeRound(g, card.rc, 9.0f, p.cardEdge);
    if (card.accent < 0)
        Text(g, card.title, m_fontTitle.get(), RectF(card.rc.X + 12, card.rc.Y + 6, 140, 20), p.label, StringAlignmentNear);
}

void PanelView::DrawKnob(Graphics& g, const Control& c, int index)
{
    const theme::Palette& p = theme::P();
    const ParamInfo& info = Info(c.id);
    const bool enabled = ControlEnabled(c);
    const float value = m_host.GetParam(c.id);
    const float cx = c.rc.X + c.rc.Width * 0.5f, cy = c.rc.Y + c.rc.Height * 0.5f, r = c.rc.Width * 0.5f;
    const bool active = index == m_drag || index == m_hot;
    const Color accent = enabled ? AccentOf(c.accent) : p.dim;

    // Arc track and value arc: flat and clean, in the style of modern plug-ins.
    const RectF arc(cx - r, cy - r, r * 2.0f, r * 2.0f);
    Pen track(p.track, 4.0f);
    track.SetStartCap(LineCapRound);
    track.SetEndCap(LineCapRound);
    g.DrawArc(&track, arc, 135.0f, 270.0f);

    const float start = info.bipolar ? 270.0f : 135.0f;
    const float sweep = info.bipolar ? (value - 0.5f) * 270.0f : value * 270.0f;
    if (fabsf(sweep) > 0.5f)
    {
        Pen pen(accent, 4.0f);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        g.DrawArc(&pen, arc, start, sweep);
    }

    // Cap and pointer.
    const float capR = r - 6.0f;
    SolidBrush cap(active ? p.buttonHot : p.button);
    g.FillEllipse(&cap, cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);
    const float deg = -135.0f + 270.0f * value;
    Pen pointer(enabled ? p.text : p.dim, 2.0f);
    pointer.SetStartCap(LineCapRound);
    pointer.SetEndCap(LineCapRound);
    g.DrawLine(&pointer, Polar(cx, cy, capR * 0.35f, deg), Polar(cx, cy, capR * 0.9f, deg));

    // Caption above; the value replaces nothing, it sits below.
    const RectF labelRc(cx - 34.0f, cy - r - 15.0f, 68.0f, 13.0f);
    const RectF valueRc(cx - 34.0f, cy + r + 1.0f, 68.0f, 13.0f);
    Text(g, CaptionFor(c), m_fontLabel.get(), labelRc, enabled ? p.label : p.dim);
    Text(g, ValueText(c.id), m_fontValue.get(), valueRc, enabled ? (active ? p.text : p.dim) : Alpha(p.dim, 120));
}

void PanelView::DrawDropdown(Graphics& g, const Control& c, int index)
{
    const theme::Palette& p = theme::P();
    const bool enabled = ControlEnabled(c);
    const bool active = index == m_hot && enabled;
    FillRound(g, c.rc, 5.0f, active ? p.buttonHot : p.button);
    const float mid = c.rc.Y + c.rc.Height * 0.5f;
    Text(g, ValueText(c.id), m_fontValue.get(), RectF(c.rc.X + 7, c.rc.Y, c.rc.Width - 22, c.rc.Height), enabled ? p.text : p.dim, StringAlignmentNear);
    Pen chevron(enabled ? AccentOf(c.accent) : p.dim, 1.5f);
    const float x = c.rc.GetRight() - 11.0f;
    g.DrawLine(&chevron, x - 3.0f, mid - 1.5f, x, mid + 1.5f);
    g.DrawLine(&chevron, x, mid + 1.5f, x + 3.0f, mid - 1.5f);
}

void PanelView::DrawToggle(Graphics& g, const Control& c, int index)
{
    const theme::Palette& p = theme::P();
    const bool on = m_host.GetParam(c.id) >= 0.5f;
    const Color accent = AccentOf(c.accent);
    FillRound(g, c.rc, c.rc.Height * 0.5f, on ? accent : (index == m_hot ? p.buttonHot : p.button));
    Text(g, c.text ? c.text : Info(c.id).label, m_fontLabel.get(), c.rc, on ? p.onAccent : p.label);
}

void PanelView::DrawLayerToggle(Graphics& g, const Control& c, int index)
{
    const theme::Palette& p = theme::P();
    const int layer = LayerOf(c.id);
    const bool on = m_host.GetParam(c.id) >= 0.5f;
    const Color accent = p.layer[layer];
    const float size = c.rc.Width;
    if (on)
    {
        SolidBrush fill(accent);
        g.FillEllipse(&fill, c.rc);
    }
    else
    {
        SolidBrush fill(index == m_hot ? p.buttonHot : p.button);
        g.FillEllipse(&fill, c.rc);
        Pen ring(p.dim, 1.2f);
        g.DrawEllipse(&ring, c.rc.X + 0.5f, c.rc.Y + 0.5f, size - 1.0f, size - 1.0f);
    }
    static const wchar_t* const letters[kNumLayers] = { L"A", L"B", L"C", L"D" };
    Text(g, letters[layer], m_fontTitle.get(), c.rc, on ? p.onAccent : p.dim);
}

void PanelView::DrawSlider(Graphics& g, const Control& c, int index)
{
    const theme::Palette& p = theme::P();
    const float value = m_host.GetParam(c.id);
    const bool active = index == m_drag || index == m_hot;
    FillRound(g, c.rc, 5.0f, p.well);
    const float mid = c.rc.X + c.rc.Width * 0.5f;
    const float x = c.rc.X + c.rc.Width * value;
    const RectF bar(min(mid, x), c.rc.Y + 1.0f, fabsf(x - mid), c.rc.Height - 2.0f);
    if (bar.Width > 0.5f)
    {
        SolidBrush fill(Alpha(p.accent, active ? 220 : 170));
        g.FillRectangle(&fill, bar);
    }
    Pen center(p.track, 1.0f);
    g.DrawLine(&center, mid, c.rc.Y + 2.0f, mid, c.rc.GetBottom() - 2.0f);
    Text(g, ValueText(c.id), m_fontValue.get(), c.rc, p.text);
}

void PanelView::DrawButton(Graphics& g, const Control& c, int index)
{
    const theme::Palette& p = theme::P();
    const bool enabled = ControlEnabled(c);
    const bool pressed = index == m_pressed;
    const bool hot = index == m_hot && enabled;

    if (c.kind == Kind::Tab)
    {
        const bool selected = (c.id == A_TAB_FX && m_page == 0) || (c.id == A_TAB_MOD && m_page == 1);
        FillRound(g, c.rc, 6.0f, selected ? p.accent : (hot ? p.buttonHot : p.button));
        Text(g, c.text, m_fontLabel.get(), c.rc, selected ? p.onAccent : p.label);
        return;
    }

    Color fill = pressed ? p.accent : (hot ? p.buttonHot : p.button);
    Color ink = pressed ? p.onAccent : (enabled ? p.text : p.dim);

    if (c.id == A_POWER && m_host.IsPowered())
    {
        fill = p.accent;
        ink = p.onAccent;
    }
    if (c.id == A_REC && Recording())
    {
        fill = p.record;
        ink = Color(255, 255, 255, 255);
    }
    FillRound(g, c.rc, 6.0f, fill);

    if (c.id == A_PATCH_MENU)
    {
        std::wstring name = m_presetName;
        if (m_presetDirty)
            name += L" *";
        Text(g, name, m_fontName.get(), RectF(c.rc.X + 9, c.rc.Y, c.rc.Width - 26, c.rc.Height), p.text, StringAlignmentNear);
        Pen chevron(p.accent, 1.6f);
        const float x = c.rc.GetRight() - 13.0f, y = c.rc.Y + c.rc.Height * 0.5f;
        g.DrawLine(&chevron, x - 4.0f, y - 2.0f, x, y + 2.0f);
        g.DrawLine(&chevron, x, y + 2.0f, x + 4.0f, y - 2.0f);
        return;
    }
    if (c.id == A_MIDI)
    {
        Text(g, c.text, m_fontLabel.get(), c.rc, ink);
        return;
    }
    if (c.id == A_PATCH_PREV || c.id == A_PATCH_NEXT)
    {
        Text(g, c.text, m_fontLogo.get(), RectF(c.rc.X, c.rc.Y - 2.0f, c.rc.Width, c.rc.Height), ink);
        return;
    }
    Text(g, c.text, m_fontLabel.get(), c.rc, ink);
}

void PanelView::DrawControl(Graphics& g, const Control& c, int index)
{
    switch (c.kind)
    {
    case Kind::Knob:        DrawKnob(g, c, index); break;
    case Kind::Dropdown:    DrawDropdown(g, c, index); break;
    case Kind::Toggle:      DrawToggle(g, c, index); break;
    case Kind::LayerToggle: DrawLayerToggle(g, c, index); break;
    case Kind::Slider:      DrawSlider(g, c, index); break;
    case Kind::Button:
    case Kind::Tab:         DrawButton(g, c, index); break;
    case Kind::Pad:         break;   // drawn with the live displays
    }
}

void PanelView::DrawLogo(Graphics& g)
{
    const theme::Palette& p = theme::P();
    Font* font = m_fontLogo.get();
    LinearGradientBrush brush(PointF(12, 0), PointF(130, 0), p.accent, p.accent2);
    StringFormat format;
    format.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"DRONE 2", -1, font, RectF(10, 8, 130, 34), &format, &brush);
    Text(g, L"Layered drone synth", m_fontSmall.get(), RectF(12, 40, 130, 16), p.dim, StringAlignmentNear);
}

void PanelView::RenderStatic(int width, int height)
{
    if (!m_static || static_cast<int>(m_static->GetWidth()) != width || static_cast<int>(m_static->GetHeight()) != height)
        m_static = std::make_unique<Bitmap>(width, height, PixelFormat32bppPARGB);
    Graphics g(m_static.get());
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.ScaleTransform(m_scale, m_scale);

    const theme::Palette& p = theme::P();
    LinearGradientBrush bg(PointF(0, 0), PointF(0, kPanelH), p.bgTop, p.bg);
    g.FillRectangle(&bg, 0.0f, 0.0f, kPanelW, kPanelH);
    DrawLogo(g);

    for (const Card& card : m_cards)
        if (card.page < 0 || card.page == m_page)
            DrawCard(g, card);
    for (int i = 0; i < static_cast<int>(m_controls.size()); ++i)
        if (Visible(m_controls[i]))
            DrawControl(g, m_controls[i], i);
    m_staticDirty = false;
}

// ---------------------------------------------------------------------------
// Live displays
// ---------------------------------------------------------------------------
void PanelView::DrawLayerPreview(Graphics& g, int l)
{
    const theme::Palette& p = theme::P();
    const RectF& rc = m_previewRect[l];
    FillRound(g, rc, 5.0f, p.well);

    const bool on = m_host.GetParam(LayerId(l, LP_ON)) >= 0.5f;
    const int algo = ToStep(ALG_COUNT, m_host.GetParam(LayerId(l, LP_ALGO)));
    const Color color = on ? p.layer[l] : p.dim;
    SynthEngine* engine = m_host.Engine();
    const float morph = engine ? engine->LayerMorph(l) : m_host.GetParam(LayerId(l, LP_MORPH));

    const bool hasWave = algo == ALG_MORPH || algo == ALG_WAVETABLE || algo == ALG_SWARM || algo == ALG_CLOUD;
    if (!hasWave)
    {
        // Algorithms with no single waveform to show get their description and a live morph bar.
        Text(g, AlgorithmBlurb(algo), m_fontSmall.get(), RectF(rc.X + 8, rc.Y + 2, rc.Width - 16, rc.Height - 14), on ? p.label : p.dim);
        const RectF track(rc.X + 10, rc.GetBottom() - 10, rc.Width - 20, 3);
        FillRound(g, track, 1.5f, p.track);
        FillRound(g, RectF(track.X, track.Y, max(track.Width * morph, 3.0f), track.Height), 1.5f, color);
        return;
    }

    using namespace wavetables;
    constexpr int kPoints = 64;
    constexpr int kMip = 3;
    const int len = MipLength(kMip);
    PointF points[kPoints];
    const int tableIndex = algo == ALG_WAVETABLE || algo == ALG_CLOUD ? ToStep(kNumTables, m_host.GetParam(LayerId(l, LP_TABLE))) : 0;
    const Table& table = Get(tableIndex);
    const float timbre = m_host.GetParam(LayerId(l, LP_TIMBRE)), colorKnob = m_host.GetParam(LayerId(l, LP_COLOR));
    auto frameSample = [&](float pos, int s)
    {
        const int f0 = static_cast<int>(pos);
        const int f1 = (std::min)(f0 + 1, table.frames - 1);
        const float a = table.Mip(f0, kMip)[s], b = table.Mip(f1, kMip)[s];
        return a + (b - a) * (pos - f0);
    };
    for (int i = 0; i < kPoints; ++i)
    {
        const int s = i * len / kPoints;
        float v;
        if (algo == ALG_MORPH)
        {
            const float a = frameSample(timbre * (kClassicWaves - 1), s);
            const float b = frameSample(colorKnob * (kClassicWaves - 1), s);
            v = a + (b - a) * morph;
        }
        else if (algo == ALG_SWARM)
        {
            v = frameSample(morph * 3.0f, s);
        }
        else
        {
            v = frameSample(morph * (table.frames - 1), s);
        }
        points[i] = PointF(rc.X + 6 + (rc.Width - 12) * i / (kPoints - 1), rc.Y + rc.Height * 0.5f - v * (rc.Height * 0.42f));
    }
    Pen line(color, 1.5f);
    line.SetLineJoin(LineJoinRound);
    g.DrawLines(&line, points, kPoints);
}

void PanelView::DrawPad(Graphics& g)
{
    const theme::Palette& p = theme::P();
    const RectF& rc = m_padRect;
    FillRound(g, rc, 6.0f, p.well);

    // A soft corner glow per layer, brighter the closer the mix is to that corner.
    SynthEngine* engine = m_host.Engine();
    const float vx = engine ? engine->VectorX() : m_host.GetParam(P_VEC_X);
    const float vy = engine ? engine->VectorY() : m_host.GetParam(P_VEC_Y);
    const float weights[kNumLayers] = { (1 - vx) * (1 - vy), vx * (1 - vy), (1 - vx) * vy, vx * vy };
    static const wchar_t* const letters[kNumLayers] = { L"A", L"B", L"C", L"D" };
    for (int l = 0; l < kNumLayers; ++l)
    {
        const bool on = m_host.GetParam(LayerId(l, LP_ON)) >= 0.5f;
        const float cx = l & 1 ? rc.GetRight() - 13 : rc.X + 13, cy = l & 2 ? rc.GetBottom() - 13 : rc.Y + 13;
        if (on)
        {
            SolidBrush glow(Alpha(p.layer[l], 40 + static_cast<int>(180 * weights[l])));
            const float r = 9.0f + 6.0f * weights[l];
            g.FillEllipse(&glow, cx - r, cy - r, r * 2, r * 2);
        }
        Text(g, letters[l], m_fontLabel.get(), RectF(cx - 10, cy - 8, 20, 16), on ? p.text : p.dim);
    }

    Pen grid(p.track, 1.0f);
    g.DrawLine(&grid, rc.X + rc.Width * 0.5f, rc.Y + 6, rc.X + rc.Width * 0.5f, rc.GetBottom() - 6);
    g.DrawLine(&grid, rc.X + 6, rc.Y + rc.Height * 0.5f, rc.GetRight() - 6, rc.Y + rc.Height * 0.5f);

    // The set position is a ring; the moving dot is where the orbit has taken it.
    const float sx = rc.X + m_host.GetParam(P_VEC_X) * rc.Width, sy = rc.Y + m_host.GetParam(P_VEC_Y) * rc.Height;
    Pen ring(p.label, 1.2f);
    g.DrawEllipse(&ring, sx - 6, sy - 6, 12.0f, 12.0f);
    const float dx = rc.X + vx * rc.Width, dy = rc.Y + vy * rc.Height;
    SolidBrush halo(Alpha(p.accent, 70));
    g.FillEllipse(&halo, dx - 8, dy - 8, 16.0f, 16.0f);
    SolidBrush dot(p.accent);
    g.FillEllipse(&dot, dx - 4, dy - 4, 8.0f, 8.0f);
}

void PanelView::DrawScope(Graphics& g)
{
    const theme::Palette& p = theme::P();
    const RectF& rc = m_scopeRect;
    FillRound(g, rc, 5.0f, p.well);
    SynthEngine* engine = m_host.Engine();
    if (!engine)
        return;
    constexpr int kWindow = 1024, kPoints = 200;
    m_scopeData.resize(kWindow * 2);
    engine->CopyScope(m_scopeData.data(), kWindow * 2);

    // Trigger on a rising zero crossing, so the trace stands still.
    int start = 0;
    for (int i = 1; i < kWindow; ++i)
        if (m_scopeData[i - 1] <= 0.0f && m_scopeData[i] > 0.0f)
        {
            start = i;
            break;
        }

    PointF points[kPoints];
    for (int i = 0; i < kPoints; ++i)
    {
        const float v = (std::clamp)(m_scopeData[start + i * kWindow / kPoints], -1.0f, 1.0f);
        points[i] = PointF(rc.X + 4 + (rc.Width - 8) * i / (kPoints - 1), rc.Y + rc.Height * 0.5f - v * (rc.Height * 0.46f));
    }
    Pen line(p.accent, 1.4f);
    line.SetLineJoin(LineJoinRound);
    g.DrawLines(&line, points, kPoints);
}

void PanelView::DrawSpectrum(Graphics& g)
{
    const theme::Palette& p = theme::P();
    const RectF& rc = m_specRect;
    FillRound(g, rc, 5.0f, p.well);
    SynthEngine* engine = m_host.Engine();
    if (!engine)
        return;
    constexpr int kSize = 2048, kBars = 64;
    std::vector<double> re(kSize), im(kSize, 0.0);
    std::vector<float> samples(kSize);
    engine->CopyScope(samples.data(), kSize);
    for (int i = 0; i < kSize; ++i)
        re[i] = samples[i] * (0.5 - 0.5 * cos(2.0 * 3.14159265358979 * i / (kSize - 1)));
    wavetables::Fft(re, im, false);

    if (m_spectrum.size() != kBars)
        m_spectrum.assign(kBars, 0.0f);
    constexpr float kLowHz = 25.0f, kHighHz = 18000.0f, kRate = 48000.0f;
    const float binHz = kRate / kSize;
    const float barW = rc.Width / kBars;
    PointF points[kBars + 2];
    for (int b = 0; b < kBars; ++b)
    {
        const float f0 = kLowHz * powf(kHighHz / kLowHz, static_cast<float>(b) / kBars);
        const float f1 = kLowHz * powf(kHighHz / kLowHz, static_cast<float>(b + 1) / kBars);
        const int i0 = (std::max)(1, static_cast<int>(f0 / binHz));
        const int i1 = (std::min)(kSize / 2 - 1, (std::max)(i0, static_cast<int>(f1 / binHz)));
        float power = 0.0f;
        for (int i = i0; i <= i1; ++i)
            power = (std::max)(power, static_cast<float>(re[i] * re[i] + im[i] * im[i]));
        const float db = 10.0f * log10f(power + 1e-6f) - 50.0f;
        const float target = (std::clamp)((db + 66.0f) / 66.0f, 0.0f, 1.0f);
        m_spectrum[b] += (target - m_spectrum[b]) * (target > m_spectrum[b] ? 0.5f : 0.12f);
        points[b + 1] = PointF(rc.X + barW * (b + 0.5f), rc.GetBottom() - 2 - m_spectrum[b] * (rc.Height - 6));
    }
    points[0] = PointF(rc.X, rc.GetBottom());
    points[kBars + 1] = PointF(rc.GetRight(), rc.GetBottom());
    LinearGradientBrush fill(PointF(0, rc.Y), PointF(0, rc.GetBottom()), Alpha(p.accent2, 200), Alpha(p.accent, 30));
    g.FillPolygon(&fill, points, kBars + 2);
}

void PanelView::DrawMeters(Graphics& g)
{
    const theme::Palette& p = theme::P();
    SynthEngine* engine = m_host.Engine();
    const float peakL = engine && m_host.IsPowered() ? engine->PeakL() : 0.0f;
    const float peakR = engine && m_host.IsPowered() ? engine->PeakR() : 0.0f;
    m_meterL = (std::max)(peakL, m_meterL * 0.9f);
    m_meterR = (std::max)(peakR, m_meterR * 0.9f);
    auto level = [](float v) { return (std::clamp)((20.0f * log10f(v + 1e-5f) + 48.0f) / 48.0f, 0.0f, 1.0f); };
    const RectF& rc = m_meterRect;
    const float w = (rc.Width - 2.0f) * 0.5f;
    FillRound(g, RectF(rc.X, rc.Y, w, rc.Height), 2.0f, p.well);
    FillRound(g, RectF(rc.X + w + 2, rc.Y, w, rc.Height), 2.0f, p.well);
    const float hL = rc.Height * level(m_meterL), hR = rc.Height * level(m_meterR);
    SolidBrush fill(p.accent);
    if (hL > 0.5f) g.FillRectangle(&fill, rc.X, rc.GetBottom() - hL, w, hL);
    if (hR > 0.5f) g.FillRectangle(&fill, rc.X + w + 2, rc.GetBottom() - hR, w, hR);
}

void PanelView::DrawLfoBars(Graphics& g)
{
    const theme::Palette& p = theme::P();
    SynthEngine* engine = m_host.Engine();
    for (int i = 0; i < kNumLfos; ++i)
    {
        const RectF& rc = m_lfoBar[i];
        FillRound(g, rc, 3.0f, p.well);
        const float v = engine ? engine->LfoValue(i) : 0.0f;
        const float mid = rc.X + rc.Width * 0.5f;
        const float x = mid + v * rc.Width * 0.5f;
        SolidBrush fill(p.accent);
        g.FillRectangle(&fill, min(mid, x), rc.Y + 1.0f, fabsf(x - mid), rc.Height - 2.0f);
    }
}

void PanelView::DrawHeaderText(Graphics& g)
{
    const theme::Palette& p = theme::P();
    std::wstring line;
    Color color = p.dim;
    const ULONGLONG now = GetTickCount64();
    Recorder* recorder = m_host.GetRecorder();
    if (recorder && recorder->IsRecording())
    {
        const int seconds = static_cast<int>(recorder->Seconds());
        wchar_t text[64];
        swprintf_s(text, L"●  Recording  %d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60);
        line = text;
        color = p.record;
        if (recorder->HasError())
            line = L"Recording error: the file could not be written";
    }
    else if (now < m_statusUntil)
    {
        line = m_status;
        color = p.label;
    }
    else if (m_hot >= 0 || m_drag >= 0)
    {
        const Control& c = m_controls[m_drag >= 0 ? m_drag : m_hot];
        if (c.kind == Kind::Button || c.kind == Kind::Tab)
        {
            if (c.tip)
                line = c.tip;
            if (c.id == A_MIDI)
                line = L"MIDI input: " + m_host.MidiInputName() + L"  (click to change)";
        }
        else if (c.kind == Kind::Pad)
            line = L"Vector mix: drag to blend the four layers. Double-click to centre.";
        else
            line = std::wstring(Info(c.id).name) + L":  " + ValueText(c.id);
    }
    if (!line.empty())
        Text(g, line, m_fontSmall.get(), m_statusRect, color, StringAlignmentNear);
}

void PanelView::DrawDynamic(Graphics& g)
{
    for (int l = 0; l < kNumLayers; ++l)
        DrawLayerPreview(g, l);
    DrawPad(g);
    DrawScope(g);
    DrawSpectrum(g);
    DrawMeters(g);
    if (m_page == 1)
        DrawLfoBars(g);
    DrawHeaderText(g);
}

void PanelView::RenderFrame(Graphics& g)
{
    g.DrawImage(m_static.get(), 0, 0);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.ScaleTransform(m_scale, m_scale);
    DrawDynamic(g);
    g.ResetTransform();
}

void PanelView::OnPaint()
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int width = rc.right - rc.left, height = rc.bottom - rc.top;
    if (width > 0 && height > 0)
    {
        EnsureFonts();
        if (m_staticDirty || !m_static || static_cast<int>(m_static->GetWidth()) != width || static_cast<int>(m_static->GetHeight()) != height)
            RenderStatic(width, height);
        if (!m_back || static_cast<int>(m_back->GetWidth()) != width || static_cast<int>(m_back->GetHeight()) != height)
            m_back = std::make_unique<Bitmap>(width, height, PixelFormat32bppPARGB);
        {
            Graphics g(m_back.get());
            RenderFrame(g);
        }
        Graphics screen(dc);
        screen.DrawImage(m_back.get(), 0, 0);
    }
    EndPaint(m_hwnd, &ps);
}

bool PanelView::SaveScreenshot(const std::wstring& path, int page)
{
    m_page = page;
    const SIZE size = PixelSize(m_scale);
    EnsureFonts();
    RenderStatic(size.cx, size.cy);
    Bitmap out(size.cx, size.cy, PixelFormat32bppPARGB);
    {
        Graphics g(&out);
        RenderFrame(g);
    }
    // {557CF406-1A04-11D3-9A73-0000F81EF32E} is GDI+'s PNG encoder.
    const CLSID png = { 0x557CF406, 0x1A04, 0x11D3, { 0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E } };
    return out.Save(path.c_str(), &png, nullptr) == Ok;
}
