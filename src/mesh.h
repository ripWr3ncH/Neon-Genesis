// ===========================================================================
//  mesh.h  -  primitive shapes and how they get onto the GPU
//
//  WHAT CHANGED FROM STAGE 1
//  A vertex used to be 6 floats:   x y z | r g b
//  Now it is 11:                   x y z | r g b | nx ny nz | u v
//
//  The two new pieces:
//
//    NORMAL (nx,ny,nz) - the direction the surface faces. Lighting is
//        basically "how closely does this surface point at the lamp?", so
//        without normals there is no lighting at all.
//
//    UV (u,v) - texture coordinates. (0,0) is one corner of the image and
//        (1,1) the opposite one. This is how a window-grid texture gets
//        stretched over a building face.
//
//  THE UNIT SHAPE RULE IS UNCHANGED
//  Every shape is still 1 x 1 x 1, centred on X and Z, base at y = 0, so
//  scale() and translate() still mean exactly what they say.
// ===========================================================================

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <utility>

// 11 floats per vertex - keep this in one place so the stride below and the
// shader's layout(location=...) list can never drift apart.
static const int FLOATS_PER_VERTEX = 11;

// ---------------------------------------------------------------------------
//  WINDING CHECK  -  catches inside-out shapes at startup
//
//  OpenGL decides which side of a triangle is the FRONT from the order of its
//  three corners: counter-clockwise as seen from the viewer = front. With
//  back-face culling on, a triangle listed the wrong way round is thrown away
//  exactly when it should be visible, and the shape looks hollow.
//
//  Each vertex also stores the normal we MEANT it to have. So for every
//  triangle we can compute the normal its corner order actually produces -
//      cross(B - A, C - A)
//  - and check it points the same way as the stored one (positive dot
//  product). Any disagreement is printed with the mesh's name.
//
//  This is how the round towers' bug was confirmed: every cylinder triangle
//  was wound clockwise, so the towers were drawn inside out.
// ---------------------------------------------------------------------------
inline void checkWinding(const std::vector<float>& v, const std::vector<unsigned int>* idx,
                         const char* name) {
    const int F = 11;   // floats per vertex (see FLOATS_PER_VERTEX below)
    auto P = [&](unsigned k) { return glm::vec3(v[k*F+0], v[k*F+1], v[k*F+2]); };
    auto N = [&](unsigned k) { return glm::vec3(v[k*F+6], v[k*F+7], v[k*F+8]); };

    size_t triCount = idx ? idx->size() / 3 : v.size() / F / 3;
    int bad = 0;
    for (size_t t = 0; t < triCount; ++t) {
        unsigned a = idx ? (*idx)[t*3+0] : (unsigned)(t*3+0);
        unsigned b = idx ? (*idx)[t*3+1] : (unsigned)(t*3+1);
        unsigned c = idx ? (*idx)[t*3+2] : (unsigned)(t*3+2);
        glm::vec3 stored = N(a) + N(b) + N(c);
        if (glm::dot(stored, stored) < 1e-6f) continue;   // no normals (sky box)
        glm::vec3 wound = glm::cross(P(b) - P(a), P(c) - P(a));
        if (glm::dot(wound, stored) < 0.0f) ++bad;
    }
    if (bad > 0)
        std::cout << "WINDING WARNING: " << name << " has " << bad << " of "
                  << triCount << " triangles facing the wrong way\n";
}

struct Mesh {
    GLuint  vao = 0;
    GLuint  vbo = 0;
    GLuint  ebo = 0;
    GLsizei count = 0;
    bool    indexed = false;
};

// ---------------------------------------------------------------------------
//  Upload a vertex list to the GPU and describe its layout.
//
//  The four glVertexAttribPointer calls tell OpenGL how to read the flat
//  wall of floats. Each says: attribute slot, how many floats, their type,
//  the STRIDE (how far to the next vertex) and the OFFSET (where this piece
//  starts inside one vertex).
//
//      [ x y z | r g b | nx ny nz | u v ]
//        ^0      ^3      ^6         ^9      <- offsets, in floats
// ---------------------------------------------------------------------------
inline Mesh makeMesh(const std::vector<float>& verts, const char* name = "mesh") {
    checkWinding(verts, nullptr, name);
    Mesh m;
    m.count   = (GLsizei)(verts.size() / FLOATS_PER_VERTEX);
    m.indexed = false;

    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);

    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);

    const GLsizei stride = FLOATS_PER_VERTEX * sizeof(float);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);                                      // position
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);                                      // colour
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);                                      // normal
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)(9 * sizeof(float)));
    glEnableVertexAttribArray(3);                                      // uv

    glBindVertexArray(0);
    return m;
}

