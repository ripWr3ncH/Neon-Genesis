// ===========================================================================
//  NEON GENESIS  -  a real-time cyberpunk city
//
//  Dewan Salman Rahman Zisan  -  2107015
//
//  ---------------------------------------------------------------------------
//  WHAT THIS PROGRAM DOES
//  ---------------------------------------------------------------------------
//  A city block is generated from a seed, then rendered at night with neon
//  lighting, and the camera can fly through the streets.
//
//  Nothing in the scene is hand-placed and nothing is pre-recorded. The
//  layout comes from one seed; every moving thing is derived from one clock.
//
//  ---------------------------------------------------------------------------
//  HOW A SINGLE FRAME IS BUILT  (the most important thing to be able to say)
//  ---------------------------------------------------------------------------
//    1. UPDATE     time of day, cars (traffic rules), rain, the camera tour
//    2. LIGHTS     pick the 24 lights that matter most to the camera
//    3. SKY        gradient, sun or moon, stars and clouds, drawn behind all
//    4. REFLECTION draw the city UPSIDE DOWN, below the road
//    5. GROUND     the wet road and the water drawn half see-through on top
//                  of it, so the upside-down city shows as a reflection
//    6. CITY       draw the city the right way up. Every lit pixel fires a
//                  RAY toward the sun / moon to find out whether a building
//                  shadows it (ray-traced shadows)
//    7. BLOOM      blur the glowing parts and add the blur back on top
//
//  Steps 3-6 go into an off-screen image, not the screen. Step 7 works on
//  that image - that is what makes neon bleed light onto the wall beside it.
//
//  ---------------------------------------------------------------------------
//  CONTROLS
//  ---------------------------------------------------------------------------
//    W A S D        move            SPACE / L-CTRL   up / down
//    MOUSE          look            SHIFT (hold)     move faster
//    TAB            free the cursor
//
//    1 / 2 / 3      shading mode: flat / Gouraud / Phong
//    P              pause or resume all animation
//    F              wireframe
//    B              bloom on / off
//    G              road reflections on / off
//    R              rain on / off
//    M              music on / off
//    T              next music track
//
//    C              get in / out of a car and DRIVE it:
//                     W accelerate    S brake, then reverse
//                     A / D steer     SPACE handbrake
//    L              show where every active light is
//    N              regenerate the city with a new seed
//    E              dusk <-> night (the sun sets, the street lamps come on)
//    H              ray-traced shadows on / off
//    X              detail effects on / off (light beams, puddles, facade
//                   relief, car reflections, holograms, lens effects)
//    V              camera TOUR - a ~1:45 automatic flight for recording
//    F12            save a screenshot
//    F11            fullscreen on / off
//    ESC            PAUSE MENU: every control on screen, RESUME or EXIT
//
//  ---------------------------------------------------------------------------
//  BUILD
//  ---------------------------------------------------------------------------
//    g++ -O2 src/main.cpp src/glad.c -Iinclude -o neon.exe -lglfw3 -lopengl32 -lgdi32 -lwinmm -lgdiplus
//  or just double-click build.bat.  (-lwinmm is Windows' multimedia library,
//  used for the background music; -lgdiplus is its image library, used to
//  read the billboard pictures.)
// ===========================================================================

// music.h brings in windows.h, which must come BEFORE glad.h: both define a
// macro called APIENTRY, and in this order they agree instead of clashing.
#include "music.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <cstdio>
#include <cstdlib>

#include "shaders.h"
#include "mesh.h"
#include "textures.h"
#include "images.h"
#include "city.h"
#include "render.h"
#include "traffic.h"
#include "menu.h"
#include "raytrace.h"

// ===========================================================================
//  WINDOW / CAMERA STATE
// ===========================================================================

static int g_width  = 1280;
static int g_height = 720;

// ---------------------------------------------------------------------------
//  THE CAMERA
//
//  There is no "camera" in OpenGL. The view matrix moves the whole WORLD so
//  that the camera ends up at the origin looking down -Z.
//
//  Direction comes from two angles:
//     yaw   - turning left and right
//     pitch - looking up and down
//  and spherical-to-Cartesian conversion turns them into a forward vector.
// ---------------------------------------------------------------------------
struct Camera {
    glm::vec3 position = glm::vec3(0.0f, 6.0f, 58.0f);
    glm::vec3 front    = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up       = glm::vec3(0.0f, 1.0f,  0.0f);

    float yaw   = -90.0f;    // -90 so we start looking down -Z, not down +X
    float pitch =  -4.0f;

    float speed       = 16.0f;
    float sensitivity = 0.09f;

    void updateVectors() {
        glm::vec3 f;
        f.x = cosf(glm::radians(yaw)) * cosf(glm::radians(pitch));
        f.y = sinf(glm::radians(pitch));
        f.z = sinf(glm::radians(yaw)) * cosf(glm::radians(pitch));
        front = glm::normalize(f);
    }

    glm::mat4 viewMatrix() const {
        return glm::lookAt(position, position + front, up);
    }
};

static Camera g_cam;
static bool   g_shotDrive = false;     // --shot test: drive automatically
static bool   g_shotDusk  = false;     // --shot test: at sunset
static bool   g_shotTour  = false;     // --shot test: during the camera tour
static bool   g_shotNoRT  = false;     // --shot test: ray-traced shadows off
static bool   g_shotNoFX  = false;     // --shot test: detail effects off
static bool   g_shotNoCars = false;    // --shot test: no AI traffic (clean driving demo)
static bool   g_shotHD     = false;    // --shot test: render and record at 1920 x 1080 off-screen
static bool   g_shotRecord = false;    // --shot test: record a VIDEO (needs ffmpeg)

// ---------------------------------------------------------------------------
//  SCRIPTED KEYS (for recording demo videos)
//  With the "keys" option, <shotPath>.keys lists lines "start end KEY" in
//  seconds of scene time, e.g. "12.0 12.2 B" or "40 46 W". While the shot
//  runs, that key counts as held down between start and end, exactly as if
//  it were pressed - so the recording shows the real feature, frame-exact.
// ---------------------------------------------------------------------------
struct ScriptedKey { float t0, t1; int key; };
static std::vector<ScriptedKey> g_keyScript;
static float g_scriptTime = 0.0f;

static bool g_menuOpen = false;          // pause menu showing: game keys are ignored

static bool keyDown(GLFWwindow* w, int key) {
    if (g_menuOpen) return false;
    if (glfwGetKey(w, key) == GLFW_PRESS) return true;
    for (const ScriptedKey& k : g_keyScript)
        if (k.key == key && g_scriptTime >= k.t0 && g_scriptTime < k.t1) return true;
    return false;
}

static void loadKeyScript(const std::string& file) {
    FILE* f = fopen(file.c_str(), "r");
    if (!f) { std::cout << "No key script " << file << "\n"; return; }
    char name[32]; float a, b;
    while (fscanf(f, "%f %f %31s", &a, &b, name) == 3) {
        std::string n = name; int key = 0;
        if      (n == "SPACE") key = GLFW_KEY_SPACE;
        else if (n == "SHIFT") key = GLFW_KEY_LEFT_SHIFT;
        else if (n == "LEFT")  key = GLFW_KEY_LEFT;
        else if (n == "RIGHT") key = GLFW_KEY_RIGHT;
        else if (n.size() == 1) key = (int)toupper((unsigned char)n[0]);   // GLFW letter/digit codes are ASCII
        if (key) g_keyScript.push_back({ a, b, key });
    }
    fclose(f);
    std::cout << "Key script: " << g_keyScript.size() << " entries\n";
}
static bool   g_mouseCaptured = true;
static bool   g_firstMouse    = true;
static float  g_lastX = 0.0f, g_lastY = 0.0f;

// GLFW callbacks are plain C function pointers, so they cannot capture
// local variables - the state they touch has to live at file scope.
static void mouse_callback(GLFWwindow*, double xpos, double ypos) {
    if (!g_mouseCaptured) return;

    // On the first event the cursor could be anywhere on screen; without
    // this guard the camera would snap violently on the first mouse move.
    if (g_firstMouse) {
        g_lastX = (float)xpos;
        g_lastY = (float)ypos;
        g_firstMouse = false;
    }

    float dx = (float)xpos - g_lastX;
    float dy = g_lastY - (float)ypos;     // screen Y grows downward, pitch grows up
    g_lastX = (float)xpos;
    g_lastY = (float)ypos;

    g_cam.yaw   += dx * g_cam.sensitivity;
    g_cam.pitch += dy * g_cam.sensitivity;

    // At exactly +/-90 the forward vector becomes parallel to 'up', the cross
    // product inside lookAt collapses to zero, and the view flips inside out.
    // Every engine clamps this.
    g_cam.pitch = std::max(-89.0f, std::min(89.0f, g_cam.pitch));

    g_cam.updateVectors();
}

static void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    if (g_shotHD) return;               // HD recording keeps its fixed 1920 x 1080 size
    g_width  = (w > 0) ? w : 1;
    g_height = (h > 0) ? h : 1;
    glViewport(0, 0, g_width, g_height);
}

// ===========================================================================
//  LIGHTS
//
//  The shader takes a fixed-size array of 24 lights. The city has 64 street
//  lamps, 36 headlights, a flying vehicle and dozens of neon strips - far
//  more than a loop in a fragment shader can afford on integrated graphics.
//
//  So every frame we score every light by distance to the camera and keep
//  the 24 best. A lamp 100 units away adds nothing visible, so the picture
//  is effectively the same, and the cost stays flat however big the city is.
//
//  Moving lights (cars, the flyer) get a bonus in that scoring. Without it
//  the 64 street lamps, being everywhere, fill every slot and the headlights
//  never get lit at all - which is exactly the bug an earlier version had.
// ===========================================================================
#define MAX_LIGHTS 24

struct LightSource {
    glm::vec3 position;
    glm::vec3 color;
    float     range;
    // Spotlight settings. cosCutoff = -2 means "not a spot": a point light
    // that shines in every direction.
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    float     cosCutoff = -2.0f;
    float     spotExp   = 1.0f;
};

static std::vector<LightSource> g_lights;

// ===========================================================================
//  UNIFORM LOCATIONS
//
//  Looked up ONCE after linking. glGetUniformLocation is a string lookup, so
//  calling it inside the draw loop - thousands of times a frame - would be
//  pure waste.
// ===========================================================================
struct SceneUniforms {
    GLint model, view, projection, normalMatrix;
    GLint viewPos, tint, shadingMode, ambient;
    GLint emissive, emissiveColor;
    GLint useTexture, tex0, uvWorldScale, uvMode, texEmissive;
    GLint fogColor, fogDensity;
    GLint specularStrength, shininess, wetness, alpha;
    GLint moonDir, moonColor, lightCount;
    GLint lightPos[MAX_LIGHTS], lightColor[MAX_LIGHTS], lightRange[MAX_LIGHTS];
    GLint lightDir[MAX_LIGHTS], lightCosCutoff[MAX_LIGHTS], lightSpotExp[MAX_LIGHTS];
    GLint time, windowSeed, bloomAmount, uvOffset;
    GLint clipPlane, waves;
    GLint detail, puddles, mirrorOn, rainAmt, envReflect, beam, holo, streak, shoreEdge, outScale;
    GLint sunDir, sunColor, rtEnabled, rtBoxes;
    GLint rtCellStart[16], rtCellCount[16];
};

static void cacheSceneUniforms(GLuint prog, SceneUniforms& u) {
    u.model            = glGetUniformLocation(prog, "model");
    u.view             = glGetUniformLocation(prog, "view");
    u.projection       = glGetUniformLocation(prog, "projection");
    u.normalMatrix     = glGetUniformLocation(prog, "normalMatrix");
    u.viewPos          = glGetUniformLocation(prog, "viewPos");
    u.tint             = glGetUniformLocation(prog, "tint");
    u.shadingMode      = glGetUniformLocation(prog, "shadingMode");
    u.ambient          = glGetUniformLocation(prog, "ambient");
    u.emissive         = glGetUniformLocation(prog, "emissive");
    u.emissiveColor    = glGetUniformLocation(prog, "emissiveColor");
    u.useTexture       = glGetUniformLocation(prog, "useTexture");
    u.tex0             = glGetUniformLocation(prog, "tex0");
    u.uvWorldScale     = glGetUniformLocation(prog, "uvWorldScale");
    u.uvMode           = glGetUniformLocation(prog, "uvMode");
    u.texEmissive      = glGetUniformLocation(prog, "texEmissive");
    u.fogColor         = glGetUniformLocation(prog, "fogColor");
    u.fogDensity       = glGetUniformLocation(prog, "fogDensity");
    u.specularStrength = glGetUniformLocation(prog, "specularStrength");
    u.shininess        = glGetUniformLocation(prog, "shininess");
    u.wetness          = glGetUniformLocation(prog, "wetness");
    u.alpha            = glGetUniformLocation(prog, "alpha");
    u.moonDir          = glGetUniformLocation(prog, "moonDir");
    u.moonColor        = glGetUniformLocation(prog, "moonColor");
    u.lightCount       = glGetUniformLocation(prog, "lightCount");
    u.time             = glGetUniformLocation(prog, "time");
    u.windowSeed       = glGetUniformLocation(prog, "windowSeed");
    u.bloomAmount      = glGetUniformLocation(prog, "bloomAmount");
    u.uvOffset         = glGetUniformLocation(prog, "uvOffset");
    u.clipPlane        = glGetUniformLocation(prog, "clipPlane");
    u.waves            = glGetUniformLocation(prog, "waves");
    u.detail           = glGetUniformLocation(prog, "detail");
    u.puddles          = glGetUniformLocation(prog, "puddles");
    u.mirrorOn         = glGetUniformLocation(prog, "mirrorOn");
    u.rainAmt          = glGetUniformLocation(prog, "rainAmt");
    u.envReflect       = glGetUniformLocation(prog, "envReflect");
    u.beam             = glGetUniformLocation(prog, "beam");
    u.holo             = glGetUniformLocation(prog, "holo");
    u.streak           = glGetUniformLocation(prog, "streak");
    u.shoreEdge        = glGetUniformLocation(prog, "shoreEdge");
    u.outScale         = glGetUniformLocation(prog, "outScale");
    u.sunDir           = glGetUniformLocation(prog, "sunDir");
    u.sunColor         = glGetUniformLocation(prog, "sunColor");
    u.rtEnabled        = glGetUniformLocation(prog, "rtEnabled");
    u.rtBoxes          = glGetUniformLocation(prog, "rtBoxes");
    for (int c = 0; c < 16; ++c) {
        char buf[64];
        snprintf(buf, sizeof(buf), "rtCellStart[%d]", c);
        u.rtCellStart[c] = glGetUniformLocation(prog, buf);
        snprintf(buf, sizeof(buf), "rtCellCount[%d]", c);
        u.rtCellCount[c] = glGetUniformLocation(prog, buf);
    }

    for (int i = 0; i < MAX_LIGHTS; ++i) {
        char buf[64];
        snprintf(buf, sizeof(buf), "lightPos[%d]", i);
        u.lightPos[i]   = glGetUniformLocation(prog, buf);
        snprintf(buf, sizeof(buf), "lightColor[%d]", i);
        u.lightColor[i] = glGetUniformLocation(prog, buf);
        snprintf(buf, sizeof(buf), "lightRange[%d]", i);
        u.lightRange[i] = glGetUniformLocation(prog, buf);
        snprintf(buf, sizeof(buf), "lightDir[%d]", i);
        u.lightDir[i] = glGetUniformLocation(prog, buf);
        snprintf(buf, sizeof(buf), "lightCosCutoff[%d]", i);
        u.lightCosCutoff[i] = glGetUniformLocation(prog, buf);
        snprintf(buf, sizeof(buf), "lightSpotExp[%d]", i);
        u.lightSpotExp[i] = glGetUniformLocation(prog, buf);
    }
}

// ===========================================================================
//  DRAW HELPERS
//
//  Every object in the scene goes through one of four calls:
//
//    drawLit      - an ordinary solid lit by the lamps (poles, car bodies)
//    drawFacade   - a lit wall whose texture is a grid of glowing windows
//    drawWet      - the road and pavements: textured, shiny, see-through
//    drawEmissive - something that IS light (neon, headlights, billboards)
//
//  g_mirror is applied in front of every model matrix. Normally it does
//  nothing; during the reflection pass it is scale(1,-1,1), which flips the
//  whole city upside down below the road. Putting it here means the drawing
//  code for the reflection and for the real city is literally the same code.
// ===========================================================================

static SceneUniforms U;
static GLuint        g_whiteTex = 0;
static glm::mat4     g_mirror   = glm::mat4(1.0f);

static void setModel(const glm::mat4& localModel) {
    glm::mat4 model = g_mirror * localModel;
    glUniformMatrix4fv(U.model, 1, GL_FALSE, glm::value_ptr(model));

    // A normal is a direction, so uneven scaling would skew it. The standard
    // correction is the inverse-transpose of the model matrix.
    glm::mat3 nrm = glm::mat3(glm::transpose(glm::inverse(model)));
    glUniformMatrix3fv(U.normalMatrix, 1, GL_FALSE, glm::value_ptr(nrm));
}

// Fractional part, always in 0..1 (also for negative numbers).
static inline float fract01(float x) { return x - floorf(x); }

// The six window textures, each with its own character (see WindowStyle).
static const WindowStyle WINDOW_STYLES[6] = {
    //  lit    cool   warm colour
    { 0.35f, 0.25f, 1.00f, 0.85f, 0.55f },   // ordinary offices
    { 0.20f, 0.10f, 1.00f, 0.78f, 0.45f },   // quiet: few lights, all warm
    { 0.50f, 0.70f, 0.95f, 0.95f, 0.90f },   // busy tech offices: cool white and cyan
    { 0.30f, 0.15f, 1.00f, 0.62f, 0.30f },   // sodium-orange apartments
    { 0.40f, 0.20f, 1.00f, 0.55f, 0.75f },   // pink-lit flats / hotel
    { 0.25f, 0.45f, 0.80f, 0.90f, 1.00f },   // pale blue night shift
};

// The flying vehicles: paint, engine glow (back) and under-glow strip.
static const int NUM_FLYERS = 2;
static const glm::vec3 FLYER_PAINT[NUM_FLYERS]  = { glm::vec3(0.12f, 0.06f, 0.16f), glm::vec3(0.05f, 0.10f, 0.12f) };
static const glm::vec3 FLYER_ENGINE[NUM_FLYERS] = { glm::vec3(0.30f, 0.85f, 1.00f), glm::vec3(1.00f, 0.55f, 0.15f) };
static const glm::vec3 FLYER_GLOW[NUM_FLYERS]   = { glm::vec3(1.00f, 0.30f, 0.70f), glm::vec3(0.20f, 1.00f, 0.80f) };

