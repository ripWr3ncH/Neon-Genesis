// ===========================================================================
//  images.h  -  loading real picture files (PNG / JPG) as textures
//
//  Everything else in the city is textured with patterns made in code. The
//  big rooftop billboards are different: they show actual images, and any
//  PNG or JPG dropped into  assets/billboards/  is picked up automatically -
//  adding a new advert needs no code change at all.
//
//  HOW THE FILE IS READ
//  Decoding a PNG by hand is a lot of work (it is compressed). Windows ships
//  a library that already does it, called GDI+, so - like the music - no
//  extra library has to be downloaded. It is linked with -lgdiplus.
//
//  The steps for one image:
//    1. GDI+ opens and decodes the file into a Bitmap
//    2. LockBits gives direct access to its pixels, 4 bytes each (B,G,R,A)
//    3. The rows are copied out BOTTOM ROW FIRST, because GDI+ stores images
//       top-to-bottom while OpenGL expects texture row 0 at the bottom -
//       without the flip every billboard would be upside down
//    4. glTexImage2D hands the pixels to the GPU, told they are in BGRA order
//
//  GDI+ must be started before use and shut down after; loadBillboardImages
//  does both, so the rest of the program never has to think about it.
// ===========================================================================

#pragma once

// GDI+ needs a couple of Windows COM headers that WIN32_LEAN_AND_MEAN (set in
// music.h) leaves out, and it uses the names min and max, which NOMINMAX (also
// in music.h) switched off. Handing it std::min / std::max inside its own
// namespace is the standard fix.
#include <algorithm>
#include <objidl.h>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <iostream>
#include <cstring>

struct ImageTexture {
    GLuint      id = 0;
    int         width = 0, height = 0;
    glm::vec3   average = glm::vec3(0.5f);   // the image's average colour -
                                             // used to tint the light the
                                             // screen throws onto the street
    std::string name;

    float aspect() const { return height > 0 ? (float)width / (float)height : 1.0f; }
};

// Windows file functions want "wide" (UTF-16) text for paths.
inline std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

// Load one image file into a GPU texture. Returns false (and prints why) if
// the file cannot be read.
inline bool loadImageTexture(const std::string& path, ImageTexture& out) {
    std::wstring wpath = widen(path);
    Gdiplus::Bitmap bmp(wpath.c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok) {
        std::cout << "IMAGE: could not read " << path << std::endl;
        return false;
    }

    int w = (int)bmp.GetWidth(), h = (int)bmp.GetHeight();
    Gdiplus::Rect rect(0, 0, w, h);
    Gdiplus::BitmapData data;
    if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        std::cout << "IMAGE: could not access pixels of " << path << std::endl;
        return false;
    }

    // Copy the rows out upside down (see step 3 above) and add up the colours
    // for the average as we go.
    std::vector<unsigned char> px((size_t)w * h * 4);
    double sumR = 0, sumG = 0, sumB = 0;
    for (int y = 0; y < h; ++y) {
        const unsigned char* src = (const unsigned char*)data.Scan0 + (size_t)y * data.Stride;
        unsigned char* dst = &px[(size_t)(h - 1 - y) * w * 4];
        memcpy(dst, src, (size_t)w * 4);
        for (int x = 0; x < w; ++x) {
            sumB += src[x * 4 + 0];
            sumG += src[x * 4 + 1];
            sumR += src[x * 4 + 2];
        }
    }
    bmp.UnlockBits(&data);

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    // GDI+ gives B,G,R,A in memory; GL_BGRA tells OpenGL to read them that way.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    // CLAMP, not REPEAT: a picture should not tile - its edge pixels should
    // not bleed round to the opposite side.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    double n = (double)w * h * 255.0;
    out.id      = id;
    out.width   = w;
    out.height  = h;
    out.average = glm::vec3((float)(sumR / n), (float)(sumG / n), (float)(sumB / n));
    size_t slash = path.find_last_of("\\/");
    out.name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    return true;
}

// Load every .png / .jpg / .jpeg in a folder, in name order.
inline std::vector<ImageTexture> loadBillboardImages(const std::string& folder) {
    std::vector<ImageTexture> images;

    // Start GDI+. The token is what shuts it down again afterwards.
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, NULL) != Gdiplus::Ok) {
        std::cout << "IMAGE: GDI+ failed to start - no picture billboards" << std::endl;
        return images;
    }

    // Collect matching file names first, so they can be sorted: the order the
    // folder listing comes back in is not guaranteed.
    std::vector<std::string> files;
    const char* patterns[3] = { "*.png", "*.jpg", "*.jpeg" };
    for (const char* pat : patterns) {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((folder + pat).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do { files.push_back(fd.cFileName); } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());

    for (const std::string& f : files) {
        ImageTexture img;
        if (loadImageTexture(folder + f, img)) images.push_back(img);
    }

    // Every Bitmap above was destroyed at the end of loadImageTexture, so it
    // is now safe to shut GDI+ down. The textures live on the GPU.
    Gdiplus::GdiplusShutdown(token);

    std::cout << "Billboard images: " << images.size() << " loaded from " << folder;
    for (const ImageTexture& img : images)
        std::cout << "\n  " << img.name << "  (" << img.width << " x " << img.height << ")";
    std::cout << std::endl;
    return images;
}
