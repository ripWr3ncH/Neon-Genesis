// ===========================================================================
//  traffic.h  -  how the cars behave, and the car you can drive
//
//  No drawing happens here - only positions and speeds. That is on purpose:
//  it means the traffic rules can be tested on their own, without a window,
//  by simulating minutes of traffic and checking no two cars ever overlap.
//
//  THE RULES EVERY AI CAR FOLLOWS, each frame:
//
//    1. KEEP RIGHT     A car's lane is decided by its direction, so cars
//                      going opposite ways never share a lane.
//    2. FOLLOW         Find the car in the corridor straight ahead; slow down
//                      to keep a safe gap behind it.
//    2b. TURN RIGHT    Where a road ends at the ring road a car MUST turn;
//                      at other junctions it turns 1 time in 4. Only RIGHT
//                      turns: with right-hand traffic they never cross the
//                      oncoming lane, so no new conflicts appear.
//    3. OBEY LIGHTS    Stop at the stop line on red. On amber, stop only if
//                      there is room to stop - otherwise carry on through.
//    4. JUNCTION BOX   Never enter a junction while a car on the crossing road
//                      is still inside it.
//    5. ACCELERATE GENTLY, BRAKE HARD - speed changes toward the target speed
//                      at a limited rate, so cars never jump or snap.
//
//  Rules 3 and 4 together are what stop cars on crossing roads from driving
//  through each other; rules 1 and 2 stop cars in the same lane from doing so.
// ===========================================================================

#pragma once

#include "city.h"
#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

// Car size, shared with the renderer so the physics matches what is drawn.
const float CAR_LENGTH = 3.6f;
const float CAR_WIDTH  = 1.7f;

// How far from the centre of the road each lane is.
const float LANE_OFFSET = ROAD * 0.23f;

// ---------------------------------------------------------------------------
//  TRAFFIC LIGHTS
//
//  One 15-second cycle per junction, shared by both roads that cross there:
//
//      0.0 -  5.0   X-road green     Z-road red
//      5.0 -  6.2   X-road amber     Z-road red
//      6.2 -  7.5   ALL RED          (lets the last car clear the junction)
//      7.5 - 12.5   X-road red       Z-road green
//     12.5 - 13.7   X-road red       Z-road amber
//     13.7 - 15.0   ALL RED
//
//  The Z-road is simply the X-road's timetable shifted by half a cycle.
//  Each junction adds its own offset so the city does not switch in unison.
//  Returns 0 = red, 1 = amber, 2 = green.
// ---------------------------------------------------------------------------
const float LIGHT_CYCLE = 15.0f;

inline int signalFor(float time, float offset, int axis) {
    float t = fmodf(time + offset, LIGHT_CYCLE);
    if (axis == 1) t = fmodf(t + LIGHT_CYCLE * 0.5f, LIGHT_CYCLE);
    if (t < 5.0f) return 2;
    if (t < 6.2f) return 1;
    return 0;
}

// Where the lane for a given road and direction is. Driving on the RIGHT:
//   a car heading +X keeps to the +Z side of the road
//   a car heading +Z keeps to the -X side
// (the right-hand side is cross(forward, up) = (-forward.z, 0, forward.x)).
inline float laneFor(int axis, int road, float dir) {
    float centre = -HALF + road * CELL;
    return (axis == 0) ? centre + dir * LANE_OFFSET
                       : centre - dir * LANE_OFFSET;
}

// ---------------------------------------------------------------------------
//  TURNING
//
//  A right turn follows a quarter circle of radius TURN_R that touches both
//  the old lane and the new one. With f = the old heading and rv = its right
//  (the new heading), the circle's centre is placed so that
//
//      position(a) = centre - rv * r * cos(a) + f * r * sin(a)     a: 0 -> 90 deg
//      heading(a)  =          f  * cos(a)     + rv * sin(a)
//
//  At a = 0 the car is still in its old lane heading f; at a = 90 degrees it
//  is in the new lane heading rv. The car moves along the arc at its own
//  speed: the angle grows by speed / r each second (arc length = r * angle).
// ---------------------------------------------------------------------------
const float TURN_R      = 3.5f;
const float TURN_SPEED  = 4.5f;          // nobody takes a corner at full speed
const int   TUNNEL_ROAD = GRID / 2;      // only the central avenues have tunnels

