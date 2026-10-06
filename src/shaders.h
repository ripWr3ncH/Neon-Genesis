// ===========================================================================
//  shaders.h  -  every piece of GLSL in the project
//
//  Kept in one header so all the shader source sits together and main.cpp
//  stays about the program logic.
//
//  There are four programs:
//
//    1. SCENE     - the big one. Lighting, fog, textures, emissive surfaces,
//                   the wet road. Has THREE shading modes selected by a
//                   uniform. It also does bloom step 1: alongside the normal
//                   picture it writes a second image holding only the pixels
//                   that glow.
//    2. SKY       - the gradient backdrop with stars. No lighting.
//    3. BLUR      - bloom step 2: smear the glowing pixels out.
//    4. COMPOSITE - bloom step 3: add the blur back over the scene, then
//                   tone-map and gamma-correct for the monitor.
//
//  A "uniform" is a value set once from C++ and read by every vertex/pixel
//  in that draw call. An "in"/"out" pair passes data from the vertex shader
//  to the fragment shader, interpolated across the triangle.
// ===========================================================================

#pragma once

#include <string>

// ---------------------------------------------------------------------------
//  SHARED LIGHTING CODE
//
//  Gouraud shading lights each VERTEX (in the vertex shader); Phong shading
//  lights each PIXEL (in the fragment shader). Both need exactly the same
//  maths, so it is written ONCE, here, and pasted into both shaders.
//
//  The shaders are built by simply joining strings with + :
//        SCENE_VS = start-of-shader + SCENE_LIGHTING_GLSL + rest-of-shader
//  so the same block of lighting code ends up inside both of them. One copy
//  means the two shading modes can never drift apart.
//
//  THE LIGHT MODEL, per light:
//
//    DIFFUSE      max(N . L, 0)             Lambert: surfaces facing the light
//                                           are brightest
//    SPECULAR     max(N . H, 0) ^ shininess Blinn-Phong highlight; H is the
//                                           halfway vector between light and eye
//    ATTENUATION  (1 - d / range)^2         fades to exactly zero at the range,
//                                           so no hard circle on the ground
//    SPOTLIGHT    cos(angle) ^ spotExponent if inside the cone, else 0.
//                 'angle' is between the beam's axis and the direction to the
//                 surface. The CUTOFF is the widest angle that still gets light;
//                 the SPOT EXPONENT concentrates the beam - a higher value
//                 gives a tighter, brighter centre. This is the classic OpenGL
//                 spotlight model named in the proposal.
//
//  A light whose cutoff is set to -2 is a plain POINT light (shines every
//  way): neon strips and the flying vehicle. Street lamps point straight down;
//  headlights point forward along the road.
// ---------------------------------------------------------------------------
static const char* SCENE_LIGHTING_GLSL = R"(
#define MAX_LIGHTS 24
uniform vec3  lightPos[MAX_LIGHTS];
uniform vec3  lightColor[MAX_LIGHTS];
uniform float lightRange[MAX_LIGHTS];
uniform vec3  lightDir[MAX_LIGHTS];        // spot: the way the beam points
uniform float lightCosCutoff[MAX_LIGHTS];  // spot: cos(cutoff); -2 = point light
uniform float lightSpotExp[MAX_LIGHTS];    // spot: the spot exponent
uniform int   lightCount;

uniform vec3  viewPos;
uniform float ambient;

// TWO DIRECTIONAL LIGHTS: the sun and the moon. Each direction is the way
// its light TRAVELS (from the sky down), and each colour is already scaled by
// the time of day in C++ - so at dusk the sun is strong and the moon zero,
// and at night the other way round. Directional means no position and no
// falloff: every point in the city receives the same light from it.
uniform vec3  sunDir;
uniform vec3  sunColor;
uniform vec3  moonDir;
uniform vec3  moonColor;

// ---------------------------------------------------------------------------
//  RAY-TRACED SHADOWS
//
//  Is this point in shadow? Fire a ray from it TOWARD the sun (or moon) and
//  see whether any building is in the way. If one is, the light cannot reach
//  the point: shadow. That one question, asked per pixel, is ray tracing.
//
//  The buildings are sent from C++ as boxes in a texture: box i is texel 2i
//  (its min corner) and texel 2i+1 (its max corner). Testing a ray against
//  every box for every pixel would be far too slow, so the boxes are sorted
//  into the city's 4 x 4 grid of blocks - an ACCELERATION STRUCTURE - and a
//  ray only tests the boxes of the few blocks it actually passes over.
// ---------------------------------------------------------------------------
uniform int       rtEnabled;
uniform sampler2D rtBoxes;
uniform int       rtCellStart[16];      // boxes of grid cell c: [start, start + count)
uniform int       rtCellCount[16];
const float RT_HALF = 60.0;             // the grid spans -60 .. +60 in x and z
const float RT_CELL = 30.0;             // one cell per city block (with its roads)
const int   RT_GRID = 4;
const float RT_TOP  = 60.0;             // nothing on the island is taller

// RAY-BOX test, the "slab method": a box is three pairs of parallel planes.
// For each axis, work out where the ray enters and leaves that pair; the ray
// is inside the box only where it is inside all three at once, i.e. between
// the LATEST entry and the EARLIEST exit. If that range exists and lies ahead
// of the start point, the ray hits the box.
bool hitCell(int cx, int cz, vec3 P, vec3 invL) {
    int c = cx * RT_GRID + cz;
    int start = rtCellStart[c];
    int count = rtCellCount[c];
    for (int i = 0; i < count; ++i) {
        vec3 bmin = texelFetch(rtBoxes, ivec2(2 * (start + i),     0), 0).xyz;
        vec3 bmax = texelFetch(rtBoxes, ivec2(2 * (start + i) + 1, 0), 0).xyz;
        vec3 t1 = (bmin - P) * invL;
        vec3 t2 = (bmax - P) * invL;
        vec3 tn = min(t1, t2), tf = max(t1, t2);
        float tEnter = max(max(tn.x, tn.y), tn.z);
        float tExit  = min(min(tf.x, tf.y), tf.z);
        if (tExit >= max(tEnter, 0.0)) return true;
    }
    return false;
}

