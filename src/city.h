// ===========================================================================
//  city.h  -  everything the city is MADE of, decided once at startup
//
//  This file draws nothing. It fills lists of plain data - "a tower of this
//  shape and size, here, with neon strips on these faces" - and the renderer
//  reads those lists every frame.
//
//  That split is why the whole city can be regenerated from a new seed (press
//  N) without touching a single line of drawing code.
// ===========================================================================

#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
//  REPRODUCIBLE RANDOMNESS
//
//  A Linear Congruential Generator:   state = state * A + C   (mod 2^32)
//
//  rand() is deliberately NOT used: it differs between compilers, so the
//  same seed would build a different city on a different machine. This one
//  is identical everywhere, which is what the proposal actually promises.
// ---------------------------------------------------------------------------
struct Random {
    unsigned int state;
    Random(unsigned int seed) : state(seed ? seed : 1u) {}

    unsigned int next() {
        state = state * 1664525u + 1013904223u;
        return state;
    }
    // >> 8 keeps the HIGH bits. In an LCG the low bits repeat with a very
    // short period and would produce visible patterns.
    float nextFloat() { return (float)(next() >> 8) / (float)(1 << 24); }
    float range(float lo, float hi) { return lo + nextFloat() * (hi - lo); }
};

// ---------------------------------------------------------------------------
//  CITY MEASUREMENTS
//
//  CELL is the repeating tile: one block plus the road beside it. Lay GRID
//  of them in each direction and the whole city follows.
// ---------------------------------------------------------------------------
const int   GRID      = 4;                    // 4 x 4 = 16 blocks
const float BLOCK     = 22.0f;                // a block is 22 x 22
const float ROAD      = 8.0f;                 // roads are 8 wide
const float CELL      = BLOCK + ROAD;         // = 30
const float CITY_SPAN = GRID * CELL;          // = 120
const float HALF      = CITY_SPAN * 0.5f;     // = 60, so the city straddles 0

// Each block sits on a raised pavement slab, and the ROAD is simply the wet
// ground plane left showing between the slabs. The slab edge is the kerb.
const float SLAB_H    = 0.15f;

// ---------------------------------------------------------------------------
//  THE ISLAND
//
//  The city stands on an island. Its land reaches ISLAND_EDGE from the
//  centre in every direction - 12 units past the outermost roads, leaving a
//  waterfront promenade - and ends at a stone sea wall. Beyond it is water,
//  whose surface lies WATER_Y below the streets.
//
//  Every road ends at the waterfront in a TUNNEL mouth: the tunnels run under
//  the water to other districts. This is where cars leave the city and where
//  they arrive, so traffic never has to drive off the edge into the sea.
//
//        centre            outer road     tunnel             sea wall
//          0 ....... 60 ...... 64 ..... 65 ======= 71.5 ... 72 | water
//                    road centre   road edge  mouth    back
// ---------------------------------------------------------------------------
const float WATER_Y      = -2.2f;
const float ISLAND_EDGE  = HALF + 12.0f;    // = 72
const float TUNNEL_START = HALF + 5.0f;     // = 65, the tunnel mouth
const float TUNNEL_END   = HALF + 11.5f;    // = 71.5, its back wall

// Size of one window on a facade, in world units. The window texture holds a
// 6 x 8 grid, so one copy of it covers 8.4 x 14.4 units of wall.
const float WINDOW_W  = 1.4f;
const float WINDOW_H  = 1.8f;

// ---------------------------------------------------------------------------
//  BUILDING PARTS
//
//  A building is no longer one box. It is a short list of PARTS - the base,
//  each tier, the ledges between tiers, the air-conditioning units on the
//  roof - and each part is one of the primitive meshes, placed and scaled.
//
//  Every part obeys the unit-shape rule: 'base' is the centre of its
//  footprint at its lowest point, and 'size' is (width, height, depth).
// ---------------------------------------------------------------------------
enum PartShape { PART_BOX, PART_CYLINDER, PART_OCTAGON };

struct BuildingPart {
    PartShape shape;
    glm::vec3 base;       // bottom-centre, in world space
    glm::vec3 size;       // width, height, depth
    glm::vec3 color;
    bool      windows;    // true = window-grid facade, false = plain surface
};

// The five kinds of tower. Mixing them is what stops the skyline reading as
// a field of identical boxes.
enum BuildingStyle { STYLE_SLAB, STYLE_TIERED, STYLE_PODIUM, STYLE_ROUND, STYLE_OCTAGON };

struct Building {
    BuildingStyle style;
    glm::vec3 position;           // centre of the lot, on the ground
    glm::vec3 color;              // concrete colour
    glm::vec3 neonColor;          // accent colour for this tower's trim
    int       windowTex;          // which window-grid texture
    std::vector<BuildingPart> parts;