inline int       roadOf(const Vehicle& v)      { return (int)lroundf((v.lane + HALF) / CELL); }
inline glm::vec3 rightOf(const glm::vec3& f)   { return glm::vec3(-f.z, 0.0f, f.x); }
inline glm::vec3 laneForward(const Vehicle& v) {
    return (v.axis == 0) ? glm::vec3(v.dir, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, v.dir);
}

// A car's position in the world: on its lane from (lane, t), or on its arc.
inline glm::vec3 vehiclePos(const Vehicle& v) {
    if (v.turning)
        return v.turnCentre - v.turnRight * (TURN_R * cosf(v.turnAngle))
                            + v.turnFwd   * (TURN_R * sinf(v.turnAngle));
    return (v.axis == 0) ? glm::vec3(v.t, 0.0f, v.lane) : glm::vec3(v.lane, 0.0f, v.t);
}

// Which way it is facing.
inline glm::vec3 vehicleForward(const Vehicle& v) {
    if (v.turning)
        return glm::normalize(v.turnFwd * cosf(v.turnAngle) + v.turnRight * sinf(v.turnAngle));
    return laneForward(v);
}

// The RING ROAD (the outermost roads, 0 and GRID) runs ONE WAY round the
// city. Why: a car whose road ends at the ring turns right, and working that
// through for all four sides shows every such turn heads the same way round
// (bottom +X, right +Z, top -X, left -Z). If cars also drove the other way,
// their right turn at a corner would point out into the water.
inline float ringFlowDir(int axis, int road) {
    if (axis == 0) return (road == GRID) ? -1.0f : 1.0f;
    return (road == GRID) ? 1.0f : -1.0f;
}

// ---------------------------------------------------------------------------
//  SPAWN THE AI CARS
//
//  Each car gets a road, a direction, a cruising speed and a colour. Its lane
//  follows from the road and direction (keep right). Its starting position is
//  picked at random, but a spot is REJECTED - and another tried - if it would
//  overlap a car already in that lane or sit inside a junction. So the very
//  first frame already has no cars inside each other.
//
//  Speeds are 5 to 9 units a second. One unit is roughly 1.25 m, so that is
//  about 22 to 40 km/h - ordinary city driving.
// ---------------------------------------------------------------------------
inline void spawnVehicles(City& city, unsigned int seed, int count = 18) {
    city.vehicles.clear();
    // XOR gives cars their own random stream, so editing car code cannot
    // reshuffle the buildings.
    Random rng(seed ^ 0x9E3779B9u);

    const glm::vec3 carColors[6] = {
        glm::vec3(0.70f, 0.08f, 0.22f),   // deep red
        glm::vec3(0.06f, 0.35f, 0.55f),   // petrol blue
        glm::vec3(0.75f, 0.62f, 0.12f),   // taxi yellow
        glm::vec3(0.35f, 0.14f, 0.60f),   // violet
        glm::vec3(0.62f, 0.64f, 0.70f),   // silver
        glm::vec3(0.06f, 0.06f, 0.07f),   // black
    };
    const float JBOX = ROAD * 0.5f + 0.3f;

    for (int i = 0; i < count; ++i) {
        Vehicle v;
        v.axis     = (int)(rng.next() % 2);
        int road   = (int)(rng.next() % (GRID + 1));
        v.dir      = (rng.nextFloat() < 0.5f) ? -1.0f : 1.0f;
        if (road == 0 || road == GRID) v.dir = ringFlowDir(v.axis, road);   // one-way ring
        v.lane     = laneFor(v.axis, road, v.dir);
        v.rng      = rng.next() | 1u;
        v.speed    = rng.range(5.0f, 9.0f);
        v.v        = v.speed * 0.5f;
        v.color    = carColors[rng.next() % 6];
        v.rimColor = neonPalette(rng.next() % 6);

        bool placed = false;
        for (int tries = 0; tries < 40 && !placed; ++tries) {
            v.t = rng.range(-HALF, HALF);
            placed = true;
            for (int k = 0; k <= GRID; ++k)                      // not in a junction
                if (fabsf(v.t - (-HALF + k * CELL)) < JBOX + CAR_LENGTH) placed = false;
            for (const Vehicle& o : city.vehicles)               // not on another car
                if (o.axis == v.axis && fabsf(o.lane - v.lane) < 0.5f &&
                    fabsf(o.t - v.t) < CAR_LENGTH + 5.0f) placed = false;
        }
        if (placed) city.vehicles.push_back(v);                  // lane full: skip it
    }
}