// 1.0 = lit, 0.0 = in shadow.
float traceShadow(vec3 P, vec3 L) {
    if (rtEnabled == 0 || L.y <= 0.0) return 1.0;      // light below the horizon

    // 1 / direction, with zero components nudged so nothing divides by zero.
    vec3 Ls = vec3(abs(L.x) < 1e-5 ? 1e-5 : L.x,
                   L.y,
                   abs(L.z) < 1e-5 ? 1e-5 : L.z);
    vec3 invL = 1.0 / Ls;

    // Walk the grid cell by cell along the ray's path seen from above - a 2D
    // DDA, the same stepping used to draw a straight line on a pixel grid. At
    // each step, move into whichever neighbouring cell the ray reaches first.
    vec2 g = (P.xz + RT_HALF) / RT_CELL;          // start, in grid units
    vec2 d = Ls.xz / RT_CELL;                      // grid units per unit of travel

    // Starting outside the grid (on the water, say)? Jump to where the ray
    // enters it - or give up if it never does.
    float t = 0.0;
    if (any(lessThan(g, vec2(0.0))) || any(greaterThanEqual(g, vec2(float(RT_GRID))))) {
        vec2 a = (vec2(0.0) - g) / d, b = (vec2(float(RT_GRID)) - g) / d;
        vec2 lo = min(a, b), hi = max(a, b);
        float t0 = max(lo.x, lo.y), t1 = min(hi.x, hi.y);
        if (t1 < max(t0, 0.0)) return 1.0;
        t = max(t0, 0.0) + 1e-3;
    }
    vec2  gp     = g + d * t;
    ivec2 cell   = ivec2(floor(gp));
    ivec2 stepC  = ivec2(sign(d));
    vec2  nextB  = vec2(cell) + max(vec2(stepC), vec2(0.0));   // next cell edges
    vec2  tMax   = t + (nextB - gp) / d;                         // travel to reach them
    vec2  tDelta = abs(1.0 / d);                                 // travel per whole cell

    for (int k = 0; k < 8; ++k) {
        if (cell.x < 0 || cell.y < 0 || cell.x >= RT_GRID || cell.y >= RT_GRID) break;
        if (P.y + L.y * t > RT_TOP) break;        // risen above every roof: free
        if (hitCell(cell.x, cell.y, P, invL)) return 0.0;
        if (tMax.x < tMax.y) { t = tMax.x; tMax.x += tDelta.x; cell.x += stepC.x; }
        else                 { t = tMax.y; tMax.y += tDelta.y; cell.y += stepC.y; }
    }
    return 1.0;
}

// Diffuse + Blinn-Phong specular from one directional light. 'toLight' is the
// direction from the surface toward the light.
vec3 directional(vec3 N, vec3 V, vec3 toLight, vec3 color, float specStrength, float shine) {
    float diff = max(dot(N, toLight), 0.0);
    vec3  H    = normalize(toLight + V);
    float spec = (diff > 0.0) ? pow(max(dot(N, H), 0.0), shine) * specStrength : 0.0;
    return (diff + spec) * color;
}

// sunShadow / moonShadow: 1 = lit, 0 = shadowed (from traceShadow).
vec3 computeLighting(vec3 P, vec3 N, float specStrength, float shine,
                     float sunShadow, float moonShadow) {
    vec3 V   = normalize(viewPos - P);
    vec3 lit = vec3(ambient);

    lit += directional(N, V, -sunDir,  sunColor,  specStrength, shine) * sunShadow;
    lit += directional(N, V, -moonDir, moonColor, specStrength, shine) * moonShadow;

    for (int i = 0; i < lightCount; ++i) {
        vec3  Lv   = lightPos[i] - P;
        float dist = length(Lv);
        if (dist > lightRange[i]) continue;          // beyond its reach
        vec3 L = Lv / dist;                           // surface -> light

        // ---- spotlight cone ----
        float spot = 1.0;
        if (lightCosCutoff[i] > -1.5) {
            // -L points from the light to the surface. Its dot product with
            // the beam direction is the cosine of the angle off the beam axis.
            float cosAngle = dot(-L, lightDir[i]);
            if (cosAngle < lightCosCutoff[i]) continue;   // outside the cone
            spot = pow(cosAngle, lightSpotExp[i]);
            // Fade the last few degrees so the edge of the pool is soft
            // instead of a jagged hard ring.
            spot *= smoothstep(lightCosCutoff[i], lightCosCutoff[i] + 0.05, cosAngle);
        }

        float diff = max(dot(N, L), 0.0);
        vec3  H    = normalize(L + V);
        float spec = (diff > 0.0) ? pow(max(dot(N, H), 0.0), shine) * specStrength : 0.0;

        float att = 1.0 - dist / lightRange[i];
        att = att * att;

        lit += (diff + spec) * lightColor[i] * att * spot;
    }
    return lit;
}
)";

// ---------------------------------------------------------------------------
//  1. SCENE VERTEX SHADER
//
//  Runs once per vertex. Jobs:
//    a) work out the final screen position  (projection * view * model)
//    b) hand the fragment shader everything it needs for lighting
//    c) in Gouraud mode, do the lighting right here, per vertex
//
//  NOTE ON THE NORMAL MATRIX
//  A normal is a direction, not a point. If an object is scaled unevenly -
//  say a building scaled (6, 30, 6) - then scaling its normals the same way
//  tilts them wrongly and the lighting goes crooked. The fix is the classic
//  transpose(inverse(model)). We pass it in from C++ as 'normalMatrix'
//  because computing an inverse per-vertex on the GPU is wasteful.
// ---------------------------------------------------------------------------
static const std::string SCENE_VS = std::string(R"(
#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec2 aUV;

out vec3 vColor;
out vec3 vNormal;      // in WORLD space
out vec3 vFragPos;     // in WORLD space
out vec2 vUV;
out vec3 vGouraud;     // only filled when shadingMode == 1

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat3 normalMatrix;

uniform int   shadingMode;     // 0 = flat, 1 = Gouraud, 2 = Phong
uniform float specularStrength;
uniform float shininess;

// How many times the texture repeats across each face, per axis. For a
// building this is (width, height, depth) divided by the size of one window,
// so a tall tower gets MORE floors instead of the same few stretched taller.
// Side faces (normal along X) run across the depth; the others across width.
uniform vec3 uvWorldScale;

// 0 = BOX: pick the scale per face from its normal (above).
// 1 = WRAP: the UVs already run round the whole outline (cylinder, octagon),
//     so x is simply "how many windows fit round the perimeter".
uniform int uvMode;

// Added AFTER scaling. Billboards use it to pick which frame of their
// animation strip is showing, and to scroll the picture sideways.
uniform vec2 uvOffset;

// CLIP PLANE, used only while drawing the upside-down reflection.
// gl_ClipDistance is a built-in output: wherever it comes out NEGATIVE, the
// GPU cuts that part of the triangle away. Written as dot(position, plane)
// it measures which side of a plane the vertex is on. For the reflection the
// plane is (0, -1, 0, 0), i.e. "keep only y <= 0": anything the flip moved
// ABOVE the ground - like the underwater part of the sea wall - is removed,
// instead of poking up through the street as a ghost wall.
uniform vec4 clipPlane;
)") + SCENE_LIGHTING_GLSL + R"(
void main() {
    // Object space -> world space. We keep the world position because
    // lighting needs real distances between the surface and each lamp.
    vec4 worldPos = model * vec4(aPos, 1.0);
    vFragPos = worldPos.xyz;

    vNormal = normalize(normalMatrix * aNormal);
    vColor  = aColor;

    vec2 uvScale;
    if (uvMode == 1) uvScale = uvWorldScale.xy;
    else             uvScale = (abs(aNormal.x) > 0.5) ? uvWorldScale.zy : uvWorldScale.xy;
    vUV = aUV * uvScale + uvOffset;

    // GOURAUD: light the vertex now; the GPU then smears the three corner
    // results across the triangle. Anything happening BETWEEN corners - a
    // lamp's pool of light in the middle of a big road - is simply lost.
    vGouraud = vec3(0.0);
    if (shadingMode == 1)
        // (no ray-traced shadows per vertex - they need every pixel)
        vGouraud = computeLighting(vFragPos, vNormal, specularStrength, shininess, 1.0, 1.0);

    gl_ClipDistance[0] = dot(worldPos, clipPlane);
    gl_Position = projection * view * worldPos;
}
)";