    // The SHAFT is the main tall facade that neon strips and billboards are
    // stuck onto. For a tiered tower it is the bottom tier; for a podium
    // tower it is the slim tower above the podium. Decorations are placed
    // relative to this, so they always sit flush on a real wall.
    glm::vec3 shaftBase;
    float     shaftW, shaftD, shaftH;
    bool      shaftRound;         // cylinder or octagon: no flat faces

    // The very top, where spires and antennas go.
    glm::vec3 roofCentre;
    float     roofW, roofD;
    bool      hasSpire;
    bool      hasAntenna;
};

// ---------------------------------------------------------------------------
//  DECORATIONS
// ---------------------------------------------------------------------------

// A neon tube. Three forms:
//   VERTICAL - a line running up one face (the signature look in the
//              reference images)
//   BAND     - a line wrapping all four faces of a flat-sided shaft
//   RING     - a glowing ring round a round or octagonal shaft
enum NeonKind { NEON_VERTICAL, NEON_BAND, NEON_RING };

struct NeonStrip {
    NeonKind  kind;
    glm::vec3 position;      // bottom end (vertical) or centre (band, ring)
    glm::vec3 color;
    float     height;        // vertical only
    float     yaw;           // vertical only: which face it is on
    glm::vec2 bandSize;      // band/ring: the shaft's width and depth
    float     flickerSeed;   // phase offset, so signs do not flicker in unison
    bool      flickers;
};

// A glowing screen stuck on a facade.
struct Billboard {
    glm::vec3 position;
    float     width, height;
    float     yaw;
    int       texIndex;
    float     scrollSeed;
};

// A LARGE rooftop screen, standing on legs above a roof - the Times Square /
// Blade Runner kind. It shows real picture files (see images.h), so it only
// stores where it stands and which way it faces; its height comes from the
// picture's own proportions when it is drawn.
struct BigBillboard {
    glm::vec3 roof;          // centre of the roof it stands on
    float     width;         // screen width; height = width / picture aspect
    float     yaw;           // which way the screen faces (0 = +Z, 90 = +X)
    float     legSpread;     // legs stand this far either side of centre
    int       imageOffset;   // so two screens do not show the same picture
};

// A DISTANT DISTRICT across the water: its own small island - raised land
// with a sea wall and a glowing waterline, like the main one - carrying a
// little grid of towers. Only scenery, far enough away that fog softens it.
//
// Each district is built in its own LOCAL frame and then turned by 'yaw' so
// its front (local +Z) faces the main city: the tallest towers stand at the
// back, the lowest at the front, so from the city it reads as a layered
// skyline rather than a random cluster.
struct FarDistrict {
    glm::vec3 centre;        // centre of its land, at street level
    float     yaw;           // degrees; turns local +Z toward the main city
    float     halfW, halfD;  // half-size of the land: across (x) and deep (z)
};

// One tower in a district, placed in that district's LOCAL frame.
struct FarTower {
    int       district;
    glm::vec3 local;         // bottom-centre, relative to the district
    glm::vec3 size;
    glm::vec3 color;
    glm::vec3 neonColor;
    int       windowTex;
    bool      hasNeon;
};

struct StreetLamp {
    glm::vec3 position;
    float     facing;        // degrees; outward = (cos, 0, sin)
};

struct TrafficLight {
    glm::vec3 position;
    float     phaseOffset;   // staggers the red/amber/green cycle
};

// A car. Only two numbers describe where it is: which road (lane) and how
// far along it (t). How it moves is decided in traffic.h.
struct Vehicle {
    glm::vec3 color;
    glm::vec3 rimColor;      // glowing wheel rims
    int   axis;              // 0 = drives along X, 1 = along Z
    float lane;              // fixed coordinate on the other axis
    float t;                 // distance travelled
    float speed;             // the speed it LIKES to cruise at
    float v;                 // the speed it is ACTUALLY doing right now -
                             // lower when following, zero at a red light
    float dir;               // +1 or -1

    // TURNING (see traffic.h). While 'turning', the car leaves its lane and
    // follows a quarter circle through the junction onto the crossing road.
    bool      turning     = false;
    float     turnAngle   = 0.0f;                  // 0 -> 90 degrees, in radians
    glm::vec3 turnCentre  = glm::vec3(0.0f);       // centre of that quarter circle
    glm::vec3 turnFwd     = glm::vec3(1, 0, 0);    // heading before the turn
    glm::vec3 turnRight   = glm::vec3(0, 0, 1);    // heading after it (its right)
    int       turnK       = -1;                    // which junction, along the old road
    int       plannedK    = -99;                   // junction the current decision is for
    bool      plannedTurn = false;                 // will it turn there?
    unsigned  rng         = 1u;                    // its own dice for turn decisions
};

struct RainDrop {
    glm::vec3 pos;
    float     speed;
    float     length;
};

