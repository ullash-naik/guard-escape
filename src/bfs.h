/*
 * bfs.h - Breadth-First Search over the maze using the circular queue.
 *
 * We run ONE BFS starting from the player every time the player moves.
 * That gives the shortest-path distance from the player to every tile
 * (a "distance field"). Each guard then simply steps to a neighbouring
 * tile whose distance is one smaller - which is always a shortest path.
 *
 * Time complexity: O(V + E) = O(rows * cols) per BFS.
 */
#ifndef GE_BFS_H
#define GE_BFS_H

#include "maze.h"
#include "queue.h"

#define BFS_UNREACHED (-1)

typedef struct {
    int  dist[MAZE_H][MAZE_W];       /* shortest distance from source */
    Cell order[MAZE_W * MAZE_H];     /* cells in the order they were dequeued */
    int  orderCount;                 /* number of cells visited */
    int  maxDist;                    /* farthest reachable distance */
    int  maxQueue;                   /* peak queue size during the search */
    int  enqueued;                   /* total enqueue operations */
} BfsField;

void bfs_compute(const Maze *m, Cell source, BfsField *out);

/* One step from `from` toward the BFS source along a shortest path.
 * Returns `from` itself if it is unreachable or already the source. */
Cell bfs_step_toward_source(const Maze *m, const BfsField *f, Cell from);

#endif