// ---------------------------------------------------------------------------
//  THE CAR YOU DRIVE
//
//  Unlike the AI cars it is not tied to a lane: it has a free position and a
//  heading, and moves like a simple real car - it can only go the way it is
//  facing, and it turns more the faster it goes (a parked car cannot spin on
//  the spot).
//
//  'heading' uses the same convention as glm::rotate about Y: an angle of h
//  turns the model's +X (its nose) to point along (cos h, 0, -sin h).
// ---------------------------------------------------------------------------
struct PlayerCar {
    bool      exists  = false;   // spawned at least once (stays parked when you get out)
    bool      driving = false;   // you are at the wheel right now
    glm::vec3 pos     = glm::vec3(0.0f);
    float     heading = 0.0f;    // radians
    float     speed   = 0.0f;    // units per second; negative = reversing

    glm::vec3 forward() const { return glm::vec3(cosf(heading), 0.0f, -sinf(heading)); }
};

// ---------------------------------------------------------------------------
//  Where the solid things are, for the car you drive.
//
//  Every block (its pavement and the buildings on it) is treated as one solid
//  square, widened by 'radius' so the car's body - not just its centre point -
//  stays out. The lamp posts and traffic lights stand on those pavements, so
//  they are covered too.
// ---------------------------------------------------------------------------
inline bool hitsBlock(const City& city, const glm::vec3& p, float radius) {
    const float h = BLOCK * 0.5f + radius;
    for (const glm::vec3& c : city.blockCentres)
        if (fabsf(p.x - c.x) < h && fabsf(p.z - c.z) < h) return true;
    return false;
}