// ---------------------------------------------------------------------------
//  2. SCENE FRAGMENT SHADER
//
//  Runs once per PIXEL. This is where the cyberpunk look is actually made.
//
//  THE THREE SHADING MODES (press 1 / 2 / 3 while running):
//
//    0 FLAT    - one normal for the whole triangle, so each face is a single
//                solid tone. Computed with dFdx/dFdy, which give the rate of
//                change of the world position across the screen; their cross
//                product is the true face normal.
//    1 GOURAUD - lighting done per VERTEX and smeared across the triangle.
//                Cheap, but light falling between the corners is lost.
//    2 PHONG   - lighting done per PIXEL. Smooth highlights. The default.
//
//  EMISSIVE SURFACES
//  A neon strip or a billboard does not react to light - it IS light. Those
//  surfaces skip the lighting maths entirely and output their colour
//  directly, bright enough for the bloom pass to pick them out.
// ---------------------------------------------------------------------------
static const std::string SCENE_FS = std::string(R"(
#version 330 core

in vec3 vColor;
in vec3 vNormal;
in vec3 vFragPos;
in vec2 vUV;
in vec3 vGouraud;

// TWO outputs. location 0 is the normal picture; location 1 collects only
// the glowing parts, which is what the bloom blur works on. Writing both in
// one pass is much cheaper than drawing the scene twice.
layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 BrightColor;

uniform vec3  tint;
uniform int   shadingMode;
uniform float time;

uniform float emissive;      // 0 = normal surface, >0 = glowing
uniform vec3  emissiveColor;
uniform float bloomAmount;   // how much a glowing surface feeds the bloom.
                             // Rain uses 0: a streak right next to the camera
                             // would otherwise blur into a wide grey bar.

uniform int       useTexture;
uniform sampler2D tex0;

// FACADE WINDOWS. When > 0 the texture is treated as a pattern of LIGHTS
// rather than a paint colour: the bright cells glow on their own, while the
// wall itself is still lit normally by the street lamps.
uniform float texEmissive;
uniform float windowSeed;    // per building, so towers do not switch in step

uniform vec3  fogColor;
uniform float fogDensity;

uniform float specularStrength;
uniform float shininess;

// WET ROAD. The road is drawn slightly SEE-THROUGH over an upside-down copy
// of the city (drawn first, from C++). How see-through depends on the viewing
// angle - see the Fresnel note below.
uniform float wetness;
uniform float alpha;

// WATER. When > 0, the surface normal is wobbled by a few moving sine waves.
// Nothing moves geometrically - the surface stays flat - but lighting uses
// the normal, so the lamps' highlights break up and glitter the way light
// does on real water, and the Fresnel see-through shimmers with it.
uniform float waves;

// ---- THE DETAIL LAYER (key X switches all of it off, for comparison) ----
uniform float detail;      // 1 = facade relief, grime, glass reflections on
uniform float puddles;     // ground only: strength of the puddle layer
uniform float mirrorOn;    // 1 when the reflected city is drawn under the ground
uniform float rainAmt;     // 1 while it rains: ripples in the puddles
uniform float envReflect;  // car paint / glass: reflected surroundings
uniform float beam;        // > 0: this mesh is a LIGHT BEAM (see below)
uniform float holo;        // screens: holographic scanlines and glitches
uniform float streak;      // emissive: 1 = this light makes a lens streak
uniform float shoreEdge;   // the island's half-width: foam where water meets it
uniform float outScale;    // brightness multiplier: < 1 while drawing the dimmer reflected city
)") + SCENE_LIGHTING_GLSL + R"(
// A cheap hash: turns any 2D point into a repeatable pseudo-random number in
// 0..1. Multiplying by large awkward constants and keeping the fractional
// part scrambles the input thoroughly - the standard trick in small shaders.
float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// VALUE NOISE: random values at the grid corners, smoothly blended in
// between (the 3t^2 - 2t^3 curve). Gives soft blotches instead of the
// hash's per-pixel static. fbm2 adds a finer copy on top for detail.
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i),                 hash21(i + vec2(1.0, 0.0)), u.x),
               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm2(vec2 p) { return 0.6 * vnoise(p) + 0.4 * vnoise(p * 2.3 + 17.1); }

// RAIN RIPPLES. The ground is cut into cells; each cell has one drop, at a
// random spot and on its own clock, and its ring grows from 0 to 0.42 cell
// widths and fades out. The ring is a sine wave in the distance from the
// drop; the result tilts the normal outward (x, z) - added to N by the caller.
vec2 rainRipple(vec2 xz, float density) {
    vec2  rp   = xz * density;
    vec2  cell = floor(rp);
    float h    = hash21(cell);
    vec2  dv   = fract(rp) - 0.5 - (vec2(hash21(cell + 3.1), hash21(cell + 7.7)) - 0.5) * 0.4;
    float t    = fract(time * 0.9 + h);
    float r    = length(dv), R = t * 0.42;
    float ring = sin((r - R) * 60.0) * (1.0 - smoothstep(0.0, 0.07, abs(r - R))) * (1.0 - t);
    return dv / max(r, 1e-3) * ring;
}

// The slope of the noise at p (how fast it rises along x and along z),
// by finite differences. Used to wobble the water's normal.
vec2 noiseSlope(vec2 p) {
    const float e = 0.15;
    float n0 = vnoise(p);
    return vec2(vnoise(p + vec2(e, 0.0)) - n0, vnoise(p + vec2(0.0, e)) - n0) / e;
}

