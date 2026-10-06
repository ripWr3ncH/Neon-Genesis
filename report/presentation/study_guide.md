# Neon Genesis — Study Guide for the Showcase

Simple explanations of everything the teacher may ask about, in the order of the teacher's checklist.
Read each **"Say it simply"** part out loud a few times; those are your answers.

---

## 0. The project in 30 seconds

> Neon Genesis is a rainy cyberpunk city in OpenGL 3.3. The whole city is **generated from one seed** (my roll number). It has **4 types of light**, **3 shading modes**, **animation from one clock**, lots of **keyboard interaction** including **driving a car**, and as a bonus, **ray-traced shadows**. It runs at about **77–160 FPS on my laptop's integrated graphics**.

---

## 1. Teacher's requirements → where they are in my project

| Requirement | In my project | Show it with |
|---|---|---|
| **Lighting** | Ambient, directional (sun/moon), point (neon, flying cars), spot (80 street lamps, headlights) | **E** (dusk ↔ night), **L** (light markers) |
| **Transformation** | Every object = unit shape × **T·R·S**; cars and lamps are hierarchical | Fly around, drive (**C**) |
| **Gouraud & Phong** (+ Flat) | Same lighting function in 3 places | **1 / 2 / 3** |
| **Motion / Animation** | Traffic, rain, signals, neon flicker, flying cars, sunset | **P** pauses everything |
| **Interaction** | 20+ keys: drive a car, toggle effects, camera tour, new city | **C, B, G, R, X, H, N, F, V** |
| **Ray tracing (bonus)** | Ray-traced shadows: one shadow ray per pixel | **H** (on/off) |
| **Proposal completion** | All proposed features done; only SSR was replaced by planar reflection (faster) | Report Table 4.1 |
| **Report** | Proposed vs implemented, how each works, formulas, references | `neon_genesis_report.tex` |

---

## 2. Transformation

**Say it simply:**
> "Every object starts as a **unit shape** (a 1×1×1 cube, a cylinder, a pyramid, a quad). I **scale** it to the right size, **rotate** it to face the right way, and **translate** it to its position. The model matrix is **M = T · R · S**. Then the view and projection matrices put it on the screen: **p_screen = P · V · M · p**."

**Key points:**
- **Order matters:** read it right to left, scale first, then rotate, then translate. If I translated first, the rotation would swing the object around the origin.
- **Hierarchical modelling:** a car is a parent matrix **T**. The wheels, lights and cabin are multiplied by **T**, so when the car moves, all its parts move with it.
- **Normal matrix:** when an object is stretched (e.g. a tall building), its normals would tilt wrongly, so I use **(M⁻¹)ᵀ**, the inverse-transpose of the model matrix.
- **Projection:** perspective, 62° field of view. **View:** `lookAt(eye, eye + front, up)`.

---

## 3. Lighting — the four light types

**Say it simply:**
> "I have **four light types**.
> **Ambient** is a small constant light everywhere, so nothing is pure black.
> **Directional** light has only a direction, no position: the **sun** at dusk and the **moon** at night.
> **Point** lights shine in all directions from a point: the **neon strips** and **flying cars**.
> **Spot** lights shine in a cone: the **80 street lamps** pointing down, and the **car headlights**."

### The formula (Phong model = ambient + diffuse + specular)

For each light:

- **Ambient:** `I_a = k_a` (a constant, 0.035 at night)
- **Diffuse (Lambert):** `I_d = max(N · L, 0)`
  → a surface facing the light is bright; one turned away gets nothing.
- **Specular (shiny highlight):**
  - Phong: `I_s = k_s · max(R · V, 0)^α`, with `R = 2(N·L)N − L` (the mirror direction)
  - **Blinn–Phong (what I use):** `I_s = k_s · max(N · H, 0)^α`, with `H = normalize(L + V)` (the halfway vector)
  → **α (shininess)** big = small sharp highlight (car paint 96), small = wide dull highlight (concrete 24).

**Total:**
```
I = ambient + Σ over lights [ (diffuse + specular) × lightColour × attenuation × spot ]
```

- **Attenuation** (light gets weaker with distance): `A = (1 − d/R)²`, which becomes exactly 0 at range R.
- **Spotlight:** `S = (cos θ)^e` inside the cone, 0 outside. θ is the angle from the beam centre, **cutoff** = cone size (58° for lamps), **exponent e** = how focused (3).

**Letters:** N = surface normal, L = direction to the light, V = direction to the eye, H = halfway vector, R = reflected light.