struct City {
    std::vector<Building>     buildings;
    std::vector<NeonStrip>    neons;
    std::vector<Billboard>    billboards;
    std::vector<BigBillboard> bigBoards;
    std::vector<StreetLamp>   lamps;
    std::vector<TrafficLight> trafficLights;
    std::vector<Vehicle>      vehicles;
    std::vector<RainDrop>     rain;
    std::vector<glm::vec3>    blockCentres;   // for the pavement slabs
    std::vector<FarDistrict>  farDistricts;   // the islands across the water
    std::vector<FarTower>     farTowers;      // ...and the towers on them
};

// ---------------------------------------------------------------------------
//  The neon palette, from the reference images: saturated cyan, magenta,
//  violet and amber against near-black concrete. The BUILDINGS stay dark on
//  purpose - neon only looks bright next to something dark.
// ---------------------------------------------------------------------------
inline glm::vec3 neonPalette(int i) {
    static const glm::vec3 p[6] = {
        glm::vec3(0.10f, 1.00f, 1.00f),   // cyan
        glm::vec3(1.00f, 0.12f, 0.62f),   // magenta
        glm::vec3(0.55f, 0.20f, 1.00f),   // violet
        glm::vec3(1.00f, 0.65f, 0.10f),   // amber
        glm::vec3(0.20f, 1.00f, 0.55f),   // green
        glm::vec3(1.00f, 0.25f, 0.25f),   // red
    };
    return p[i % 6];
}

// ---------------------------------------------------------------------------
//  BUILDING SHAPES
//
//  Each function below adds the parts for one style of tower. They all take
//  the same lot: a footprint w x d, a total height h, centred at 'pos'.
// ---------------------------------------------------------------------------

// Plain tower - one box.
inline void buildSlab(Building& b, Random&, float w, float h, float d) {
    b.parts.push_back({ PART_BOX, b.position, glm::vec3(w, h, d), b.color, true });
    b.shaftBase = b.position; b.shaftW = w; b.shaftD = d; b.shaftH = h;
    b.shaftRound = false;
    b.roofCentre = b.position + glm::vec3(0.0f, h, 0.0f);
    b.roofW = w; b.roofD = d;
}

// Tiered tower - three boxes, each narrower than the one below, stacked.
//
//   The heights split 50% / 30% / 20% and the footprints shrink to 78% and
//   then 56%. Each tier's base sits exactly on the top of the one beneath it,
//   which is the unit-shape rule doing the work: translate up by the height
//   below, and the part lands on the roof.
//
//   A slightly wider, thin slab - a LEDGE - is added at each step. It is a
//   tiny detail but it is what makes the setback read as architecture.
inline void buildTiered(Building& b, Random&, float w, float h, float d) {
    const float heightShare[3] = { 0.50f, 0.30f, 0.20f };
    const float widthShare[3]  = { 1.00f, 0.78f, 0.56f };

    float y = 0.0f;
    for (int t = 0; t < 3; ++t) {
        float tw = w * widthShare[t];
        float td = d * widthShare[t];
        float th = h * heightShare[t];

        glm::vec3 base = b.position + glm::vec3(0.0f, y, 0.0f);
        b.parts.push_back({ PART_BOX, base, glm::vec3(tw, th, td), b.color, true });

        // ledge on top of this tier (not on the very top one)
        if (t < 2) {
            b.parts.push_back({ PART_BOX, base + glm::vec3(0.0f, th, 0.0f),
                                glm::vec3(tw + 0.5f, 0.35f, td + 0.5f),
                                b.color * 1.6f, false });
        }
        y += th;
    }
    b.shaftBase = b.position; b.shaftW = w; b.shaftD = d; b.shaftH = h * heightShare[0];
    b.shaftRound = false;
    b.roofCentre = b.position + glm::vec3(0.0f, h, 0.0f);
    b.roofW = w * widthShare[2]; b.roofD = d * widthShare[2];
}

// Podium tower - a wide, low base with a slimmer tower rising out of it.
inline void buildPodium(Building& b, Random& rng, float w, float h, float d) {
    float podiumH = rng.range(5.0f, 8.0f);
    float tw = w * 0.64f, td = d * 0.64f;

    b.parts.push_back({ PART_BOX, b.position, glm::vec3(w, podiumH, d), b.color * 0.8f, true });

    glm::vec3 towerBase = b.position + glm::vec3(0.0f, podiumH, 0.0f);
    b.parts.push_back({ PART_BOX, towerBase, glm::vec3(tw, h - podiumH, td), b.color, true });

    // a canopy edge round the podium roof
    b.parts.push_back({ PART_BOX, towerBase, glm::vec3(w + 0.3f, 0.3f, d + 0.3f),
                        b.color * 1.6f, false });

    b.shaftBase = towerBase; b.shaftW = tw; b.shaftD = td; b.shaftH = h - podiumH;
    b.shaftRound = false;
    b.roofCentre = b.position + glm::vec3(0.0f, h, 0.0f);
    b.roofW = tw; b.roofD = td;
}

