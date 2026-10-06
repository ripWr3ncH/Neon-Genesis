# Neon Genesis — Presentation Speech

**Time:** about 3.5 minutes for the slides, plus about 2 minutes 10 seconds while the demo video plays.
**Tip:** you don't have to say it word for word. Learn the **bold key points** on each slide; the sentences around them are there to help you.

---

## Slide 1 — Title  (≈ 20 s)

Assalamu Alaikum, respected teachers. I am **Dewan Salman Rahman Zisan, roll 2107015**.
My project is **Neon Genesis**, a **procedurally generated, real-time cyberpunk city** made with **OpenGL 3.3**, with **ray-traced shadows**.

## Slide 2 — Introduction  (≈ 35 s)

Neon Genesis is a rainy cyberpunk island city that you can **fly through, drive in, and watch change from sunset to night**.
The whole city is **generated from one seed**, my roll number, so the same seed always builds the same city.
Everything moves on **one clock**: cars obey traffic signals, rain falls, neon flickers and the sun sets.

It covers every course requirement: **four light types, transformations, flat, Gouraud and Phong shading, animation and interaction**. As a bonus it has **ray tracing**.
It has **53 towers, 80 street lamps and 18 AI cars**, and it runs at **77 to 160 FPS on my laptop's integrated graphics**.

## Slide 3 — Demo video  (≈ 2 min 10 s, see the video script below)

Now I will show the demo. The captions show what each part is, and I will explain while it plays.
*(Play the video. Follow the **Video Script** section.)*

## Slide 4 — Geometry & transformation  (≈ 25 s)

Every object is built from a **few unit primitives**: cube, cylinder, pyramid, quad, and extruded 2D outlines.
Each one is placed by its **model matrix, M = T · R · S**, then **P · V · M** takes it to the screen.
Cars, lamps and districts are **hierarchical**: move the parent and all parts follow.
For normals I use the **inverse-transpose matrix**, so scaling does not break the lighting.

## Slide 5 — Lighting  (≈ 30 s)

There are **four light types**. **Ambient** keeps dark walls visible. **Directional** is the sun at dusk and the moon at night. **Point** lights are neon and the flying cars. **Spot** lights are the 80 street lamps and the car headlights.
I use **Blinn–Phong**: **diffuse = max(N·L, 0)** and **specular = max(N·H, 0) to the power alpha**, with **attenuation** and a **spotlight cone**.
There are over 200 lights, so each frame I keep the **best 24, the closest to the camera**.

## Slide 6 — Shading  (≈ 25 s)

Keys **1, 2 and 3** switch shading. **Flat** uses one normal per triangle, so every face has one tone.
**Gouraud** lights the vertices and blends the colour, so it can lose a lamp's light in the middle of a big road.
**Phong** lights every pixel, which gives the most accurate result, so it is the default.

## Slide 7 — Ray tracing (bonus)  (≈ 30 s)

For the bonus I added **ray-traced shadows**. Every pixel sends **one shadow ray toward the sun or moon**.
The buildings are stored as **127 boxes**, and I test the ray with the **slab method**.
To make it fast, the rays walk a **4 × 4 grid** and only check the blocks they pass through.
On the left the shadows are on, on the right they are off with **key H**. I checked it: **14.6% of pixels got darker, and none got lighter**, which is correct, because a shadow can only remove light.

## Slide 8 — Motion & interaction  (≈ 25 s)

The traffic follows rules: **keep right, keep a safe gap, stop at red lights**, and turn on smooth arcs. I tested it for **10 minutes with 0 collisions**.
Press **C** to drive a car with a **chase camera**, **V** for a camera tour, and **E** for dusk and night.

## Slide 9 — Surfaces & effects  (≈ 25 s)

The wet road is a **mirror reflection** of the city with a **Fresnel effect**, so it reflects more at low angles.
The glow is **bloom**: a **Gaussian blur at half resolution**, which is fast.
All textures are **generated in code**.
Key **X** turns the detail layer on and off: **bump-mapped walls, puddles, light beams and car reflections**.

## Slide 10 — Thank you  (≈ 10 s)

To summarise: **lighting, transformations, shading, motion, interaction and ray tracing are all done**.
**Thank you. I am happy to answer your questions.**

---

## Video script — say this while the demo plays (2:10)

Look at the caption and say the matching line. Short pauses are fine.

| Time | What is on screen | What to say |
|---|---|---|
| 0:00 | Sunset over the island | "This is the island at sunset. The sun is a **directional light**, and the sea reflects the city using **Fresnel**." |
| 0:10 | Flying over the city | "The city is **procedural**: 53 towers from one seed, each placed with **T·R·S**." |
| 0:20 | Long shadows between towers | "These are the **ray-traced shadows**: one shadow ray per pixel toward the sun." |
| 0:30 | Night falls | "Now night falls. The sky changes and the **80 street lamps switch on** one after another." | 
| 0:40 | Neon streets | "Here is **Blinn–Phong lighting** from the lamps, neon and cars, the 24 nearest lights each frame." |
| 0:50 | Glowing signs | "The glow is **bloom**: bright parts are blurred and added back." |
| 0:58 | Street level | "Now I control it myself with the keyboard." |
| 1:01 | Flat → Gouraud → Phong | "**Flat**, one colour per face… **Gouraud**, per vertex… and **Phong**, per pixel." |
| 1:10 | Bloom off/on | "Key **B** turns bloom off… and on." |
| 1:15 | Reflection off/on | "Key **G** turns the wet-road reflection off… and back on." |
| 1:20 | Detail off/on | "Key **X**: the detail layer: puddles, bump mapping, light beams." |
| 1:25 | Rain off/on | "Key **R** turns the rain off and on. 1,500 drops with splashes." |
| 1:29 | Sky turns orange | "Key **E** brings back the sunset in 9 seconds." |
| 1:38 | Shadows off/on | "Key **H** turns the ray-traced shadows off… and on again." |
| 1:43 | Driving | "Key **C**: now I drive a car. The headlights are spotlights and the camera follows smoothly." |
| 1:50 | City changes | "Key **N** generates a **completely new city** from a new seed." |
| 1:53 | Wireframe | "And key **F** shows the wireframe, all the triangles." |
| 1:57 | Camera pulls back over the island | "And to finish, the camera pulls back over the island at night. The **moonlight glitters on the waves**." |
| 2:04 | Final view with the moon | "That is Neon Genesis." *(go to slide 4)* |

---

## Likely teacher questions (short answers)

- **What is the difference between Gouraud and Phong?** Gouraud computes light at the vertices and blends the colour; Phong blends the normal and computes light at every pixel, so it is more accurate.
- **Why Blinn–Phong instead of Phong?** The half vector H is cheaper than the reflection vector R and looks better at grazing angles.
- **How do you change a light value?** In `src/main.cpp`, for example the street lamp is `spotLight(... 2.2f ..., 22.0f, ..., 58.0f, 3.0f)`: intensity 2.2, range 22, cutoff 58°, exponent 3.
- **Is it real ray tracing?** It is hybrid: the image is rasterised, and the shadows are real rays intersected with boxes in the shader.
- **Why is the road a mirror?** The city is drawn again flipped upside down, and the road is half-transparent over it, more transparent at low angles (Fresnel).