inline Mesh makeIndexedMesh(const std::vector<float>& verts,
                            const std::vector<unsigned int>& idx,
                            const char* name = "mesh") {
    checkWinding(verts, &idx, name);
    Mesh m;
    m.count   = (GLsizei)idx.size();
    m.indexed = true;

    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);

    glBindVertexArray(m.vao);

    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);

    // The EBO binding lives inside the VAO, so it must be bound while the
    // VAO is bound.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int),
                 idx.data(), GL_STATIC_DRAW);

    const GLsizei stride = FLOATS_PER_VERTEX * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)(9 * sizeof(float)));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);
    return m;
}

inline void drawMesh(const Mesh& m) {
    glBindVertexArray(m.vao);
    if (m.indexed) glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, 0);
    else           glDrawArrays(GL_TRIANGLES, 0, m.count);
}

inline void freeMesh(Mesh& m) {
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.ebo) glDeleteBuffers(1, &m.ebo);
    m.vao = m.vbo = m.ebo = 0;
}

// Small helper so the shape tables below stay readable.
// One call = one complete vertex.
inline void pushVert(std::vector<float>& v,
                     float x, float y, float z,
                     float r, float g, float b,
                     float nx, float ny, float nz,
                     float u, float tv) {
    v.push_back(x);  v.push_back(y);  v.push_back(z);
    v.push_back(r);  v.push_back(g);  v.push_back(b);
    v.push_back(nx); v.push_back(ny); v.push_back(nz);
    v.push_back(u);  v.push_back(tv);
}

// ---------------------------------------------------------------------------
//  CUBE  -  x: -0.5..+0.5,  y: 0..1,  z: -0.5..+0.5
//
//  Still 36 vertices (6 faces x 2 triangles x 3 corners), and NOW the reason
//  is visible: each face carries its own normal. The front face points at
//  (0,0,1), the top at (0,1,0), and so on. A shared corner could only hold
//  one of those, which is exactly why the corners are duplicated.
//
//  The flat per-face brightness from Stage 1 is GONE. Every face is plain
//  white now, because real lighting does that job properly.
//
//  UVs run 0..1 across each face, so a window texture tiles once per face
//  and the C++ side can repeat it by scaling the coordinates.
// ---------------------------------------------------------------------------
inline Mesh makeCube() {
    std::vector<float> v;
    v.reserve(36 * FLOATS_PER_VERTEX);

    const float C = 1.0f;   // white - the tint uniform supplies real colour

    // FRONT  (+Z)
    pushVert(v, -0.5f,0.0f, 0.5f, C,C,C,  0,0,1,  0,0);
    pushVert(v,  0.5f,0.0f, 0.5f, C,C,C,  0,0,1,  1,0);
    pushVert(v,  0.5f,1.0f, 0.5f, C,C,C,  0,0,1,  1,1);
    pushVert(v,  0.5f,1.0f, 0.5f, C,C,C,  0,0,1,  1,1);
    pushVert(v, -0.5f,1.0f, 0.5f, C,C,C,  0,0,1,  0,1);
    pushVert(v, -0.5f,0.0f, 0.5f, C,C,C,  0,0,1,  0,0);

    // BACK  (-Z)
    pushVert(v, -0.5f,0.0f,-0.5f, C,C,C,  0,0,-1, 1,0);
    pushVert(v, -0.5f,1.0f,-0.5f, C,C,C,  0,0,-1, 1,1);
    pushVert(v,  0.5f,1.0f,-0.5f, C,C,C,  0,0,-1, 0,1);
    pushVert(v,  0.5f,1.0f,-0.5f, C,C,C,  0,0,-1, 0,1);
    pushVert(v,  0.5f,0.0f,-0.5f, C,C,C,  0,0,-1, 0,0);
    pushVert(v, -0.5f,0.0f,-0.5f, C,C,C,  0,0,-1, 1,0);

    // LEFT  (-X)
    pushVert(v, -0.5f,1.0f, 0.5f, C,C,C, -1,0,0,  1,1);
    pushVert(v, -0.5f,1.0f,-0.5f, C,C,C, -1,0,0,  0,1);
    pushVert(v, -0.5f,0.0f,-0.5f, C,C,C, -1,0,0,  0,0);
    pushVert(v, -0.5f,0.0f,-0.5f, C,C,C, -1,0,0,  0,0);
    pushVert(v, -0.5f,0.0f, 0.5f, C,C,C, -1,0,0,  1,0);
    pushVert(v, -0.5f,1.0f, 0.5f, C,C,C, -1,0,0,  1,1);

    // RIGHT (+X)
    pushVert(v,  0.5f,1.0f, 0.5f, C,C,C,  1,0,0,  0,1);
    pushVert(v,  0.5f,0.0f, 0.5f, C,C,C,  1,0,0,  0,0);
    pushVert(v,  0.5f,0.0f,-0.5f, C,C,C,  1,0,0,  1,0);
    pushVert(v,  0.5f,0.0f,-0.5f, C,C,C,  1,0,0,  1,0);
    pushVert(v,  0.5f,1.0f,-0.5f, C,C,C,  1,0,0,  1,1);
    pushVert(v,  0.5f,1.0f, 0.5f, C,C,C,  1,0,0,  0,1);

    // BOTTOM (-Y)
    pushVert(v, -0.5f,0.0f,-0.5f, C,C,C,  0,-1,0, 0,0);
    pushVert(v,  0.5f,0.0f,-0.5f, C,C,C,  0,-1,0, 1,0);
    pushVert(v,  0.5f,0.0f, 0.5f, C,C,C,  0,-1,0, 1,1);
    pushVert(v,  0.5f,0.0f, 0.5f, C,C,C,  0,-1,0, 1,1);
    pushVert(v, -0.5f,0.0f, 0.5f, C,C,C,  0,-1,0, 0,1);
    pushVert(v, -0.5f,0.0f,-0.5f, C,C,C,  0,-1,0, 0,0);

    // TOP (+Y)
    pushVert(v, -0.5f,1.0f,-0.5f, C,C,C,  0,1,0,  0,1);
    pushVert(v, -0.5f,1.0f, 0.5f, C,C,C,  0,1,0,  0,0);
    pushVert(v,  0.5f,1.0f, 0.5f, C,C,C,  0,1,0,  1,0);
    pushVert(v,  0.5f,1.0f, 0.5f, C,C,C,  0,1,0,  1,0);
    pushVert(v,  0.5f,1.0f,-0.5f, C,C,C,  0,1,0,  1,1);
    pushVert(v, -0.5f,1.0f,-0.5f, C,C,C,  0,1,0,  0,1);

    return makeMesh(v, "cube");
}

