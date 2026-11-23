# Guard Escape

A 3D stealth and combat maze game written in **C** with the raylib graphics library, designed for the DSA Experiential Learning project.

The game rules run on a high-performance 2D grid while `render3d.c` transforms that matrix into an immersive 3D college corridor at night.

---

## What's New in this Combined Version

This build unifies the best features from both development branches into one cohesive game:

1. **Difficulty System (from V2):**
   - **EASY:** 5 lives, fewer/slower guards, 6s stun, 3 darts / 1 flash grenade, 4 pickups/level, ×0.75 score multiplier.
   - **MEDIUM:** 3 lives, classic balanced guards, 4.5s stun, 2 darts / 1 flash grenade, 3 pickups/level, ×1.00 score multiplier.
   - **HARD:** 2 lives, more and faster guards with keen hearing, 3s stun, 1 dart / 0 flash grenades, 2 pickups/level, ×1.50 score multiplier.
   - Select difficulty with **LEFT/RIGHT** or **1 / 2 / 3** on the main menu.
   - Difficulty is tracked and persisted in the Binary Search Tree leaderboard.

2. **Handcrafted Maps & Tour Mode (from V2):**
   - **Random Maze:** Carved uniquely each level with randomized DFS and backtracking stack.
   - **Classrooms:** Rows of classrooms connected by dual long corridors.
   - **Library:** Long shelves and tall stacks breaking line-of-sight.
   - **Courtyard:** Two ring corridors surrounding an open courtyard.
   - **Offices:** Perimeter meeting rooms and inner cubicle maze.
   - **Tour:** Plays through each map in turn, level by level!
   - Select map with **UP/DOWN** on the menu; 3D background live-previews the chosen map.

3. **Weapons & Floor Pickups (from V2):**
   - **Stun Dart (`Z` / `1`):** Fires a projectile straight ahead (16 tiles/sec, range 9 tiles) that hits and stuns guards.
   - **Flash Grenade (`E` / `G` / `2`):** Detonates with a concussive blast, stunning every guard within 4 BFS steps (works around corners!).
   - **Guard Stun FX:** Stunned guards freeze in place, torch turns off, electric rings and stars spin around them, with a floating `zZ <timer>` countdown.
   - **Floor Pickups:** Blue ammo crates (+2 darts) and green grenades (+1 flash) bob and rotate on the floor and show on the minimap.

4. **Stealth Mechanics & Sanctuary Hiding (from V1):**
   - **Lockers (`SPACE` when near locker):** Emerald stealth lockers with neon status indicators. Hiding inside makes you 100% invisible and undetectable to patrolling or chasing guards.
   - **Distraction Coins (`Q`):** Toss a coin down corridors. The metallic clink alerts nearby patrolling guards to inspect the landing site.
   - **Sprint & Stamina (`Left Shift`):** Boost to 1.45× speed at the cost of stamina and louder footsteps.

5. **Pathfinding Dual-Mode & Viva Comparison (from V1):**
   - Toggle live with **`X`**: Compare **A\* Search** (using an explicit Binary Min-Heap Priority Queue and Manhattan heuristic) vs **BFS** (Circular Queue).
   - Real-time performance profiling shown on the footer (nodes visited, heap peak, runtime).

6. **Procedural Audio Engine (from V1 + V2 additions):**
   - Procedural sound synthesis in memory with zero external sound files: ambient menu pads, footsteps, heartbeats, danger drone, locker creaks, coin clinks, weapon fires, electric zaps, flash bangs, and pickup chimes.

7. **Retargeted 3D Animations & Visuals:**
   - Smooth in-place character animations (root horizontal motion stripped to eliminate drifting/clipping into walls).
   - 4 Camera views (`C`): Third-Person, Chase, First-Person, Overview.
   - Borderless Fullscreen toggle with `F11` or `Alt+Enter`.

---

## Running the Project

- **Play:** Run `GuardEscape.exe` or `build.bat run`
- **Recompile:** Run `build.bat`
- **Run DSA Unit Tests (30 tests):** Run `build.bat test`

Requirements: GCC (MinGW-w64 on Windows). Raylib is pre-bundled in `vendor/raylib`.

---

## Controls

| Key | Action |
|---|---|
| **WASD / Arrow Keys** | Move (First person: W/S forward/back, A/D turn 90°) |
| **Left Shift** | Sprint boost (1.45× speed, consumes stamina, louder footsteps) |
| **Z / 1** | Fire Stun Dart (flies straight, freezes guard) |
| **E / G / 2** | Throw Flash Grenade (AOE 4-step stun) |
| **Q** | Toss Distraction Coin (lures guards) |
| **SPACE** | Hide in Locker / Exit Locker (stealth sanctuary) |
| **M / V** | Toggle AI Map & BFS Queue Inspector dashboard |
| **B** | Replay BFS Wavefront expansion animation |
| **X** | Toggle AI Pathfinding: A* (Min-Heap Priority Queue) vs BFS (Circular Queue) |
| **C** | Cycle Camera: Third Person / Chase / First Person / Overview |
| **H** | Help & DSA System Architecture screen |
| **P** | Pause game |
| **ESC** | Open Menu / Pause (with Continue Run) |
| **F11 / Alt+Enter** | Toggle Borderless Fullscreen |
| **ESC** | Return to Menu / Quit |

---

## Data Structures & Algorithms Used

| Data Structure / Algorithm | File | Where It Appears in the Game |
|---|---|---|
| **2D Array** | `src/maze.c` | `grid[21][31]` stores the maze; `dist[21][31]` stores BFS distances |
| **Stack (Array-based)** | `src/maze.c` | Generates procedural mazes each level (randomized DFS with backtracking) |
| **Circular Queue** | `src/queue.c` | BFS frontier queue; FIFO on-screen message logging system |
| **Binary Min-Heap Priority Queue** | `src/astar.c` | Priority queue keyed by $f(n) = g(n) + h(n)$ for live $A^*$ pathfinding |
| **Breadth-First Search (Graph)** | `src/bfs.c` | Shortest path distance field from player; flash grenade blast routing |
| **Binary Search Tree** | `src/bst.c` | High-score leaderboard keyed by score with difficulty tier tracking; visual topology rendered on leaderboard screen |
| **File Persistence** | `src/bst.c` | BST saved in pre-order traversal to `leaderboard.txt` for identical tree reconstruction |

---

## Project Structure

```
guard-escape/
├── GuardEscape.exe     Compiled standalone executable
├── build.bat           Build / run / test script
├── src/
│   ├── game.h          Shared game state, difficulty configs, weapons & types
│   ├── main.c          Game loop, weapon mechanics, guard AI, UI menus & HUD
│   ├── render3d.c      3D world renderer: lighting, props, stun FX, pickups, cameras
│   ├── retarget.c      Mixamo skeletal retargeting & in-place motion filtering
│   ├── maze.c/.h       2D array generator, stack DFS, and 4 preset maps
│   ├── queue.c/.h      Circular queue implementation
│   ├── bfs.c/.h        Breadth-first search shortest path solver
│   ├── astar.c/.h      A* search with binary min-heap priority queue
│   ├── audio.c/.h      Procedural sound synthesizer (zero audio asset dependencies)
│   └── bst.c/.h        Leaderboard binary search tree with pre-order file I/O
├── assets/             3D models (.glb) and PBR textures (.png)
├── tests/test_ds.c     30 automated console tests for all data structures
├── tools/              Mixamo import batch scripts and probe utilities
└── vendor/raylib/      Raylib C headers and static libraries
```