// Round tower - a cylinder, sized to fit inside the lot.
inline void buildRound(Building& b, Random&, float w, float h, float d) {
    float dia = fminf(w, d);
    b.parts.push_back({ PART_CYLINDER, b.position, glm::vec3(dia, h, dia), b.color, true });
    // a plain drum on top, like a plant room
    b.parts.push_back({ PART_CYLINDER, b.position + glm::vec3(0.0f, h, 0.0f),
                        glm::vec3(dia * 0.6f, 1.6f, dia * 0.6f), b.color * 1.3f, false });

    b.shaftBase = b.position; b.shaftW = dia; b.shaftD = dia; b.shaftH = h;
    b.shaftRound = true;
    b.roofCentre = b.position + glm::vec3(0.0f, h + 1.6f, 0.0f);
    b.roofW = dia * 0.6f; b.roofD = dia * 0.6f;
}

// Octagonal tower - a box with its corners cut off (see makeExtrusion).
inline void buildOctagon(Building& b, Random&, float w, float h, float d) {
    b.parts.push_back({ PART_OCTAGON, b.position, glm::vec3(w, h, d), b.color, true });
    // a narrower octagonal crown
    b.parts.push_back({ PART_OCTAGON, b.position + glm::vec3(0.0f, h, 0.0f),
                        glm::vec3(w * 0.7f, 2.2f, d * 0.7f), b.color * 1.3f, true });

    b.shaftBase = b.position; b.shaftW = w; b.shaftD = d; b.shaftH = h;
    b.shaftRound = true;
    b.roofCentre = b.position + glm::vec3(0.0f, h + 2.2f, 0.0f);
    b.roofW = w * 0.7f; b.roofD = d * 0.7f;
}

// Rooftop clutter: air-conditioning boxes and a water tank. Small, plain,
// unlit - but a flat empty roof is the quickest giveaway of a model city.
inline void addRooftop(Building& b, Random& rng) {
    int units = 1 + (int)(rng.next() % 3);
    for (int i = 0; i < units; ++i) {
        float ox = rng.range(-0.3f, 0.3f) * b.roofW;
        float oz = rng.range(-0.3f, 0.3f) * b.roofD;
        glm::vec3 s(rng.range(0.9f, 1.8f), rng.range(0.6f, 1.2f), rng.range(0.9f, 1.8f));
        b.parts.push_back({ PART_BOX, b.roofCentre + glm::vec3(ox, 0.0f, oz), s,
                            glm::vec3(0.10f, 0.10f, 0.12f), false });
    }
    if (rng.nextFloat() < 0.4f) {
        float ox = rng.range(-0.25f, 0.25f) * b.roofW;
        float oz = rng.range(-0.25f, 0.25f) * b.roofD;
        b.parts.push_back({ PART_CYLINDER, b.roofCentre + glm::vec3(ox, 0.0f, oz),
                            glm::vec3(1.4f, 1.8f, 1.4f), glm::vec3(0.12f, 0.09f, 0.08f), false });
    }
}

