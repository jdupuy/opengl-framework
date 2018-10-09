# Implicit Subdivision on the GPU

![alt text](preview.png "Preview")

This demo provides additional implementations for the article 
"Adaptive GPU Tessellation with Compute Shaders" by 
[Jad Khoury](https://github.com/jadkhoury), 
[Jonathan Dupuy](http://onrendering.com/) (myself) and 
[Christophe Riccio](https://github.com/g-truc); 
the paper is available on my website: <http://onrendering.com>.

Many thanks to [Cyril Crassin](https://twitter.com/Icare3D) for 
helping me putting this demo up (the mesh shader pipeline exists thanks to 
his help). 

Specifically, the demo provides up to 4 rendering techniques:

* *Compute Shader* -- Similar to that of the original article; original demo available here: https://github.com/jadkhoury/TessellationDemo

* *Tessellation Shader* -- Single-pass rendering with tessellation shaders; the tessellation shader produces a grid with *fixed* 
tessellation factors

* *Geometry Shader* -- Single-pass rendering with a geometry shader; the geometry shader produces a grid with *fixed* 
tessellation factors

* *Mesh Shader* -- Single-pass rendering with a task and mesh shader (Turing GPUs only); the mesh shader produces a grid 
with *fixed* tessellation factors. 

The demo loads a 16-bit displacement map and allows the user to play with the subdivision parameters.
This source code is released in order to facilitate adoption by developpers.