// ---------------------------------------------------------------------------
//  CYLINDER  -  radius 0.5, height 0..1, standing along Y
//
//  A circle has no flat faces, so it is approximated by walking around it in
//  equal angle steps:
//        x = r * cos(angle)      z = r * sin(angle)
//  Using cos->x and sin->z (not y) puts the circle FLAT on the ground, so
//  the cylinder stands upright like a lamp post.
//
//  NORMALS ARE THE INTERESTING PART HERE.
//  On the curved wall the normal points straight out from the axis, which is
//  simply (cos, 0, sin) - the same direction as the vertex itself. Because
//  neighbouring segments get slightly different normals, the GPU interpolates
//  between them and the 16-sided tube is shaded as if it were truly round.
//  The caps are flat, so they get a single normal each: (0,1,0) / (0,-1,0).
// ---------------------------------------------------------------------------
// caps = false leaves the top and bottom open, giving a hollow TUBE. That is
// what a neon ring round a tower needs: with caps it reads as a solid glowing
// disc floating in the air.
inline Mesh makeCylinder(int segments = 16, bool caps = true) {
    std::vector<float> v;
    const float PI = 3.14159265358979f;
    const float r  = 0.5f;

    for (int i = 0; i < segments; ++i) {
        float a0 = 2.0f * PI * (float)i       / (float)segments;
        float a1 = 2.0f * PI * (float)(i + 1) / (float)segments;

        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);
        float x0 = r * c0,   z0 = r * s0;
        float x1 = r * c1,   z1 = r * s1;

        float u0 = (float)i       / (float)segments;
        float u1 = (float)(i + 1) / (float)segments;

        // side wall - 2 triangles, normals point radially outward.
        //
        // CORNER ORDER MATTERS. The angle grows from +X toward +Z, which seen
        // from OUTSIDE the tube runs right-to-left. So to go counter-clockwise
        // (= front-facing) each triangle is listed  bottom0 -> top1 -> bottom1.
        // An earlier version listed them the other way round, and every round
        // tower was drawn inside out - it looked hollow.
        pushVert(v, x0,0.0f,z0, 1,1,1, c0,0,s0, u0,0);
        pushVert(v, x1,1.0f,z1, 1,1,1, c1,0,s1, u1,1);
        pushVert(v, x1,0.0f,z1, 1,1,1, c1,0,s1, u1,0);
        pushVert(v, x1,1.0f,z1, 1,1,1, c1,0,s1, u1,1);
        pushVert(v, x0,0.0f,z0, 1,1,1, c0,0,s0, u0,0);
        pushVert(v, x0,1.0f,z0, 1,1,1, c0,0,s0, u0,1);

        if (!caps) continue;

        // top cap - a pie slice facing straight up: centre -> x1 -> x0 is
        // counter-clockwise when looked at from above
        pushVert(v, 0,1.0f,0,   1,1,1, 0,1,0, 0.5f,0.5f);
        pushVert(v, x1,1.0f,z1, 1,1,1, 0,1,0, c1*0.5f+0.5f, s1*0.5f+0.5f);
        pushVert(v, x0,1.0f,z0, 1,1,1, 0,1,0, c0*0.5f+0.5f, s0*0.5f+0.5f);

        // bottom cap - the same slice in the opposite order, so it faces down
        pushVert(v, 0,0.0f,0,   1,1,1, 0,-1,0, 0.5f,0.5f);
        pushVert(v, x0,0.0f,z0, 1,1,1, 0,-1,0, c0*0.5f+0.5f, s0*0.5f+0.5f);
        pushVert(v, x1,0.0f,z1, 1,1,1, 0,-1,0, c1*0.5f+0.5f, s1*0.5f+0.5f);
    }
    return makeMesh(v, caps ? "cylinder" : "tube");
}