// FACADE RELIEF: a height for every point of the window grid (g counts in
// window cells). 0 on the wall, -1 down in the recessed glass, -0.5 in the
// thin groove between floors. Its slope becomes a normal tilt below - this
// is BUMP MAPPING: the wall stays flat, only the lighting is told otherwise.
float facadeHeight(vec2 g) {
    vec2 c = fract(g);
    float gx = smoothstep(0.21, 0.27, c.x) * (1.0 - smoothstep(0.73, 0.79, c.x));
    float gy = smoothstep(0.15, 0.21, c.y) * (1.0 - smoothstep(0.85, 0.90, c.y));
    float groove = 1.0 - smoothstep(0.0, 0.04, min(c.y, 1.0 - c.y));
    return -gx * gy - 0.5 * groove;
}

// A FAKE ENVIRONMENT for reflections: what a shiny surface would see in
// direction R. Dark sky above, the glow of the city along the horizon in
// bands of magenta and cyan, the dark street below. Not traced - just a
// function of the direction - but on curved car paint it reads as the city
// sliding over the body as it drives.
vec3 envColor(vec3 R) {
    float up   = R.y;
    float az   = atan(R.z, R.x);
    vec3  sky  = mix(fogColor * 4.0 + sunColor * 0.04, vec3(0.015, 0.012, 0.04), smoothstep(0.0, 0.6, up));
    float band = exp(-abs(up - 0.06) * 10.0);
    float neon = pow(0.5 + 0.5 * sin(az * 7.0), 6.0);
    vec3  hue  = mix(vec3(1.0, 0.2, 0.6), vec3(0.1, 0.9, 1.0), 0.5 + 0.5 * sin(az * 3.0 + 1.3));
    vec3  col  = sky + band * (hue * neon * 0.9 + vec3(1.0, 0.75, 0.45) * 0.15 + sunColor * 0.08);
    return mix(col, vec3(0.01, 0.01, 0.02), smoothstep(0.0, -0.25, up));
}

