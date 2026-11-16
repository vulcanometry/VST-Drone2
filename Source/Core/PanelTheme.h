// PanelTheme.h - colour themes and drawing helpers for the editor.
#pragma once

#include "PlatformWin.h"

#include <cmath>

namespace theme
{
using Gdiplus::Color;

struct Palette
{
    const wchar_t* name;
    Color bg, bgTop, card, cardEdge, well, track;
    Color text, label, dim;
    Color accent, accent2;
    Color layer[4];
    Color button, buttonHot, onAccent;
    Color record;
};

int Count();
const wchar_t* Name(int index);
int Current();
const Palette& P();
void Apply(int index);       // switches the palette and saves the choice
void LoadSaved();            // applies the theme saved in %APPDATA%\Drone 2\Settings.txt

inline Color Alpha(const Color& c, int a) { return Color(static_cast<BYTE>(a), c.GetR(), c.GetG(), c.GetB()); }
inline Color Mix(const Color& a, const Color& b, float t)
{
    auto m = [t](BYTE x, BYTE y) { return static_cast<BYTE>(x + (y - x) * t); };
    return Color(m(a.GetA(), b.GetA()), m(a.GetR(), b.GetR()), m(a.GetG(), b.GetG()), m(a.GetB(), b.GetB()));
}

inline void RoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, float radius)
{
    const float d = std::min(radius * 2.0f, std::min(r.Width, r.Height));
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

inline void FillRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const Color& c)
{
    Gdiplus::GraphicsPath path;
    RoundRect(path, r, radius);
    Gdiplus::SolidBrush b(c);
    g.FillPath(&b, &path);
}

inline void StrokeRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const Color& c, float width = 1.0f)
{
    Gdiplus::GraphicsPath path;
    RoundRect(path, r, radius);
    Gdiplus::Pen p(c, width);
    g.DrawPath(&p, &path);
}
} // namespace theme