// ---------------------------------------------------------------------------
//  BUILD THE WHOLE CITY
// ---------------------------------------------------------------------------
inline void generateCity(City& city, unsigned int seed, int numWindowTex, int numBillboardTex) {
    city.buildings.clear();
    city.neons.clear();
    city.billboards.clear();
    city.bigBoards.clear();
    city.lamps.clear();
    city.trafficLights.clear();
    city.blockCentres.clear();

    Random rng(seed);

    // Facade MATERIALS. Still all dark (it is night, and the neon must stand
    // out), but no longer one family of blue-black concrete: each tower is
    // built of one of these, so neighbours differ in hue as well as shape.
    // All have about the same brightness, so none looks lit up.
    const int NUM_MATERIALS = 9;
    const glm::vec3 concrete[NUM_MATERIALS] = {
        glm::vec3(0.040f, 0.042f, 0.060f),   // blue-violet concrete (the original)
        glm::vec3(0.036f, 0.050f, 0.058f),   // cool grey concrete
        glm::vec3(0.064f, 0.042f, 0.032f),   // brown brick
        glm::vec3(0.020f, 0.052f, 0.060f),   // dark teal glass
        glm::vec3(0.062f, 0.028f, 0.038f),   // wine-red cladding
        glm::vec3(0.066f, 0.060f, 0.050f),   // weathered sandstone
        glm::vec3(0.024f, 0.032f, 0.074f),   // navy glass
        glm::vec3(0.034f, 0.034f, 0.036f),   // graphite steel
        glm::vec3(0.048f, 0.050f, 0.034f),   // olive-grey panels
    };

    for (int gx = 0; gx < GRID; ++gx) {
        for (int gz = 0; gz < GRID; ++gz) {

            // Block centre. +0.5 lands in the middle of the cell rather than
            // its corner; -HALF shifts the grid so the city straddles 0.
            float cx = -HALF + (gx + 0.5f) * CELL;
            float cz = -HALF + (gz + 0.5f) * CELL;
            city.blockCentres.push_back(glm::vec3(cx, 0.0f, cz));

            // 2x2 towers per block, so the streets read as canyons.
            for (int sx = 0; sx < 2; ++sx) {
                for (int sz = 0; sz < 2; ++sz) {

                    // Leave a fifth of the lots empty - gaps make the skyline
                    // look designed rather than like a checkerboard.
                    if (rng.nextFloat() < 0.20f) continue;

                    float ox = (sx == 0 ? -1.0f : 1.0f) * BLOCK * 0.25f;
                    float oz = (sz == 0 ? -1.0f : 1.0f) * BLOCK * 0.25f;

                    // At most 8.6 wide: with the tower centred 5.5 from the
                    // block centre, that keeps it 1.2 clear of the kerb, which
                    // is where the lamp posts and traffic lights stand.
                    float w = rng.range(6.0f, 8.6f);
                    float d = rng.range(6.0f, 8.6f);
                    // A WIDE height range is what gives the jagged skyline.
                    float h = rng.range(10.0f, 48.0f);

                    Building b;
                    b.position  = glm::vec3(cx + ox, 0.0f, cz + oz);
                    b.color     = concrete[rng.next() % NUM_MATERIALS];
                    b.neonColor = neonPalette(rng.next() % 6);
                    b.windowTex = (int)(rng.next() % (unsigned)numWindowTex);

                    // ---- choose a style --------------------------------------
                    // Tiered and podium towers only make sense when tall.
                    float roll = rng.nextFloat();
                    if      (roll < 0.26f && h > 22.0f) b.style = STYLE_TIERED;
                    else if (roll < 0.48f && h > 18.0f) b.style = STYLE_PODIUM;
                    else if (roll < 0.62f)              b.style = STYLE_ROUND;
                    else if (roll < 0.78f)              b.style = STYLE_OCTAGON;
                    else                                b.style = STYLE_SLAB;

                    switch (b.style) {
                        case STYLE_TIERED:  buildTiered (b, rng, w, h, d); break;
                        case STYLE_PODIUM:  buildPodium (b, rng, w, h, d); break;
                        case STYLE_ROUND:   buildRound  (b, rng, w, h, d); break;
                        case STYLE_OCTAGON: buildOctagon(b, rng, w, h, d); break;
                        default:            buildSlab   (b, rng, w, h, d); break;
                    }

                    b.hasSpire   = (h > 34.0f) && !b.shaftRound && rng.nextFloat() < 0.5f;
                    b.hasAntenna = (h > 38.0f) && !b.hasSpire;
                    if (!b.hasSpire) addRooftop(b, rng);

                    // ---- vertical neon strips on the shaft -----------------
                    int stripCount = 1 + (int)(rng.next() % 3);
                    for (int s = 0; s < stripCount; ++s) {
                        NeonStrip n;
                        n.kind = NEON_VERTICAL;

                        // Pick one face. For a round shaft the strip touches
                        // the curve at the middle of that side.
                        int face = (int)(rng.next() % 4);
                        float fx = 0.0f, fz = 0.0f, yaw = 0.0f;
                        float gap = b.shaftRound ? 0.02f : 0.06f;
                        switch (face) {
                            case 0: fz =  b.shaftD * 0.5f + gap; yaw =   0.0f; break;
                            case 1: fz = -b.shaftD * 0.5f - gap; yaw = 180.0f; break;
                            case 2: fx =  b.shaftW * 0.5f + gap; yaw =  90.0f; break;
                            default:fx = -b.shaftW * 0.5f - gap; yaw = 270.0f; break;
                        }
                        // Slide it sideways along a flat face so several
                        // strips do not stack. Round faces stay centred.
                        if (!b.shaftRound) {
                            float side = rng.range(-0.32f, 0.32f);
                            if (face < 2) fx = side * b.shaftW; else fz = side * b.shaftD;
                        }

                        float start = rng.range(0.0f, b.shaftH * 0.15f);
                        n.position    = b.shaftBase + glm::vec3(fx, start + 0.2f, fz);
                        n.color       = (rng.nextFloat() < 0.75f) ? b.neonColor
                                                                  : neonPalette(rng.next() % 6);
                        // Never taller than the shaft, so it never runs off
                        // the top of a tier into thin air.
                        n.height      = (b.shaftH - start) * rng.range(0.55f, 0.92f);
                        n.yaw         = yaw;
                        n.bandSize    = glm::vec2(0.0f);
                        n.flickers    = rng.nextFloat() < 0.22f;
                        n.flickerSeed = rng.range(0.0f, 100.0f);
                        city.neons.push_back(n);
                    }

                    // ---- bands / rings round the shaft ---------------------
                    if (rng.nextFloat() < 0.5f) {
                        int bands = 1 + (int)(rng.next() % 3);
                        for (int k = 0; k < bands; ++k) {
                            NeonStrip n;
                            n.kind        = b.shaftRound ? NEON_RING : NEON_BAND;
                            n.position    = b.shaftBase + glm::vec3(0.0f,
                                                rng.range(b.shaftH * 0.25f, b.shaftH * 0.94f), 0.0f);
                            n.color       = b.neonColor;
                            n.height      = 0.0f;
                            n.yaw         = 0.0f;
                            n.bandSize    = glm::vec2(b.shaftW, b.shaftD);
                            // A ring must clear the shaft's widest point. For
                            // a cylinder that is its diameter; an octagon's
                            // corners reach sqrt(0.5^2 + 0.3^2) = 0.583 of its
                            // width from the centre, so its ring is ~1.17x.
                            if (n.kind == NEON_RING) {
                                float grow = (b.style == STYLE_ROUND) ? 1.05f : 1.19f;
                                n.bandSize *= grow;
                            }
                            n.flickers    = rng.nextFloat() < 0.12f;
                            n.flickerSeed = rng.range(0.0f, 100.0f);
                            city.neons.push_back(n);
                        }
                    }

                    // ---- a big glowing screen, on flat-faced shafts --------
                    if (!b.shaftRound && b.shaftH > 14.0f && rng.nextFloat() < 0.5f) {
                        Billboard bb;
                        int face = (int)(rng.next() % 4);
                        float bw = 0.0f, yaw = 0.0f;
                        glm::vec3 off(0.0f);
                        switch (face) {
                            case 0: off.z =  b.shaftD * 0.5f + 0.10f; yaw =   0.0f; bw = b.shaftW * 0.8f; break;
                            case 1: off.z = -b.shaftD * 0.5f - 0.10f; yaw = 180.0f; bw = b.shaftW * 0.8f; break;
                            case 2: off.x =  b.shaftW * 0.5f + 0.10f; yaw =  90.0f; bw = b.shaftD * 0.8f; break;
                            default:off.x = -b.shaftW * 0.5f - 0.10f; yaw = 270.0f; bw = b.shaftD * 0.8f; break;
                        }
                        bb.height     = fminf(rng.range(4.5f, 9.0f), b.shaftH * 0.4f);
                        bb.width      = bw;
                        bb.yaw        = yaw;
                        bb.position   = b.shaftBase + off + glm::vec3(0.0f,
                                            rng.range(b.shaftH * 0.30f, b.shaftH * 0.55f), 0.0f);
                        bb.texIndex   = (int)(rng.next() % (unsigned)numBillboardTex);
                        bb.scrollSeed = rng.range(0.0f, 10.0f);
                        city.billboards.push_back(bb);
                    }

                    city.buildings.push_back(b);
                }
            }

            // ---- street lamps, one on each kerb ---------------------------
            //
            // The block's pavement slab ends at BLOCK/2 from its centre - that
            // edge is the kerb. The post stands KERB units INSIDE it, on the
            // pavement, and the arm then reaches out over the road.
            //
            // 'facing' is a compass angle, turned into a direction by the
            // renderer with outward = (cos, 0, sin):
            //     0 -> +X     90 -> +Z     180 -> -X     270 -> -Z
            // A lamp on the -X kerb must reach back toward -X, so 180.
            const float KERB = 0.8f;
            const float EX   = BLOCK * 0.5f - KERB;

            city.lamps.push_back({ glm::vec3(cx - EX, SLAB_H, cz), 180.0f });
            city.lamps.push_back({ glm::vec3(cx + EX, SLAB_H, cz),   0.0f });
            city.lamps.push_back({ glm::vec3(cx, SLAB_H, cz - EX), 270.0f });
            city.lamps.push_back({ glm::vec3(cx, SLAB_H, cz + EX),  90.0f });
        }
    }

    // ---- street lamps along the waterfront promenade ----------------------
    // The ring road has lamps on its INNER side already (from the outer
    // blocks). These stand on the promenade across the road from them, arms
    // reaching back over the ring road, one opposite each block - so the
    // ring road is lit from both sides like the streets inside the city.
    {
        const float OUTER = HALF + ROAD * 0.5f + 0.8f;    // just onto the promenade
        for (int g = 0; g < GRID; ++g) {
            float c = -HALF + (g + 0.5f) * CELL;           // opposite a block centre
            city.lamps.push_back({ glm::vec3( OUTER, SLAB_H, c), 180.0f });   // east side, reach -X
            city.lamps.push_back({ glm::vec3(-OUTER, SLAB_H, c),   0.0f });   // west side, reach +X
            city.lamps.push_back({ glm::vec3(c, SLAB_H,  OUTER), 270.0f });   // north side, reach -Z
            city.lamps.push_back({ glm::vec3(c, SLAB_H, -OUTER),  90.0f });   // south side, reach +Z
        }
    }

    // ---- traffic lights, one per junction ---------------------------------
    // Junctions are where two road centre-lines cross: (-HALF + i*CELL,
    // -HALF + j*CELL). Half a road width plus 0.7 from there, on both axes,
    // lands on the corner of the neighbouring block's pavement.
    const float CORNER = ROAD * 0.5f + 0.7f;
    for (int i = 0; i <= GRID; ++i) {
        for (int j = 0; j <= GRID; ++j) {
            // On the city's far edge there is no block on the + side, so
            // those junctions use the corner on the - side instead.
            float sx = (i < GRID) ? 1.0f : -1.0f;
            float sz = (j < GRID) ? 1.0f : -1.0f;

            TrafficLight t;
            t.position    = glm::vec3(-HALF + i * CELL + CORNER * sx, SLAB_H,
                                      -HALF + j * CELL + CORNER * sz);
            // Stagger the cycles so the whole city does not blink together.
            t.phaseOffset = (float)((i * 7 + j * 3) % 10);
            city.trafficLights.push_back(t);
        }
    }

    // ---- the two big rooftop screens ---------------------------------------
    //
    // No random numbers here - the choice follows fixed rules, so adding the
    // screens does not change a single building:
    //
    //   * FLAT ROOF ONLY: slab or podium towers with no spire or antenna. The
    //     legs must stand ON the roof; on a narrow tiered top they would stand
    //     in mid-air.
    //   * MID HEIGHT (14-34): tall enough to rise over the street, low enough
    //     to be seen from it.
    //   * NEAR A MAIN AVENUE: the two roads through the middle of the city
    //     (x = 0 and z = 0) are the longest views, so candidates are ranked by
    //     how close they stand to one.
    //   * FACING THE CENTRE: the screen turns to face the junction where the
    //     two avenues cross, (0, 0). People on an avenue look ALONG it, so a
    //     screen facing straight across the avenue is only seen edge-on;
    //     facing the centre, it is seen head-on from the middle of the city
    //     and from most of both avenues - like the screens round Times Square.
    //   * DIFFERENT BLOCKS, so the two screens are not side by side.
    //
    // Width 12 keeps the screen clear of the neighbouring tower in the same
    // block: towers there stand 11 apart, so +/- 6 leaves a gap of about 1.
    std::vector<int> cand;
    for (int i = 0; i < (int)city.buildings.size(); ++i) {
        const Building& b = city.buildings[i];
        float h = b.roofCentre.y;
        bool flat = (b.style == STYLE_SLAB || b.style == STYLE_PODIUM);
        if (flat && !b.hasSpire && !b.hasAntenna && h >= 14.0f && h <= 34.0f)
            cand.push_back(i);
    }
    auto avenueDist = [&](int i) {
        const glm::vec3& p = city.buildings[i].position;
        return std::min(fabsf(p.x), fabsf(p.z));
    };
    std::sort(cand.begin(), cand.end(), [&](int a, int b) {
        float da = avenueDist(a), db = avenueDist(b);
        return (da != db) ? da < db : a < b;           // index breaks ties
    });

    for (int i : cand) {
        if (city.bigBoards.size() >= 2) break;
        const Building& b = city.buildings[i];

        bool farEnough = true;                          // not in an already-used block
        for (const BigBillboard& o : city.bigBoards)
            if (glm::length(glm::vec2(o.roof.x - b.position.x, o.roof.z - b.position.z)) < 20.0f)
                farEnough = false;
        if (!farEnough) continue;

        BigBillboard bb;
        bb.roof  = b.roofCentre;
        bb.width = 12.0f;
        // Face the centre (0, 0). The screen panel faces +Z before turning,
        // and rotating by an angle a about Y sends +Z to (sin a, 0, cos a).
        // Setting that equal to the direction to the centre, (-x, 0, -z),
        // gives  a = atan2(-x, -z).
        bb.yaw = glm::degrees(atan2f(-b.position.x, -b.position.z));
        // Keep both legs on the roof.
        bb.legSpread   = std::min(bb.width * 0.32f, std::min(b.roofW, b.roofD) * 0.40f);
        bb.imageOffset = (int)city.bigBoards.size();
        city.bigBoards.push_back(bb);
    }

    // ---- the skyline across the water --------------------------------------
    //
    // Five DISTRICTS spread round the horizon, each a cluster of towers. A
    // cluster reads as "another part of the city"; towers spread evenly all
    // round would read as a fence.
    //
    // This uses its OWN random stream (seed XOR a constant), so adding or
    // changing the skyline never shifts a single building on the island.
    Random skyRng(seed ^ 0x5EA5CA9Eu);
    city.farDistricts.clear();
    city.farTowers.clear();

    // Where the districts are. Four sit at the ENDS OF THE TWO BRIDGES, so
    // the bridges now lead somewhere instead of fading out over open water;
    // two more fill the empty north and west horizons. 'yaw' turns each one's
    // front toward the main city:  0 = faces +Z, 90 = +X, 180 = -Z, -90 = -X.
    // (The bridges, in main.cpp, run along z = -102 and x = 112.)
    const FarDistrict districts[6] = {
        // 185 from the centre: near enough to show through the fog from the
        // city (at 230+ they faded out), far enough to read as "across the water".
        { glm::vec3( 185.0f, 0.0f, -102.0f), -90.0f, 34.0f, 26.0f },   // east end of the south bridge
        { glm::vec3(-185.0f, 0.0f, -102.0f),  90.0f, 34.0f, 26.0f },   // west end of the south bridge
        { glm::vec3( 112.0f, 0.0f,  185.0f), 180.0f, 34.0f, 26.0f },   // north end of the east bridge
        { glm::vec3( 112.0f, 0.0f, -185.0f),   0.0f, 34.0f, 26.0f },   // south end of the east bridge
        { glm::vec3( -90.0f, 0.0f,  185.0f), 180.0f, 34.0f, 26.0f },   // north horizon
        { glm::vec3(-185.0f, 0.0f,  110.0f),  90.0f, 34.0f, 26.0f },   // west horizon
    };

    // Each district's towers stand on a 4 x 3 grid of lots, 15 apart, with
    // streets between them. The row nearest the city (local z = +14) is the
    // lowest, the back row the tallest.
    const float lotX[4] = { -22.5f, -7.5f, 7.5f, 22.5f };
    const float lotZ[3] = { -14.0f, 0.0f, 14.0f };             // back, middle, front
    const float minH[3] = { 35.0f, 22.0f, 12.0f };
    const float maxH[3] = { 75.0f, 50.0f, 30.0f };

    for (int dIdx = 0; dIdx < 6; ++dIdx) {
        city.farDistricts.push_back(districts[dIdx]);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 4; ++col) {
                if (skyRng.nextFloat() < 0.18f) continue;      // a few empty lots
                FarTower ft;
                ft.district  = dIdx;
                ft.size      = glm::vec3(skyRng.range(7.0f, 11.0f),
                                         skyRng.range(minH[row], maxH[row]),
                                         skyRng.range(7.0f, 11.0f));
                // standing ON the land, whose top is 0.3 above street level
                ft.local     = glm::vec3(lotX[col], 0.3f, lotZ[row]);
                ft.color     = glm::vec3(0.030f, 0.032f, 0.048f);
                ft.neonColor = neonPalette(skyRng.next() % 6);
                ft.windowTex = (int)(skyRng.next() % (unsigned)numWindowTex);
                ft.hasNeon   = skyRng.nextFloat() < 0.45f;
                city.farTowers.push_back(ft);
            }
        }
    }
}