**Why Blinn–Phong?** "It is a small improvement on Phong. It is cheaper (no reflection vector) and looks better at grazing angles. The idea, ambient + diffuse + specular, is the same."

**Too many lights?** "There are 200+ lights, but the shader can only loop over 24 quickly. So every frame I **pick the 24 closest to the camera**."

---

## 4. Shading: Flat, Gouraud, Phong

**Say it simply:**
> "Shading means **where** I calculate the light.
> **Flat:** once per **triangle**, so each face has one colour and looks faceted.
> **Gouraud:** at each **vertex**, then the colours are **blended** across the triangle. It's cheap, but light that falls in the middle of a triangle is lost.
> **Phong:** the **normal** is blended across the triangle and the light is calculated at **every pixel**. It's the most accurate, so it's my default."

| | Where light is computed | Which shader | Result |
|---|---|---|---|
| Flat (key 1) | once per triangle | fragment shader, normal from `cross(dFdx(P), dFdy(P))` | faceted |
| Gouraud (key 2) | per vertex | **vertex shader** | smooth, but loses details |
| Phong (key 3) | per pixel | **fragment shader** | smooth + accurate |

**Gouraud blending formula:** `I = λ₁I₁ + λ₂I₂ + λ₃I₃` (barycentric weights).

**Best example to show:** a street lamp shining on the **middle of the big road**. In **Gouraud** the light pool is weak or missing, because the road's corners are far from the lamp. In **Phong** the pool is clearly there.

---

## 5. Motion / Animation

**Say it simply:**
> "Everything moves using **one clock**. Each frame the clock goes up by Δt (the frame time), so speed is the same on fast and slow computers. Pressing **P** stops the clock and everything freezes."

- **Cars:** keep right, keep a safe gap (slow down when the car ahead is close), **stop at red lights**, turn on a smooth **quarter-circle arc**. I tested 10 minutes with **0 collisions**.
- **Traffic lights:** 15-second cycle (green 5 s → amber 1.2 s → all red 1.3 s).
- **Rain:** 1,500 drops fall, lean with gusting wind, and make ripples and splashes. Splash droplets follow `p = p₀ + v₀t + ½gt²`.
- **Neon flicker, billboards** changing frames, **two flying cars** on circle/ellipse paths.
- **Sunset ↔ night** (key E): blends over 9 seconds; the lamps switch on one after another.
- **Camera tour** (key V): the camera follows a smooth **Catmull–Rom spline** through 12 points.

---

## 6. Interaction

**Say it simply:**
> "The user controls everything with the keyboard and mouse: fly the camera, **drive a car**, switch shading, and turn effects on and off."

| Key | Does |
|---|---|
| W A S D + mouse | fly the camera |
| **C** | get into a car and drive (W/S speed, A/D steer, SPACE handbrake) |
| 1 / 2 / 3 | Flat / Gouraud / Phong |
| **E** | dusk ↔ night |
| **H** | ray-traced shadows on/off |
| B / G / R / X | bloom / reflection / rain / detail effects |
| **N** | a completely new city (new seed) |
| F | wireframe (shows the triangles) |
| V | camera tour |
| P | pause |
| M / T | music on/off, next song |
| F11 / F12 | fullscreen / screenshot |

---

## 7. Ray tracing (bonus)

**Say it simply:**
> "Normal OpenGL draws triangles; it doesn't know if something is blocking the sun. So for **every pixel** I shoot **one ray toward the sun** (or moon). If the ray **hits a building**, the pixel is **in shadow**. This is called a **shadow ray**. It's **hybrid** rendering: the picture is rasterised, but the shadows are ray traced."

**How it works, step by step:**
1. All buildings are stored as **127 boxes** in a texture the shader can read.
2. From the pixel, start a ray a tiny bit off the surface (`P + 0.06·N`) so it doesn't hit its own wall ("shadow acne").
3. **Ray–box test (slab method):** for each axis, find where the ray enters and leaves the box:
   ```
   t1 = (box_min − P) / L,   t2 = (box_max − P) / L
   t_in  = largest of the min(t1, t2)
   t_out = smallest of the max(t1, t2)
   hit if t_out ≥ max(t_in, 0)
   ```
4. **Speed trick:** the city is a **4 × 4 grid**. The ray only checks boxes in the grid cells it passes through (**DDA walk**), not all 127.
5. **Proof it's correct:** with shadows on, **14.6% of pixels got darker and 0% got lighter**. A shadow can only remove light, so this is right.

**Demo:** press **E** for dusk (long sun shadows), then press **H** a few times.

---

## 8. Other effects (if asked)