// ---------------------------------------------------------------------------
//  CONE  -  open base of radius 0.5 at y = 0, apex at (0, 1, 0)
//
//  Used for the LIGHT BEAMS under the street lamps and in front of the
//  headlights. No caps: a beam is only its glowing side wall. The normals
//  point straight out from the axis (the slope is ignored): the beam shader
//  only needs to know how much a pixel faces the camera, to fade the edges.
//  v runs 0 at the base to 1 at the apex - "how close to the lamp".
// ---------------------------------------------------------------------------
inline Mesh makeCone(int segments = 20) {
    std::vector<float> v;
    const float PI = 3.14159265358979f;
    for (int i = 0; i < segments; ++i) {
        float a0 = 2.0f * PI * (float)i       / (float)segments;
        float a1 = 2.0f * PI * (float)(i + 1) / (float)segments;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        float am = 0.5f * (a0 + a1);
        // base0 -> apex -> base1, counter-clockwise seen from outside
        pushVert(v, 0.5f*c0,0.0f,0.5f*s0, 1,1,1, c0,0,s0,             0,0);
        pushVert(v, 0.0f,   1.0f,0.0f,    1,1,1, cosf(am),0,sinf(am), 0,1);
        pushVert(v, 0.5f*c1,0.0f,0.5f*s1, 1,1,1, c1,0,s1,             0,0);
    }
    return makeMesh(v, "cone");
}

