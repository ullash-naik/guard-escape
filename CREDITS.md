# Asset credits

All code is original and written in C. The game uses these third-party art assets.

## Characters (`assets/characters/`)

| File | Used for | Source | Terms |
|---|---|---|---|
| `Soldier.glb` | Guards, plus the idle/walk/run animations for both characters | three.js examples repository, `examples/models/gltf/Soldier.glb` (Mixamo character and animations) | Mixamo assets may be used royalty-free in projects. This copy comes from the three.js repo, so it is fine for this student project. Re-check Adobe's Mixamo terms before any commercial release. |
| `Michelle.glb` | Player | three.js examples repository, `examples/models/gltf/Michelle.glb` (Mixamo character) | Same as above. Her original clip is a dance; the walk, run and idle motion is copied from `Soldier.glb` at load time (`src/retarget.c`). |

Loading fixes applied in code, not by editing the files: the animation frames are moved into the rest-pose space (`anim_fix_space`), bone scale is set to 1 (`model_normalize_bind_scale`), and the animation is retargeted by bone name (`retarget_animation`).

## Textures (`assets/textures/`), CC0 from [Poly Haven](https://polyhaven.com)

Downloaded at 1k as JPG, then converted to 512×512 PNG.

| Texture | Used for | Authors |
|---|---|---|
| `terrazzo_tiles` (diffuse + normal) | Corridor floor | Amal Kumar |
| `beige_wall_001` (diffuse + normal) | Corridor walls | Dimitrios Savva, Rico Cilliers |
| `painted_metal_shutter` (diffuse + normal) | Lockers | Dario Barresi, Rico Cilliers, Charlotte Baglioni |
| `old_wood_floor` (diffuse + normal) | Doors, notice boards | Guillaume Monsergent |

CC0 means no attribution is required. We credit the authors anyway.

## Optional Mixamo characters

If `assets/characters/student.glb`, `guard.glb` and the `anim_*.glb` files exist, the game uses them instead. They are made by `tools/import_mixamo.bat` from FBX files the user downloads from mixamo.com with their own Adobe account. Mixamo's terms allow royalty-free use in projects but not redistributing the raw files. Don't upload `incoming/` or these `.glb` files publicly.

## Tools

`tools/FBX2glTF.exe`: Godot's fork of Facebook FBX2glTF v0.13.1, BSD licence (`tools/FBX2glTF-License.txt`). It includes the Autodesk FBX SDK.

## Library

[raylib](https://www.raylib.com) 5.5 by Ramon Santamaria, zlib licence (`vendor/raylib/LICENSE`).