// One place that sets every per-object value, so nothing can "leak" from the
// previous object into the next one.
static void drawSurface(const Mesh& mesh, const glm::mat4& model, const glm::vec3& tint,
                        GLuint texture, const glm::vec3& uvScale, int uvMode,
                        float texEmissive, float specular, float shininess,
                        float wetness, float alpha) {
    setModel(model);
    glUniform3fv(U.tint, 1, glm::value_ptr(tint));
    glUniform1f(U.emissive, 0.0f);
    glUniform1i(U.useTexture, texture ? 1 : 0);
    glUniform3fv(U.uvWorldScale, 1, glm::value_ptr(uvScale));
    glUniform1i(U.uvMode, uvMode);
    glUniform1f(U.texEmissive, texEmissive);
    glUniform1f(U.specularStrength, specular);
    glUniform1f(U.shininess, shininess);
    glUniform1f(U.wetness, wetness);
    glUniform1f(U.alpha, alpha);
    glUniform2f(U.uvOffset, 0.0f, 0.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture ? texture : g_whiteTex);
    drawMesh(mesh);
}

static void drawLit(const Mesh& mesh, const glm::mat4& model, const glm::vec3& tint,
                    float specular = 0.25f, float shininess = 24.0f) {
    drawSurface(mesh, model, tint, 0, glm::vec3(1.0f), 0, 0.0f, specular, shininess, 0.0f, 1.0f);
}

static void drawFacade(const Mesh& mesh, const glm::mat4& model, const glm::vec3& tint,
                       GLuint windowTexture, const glm::vec3& uvScale, int uvMode,
                       float windowSeed) {
    // Each building passes its own seed, so its windows switch on and off
    // on a different schedule from its neighbours'.
    glUniform1f(U.windowSeed, windowSeed);
    // Low specular: concrete is matt. Shiny walls turned pale under the lamps.
    drawSurface(mesh, model, tint, windowTexture, uvScale, uvMode, 0.6f, 0.12f, 24.0f, 0.0f, 1.0f);
}

static void drawWet(const Mesh& mesh, const glm::mat4& model, const glm::vec3& tint,
                    GLuint texture, const glm::vec3& uvScale, float wetness, float alpha,
                    float specular = 0.9f) {
    drawSurface(mesh, model, tint, texture, uvScale, 0, 0.0f, specular, 90.0f, wetness, alpha);
}

// uvScale / uvOffset pick a region of the texture - billboards use them to
// show one frame of their animation strip and to scroll it sideways.
static void drawEmissive(const Mesh& mesh, const glm::mat4& model,
                         const glm::vec3& color, float intensity = 1.0f,
                         GLuint texture = 0,
                         const glm::vec2& uvScale  = glm::vec2(1.0f),
                         const glm::vec2& uvOffset = glm::vec2(0.0f),
                         float bloomAmount = 1.0f) {
    setModel(model);
    glUniform3f(U.tint, 1.0f, 1.0f, 1.0f);
    glUniform1f(U.emissive, 1.0f);
    glUniform3fv(U.emissiveColor, 1, glm::value_ptr(color * intensity));
    glUniform1i(U.useTexture, texture ? 1 : 0);
    glUniform3f(U.uvWorldScale, uvScale.x, uvScale.y, 1.0f);
    glUniform2fv(U.uvOffset, 1, glm::value_ptr(uvOffset));
    glUniform1i(U.uvMode, 0);
    glUniform1f(U.alpha, 1.0f);
    glUniform1f(U.bloomAmount, bloomAmount);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture ? texture : g_whiteTex);
    drawMesh(mesh);
}

// Send the chosen lights to the shader. For the reflection pass every light
// is mirrored below the road too, so the upside-down city is lit from the
// matching upside-down positions.
// 'maxCount' limits how many are sent. The lights are sorted best-first, so
// sending fewer simply drops the least important ones.
static void uploadLights(bool mirrored, int maxCount = MAX_LIGHTS) {
    glUniform1i(U.lightCount, std::min((int)g_lights.size(), maxCount));
    for (size_t i = 0; i < g_lights.size(); ++i) {
        const LightSource& L = g_lights[i];
        glm::vec3 p = L.position;
        glm::vec3 d = L.direction;
        // Mirroring the world flips heights, so a lamp shining DOWN on the
        // road becomes one shining UP on the upside-down city. Both the
        // position and the beam direction are flipped.
        if (mirrored) { p.y = -p.y; d.y = -d.y; }
        glUniform3fv(U.lightPos[i],   1, glm::value_ptr(p));
        glUniform3fv(U.lightColor[i], 1, glm::value_ptr(L.color));
        glUniform1f (U.lightRange[i], L.range);
        glUniform3fv(U.lightDir[i],   1, glm::value_ptr(d));
        glUniform1f (U.lightCosCutoff[i], L.cosCutoff);
        glUniform1f (U.lightSpotExp[i],   L.spotExp);
    }
}

// ===========================================================================
//  SMALL MATHS HELPERS
// ===========================================================================

// Which yaw angle turns an object's +X axis to face 'dir'?
//
// rotate(angle, Y) sends +X to (cos a, 0, -sin a). Setting that equal to
// (dir.x, 0, dir.z) gives  a = atan2(-dir.z, dir.x). Used to point cars and
// the flying vehicle the way they are travelling.
static float yawFromDirection(const glm::vec3& dir) {
    return atan2f(-dir.z, dir.x);
}

// Where the moon is: a unit vector from the viewer TOWARD it. About 24 degrees
// above the horizon, ahead and to the right of the starting view (which looks
// down -Z). The sky shader draws the moon in this direction, and the scene's
// moonlight shines the opposite way - from the moon - so the light on the
// buildings really comes from where the moon is seen.
static const glm::vec3 MOON_DIR = glm::normalize(glm::vec3(0.55f, 0.40f, -0.73f));

// The compass direction of the setting sun. It sits BEHIND the city as seen
// from the tour's opening shot and ahead-right of the starting view, so the
// towers stand dark against the glow and their shadows stretch toward the
// viewer. It is the same part of the sky the moon rises in, so the sun sets
// and the moon takes over in one place. Its height comes from the time of day.
static const glm::vec3 SUN_AZIMUTH = glm::normalize(glm::vec3(0.60f, 0.0f, -0.80f));

// ===========================================================================
//  CAMERA TOUR  (press V)
//
//  A ~1:45 automatic flight through the city, made for recording the video.
//  The path is a list of KEYS - a time, a camera position and a point to
//  look at - and between keys the camera follows a CATMULL-ROM SPLINE: a
//  smooth curve that passes exactly through every key. For the stretch from
//  key 1 to key 2 it also uses keys 0 and 3, so the curve arrives and leaves
//  each key with a sensible direction instead of making a sharp corner:
//
//    p(u) = 0.5 * ( 2*p1 + (p2 - p0)*u + (2p0 - 5p1 + 4p2 - p3)*u^2
//                                        + (3p1 - p0 - 3p2 + p3)*u^3 )
//
//  with u going 0 -> 1 between p1 and p2. Positions and look-at points are
//  smoothed the same way, so both the movement and the turning are smooth.
//
//  The keys keep to open space - over the water, along the avenues, above
//  the roofs - so the camera never passes through a building.
// ===========================================================================
struct TourKey { float t; glm::vec3 pos, look; };
static const TourKey TOUR[] = {
    {   0.0f, glm::vec3(-150.0f, 75.0f, 170.0f), glm::vec3(  0.0f, 10.0f,    0.0f) },  // wide: sunset, long shadows
    {  11.0f, glm::vec3( -95.0f, 50.0f, 115.0f), glm::vec3(  0.0f, 12.0f,    0.0f) },  // over the water
    {  21.0f, glm::vec3(   0.0f, 24.0f,  98.0f), glm::vec3(  0.0f,  6.0f,   30.0f) },  // line up with the avenue
    {  30.0f, glm::vec3(   0.0f,  5.0f,  55.0f), glm::vec3(  0.0f,  4.0f,   10.0f) },  // street level: lamps come on
    {  40.0f, glm::vec3(   0.0f,  4.5f,  18.0f), glm::vec3(-30.0f,  8.0f,   -4.0f) },  // down the avenue, traffic
    {  50.0f, glm::vec3(  -4.0f,  7.0f,   0.0f), glm::vec3(-50.0f, 22.0f,   -9.0f) },  // the big billboard
    {  60.0f, glm::vec3( -30.0f,  6.0f,   0.0f), glm::vec3(-70.0f, 12.0f,  -25.0f) },  // along the cross avenue
    {  66.0f, glm::vec3( -62.0f,  9.0f,   0.0f), glm::vec3(-120.0f, 10.0f, -50.0f) }, // the ring road, the tunnel
    {  74.0f, glm::vec3( -85.0f, 16.0f,  -5.0f), glm::vec3(-185.0f, 10.0f, -102.0f) },// out over the water: bridge, district
    {  86.0f, glm::vec3( -60.0f, 45.0f, -100.0f), glm::vec3( 0.0f, 15.0f,    0.0f) },  // rise, look back at the city
    // the closing shot sweeps round the west side to look at the city WITH the
    // moon above it (the moon is to the north-east, so the camera ends up
    // south-west of the city, looking almost level)
    { 100.0f, glm::vec3(-110.0f, 35.0f,  145.0f), glm::vec3( 20.0f, 30.0f,  -20.0f) },  // final shot: city under the moon
    { 106.0f, glm::vec3(-100.0f, 34.0f,  132.0f), glm::vec3( 20.0f, 30.0f,  -20.0f) },
};
static const int   TOUR_KEYS     = (int)(sizeof(TOUR) / sizeof(TOUR[0]));
static const float TOUR_LENGTH   = 106.0f;
static const float TOUR_NIGHTFALL = 26.0f;    // when the sun starts to set

static glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1,
                            const glm::vec3& p2, const glm::vec3& p3, float u) {
    float u2 = u * u, u3 = u2 * u;
    return 0.5f * (2.0f * p1 + (p2 - p0) * u
                   + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2
                   + (3.0f * p1 - p0 - 3.0f * p2 + p3) * u3);
}

static void tourSample(float t, glm::vec3& pos, glm::vec3& look) {
    t = std::max(0.0f, std::min(TOUR_LENGTH, t));
    int i = 0;
    while (i < TOUR_KEYS - 2 && t > TOUR[i + 1].t) ++i;          // the stretch t is in
    const TourKey& a = TOUR[std::max(i - 1, 0)];
    const TourKey& b = TOUR[i];
    const TourKey& c = TOUR[i + 1];
    const TourKey& d = TOUR[std::min(i + 2, TOUR_KEYS - 1)];
    float u = (t - b.t) / (c.t - b.t);
    pos  = catmullRom(a.pos,  b.pos,  c.pos,  d.pos,  u);
    look = catmullRom(a.look, b.look, c.look, d.look, u);
}

// A repeatable 0..1 number per integer - used to give each street lamp its
// own moment to switch on, so they come on in a ripple, not all at once.
static float hash01(int i) {
    float x = sinf((float)i * 12.9898f) * 43758.5453f;
    return x - floorf(x);
}

// (The traffic-light timetable, signalFor(), lives in traffic.h - the cars
// obey the same function the lights are drawn from, so they always agree.)

// ===========================================================================
//  SCREENSHOT  (F12)
//
//  Reads the finished frame back from the GPU and writes it as a .bmp.
//  BMP is used because it needs no library: a 54-byte header followed by raw
//  pixels. Two details line up conveniently with OpenGL:
//    - BMP stores rows bottom-to-top, and so does glReadPixels
//    - BMP pads each row to a multiple of 4 bytes, and so does OpenGL's
//      default GL_PACK_ALIGNMENT of 4
//  so the pixel block can be written exactly as OpenGL returns it.
// ===========================================================================
static bool saveScreenshot(const char* path, int w, int h) {
    int rowBytes = (w * 3 + 3) & ~3;             // round up to a multiple of 4
    std::vector<unsigned char> px(rowBytes * h);

    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, px.data());   // BMP wants BGR

    FILE* f = fopen(path, "wb");
    if (!f) return false;

    unsigned int fileSize = 54 + (unsigned int)px.size();
    unsigned char header[54] = { 'B','M' };
    auto put32 = [&](int off, unsigned int v) {
        header[off] = v & 255; header[off+1] = (v >> 8) & 255;
        header[off+2] = (v >> 16) & 255; header[off+3] = (v >> 24) & 255;
    };
    put32(2, fileSize);
    put32(10, 54);             // where the pixels start
    put32(14, 40);             // size of the info header
    put32(18, (unsigned)w);
    put32(22, (unsigned)h);
    header[26] = 1;            // colour planes
    header[28] = 24;           // bits per pixel
    put32(34, (unsigned int)px.size());

    fwrite(header, 1, 54, f);
    fwrite(px.data(), 1, px.size(), f);
    fclose(f);
    return true;
}