// ---------------------------------------------------------------------------
//  PYRAMID  -  square base 1x1 at y=0, apex at (0,1,0)
//
//  The four slopes are tilted, so their normals are tilted too. Each is
//  computed as the cross product of two edges of that triangle, which is the
//  standard way to get a face normal:
//        N = normalize( cross(B - A, C - A) )
//  Doing it in code rather than typing guessed numbers means the lighting is
//  correct no matter how the shape is later scaled.
// ---------------------------------------------------------------------------
inline Mesh makePyramid() {
    std::vector<float> v;

    glm::vec3 apex(0.0f, 1.0f, 0.0f);
    glm::vec3 c[4] = {
        glm::vec3(-0.5f, 0.0f,  0.5f),   // front-left
        glm::vec3( 0.5f, 0.0f,  0.5f),   // front-right
        glm::vec3( 0.5f, 0.0f, -0.5f),   // back-right
        glm::vec3(-0.5f, 0.0f, -0.5f),   // back-left
    };

    // four slanted sides
    for (int i = 0; i < 4; ++i) {
        glm::vec3 a = c[i];
        glm::vec3 b = c[(i + 1) % 4];
        glm::vec3 n = glm::normalize(glm::cross(b - a, apex - a));

        pushVert(v, a.x,a.y,a.z, 1,1,1, n.x,n.y,n.z, 0,0);
        pushVert(v, b.x,b.y,b.z, 1,1,1, n.x,n.y,n.z, 1,0);
        pushVert(v, apex.x,apex.y,apex.z, 1,1,1, n.x,n.y,n.z, 0.5f,1);
    }

    // square base, facing down
    pushVert(v, c[3].x,0,c[3].z, 1,1,1, 0,-1,0, 0,0);
    pushVert(v, c[2].x,0,c[2].z, 1,1,1, 0,-1,0, 1,0);
    pushVert(v, c[1].x,0,c[1].z, 1,1,1, 0,-1,0, 1,1);
    pushVert(v, c[1].x,0,c[1].z, 1,1,1, 0,-1,0, 1,1);
    pushVert(v, c[0].x,0,c[0].z, 1,1,1, 0,-1,0, 0,1);
    pushVert(v, c[3].x,0,c[3].z, 1,1,1, 0,-1,0, 0,0);

    return makeMesh(v, "pyramid");
}

// ---------------------------------------------------------------------------
//  QUAD  -  flat 1x1 plate lying in the XZ plane at y = 0
//
//  Used for the ground, roads and lane markings. This is the one shape where
//  an EBO genuinely pays off: 4 corners stored, 6 indices drawn, and because
//  the whole plate is flat every corner shares the same normal (0,1,0), so
//  nothing is lost by sharing them.
// ---------------------------------------------------------------------------
inline Mesh makeQuad() {
    std::vector<float> v;
    pushVert(v, -0.5f,0.0f,-0.5f, 1,1,1, 0,1,0, 0,0);
    pushVert(v,  0.5f,0.0f,-0.5f, 1,1,1, 0,1,0, 1,0);
    pushVert(v,  0.5f,0.0f, 0.5f, 1,1,1, 0,1,0, 1,1);
    pushVert(v, -0.5f,0.0f, 0.5f, 1,1,1, 0,1,0, 0,1);

    std::vector<unsigned int> idx = { 0, 2, 1,   0, 3, 2 };
    return makeIndexedMesh(v, idx, "quad");
}

// ---------------------------------------------------------------------------
//  UPRIGHT QUAD  -  a 1x1 plate STANDING in the XY plane, facing +Z
//
//  New in this stage. Billboards, neon strips and window panels are all flat
//  things stuck to the side of a building, and building sides are vertical.
//  Rotating the floor quad would work, but a dedicated shape keeps the draw
//  code simple: translate, scale, done.
//
//  Centred on x, base at y = 0 - the unit-shape rule again.
// ---------------------------------------------------------------------------
inline Mesh makePanel() {
    std::vector<float> v;
    pushVert(v, -0.5f,0.0f,0.0f, 1,1,1, 0,0,1, 0,0);
    pushVert(v,  0.5f,0.0f,0.0f, 1,1,1, 0,0,1, 1,0);
    pushVert(v,  0.5f,1.0f,0.0f, 1,1,1, 0,0,1, 1,1);
    pushVert(v, -0.5f,1.0f,0.0f, 1,1,1, 0,0,1, 0,1);

    std::vector<unsigned int> idx = { 0, 1, 2,   0, 2, 3 };
    return makeIndexedMesh(v, idx, "panel");
}

// ---------------------------------------------------------------------------
//  SKYBOX  -  a 2x2x2 cube centred on the origin, wound INWARDS
//
//  Every other shape is seen from outside. The sky is seen from INSIDE, so
//  the renderer simply switches back-face culling OFF while drawing it -
//  otherwise it could throw away exactly the faces we need to see.
//
//  It carries positions only; the sky shader uses the raw direction and needs
//  no normals or UVs, but the layout must still match, so those slots are
//  filled with zeros.
// ---------------------------------------------------------------------------
inline Mesh makeSkybox() {
    const float s = 1.0f;
    float pts[8][3] = {
        {-s,-s,-s}, { s,-s,-s}, { s, s,-s}, {-s, s,-s},
        {-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s},
    };
    // Each face listed so that, viewed from inside the box, it faces us.
    int faces[6][4] = {
        {0,1,2,3},   // -Z
        {5,4,7,6},   // +Z
        {4,0,3,7},   // -X
        {1,5,6,2},   // +X
        {4,5,1,0},   // -Y
        {3,2,6,7},   // +Y
    };

    std::vector<float> v;
    for (int f = 0; f < 6; ++f) {
        int i0 = faces[f][0], i1 = faces[f][1], i2 = faces[f][2], i3 = faces[f][3];
        int tri[6] = { i0, i1, i2,  i0, i2, i3 };
        for (int k = 0; k < 6; ++k) {
            float* p = pts[tri[k]];
            pushVert(v, p[0], p[1], p[2], 1,1,1, 0,0,0, 0,0);
        }
    }
    return makeMesh(v, "skybox");
}