- **Wet road reflection:** "I draw the city a **second time upside down** (scale y by −1) under the road. The road is half-transparent, so the upside-down city shows like a reflection. It reflects more at low angles: **Fresnel**, `F = (1 − N·V)⁵`."
- **Bloom (glow):** "Bright parts are saved in a second image, **blurred** with a Gaussian blur, and added back on top. The blur is done horizontally then vertically (**separable**), at half size, so it's fast."
- **Tone mapping:** "Light values can go above 1, but a screen can't show that, so I squeeze them with **Reinhard**: `c / (1 + c)`. Then gamma correction `c^(1/2.2)`."
- **Fog:** `f = 1 − e^(−(d·ρ)²)`: far things fade into the sky colour.
- **Textures:** "All textures (windows, asphalt, tiles, stone) are **generated in code**, no image files, except the billboard pictures."
- **Procedural city:** "A random-number generator (LCG) with seed **2107015** decides tower sizes, styles and neon. The same seed always gives the same city; **N** gives a new seed."

---

## 9. Proposal vs final project

> "Everything I proposed is done. One thing changed: I proposed **screen-space reflections**, but they were too slow on integrated graphics, so I used **planar (mirror) reflection** with Fresnel instead. I also added extra things: **ray-traced shadows, dusk-to-night, driving, the island and water, the camera tour, and the detail effects**."

---

## 10. If the teacher asks you to change a value (main ones only)

Change it, save, run `build.bat`, and show the effect. Find each one with **Ctrl+F**.

| Topic | File | Search for | Try | Effect |
|---|---|---|---|---|
| Ambient light | `src/main.cpp` | `U.ambient, glm::mix(0.035f` | 0.035 → 0.15 | whole night scene brighter |
| Street lamp (spot) | `src/main.cpp` | `0.45f) * 2.2f` | 2.2 → 5.0 (brightness); `58.0f` → 30 (cutoff) | brighter lamps / smaller light circles |
| Moon (directional) | `src/main.cpp` | `moonLight= glm::vec3(` | multiply by 3 | stronger blue moonlight |
| Shininess (specular) | `src/main.cpp`, in `drawCar` | `color, 0.9f, 96.0f` | 96 → 8 | car highlight becomes wide and dull |
| Camera speed | `src/main.cpp` | `float speed       = 16.0f` | 16 → 40 | fly faster |
| Field of view | `src/main.cpp` | `glm::radians(62.0f)` | 62 → 90 | wider view |
| Traffic light cycle | `src/traffic.h` | `LIGHT_CYCLE = 15.0f` | 15 → 30 | slower signals |

---

## 11. Quick answers to likely questions

- **What is a shader?** A small program that runs on the GPU. The **vertex shader** runs per vertex (positions, Gouraud light); the **fragment shader** runs per pixel (Phong light, textures, fog).
- **What is a normal?** A unit vector pointing straight out of a surface. Lighting uses it to know which way the surface faces.
- **What is the depth buffer?** It stores how far each pixel is, so near objects hide far ones.
- **What is back-face culling?** It skips triangles facing away from the camera, which saves time.
- **What is blending?** Mixing a new colour with what is already on screen using alpha; I use it for the wet road and the water.
- **Why are the lights limited to 24?** The fragment shader loops over every light for every pixel, so too many lights is too slow.
- **What did you follow?** The **LearnOpenGL** tutorials for the OpenGL structure, the papers of **Phong, Blinn and Gouraud** for the lighting, and **Whitted** and **Kay–Kajiya** for ray tracing; the full list is in the report's references. The base was the **lab starter code** (GLFW + GLAD + GLM).

---

## 12. Where is it in the code? (code map)

**How to jump:** in VS Code press **Ctrl+P**, type the file name, press Enter, then **Ctrl+G** and type the line number. Or press **Ctrl+F** and type the *search text*, which still works if the line numbers move after edits.

### The files, one sentence each

| File | What it does |
|---|---|
| `src/main.cpp` | Opens the window, reads keys/mouse, runs the frame loop, draws every object, chooses lights |
| `src/shaders.h` | **All GLSL shaders**: lighting, 3 shading modes, ray-traced shadows, fog, sky, bloom, tone mapping |
| `src/mesh.h` | Builds the shapes (cube, cylinder, cone, extrusion) and uploads them to the GPU |
| `src/city.h` | **Procedural city**: buildings, neon, lamps, rain, flying cars, from the seed |
| `src/traffic.h` | AI cars, traffic signals and the car you drive |
| `src/textures.h` | Procedural textures (windows, billboards, asphalt, tiles, stone) |
| `src/images.h` | Loads the billboard picture (PNG/JPG) with GDI+ |
| `src/raytrace.h` | Packs the 127 building boxes into a texture for the shadow rays |
| `src/render.h` | Framebuffers, bloom blur, compiling shaders |
| `src/menu.h` | The pause menu (ESC) |
| `src/music.h` | Background music |