// (The cars are spawned in traffic.h, next to the rules they drive by.)

// ---------------------------------------------------------------------------
//  RAIN
//
//  Each drop is an independent falling streak. When one hits the ground it is
//  lifted back to the top rather than destroyed, so the count never changes
//  and no memory is allocated while the program runs.
//
//  These starting positions only need to be random. Every frame the renderer
//  folds the drops into a box around the camera (see "THE RAIN FOLLOWS THE
//  CAMERA" in main.cpp), so wherever the viewer goes the rain is dense.
// ---------------------------------------------------------------------------
inline void spawnRain(City& city, unsigned int seed, int count = 900) {
    city.rain.clear();
    Random rng(seed ^ 0x51ED2701u);

    for (int i = 0; i < count; ++i) {
        RainDrop d;
        d.pos    = glm::vec3(rng.range(-HALF - 12.0f, HALF + 12.0f),
                             rng.range(0.0f, 70.0f),
                             rng.range(-HALF - 12.0f, HALF + 12.0f));
        d.speed  = rng.range(28.0f, 46.0f);
        d.length = rng.range(0.5f, 1.3f);
        city.rain.push_back(d);
    }
}

// ---------------------------------------------------------------------------
//  THE FLYING VEHICLE
//
//  The purest example of the proposal's single-clock rule: it stores no
//  state at all. Position is a pure function of time, so pausing the clock
//  parks it with no extra code.
// ---------------------------------------------------------------------------
//  Vehicle 0 circles the city at about 56 units. Vehicle 1 flies the other
//  way round a stretched ELLIPSE (x and z radii differ), a little higher so
//  the two never meet, and bobs on a different rhythm.
inline glm::vec3 flyerPosition(float time, int which = 0) {
    if (which == 1) {
        float angle = -time * 0.22f + 1.7f;
        return glm::vec3(HALF * 0.80f * cosf(angle),
                         64.0f + 3.5f * sinf(time * 0.9f + 1.0f),
                         HALF * 0.40f * sinf(angle));
    }
    float radius = HALF * 0.62f;
    float angle  = time * 0.28f;
    return glm::vec3(radius * cosf(angle),
                     56.0f + 5.0f * sinf(time * 0.7f),
                     radius * sinf(angle));
}