// Does a point come within 'radius' of an AI car's body? A car on its lane is
// an axis-aligned box, long along the road; a car mid-turn is slanted, so it
// is treated as a circle big enough to hold it.
inline bool hitsTraffic(const City& city, const glm::vec3& p, float radius) {
    for (const Vehicle& v : city.vehicles) {
        glm::vec3 c = vehiclePos(v);
        if (v.turning) {
            glm::vec2 d(p.x - c.x, p.z - c.z);
            if (glm::dot(d, d) < (1.95f + radius) * (1.95f + radius)) return true;
            continue;
        }
        float hx = (v.axis == 0 ? CAR_LENGTH : CAR_WIDTH) * 0.5f + radius;
        float hz = (v.axis == 0 ? CAR_WIDTH : CAR_LENGTH) * 0.5f + radius;
        if (fabsf(p.x - c.x) < hx && fabsf(p.z - c.z) < hz) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  AI TRAFFIC - one step
// ---------------------------------------------------------------------------
inline void updateTraffic(City& city, float dt, float time, const PlayerCar& player) {
    const int   N     = (int)city.vehicles.size();
    // Where a car on a TUNNEL road leaves and re-enters: deep inside the
    // tunnel, so it vanishes into the dark and reappears inside the tunnel on
    // the far side. Its front (1.8 further) is still short of the back wall.
    const float LIMIT = TUNNEL_END - CAR_LENGTH * 0.5f - 0.2f;
    const float JBOX  = ROAD * 0.5f + 0.3f;    // half-size of a junction box
    const float PI_2  = 1.5707963f;

    // Junction k, along a car's road: where it is, where a right turn there
    // starts (the arc begins LANE_OFFSET + TURN_R before the junction centre).
    auto junctionAlong = [](int k) { return -HALF + k * CELL; };
    auto turnStartT    = [&](const Vehicle& v, int k) {
        return junctionAlong(k) - v.dir * (LANE_OFFSET + TURN_R);
    };

    // ---- decide, once per junction, whether to turn there -----------------
    for (Vehicle& v : city.vehicles) {
        if (v.turning) continue;
        int pk = -1; float best = 1e9f;
        for (int k = 0; k <= GRID; ++k) {
            float d = (turnStartT(v, k) - v.t) * v.dir;        // distance to that turn
            if (d > -0.05f && d < best) { best = d; pk = k; }
        }
        if (pk == v.plannedK) continue;                        // already decided
        v.plannedK = pk;
        v.plannedTurn = false;
        if (pk < 0) continue;

        bool edge   = (v.dir > 0.0f) ? (pk == GRID) : (pk == 0); // the road ends here
        bool tunnel = (roadOf(v) == TUNNEL_ROAD);
        v.rng = v.rng * 1664525u + 1013904223u;                 // roll the car's dice
        float dice = (float)(v.rng >> 8) / 16777216.0f;

        // Where would a right turn here lead? Onto the road through this
        // junction (road number pk), heading the car's current right. If that
        // road is the one-way RING, the turn is only allowed WITH its flow.
        // (A car just out of a tunnel, turning right at the ring, would
        // otherwise join it the wrong way - and its next right turn would
        // point into the water. The headless test caught exactly that.)
        glm::vec3 rv    = rightOf(laneForward(v));
        int   newAxis   = 1 - v.axis;
        float newDir    = (newAxis == 0) ? rv.x : rv.z;
        bool  allowed   = !(pk == 0 || pk == GRID) || newDir == ringFlowDir(newAxis, pk);

        // MUST turn where the road ends without a tunnel (those turns always
        // go with the ring's flow); anywhere else, if allowed, by chance.
        //
        // The chance is much HIGHER on the ring road. Every road that ends at
        // the waterfront forces its cars ONTO the ring, so if ring cars only
        // left 1 time in 4 like everyone else, the single-lane ring slowly
        // filled up with half the city's traffic and formed one long convoy.
        // Measured: 8-11 of 18 cars on the ring at 25%; with 60% it balances.
        bool  onRing    = (roadOf(v) == 0 || roadOf(v) == GRID);
        float turnOdds  = onRing ? 0.60f : 0.25f;
        v.plannedTurn = (edge && !tunnel) || (dice < turnOdds && allowed);
    }

    // ---- which junctions are occupied, and by which road direction --------
    // occupied[i][j][axis] is true if a car driving along 'axis' has any part
    // of its body inside junction (i, j) right now. A car mid-turn is inside
    // BOTH roads' paths at once, so it blocks both directions.
    bool occupied[GRID + 1][GRID + 1][2] = {};
    for (const Vehicle& v : city.vehicles) {
        int road = roadOf(v);
        if (v.turning) {
            int i = (v.axis == 0) ? v.turnK : road;
            int j = (v.axis == 0) ? road : v.turnK;
            occupied[i][j][0] = occupied[i][j][1] = true;
            continue;
        }
        for (int k = 0; k <= GRID; ++k) {
            if (fabsf(v.t - junctionAlong(k)) < JBOX + CAR_LENGTH * 0.5f) {
                int i = (v.axis == 0) ? k : road;
                int j = (v.axis == 0) ? road : k;
                occupied[i][j][v.axis] = true;
            }
        }
    }
    // The player's car blocks a junction for BOTH directions.
    if (player.exists) {
        for (int i = 0; i <= GRID; ++i)
            for (int j = 0; j <= GRID; ++j) {
                float jx = -HALF + i * CELL, jz = -HALF + j * CELL;
                if (fabsf(player.pos.x - jx) < JBOX + 2.0f && fabsf(player.pos.z - jz) < JBOX + 2.0f)
                    occupied[i][j][0] = occupied[i][j][1] = true;
            }
    }

    // Is the lane a right turn would come out in clear? A car waits at the
    // stop line rather than turning into the back of a queue.
    auto exitBlocked = [&](const Vehicle& v, int k) {
        glm::vec3 f  = laneForward(v), rv = rightOf(f);
        int   newAxis = 1 - v.axis;
        float newDir  = (newAxis == 0) ? rv.x : rv.z;
        float newLane = laneFor(newAxis, k, newDir);
        glm::vec3 J   = (v.axis == 0) ? glm::vec3(junctionAlong(k), 0.0f, -HALF + roadOf(v) * CELL)
                                      : glm::vec3(-HALF + roadOf(v) * CELL, 0.0f, junctionAlong(k));
        glm::vec3 end = J + rv * (LANE_OFFSET + TURN_R) - f * LANE_OFFSET;
        float exitT   = (newAxis == 0) ? end.x : end.z;
        for (const Vehicle& o : city.vehicles) {
            if (o.turning || o.axis != newAxis || o.dir != newDir || fabsf(o.lane - newLane) > 0.5f) continue;
            float rel = (o.t - exitT) * newDir;
            if (rel > -(CAR_LENGTH + 1.0f) && rel < CAR_LENGTH + 5.0f) return true;
        }
        return false;
    };

    std::vector<float> target(N);

    for (int a = 0; a < N; ++a) {
        const Vehicle& v = city.vehicles[a];
        float want = v.turning ? std::min(v.speed, TURN_SPEED) : v.speed;

        // ---- rule 2: follow whatever is in the corridor straight ahead ------
        // For every other car: how far AHEAD is it (along my heading) and how
        // far to the SIDE? Anything ahead and within 1.3 sideways is in my
        // path. This one test covers a car in my lane, a car mid-turn, and a
        // car crossing in front of me - and it works while I am turning too.
        glm::vec3 myPos = vehiclePos(v), myFwd = vehicleForward(v), myRight = rightOf(myFwd);
        float nearest = 1e9f;
        for (int b = 0; b < N; ++b) {
            if (b == a) continue;
            glm::vec3 rel = vehiclePos(city.vehicles[b]) - myPos;
            float ahead = glm::dot(rel, myFwd);
            float side  = fabsf(glm::dot(rel, myRight));
            if (ahead > 0.0f && side < 1.3f) nearest = std::min(nearest, ahead);
        }
        // The player's car is not lane-aligned, so its corridor is wider.
        if (player.exists) {
            glm::vec3 rel = player.pos - myPos;
            float ahead = glm::dot(rel, myFwd);
            float side  = fabsf(glm::dot(rel, myRight));
            if (ahead > 0.0f && side < CAR_WIDTH + 0.4f) nearest = std::min(nearest, ahead);
        }
        if (nearest < 1e8f) {
            float gap = nearest - CAR_LENGTH;                  // bumper to bumper
            want = std::min(want, std::max(0.0f, (gap - 1.5f) * 1.4f));
        }

        // ---- rules 3 & 4: lights and junction boxes -----------------------
        // (a car already mid-turn is inside the junction - it just carries on)
        if (!v.turning) {
            int   road     = roadOf(v);
            float stopDist = 1e9f;
            int   stopK    = -1;
            for (int k = 0; k <= GRID; ++k) {
                float stopT = junctionAlong(k) - v.dir * (JBOX + 0.8f + CAR_LENGTH * 0.5f);
                float d     = (stopT - v.t) * v.dir;           // distance to that line
                if (d > -0.3f && d < stopDist) { stopDist = d; stopK = k; }
            }
            if (stopK >= 0) {
                int i = (v.axis == 0) ? stopK : road;
                int j = (v.axis == 0) ? road : stopK;
                const TrafficLight& tl = city.trafficLights[i * (GRID + 1) + j];
                int sig = signalFor(time, tl.phaseOffset, v.axis);

                // Stopping distance at this speed with our braking rate (v^2/2a).
                float canStopIn = v.v * v.v / (2.0f * 10.0f) + 0.5f;

                bool turningHere = v.plannedTurn && v.plannedK == stopK;
                bool mustStop =
                    sig == 0 ||                                   // red
                    (sig == 1 && stopDist > canStopIn) ||         // amber, room to stop
                    occupied[i][j][1 - v.axis] ||                 // crossing car inside
                    (turningHere && exitBlocked(v, stopK));       // nowhere to turn into

                if (mustStop)
                    want = std::min(want, std::max(0.0f, stopDist * 1.3f));
            }
        }
        target[a] = want;
    }

    // ---- rule 5: move toward the target speed, then drive ------------------
    for (int a = 0; a < N; ++a) {
        Vehicle& v = city.vehicles[a];
        float change = target[a] - v.v;
        change = std::max(-14.0f * dt, std::min(4.0f * dt, change));   // brake 14, accelerate 4
        v.v = std::max(0.0f, v.v + change);

        if (v.turning) {
            // Along the arc: angle grows by speed / radius.
            v.turnAngle += v.v / TURN_R * dt;
            glm::vec3 p = vehiclePos(v);
            v.t = (v.axis == 0) ? p.x : p.z;          // keep 't' roughly right meanwhile

            if (v.turnAngle >= PI_2) {
                // Arrived: it now drives on the crossing road, heading 'turnRight'.
                v.turnAngle = PI_2;
                glm::vec3 end = vehiclePos(v);
                int   newAxis = 1 - v.axis;
                float newDir  = (newAxis == 0) ? v.turnRight.x : v.turnRight.z;
                v.axis     = newAxis;
                v.dir      = (newDir > 0.0f) ? 1.0f : -1.0f;
                v.lane     = laneFor(newAxis, v.turnK, v.dir);
                v.t        = (newAxis == 0) ? end.x : end.z;
                v.turning  = false;
                v.plannedK = -99;                         // decide afresh from here
            }
            continue;
        }

        v.t += v.v * v.dir * dt;

        // Reached the start of a planned right turn? Then leave the lane and
        // follow the arc (see TURNING above), carrying over any overshoot.
        if (v.plannedTurn && v.plannedK >= 0) {
            float ts   = turnStartT(v, v.plannedK);
            float past = (v.t - ts) * v.dir;
            if (past >= 0.0f) {
                glm::vec3 f  = laneForward(v), rv = rightOf(f);
                glm::vec3 J  = (v.axis == 0)
                    ? glm::vec3(junctionAlong(v.plannedK), 0.0f, -HALF + roadOf(v) * CELL)
                    : glm::vec3(-HALF + roadOf(v) * CELL, 0.0f, junctionAlong(v.plannedK));
                glm::vec3 start = J + rv * LANE_OFFSET - f * (LANE_OFFSET + TURN_R);
                v.turning    = true;
                v.turnK      = v.plannedK;
                v.turnFwd    = f;
                v.turnRight  = rv;
                v.turnCentre = start + rv * TURN_R;
                v.turnAngle  = past / TURN_R;
                v.plannedTurn = false;
                continue;
            }
        }

        // Only the tunnel roads reach this far: into the tunnel, and out of
        // the one on the far side - but only if that exit is clear, so a car
        // never appears on top of another one.
        if (v.t * v.dir > LIMIT) {
            float entry = -LIMIT * v.dir;
            bool clear = true;
            for (int b = 0; b < N; ++b) {
                if (b == a) continue;
                const Vehicle& o = city.vehicles[b];
                if (!o.turning && o.axis == v.axis && o.dir == v.dir && fabsf(o.lane - v.lane) < 0.5f &&
                    fabsf(o.t - entry) < CAR_LENGTH + 4.0f) { clear = false; break; }
            }
            if (clear) { v.t = entry; v.plannedK = -99; }
            else       { v.t = LIMIT * v.dir; v.v = 0.0f; }
        }
    }
}

// ---------------------------------------------------------------------------
//  THE PLAYER'S CAR - one step
//
//  throttle: +1 accelerate, -1 brake / reverse, 0 coast.  steer: +1 left,
//  -1 right.  handbrake: stop hard.
// ---------------------------------------------------------------------------
inline void updatePlayer(PlayerCar& car, const City& city, float dt,
                         float throttle, float steer, bool handbrake) {
    const float MAX_FORWARD = 20.0f;     // about 90 km/h
    const float MAX_REVERSE = -6.0f;

    // ---- speed ----
    if (handbrake) {
        car.speed -= std::min(fabsf(car.speed), 30.0f * dt) * (car.speed > 0 ? 1.0f : -1.0f);
    } else if (throttle > 0.0f) {
        car.speed += (car.speed < 0.0f ? 20.0f : 9.0f) * dt;    // brake out of reverse faster
    } else if (throttle < 0.0f) {
        car.speed -= (car.speed > 0.0f ? 20.0f : 6.0f) * dt;    // brake, then reverse
    } else {
        // no pedal: rolling resistance slowly brings it to a stop
        float drag = std::min(fabsf(car.speed), 4.0f * dt);
        car.speed -= drag * (car.speed > 0 ? 1.0f : -1.0f);
    }
    car.speed = std::max(MAX_REVERSE, std::min(MAX_FORWARD, car.speed));

    // ---- steering ----
    // Turning rate grows with speed up to a limit, and flips when reversing
    // (reverse a real car with the wheel turned left and the back swings left).
    float grip = std::min(1.0f, fabsf(car.speed) / 6.0f);
    car.heading += steer * 1.7f * grip * (car.speed >= 0.0f ? 1.0f : -1.0f) * dt;

    // ---- move, with collisions ----
    // Try the full move. If that hits something, try sliding along only X,
    // then only Z - so brushing a wall scrapes along it instead of sticking.
    const float RADIUS = 1.1f;
    glm::vec3 step = car.forward() * car.speed * dt;
    auto blocked = [&](const glm::vec3& p) {
        // Stay on the roads: not past the outer roads' edge (so not into the
        // tunnels - those are for through traffic - and never into the water).
        // (The limit is for the car's CENTRE, so it stops a car-width short
        // of the kerb barrier.)
        bool outside = fabsf(p.x) > HALF + ROAD * 0.5f - 1.0f ||
                       fabsf(p.z) > HALF + ROAD * 0.5f - 1.0f;
        return outside || hitsBlock(city, p, RADIUS) || hitsTraffic(city, p, RADIUS);
    };

    glm::vec3 full  = car.pos + step;
    glm::vec3 onlyX = car.pos + glm::vec3(step.x, 0.0f, 0.0f);
    glm::vec3 onlyZ = car.pos + glm::vec3(0.0f, 0.0f, step.z);

    // Escape hatch: if the car is ALREADY overlapping something (another car
    // drove into it while parked, say), every move would count as blocked and
    // it could never get out. So while stuck, let it move freely until clear.
    if (blocked(car.pos)) { car.pos = full; return; }

    if      (!blocked(full))  car.pos = full;
    else if (!blocked(onlyX)) { car.pos = onlyX; car.speed *= 0.9f; }
    else if (!blocked(onlyZ)) { car.pos = onlyZ; car.speed *= 0.9f; }
    else                      car.speed = -car.speed * 0.2f;    // small bounce back
}

// ---------------------------------------------------------------------------
//  Put the player's car on the road nearest to a point (the camera), facing
//  roughly the way the camera looks, in the correct right-hand lane.
// ---------------------------------------------------------------------------
inline void placePlayerNear(PlayerCar& car, const City& city,
                            const glm::vec3& at, const glm::vec3& look) {
    auto nearestRoad = [](float v) {
        float k = roundf((v + HALF) / CELL);
        k = std::max(0.0f, std::min((float)GRID, k));
        return (int)k;
    };
    int rx = nearestRoad(at.x), rz = nearestRoad(at.z);
    float dx = fabsf(at.x - (-HALF + rx * CELL));
    float dz = fabsf(at.z - (-HALF + rz * CELL));

    glm::vec3 dir;
    if (dx < dz) {          // closer to a road running along Z
        float s = (look.z >= 0.0f) ? 1.0f : -1.0f;
        dir = glm::vec3(0.0f, 0.0f, s);
        car.pos = glm::vec3(laneFor(1, rx, s), 0.0f, std::max(-HALF, std::min(HALF, at.z)));
    } else {                // closer to a road running along X
        float s = (look.x >= 0.0f) ? 1.0f : -1.0f;
        dir = glm::vec3(s, 0.0f, 0.0f);
        car.pos = glm::vec3(std::max(-HALF, std::min(HALF, at.x)), 0.0f, laneFor(0, rz, s));
    }
    car.heading = atan2f(-dir.z, dir.x);
    car.speed   = 0.0f;

    // If an AI car is sitting exactly there, move along the road until clear.
    for (int tries = 0; tries < 12 && hitsTraffic(city, car.pos, 1.3f); ++tries)
        car.pos += dir * 5.0f;

    car.exists = true;
}
