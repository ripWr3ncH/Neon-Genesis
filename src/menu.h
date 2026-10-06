// ===========================================================================
//  menu.h  -  the PAUSE MENU (press ESC)
//
//  OpenGL has no built-in way to draw text. Instead of adding a font
//  library, the whole menu - dark panel, title, the list of every control,
//  and the RESUME / EXIT buttons - is drawn ONCE into an image with Windows
//  GDI+ (the same library that loads the billboard pictures), uploaded as a
//  texture, and then simply drawn over the finished frame as a
//  see-through fullscreen quad while the game is paused.
//
//  Three versions are made: no button highlighted, RESUME highlighted and
//  EXIT highlighted. Which one is drawn depends on where the mouse is, so
//  the buttons light up when hovered.
// ===========================================================================

#pragma once

#include <glad/glad.h>
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <cstring>

// Menu image size. It is stretched over the whole window, so the button
// positions below are kept as FRACTIONS of it for the mouse hit-test.
static const int MENU_W = 1600, MENU_H = 900;

struct MenuButton { float x0, y0, x1, y1; };          // fractions of the image, y from the top
static const MenuButton MENU_RESUME = { 480.0f / MENU_W, 712.0f / MENU_H, 770.0f / MENU_W, 782.0f / MENU_H };
static const MenuButton MENU_EXIT   = { 830.0f / MENU_W, 712.0f / MENU_H, 1120.0f / MENU_W, 782.0f / MENU_H };

inline bool menuHit(const MenuButton& b, double fx, double fy) {
    return fx >= b.x0 && fx <= b.x1 && fy >= b.y0 && fy <= b.y1;
}

struct PauseMenu {
    GLuint tex[3] = { 0, 0, 0 };     // 0 = plain, 1 = RESUME hovered, 2 = EXIT hovered
};

