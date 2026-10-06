Neon Genesis 
2107015 
Dewan Salman Rahman Zisan 

1. What the Project Is About 
The project is a real-time OpenGL simulation of a cyberpunk city block at night, rendered from a camera 
that can be flown through the streets. The city is not modelled by hand: a seeded procedural generator 
lays out the street grid, then extrudes building footprints to random heights and assigns each facade a 
neon colour, so a different city is produced for every seed while the layout stays reproducible. 
The scene is driven by a single global clock. From that one time value the simulation derives vehicle 
positions along their street paths, the current frame of every animated billboard, the flicker of failing neon 
signs, the traffic-light cycle and the fall of the rain, so the whole city animates without any pre-recorded 
keyframes. Wet asphalt reflects the neon above it, which is what carries the high-contrast look of the 
setting. 

2. Objects in the Scene (Point-wise) 
• Ground plane and street grid : static, subdivided quad mesh; wet asphalt receives reflections 
• Buildings :  procedurally extruded boxes at randomised heights; static once generated 
• Facade windows : emissive quads instanced over each building; switch on and off over time 
• Neon signs : emissive strips and tubes on the facades; several flicker on a noise signal 
• Animated billboards : large screens cycling through scrolling texture frames 
• Street lights : pole, arm and emissive lamp head; each is a spotlight cone on the road 
• Traffic lights : three emissive lamps switching on a fixed red/amber/green cycle 
• Vehicles : bodies built from scaled cubes, translating along street paths with headlights and tail lights 
• Flying vehicle : follows a raised circular path above the rooftops as a moving light source 
• Rain : point/line particle system falling continuously and recycling at the top 
• Skybox : static gradient backdrop enclosing the city 

3. Extra Features 
• Procedural city generation from a seed - street grid, building heights and neon palette are computed, 
not hand-placed, so the city is regenerated on demand and reproducible 
• Single-clock animation - vehicle motion, billboard frames, neon flicker, traffic-light state and rainfall 
are all derived from one time value each frame, with no keyframes 
• Many light sources  - street lamps as spotlights with cutoff, spot exponent and distance attenuation, 
plus vehicle headlights as moving lights, over full ambient, diffuse and specular terms 
• Emissive neon materials with a bloom pass, giving the glow that defines the cyberpunk look 
• Screen-space reflections on wet roads and puddles, so the neon is mirrored in the street surface 
• Three switchable shading modes - flat, Gouraud and per-pixel Phong via a custom GLSL shader 
• Distance fog and alpha blending for depth; hidden-surface removal via depth buffering and back