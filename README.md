# Guard Escape 3D 🎮

[![C99](https://img.shields.io/badge/Language-C99-00599C.svg)](https://en.wikipedia.org/wiki/C99)
[![Raylib](https://img.shields.io/badge/Graphics-Raylib_5.0-white.svg)](https://www.raylib.com/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/Platform-Windows-blue.svg)](https://www.microsoft.com/windows)

A 3D stealth maze escape game written in **C** using the **Raylib** engine, engineered for the **Data Structures & Algorithms (DSA) Experiential Learning (PBL)** project at RVCE.

The game transforms a dynamic 2D discrete grid into a fully rendered 3D college corridor at night, featuring intelligent AI guards powered by live graph search, procedural audio synthesis, and interactive visual data structure inspections.

---

## 🚀 Quick Start (Play on Any Windows Laptop)

No compiler or dependencies required! The game is pre-compiled and statically linked.

1. **Download this repository:**
   - Click the green **Code** button at the top right, then click **Download ZIP**.
   - Extract the ZIP folder to your computer.
   *(Or run `git clone https://github.com/ullash-naik/guard-escape.git`)*
2. **Launch the game:**
   - Double-click **`play.bat`** (or **`GuardEscape.exe`**).
   - Enjoy the game!

---

## 🕹️ Game Controls

| Key | Action |
|---|---|
| **WASD** / **Arrow Keys** | Walk / Navigate corridors |
| **Left Shift** | **Sprint** (1.45× speed, consumes stamina, louder footsteps) |
| **E** / **Q** | **Toss Distraction Coin** (lures nearby patrolling guards with audio clink) |
| **SPACE** | **Hide / Exit Locker** (100% invisible sanctuary from guards) |
| **C** | Cycle 3D Camera: *Third-Person* ➔ *Chase* ➔ *First-Person* ➔ *Overview* |
| **X** | **Toggle AI Pathfinding:** A* (Binary Min-Heap) vs BFS (Circular Queue) |
| **V** | Toggle BFS Distance Overlay & live guard path tracing |
| **B** | Replay animated BFS Wavefront expansion |
| **F** | Toggle corridor lantern & ambient lighting |
| **P** | Pause game |
| **L** | Open Interactive BST Leaderboard & Architecture Visualizer |
| **H** | Help & In-game controls guide |
| **F11** / **Alt+Enter** | Toggle Borderless Fullscreen |
| **ESC** | Main Menu / Pause |

---

## 🎯 Gameplay & Objective

1. **Find All Gold Keys:** Navigate the procedurally generated labyrinth to find all scattered keys. Keys are systematically distributed across different wings of the maze.
2. **Evade Guard Patrols:** Guards project a visible 3D flashlight cone. Entering their line of sight triggers a high-speed chase accompanied by intense danger audio.
3. **Use Stealth Sanctuaries:**
   - **Lockers:** Distributed evenly across corridor rows with glowing green status indicators. Step in front and press `SPACE` to vanish completely from guard detection.
   - **Distraction Coins:** Throwing a coin (`E`/`Q`) produces a metallic clink that causes patrolling guards to investigate the landing site.
4. **Reach the Exit:** Once all keys are collected, the exit gate opens with a glowing green beacon. Reach it to clear the level!

---

## 🧠 Data Structures & Algorithms Used (College PBL / Viva)

Every core gameplay system is powered by classic computer science data structures implemented from scratch in pure C:

| Data Structure / Algorithm | Source File | Real-world Purpose in the Game | Time / Space Complexity |
|---|---|---|---|
| **2D Array Matrix** | `src/maze.c` | Stores maze occupancy (`grid[21][31]`) and distance fields (`dist[21][31]`) | $O(1)$ lookup, $O(W \times H)$ space |
| **Stack (Array-based)** | `src/maze.c` | Procedural maze generation via randomized Depth-First Search (DFS) with backtracking | $O(V + E)$ generation time |
| **Circular Queue** | `src/queue.c` | FIFO frontier queue for BFS traversal; ring-buffer for the on-screen HUD combat event log | $O(1)$ push / pop, no reallocations |
| **Breadth-First Search (BFS)** | `src/bfs.c` | Computes exact shortest distance fields from player to all accessible tiles for guard pathfinding | $O(V + E)$ optimal shortest path |
| **A\* Pathfinding** | `src/astar.c` | Heuristic goal-directed pathfinding using Manhattan distance $f(n) = g(n) + h(n)$ | Fast goal search, heuristic pruning |
| **Binary Min-Heap** | `src/astar.c` | Priority queue managing active open-set frontier nodes for the $A^*$ search engine | $O(\log N)$ insert / extract-min |
| **Binary Search Tree (BST)** | `src/bst.c` | Manages the dynamic high-score leaderboard keyed by score; visualizes live tree topology | $O(\log N)$ average insertion / search |
| **Reverse In-Order Traversal** | `src/bst.c` | Recursively traverses the BST (Right ➔ Root ➔ Left) to generate descending rank orders | $O(N)$ linear-time ranking |
| **Pre-Order File Serialization** | `src/bst.c` | Saves the BST structure to `leaderboard.txt` and reconstructs an identical tree on startup | $O(N)$ persistence |

---

## 🛠️ Building from Source (For Developers)

If you want to modify the source code or run the DSA unit tests:

### Prerequisites
- Windows 10 / 11
- GCC with MinGW-w64 (Raylib 5.0 headers and pre-built static libraries are bundled in `vendor/raylib/`)

### Compilation
Open PowerShell or CMD in the project directory:

```bat
:: Build the game executable (GuardEscape.exe)
build.bat

:: Build and immediately launch the game
build.bat run

:: Run the automated DSA Unit Test Suite (24 test assertions)
build.bat test
```

---

## 📁 Repository Structure

```text
guard-escape/
├── assets/                  # 3D character models (.glb) and PBR textures
│   ├── characters/          # Mixamo student, guard, and retargeted animation clips
│   └── textures/            # Terrazzo tiles, walls, and PBR normal maps
├── src/                     # Pure C source code
│   ├── astar.c / astar.h    # A* search with Binary Min-Heap Priority Queue
│   ├── audio.c / audio.h    # Procedural sound synthesis engine
│   ├── bfs.c   / bfs.h      # Graph Breadth-First Search distance fields
│   ├── bst.c   / bst.h      # Binary Search Tree high score ranking engine
│   ├── game.h               # Global game state and configuration
│   ├── main.c               # Game loop, input handling, HUD, and UI screens
│   ├── maze.c  / maze.h     # Randomized DFS maze generator
│   ├── queue.c / queue.h    # Circular queue implementation
│   ├── render3d.c           # Raylib 3D rendering pipeline and shaders
│   └── retarget.c           # Skeletal animation retargeting system
├── tests/
│   └── test_ds.c            # Comprehensive unit tests for Queue, BFS, BST, and A*
├── vendor/raylib/           # Pre-bundled Raylib 5.0 headers and Windows static library
├── build.bat                # Build script for Windows
├── play.bat                 # One-click game launcher for end users
├── GuardEscape.exe          # Ready-to-play pre-compiled game executable
└── leaderboard.txt          # Persistent BST serialized leaderboard data
```

---

## 🏆 Project Credits

- **Developer:** Ullash Naik ([@ullash-naik](https://github.com/ullash-naik))
- **Institution:** RV College of Engineering (RVCE), Department of Computer Science & Engineering
- **Subject:** Data Structures & Algorithms (DSA in C) Experiential Learning Project
- **Graphics Engine:** [Raylib](https://www.raylib.com/) by Ramon Santamaria (@raysan5)
