// PlatformWin.h - Windows + GDI+ includes shared by the core, the app and the plugin.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>

// GDI+ expects min/max, which NOMINMAX removes from windows.h.
#include <algorithm>
namespace Gdiplus
{
    using std::min;
    using std::max;
}
#include <gdiplus.h>