// ---------------------------------------------------------------------------
//  EXTRUSION  -  turn any flat outline into a solid
//
//  This is how every shape that is not a box or a cylinder is made. You give
//  a 2D outline (a list of corner points going round the shape) and the
//  function "pushes" it out into 3D, like squeezing toothpaste through a
//  shaped nozzle:
//
//        outline (2D)                extruded (3D)
//          ___                          ___
//         /   \           --->         /   \ |
//         \___/                        \___/ |
//                                       \____|
//
//  It produces three kinds of triangles:
//    - SIDE WALLS  one rectangle (2 triangles) per edge of the outline
//    - TWO CAPS    the outline itself, at each end, filled in
//
//  FILLING THE CAPS - "triangle fan from the centre"
//  Each cap is cut into triangles that all share the outline's centre point,
//  like slices of a pie. This works for any outline that can be "seen" whole
//  from its centre (convex shapes, and many others). Every outline used in
//  this project - octagon, car body, car cabin - is convex.
//
//  WINDING IS COMPUTED, NOT GUESSED
//  For every triangle the code works out its normal with a cross product and
//  checks it points AWAY from the middle of the shape. If it points inward,
//  two corners are swapped. So the outline can be listed clockwise or
//  anticlockwise and back-face culling still works.
//
//  alongY = true : outline is a FOOTPRINT (x,z), extruded UP from y=0 to y=1.
//                  Used for towers - the unit-shape rule again, base on y=0.
//  alongY = false: outline is a SIDE PROFILE (x,y), extruded across z from
//                  -0.5 to +0.5. Used for the car, whose outline is its
//                  silhouette seen from the side.
//
//  UVs on the side walls wrap around the outline (u = distance travelled
//  round the edge, as a fraction of the whole perimeter), so a window
//  texture flows continuously round an octagonal tower with no seams.
// ---------------------------------------------------------------------------
inline Mesh makeExtrusion(const std::vector<glm::vec2>& outline, bool alongY) {
    const int n = (int)outline.size();

    // Map an outline point + an end (0 or 1) into 3D.
    auto to3D = [&](const glm::vec2& p, float end) {
        return alongY ? glm::vec3(p.x, end, p.y)
                      : glm::vec3(p.x, p.y, end - 0.5f);
    };

    // Centre of the outline - used for the cap fans and the outward checks.
    glm::vec2 c2(0.0f);
    for (const glm::vec2& p : outline) c2 += p;
    c2 /= (float)n;
    glm::vec3 centre = to3D(c2, 0.5f);

    // Total perimeter, so the side UVs can run 0..1 all the way round.
    float perimeter = 0.0f;
    for (int i = 0; i < n; ++i) perimeter += glm::length(outline[(i + 1) % n] - outline[i]);

    std::vector<float> v;

    // Emit one triangle, fixing its winding so the normal points away from
    // 'inside' (a point known to be inside the solid).
    auto tri = [&](glm::vec3 a, glm::vec2 ua, glm::vec3 b, glm::vec2 ub,
                   glm::vec3 c, glm::vec2 uc, glm::vec3 inside) {
        glm::vec3 nrm = glm::normalize(glm::cross(b - a, c - a));
        glm::vec3 mid = (a + b + c) / 3.0f;
        if (glm::dot(nrm, mid - inside) < 0.0f) {      // pointing inward?
            std::swap(b, c); std::swap(ub, uc);        // flip the winding
            nrm = -nrm;
        }
        pushVert(v, a.x,a.y,a.z, 1,1,1, nrm.x,nrm.y,nrm.z, ua.x,ua.y);
        pushVert(v, b.x,b.y,b.z, 1,1,1, nrm.x,nrm.y,nrm.z, ub.x,ub.y);
        pushVert(v, c.x,c.y,c.z, 1,1,1, nrm.x,nrm.y,nrm.z, uc.x,uc.y);
    };

    // ---- side walls ----
    float walked = 0.0f;
    for (int i = 0; i < n; ++i) {
        const glm::vec2& p0 = outline[i];
        const glm::vec2& p1 = outline[(i + 1) % n];
        float u0 = walked / perimeter;
        walked  += glm::length(p1 - p0);
        float u1 = walked / perimeter;

        glm::vec3 a0 = to3D(p0, 0.0f), a1 = to3D(p1, 0.0f);
        glm::vec3 b0 = to3D(p0, 1.0f), b1 = to3D(p1, 1.0f);

        tri(a0, {u0,0}, a1, {u1,0}, b1, {u1,1}, centre);
        tri(a0, {u0,0}, b1, {u1,1}, b0, {u0,1}, centre);
    }

    // ---- the two caps, as fans from the centre ----
    for (int end = 0; end <= 1; ++end) {
        glm::vec3 cc = to3D(c2, (float)end);
        // The solid's centre is inside it, so each cap's normal must point
        // away from there: down for the bottom cap, up for the top one.
        glm::vec3 inside = centre;
        for (int i = 0; i < n; ++i) {
            const glm::vec2& p0 = outline[i];
            const glm::vec2& p1 = outline[(i + 1) % n];
            tri(cc, c2 + glm::vec2(0.5f), to3D(p0, (float)end), p0 + glm::vec2(0.5f),
                to3D(p1, (float)end), p1 + glm::vec2(0.5f), inside);
        }
    }
    return makeMesh(v, "extrusion");
}