// Draw one version of the menu into a bitmap and upload it.
inline GLuint buildMenuTexture(int hover) {
    using namespace Gdiplus;
    Bitmap bmp(MENU_W, MENU_H, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    const Color RED(255, 255, 77, 90), CYAN(255, 79, 240, 255), TEXTC(255, 225, 232, 240), DIM(255, 150, 150, 165);

    // Darken the frozen scene behind the menu, then the panel itself.
    g.Clear(Color(150, 4, 4, 10));
    SolidBrush panel(Color(238, 16, 10, 16));
    g.FillRectangle(&panel, 300, 70, 1000, 760);
    Pen border(RED, 2.0f), thin(Color(120, 255, 77, 90), 1.0f);
    g.DrawRectangle(&border, 300, 70, 1000, 760);
    g.DrawLine(&thin, 340, 205, 1260, 205);
    g.DrawLine(&thin, 340, 680, 1260, 680);
    SolidBrush redB(RED), cyanB(CYAN), textB(TEXTC), dimB(DIM);   // (not "TEXT": windows.h macro)
    g.FillRectangle(&redB, 300, 70, 8, 760);

    // Fonts: Bahnschrift ships with Windows 10/11; fall back to Segoe UI.
    FontFamily bahn(L"Bahnschrift"), segoe(L"Segoe UI");
    const FontFamily* fam = bahn.IsAvailable() ? &bahn : &segoe;
    Font title(fam, 64, FontStyleBold, UnitPixel);
    Font sub(fam, 20, FontStyleRegular, UnitPixel);
    Font keyF(fam, 21, FontStyleBold, UnitPixel);
    Font actF(fam, 21, FontStyleRegular, UnitPixel);
    Font btnF(fam, 30, FontStyleBold, UnitPixel);

    StringFormat centre;
    centre.SetAlignment(StringAlignmentCenter);
    centre.SetLineAlignment(StringAlignmentCenter);

    g.DrawString(L"PAUSED", -1, &title, RectF(300, 88, 1000, 80), &centre, &redB);
    g.DrawString(L"NEON GENESIS   \x00B7   ALL CONTROLS", -1, &sub, RectF(300, 160, 1000, 34), &centre, &dimB);

    // The full control list, in two columns: key (cyan) and what it does.
    const wchar_t* left[][2] = {
        { L"W A S D",        L"fly the camera" },
        { L"MOUSE",          L"look around" },
        { L"SPACE / CTRL",   L"up / down" },
        { L"SHIFT",          L"move faster" },
        { L"C",              L"get in / out of a car" },
        { L"W S  A D",       L"drive: speed, steer" },
        { L"SPACE",          L"handbrake (driving)" },
        { L"V",              L"camera tour" },
        { L"E",              L"dusk  \x2194  night" },
        { L"N",              L"generate a new city" },
        { L"TAB",            L"free / lock the cursor" },
    };
    const wchar_t* right[][2] = {
        { L"1  2  3",        L"flat / Gouraud / Phong" },
        { L"H",              L"ray-traced shadows" },
        { L"B",              L"bloom (glow)" },
        { L"G",              L"road reflections" },
        { L"R",              L"rain" },
        { L"X",              L"detail effects" },
        { L"L",              L"show light positions" },
        { L"F",              L"wireframe" },
        { L"P",              L"pause animation" },
        { L"M  /  T",        L"music on-off / next" },
        { L"F11  /  F12",    L"fullscreen / screenshot" },
    };
    for (int i = 0; i < 11; ++i) {
        float y = 228.0f + i * 40.0f;
        g.DrawString(left[i][0],  -1, &keyF, PointF(360, y), &cyanB);
        g.DrawString(left[i][1],  -1, &actF, PointF(530, y), &textB);
        g.DrawString(right[i][0], -1, &keyF, PointF(820, y), &cyanB);
        g.DrawString(right[i][1], -1, &actF, PointF(960, y), &textB);
    }

    // Buttons. The hovered one is filled; the other is an outline.
    auto button = [&](const MenuButton& b, const wchar_t* label, bool hot, const Color& c) {
        RectF r(b.x0 * MENU_W, b.y0 * MENU_H, (b.x1 - b.x0) * MENU_W, (b.y1 - b.y0) * MENU_H);
        SolidBrush fill(hot ? c : Color(60, c.GetR(), c.GetG(), c.GetB()));
        Pen edge(c, 2.0f);
        g.FillRectangle(&fill, r);
        g.DrawRectangle(&edge, r);
        SolidBrush txt(hot ? Color(255, 14, 10, 16) : c);
        g.DrawString(label, -1, &btnF, r, &centre, &txt);
    };
    button(MENU_RESUME, L"RESUME", hover == 1, CYAN);
    button(MENU_EXIT,   L"EXIT",   hover == 2, RED);
    g.DrawString(L"ESC or ENTER to resume   \x00B7   Q to quit", -1, &sub, RectF(300, 790, 1000, 30), &centre, &dimB);

    // Copy the pixels out bottom row first (OpenGL's image origin is the
    // bottom-left corner) and upload them.
    Rect rect(0, 0, MENU_W, MENU_H);
    BitmapData data;
    bmp.LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &data);
    std::vector<unsigned char> px((size_t)MENU_W * MENU_H * 4);
    for (int y = 0; y < MENU_H; ++y)
        memcpy(&px[(size_t)(MENU_H - 1 - y) * MENU_W * 4],
               (const unsigned char*)data.Scan0 + (size_t)y * data.Stride, (size_t)MENU_W * 4);
    bmp.UnlockBits(&data);

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, MENU_W, MENU_H, 0, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

inline void createPauseMenu(PauseMenu& m) {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, NULL) != Gdiplus::Ok) return;
    for (int i = 0; i < 3; ++i) m.tex[i] = buildMenuTexture(i);
    Gdiplus::GdiplusShutdown(token);       // every Bitmap is gone by now
}

inline void destroyPauseMenu(PauseMenu& m) {
    glDeleteTextures(3, m.tex);
}

// The shader that draws the menu image: just the texture, alpha included.
static const char* MENU_FS = R"(
#version 330 core
in  vec2 vUV;
out vec4 FragColor;
uniform sampler2D image;
void main() { FragColor = texture(image, vUV); }
)";
