/*
 * maze.h - The maze is a plain 2D ARRAY of tiles.
 *
 * DSA used here:
 *   - 2D array  : grid[MAZE_H][MAZE_W] stores every tile (wall or floor)
 *   - Stack     : an array used as a stack drives maze generation
 *                 (iterative "recursive backtracker" / randomized DFS)
 */
#ifndef GE_MAZE_H
#define GE_MAZE_H

#define MAZE_W 31          /* columns (must be odd) */
#define MAZE_H 21          /* rows    (must be odd) */

#define TILE_FLOOR 0
#define TILE_WALL  1

/* One grid position. Used everywhere: player, guards, keys, BFS queue. */
typedef struct {
    int x;
    int y;
} Cell;

typedef struct {
    unsigned char grid[MAZE_H][MAZE_W];
} Maze;

/* Direction vectors: up, right, down, left */
static const int DIR_X[4] = { 0, 1, 0, -1 };
static const int DIR_Y[4] = { -1, 0, 1, 0 };

/* Builds a random perfect maze, then knocks down `loops` extra walls so
 * the player has more than one route to escape a guard. */
void maze_generate(Maze *m, int loops);

/* Hand-made maps, stored as ASCII art in maze.c (# wall, . floor).
 * Every map has the start at (1, 1) and the exit at (MAZE_W-2, MAZE_H-2). */
#define MAZE_PRESET_COUNT 4

/* Copies preset map `index` (0 .. MAZE_PRESET_COUNT-1) into the 2D array.
 * Returns 0 if the index is out of range. */
int maze_load_preset(Maze *m, int index);

/* 1 if (x, y) is inside the maze and is a floor tile. */
int maze_walkable(const Maze *m, int x, int y);

#endif