// ===========================================================================
//  MAIN
//
//  Normally takes no arguments. For making report images it also accepts
//      neon.exe --shot out.bmp  x y z  yaw pitch  [shading] [seconds]
//  which places the camera, runs the scene for a while (5 s by default),
//  saves the picture and exits - without grabbing the mouse or stealing
//  focus. 'shading' picks the mode: 0 flat, 1 Gouraud, 2 Phong. Shooting the
//  same spot at two different 'seconds' shows the animation working.
// ===========================================================================
int main(int argc, char** argv) {
    const char* shotPath = nullptr;
    int         shotShading = 2;
    int         shotFrame   = 300;     // 5 seconds at the fixed 1/60 step
    if (argc >= 3 && std::string(argv[1]) == "--shot") {
        shotPath = argv[2];
        if (argc >= 8) {
            g_cam.position = glm::vec3((float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5]));
            g_cam.yaw      = (float)atof(argv[6]);
            g_cam.pitch    = (float)atof(argv[7]);
        }
        if (argc >= 9)  shotShading = atoi(argv[8]);
        if (argc >= 10) shotFrame   = std::max(2, (int)(atof(argv[9]) * 60.0));
        // "drive" as an 11th argument: get into a car at the camera and hold
        // the accelerator - used to test the driving and the chase camera.
        if (argc >= 11 && std::string(argv[10]) == "drive") g_shotDrive = true;
        // "dusk" shoots at sunset; "tour" starts the camera tour, so
        // 'seconds' becomes a moment in the tour.
        // Options can be combined, e.g. "dusk-nort" or "tour-nort" ("nort" =
        // ray-traced shadows off), for before/after comparisons.
        if (argc >= 11) {
            std::string opt = argv[10];
            if (opt.find("dusk") != std::string::npos) g_shotDusk = true;
            if (opt.find("tour") != std::string::npos) g_shotTour = true;
            if (opt.find("nort") != std::string::npos) g_shotNoRT = true;
            if (opt.find("nofx") != std::string::npos) g_shotNoFX = true;
            // "rec": record a video instead of one picture. shotPath is then
            // the .mp4 to write and 'seconds' its length, e.g.
            //   neon.exe --shot tour.mp4 0 0 0 0 0 2 106 tour-rec
            if (opt.find("rec")  != std::string::npos) g_shotRecord = true;
            if (opt.find("nocars") != std::string::npos) g_shotNoCars = true;
            // "hd": the frame is drawn at 1920 x 1080 into an off-screen buffer
            // (a laptop screen cannot show a window that large) and recorded
            // from there, at full resolution.
            if (opt.find("hd") != std::string::npos) { g_shotHD = true; g_width = 1920; g_height = 1080; }
            if (opt.find("keys") != std::string::npos) loadKeyScript(std::string(shotPath) + ".keys");
        }
    }

    // ---- window and OpenGL context ----------------------------------------
    if (!glfwInit()) {
        std::cout << "Failed to init GLFW\n";
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // In --shot mode the window must not steal focus: it opens in the
    // background, renders, saves and closes without interrupting whatever
    // the user is doing.
    if (shotPath) {
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }

    GLFWwindow* window = glfwCreateWindow(g_width, g_height, "Neon Genesis", NULL, NULL);
    if (!window) {
        std::cout << "Failed to create window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);

    // Lock the cursor to the window for mouse-look - but NOT in --shot mode,
    // where nobody is steering and grabbing the mouse would just freeze the
    // user's cursor until the program exits.
    if (shotPath) {
        g_mouseCaptured = false;
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    // GLAD must load AFTER the context is current, or every gl* call crashes.
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialise GLAD\n";
        return -1;
    }

    // On a high-DPI screen the window's pixel size can differ from the size
    // we asked for, so read the real one rather than trusting 1280 x 720.
    if (!g_shotHD) glfwGetFramebufferSize(window, &g_width, &g_height);   // HD keeps 1920 x 1080

    // Wait for one screen refresh before showing each frame. Without this the
    // GPU renders hundreds of pointless frames a second and the laptop gets
    // hot for no visible benefit.
    glfwSwapInterval(1);

    // In --shot mode, switch the cap OFF so the FPS printed is what the
    // hardware can really do, not just the monitor's refresh rate.
    if (shotPath) glfwSwapInterval(0);

    std::cout << "=====================================================\n";
    std::cout << "  NEON GENESIS\n";
    std::cout << "  GPU: " << glGetString(GL_RENDERER) << "\n";
    std::cout << "  GL : " << glGetString(GL_VERSION) << "\n";
    std::cout << "=====================================================\n";

    // ---- global GL state ---------------------------------------------------
    // Depth testing is what makes 3D work: without it, whatever is drawn LAST
    // appears in front regardless of distance.
    glEnable(GL_DEPTH_TEST);

    // Back-face culling throws away the ~50% of triangles pointing away from
    // the camera. It relies on every shape being wound counter-clockwise.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Used by the see-through road.
    // Colour: the usual "over" blend. Alpha: keep what is already stored
    // (src * 0 + dst * 1). The bright image's alpha holds the lens-streak
    // flag, and a see-through road drawn on top must not overwrite it.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

    // ---- shaders -----------------------------------------------------------
    GLuint sceneProg     = makeProgram(SCENE_VS.c_str(), SCENE_FS.c_str(), "scene");
    GLuint skyProg       = makeProgram(SKY_VS,   SKY_FS,   "sky");
    GLuint blurProg      = makeProgram(POST_VS,  BLUR_FS,  "blur");
    GLuint compositeProg = makeProgram(POST_VS,  COMPOSITE_FS, "composite");
    GLuint menuProg      = makeProgram(POST_VS,  MENU_FS,      "menu");
    PauseMenu pauseMenu;
    createPauseMenu(pauseMenu);

    cacheSceneUniforms(sceneProg, U);

    GLint skyView  = glGetUniformLocation(skyProg, "view");
    GLint skyProj  = glGetUniformLocation(skyProg, "projection");
    GLint skyHoriz = glGetUniformLocation(skyProg, "horizonColor");
    GLint skyZen   = glGetUniformLocation(skyProg, "zenithColor");
    GLint skyTime  = glGetUniformLocation(skyProg, "time");
    GLint skyMoon  = glGetUniformLocation(skyProg, "moonDirection");
    GLint skySun   = glGetUniformLocation(skyProg, "sunDirection");
    GLint skyDusk  = glGetUniformLocation(skyProg, "dusk");

    GLint cmpScene    = glGetUniformLocation(compositeProg, "scene");
    GLint cmpBloom    = glGetUniformLocation(compositeProg, "bloomTex");
    GLint cmpStrength = glGetUniformLocation(compositeProg, "bloomStrength");
    GLint cmpExposure = glGetUniformLocation(compositeProg, "exposure");
    GLint cmpSaturation = glGetUniformLocation(compositeProg, "saturation");
    GLint cmpStreakTex  = glGetUniformLocation(compositeProg, "streakTex");
    GLint cmpStreak     = glGetUniformLocation(compositeProg, "streakStrength");
    GLint cmpAberration = glGetUniformLocation(compositeProg, "aberration");
    GLint cmpGrain      = glGetUniformLocation(compositeProg, "grain");
    GLint cmpTime       = glGetUniformLocation(compositeProg, "time");

    // ---- geometry: every mesh is built once, here --------------------------
    Mesh cube      = makeCube();
    Mesh cylinder  = makeCylinder(20);
    Mesh tube      = makeCylinder(28, false);   // open-ended, for neon rings
    Mesh pyramid   = makePyramid();
    Mesh quad      = makeQuad();
    Mesh panel     = makePanel();
    Mesh skybox    = makeSkybox();
    Mesh octagon   = makeExtrusion(octagonOutline(),  true);
    Mesh carBody   = makeExtrusion(carBodyOutline(),  false);
    Mesh carCabin  = makeExtrusion(carCabinOutline(), false);
    Mesh cone      = makeCone(20);              // light beams
    const float OCT_PERIMETER = octagonPerimeter();

    // ---- rain: ONE buffer of line segments ----------------------------------
    // Each drop is a line - two vertices, top and bottom. The positions change
    // every frame, so the buffer is created empty with GL_DYNAMIC_DRAW (a hint
    // that it will be rewritten often) and refilled each frame. All 900 drops
    // are then drawn with a single glDrawArrays call.
    //
    // Lines are always 1 pixel wide however close they are, so a drop that
    // falls right past the camera stays a thin streak instead of becoming a
    // big grey slab - the problem with drawing each drop as a tiny box.
    //
    // The same buffer also holds the SPLASH rings drawn where drops hit the
    // ground, so rain and splashes together are still one draw call.
    const int RAIN_COUNT   = 1500;
    const int SPLASH_MAX   = 500;     // splashes alive at once, at most
    const int SPLASH_SEGS  = 14;      // each ring is a 14-sided polygon
    const int SPLASH_DROPS = 4;       // tiny droplets thrown up by each splash
    const int RAIN_VERTS   = RAIN_COUNT * 2 + SPLASH_MAX * (SPLASH_SEGS + SPLASH_DROPS) * 2;
    GLuint rainVAO, rainVBO;
    glGenVertexArrays(1, &rainVAO);
    glGenBuffers(1, &rainVBO);
    glBindVertexArray(rainVAO);
    glBindBuffer(GL_ARRAY_BUFFER, rainVBO);
    glBufferData(GL_ARRAY_BUFFER, RAIN_VERTS * FLOATS_PER_VERTEX * sizeof(float),
                 nullptr, GL_DYNAMIC_DRAW);
    {
        // same 11-float layout as every other mesh, so the scene shader can
        // draw it without any changes
        const GLsizei stride = FLOATS_PER_VERTEX * sizeof(float);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)(9 * sizeof(float)));
        glEnableVertexAttribArray(3);
    }
    glBindVertexArray(0);
    std::vector<float> rainVerts;
    rainVerts.reserve(RAIN_VERTS * FLOATS_PER_VERTEX);

    // A splash: a ripple ring spreading out where a drop hit the ground.
    // They live in a fixed-size pool, reused round-robin - the oldest one is
    // simply overwritten - so nothing is allocated while running.
    struct Splash { glm::vec3 pos; float age; bool alive; };
    std::vector<Splash> splashes(SPLASH_MAX, Splash{ glm::vec3(0.0f), 0.0f, false });
    int nextSplash = 0;
    const float SPLASH_LIFE = 0.35f;     // seconds a ripple lasts

    // Rain needs its own random numbers to re-scatter drops as they land.
    Random rainRng(2107015u ^ 0xA5A5A5A5u);

    // ---- textures, generated in code ---------------------------------------
    const int NUM_WINDOW_TEX    = 6;
    const int NUM_BILLBOARD_TEX = 5;
    GLuint windowTex[NUM_WINDOW_TEX];
    GLuint billboardTex[NUM_BILLBOARD_TEX];

    for (int i = 0; i < NUM_WINDOW_TEX; ++i)
        windowTex[i] = makeWindowTexture(1000u + i * 77u, 6, 8, WINDOW_STYLES[i]);
    for (int i = 0; i < NUM_BILLBOARD_TEX; ++i)
        billboardTex[i] = makeBillboardTexture(5000u + i * 131u);

    GLuint asphaltTex = makeAsphaltTexture(31337u);
    GLuint tileTex    = makeTileTexture(4242u);    // pavements
    GLuint stoneTex   = makeStoneTexture(777u);    // sea wall, tunnels, far islands

    // Real pictures for the big rooftop screens: every PNG/JPG in
    // assets/billboards. Drop in a new file and it appears - no code change.
    std::vector<ImageTexture> billboardImages =
        loadBillboardImages(exeFolder() + "\\assets\\billboards\\");
    g_whiteTex = makeWhiteTexture();

    // ---- off-screen buffers ------------------------------------------------
    SceneFBO sceneFBO;
    BloomFBO bloomFBO;
    createSceneFBO(sceneFBO, g_width, g_height);
    createBloomFBO(bloomFBO, g_width / 2, g_height / 2);   // half res: 4x cheaper
    BloomFBO streakFBO;                                     // lens streaks
    createBloomFBO(streakFBO, g_width / 4, g_height / 2);
    FullscreenQuad fsQuad = createFullscreenQuad();

    // Off-screen 1920 x 1080 target for "hd" recordings.
    GLuint hdFbo = 0, hdTex = 0;
    if (g_shotHD) {
        glGenFramebuffers(1, &hdFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, hdFbo);
        glGenTextures(1, &hdTex);
        glBindTexture(GL_TEXTURE_2D, hdTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_width, g_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ---- the city ----------------------------------------------------------
    unsigned int seed = 2107015;      // my student ID
    City city;
    generateCity(city, seed, NUM_WINDOW_TEX, NUM_BILLBOARD_TEX);
    spawnVehicles(city, seed, 18);
    if (g_shotNoCars) city.vehicles.clear();   // recording only; normal runs keep traffic
    spawnRain(city, seed, RAIN_COUNT);

    auto printCity = [&]() {
        int styles[5] = { 0, 0, 0, 0, 0 };
        size_t parts = 0;
        for (const Building& b : city.buildings) { styles[b.style]++; parts += b.parts.size(); }
        std::cout << "\nCity from seed " << seed << ":\n";
        std::cout << "  buildings   : " << city.buildings.size()
                  << "  (slab " << styles[STYLE_SLAB] << ", tiered " << styles[STYLE_TIERED]
                  << ", podium " << styles[STYLE_PODIUM] << ", round " << styles[STYLE_ROUND]
                  << ", octagon " << styles[STYLE_OCTAGON] << ")\n";
        std::cout << "  building parts: " << parts << "\n";
        std::cout << "  neon strips : " << city.neons.size()         << "\n";
        std::cout << "  billboards  : " << city.billboards.size()    << "\n";
        std::cout << "  lamps       : " << city.lamps.size()         << "\n";
        std::cout << "  junctions   : " << city.trafficLights.size() << "\n";
        std::cout << "  vehicles    : " << city.vehicles.size()      << "\n";
        std::cout << "  raindrops   : " << city.rain.size()          << "\n";
    };
    printCity();

    std::cout << "\nCONTROLS\n"
                 "  WASD / mouse      fly\n"
                 "  SHIFT             faster      SPACE / CTRL  up / down\n"
                 "  1 2 3             flat / Gouraud / Phong\n"
                 "  P pause   F wireframe   B bloom   G reflections   R rain\n"
                 "  M music on/off    T next track\n"
                 "  C drive a car  (W/S speed, A/D steer, SPACE handbrake, C to get out)\n"
                 "  L light markers   N new city   F12 screenshot   F11 fullscreen\n"
                 "  E dusk/night   H ray-traced shadows   V camera tour (for recording)\n"
                 "  X detail effects (beams, puddles, relief, reflections, lens) on/off\n"
                 "  TAB free cursor   ESC pause menu (controls, resume, exit)\n\n";

    g_cam.updateVectors();

    // ---- background music --------------------------------------------------
    // Nightcall (track 0) starts straight away and loops. Not in --shot mode:
    // a screenshot run should not suddenly play music.
    MusicPlayer music;
    auto showNowPlaying = [&]() {
        std::string title = "Neon Genesis   |   " + musicStatus(music);
        glfwSetWindowTitle(window, title.c_str());
        std::cout << "\n" << musicStatus(music) << "\n";
    };
    if (!shotPath) {
        musicInit(music);
        musicPlay(music, 0);
        showNowPlaying();
    }

    // ---- runtime toggles ---------------------------------------------------
    int   shadingMode = shotPath ? shotShading : 2;    // 2 = Phong
    bool  animate     = true;
    bool  wireframe   = false;
    bool  bloomOn     = true;
    bool  reflectOn   = true;
    bool  rainOn      = true;
    bool  showLights  = false;
    bool  fxOn        = !g_shotNoFX;   // the detail effects (key X)
    float dynScale    = 1.0f;          // dynamic resolution, 0.70 .. 1.0

    bool  k1=false,k2=false,k3=false,kP=false,kF=false,kB=false,kG=false,kR=false,
          kL=false,kN=false,kTab=false,kF12=false,kM=false,kT=false,kC=false,
          kF11=false,kEsc=false,kE=false,kH=false,kV=false,kX=false;

    // ---- fullscreen ---------------------------------------------------------
    // glfwSetWindowMonitor switches a window between fullscreen (give it a
    // monitor) and windowed (give it NULL plus a position and size). The
    // window's old position and size are saved first so that leaving
    // fullscreen puts it back exactly where it was.
    //
    // It goes fullscreen on whichever monitor the window's centre is on, so
    // with two screens it stays on the screen you are looking at.
    //
    // Nothing else needs to know: the size change triggers
    // framebuffer_size_callback, and the off-screen buffers are rebuilt to
    // the new size at the start of the next frame.
    bool fullscreen = false;
    int  winX = 100, winY = 100, winW = g_width, winH = g_height;
    auto toggleFullscreen = [&]() {
        if (!fullscreen) {
            glfwGetWindowPos(window, &winX, &winY);
            glfwGetWindowSize(window, &winW, &winH);

            int cx = winX + winW / 2, cy = winY + winH / 2;
            int count = 0;
            GLFWmonitor** monitors = glfwGetMonitors(&count);
            GLFWmonitor* target = glfwGetPrimaryMonitor();
            for (int i = 0; i < count; ++i) {
                int mx, my;
                glfwGetMonitorPos(monitors[i], &mx, &my);
                const GLFWvidmode* vm = glfwGetVideoMode(monitors[i]);
                if (cx >= mx && cx < mx + vm->width && cy >= my && cy < my + vm->height)
                    target = monitors[i];
            }
            const GLFWvidmode* mode = glfwGetVideoMode(target);
            glfwSetWindowMonitor(window, target, 0, 0, mode->width, mode->height, mode->refreshRate);
            std::cout << "\nFullscreen " << mode->width << " x " << mode->height << "  (F11 or ESC to leave)\n";
        } else {
            glfwSetWindowMonitor(window, NULL, winX, winY, winW, winH, 0);
            std::cout << "\nWindowed\n";
        }
        fullscreen = !fullscreen;
        // Some drivers forget the vsync setting when the display mode
        // changes, so set it again.
        glfwSwapInterval(shotPath ? 0 : 1);
    };

    // ---- time of day ---------------------------------------------------------
    // ONE number: dusk = 1 at sunset, 0 at full night. Everything that
    // changes with the time of day - the sun's height and colour, the moon,
    // the sky, the fog, the ambient light, the street lamps - is worked out
    // from it each frame. Pressing E sets the TARGET; dusk then glides toward
    // it over about nine seconds, so the change is a real sunset, not a cut.
    float dusk       = g_shotDusk ? 1.0f : 0.0f;
    float duskTarget = dusk;
    bool  rtOn       = !g_shotNoRT;            // ray-traced shadows

    // ---- camera tour ------------------------------------------------------------
    bool  touring  = false;
    float tourTime = 0.0f;

    // How far each street lamp is switched on (0..1), refilled every frame
    // from the time of day. Declared out here so the drawing code can see it.
    std::vector<float> lampOn(city.lamps.size(), 1.0f);

    // ---- ray-tracing data: the buildings as boxes, sorted by block ----------------
    RayTraceScene rtScene;
    buildRayTraceScene(rtScene, city);
    std::cout << "Ray tracing: " << rtScene.boxCount << " building boxes in a 4 x 4 grid\n";

    float lastFrame   = 0.0f;
    float sceneTime   = 0.0f;
    int   frameNumber = 0;
    int   shotCount   = 0;

    // FPS counter, printed once a second so the console is not flooded.
    // It uses the REAL clock, not deltaTime - in --shot mode deltaTime is a
    // fixed 1/60, which would make the counter always read ~60.
    float fpsStart = (float)glfwGetTime();
    int   fpsCount = 0;

    // Per-car placement for this frame: computed once, then used for the
    // headlight positions AND for drawing, in both the reflection and the
    // real pass, so all three always agree.
    std::vector<glm::mat4> carFrame;

    // The car you can drive. It does not exist until you press C.
    PlayerCar player;
    float     titleTimer = 0.0f;
    auto updateTitle = [&]() {
        std::string title = "Neon Genesis";
        if (player.driving) {
            // 1 unit is about 1.25 m, so units/s x 1.25 x 3.6 = km/h
            int kmh = (int)roundf(fabsf(player.speed) * 4.5f);
            title += "   |   DRIVING  " + std::to_string(kmh) + " km/h";
            if (player.speed < -0.1f) title += " (reverse)";
        }
        if (touring) {
            int secs = (int)tourTime;
            char buf[32];
            snprintf(buf, sizeof(buf), "   |   TOUR %d:%02d", secs / 60, secs % 60);
            title += buf;
        }
        title += (duskTarget > 0.5f) ? "   |   DUSK" : "   |   NIGHT";
        title += rtOn ? "   |   RT shadows ON" : "   |   RT shadows OFF";
        title += "   |   " + musicStatus(music);
        glfwSetWindowTitle(window, title.c_str());
    };

    // =======================================================================
    //  THE BIG ROOFTOP SCREENS - what each is showing right now
    //
    //  With several pictures loaded the screens take turns through them,
    //  every 8 seconds; imageOffset keeps the two screens on different ones.
    //  The screen's height comes from the picture's own proportions, so a
    //  wide picture is never squashed. With no pictures at all, the screens
    //  fall back to one of the animated pattern strips.
    // =======================================================================
    const float BIG_LEG_H = 2.2f;          // gap between the roof and the screen
    struct ScreenContent { GLuint tex; float aspect; glm::vec3 glow; glm::vec2 uvScale; };
    auto bigScreen = [&](const BigBillboard& bb) {
        if (!billboardImages.empty()) {
            int idx = ((int)floorf(sceneTime / 8.0f) + bb.imageOffset) % (int)billboardImages.size();
            const ImageTexture& img = billboardImages[idx];
            return ScreenContent{ img.id, img.aspect(), img.average, glm::vec2(1.0f) };
        }
        return ScreenContent{ billboardTex[bb.imageOffset % NUM_BILLBOARD_TEX], 16.0f / 9.0f,
                              glm::vec3(0.6f, 0.3f, 0.8f), glm::vec2(1.0f, 1.0f / BILLBOARD_FRAMES) };
    };

    // =======================================================================
    //  DRAW THE CITY
    //
    //  Written once as a lambda and called TWICE per frame - once upside-down
    //  for the reflection, once the right way up. It draws buildings, neon,
    //  billboards, lamps, traffic lights, cars and the flying vehicle.
    //  The road, pavements and rain are drawn separately, only once.
    // =======================================================================
    auto drawCity = [&]() {

        // ---- buildings ------------------------------------------------------
        // Each building is a list of parts. Every part is one primitive mesh,
        // moved to its base and scaled to its size - the same translate-then-
        // scale pattern as always. Only the mesh and the texture tiling differ.
        for (const Building& b : city.buildings) {
            for (const BuildingPart& p : b.parts) {
                glm::mat4 m(1.0f);
                m = glm::translate(m, p.base);   // bottom-centre of the part
                m = glm::scale(m, p.size);       // unit shape -> real size

                const Mesh* mesh = &cube;
                if (p.shape == PART_CYLINDER) mesh = &cylinder;
                if (p.shape == PART_OCTAGON)  mesh = &octagon;

                if (!p.windows) {
                    drawLit(*mesh, m, p.color, 0.3f, 32.0f);
                    continue;
                }

                // How many window cells fit on this part. The texture holds
                // 6 columns x 8 rows, so one copy covers 6*WINDOW_W wide and
                // 8*WINDOW_H tall - divide the wall size by that.
                const float tileW = 6.0f * WINDOW_W;
                const float tileH = 8.0f * WINDOW_H;
                glm::vec3 uv;
                int mode;
                if (p.shape == PART_BOX) {
                    uv   = glm::vec3(p.size.x / tileW, p.size.y / tileH, p.size.z / tileW);
                    mode = 0;                     // pick width or depth per face
                } else {
                    // Wrapped round the outline: the distance round is the
                    // circumference (pi * diameter) or the octagon perimeter.
                    float round = (p.shape == PART_CYLINDER) ? 3.14159f * p.size.x
                                                             : OCT_PERIMETER * p.size.x;
                    uv   = glm::vec3(round / tileW, p.size.y / tileH, 1.0f);
                    mode = 1;
                }
                drawFacade(*mesh, m, p.color, windowTex[b.windowTex], uv, mode,
                           b.position.x * 0.37f + b.position.z * 1.71f);
            }

            if (b.hasSpire) {
                glm::mat4 s(1.0f);
                s = glm::translate(s, b.roofCentre);
                s = glm::scale(s, glm::vec3(b.roofW * 0.45f, 8.0f, b.roofD * 0.45f));
                drawLit(pyramid, s, b.color * 1.5f, 0.5f, 48.0f);
            }

            if (b.hasAntenna) {
                glm::mat4 a(1.0f);
                a = glm::translate(a, b.roofCentre);
                a = glm::scale(a, glm::vec3(0.28f, 9.0f, 0.28f));
                drawLit(cylinder, a, glm::vec3(0.14f, 0.14f, 0.18f));

                // blinking red aircraft-warning light on the mast
                float blink = (sinf(sceneTime * 2.6f + b.position.x) > 0.0f) ? 1.0f : 0.1f;
                glm::mat4 w(1.0f);
                w = glm::translate(w, b.roofCentre + glm::vec3(0.0f, 9.0f, 0.0f));
                w = glm::scale(w, glm::vec3(0.5f));
                drawEmissive(cube, w, glm::vec3(1.0f, 0.12f, 0.10f), blink * 2.5f);
            }
        }

        // ---- neon -----------------------------------------------------------
        for (const NeonStrip& n : city.neons) {
            // Failing signs flicker on a noise-like signal: two sine waves of
            // unrelated frequency multiplied, so the pattern never visibly
            // repeats. Below a threshold the tube drops almost dark.
            float intensity = 2.4f;
            if (n.flickers) {
                float f = sinf(sceneTime * 11.0f + n.flickerSeed)
                        * sinf(sceneTime * 4.3f + n.flickerSeed * 2.0f);
                intensity = (f > -0.25f) ? 2.4f : 0.15f;
            }

            if (n.kind == NEON_VERTICAL) {
                glm::mat4 m(1.0f);
                m = glm::translate(m, n.position);
                m = glm::rotate(m, glm::radians(n.yaw), glm::vec3(0, 1, 0));
                m = glm::scale(m, glm::vec3(0.28f, n.height, 1.0f));
                drawEmissive(panel, m, n.color, intensity);
            }
            else if (n.kind == NEON_BAND) {
                // Four thin panels, one per face of THIS shaft. Rotating by
                // 90 degrees swaps which dimension runs along the face, so
                // odd faces use the depth and are pushed out by half the width.
                for (int f = 0; f < 4; ++f) {
                    bool odd    = (f % 2) == 1;
                    float along = odd ? n.bandSize.y : n.bandSize.x;
                    float out   = odd ? n.bandSize.x : n.bandSize.y;

                    glm::mat4 m(1.0f);
                    m = glm::translate(m, n.position);
                    m = glm::rotate(m, glm::radians(f * 90.0f), glm::vec3(0, 1, 0));
                    m = glm::translate(m, glm::vec3(0.0f, 0.0f, out * 0.5f + 0.06f));
                    m = glm::scale(m, glm::vec3(along + 0.12f, 0.24f, 1.0f));
                    drawEmissive(panel, m, n.color, intensity * 0.85f);
                }
            }
            else {
                // A glowing ring: a short OPEN tube, a touch wider than the
                // round shaft it wraps. Open, because a capped cylinder would
                // glow across its whole top and look like a floating disc.
                glm::mat4 m(1.0f);
                m = glm::translate(m, n.position);
                m = glm::scale(m, glm::vec3(n.bandSize.x, 0.26f, n.bandSize.y));
                drawEmissive(tube, m, n.color, intensity * 0.85f);
            }
        }

        // ---- billboards ------------------------------------------------------
        // Each billboard texture is a strip of BILLBOARD_FRAMES pictures
        // stacked vertically (see makeBillboardTexture). Showing one of them:
        //   v scale  = 1 / frames          -> the panel covers one frame's height
        //   v offset = frame / frames      -> which frame, from the clock
        //   u offset = time * speed        -> slides the picture sideways
        // Every billboard has its own seed, so they do not all change at once.
        glUniform1f(U.holo, fxOn ? 1.0f : 0.0f);    // holographic look (shader)
        for (const Billboard& b : city.billboards) {
            glm::mat4 m(1.0f);
            m = glm::translate(m, b.position);
            m = glm::rotate(m, glm::radians(b.yaw), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(b.width, b.height, 1.0f));

            const float FRAME_SECONDS = 3.0f;
            int   frame  = (int)floorf(sceneTime / FRAME_SECONDS + b.scrollSeed) % BILLBOARD_FRAMES;
            float scroll = sceneTime * 0.12f + b.scrollSeed;

            // a brief flash as the frame changes, like a screen refreshing
            float intoFrame = fmodf(sceneTime / FRAME_SECONDS + b.scrollSeed, 1.0f);
            float flash = (intoFrame < 0.04f) ? 1.6f : 1.0f;

            drawEmissive(panel, m, glm::vec3(1.0f), 1.7f * flash, billboardTex[b.texIndex],
                         glm::vec2(1.0f, 1.0f / BILLBOARD_FRAMES),
                         glm::vec2(scroll, (float)frame / BILLBOARD_FRAMES));
        }
        glUniform1f(U.holo, 0.0f);

        // ---- the big rooftop screens ------------------------------------------
        // Built like a real rooftop sign, all relative to T (the roof point,
        // turned to face the avenue):
        //   two steel LEGS standing on the roof, a dark FRAME box, the SCREEN
        //   panel on the frame's front, and a thin glowing TRIM along the top.
        // The panel faces +Z before T turns it, and sits just in front of the
        // frame (z = 0 against the frame's front at z = -0.08), so the frame
        // never covers the picture.
        for (const BigBillboard& bb : city.bigBoards) {
            ScreenContent sc = bigScreen(bb);
            float w = bb.width;
            float h = w / sc.aspect;

            glm::mat4 T(1.0f);
            T = glm::translate(T, bb.roof);
            T = glm::rotate(T, glm::radians(bb.yaw), glm::vec3(0, 1, 0));

            for (int side = -1; side <= 1; side += 2) {
                glm::mat4 leg = T;
                leg = glm::translate(leg, glm::vec3(side * bb.legSpread, 0.0f, -0.32f));
                leg = glm::scale(leg, glm::vec3(0.26f, BIG_LEG_H + h * 0.5f, 0.26f));
                drawLit(cylinder, leg, glm::vec3(0.08f, 0.08f, 0.10f), 0.6f, 48.0f);
            }

            glm::mat4 frame = T;
            frame = glm::translate(frame, glm::vec3(0.0f, BIG_LEG_H - 0.3f, -0.26f));
            frame = glm::scale(frame, glm::vec3(w + 0.6f, h + 0.6f, 0.36f));
            drawLit(cube, frame, glm::vec3(0.03f, 0.03f, 0.04f), 0.4f, 32.0f);

            // A slow brightness breathing, like a real LED wall. Bloom is kept
            // to half, or a whole bright picture would blur into a haze.
            float breathe = 1.25f + 0.08f * sinf(sceneTime * 1.3f + bb.imageOffset * 2.0f);

            // TWO SCREENS, back to back. The panel mesh only shows its front
            // (+Z), so the back screen is the same panel turned 180 degrees
            // about Y and moved behind the frame (the frame's back face is at
            // z = -0.44). Turning it - rather than flipping it with a negative
            // scale - keeps the picture reading left-to-right from behind, not
            // mirrored. The trim along the top is doubled the same way.
            for (int side = 0; side < 2; ++side) {
                glm::mat4 S = T;
                if (side == 1) {
                    S = glm::translate(S, glm::vec3(0.0f, 0.0f, -0.53f));
                    S = glm::rotate(S, glm::radians(180.0f), glm::vec3(0, 1, 0));
                }

                glm::mat4 screen = S;
                screen = glm::translate(screen, glm::vec3(0.0f, BIG_LEG_H, 0.0f));
                screen = glm::scale(screen, glm::vec3(w, h, 1.0f));
                glUniform1f(U.holo, fxOn ? 0.5f : 0.0f);     // a gentler version
                drawEmissive(panel, screen, glm::vec3(1.0f), breathe, sc.tex,
                             sc.uvScale, glm::vec2(0.0f), 0.45f);
                glUniform1f(U.holo, 0.0f);

                glm::mat4 trim = S;
                trim = glm::translate(trim, glm::vec3(0.0f, BIG_LEG_H + h + 0.12f, 0.01f));
                trim = glm::scale(trim, glm::vec3(w + 0.6f, 0.12f, 1.0f));
                drawEmissive(panel, trim, glm::vec3(0.10f, 1.00f, 1.00f), 2.4f);
            }
        }

        // ---- street lamps ----------------------------------------------------
        // A composite object: cylinder post + cube arm + glowing cube head.
        for (const StreetLamp& L : city.lamps) {
            const float POST_H  = 7.5f;
            const float ARM_LEN = 2.4f;

            float rad = glm::radians(L.facing);
            glm::vec3 outward(cosf(rad), 0.0f, sinf(rad));

            {
                glm::mat4 m(1.0f);
                m = glm::translate(m, L.position);
                m = glm::scale(m, glm::vec3(0.26f, POST_H, 0.26f));
                drawLit(cylinder, m, glm::vec3(0.09f, 0.09f, 0.12f), 0.6f, 48.0f);
            }
            {
                // The unit cube is centred on X, so the arm's centre must sit
                // HALF a length out for it to start at the post.
                glm::mat4 m(1.0f);
                m = glm::translate(m, L.position + glm::vec3(0.0f, POST_H - 0.2f, 0.0f)
                                    + outward * (ARM_LEN * 0.5f));
                m = glm::rotate(m, -rad, glm::vec3(0, 1, 0));
                m = glm::scale(m, glm::vec3(ARM_LEN, 0.18f, 0.18f));
                drawLit(cube, m, glm::vec3(0.09f, 0.09f, 0.12f), 0.6f, 48.0f);
            }
            {
                // The head hangs at the FULL arm length.
                glm::mat4 m(1.0f);
                m = glm::translate(m, L.position + glm::vec3(0.0f, POST_H - 0.42f, 0.0f)
                                    + outward * ARM_LEN);
                m = glm::rotate(m, -rad, glm::vec3(0, 1, 0));
                m = glm::scale(m, glm::vec3(0.9f, 0.24f, 0.46f));
                // glass glows only once the lamp is on; a faint glint when off
                float on = lampOn[&L - &city.lamps[0]];
                glUniform1f(U.streak, (fxOn && on > 0.5f) ? 1.0f : 0.0f);   // a lens streak
                drawEmissive(cube, m, glm::vec3(1.0f, 0.82f, 0.55f), 0.08f + 2.5f * on);
                glUniform1f(U.streak, 0.0f);
            }
        }

        // ---- traffic lights --------------------------------------------------
        // Each junction lets the X-road and the Z-road go in turn, so it needs
        // TWO sets of lamps: one on the face toward the Z-road (+Z) showing
        // the Z-road's signal, one on the face toward the X-road (+X) showing
        // the X-road's. Both come from signalFor() - the same function the
        // cars obey - so what you see is exactly what the cars react to.
        for (const TrafficLight& t : city.trafficLights) {
            {
                glm::mat4 m(1.0f);
                m = glm::translate(m, t.position);
                m = glm::scale(m, glm::vec3(0.2f, 5.2f, 0.2f));
                drawLit(cylinder, m, glm::vec3(0.08f, 0.08f, 0.10f), 0.6f, 48.0f);
            }
            {
                glm::mat4 m(1.0f);
                m = glm::translate(m, t.position + glm::vec3(0.0f, 5.2f, 0.0f));
                m = glm::scale(m, glm::vec3(0.6f, 1.85f, 0.6f));
                drawLit(cube, m, glm::vec3(0.04f, 0.04f, 0.05f));
            }

            // k = 0 red (top), 1 amber, 2 green (bottom). The lamp whose index
            // matches the signal is the one lit.
            const glm::vec3 lampCol[3] = {
                glm::vec3(1.00f, 0.10f, 0.10f),
                glm::vec3(1.00f, 0.72f, 0.10f),
                glm::vec3(0.12f, 1.00f, 0.35f),
            };
            for (int face = 0; face < 2; ++face) {
                int   axis  = (face == 0) ? 1 : 0;          // +Z face = Z-road
                int   state = signalFor(sceneTime, t.phaseOffset, axis);
                float turn  = (face == 0) ? 0.0f : 90.0f;   // 90 turns +Z to +X
                for (int k = 0; k < 3; ++k) {
                    bool on = (k == state);
                    glm::mat4 m(1.0f);
                    m = glm::translate(m, t.position + glm::vec3(0.0f, 6.52f - k * 0.52f, 0.0f));
                    m = glm::rotate(m, glm::radians(turn), glm::vec3(0, 1, 0));
                    m = glm::translate(m, glm::vec3(0.0f, 0.0f, 0.31f));
                    m = glm::scale(m, glm::vec3(0.28f, 0.28f, 0.1f));
                    drawEmissive(cube, m, lampCol[k], on ? 2.8f : 0.08f);
                }
            }
        }

        // ---- cars --------------------------------------------------------------
        // Every piece of a car is placed relative to the car itself: first
        // T moves and turns to the car's spot, then each piece adds its own
        // local offset. This is hierarchical modelling - move the parent and
        // every child follows.
        //
        // Written once as a function and used for every AI car AND the car you
        // drive: only T (where and which way) and the colours differ.
        const float CAR_L = CAR_LENGTH, CAR_H = 1.4f, CAR_W = CAR_WIDTH;
        const float WHEEL_R = 0.31f, WHEEL_W = 0.28f;

        auto drawCar = [&](const glm::mat4& T, const glm::vec3& color, const glm::vec3& rimColor) {

            // body and glass cabin - the two extruded silhouettes. Glossy
            // paint and glass reflect the (faked) surroundings - envReflect.
            glUniform1f(U.envReflect, fxOn ? 0.55f : 0.0f);
            drawLit(carBody,  T * glm::scale(glm::mat4(1.0f), glm::vec3(CAR_L, CAR_H, CAR_W)),
                    color, 0.9f, 96.0f);
            glUniform1f(U.envReflect, fxOn ? 0.9f : 0.0f);
            drawLit(carCabin, T * glm::scale(glm::mat4(1.0f), glm::vec3(CAR_L, CAR_H, CAR_W * 0.86f)),
                    glm::vec3(0.02f, 0.025f, 0.04f), 1.2f, 140.0f);
            glUniform1f(U.envReflect, 0.0f);

            // ---- wheels ----
            // The cylinder mesh stands upright with its base at y = 0 and top
            // at y = 1 - it is NOT centred on its own axis. The last transform
            // below (applied FIRST) slides it down by 0.5 so it is centred;
            // then rotating 90 degrees about X lays the axle across the car.
            // Because it is now centred, the left and right wheels are exact
            // mirror images - no per-side special cases.
            for (int fb = -1; fb <= 1; fb += 2) {           // back / front axle
                for (int lr = -1; lr <= 1; lr += 2) {       // left / right side
                    glm::vec3 hub(1.15f * fb, WHEEL_R, (CAR_W * 0.5f - WHEEL_W * 0.5f + 0.03f) * lr);

                    glm::mat4 w = T;
                    w = glm::translate(w, hub);
                    w = glm::rotate(w, glm::radians(90.0f), glm::vec3(1, 0, 0));
                    w = glm::scale(w, glm::vec3(WHEEL_R * 2.0f, WHEEL_W, WHEEL_R * 2.0f));
                    w = glm::translate(w, glm::vec3(0.0f, -0.5f, 0.0f));
                    drawLit(cylinder, w, glm::vec3(0.025f, 0.025f, 0.03f), 0.3f, 24.0f);

                    // A glowing rim: a smaller disc, slightly thicker than the
                    // tyre so its face shows on the outside.
                    glm::mat4 r = T;
                    r = glm::translate(r, hub);
                    r = glm::rotate(r, glm::radians(90.0f), glm::vec3(1, 0, 0));
                    r = glm::scale(r, glm::vec3(WHEEL_R * 1.2f, WHEEL_W + 0.03f, WHEEL_R * 1.2f));
                    r = glm::translate(r, glm::vec3(0.0f, -0.5f, 0.0f));
                    drawEmissive(cylinder, r, rimColor, 1.4f);
                }
            }

            // head lights at the front (+X), tail lights at the back (-X)
            for (int lr = -1; lr <= 1; lr += 2) {
                glm::mat4 h = T;
                h = glm::translate(h, glm::vec3(CAR_L * 0.5f + 0.01f, 0.40f, 0.55f * lr));
                h = glm::scale(h, glm::vec3(0.06f, 0.13f, 0.36f));
                glUniform1f(U.streak, fxOn ? 1.0f : 0.0f);          // headlights streak
                drawEmissive(cube, h, glm::vec3(0.9f, 0.95f, 1.0f), 3.0f);
                glUniform1f(U.streak, 0.0f);

                glm::mat4 t = T;
                t = glm::translate(t, glm::vec3(-CAR_L * 0.5f - 0.01f, 0.44f, 0.56f * lr));
                t = glm::scale(t, glm::vec3(0.06f, 0.11f, 0.40f));
                drawEmissive(cube, t, glm::vec3(1.0f, 0.08f, 0.06f), 2.2f);
            }
        };

        for (size_t i = 0; i < city.vehicles.size(); ++i)
            drawCar(carFrame[i], city.vehicles[i].color, city.vehicles[i].rimColor);

        // The car you drive: bright white paint and cyan rims, so it is easy
        // to spot among the traffic.
        if (player.exists) {
            glm::mat4 T(1.0f);
            T = glm::translate(T, player.pos);
            T = glm::rotate(T, player.heading, glm::vec3(0, 1, 0));
            drawCar(T, glm::vec3(0.80f, 0.82f, 0.90f), glm::vec3(0.10f, 1.00f, 1.00f));
        }

        // ==================================================================
        //  THE ISLAND'S EDGE AND WHAT LIES BEYOND IT
        // ==================================================================
        const glm::vec3 STONE(0.045f, 0.045f, 0.055f);

        // Stone-textured box. The texture repeats every 8 x 6.4 units, so its
        // 4 x 8 grid of blocks comes out about 2 units long and 0.8 tall -
        // plausible masonry - whatever size the box is.
        auto drawStone = [&](const glm::mat4& model, const glm::vec3& size) {
            drawSurface(cube, model, glm::vec3(0.075f, 0.075f, 0.090f), stoneTex,
                        glm::vec3(size.x / 8.0f, size.y / 6.4f, size.z / 8.0f), 0,
                        0.0f, 0.3f, 24.0f, 0.0f, 1.0f);
        };

        // ---- sea wall ----------------------------------------------------
        // Four long stone boxes, one per side, from below the waterline up to
        // a parapet 0.3 above the street. That raised lip is the clear border
        // round the city. A purple neon strip runs along each wall's outer
        // face and warm bollard lights stand every 12 units along the top.
        {
            const float WALL_T      = 1.2f;              // thickness
            const float WALL_BOTTOM = WATER_Y - 1.0f;
            const float WALL_TOP    = 0.3f;
            for (int side = 0; side < 4; ++side) {
                bool  alongX = side < 2;                 // sides 0,1 run along X
                float sgn    = (side % 2 == 0) ? 1.0f : -1.0f;
                float inner  = sgn * (ISLAND_EDGE - WALL_T * 0.5f);

                glm::vec3 c  = alongX ? glm::vec3(0.0f, WALL_BOTTOM, inner)
                                      : glm::vec3(inner, WALL_BOTTOM, 0.0f);
                glm::vec3 sz = alongX ? glm::vec3(ISLAND_EDGE * 2.0f, WALL_TOP - WALL_BOTTOM, WALL_T)
                                      : glm::vec3(WALL_T, WALL_TOP - WALL_BOTTOM, ISLAND_EDGE * 2.0f);
                drawStone(glm::scale(glm::translate(glm::mat4(1.0f), c), sz), sz);

                float face = sgn * (ISLAND_EDGE + 0.02f);
                glm::vec3 nc = alongX ? glm::vec3(0.0f, WALL_TOP - 0.2f, face)
                                      : glm::vec3(face, WALL_TOP - 0.2f, 0.0f);
                glm::vec3 ns = alongX ? glm::vec3(ISLAND_EDGE * 2.0f, 0.09f, 0.04f)
                                      : glm::vec3(0.04f, 0.09f, ISLAND_EDGE * 2.0f);
                drawEmissive(cube, glm::scale(glm::translate(glm::mat4(1.0f), nc), ns),
                             glm::vec3(0.75f, 0.25f, 1.00f), 2.2f);

                for (float t = -ISLAND_EDGE + 6.0f; t < ISLAND_EDGE; t += 12.0f) {
                    float on = sgn * (ISLAND_EDGE - 0.6f);
                    glm::vec3 bp = alongX ? glm::vec3(t, WALL_TOP, on) : glm::vec3(on, WALL_TOP, t);
                    drawEmissive(cube, glm::scale(glm::translate(glm::mat4(1.0f), bp),
                                                  glm::vec3(0.28f, 0.45f, 0.28f)),
                                 glm::vec3(1.00f, 0.75f, 0.40f), 2.2f);
                }
            }
        }

        // ---- tunnel mouths ------------------------------------------------
        // Only the two CENTRAL AVENUES run on under the water, so there are
        // just four tunnels - one at each end of each avenue. (Every other
        // road ends at the ring road, where its cars turn - see traffic.h.)
        // Each is built in "road space" - local +X pointing OUT along the
        // road, away from the city - and T turns that into the world, so one
        // piece of code builds all four: two side walls, a roof, a pitch-black
        // back wall (an emissive surface of colour zero is simply black - the
        // dark depth of the tunnel) and a glowing sign over the entrance.
        {
            const float DEPTH  = TUNNEL_END - TUNNEL_START;      // 6.5
            const float HALF_W = ROAD * 0.5f + 0.3f;             // opening half-width
            const float TUN_H  = 4.2f;
            {
                const int k = TUNNEL_ROAD;
                float roadCentre = -HALF + k * CELL;
                for (int axis = 0; axis < 2; ++axis) {
                    for (int end = -1; end <= 1; end += 2) {
                        float mid = end * (TUNNEL_START + TUNNEL_END) * 0.5f;
                        glm::vec3 centre  = (axis == 0) ? glm::vec3(mid, 0.0f, roadCentre)
                                                        : glm::vec3(roadCentre, 0.0f, mid);
                        glm::vec3 outward = (axis == 0) ? glm::vec3((float)end, 0.0f, 0.0f)
                                                        : glm::vec3(0.0f, 0.0f, (float)end);
                        glm::mat4 T(1.0f);
                        T = glm::translate(T, centre);
                        T = glm::rotate(T, yawFromDirection(outward), glm::vec3(0, 1, 0));

                        for (int sd = -1; sd <= 1; sd += 2)
                            drawStone(glm::scale(glm::translate(T, glm::vec3(0.0f, 0.0f, sd * (HALF_W + 0.35f))),
                                                 glm::vec3(DEPTH, TUN_H, 0.7f)), glm::vec3(DEPTH, TUN_H, 0.7f));
                        drawStone(glm::scale(glm::translate(T, glm::vec3(0.0f, TUN_H, 0.0f)),
                                             glm::vec3(DEPTH, 0.8f, HALF_W * 2.0f + 1.4f)),
                                  glm::vec3(DEPTH, 0.8f, HALF_W * 2.0f + 1.4f));
                        drawEmissive(cube, glm::scale(glm::translate(T, glm::vec3(DEPTH * 0.5f - 0.2f, 0.0f, 0.0f)),
                                                      glm::vec3(0.4f, TUN_H, HALF_W * 2.0f)), glm::vec3(0.0f), 0.0f);
                        drawEmissive(cube, glm::scale(glm::translate(T, glm::vec3(-DEPTH * 0.5f - 0.05f, TUN_H + 0.25f, 0.0f)),
                                                      glm::vec3(0.1f, 0.25f, HALF_W * 2.0f + 1.4f)),
                                     glm::vec3(0.10f, 1.00f, 1.00f), 2.4f);
                    }
                }
            }
        }

        // ---- the districts across the water ------------------------------------
        // Everything in a district is placed in its LOCAL frame and moved into
        // the world by one matrix per district:
        //     D = translate(centre) * rotate(yaw)
        // - the same parent-child idea as a car and its wheels. The land, its
        // edge lights, its street lights and its towers all follow D.
        {
            std::vector<glm::mat4> D(city.farDistricts.size());
            for (size_t i = 0; i < city.farDistricts.size(); ++i) {
                const FarDistrict& fd = city.farDistricts[i];
                D[i] = glm::rotate(glm::translate(glm::mat4(1.0f), fd.centre),
                                   glm::radians(fd.yaw), glm::vec3(0, 1, 0));

                // the land: from below the water up to 0.3 above street level
                const float bottom = WATER_Y - 1.0f, top = 0.3f;
                glm::vec3 landSize(fd.halfW * 2.0f, top - bottom, fd.halfD * 2.0f);
                drawStone(glm::scale(glm::translate(D[i], glm::vec3(0.0f, bottom, 0.0f)), landSize), landSize);

                // a glowing strip along its waterline, like the main island's
                for (int e = 0; e < 4; ++e) {
                    bool alongX = e < 2;
                    float sgn   = (e % 2 == 0) ? 1.0f : -1.0f;
                    glm::vec3 c = alongX ? glm::vec3(0.0f, top - 0.2f, sgn * (fd.halfD + 0.02f))
                                         : glm::vec3(sgn * (fd.halfW + 0.02f), top - 0.2f, 0.0f);
                    glm::vec3 s = alongX ? glm::vec3(fd.halfW * 2.0f, 0.09f, 0.04f)
                                         : glm::vec3(0.04f, 0.09f, fd.halfD * 2.0f);
                    drawEmissive(cube, glm::scale(glm::translate(D[i], c), s),
                                 glm::vec3(0.75f, 0.25f, 1.00f), 2.2f);
                }

                // amber street lights running between the lots
                const glm::vec3 AMBER(1.00f, 0.62f, 0.20f);
                for (float x : { -15.0f, 0.0f, 15.0f })
                    drawEmissive(cube, glm::scale(glm::translate(D[i], glm::vec3(x, top + 0.02f, 0.0f)),
                                                  glm::vec3(0.18f, 0.05f, fd.halfD * 2.0f - 4.0f)), AMBER, 1.6f);
                for (float z : { -7.0f, 7.0f })
                    drawEmissive(cube, glm::scale(glm::translate(D[i], glm::vec3(0.0f, top + 0.02f, z)),
                                                  glm::vec3(fd.halfW * 2.0f - 4.0f, 0.05f, 0.18f)), AMBER, 1.6f);
            }

            // the towers, standing on their district's land
            const float tileW = 6.0f * WINDOW_W, tileH = 8.0f * WINDOW_H;
            for (const FarTower& ft : city.farTowers) {
                glm::mat4 m = glm::scale(glm::translate(D[ft.district], ft.local), ft.size);
                drawFacade(cube, m, ft.color, windowTex[ft.windowTex],
                           glm::vec3(ft.size.x / tileW, ft.size.y / tileH, ft.size.z / tileW), 0,
                           ft.local.x * 0.13f + ft.local.z * 0.71f + ft.district * 9.1f);
                if (ft.hasNeon) {
                    glm::vec3 band = ft.local + glm::vec3(0.0f, ft.size.y - 4.0f, 0.0f);
                    drawEmissive(cube, glm::scale(glm::translate(D[ft.district], band),
                                                  glm::vec3(ft.size.x + 0.3f, 0.45f, ft.size.z + 0.3f)),
                                 ft.neonColor, 2.4f);
                }
            }
        }

        // ---- bridges ---------------------------------------------------------
        // Two elevated highways crossing the water past the island, at
        // different heights so the higher one clears the lower where they
        // cross. Each is a long deck box on cylinder pillars, with a neon
        // strip along each edge. The lights moving along them are cars:
        // each is just a position computed from the clock -
        //     t = (time * speed + its own offset), wrapped to the deck length
        // - the same single-clock rule as the rest of the city, and no
        // stored state at all.
        {
            struct BridgeDef { int axis; float offset; float height; };
            const BridgeDef bridges[2] = {
                { 0, -(ISLAND_EDGE + 30.0f),  8.0f },   // runs along X, south of the island
                { 1,   ISLAND_EDGE + 40.0f,  11.0f },   // runs along Z, east of the island
            };
            // Long enough to run 6 units onto the districts at each end, whose
            // land begins 159 from the centre (185 - 26).
            const float B_HALF_LEN = 165.0f, B_W = 7.0f, DECK_T = 0.7f;

            for (const BridgeDef& b : bridges) {
                // Build along X, then turn 90 degrees for the bridge along Z.
                glm::mat4 T(1.0f);
                T = (b.axis == 0) ? glm::translate(T, glm::vec3(0.0f, 0.0f, b.offset))
                                  : glm::translate(T, glm::vec3(b.offset, 0.0f, 0.0f));
                if (b.axis == 1) T = glm::rotate(T, glm::radians(90.0f), glm::vec3(0, 1, 0));

                drawLit(cube, glm::scale(glm::translate(T, glm::vec3(0.0f, b.height, 0.0f)),
                                         glm::vec3(B_HALF_LEN * 2.0f, DECK_T, B_W)), STONE, 0.5f, 40.0f);

                for (float t = -B_HALF_LEN + 12.5f; t < B_HALF_LEN; t += 25.0f)
                    drawLit(cylinder, glm::scale(glm::translate(T, glm::vec3(t, WATER_Y - 1.0f, 0.0f)),
                                                 glm::vec3(1.6f, b.height - WATER_Y + 1.0f, 1.6f)),
                            STONE, 0.4f, 32.0f);

                const glm::vec3 edgeCol[2] = { glm::vec3(1.00f, 0.15f, 0.65f), glm::vec3(0.10f, 1.00f, 1.00f) };
                for (int e = 0; e < 2; ++e) {
                    float side = (e == 0 ? 1.0f : -1.0f) * (B_W * 0.5f - 0.1f);
                    drawEmissive(cube, glm::scale(glm::translate(T, glm::vec3(0.0f, b.height + DECK_T, side)),
                                                  glm::vec3(B_HALF_LEN * 2.0f, 0.12f, 0.12f)),
                                 edgeCol[e], 2.2f);
                }

                // traffic: white headlights one way, red tail lights the other
                for (int dir = -1; dir <= 1; dir += 2) {
                    for (int i = 0; i < 9; ++i) {
                        float t = fmodf(sceneTime * 14.0f * dir + i * (B_HALF_LEN * 2.0f / 9.0f) + 1.0e4f,
                                        B_HALF_LEN * 2.0f) - B_HALF_LEN;
                        glm::vec3 col = (dir > 0) ? glm::vec3(0.9f, 0.95f, 1.0f) : glm::vec3(1.0f, 0.12f, 0.08f);
                        drawEmissive(cube, glm::scale(glm::translate(T, glm::vec3(t, b.height + DECK_T, dir * 1.6f)),
                                                      glm::vec3(1.3f, 0.35f, 0.9f)), col, 2.0f);
                    }
                }
            }
        }

        // ---- the flying vehicles -------------------------------------------------
        // The car body and cabin again, stretched longer and lower, with engine
        // glows at the back and a glowing strip underneath. Two of them, on
        // different routes, each with its own paint and glow colours.
        for (int k = 0; k < NUM_FLYERS; ++k) {
            glm::vec3 p = flyerPosition(sceneTime, k);
            // Direction of travel: where it will be a moment later, minus
            // where it is now (works for any path, not only circles).
            glm::vec3 travel = flyerPosition(sceneTime + 0.05f, k) - p;
            travel.y = 0.0f;
            travel = glm::normalize(travel);

            glm::mat4 T(1.0f);
            T = glm::translate(T, p);
            T = glm::rotate(T, yawFromDirection(travel), glm::vec3(0, 1, 0));

            glUniform1f(U.envReflect, fxOn ? 0.6f : 0.0f);
            drawLit(carBody,  T * glm::scale(glm::mat4(1.0f), glm::vec3(5.4f, 1.3f, 2.4f)),
                    FLYER_PAINT[k], 1.0f, 120.0f);
            drawLit(carCabin, T * glm::scale(glm::mat4(1.0f), glm::vec3(5.4f, 1.3f, 2.0f)),
                    glm::vec3(0.02f, 0.03f, 0.05f), 1.2f, 140.0f);
            glUniform1f(U.envReflect, 0.0f);

            for (int lr = -1; lr <= 1; lr += 2) {
                glm::mat4 e = T;
                e = glm::translate(e, glm::vec3(-2.75f, 0.35f, 0.75f * lr));
                e = glm::scale(e, glm::vec3(0.25f, 0.35f, 0.55f));
                drawEmissive(cube, e, FLYER_ENGINE[k], 3.2f);
            }
            glm::mat4 u = T;
            u = glm::translate(u, glm::vec3(0.0f, 0.12f, 0.0f));
            u = glm::scale(u, glm::vec3(4.2f, 0.06f, 1.9f));
            drawEmissive(cube, u, FLYER_GLOW[k], 2.4f);
        }
    };

    // ---- the pause menu (ESC) ---------------------------------------------
    // Opening it stops the scene clock (remembering whether it was already
    // paused with P) and frees the mouse cursor so the buttons can be
    // clicked; closing it restores both.
    int  menuHover = 0;
    bool kClick = false, animateBeforeMenu = true;
    auto openMenu = [&]() {
        g_menuOpen = true;
        animateBeforeMenu = animate;
        animate = false;
        g_mouseCaptured = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        kClick = true;                       // ignore a button already held
    };
    auto closeMenu = [&]() {
        g_menuOpen = false;
        animate = animateBeforeMenu;
        g_mouseCaptured = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        g_firstMouse = true;                 // no camera jump from the mouse moving meanwhile
    };

    // =======================================================================
    //  RENDER LOOP
    // =======================================================================
    while (!glfwWindowShouldClose(window)) {
        float now       = (float)glfwGetTime();
        float deltaTime = now - lastFrame;
        lastFrame       = now;
        // A huge delta (after dragging the window, say) would teleport every
        // car across the city, so clamp it.
        deltaTime = std::min(deltaTime, 0.05f);

        // In --shot mode use a fixed step, so frame 300 is always exactly
        // 5.0 seconds into the scene and a screenshot is repeatable.
        if (shotPath) deltaTime = 1.0f / 60.0f;

        if (animate) sceneTime += deltaTime;

        // ---- FPS ----------------------------------------------------------
        fpsCount++;
        if (now - fpsStart >= 1.0f) {
            // DYNAMIC RESOLUTION. Once a second: below 45 FPS draw the scene
            // a little smaller (down to 70% width and height, about half the
            // pixels); at 57 or more, step back up toward full size. The two
            // limits are apart on purpose, so it does not flip every second.
            // The picture is still stretched to the whole window, and the
            // bloom hides most of the softness. Off in --shot mode, so test
            // images are always full size and comparable.
            if (!shotPath) {
                if      (fpsCount < 45 && dynScale > 0.70f) dynScale = std::max(0.70f, dynScale - 0.07f);
                else if (fpsCount >= 57 && dynScale < 1.0f) dynScale = std::min(1.0f,  dynScale + 0.05f);
            }
            std::cout << "FPS: " << fpsCount
                      << "   scale: " << (int)roundf(dynScale * 100.0f) << "%"
                      << "   lights: " << g_lights.size()
                      << "   mode: " << (shadingMode == 0 ? "flat   " :
                                         shadingMode == 1 ? "Gouraud" : "Phong  ")
                      << "\r" << std::flush;
            fpsStart = now;
            fpsCount = 0;
        }

        // ---- input ---------------------------------------------------------
        // ESC leaves fullscreen first and only quits from a window, so a stray
        // press during a fullscreen demo does not close the program. It is
        // edge-detected (reacts once per press): otherwise holding it would
        // leave fullscreen AND quit in consecutive frames.
        {
            bool esc = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
            if (esc && !kEsc) {
                if (touring) {
                    touring = false;
                    g_cam.yaw   = glm::degrees(atan2f(g_cam.front.z, g_cam.front.x));
                    g_cam.pitch = glm::degrees(asinf(std::max(-1.0f, std::min(1.0f, g_cam.front.y))));
                    g_firstMouse = true;
                    updateTitle();
                }
                else if (g_menuOpen) closeMenu();
                else                 openMenu();
            }
            kEsc = esc;

            // While the menu is open: ENTER resumes, Q quits, and the two
            // buttons can be clicked. The mouse position is turned into a
            // fraction of the window, which is how the buttons are stored.
            if (g_menuOpen) {
                if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS) closeMenu();
                if (glfwGetKey(window, GLFW_KEY_Q)     == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
                double mx, my;
                glfwGetCursorPos(window, &mx, &my);
                double fx = mx / std::max(1, g_width), fy = my / std::max(1, g_height);
                menuHover = menuHit(MENU_RESUME, fx, fy) ? 1 : menuHit(MENU_EXIT, fx, fy) ? 2 : 0;
                bool click = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                if (click && !kClick) {
                    if (menuHover == 1) closeMenu();
                    if (menuHover == 2) glfwSetWindowShouldClose(window, true);
                }
                kClick = click;
            }
        }

        // The same keys do two jobs: while DRIVING, WASD work the car; the
        // rest of the time they fly the camera.
        g_scriptTime = frameNumber / 60.0f;      // scripted keys follow the fixed 1/60 step
        auto down = [&](int key) { return keyDown(window, key); };
        float throttle = 0.0f, steer = 0.0f;
        bool  handbrake = false;

        if (player.driving) {
            if (down(GLFW_KEY_W) || g_shotDrive) throttle += 1.0f;
            if (down(GLFW_KEY_S)) throttle -= 1.0f;
            if (down(GLFW_KEY_A)) steer += 1.0f;
            if (down(GLFW_KEY_D)) steer -= 1.0f;
            handbrake = down(GLFW_KEY_SPACE);
        } else {
            float velocity = g_cam.speed * deltaTime;
            if (down(GLFW_KEY_LEFT_SHIFT)) velocity *= 3.5f;

            // 'right' is perpendicular to both front and up; normalising keeps
            // strafing the same speed even when looking steeply up or down.
            glm::vec3 right = glm::normalize(glm::cross(g_cam.front, g_cam.up));

            if (down(GLFW_KEY_W)) g_cam.position += g_cam.front * velocity;
            if (down(GLFW_KEY_S)) g_cam.position -= g_cam.front * velocity;
            if (down(GLFW_KEY_A)) g_cam.position -= right * velocity;
            if (down(GLFW_KEY_D)) g_cam.position += right * velocity;
            if (down(GLFW_KEY_SPACE))        g_cam.position.y += velocity;
            if (down(GLFW_KEY_LEFT_CONTROL)) g_cam.position.y -= velocity;

            if (g_cam.position.y < 0.8f) g_cam.position.y = 0.8f;   // stay above the road
        }

        // Getting in and out of the car. Getting in puts the car on the road
        // nearest the camera, facing the way the camera looks.
        auto toggleDriving = [&]() {
            if (!player.driving) {
                placePlayerNear(player, city, g_cam.position, g_cam.front);
                player.driving = true;
                std::cout << "\nDRIVING - W/S speed, A/D steer, SPACE handbrake, C to get out\n";
            } else {
                player.driving = false;
                player.speed   = 0.0f;
                // Carry on flying from where the chase camera was, looking the
                // same way, so the switch is seamless.
                g_cam.yaw   = glm::degrees(atan2f(g_cam.front.z, g_cam.front.x));
                g_cam.pitch = glm::degrees(asinf(std::max(-1.0f, std::min(1.0f, g_cam.front.y))));
                g_firstMouse = true;
                std::cout << "\nOut of the car - flying again\n";
            }
            updateTitle();
        };
        if (g_shotDrive && frameNumber == 1 && !player.driving) toggleDriving();

        // Start / stop the camera tour. It begins at SUNSET with everything
        // on, so the recording shows the long ray-traced shadows first; the
        // sun sets partway through (see TOUR_NIGHTFALL).
        auto startOrStopTour = [&]() {
            touring = !touring;
            if (touring) {
                tourTime = 0.0f;
                if (player.driving) toggleDriving();
                dusk = duskTarget = 1.0f;
                rtOn = !g_shotNoRT;
                reflectOn = bloomOn = animate = true;
                rainOn = false;                     // no rain under a clear sunset
                shadingMode = 2;
                std::cout << "\nCAMERA TOUR - about 1:45. V or ESC to stop.\n";
            } else {
                // Hand the camera back pointing the way the tour left it.
                g_cam.yaw   = glm::degrees(atan2f(g_cam.front.z, g_cam.front.x));
                g_cam.pitch = glm::degrees(asinf(std::max(-1.0f, std::min(1.0f, g_cam.front.y))));
                g_firstMouse = true;
                std::cout << "\nTour ended.\n";
            }
            updateTitle();
        };
        if (g_shotTour && frameNumber == 1 && !touring) startOrStopTour();

        // Edge-detected toggles: fire only on the frame the key goes DOWN,
        // otherwise holding it flips the state every frame.
        #define EDGE(KEY, VAR, BODY) { bool d = keyDown(window, KEY); \
                                       if (d && !VAR) { BODY; } VAR = d; }

        EDGE(GLFW_KEY_1, k1, shadingMode = 0; std::cout << "\nShading: FLAT\n")
        EDGE(GLFW_KEY_2, k2, shadingMode = 1; std::cout << "\nShading: GOURAUD\n")
        EDGE(GLFW_KEY_3, k3, shadingMode = 2; std::cout << "\nShading: PHONG\n")
        EDGE(GLFW_KEY_P, kP, animate = !animate)
        EDGE(GLFW_KEY_B, kB, bloomOn = !bloomOn)
        EDGE(GLFW_KEY_G, kG, reflectOn = !reflectOn)
        EDGE(GLFW_KEY_M, kM, musicToggle(music); showNowPlaying(); updateTitle())
        EDGE(GLFW_KEY_T, kT, musicNext(music);   showNowPlaying(); updateTitle())
        EDGE(GLFW_KEY_C, kC, toggleDriving())
        EDGE(GLFW_KEY_F11, kF11, toggleFullscreen())
        EDGE(GLFW_KEY_E, kE, duskTarget = (duskTarget > 0.5f) ? 0.0f : 1.0f;
                             std::cout << (duskTarget > 0.5f ? "\nSunset...\n" : "\nNightfall...\n");
                             updateTitle())
        EDGE(GLFW_KEY_H, kH, rtOn = !rtOn;
                             std::cout << "\nRay-traced shadows " << (rtOn ? "ON" : "OFF") << "\n";
                             updateTitle())
        EDGE(GLFW_KEY_V, kV, startOrStopTour())
        EDGE(GLFW_KEY_X, kX, fxOn = !fxOn;
                             std::cout << "\nDetail effects " << (fxOn ? "ON" : "OFF") << "\n")
        EDGE(GLFW_KEY_R, kR, rainOn = !rainOn)
        EDGE(GLFW_KEY_L, kL, showLights = !showLights)
        EDGE(GLFW_KEY_F, kF, wireframe = !wireframe;
                             glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL))
        EDGE(GLFW_KEY_N, kN, seed = seed * 1664525u + 1013904223u;
                             generateCity(city, seed, NUM_WINDOW_TEX, NUM_BILLBOARD_TEX);
                             spawnVehicles(city, seed, 18);
                             player = PlayerCar();          // the old spot may be a building now
                             buildRayTraceScene(rtScene, city); // new buildings, new boxes
                             printCity())
        EDGE(GLFW_KEY_TAB, kTab, g_mouseCaptured = !g_mouseCaptured;
                                 glfwSetInputMode(window, GLFW_CURSOR,
                                     g_mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
                                 g_firstMouse = true)
        #undef EDGE

        // ---- render scale: how big the off-screen scene image is -------------
        //
        // The 3D scene is not drawn straight onto the screen but into an
        // off-screen image, which the final pass then copies onto the screen.
        // That copy can STRETCH it - so the scene can be drawn at a lower
        // resolution than the window and scaled up, with no other change.
        //
        // This matters for fullscreen. Measured on this laptop: about 92 FPS at
        // 1280 x 720 but only about 43 at 1920 x 1080 (2.25x the pixels) - and
        // with vsync on a 60 Hz screen, anything under 60 drops to 30.
        //
        // So the scene is capped at MAX_RENDER_PIXELS (1440 x 810). A 1280 x
        // 720 window is under the cap and is drawn at full size; 1080p
        // fullscreen is drawn at 75% and stretched. Raise the cap on a faster
        // machine for a sharper image.
        const float MAX_RENDER_PIXELS = 1440.0f * 810.0f;
        float renderScale = g_shotHD ? 1.0f
                          : std::min(1.0f, sqrtf(MAX_RENDER_PIXELS / ((float)g_width * (float)g_height)));
        renderScale *= dynScale;      // lowered automatically if the FPS drops (see "FPS" above)
        int   renderW = std::max(1, (int)(g_width  * renderScale));
        int   renderH = std::max(1, (int)(g_height * renderScale));

        // keep the off-screen buffers matching that size
        if (sceneFBO.width != renderW || sceneFBO.height != renderH) {
            createSceneFBO(sceneFBO, renderW, renderH);
            createBloomFBO(bloomFBO, renderW / 2, renderH / 2);
            createBloomFBO(streakFBO, renderW / 4, renderH / 2);
        }

        // ===================================================================
        //  1. UPDATE
        // ===================================================================
        // ---- time of day glides toward its target (about 9 s end to end) ----
        if (animate) {
            float step = deltaTime / 9.0f;
            dusk += std::max(-step, std::min(step, duskTarget - dusk));
        }

        // ---- the camera tour drives the camera ---------------------------------
        if (touring) {
            tourTime += deltaTime;
            glm::vec3 pos, look;
            tourSample(tourTime, pos, look);
            g_cam.position = pos;
            g_cam.front    = glm::normalize(look - pos);
            // the script: the sun sets, then rain comes in with the night
            if (tourTime > TOUR_NIGHTFALL) duskTarget = 0.0f;
            rainOn = dusk < 0.5f;
            titleTimer += deltaTime;
            if (titleTimer > 0.5f) { titleTimer = 0.0f; updateTitle(); }
            if (tourTime >= TOUR_LENGTH) startOrStopTour();
        }

        // ---- street lamps switch on as night falls -------------------------------
        // Each lamp has its own switch-on point between dusk = 0.72 and 0.42, so
        // as the sky darkens they come on in a scattered ripple across the
        // city instead of all in the same frame.
        lampOn.assign(city.lamps.size(), 0.0f);
        for (size_t i = 0; i < city.lamps.size(); ++i) {
            float at = 0.42f + 0.30f * hash01((int)i);
            lampOn[i] = 1.0f - glm::smoothstep(at - 0.03f, at + 0.03f, dusk);
        }

        // The AI traffic follows its rules (see traffic.h): keep right, keep a
        // gap to the car ahead, stop at red lights and for crossing cars.
        if (animate) updateTraffic(city, deltaTime, sceneTime, player);

        // Your car, if you are driving it.
        if (player.driving && animate)
            updatePlayer(player, city, deltaTime, throttle, steer, handbrake);

        carFrame.resize(city.vehicles.size());
        for (size_t i = 0; i < city.vehicles.size(); ++i) {
            const Vehicle& v = city.vehicles[i];

            // Two numbers describe a car: which road (lane) and how far along
            // it (t). Which slot each goes in depends on the road's direction.
            // Where the car is and which way it faces - on its lane, or part
            // way round a turn (traffic.h works out both).
            glm::vec3 pos = vehiclePos(v);
            glm::vec3 dir = vehicleForward(v);

            // The car model faces +X; turn it to face its direction of travel.
            glm::mat4 T(1.0f);
            T = glm::translate(T, pos);
            T = glm::rotate(T, yawFromDirection(dir), glm::vec3(0, 1, 0));
            carFrame[i] = T;
        }

        // ---- chase camera while driving ------------------------------------
        // The camera sits behind and above the car and looks a little ahead
        // of it. Instead of snapping to that spot it moves a fraction of the
        // way each frame - 1 - e^(-6 dt) - which is frame-rate independent
        // and makes it lag slightly on turns, like a real follow camera.
        if (player.driving) {
            glm::vec3 fwd     = player.forward();
            glm::vec3 wantPos = player.pos - fwd * 8.5f + glm::vec3(0.0f, 3.4f, 0.0f);
            glm::vec3 lookAt  = player.pos + fwd * 5.0f + glm::vec3(0.0f, 1.1f, 0.0f);
            float follow = 1.0f - expf(-6.0f * deltaTime);
            g_cam.position = glm::mix(g_cam.position, wantPos, follow);
            if (g_cam.position.y < 0.8f) g_cam.position.y = 0.8f;
            g_cam.front = glm::normalize(lookAt - g_cam.position);

            titleTimer += deltaTime;
            if (titleTimer > 0.25f) { titleTimer = 0.0f; updateTitle(); }   // speedometer
        }

        // ---- rain ----------------------------------------------------------
        //
        // THE RAIN FOLLOWS THE CAMERA.
        // Spreading 1500 drops over the whole 120 x 120 city puts only a few
        // anywhere near the viewer, and from street level it hardly looks like
        // rain at all. Instead every drop lives in a box RAIN_BOX units either
        // side of the camera. A drop that drifts out of one side of the box
        // re-enters on the opposite side (like the screen wrap in old arcade
        // games), so walking forward never leaves the rain behind - and the
        // same 1500 drops are about 20 times denser around you.
        //
        // When a drop reaches the ground it leaves a splash, then restarts at
        // the top of the box at a new random spot.
        const float     RAIN_BOX = 30.0f;
        // WIND IN GUSTS: a steady breeze plus two slow sine waves of
        // unrelated period multiplied together, so every so often the rain
        // leans over harder and then straightens again - never in a visible
        // repeating rhythm.
        const float gust = sinf(sceneTime * 0.37f) * sinf(sceneTime * 0.11f + 1.3f);
        const glm::vec3 WIND(1.6f + 3.2f * gust, 0.0f, 0.9f + 1.4f * sinf(sceneTime * 0.23f));
        const float     rainTop  = std::max(g_cam.position.y + 30.0f, 40.0f);

        // Height of whatever the drop would land on: the raised pavement
        // inside a block, or the road between blocks. Road centre-lines sit at
        // -HALF + k*CELL, so 'local' is the distance past the nearest one
        // below; the block occupies ROAD/2 .. ROAD/2 + BLOCK of each cell.
        auto groundAt = [&](float x, float z) {
            float lx = fmodf(x + HALF + CITY_SPAN * 4.0f, CELL);
            float lz = fmodf(z + HALF + CITY_SPAN * 4.0f, CELL);
            bool inCity  = fabsf(x) < HALF && fabsf(z) < HALF;
            bool onBlock = lx > ROAD * 0.5f && lx < ROAD * 0.5f + BLOCK &&
                           lz > ROAD * 0.5f && lz < ROAD * 0.5f + BLOCK;
            // Past the island's edge a drop falls all the way to the water.
            if (fabsf(x) > ISLAND_EDGE || fabsf(z) > ISLAND_EDGE) return WATER_Y;
            // The promenade outside the ring road is raised pavement too -
            // except the gaps where the two avenues run through to the tunnels.
            float ring = HALF + ROAD * 0.5f;
            bool promX = fabsf(x) > ring && fabsf(z) > ROAD * 0.5f;
            bool promZ = fabsf(z) > ring && fabsf(x) > ROAD * 0.5f;
            if (promX || promZ) return SLAB_H;
            return (inCity && onBlock) ? SLAB_H : 0.0f;
        };
        // Wrap a coordinate back into centre +/- half.
        auto wrapAround = [](float v, float centre, float half) {
            float d = fmodf(v - centre + half, 2.0f * half);
            if (d < 0.0f) d += 2.0f * half;
            return centre + d - half;
        };

        if (rainOn) {
            for (RainDrop& d : city.rain) {
                if (animate)
                    d.pos += (WIND - glm::vec3(0.0f, d.speed, 0.0f)) * deltaTime;

                d.pos.x = wrapAround(d.pos.x, g_cam.position.x, RAIN_BOX);
                d.pos.z = wrapAround(d.pos.z, g_cam.position.z, RAIN_BOX);

                // Drops far above the box (the camera just came down from
                // high up) are dropped straight into it instead of taking
                // seconds to fall into view.
                if (d.pos.y > rainTop + 5.0f) d.pos.y = rainRng.range(0.0f, rainTop);

                float ground = groundAt(d.pos.x, d.pos.z);
                if (d.pos.y < ground) {
                    if (animate) {
                        splashes[nextSplash] = { glm::vec3(d.pos.x, ground, d.pos.z), 0.0f, true };
                        nextSplash = (nextSplash + 1) % SPLASH_MAX;
                    }
                    d.pos = glm::vec3(g_cam.position.x + rainRng.range(-RAIN_BOX, RAIN_BOX),
                                      rainTop - rainRng.range(0.0f, 4.0f),
                                      g_cam.position.z + rainRng.range(-RAIN_BOX, RAIN_BOX));
                }
            }
            if (animate) {
                for (Splash& sp : splashes) {
                    if (!sp.alive) continue;
                    sp.age += deltaTime;
                    if (sp.age > SPLASH_LIFE) sp.alive = false;
                }
            }
        }

        // ===================================================================
        //  2. LIGHTS - score every candidate, keep the best 24
        // ===================================================================
        struct Candidate { LightSource light; float score; };
        std::vector<Candidate> cands;
        cands.reserve(160);

        // weight < 1 makes a light count as "closer" than it is, so it wins
        // a slot more easily. Moving lights get the biggest boost.
        auto consider = [&](const LightSource& L, float weight) {
            glm::vec3 d = L.position - g_cam.position;
            float d2 = glm::dot(d, d);
            float reach = L.range + 200.0f;
            if (d2 > reach * reach) return;      // too far to matter at all
            cands.push_back({ L, d2 * weight });
        };
        // Build a point light (shines every way) or a spotlight (a cone).
        auto pointLight = [](const glm::vec3& p, const glm::vec3& c, float range) {
            LightSource L; L.position = p; L.color = c; L.range = range;
            return L;
        };
        auto spotLight = [](const glm::vec3& p, const glm::vec3& c, float range,
                            const glm::vec3& dir, float cutoffDeg, float exponent) {
            LightSource L; L.position = p; L.color = c; L.range = range;
            L.direction = glm::normalize(dir);
            L.cosCutoff = cosf(glm::radians(cutoffDeg));
            L.spotExp   = exponent;
            return L;
        };

        for (size_t li = 0; li < city.lamps.size(); ++li) {
            const StreetLamp& L = city.lamps[li];
            if (lampOn[li] < 0.02f) continue;          // still off: frees a light slot
            float rad = glm::radians(L.facing);
            glm::vec3 outward(cosf(rad), 0.0f, sinf(rad));
            glm::vec3 head = L.position + glm::vec3(0.0f, 7.0f, 0.0f) + outward * 2.4f;
            // STREET LAMP = SPOTLIGHT pointing straight down.
            //   cutoff 58 deg : from 7 units up, the cone reaches the road
            //                   7 * tan(58) = about 11 units out - a pool that
            //                   covers the road without lighting the rooftops
            //   exponent 3    : brightest right under the lamp, fading outward
            // The cone means light no longer spills up the building walls
            // above the lamp, which is what a real street lamp does.
            consider(spotLight(head, glm::vec3(1.00f, 0.78f, 0.45f) * 2.2f * lampOn[li], 22.0f,
                               glm::vec3(0.0f, -1.0f, 0.0f), 58.0f, 3.0f), 1.0f);
        }
        for (size_t i = 0; i < city.vehicles.size(); ++i) {
            // HEADLIGHTS = SPOTLIGHT aimed forward and slightly down, in the
            // car's own frame: carFrame turns the car's +X ("forward") into
            // the world direction it is driving.
            glm::vec3 front = glm::vec3(carFrame[i] * glm::vec4(1.9f, 0.5f, 0.0f, 1.0f));
            glm::vec3 ahead = glm::vec3(carFrame[i] * glm::vec4(1.0f, -0.12f, 0.0f, 0.0f));
            consider(spotLight(front, glm::vec3(0.85f, 0.90f, 1.00f) * 3.0f, 20.0f,
                               ahead, 32.0f, 6.0f), 0.3f);
        }
        if (player.exists) {
            // Your headlights get the highest priority (weight 0.05), so the
            // road ahead of you is always lit.
            glm::vec3 fwd = player.forward();
            consider(spotLight(player.pos + fwd * 1.9f + glm::vec3(0.0f, 0.5f, 0.0f),
                               glm::vec3(0.9f, 0.95f, 1.0f) * 3.4f, 24.0f,
                               fwd + glm::vec3(0.0f, -0.12f, 0.0f), 30.0f, 6.0f), 0.05f);
        }
        for (int k = 0; k < NUM_FLYERS; ++k)
            consider(pointLight(flyerPosition(sceneTime, k), FLYER_GLOW[k] * 3.0f, 40.0f), 0.3f);
        // A big screen washes the street in front of it with its own colour:
        // a point light a little in front of the screen's centre, tinted by
        // the picture's average colour.
        for (const BigBillboard& bb : city.bigBoards) {
            ScreenContent sc = bigScreen(bb);
            float h = bb.width / sc.aspect;
            glm::mat4 T(1.0f);
            T = glm::translate(T, bb.roof);
            T = glm::rotate(T, glm::radians(bb.yaw), glm::vec3(0, 1, 0));
            // one light in front of EACH face, since both sides now glow
            for (float zOut : { 3.0f, -3.5f }) {
                glm::vec3 centre = glm::vec3(T * glm::vec4(0.0f, BIG_LEG_H + h * 0.5f, zOut, 1.0f));
                consider(pointLight(centre, sc.glow * 3.2f, 30.0f), 0.5f);
            }
        }
        for (const NeonStrip& n : city.neons) {
            if (n.kind != NEON_VERTICAL) continue;
            consider(pointLight(n.position + glm::vec3(0.0f, n.height * 0.5f, 0.0f),
                                n.color * 1.4f, 14.0f), 0.7f);
        }

        // Only the best MAX_LIGHTS need to be in order, so a partial sort is
        // enough - cheaper than sorting the whole list.
        size_t keep = std::min(cands.size(), (size_t)MAX_LIGHTS);
        std::partial_sort(cands.begin(), cands.begin() + keep, cands.end(),
                          [](const Candidate& a, const Candidate& b) { return a.score < b.score; });
        g_lights.clear();
        for (size_t i = 0; i < keep; ++i) g_lights.push_back(cands[i].light);

        // ===================================================================
        //  Into the off-screen buffer
        // ===================================================================
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO.fbo);
        glViewport(0, 0, renderW, renderH);      // the scene image's size, not the window's
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        // Clearing BOTH buffers matters: forgetting the depth bit leaves last
        // frame's depths in place and the image freezes over.
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(glm::radians(62.0f),
                                                (float)g_width / (float)g_height,
                                                0.1f, 420.0f);
        glm::mat4 view = g_cam.viewMatrix();

        // ---- everything the time of day controls ------------------------------
        // dusk = 1 is sunset, 0 is night; mix(a, b, dusk) blends between them.
        //
        // The sun: its height above the horizon drops from 9 degrees to 6
        // below as dusk goes 1 -> 0 (it really sets), and its light fades out
        // over the last third. The moon's light fades in as the sun's goes.
        float     sunElev  = glm::mix(-0.10f, 0.16f, dusk);              // radians
        glm::vec3 toSun    = glm::normalize(SUN_AZIMUTH * cosf(sunElev) + glm::vec3(0.0f, sinf(sunElev), 0.0f));
        // Strong: a low sun is the brightest, most directional light of the
        // day, which is what makes its (ray-traced) shadows bold.
        glm::vec3 sunLight = glm::vec3(1.00f, 0.56f, 0.30f) * 3.5f * glm::smoothstep(0.0f, 0.35f, dusk);
        glm::vec3 moonLight= glm::vec3(0.064f, 0.051f, 0.120f) * (1.0f - dusk);

        // One colour shared by the fog AND the sky's horizon, so the far city
        // fades into the sky with no visible edge: deep violet at night,
        // burning orange at sunset.
        // The dusk haze is kept DARK and muted: the fog colour covers the whole
        // distant view, so a bright orange turned the city into a sandstorm.
        // The brightness of a sunset is concentrated round the sun instead.
        const glm::vec3 fogColor = glm::mix(glm::vec3(0.010f, 0.007f, 0.024f),
                                            glm::vec3(0.110f, 0.045f, 0.050f), dusk);
        const glm::vec3 zenith   = glm::mix(glm::vec3(0.002f, 0.002f, 0.009f),
                                            glm::vec3(0.030f, 0.022f, 0.080f), dusk);

        // ---- the sky -------------------------------------------------------
        // Stripping the translation from the view matrix (keeping only the
        // rotation) makes the sky turn with the head but never move when you
        // walk - which is what makes it feel infinitely far away.
        //
        // The sky shader puts every pixel at the very back (depth 1.0), so the
        // depth test must be LESS-OR-EQUAL for it to pass against the cleared
        // depth of 1.0. Culling is off because we are INSIDE the box.
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_CULL_FACE);
        glUseProgram(skyProg);
        glm::mat4 skyViewMat = glm::mat4(glm::mat3(view));
        glUniformMatrix4fv(skyView, 1, GL_FALSE, glm::value_ptr(skyViewMat));
        glUniformMatrix4fv(skyProj, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(skyHoriz, 1, glm::value_ptr(fogColor));
        glUniform3fv(skyZen, 1, glm::value_ptr(zenith));
        glUniform3fv(skySun, 1, glm::value_ptr(toSun));
        glUniform1f(skyDusk, dusk);
        glUniform1f(skyTime, sceneTime);
        glUniform3fv(skyMoon, 1, glm::value_ptr(MOON_DIR));
        drawMesh(skybox);
        glEnable(GL_CULL_FACE);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        // ---- values shared by everything in the scene ------------------------
        glUseProgram(sceneProg);
        glUniformMatrix4fv(U.view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(U.projection, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(U.viewPos, 1, glm::value_ptr(g_cam.position));
        glUniform1i(U.shadingMode, shadingMode);
        glUniform1i(U.tex0, 0);
        // The detail layer. Each effect is switched on only around the draws
        // that use it (like 'waves' for the water), so start with all off.
        glUniform1f(U.detail,  fxOn ? 1.0f : 0.0f);
        glUniform1f(U.rainAmt, rainOn ? 1.0f : 0.0f);
        glUniform1f(U.mirrorOn, reflectOn ? 1.0f : 0.0f);
        glUniform1f(U.puddles, 0.0f);
        glUniform1f(U.envReflect, 0.0f);
        glUniform1f(U.beam, 0.0f);
        glUniform1f(U.holo, 0.0f);
        glUniform1f(U.streak, 0.0f);
        glUniform1f(U.shoreEdge, ISLAND_EDGE);
        glUniform1f(U.outScale, 1.0f);
        // Only a little more ambient at sunset: too much fills in the shadows
        // and the sun's light stops looking directional.
        // Ambient = the soft light from the whole sky that every surface gets.
        // It is what keeps the walls facing AWAY from the sun and lamps from
        // going pure black: the filmic tone curve crushes very dark values,
        // so a shaded wall needs this much to read as dark blue-grey.
        glUniform1f(U.ambient, glm::mix(0.035f, 0.05f, dusk));
        glUniform1f(U.time, sceneTime);      // drives the windows switching on/off

        // Moonlight: a dim lavender directional light shining FROM the moon
        // (so the opposite of MOON_DIR), matching the moon drawn in the sky.
        // It keeps surfaces facing away from every lamp from being pure black.
        glUniform3fv(U.moonDir, 1, glm::value_ptr(-MOON_DIR));
        glUniform3fv(U.moonColor, 1, glm::value_ptr(moonLight));
        glUniform3fv(U.sunDir, 1, glm::value_ptr(-toSun));          // the way sunlight travels
        glUniform3fv(U.sunColor, 1, glm::value_ptr(sunLight));

        // Ray-tracing data: the box texture on texture unit 2, and where each
        // grid cell's boxes are in it.
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, rtScene.tex);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(U.rtBoxes, 2);
        for (int c = 0; c < 16; ++c) {
            glUniform1i(U.rtCellStart[c], rtScene.cellStart[c]);
            glUniform1i(U.rtCellCount[c], rtScene.cellCount[c]);
        }
        glUniform1i(U.rtEnabled, rtOn ? 1 : 0);

        glUniform3fv(U.fogColor, 1, glm::value_ptr(fogColor));
        // Thinner than before, so the skyline across the water shows through.
        glUniform1f(U.fogDensity, glm::mix(0.0045f, 0.0032f, dusk));   // clearer air at sunset
        glUniform4f(U.clipPlane, 0.0f, 0.0f, 0.0f, 1.0f);   // no clipping
        glUniform1f(U.waves, 0.0f);

        // ===================================================================
        //  3. REFLECTION - the city again, upside down below the road
        //
        //  scale(1, -1, 1) flips every y. A mirrored triangle's corners come
        //  out in the opposite order, so clockwise becomes the new "front" -
        //  without flipping glFrontFace the reflection would be drawn inside
        //  out, showing only the back walls.
        // ===================================================================
        if (reflectOn) {
            g_mirror = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));
            glFrontFace(GL_CW);
            // Only the 8 most important lights for the reflection. Every
            // pixel loops over every light it is given, and the reflection is
            // dim and half see-through under the wet road - the other 16
            // lights would cost a full second lighting pass for no visible
            // difference.
            uploadLights(true, 8);

            // Keep only what the flip puts BELOW the ground (see clipPlane in
            // the vertex shader). Without this, the underwater parts of the sea
            // wall, bridge pillars and far towers would be flipped UP through
            // the island as ghost walls.
            glEnable(GL_CLIP_DISTANCE0);
            glUniform4f(U.clipPlane, 0.0f, -1.0f, 0.0f, 0.0f);
            // No ray-traced shadows in the reflection: its positions are
            // mirrored (the rays would start underground), the blurred,
            // see-through reflection would not show them anyway, and skipping
            // them halves the ray-tracing work.
            glUniform1i(U.rtEnabled, 0);
            // The reflected city is drawn DIMMER: a wet street reflects only
            // part of the light. Without this, against the very dark asphalt
            // even a faint mirror image of the bright windows dominated the
            // road. (Lower = weaker street reflections.)
            glUniform1f(U.outScale, 0.45f);

            drawCity();
            glUniform1f(U.outScale, 1.0f);

            glUniform1i(U.rtEnabled, rtOn ? 1 : 0);
            glUniform4f(U.clipPlane, 0.0f, 0.0f, 0.0f, 1.0f);
            glDisable(GL_CLIP_DISTANCE0);

            glFrontFace(GL_CCW);
            g_mirror = glm::mat4(1.0f);
        }
        uploadLights(false);

        // ===================================================================
        //  4. PAVEMENTS AND THE WET ROAD
        // ===================================================================
        // Pavement slabs are solid and only slightly damp: they cover the
        // reflection underneath them, which is correct - pavements are not
        // mirrors. EXCEPT in their puddles: there the shader lowers alpha, so
        // blending is on and the reflected city shows through the water.
        // KERBS: a strip of pale kerb stone round every block, a little
        // higher than the slab and slightly proud of its edge. Real streets
        // are told apart from pavements mostly by this bright edge line -
        // without it the dark wet slab and the dark wet road merged.
        for (const glm::vec3& c : city.blockCentres) {
            const float K = 0.32f, KH = SLAB_H + 0.05f, E = BLOCK * 0.5f + 0.02f;
            const glm::vec3 kerbCol(0.13f, 0.13f, 0.14f);
            const glm::vec3 pos[4]  = { c + glm::vec3(0, 0,  E - K * 0.5f), c + glm::vec3(0, 0, -E + K * 0.5f),
                                        c + glm::vec3( E - K * 0.5f, 0, 0), c + glm::vec3(-E + K * 0.5f, 0, 0) };
            const glm::vec3 size[4] = { glm::vec3(2 * E, KH, K), glm::vec3(2 * E, KH, K),
                                        glm::vec3(K, KH, 2 * E), glm::vec3(K, KH, 2 * E) };
            for (int k = 0; k < 4; ++k)
                drawSurface(cube, glm::scale(glm::translate(glm::mat4(1.0f), pos[k]), size[k]),
                            kerbCol, stoneTex, glm::vec3(size[k].x, 1.0f, size[k].z) * 0.5f, 0,
                            0.0f, 0.35f, 40.0f, 0.0f, 1.0f);
        }

        glEnable(GL_BLEND);
        glUniform1f(U.puddles, fxOn ? 1.0f : 0.0f);
        for (const glm::vec3& c : city.blockCentres) {
            glm::mat4 m(1.0f);
            m = glm::translate(m, c);
            m = glm::scale(m, glm::vec3(BLOCK, SLAB_H, BLOCK));
            // Paving tiles: the texture holds a 4 x 4 grid of slabs and repeats
            // every 4 units, so each slab is about 1 unit (1.25 m) square.
            // Paving is LIGHTER and a touch warmer than the asphalt (0.022),
            // so the two read as different materials.
            drawWet(cube, m, glm::vec3(0.060f, 0.057f, 0.058f), tileTex,
                    glm::vec3(BLOCK / 4.0f, 1.0f, BLOCK / 4.0f), 0.0f, 1.0f, 0.25f);
        }

        // ---- the waterfront promenade --------------------------------------
        // The same raised pavement, running all round OUTSIDE the ring road:
        // from the road's outer edge (HALF + ROAD/2 = 64) to the sea wall
        // (ISLAND_EDGE = 72). It gives the ring road a kerb on its outer side
        // too - before this, the road's outer edge just faded into the flat
        // quay, while its inner edge was clear because of the blocks.
        //
        // Eight pieces: the north and south strips run the full width; the
        // east and west strips fit between them (so no two pieces overlap and
        // flicker). Each strip is split where a central avenue crosses it on
        // its way to its tunnel.
        {
            // (Not called IN / OUT: windows.h defines both as empty macros.)
            const float innerEdge = HALF + ROAD * 0.5f;          // 64: the ring road's outer edge
            const float outerEdge = ISLAND_EDGE;                 // 72: the sea wall
            const float GAP = ROAD * 0.5f;                 // half the avenue's width
            auto slab = [&](float x0, float x1, float z0, float z1) {
                glm::mat4 m(1.0f);
                m = glm::translate(m, glm::vec3((x0 + x1) * 0.5f, 0.0f, (z0 + z1) * 0.5f));
                m = glm::scale(m, glm::vec3(x1 - x0, SLAB_H, z1 - z0));
                drawWet(cube, m, glm::vec3(0.060f, 0.057f, 0.058f), tileTex,
                        glm::vec3((x1 - x0) / 4.0f, 1.0f, (z1 - z0) / 4.0f), 0.0f, 1.0f, 0.25f);
            };
            // A low BARRIER along the ring road's outer kerb, with a thin amber
            // light strip on top: the city's outer border, as clear as the
            // inner one. Same split as the pavement where the avenues pass.
            auto barrier = [&](float x0, float x1, float z0, float z1) {
                // solid, so drawn without blending (its glowing trim's bloom
                // would otherwise be blended away by the streak-flag alpha)
                glDisable(GL_BLEND);
                glm::vec3 c((x0 + x1) * 0.5f, SLAB_H, (z0 + z1) * 0.5f);
                glm::vec3 sz(x1 - x0, 0.55f, z1 - z0);
                drawLit(cube, glm::scale(glm::translate(glm::mat4(1.0f), c), sz),
                        glm::vec3(0.07f, 0.07f, 0.085f), 0.4f, 32.0f);
                drawEmissive(cube, glm::scale(glm::translate(glm::mat4(1.0f), c + glm::vec3(0.0f, 0.55f, 0.0f)),
                                              glm::vec3(sz.x, 0.06f, sz.z)),
                             glm::vec3(1.00f, 0.62f, 0.20f), 0.8f, 0,
                             glm::vec2(1.0f), glm::vec2(0.0f), 0.5f);   // a line, not a glowing bar
                glEnable(GL_BLEND);
            };
            const float B0 = innerEdge + 0.05f, B1 = innerEdge + 0.40f;   // barrier thickness
            for (int s = -1; s <= 1; s += 2) {
                float b0 = fminf(s * B0, s * B1), b1 = fmaxf(s * B0, s * B1);
                barrier(-innerEdge, -GAP, b0, b1);   barrier(GAP, innerEdge, b0, b1);   // N / S
                barrier(b0, b1, -innerEdge, -GAP);   barrier(b0, b1, GAP, innerEdge);   // E / W
            }

            for (int s = -1; s <= 1; s += 2) {
                // north / south strips (full width, split at x = 0)
                slab(-outerEdge, -GAP, fminf(s * innerEdge, s * outerEdge), fmaxf(s * innerEdge, s * outerEdge));
                slab( GAP,  outerEdge, fminf(s * innerEdge, s * outerEdge), fmaxf(s * innerEdge, s * outerEdge));
                // east / west strips (between them, split at z = 0)
                slab(fminf(s * innerEdge, s * outerEdge), fmaxf(s * innerEdge, s * outerEdge), -innerEdge, -GAP);
                slab(fminf(s * innerEdge, s * outerEdge), fmaxf(s * innerEdge, s * outerEdge),  GAP,  innerEdge);
            }
        }

        // The road is one huge plane, drawn HALF SEE-THROUGH over the upside-
        // down city. Blending mixes the two: out = road * a + reflection * (1-a).
        // The shader lowers 'a' at grazing angles (Fresnel), so the reflection
        // strengthens toward the horizon exactly as on a real wet street.
        // Its puddles (still on from the pavements) are near-mirrors.
        {
            // The island's ground: exactly the island, edge to edge.
            glm::mat4 m(1.0f);
            m = glm::scale(m, glm::vec3(ISLAND_EDGE * 2.0f, 1.0f, ISLAND_EDGE * 2.0f));
            drawWet(quad, m, glm::vec3(0.022f, 0.022f, 0.028f), asphaltTex,
                    glm::vec3(35.0f, 35.0f, 35.0f), 1.0f, reflectOn ? 0.95f : 1.0f);
        }
        glUniform1f(U.puddles, 0.0f);

        // ---- the water round the island ------------------------------------
        // Four big rectangles forming a frame round the island - NOT one huge
        // sheet, because a sheet under the island would sit between the road
        // and its reflection and dim it. Darker, more see-through and shinier
        // than the road, with moving waves on the normal: it reflects the
        // island, the bridges and the far skyline much more strongly.
        {
            const float SEA = 700.0f;                 // reaches past the fog
            const float E   = ISLAND_EDGE;
            const glm::vec3 centre[4] = {
                glm::vec3(0.0f, WATER_Y,  (E + SEA) * 0.5f), glm::vec3(0.0f, WATER_Y, -(E + SEA) * 0.5f),
                glm::vec3( (E + SEA) * 0.5f, WATER_Y, 0.0f), glm::vec3(-(E + SEA) * 0.5f, WATER_Y, 0.0f) };
            const glm::vec3 size[4] = {
                glm::vec3(SEA * 2.0f, 1.0f, SEA - E), glm::vec3(SEA * 2.0f, 1.0f, SEA - E),
                glm::vec3(SEA - E, 1.0f, E * 2.0f),   glm::vec3(SEA - E, 1.0f, E * 2.0f) };

            glUniform1f(U.waves, 0.16f);
            for (int i = 0; i < 4; ++i) {
                glm::mat4 m(1.0f);
                m = glm::translate(m, centre[i]);
                m = glm::scale(m, size[i]);
                drawSurface(quad, m, glm::vec3(0.004f, 0.005f, 0.014f), 0, glm::vec3(1.0f), 0,
                            0.0f, 1.3f, 160.0f, 1.0f, reflectOn ? 0.50f : 1.0f);
            }
            glUniform1f(U.waves, 0.0f);
        }
        glDisable(GL_BLEND);

        // ---- lane markings, faintly glowing --------------------------------
        for (int i = 0; i <= GRID; ++i) {
            float p = -HALF + i * CELL;
            for (float t = -HALF; t < HALF; t += 9.0f) {
                glm::mat4 a(1.0f);
                a = glm::translate(a, glm::vec3(p, 0.02f, t));
                a = glm::scale(a, glm::vec3(0.25f, 1.0f, 2.6f));
                drawEmissive(quad, a, glm::vec3(0.85f, 0.70f, 0.25f), 0.32f, 0, glm::vec2(1.0f), glm::vec2(0.0f), 0.15f);

                glm::mat4 b(1.0f);
                b = glm::translate(b, glm::vec3(t, 0.02f, p));
                b = glm::scale(b, glm::vec3(2.6f, 1.0f, 0.25f));
                drawEmissive(quad, b, glm::vec3(0.85f, 0.70f, 0.25f), 0.32f, 0, glm::vec2(1.0f), glm::vec2(0.0f), 0.15f);
            }
        }

        // ===================================================================
        //  5. THE CITY, THE RIGHT WAY UP
        // ===================================================================
        drawCity();

        // ---- rain and splashes --------------------------------------------------
        // Rebuild the line list from the current positions, upload it, draw
        // it in one call. Not reflected - not worth a second pass.
        //
        // The vertex COLOUR is used as a brightness: the shader multiplies it
        // into the glow. So each streak fades from a faint tail (0.15) to a
        // bright tip (1.0), and each ripple fades out as it spreads.
        // The lamps whose beams could hold rain right now: switched on and
        // near the camera (the rain only exists in a box round the camera).
        // Lamp heads hang 7 units up; the visible beam reaches BEAM_R wide
        // at the ground.
        const float BEAM_H = 7.0f, BEAM_R = 2.6f;
        struct BeamLamp { glm::vec3 head; float on; };
        std::vector<BeamLamp> beamLamps;
        for (size_t li = 0; li < city.lamps.size(); ++li) {
            if (lampOn[li] < 0.02f) continue;
            const StreetLamp& L = city.lamps[li];
            float rad = glm::radians(L.facing);
            glm::vec3 head = L.position + glm::vec3(0.0f, BEAM_H, 0.0f)
                           + glm::vec3(cosf(rad), 0.0f, sinf(rad)) * 2.4f;
            glm::vec2 d(head.x - g_cam.position.x, head.z - g_cam.position.z);
            if (glm::dot(d, d) < 95.0f * 95.0f) beamLamps.push_back({ head, lampOn[li] });
        }

        if (rainOn) {
            rainVerts.clear();

            // Streaks. Each is drawn from the drop back along its motion, so
            // it slants with the wind like real rain seen at a shutter speed.
            //
            // A drop falling THROUGH a lamp's beam catches its light: the
            // vertex colour is pushed up and toward the lamp's warm orange,
            // so the beams fill with bright falling streaks - the classic
            // rainy-street-light shot. (Inside the cone = closer to the axis
            // than the cone's radius at that height.)
            //
            // DEPTH: drops close to the camera are brighter than distant ones,
            // which fade toward the haze - so the rain has layers instead of
            // one flat curtain of identical lines.
            //
            // HEADLIGHTS: a drop inside a car's headlight cone (forward of the
            // car, within a widening radius of its axis) flashes cool white.
            struct HeadCone { glm::vec3 apex, fwd; };
            std::vector<HeadCone> heads;
            if (fxOn) {
                for (size_t i = 0; i < city.vehicles.size(); ++i) {
                    glm::vec3 p(carFrame[i][3]), f(carFrame[i][0]);
                    glm::vec2 d(p.x - g_cam.position.x, p.z - g_cam.position.z);
                    if (glm::dot(d, d) < (RAIN_BOX + 12.0f) * (RAIN_BOX + 12.0f))
                        heads.push_back({ p + f * (CAR_LENGTH * 0.5f) + glm::vec3(0.0f, 0.45f, 0.0f), f });
                }
                if (player.exists) {
                    glm::vec3 f = player.forward();
                    heads.push_back({ player.pos + f * (CAR_LENGTH * 0.5f) + glm::vec3(0.0f, 0.45f, 0.0f), f });
                }
            }
            for (const RainDrop& d : city.rain) {
                glm::vec3 vel  = WIND - glm::vec3(0.0f, d.speed, 0.0f);
                glm::vec3 tail = d.pos - vel * (0.028f * d.length);
                float dist  = glm::length(d.pos - g_cam.position);
                float layer = 1.25f - 0.85f * std::min(dist / (RAIN_BOX * 1.2f), 1.0f);
                glm::vec3 c(layer);
                if (d.pos.y < 3.5f) {
                    for (const HeadCone& h : heads) {
                        glm::vec3 v = d.pos - h.apex;
                        float along = glm::dot(v, h.fwd);
                        if (along < 0.2f || along > 12.0f) continue;
                        float r = 0.35f + along * 0.16f;
                        glm::vec3 perp = v - h.fwd * along;
                        if (glm::dot(perp, perp) < r * r) {
                            c = glm::vec3(3.0f, 3.2f, 3.6f) * (1.0f - along / 12.0f) + c;
                            break;
                        }
                    }
                }
                if (fxOn && d.pos.y < BEAM_H) {
                    for (const BeamLamp& b : beamLamps) {
                        float dx = d.pos.x - b.head.x, dz = d.pos.z - b.head.z;
                        float r  = BEAM_R * (1.0f - d.pos.y / BEAM_H) + 0.15f;
                        float q  = (dx * dx + dz * dz) / (r * r);
                        if (q < 1.0f) {
                            float k = (1.0f - q) * b.on;
                            c = glm::mix(c, glm::vec3(4.2f, 2.9f, 1.4f), k);
                            break;
                        }
                    }
                }
                pushVert(rainVerts, tail.x,  tail.y,  tail.z,  0.15f*c.x,0.15f*c.y,0.15f*c.z, 0,1,0, 0,0);
                pushVert(rainVerts, d.pos.x, d.pos.y, d.pos.z, c.x, c.y, c.z,  0,1,0, 0,0);
            }

            // Ripples. A ring whose radius grows with age and whose brightness
            // falls away: t runs 0 -> 1 over the splash's life, radius from
            // 0.05 to 0.40, brightness as (1 - t)^2 so it fades out quickly.
            // Kept small and dim: a big bright ring right by the camera shows
            // its straight edges and reads as a wireframe shape, not water.
            const float TWO_PI = 6.2831853f;
            for (const Splash& sp : splashes) {
                if (!sp.alive) continue;
                // THE CROWN: in the first 0.18 s a few tiny droplets jump up
                // and out from the impact and fall back - a short ballistic
                // arc, p = p0 + v t + 0.5 g t^2, each a little line along its
                // own velocity. Directions come from the splash's position, so
                // they are random-looking but need no stored state.
                if (sp.age < 0.18f) {
                    float s = sp.age;
                    float b = (1.0f - s / 0.18f) * 0.9f;
                    for (int k = 0; k < SPLASH_DROPS; ++k) {
                        float a  = 6.2831853f * (k + fract01(sp.pos.x * 3.7f + sp.pos.z * 1.3f)) / SPLASH_DROPS;
                        glm::vec3 v0(cosf(a) * 1.1f, 2.4f, sinf(a) * 1.1f);
                        glm::vec3 p  = sp.pos + v0 * s + glm::vec3(0.0f, -9.8f * 0.5f * s * s, 0.0f);
                        glm::vec3 vv = v0 + glm::vec3(0.0f, -9.8f * s, 0.0f);
                        glm::vec3 q  = p - vv * 0.025f;
                        pushVert(rainVerts, q.x, q.y + 0.03f, q.z, b*0.4f,b*0.4f,b*0.4f, 0,1,0, 0,0);
                        pushVert(rainVerts, p.x, p.y + 0.03f, p.z, b,b,b,             0,1,0, 0,0);
                    }
                }
                float t      = sp.age / SPLASH_LIFE;
                float radius = 0.05f + 0.35f * t;
                float b      = (1.0f - t) * (1.0f - t) * 0.6f;
                float y      = sp.pos.y + 0.03f;     // just above the surface
                for (int k = 0; k < SPLASH_SEGS; ++k) {
                    float a0 = TWO_PI * k / SPLASH_SEGS, a1 = TWO_PI * (k + 1) / SPLASH_SEGS;
                    pushVert(rainVerts, sp.pos.x + cosf(a0) * radius, y, sp.pos.z + sinf(a0) * radius,
                             b,b,b, 0,1,0, 0,0);
                    pushVert(rainVerts, sp.pos.x + cosf(a1) * radius, y, sp.pos.z + sinf(a1) * radius,
                             b,b,b, 0,1,0, 0,0);
                }
            }
            glBindBuffer(GL_ARRAY_BUFFER, rainVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, rainVerts.size() * sizeof(float), rainVerts.data());

            setModel(glm::mat4(1.0f));
            glUniform3f(U.tint, 1.0f, 1.0f, 1.0f);
            glUniform1f(U.emissive, 1.0f);
            glUniform3f(U.emissiveColor, 0.55f, 0.65f, 0.85f);
            glUniform1f(U.bloomAmount, 0.0f);      // rain never glows
            glUniform1i(U.useTexture, 0);
            glBindTexture(GL_TEXTURE_2D, g_whiteTex);

            glBindVertexArray(rainVAO);
            glDrawArrays(GL_LINES, 0, (GLsizei)(rainVerts.size() / FLOATS_PER_VERTEX));
            glBindVertexArray(0);
        }

        // ---- LIGHT BEAMS ------------------------------------------------------
        // Cones of light under the street lamps and in front of every car,
        // drawn last and ADDED on top (glBlendFunc(ONE, ONE): out = dst + src),
        // so overlapping beams simply get brighter, like real light does.
        // They are see-through, so they do not write depth (they must not
        // hide what is behind them), and both sides of the cone are drawn:
        // the far side through the near side is what gives a beam its body.
        // The shading itself is the 'beam' branch at the top of SCENE_FS.
        if (fxOn) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glUniform1f(U.emissive, 0.0f);
            glUniform1i(U.useTexture, 0);
            glUniform1f(U.alpha, 1.0f);

            // street lamps: from the head straight down to the pavement
            glUniform3f(U.emissiveColor, 1.00f, 0.74f, 0.42f);
            for (const BeamLamp& b : beamLamps) {
                glm::mat4 m(1.0f);
                m = glm::translate(m, glm::vec3(b.head.x, SLAB_H, b.head.z));
                m = glm::scale(m, glm::vec3(BEAM_R * 2.0f, b.head.y - 0.25f - SLAB_H, BEAM_R * 2.0f));
                glUniform1f(U.beam, 0.065f * b.on);
                setModel(m);
                drawMesh(cone);
            }

            // headlights: a long thin cone from each lamp, tipped 3 degrees
            // down. In the car's frame forward is +X, so the cone (apex at
            // its top) is first moved so its apex is at the origin, then
            // turned 90 - 3 degrees about Z, which swings "down the cone"
            // to point forward and a little toward the road.
            const float HL_LEN = 10.0f, HL_R = 1.5f;
            glUniform3f(U.emissiveColor, 0.80f, 0.88f, 1.00f);
            glUniform1f(U.beam, 0.035f * (1.0f - 0.7f * dusk));
            auto headBeams = [&](const glm::mat4& T, float frontX, float height, float side) {
                for (int lr = -1; lr <= 1; lr += 2) {
                    glm::mat4 m = T;
                    m = glm::translate(m, glm::vec3(frontX, height, side * lr));
                    m = glm::rotate(m, glm::radians(87.0f), glm::vec3(0, 0, 1));
                    m = glm::translate(m, glm::vec3(0.0f, -HL_LEN, 0.0f));
                    m = glm::scale(m, glm::vec3(HL_R * 2.0f, HL_LEN, HL_R * 2.0f));
                    setModel(m);
                    drawMesh(cone);
                }
            };
            for (size_t i = 0; i < city.vehicles.size(); ++i) {
                glm::vec3 p(carFrame[i][3]);
                glm::vec2 d(p.x - g_cam.position.x, p.z - g_cam.position.z);
                if (glm::dot(d, d) < 110.0f * 110.0f)
                    headBeams(carFrame[i], CAR_LENGTH * 0.5f + 0.05f, 0.40f, 0.55f);
            }
            if (player.exists) {
                glm::mat4 T(1.0f);
                T = glm::translate(T, player.pos);
                T = glm::rotate(T, player.heading, glm::vec3(0, 1, 0));
                headBeams(T, CAR_LENGTH * 0.5f + 0.05f, 0.40f, 0.55f);
            }

            glUniform1f(U.beam, 0.0f);
            glEnable(GL_CULL_FACE);
            glDepthMask(GL_TRUE);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            glDisable(GL_BLEND);
        }

        // ---- optional light markers ----------------------------------------------
        if (showLights) {
            for (const LightSource& L : g_lights) {
                glm::mat4 m(1.0f);
                m = glm::translate(m, L.position);
                m = glm::scale(m, glm::vec3(0.45f));
                drawEmissive(cube, m, glm::normalize(L.color), 3.0f);
            }
        }

        // ===================================================================
        //  6. BLOOM, THEN COMBINE AND SHOW
        // ===================================================================
        // Wireframe must not apply to the fullscreen passes, or the final
        // image would be a couple of lines instead of a picture.
        if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_DEPTH_TEST);     // post-processing is 2D, depth is irrelevant

        GLuint bloomResult = 0;
        if (bloomOn) {
            // 8 passes (4 horizontal + 4 vertical), samples spaced 1.5 texels
            // apart, at half resolution: a wide soft glow at low cost.
            bloomResult = runBloomBlur(bloomFBO, blurProg, fsQuad, sceneFBO.brightTex, 8, 1.5f);
        }
        GLuint streakResult = 0;
        if (bloomOn && fxOn)
            streakResult = runStreakBlur(streakFBO, blurProg, fsQuad, sceneFBO.brightTex, 1.6f, 2);   // 2 passes = short flares

        // Back to the real screen, at the WINDOW's size: drawing the scene image
        // across all of it is what stretches it up when renderScale < 1.
        glBindFramebuffer(GL_FRAMEBUFFER, g_shotHD ? hdFbo : 0);
        glViewport(0, 0, g_width, g_height);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(compositeProg);
        glUniform1i(cmpScene, 0);
        glUniform1i(cmpBloom, 1);
        glUniform1f(cmpStrength, bloomOn ? 1.0f : 0.0f);
        glUniform1f(cmpExposure, glm::mix(1.1f, 0.9f, dusk));    // the dusk scene is brighter
        glUniform1f(cmpSaturation, 1.10f);   // slightly richer than plain
        glUniform1i(cmpStreakTex, 2);
        glUniform1f(cmpStreak,     streakResult ? 0.25f : 0.0f);
        glUniform1f(cmpAberration, fxOn ? 0.008f : 0.0f);
        glUniform1f(cmpGrain,      fxOn ? 0.022f : 0.0f);
        glUniform1f(cmpTime,       sceneTime);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneFBO.colorTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloomOn ? bloomResult : g_whiteTex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, streakResult ? streakResult : g_whiteTex);
        glActiveTexture(GL_TEXTURE0);

        drawFullscreenQuad(fsQuad);

        // The pause menu, drawn see-through on top of the frozen frame.
        if (g_menuOpen) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glUseProgram(menuProg);
            glUniform1i(glGetUniformLocation(menuProg, "image"), 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, pauseMenu.tex[menuHover]);
            drawFullscreenQuad(fsQuad);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            glDisable(GL_BLEND);
        }

        glEnable(GL_DEPTH_TEST);
        if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        // ---- screenshots ---------------------------------------------------
        // Read back BEFORE swapping, while the finished frame is still in the
        // back buffer.
        frameNumber++;
        bool f12 = glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS;
        if (f12 && !kF12) {
            char name[64];
            snprintf(name, sizeof(name), "screenshot_%02d.bmp", ++shotCount);
            if (saveScreenshot(name, g_width, g_height))
                std::cout << "\nSaved " << name << "\n";
        }
        kF12 = f12;

        // --shot "rec" mode: every second frame (the fixed step is 1/60 s,
        // so 30 frames per second of scene time) is read back and piped into
        // ffmpeg, which encodes the MP4. The frames come out bottom row first,
        // so ffmpeg flips them (vflip).
        if (shotPath && g_shotRecord) {
            static FILE* rec = nullptr;
            static std::vector<unsigned char> frame;
            if (!rec) {
                char cmd[1024];
                snprintf(cmd, sizeof(cmd),
                         "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s %dx%d -r 30 -i - "
                         "-vf vflip -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p \"%s\"",
                         g_width, g_height, shotPath);
                rec = popen(cmd, "wb");
                frame.resize((size_t)g_width * g_height * 3);
                if (!rec) { std::cout << "Could not start ffmpeg\n"; glfwSetWindowShouldClose(window, true); }
            }
            if (rec && frameNumber % 2 == 0) {
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                if (g_shotHD) { glBindFramebuffer(GL_READ_FRAMEBUFFER, hdFbo); glReadBuffer(GL_COLOR_ATTACHMENT0); }
                else          glReadBuffer(GL_BACK);
                glReadPixels(0, 0, g_width, g_height, GL_RGB, GL_UNSIGNED_BYTE, frame.data());
                fwrite(frame.data(), 1, frame.size(), rec);
            }
            if (frameNumber >= shotFrame) {
                if (rec) pclose(rec);
                rec = nullptr;
                std::cout << "\nRecorded " << shotPath << "\n";
                glfwSetWindowShouldClose(window, true);
            }
        }
        // --shot mode: let the scene run for a moment so the traffic lights,
        // rain and cars are mid-motion, then save and quit.
        else if (shotPath && frameNumber == shotFrame) {
            if (saveScreenshot(shotPath, g_width, g_height))
                std::cout << "\nSaved " << shotPath << "\n";
            glfwSetWindowShouldClose(window, true);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ---- cleanup -------------------------------------------------------------
    freeMesh(cube);    freeMesh(cylinder); freeMesh(pyramid);
    freeMesh(quad);    freeMesh(panel);    freeMesh(skybox);
    freeMesh(cone);
    freeMesh(octagon); freeMesh(carBody);  freeMesh(carCabin);
    freeMesh(tube);
    glDeleteVertexArrays(1, &rainVAO);
    glDeleteBuffers(1, &rainVBO);

    glDeleteTextures(NUM_WINDOW_TEX, windowTex);
    glDeleteTextures(NUM_BILLBOARD_TEX, billboardTex);
    glDeleteTextures(1, &asphaltTex);
    glDeleteTextures(1, &tileTex);
    glDeleteTextures(1, &stoneTex);
    for (ImageTexture& img : billboardImages) glDeleteTextures(1, &img.id);
    glDeleteTextures(1, &g_whiteTex);

    destroySceneFBO(sceneFBO);
    destroyBloomFBO(bloomFBO);
    destroyBloomFBO(streakFBO);

    glDeleteProgram(sceneProg);
    glDeleteProgram(skyProg);
    glDeleteProgram(blurProg);
    glDeleteProgram(compositeProg);
    glDeleteProgram(menuProg);
    destroyPauseMenu(pauseMenu);

    destroyRayTraceScene(rtScene);
    musicShutdown(music);
    glfwTerminate();
    std::cout << "\nClosed cleanly.\n";
    return 0;
}
