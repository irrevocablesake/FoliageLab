# Grass Field Simulation

<img src="https://raw.githubusercontent.com/irrevocablesake/FoliageLab/refs/heads/master/images/Simulation.png" width="100%">

A Grass Simulation Experiment written in **Vulkan**, based on - one of my previous [ project ]( https://irrevocablesake.github.io/Grass-Field-Simulation/ )

## Intuition
Fluids are everywhere, water, honey, air, well anything that flows falls in that category. The goal was to implement a **2D Fluid** Simulation using **C++** and **Vulkan**, and later expand into **3D Fluid** Simulation. All of this is based on **Jos Stam Real-Time Fluid Dynamics** Technique. 

So far with the progress, we start with a velocity field initialized Perlin Noise, using this velocity field we transport the density field. In between there are multiple passes to simulate proper flow of fluids with respect to laws of physics, since this is WIP - the physics is wonky but constant tweaks are being made so it becomes a better simulation.

# Simulation


https://github.com/user-attachments/assets/1af9db01-0a11-425a-a9c3-428b8964e038



## Resources
- [ Fluid Dynamics NVIDIA ](https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu)
- [ Fluid Simulation for Dummies ]( https://www.mikeash.com/pyblog/fluid-simulation-for-dummies.html )
- [ Coding Train - Fluid Simulation ]( https://www.youtube.com/watch?v=alhpH6ECFvQ )

