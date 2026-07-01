# External Research (web / theory)

The web half of engine research. Use it to understand the *technique or API* behind a change, then map it onto what VertexForge already does. **Never recommend a technique without first checking the codebase doesn't already implement it.**

## When to go external

Go to the web when the question is about knowledge that doesn't live in this repo:

- **Vulkan API semantics** - exact behavior/synchronization/validity of an API (barriers, descriptor indexing, dynamic rendering, VUIDs, queue families).
- **Algorithm / technique comparison** - choosing or understanding a rendering/physics/AI technique (VSM vs RT shadows, Surface Nets vs Marching Cubes, FSR vs DLSS, navmesh generation).
- **Library capabilities / version behavior** - what a vendored lib supports, API changes, known bugs.

Stay **in-codebase** (use [investigation-playbook.md](investigation-playbook.md)) for anything about *how VertexForge wires it up* - that is always more authoritative than a generic article.

## Library landscape

What to search for each major dependency (full list in `CLAUDE.md`):

| Library | Search for |
|---------|-----------|
| Vulkan + shaderc | Spec/VUID semantics, dynamic rendering, descriptor indexing, sync2 barriers |
| JoltPhysics | Body/constraint/motor API, ragdoll, character controller, version notes |
| EnTT | Registry/view/group semantics, signal/observer, snapshot serialization |
| recastnavigation | Navmesh build params, tiled navmesh, detour crowd/off-mesh links |
| meshoptimizer | Meshlet/LOD generation, vertex/index optimization |
| NVIDIA Streamline / DLSS | DLSS-SR/FG/Reflex integration, motion vectors, depth/exposure inputs |
| AMD FidelityFX (FSR/FFX) | FSR upscale/frame-gen integration, required resources, known crashes |
| mType + asmjit | Language/JIT semantics - but source is local at `C:\matan\mType`, prefer reading it if accessible |
| GLM | Math conventions (note engine uses `GLM_FORCE_DEPTH_ZERO_TO_ONE`, Vulkan [0,1] depth) |
| Jolt/recast/EnTT version | Check the submodule/vendored version before trusting docs |

For mType specifically: the language repo may be local at `C:\matan\mType`; read `docs/` and `tests/testFiles/` there rather than searching the web when it is accessible.

## Query patterns

1. **Official spec/docs first.** For a known doc URL, open the exact official page before doing a broad search. Vulkan -> `registry.khronos.org` / `docs.vulkan.org`; libraries -> their GitHub README/wiki/issues.
2. **Then technique write-ups** - GPU conference talks, blog posts, papers - when you need the *why*, not just the API.
3. **Check the vendored version** before trusting any API claim - the repo may pin an older release with different behavior.
4. **Cite the source URL** for every external finding in the Output Template.

## Map findings back to the codebase

Every external finding must end with one line: *"VertexForge already does X in `<file:line>`"* or *"not present; would be new."* This prevents recommending what already exists and grounds theory in the actual implementation. Cross-check against current code and any accessible prior notes; many techniques may already be implemented or in progress.
