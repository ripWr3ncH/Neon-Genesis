// ===========================================================================
//  raytrace.h  -  the scene data the ray-traced shadows look at
//
//  The fragment shader's traceShadow() fires a ray from each pixel toward
//  the sun or moon and asks: does it hit a building? To answer, it needs the
//  buildings - so this file packs them into a form a shader can read.
//
//  WHAT IS SENT
//  Every building part (tower, tier, podium, round or octagonal shaft) as an
//  axis-aligned BOX: its min corner and max corner. Round and octagonal
//  towers are sent as the box around them - a little generous at the
//  corners, but invisible at shadow scale, and a box test is far cheaper
//  than a cylinder test. Rooftop clutter and thin ledges are left out: they
//  are too small to cast a shadow worth the cost.
//
//  HOW IT IS SENT
//  As a TEXTURE, one pixel per corner (RGBA32F: four full-precision floats
//  per pixel). A uniform array would run out of room; a texture can hold
//  thousands of boxes, and texelFetch() reads exact values back.
//
//  THE ACCELERATION STRUCTURE
//  Testing every box for every pixel would be hundreds of tests per pixel.
//  But the city is already a 4 x 4 grid of blocks, and every building lies
//  inside one block. So the boxes are SORTED BY BLOCK, and for each block we
//  record where its boxes start in the list and how many there are. A ray
//  then only tests the blocks it passes over - usually two to four.
//  This is a "uniform grid", one of the standard ray-tracing acceleration
//  structures (alongside BVHs and k-d trees).
// ===========================================================================

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include "city.h"

struct RayTraceScene {
    GLuint tex = 0;            // the box texture
    int    cellStart[16] = {}; // first box of each grid cell
    int    cellCount[16] = {}; // how many boxes it has
    int    boxCount = 0;
};

inline void destroyRayTraceScene(RayTraceScene& rt) {
    if (rt.tex) glDeleteTextures(1, &rt.tex);
    rt = RayTraceScene();
}

inline void buildRayTraceScene(RayTraceScene& rt, const City& city) {
    destroyRayTraceScene(rt);

    // Collect each block's boxes separately, then lay them out block by block.
    std::vector<glm::vec4> perCell[16];
    for (const Building& b : city.buildings) {
        // Which grid cell (block) is this building in? Same formula the
        // shader uses: (position + HALF) / CELL, rounded down.
        int cx = (int)floorf((b.position.x + HALF) / CELL);
        int cz = (int)floorf((b.position.z + HALF) / CELL);
        if (cx < 0 || cz < 0 || cx >= GRID || cz >= GRID) continue;
        int c = cx * GRID + cz;

        for (const BuildingPart& p : b.parts) {
            if (p.size.y < 1.5f) continue;        // ledges, canopies, AC units
            glm::vec3 lo = p.base - glm::vec3(p.size.x * 0.5f, 0.0f, p.size.z * 0.5f);
            glm::vec3 hi = p.base + glm::vec3(p.size.x * 0.5f, p.size.y, p.size.z * 0.5f);
            perCell[c].push_back(glm::vec4(lo, 1.0f));
            perCell[c].push_back(glm::vec4(hi, 1.0f));
        }
    }

    std::vector<glm::vec4> texels;
    for (int c = 0; c < 16; ++c) {
        rt.cellStart[c] = (int)(texels.size() / 2);
        rt.cellCount[c] = (int)(perCell[c].size() / 2);
        texels.insert(texels.end(), perCell[c].begin(), perCell[c].end());
    }
    rt.boxCount = (int)(texels.size() / 2);
    if (texels.empty()) texels.push_back(glm::vec4(0.0f));   // never upload nothing

    glGenTextures(1, &rt.tex);
    glBindTexture(GL_TEXTURE_2D, rt.tex);
    // GL_NEAREST and no mipmaps: these are numbers, not a picture - they must
    // come back exactly as stored, never blended with their neighbours.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, (GLsizei)texels.size(), 1, 0,
                 GL_RGBA, GL_FLOAT, texels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}