// ---------------------------------------------------------------------------
//  OCTAGONAL TOWER  -  a unit square footprint with its corners cut off
//
//  CUT is how much is sliced off each corner. 0.2 of a 1-wide square leaves
//  flat faces 0.6 long and diagonal faces about 0.28 long - clearly octagonal
//  but still reading as a building rather than a cylinder.
// ---------------------------------------------------------------------------
static const float OCT_CUT = 0.2f;

inline std::vector<glm::vec2> octagonOutline() {
    const float h = 0.5f, c = OCT_CUT;
    return {
        { -h + c, -h }, {  h - c, -h }, {  h, -h + c }, {  h,  h - c },
        {  h - c,  h }, { -h + c,  h }, { -h,  h - c }, { -h, -h + c },
    };
}

// Perimeter of that outline, needed to tile windows at the right density.
inline float octagonPerimeter() {
    std::vector<glm::vec2> o = octagonOutline();
    float p = 0.0f;
    for (size_t i = 0; i < o.size(); ++i) p += glm::length(o[(i + 1) % o.size()] - o[i]);
    return p;
}

// ---------------------------------------------------------------------------
//  CAR BODY and CAR CABIN  -  two side silhouettes, extruded across the car
//
//  Coordinates are fractions of the car: x runs from the back (-0.5) to the
//  front (+0.5), y from the ground (0) to the roof (~0.74). The renderer
//  scales them to real size - a 3.6 long, 1.4 tall, 1.7 wide car.
//
//  WHY TWO PIECES AND NOT ONE?
//  A car's side view is NOT convex: the bonnet dips down in front of the
//  windscreen. A single fan from the centre would cut across that dip and
//  draw triangles in empty air. Splitting it into a lower BODY and an upper
//  CABIN makes each piece convex, and the cabin can be tinted as dark glass.
//
//      cabin:          ____
//                    /      \___
//      body:     ___/___________\__
//               |__________________|    <- raised 0.14 so the wheels show
// ---------------------------------------------------------------------------
inline std::vector<glm::vec2> carBodyOutline() {
    return {
        { -0.50f, 0.14f },   // bottom of the rear bumper
        {  0.50f, 0.14f },   // bottom of the front bumper
        {  0.50f, 0.36f },   // front bumper, top
        {  0.46f, 0.44f },   // front of the bonnet
        { -0.48f, 0.46f },   // rear of the boot lid
        { -0.50f, 0.38f },   // top of the rear bumper
    };
}

inline std::vector<glm::vec2> carCabinOutline() {
    return {
        { -0.36f, 0.45f },   // base of the rear window
        {  0.26f, 0.44f },   // base of the windscreen
        {  0.04f, 0.74f },   // top of the windscreen
        { -0.24f, 0.74f },   // top of the rear window
    };
}
