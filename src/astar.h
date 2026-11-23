/*
 * astar.h - A* (A-Star) Pathfinding algorithm using a Binary Min-Heap Priority Queue.
 *
 * Implemented for live side-by-side algorithmic comparison against classic BFS
 * in the DSA Experiential Learning project.
 *
 * Data structures used:
 *   - Binary Min-Heap Priority Queue keyed by f(n) = g(n) + h(n)
 *   - 2D Arrays for gScore, closed set, and parent reconstruction
 *   - Admissible Manhattan Distance Heuristic: h(n) = |dx| + |dy|
 */
#ifndef GE_ASTAR_H
#define GE_ASTAR_H

#include "maze.h"

typedef struct {
    int nodesExplored;       /* Number of nodes popped from min-heap */
    int peakHeapSize;        /* Maximum elements in binary min-heap */
    float timeMicroseconds;  /* Execution duration in microseconds */
    int pathLength;          /* Steps from start to goal */
    Cell nextStep;           /* The immediate next cell toward goal */
    int reachable;           /* 1 if path exists, 0 otherwise */
} AStarResult;

/* Computes A* search from start to goal over the given maze */
AStarResult astar_search(const Maze *m, Cell start, Cell goal);

#endif