void main() {
    // Screen-space derivatives, taken first while every pixel of the 2x2
    // block is still running (they are undefined inside branches that only
    // some pixels take). Used by flat shading and the facade relief.
    vec3 dPdx = dFdx(vFragPos), dPdy = dFdy(vFragPos);
    vec2 dUVdx = dFdx(vUV),     dUVdy = dFdy(vUV);

    // ---- LIGHT BEAMS: the "volumetric" look --------------------------------
    // A real beam shows because rain and haze scatter some of its light
    // toward the eye. Here the beam is a cone mesh added ON TOP of the image
    // (additive blending, set in C++). Three things make it read as light in
    // the air rather than a glowing plastic cone:
    //   EDGES  - brightness by |N . V|: the middle of the cone (seen through
    //            the most "air") is bright, the silhouette fades to nothing
    //   LENGTH - strongest at the lamp (v = 1), fading toward the ground,
    //            and gone right at the floor so no hard line shows there
    //   DUST   - slowly drifting noise, like rain and mist in the beam
    if (beam > 0.0) {
        vec3  V     = normalize(viewPos - vFragPos);
        float edge  = pow(abs(dot(normalize(vNormal), V)), 2.5);
        float along = vUV.y;
        float fall  = mix(0.12, 1.0, along * along) * smoothstep(0.0, 0.18, along);
        float dust  = 0.6 + 0.4 * vnoise(vFragPos.xz * 1.4 + vec2(vFragPos.y * 0.8 + time * 0.5, time * 0.3));
        float d     = length(viewPos - vFragPos);
        float near  = smoothstep(0.6, 3.5, d);              // fade when the camera walks into it
        float fogT  = exp(-pow(d * fogDensity, 2.0));
        vec3  c     = emissiveColor * beam * edge * fall * dust * near * fogT;
        FragColor   = vec4(c, 1.0);
        BrightColor = vec4(c * 0.12, 0.0);
        return;
    }

    vec3 texel = vec3(1.0);
    if (useTexture == 1) {
        vec4 t = texture(tex0, vUV);
        if (t.a < 0.1) discard;      // cut out fully transparent texels
        texel = t.rgb;
    }

    // ---- EMISSIVE SHORT-CIRCUIT -------------------------------------------
    // Neon, lamp heads, billboards, headlights land here. They ignore every
    // light in the scene, because they ARE light.
    if (emissive > 0.5) {
        // HOLOGRAPHIC SCREENS. Four cheap tricks on top of the picture:
        //   GLITCH     - now and then a few thin bands jump sideways
        //   RGB SPLIT  - red and blue read slightly apart, like a bad signal
        //   SCANLINES  - fine dark lines, faded out when too small to show
        //   FLICKER and a bright bar rolling slowly up the screen
        if (holo > 0.0 && useTexture == 1) {
            float band   = floor(vUV.y * 90.0);
            float burst  = step(0.55, hash21(vec2(floor(time * 2.5), 3.7)));
            float jump   = step(0.92, hash21(vec2(band, floor(time * 14.0)))) * burst * holo;
            vec2  uv     = vUV + vec2(jump * 0.07, 0.0);
            float split  = 0.0035 * holo + jump * 0.02;
            texel = vec3(texture(tex0, uv + vec2(split, 0.0)).r,
                         texture(tex0, uv).g,
                         texture(tex0, uv - vec2(split, 0.0)).b);
            float lines  = vUV.y * 1200.0;
            float fade   = 1.0 - smoothstep(0.25, 0.6, abs(dFdy(lines)) + abs(dFdx(lines)));
            float scan   = 1.0 - 0.28 * holo * fade * (0.5 + 0.5 * sin(lines));
            float roll   = exp(-pow(fract(vUV.y * 1.5 - time * 0.25) - 0.5, 2.0) * 300.0) * 0.35 * holo;
            float flick  = 1.0 - 0.07 * holo * hash21(vec2(floor(time * 24.0), 1.0));
            texel = texel * scan * flick + texel * roll;
        }
        vec3 glow = emissiveColor * vColor * tint * texel * outScale;
        FragColor   = vec4(glow, 1.0);
        // The ALPHA of the bright image marks which lights may make a lens
        // streak (only lamp heads and headlights: see runStreakBlur).
        BrightColor = vec4(glow * bloomAmount, streak);
        return;
    }

    // Window facades keep their wall colour; the texture only adds the glow.
    // Everything else is painted by the texture as usual.
    vec3 baseColor = vColor * tint;
    if (useTexture == 1 && texEmissive <= 0.0) baseColor *= texel;

    // ---- PICK THE NORMAL ---------------------------------------------------
    vec3 N;
    if (shadingMode == 0) {
        // FLAT: the true face normal, from how the world position changes
        // between neighbouring pixels. Constant across a triangle, so each
        // face gets one tone - the classic faceted look.
        N = normalize(cross(dPdx, dPdy));
        if (dot(N, viewPos - vFragPos) < 0.0) N = -N;
    } else {
        N = normalize(vNormal);
    }

    if (waves > 0.0) {
        // Three wave trains crossing at different angles and speeds; their
        // sum never visibly repeats. Only x and z of the normal are nudged,
        // so the surface still faces mostly up.
        vec2 p = vFragPos.xz;
        float wx = sin(p.x * 0.90 + time * 1.3) + 0.5 * sin(p.x * 2.30 - p.y * 1.10 + time * 2.1);
        float wz = sin(p.y * 0.75 - time * 1.1) + 0.5 * sin(p.y * 2.70 + p.x * 0.80 + time * 1.7);
        // On top of the long swells, two layers of drifting noise: a
        // medium chop and fine ripples moving the other way. Their slopes
        // tilt the normal, so the moonlight breaks into thousands of glints.
        vec2 chop = noiseSlope(p * 0.45 + vec2(time * 0.30, time * 0.17));
        vec2 fine = noiseSlope(p * 1.70 - vec2(time * 0.55, -time * 0.40));
        vec2 tilt = vec2(wx, wz) * 0.55 + chop * 0.9 + fine * 0.45;
        if (rainAmt > 0.0 && detail > 0.5) tilt += rainRipple(p, 0.9) * 3.0;
        N = normalize(N + waves * vec3(tilt.x, 0.0, tilt.y));
    }

    float specS = specularStrength, shineS = shininess;

    // ---- PUDDLES ------------------------------------------------------------
    // A noise pattern decides where water has collected (the brightest
    // blotches of fbm2). Inside a puddle the ground is darker (wet), much
    // shinier, and more SEE-THROUGH, so the reflected city drawn underneath
    // shows clearly - outside it the ground is only damp. While it rains,
    // each puddle cell gets an expanding RIPPLE RING on its own clock: a sine
    // wave in the distance from the drop's centre, tilting the normal
    // outward, fading as the ring grows.
    float pmask = 0.0;
    if (puddles > 0.0 && N.y > 0.5) {
        pmask = smoothstep(0.53, 0.61, fbm2(vFragPos.xz * 0.16)) * puddles;
        if (rainAmt > 0.0) {
            vec2 rr = rainRipple(vFragPos.xz, 1.6) * 0.35 * mix(0.3, 1.0, pmask) * rainAmt;
            N = normalize(N + vec3(rr.x, 0.0, rr.y));
        }
        baseColor *= mix(1.0, 0.30, pmask);
        specS  = mix(specS, 1.6, pmask);
        shineS = mix(shineS, 220.0, pmask);
    }

    // ---- FACADE RELIEF AND GRIME (window walls only) ------------------------
    // The slope of facadeHeight() along the two texture directions is turned
    // into a tilt of the normal. T and B - the world directions in which the
    // grid's x and y grow - come from the screen derivatives (the
    // "cotangent frame"), so this works on boxes, cylinders and octagons
    // alike with no tangent data stored in the mesh. The relief fades out
    // when a window is only a few pixels across, where it would only shimmer.
    float glassMask = 0.0;
    if (detail > 0.5 && useTexture == 1 && texEmissive > 0.0 && abs(N.y) < 0.5) {
        vec2  g    = vUV * vec2(6.0, 8.0);
        vec2  gpx  = abs(dUVdx * vec2(6.0, 8.0)) + abs(dUVdy * vec2(6.0, 8.0));
        float keep = 1.0 - smoothstep(0.04, 0.16, max(gpx.x, gpx.y));
        glassMask  = -min(facadeHeight(g) + 0.5 * (1.0 - smoothstep(0.0, 0.04, min(fract(g.y), 1.0 - fract(g.y)))), 0.0);

        if (keep > 0.0) {
            const float e = 0.02;
            float h0 = facadeHeight(g);
            float hx = (facadeHeight(g + vec2(e, 0.0)) - h0) / e;
            float hy = (facadeHeight(g + vec2(0.0, e)) - h0) / e;
            vec3 dp2perp = cross(dPdy, N), dp1perp = cross(N, dPdx);
            vec3 T = dp2perp * dUVdx.x + dp1perp * dUVdy.x;
            vec3 B = dp2perp * dUVdx.y + dp1perp * dUVdy.y;
            T = normalize(T + 1e-6); B = normalize(B + 1e-6);
            N = normalize(N - 0.035 * keep * (hx * T + hy * B));
        }
        // Grime: long vertical rain streaks plus big blotches, in WORLD space
        // so they run continuously across the window grid.
        float streak = vnoise(vec2((vFragPos.x + vFragPos.z) * 1.9, vFragPos.y * 0.05));
        float blotch = fbm2(vec2(vFragPos.x - vFragPos.z, vFragPos.y) * 0.12);
        baseColor *= mix(0.55, 1.30, streak * 0.55 + blotch * 0.45);
    }

    // ---- ray-traced shadows (flat and Phong only) ---------------------------
    // Only worth tracing when the light can reach this face at all (facing
    // it) and is switched on. The start point is lifted 0.06 off the surface
    // along its normal, so the ray does not immediately hit the very wall it
    // starts on ("shadow acne").
    float sunSh = 1.0, moonSh = 1.0;
    if (shadingMode != 1) {
        vec3 Pb = vFragPos + N * 0.06;
        if (dot(sunColor,  sunColor)  > 1e-4 && dot(N, -sunDir)  > 0.0) sunSh  = traceShadow(Pb, -sunDir);
        if (dot(moonColor, moonColor) > 1e-4 && dot(N, -moonDir) > 0.0) moonSh = traceShadow(Pb, -moonDir);
    }

    // Gouraud already lit the vertices; flat and Phong light this pixel now.
    vec3 lighting = (shadingMode == 1) ? vGouraud
                                       : computeLighting(vFragPos, N, specS, shineS, sunSh, moonSh);

    vec3 color = baseColor * lighting;

    // ---- LIT WINDOWS, SWITCHING ON AND OFF ----------------------------------
    // Only the bright cells glow (the dark frames sit below the 0.12 cut),
    // and never on the roof, which faces straight up.
    vec3 windowGlow = vec3(0.0);
    if (useTexture == 1 && texEmissive > 0.0 && abs(N.y) < 0.5) {
        vec3 glow = max(texel - vec3(0.12), vec3(0.0));

        // The window texture is a 6 x 8 grid, and vUV counts in whole copies
        // of it. So vUV * (6, 8) counts in WINDOWS: its whole part says which
        // window this pixel is in, its fractional part where inside it.
        vec2 grid   = vUV * vec2(6.0, 8.0);
        vec2 cell   = floor(grid);
        vec2 inCell = fract(grid);
        // The glass occupies the middle of each cell, the rest is wall. These
        // limits match the frame drawn in makeWindowTexture().
        bool glass  = inCell.x > 0.25 && inCell.x < 0.75 &&
                      inCell.y > 0.19 && inCell.y < 0.875;

        // Every window has its own clock: its hash staggers when its "epoch"
        // ticks over (every 6 seconds). At each tick it rolls again, and 12%
        // of the time it is FLIPPED - a lit office goes dark, a dark one
        // lights up. Nothing is stored; it is all a function of time.
        float h     = hash21(cell + windowSeed);
        float epoch = floor(time / 6.0 + h * 6.0);
        if (hash21(cell * 1.37 + vec2(epoch, windowSeed)) < 0.12) {
            // Was this window lit in the texture? Sample the MIDDLE of the
            // cell, so the answer is the same for every pixel of the window.
            vec2 centreUV = (cell + vec2(0.5, 0.53)) / vec2(6.0, 8.0);
            // textureLod(..., 0) reads the full-detail image. The centre
            // jumps at each cell border, and plain texture() would react to
            // that jump by picking a blurry low-detail copy there.
            bool wasLit   = dot(textureLod(tex0, centreUV, 0.0).rgb, vec3(0.333)) > 0.2;

            if (wasLit) {
                // Switch OFF: darken the WHOLE cell, frame included. Texture
                // filtering smears a little of the bright glass into the
                // frame around it; darkening only the glass left a faint
                // glowing outline behind.
                glow = vec3(0.0);
            } else if (glass) {
                // Switch ON: light just the glass area.
                glow = vec3(1.0, 0.82, 0.55) * (0.45 + 0.4 * h);
            }
        }

        windowGlow = glow * texEmissive;
        color += windowGlow;
    }

    // ---- REFLECTED SURROUNDINGS ------------------------------------------------
    // Fresnel again (Schlick): F = F0 + (1 - F0)(1 - N.V)^5, with F0 = 0.04
    // for paint and glass. Glossy car paint and the dark (unlit) window glass
    // show the fake environment, faint face-on and strong at grazing angles.
    vec3  Vd   = normalize(viewPos - vFragPos);
    float fres = pow(1.0 - max(dot(N, Vd), 0.0), 5.0);
    float envAmt = envReflect + glassMask * (1.0 - min(dot(windowGlow, vec3(4.0)), 1.0)) * 0.7 * detail;
    if (envAmt > 0.0)
        color += envColor(reflect(-Vd, N)) * (0.04 + 0.96 * fres) * envAmt * 1.6;

    // ---- OPEN WATER ------------------------------------------------------------
    // What makes a dark surface read as WATER:
    //   GLITTER PATH - the moon (or the setting sun) reflected in the moving
    //                  waves: R . L ^ 300 is only near 1 where a wave happens
    //                  to tilt the reflection straight at the moon, so the
    //                  moon becomes a column of sparkles stretching toward
    //                  the viewer - the classic "moon on the sea" look
    //   SKY          - the water mirrors the sky, more at grazing angles
    //                  (Fresnel), so it brightens toward the horizon
    //   FOAM         - churned white water where waves hit the sea wall
    float foam = 0.0;
    if (waves > 0.0) {
        vec3  R     = reflect(-Vd, N);
        vec3  toMoon = -moonDir, toSun = -sunDir;
        float moonA = max(dot(R, toMoon), 0.0), sunA = max(dot(R, toSun), 0.0);
        vec3  moonTint = moonColor * 14.0;              // moonColor is already 0 at dusk
        color += moonTint * (pow(moonA, 300.0) * 4.0 + pow(moonA, 25.0) * 0.30);
        color += sunColor * (pow(sunA, 300.0) * 3.0 + pow(sunA, 20.0) * 0.12);
        vec3 skyRefl = mix(fogColor * 3.5, fogColor * 0.8, clamp(R.y, 0.0, 1.0)) + moonTint * 0.02;
        color += skyRefl * (0.25 + 0.75 * fres);

        // foam: within ~2.5 units of the island's square edge, broken up by
        // drifting noise so it comes and goes like waves breaking
        if (detail > 0.5) {
            float e    = max(abs(vFragPos.x), abs(vFragPos.z)) - shoreEdge;
            float band = 1.0 - smoothstep(0.0, 2.5, e);
            float n    = vnoise(vFragPos.xz * 1.3 + vec2(time * 0.7, time * 0.4))
                       + 0.5 * sin(e * 4.0 - time * 2.2);
            foam = band * smoothstep(0.45, 0.85, n + band * 0.35);
            color = mix(color, vec3(0.30, 0.34, 0.42) * (lighting + vec3(0.08)), foam);
        }
    }

    // ---- WET ROAD: FRESNEL -------------------------------------------------
    // Real wet asphalt reflects little when you look straight down at it and
    // a lot when you look along it at a low angle. The Schlick approximation
    // (1 - cos)^5 models that. Lower alpha = more of the mirrored city below
    // shows through, so the reflection strengthens toward the horizon.
    float outAlpha = alpha;
    if (wetness > 0.0) {
        outAlpha = alpha * (1.0 - fres * wetness * 0.18);
    }
    // Puddles let much more of the reflected city through.
    if (pmask > 0.0 && mirrorOn > 0.5)
        outAlpha = mix(outAlpha, 0.78 - 0.22 * fres, pmask);
    outAlpha = mix(outAlpha, 1.0, foam);              // foam is not see-through

    // ---- DISTANCE FOG ------------------------------------------------------
    // Exponential-squared: almost nothing close up, thickening fast with
    // distance. The fog colour matches the sky at the horizon, so the far
    // city melts into the sky instead of ending at a hard line.
    float d      = length(viewPos - vFragPos);
    float fogAmt = clamp(1.0 - exp(-pow(d * fogDensity, 2.0)), 0.0, 1.0);
    color        = mix(color, fogColor, fogAmt);

    color    *= outScale;
    FragColor = vec4(color, outAlpha);

    // ---- feed the bloom buffer --------------------------------------------
    // Windows only feed the bloom a little. At full strength the blur of a
    // whole wall of windows spread over the wall itself and turned it pale.
    float luma   = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3  bright = windowGlow * 0.2 * (1.0 - fogAmt);
    if (luma > 1.0) bright += color;
    // Alpha: blended surfaces (the road, the water) need their alpha here
    // as the blend factor; solid ones write 0 = "no lens streak". (The
    // stored alpha is not changed by blended draws - see glBlendFuncSeparate
    // in main.cpp.)
    bool blended = wetness > 0.0 || puddles > 0.0 || waves > 0.0;   // drawn with blending on
    BrightColor  = vec4(bright, blended ? outAlpha : 0.0);
}
)";

