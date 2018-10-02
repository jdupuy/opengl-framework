# Implicit Subdivision on the GPU

This demo provides additional implementations for the article 
"Adaptive GPU Tessellation with Compute Shaders" by Jad Khoury, Jonathan Dupuy (myself) and 
Christophe Riccio; the paper is available on my website: <http://onrendering.com>.

Specifically, the demo provides up to 4 rendering techniques:

* *Compute Shader* -- Similar to that of the original article; original demo available here: https://github.com/jadkhoury/TessellationDemo

* *Tesselation Shader* -- Single-pass rendering with tessellation shaders; the tessellation shader produces a grid with *fixed* 
tessellation factors

* *Geometry Shader* -- Single-pass rendering with a geometry shader; the geometry shader produces a grid with *fixed* 
tessellation factors

* *Mesh Shader* -- Single-pass rendering with a task and mesh shader (Turing GPUs only); the mesh shader produces a grid 
with *fixed* tessellation factors.

The demo loads a 16-bit displacement map and allows the user to play with the subdivision parameters.
This source code is released in order to facilitate adoption by developpers.