### Transformation

| What | Where | Search text |
|---|---|---|
| Unit shapes (cube, cylinder, cone, extruded outline) | [mesh.h:199](../../src/mesh.h#L199), [275](../../src/mesh.h#L275), [331](../../src/mesh.h#L331), [509](../../src/mesh.h#L509) | `inline Mesh makeCube` |
| Model matrix → shader, **normal matrix (M⁻¹)ᵀ** | [main.cpp:371-377](../../src/main.cpp#L371-L377) | `glm::transpose(glm::inverse(model))` |
| Example of **T·R·S** (street lamp: translate, rotate, scale) | [main.cpp:1292](../../src/main.cpp#L1292) | `// ---- street lamps` |
| **Hierarchical** car (parent T × child parts) | [main.cpp:1386](../../src/main.cpp#L1386) | `auto drawCar =` |
| **View matrix** (lookAt) | [main.cpp:138](../../src/main.cpp#L138) | `glm::lookAt` |
| **Projection matrix** (62°) | [main.cpp:2208](../../src/main.cpp#L2208) | `glm::perspective(glm::radians(62.0f)` |
| Vertex shader: `P · V · M · position` | [shaders.h:304](../../src/shaders.h#L304) | `gl_Position = projection * view * worldPos` |

### Lighting

| What | Where | Search text |
|---|---|---|
| **The lighting function** (ambient + diffuse + specular, attenuation, spot cone) | [shaders.h:179](../../src/shaders.h#L179) | `vec3 computeLighting(` |
| Directional light (sun / moon) | [shaders.h:171](../../src/shaders.h#L171) | `vec3 directional(` |
| **Ambient** value | [main.cpp:2288](../../src/main.cpp#L2288) | `U.ambient, glm::mix` |
| **Sun** colour (directional) | [main.cpp:2223](../../src/main.cpp#L2223) | `glm::vec3 sunLight =` |
| **Moon** colour (directional) | [main.cpp:2224](../../src/main.cpp#L2224) | `moonLight= glm::vec3` |
| Moon direction | [main.cpp:512](../../src/main.cpp#L512) | `MOON_DIR =` |
| Point / spot light helpers | [main.cpp:2125](../../src/main.cpp#L2125) | `auto spotLight =` |
| **Street lamp = spotlight** (2.2 bright, range 22, 58°, exp 3) | [main.cpp:2147](../../src/main.cpp#L2147) | `consider(spotLight(head` |
| Choose the best 24 lights | [main.cpp:2105](../../src/main.cpp#L2105) | `LIGHTS - score every candidate` |
| Send lights to the shader | [main.cpp:475](../../src/main.cpp#L475) | `static void uploadLights` |

### Shading (Flat / Gouraud / Phong)

| What | Where | Search text |
|---|---|---|
| Keys 1 / 2 / 3 | [main.cpp:1881](../../src/main.cpp#L1881) | `EDGE(GLFW_KEY_1` |
| **Gouraud**: lighting in the vertex shader | [shaders.h:301](../../src/shaders.h#L301) | `vGouraud = computeLighting` |
| **Flat**: face normal from `dFdx × dFdy` | [shaders.h:540](../../src/shaders.h#L540) | `if (shadingMode == 0)` |
| **Phong**: lighting per pixel in the fragment shader | [shaders.h:635](../../src/shaders.h#L635) | `computeLighting(vFragPos, N, specS` |

### Ray tracing (bonus)

| What | Where | Search text |
|---|---|---|
| Building boxes packed into a texture | [raytrace.h:50](../../src/raytrace.h#L50) | `buildRayTraceScene` |
| **Shadow ray** (slab test + grid walk) | [shaders.h:127](../../src/shaders.h#L127) | `float traceShadow` |
| Where each pixel fires its ray | [shaders.h:629](../../src/shaders.h#L629) | `sunSh  = traceShadow` |
| Key H | in the key list | `EDGE(GLFW_KEY_H` |

### Motion / animation

| What | Where | Search text |
|---|---|---|
| **The one clock** | [main.cpp:1734](../../src/main.cpp#L1734) | `if (animate) sceneTime += deltaTime` |
| Traffic rules (gap, signals, turning) | [traffic.h:241](../../src/traffic.h#L241) | `inline void updateTraffic` |
| Traffic-light cycle (15 s) | [traffic.h:60](../../src/traffic.h#L60) | `LIGHT_CYCLE = ` |
| Brake 14 / accelerate 4 | [traffic.h:419](../../src/traffic.h#L419) | `brake 14, accelerate 4` |
| Rain movement + splashes | [main.cpp:2022](../../src/main.cpp#L2022) | `// ---- rain ---` |
| Sunset ↔ night blending | [main.cpp:1947](../../src/main.cpp#L1947) | `time of day glides` |
| Flying cars' paths | [city.h:783](../../src/city.h#L783) | `inline glm::vec3 flyerPosition` |
| Camera tour (Catmull–Rom spline) | [main.cpp:541](../../src/main.cpp#L541), [562](../../src/main.cpp#L562) | `static glm::vec3 catmullRom` |

### Interaction

| What | Where | Search text |
|---|---|---|
| Camera (WASD movement) | [main.cpp:1760](../../src/main.cpp#L1760) | `// ---- input ---` |
| Mouse look | [main.cpp:194](../../src/main.cpp#L194) | `static void mouse_callback` |
| **All toggle keys** (B, G, R, X, H, E, N, F, V…) | [main.cpp:1881](../../src/main.cpp#L1881) | `EDGE(GLFW_KEY_` |
| **Driving the car** (physics) | [traffic.h:492](../../src/traffic.h#L492) | `inline void updatePlayer` |
| Get in / out of the car (C) | [main.cpp:1832](../../src/main.cpp#L1832) | `auto toggleDriving =` |
| **Pause menu** (ESC) | [main.cpp:1703](../../src/main.cpp#L1703), [menu.h:41](../../src/menu.h#L41) | `auto openMenu` |

### City and textures

| What | Where | Search text |
|---|---|---|
| **Seed = roll number** | [main.cpp:887](../../src/main.cpp#L887) | `unsigned int seed = 2107015` |
| Random number generator (LCG) | [city.h:28](../../src/city.h#L28) | `struct Random` |
| **Generate the city** | [city.h:394](../../src/city.h#L394) | `inline void generateCity` |
| Wall colours (9 materials) | [city.h:410](../../src/city.h#L410) | `const glm::vec3 concrete` |
| Window / billboard / asphalt / tile / stone textures | [textures.h:132](../../src/textures.h#L132), [263](../../src/textures.h#L263), [359](../../src/textures.h#L359), [405](../../src/textures.h#L405), [439](../../src/textures.h#L439) | `inline GLuint make` |
| Upload texture (mipmaps, anisotropic) | [textures.h:56](../../src/textures.h#L56) | `inline GLuint uploadTexture` |
| Textures created at start-up | [main.cpp:863](../../src/main.cpp#L863) | `makeWindowTexture(1000u` |
| Shader reads the texture | [shaders.h:495](../../src/shaders.h#L495) | `texture(tex0, vUV)` |
| Billboard picture from file | [images.h:65](../../src/images.h#L65) | `inline bool loadImageTexture` |

### Effects

| What | Where | Search text |
|---|---|---|
| **Reflection**: city drawn upside down | [main.cpp:2318](../../src/main.cpp#L2318) | `REFLECTION - the city again` |
| Fresnel (wet road) | [shaders.h:693](../../src/shaders.h#L693) | `float fres = pow` |
| Fog | [shaders.h:750](../../src/shaders.h#L750) | `fogAmt = clamp` |
| Sky, moon, stars, clouds | [shaders.h:796](../../src/shaders.h#L796) | `static const char* SKY_FS` |
| **Bloom** blur | [render.h:260](../../src/render.h#L260), [shaders.h:974](../../src/shaders.h#L974) | `inline GLuint runBloomBlur` |
| **Tone mapping** (Reinhard) + gamma | [shaders.h:1085](../../src/shaders.h#L1085), [1097](../../src/shaders.h#L1097) | `Reinhard tone mapping` |
| Windows switching on/off | [shaders.h:639](../../src/shaders.h#L639) | `LIT WINDOWS, SWITCHING` |
| Puddles, bump mapping | [shaders.h:569](../../src/shaders.h#L569), [589](../../src/shaders.h#L589) | `PUDDLES` |
| Light beams | [main.cpp:2667](../../src/main.cpp#L2667) | `// ---- LIGHT BEAMS` |
| Water (moon glitter, foam) | [shaders.h:698](../../src/shaders.h#L698) | `OPEN WATER` |

> Line numbers are from the current version. If you edit the code above a line, it moves a little; use the **search text** then.