// ---------------------------------------------------------------------------
//  3. SKY
//
//  A large box drawn around the camera. Two tricks:
//    - the view matrix has its translation stripped in C++, so the box never
//      moves relative to the camera and feels infinitely far away
//    - gl_Position.z is forced to w, which puts every sky pixel at maximum
//      depth so all real geometry passes in front of it
// ---------------------------------------------------------------------------
static const char* SKY_VS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 vDir;

uniform mat4 view;         // rotation only
uniform mat4 projection;

void main() {
    vDir = aPos;
    vec4 p = projection * view * vec4(aPos, 1.0);
    gl_Position = p.xyww;   // z = w  ->  depth is always 1.0, the far plane
}
)";

static const char* SKY_FS = R"(
#version 330 core
in vec3 vDir;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 BrightColor;

uniform vec3  horizonColor;
uniform vec3  zenithColor;
uniform float time;
uniform vec3  moonDirection;   // unit vector from the viewer TOWARD the moon
uniform vec3  sunDirection;    // ... and toward the sun
uniform float dusk;            // 1 = sunset, 0 = full night (C++ blends between)

// A cheap hash: any 2D point -> a repeatable pseudo-random number in 0..1.
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// VALUE NOISE: random numbers at whole-number grid points, smoothly blended
// in between. The smoothstep-style curve f*f*(3-2f) removes the creases a
// straight linear blend would leave at every grid line.
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i),                 b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0)), d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// FRACTAL NOISE (fbm): the same noise added at 5 sizes, each half as big and
// half as strong as the last. Big soft shapes from the first layers, ragged
// cloud edges from the small ones - this is how most procedural clouds,
// terrain and smoke are made.
float fbm(vec2 p) {
    float v = 0.0, amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        v   += amp * noise(p);
        p    = p * 2.03 + vec2(1.7, 9.2);
        amp *= 0.5;
    }
    return v;
}

void main() {
    vec3 d = normalize(vDir);

    // ---- gradient ------------------------------------------------------------
    // Horizon to zenith by height. C++ passes the FOG colour as the horizon
    // colour, so the far water and skyline fade into the sky with no edge.
    float h  = clamp(d.y, 0.0, 1.0);
    vec3 col = mix(horizonColor, zenithColor, smoothstep(0.0, 0.55, h));

    // Below the horizon the sky is only ever seen THROUGH the water, which
    // shows it as a reflection - darker than the sky itself. Without this the
    // sea took on the bright haze colour at sunset.
    float below = clamp(-d.y / 0.15, 0.0, 1.0);
    col *= mix(1.0, 0.35, below);

    // ---- stars ---------------------------------------------------------------
    if (d.y > 0.10) {
        // One candidate star per grid cell, drawn as a small soft dot at a
        // random spot inside the cell (lighting the whole cell made squares).
        vec2 g    = d.xz * 140.0;
        vec2 cell = floor(g);
        vec2 f    = fract(g);
        float r   = hash(cell);
        if (r > 0.985) {
            vec2  spot = vec2(hash(cell + 1.3), hash(cell + 7.1)) * 0.6 + 0.2;
            float dot_ = smoothstep(0.22, 0.0, length(f - spot));
            float tw   = 0.65 + 0.35 * sin(time * 2.2 + r * 60.0);   // twinkle
            float fade = smoothstep(0.10, 0.42, d.y) * (1.0 - dusk);   // no stars at sunset
            col += vec3(dot_ * tw * fade);
        }
    }

    // ---- the moon --------------------------------------------------------------
    // The angle between this pixel's direction and the moon's direction tells
    // whether the pixel is on the moon's disc. A CRESCENT is the disc minus a
    // second, slightly shifted disc - the part of the moon in shadow.
    const float R = 0.045;                          // disc radius, in radians
    float toMoon  = acos(clamp(dot(d, moonDirection), -1.0, 1.0));
    float disc    = smoothstep(R, R * 0.92, toMoon);

    // The shadow disc is nudged sideways, perpendicular to the moon direction
    // and to "up", so the lit sliver sits on one side like the reference.
    vec3  side      = normalize(cross(moonDirection, vec3(0.0, 1.0, 0.0)));
    vec3  shadowDir = normalize(moonDirection + side * R * 0.62 + vec3(0.0, R * 0.25, 0.0));
    float inShadow  = smoothstep(R * 0.93, R * 0.85, acos(clamp(dot(d, shadowDir), -1.0, 1.0)));
    float crescent  = disc * (1.0 - inShadow);

    // A soft halo around it: two falloffs, a tight bright one and a wide
    // faint one, both from how closely this direction lines up with the moon.
    float align = max(dot(d, moonDirection), 0.0);
    // Kept fairly dim on purpose: much brighter and the tone mapping squeezes
    // it to plain white, losing the lavender of the reference.
    vec3  moonCol  = vec3(0.78, 0.52, 1.00);        // lavender, as in the reference
    vec3  moonGlow = (moonCol * (crescent * 1.35)
                   + vec3(0.55, 0.22, 0.85) * (pow(align, 900.0) * 0.45 + pow(align, 60.0) * 0.10))
                   * (1.0 - dusk);                    // the moon only shows at night

    // ---- the sun (dusk only) -------------------------------------------------
    // The same "how closely does this direction line up" idea as the moon's
    // halo: a sharp disc, a warm glow round it, and a very wide faint wash
    // that tints the whole western sky.
    float sunAlign = max(dot(d, sunDirection), 0.0);
    vec3  sunGlow  = dusk * ( vec3(1.00, 0.62, 0.30) * smoothstep(0.99935, 0.99965, sunAlign) * 4.0
                            + vec3(1.00, 0.45, 0.18) * pow(sunAlign, 24.0) * 0.70
                            + vec3(0.90, 0.32, 0.22) * pow(sunAlign, 4.0)  * 0.18 );

    // ---- clouds ----------------------------------------------------------------
    // Imagine a flat ceiling of cloud high above. A view direction d hits it
    // at d.xz / d.y - dividing by the height makes clouds near the horizon
    // stretch out and shrink with distance, like a real cloud layer. That
    // point is fed to the fractal noise, drifting slowly with time.
    float cover = 0.0;
    vec3  cloudCol = vec3(0.0);
    if (d.y > 0.0) {
        vec2  uv    = d.xz / (d.y + 0.12) * 0.55 + vec2(time * 0.006, time * 0.003);
        float n     = fbm(uv);
        cover       = smoothstep(0.48, 0.80, n);
        cover      *= smoothstep(0.02, 0.22, d.y);   // thin out at the horizon

        // Dark clouds, lit from behind on the side facing the moon - the
        // purple "silver lining" in the reference. Thin edges (low cover)
        // catch the most light.
        float nearMoon = pow(align, 10.0);
        vec3 nightCloud = vec3(0.020, 0.012, 0.038)
                        + vec3(0.30, 0.13, 0.46) * nearMoon * (1.2 - cover);
        // At sunset the clouds are lit from below by the low sun: warm pink
        // overall, burning orange on the side toward the sun.
        vec3 duskCloud  = vec3(0.10, 0.045, 0.07)
                        + vec3(0.85, 0.35, 0.15) * (0.35 + 0.65 * pow(sunAlign, 6.0)) * (1.2 - cover) * 0.6;
        cloudCol = mix(nightCloud, duskCloud, dusk);
    }

    // Moon and stars sit BEHIND the clouds, so the clouds are mixed over them.
    col = col + moonGlow + sunGlow;
    col = mix(col, cloudCol, cover * 0.92);

    FragColor = vec4(col, 1.0);

    // Only the moon and sun glow (feed the bloom), and only where no cloud
    // hides them.
    BrightColor = vec4((moonGlow + sunGlow) * (1.0 - cover * 0.92) * 0.8, 0.0);
}
)";

// ---------------------------------------------------------------------------
//  4. FULLSCREEN QUAD  -  shared by every post-processing step
//
//  Draws two triangles covering the whole screen. The vertex positions are
//  already in clip space (-1..+1), so no matrices are needed at all.
// ---------------------------------------------------------------------------
static const char* POST_VS = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;

out vec2 vUV;

void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// ---------------------------------------------------------------------------
//  5. BLOOM BLUR  -  a separable Gaussian
//
//  Blurring a WxH image with an NxN kernel costs N*N samples per pixel.
//  But a Gaussian blur is "separable": blurring horizontally and then
//  vertically gives the same result for only N+N samples. With N = 9 that
//  is 18 samples instead of 81 - the difference between smooth and
//  unusable on integrated graphics.
//
//  So C++ runs this shader twice, flipping 'horizontal' each time.
// ---------------------------------------------------------------------------
static const char* BLUR_FS = R"(
#version 330 core
in  vec2 vUV;
out vec4 FragColor;

uniform sampler2D image;
uniform bool horizontal;
uniform float spread;     // step between samples, in texels. >1 widens the
                          // glow for free: same 9 samples, spaced further out.
uniform float threshold;  // only light ABOVE this passes (the lens streaks
                          // use it so only the brightest points streak)

uniform int useMask;       // 1 = multiply by the image's alpha (the streak flag)

vec3 fetch(vec2 uv) {
    vec4 t = texture(image, uv);
    return max(t.rgb - vec3(threshold), vec3(0.0)) * (useMask == 1 ? t.a : 1.0);
}

// Gaussian weights. They sum to 1, so the blur does not brighten or darken
// the image overall.
const float weight[5] = float[](0.227027, 0.194594, 0.121621, 0.054054, 0.016216);

void main() {
    // Size of one texel, so we step exactly one pixel at a time.
    vec2 texel = spread / vec2(textureSize(image, 0));
    vec3 result = fetch(vUV) * weight[0];

    if (horizontal) {
        for (int i = 1; i < 5; ++i) {
            result += fetch(vUV + vec2(texel.x * i, 0.0)) * weight[i];
            result += fetch(vUV - vec2(texel.x * i, 0.0)) * weight[i];
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            result += fetch(vUV + vec2(0.0, texel.y * i)) * weight[i];
            result += fetch(vUV - vec2(0.0, texel.y * i)) * weight[i];
        }
    }
    FragColor = vec4(result, 1.0);
}
)";

// ---------------------------------------------------------------------------
//  6. COMPOSITE  -  the final image
//
//  Adds the blurred glow on top of the sharp scene, tone-maps it, grades its
//  colour, and corrects it for the monitor.
//
//  WHY TONE MAPPING?
//  After adding bloom, bright areas can exceed 1.0, which a monitor cannot
//  show - they would simply clip to flat white and lose all colour. A tone
//  map squeezes any brightness into 0..1 smoothly.
//
//  WHICH CURVE?
//  REINHARD:  c / (c + 1). Simple and soft - it keeps the dark walls readable
//  and the neon gentle. This is the one used.
//  ACES "filmic" (the acesFilm function below) was also tried: punchier, but
//  it crushed the dark buildings to black and over-saturated the neon, so
//  the softer Reinhard look was kept. It is left in so the two can be
//  compared by swapping one line.
//
//  COLOUR GRADING
//  One finishing adjustment: SATURATION pushes every colour away from its
//  own grey (its luminance) by the 'saturation' factor - set just above 1
//  for slightly richer colour.
// ---------------------------------------------------------------------------
static const char* COMPOSITE_FS = R"(
#version 330 core
in  vec2 vUV;
out vec4 FragColor;

uniform sampler2D scene;
uniform sampler2D bloomTex;
uniform float bloomStrength;
uniform float exposure;
uniform float saturation;     // 1 = unchanged, more = more vivid

// CAMERA EFFECTS (key X switches them off)
uniform sampler2D streakTex;  // horizontal lens streaks (anamorphic flares)
uniform float streakStrength;
uniform float aberration;     // chromatic aberration: colour fringes at the edges
uniform float grain;          // film grain
uniform float time;

vec3 acesFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    // CHROMATIC ABERRATION: a real lens bends red and blue light by slightly
    // different amounts, so toward the edges of the frame the colours part.
    // Read red a little outward and blue a little inward, by distance from
    // the centre (zero in the middle).
    vec2 fromC = vUV - 0.5;
    vec3 hdr;
    if (aberration > 0.0) {
        vec2 off = fromC * dot(fromC, fromC) * aberration;
        hdr = vec3(texture(scene, vUV + off).r, texture(scene, vUV).g, texture(scene, vUV - off).b);
    } else {
        hdr = texture(scene, vUV).rgb;
    }
    vec3 bloom  = texture(bloomTex, vUV).rgb;
    // ANAMORPHIC STREAKS: the brightest lights smeared far sideways (see
    // runStreakBlur in render.h), tinted blue like a cinema lens flare.
    vec3 streak = texture(streakTex, vUV).rgb * vec3(0.55, 0.75, 1.30);

    vec3 color = hdr + bloom * bloomStrength + streak * streakStrength;

    // exposure, then ACES filmic tone mapping
    color = color * exposure;
    color = color / (color + vec3(1.0));          // Reinhard tone mapping

    // ---- colour grading ----
    // Saturation: how far each pixel is from its own grey (its luminance),
    // scaled up.
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = max(mix(vec3(luma), color, saturation), 0.0);

    color = clamp(color, 0.0, 1.0);

    // Gamma correction. Monitors are non-linear; without this everything
    // looks muddy and dark. 1/2.2 is the standard sRGB approximation.
    color = pow(color, vec3(1.0 / 2.2));

    // A faint vignette focuses the eye toward the centre of the frame.
    vec2  d   = vUV - 0.5;
    float vig = 1.0 - dot(d, d) * 0.55;
    color *= vig;

    // FILM GRAIN: a new random value per pixel per frame, added very faintly.
    // It hides the banding of the smooth dark gradients in the sky and fog.
    float n = fract(sin(dot(gl_FragCoord.xy + fract(time * 7.13) * vec2(91.7, 37.3),
                            vec2(12.9898, 78.233))) * 43758.5453);
    color += (n - 0.5) * grain;

    FragColor = vec4(color, 1.0);
}
)";
