/*
 * astar.c - A* Search with Binary Min-Heap Priority Queue.
 */
#include "astar.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HEAP_CAP (MAZE_W * MAZE_H * 2)

typedef struct {
    Cell cell;
    int f;   /* f = g + h */
    int g;   /* exact cost from start */
} HeapNode;

typedef struct {
    HeapNode data[HEAP_CAP];
    int size;
    int peakSize;
} MinHeap;

static void heap_init(MinHeap *h)
{
    h->size = 0;
    h->peakSize = 0;
}

static void heap_push(MinHeap *h, Cell c, int f, int g)
{
    if (h->size >= HEAP_CAP - 1)
        return;
    int i = h->size++;
    h->data[i] = (HeapNode){ c, f, g };
    if (h->size > h->peakSize)
        h->peakSize = h->size;

    /* Sift up */
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h->data[i].f < h->data[p].f) {
            HeapNode tmp = h->data[i];
            h->data[i] = h->data[p];
            h->data[p] = tmp;
            i = p;
        } else {
            break;
        }
    }
}

static HeapNode heap_pop(MinHeap *h)
{
    HeapNode root = h->data[0];
    h->data[0] = h->data[--h->size];
    int i = 0;

    /* Sift down */
    while (1) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int smallest = i;

        if (left < h->size && h->data[left].f < h->data[smallest].f)
            smallest = left;
        if (right < h->size && h->data[right].f < h->data[smallest].f)
            smallest = right;

        if (smallest != i) {
            HeapNode tmp = h->data[i];
            h->data[i] = h->data[smallest];
            h->data[smallest] = tmp;
            i = smallest;
        } else {
            break;
        }
    }
    return root;
}

static inline int manhattan(Cell a, Cell b)
{
    return abs(a.x - b.x) + abs(a.y - b.y);
}

AStarResult astar_search(const Maze *m, Cell start, Cell goal)
{
    clock_t t0 = clock();
    AStarResult res = { 0 };
    res.nextStep = start;

    if (start.x == goal.x && start.y == goal.y) {
        res.reachable = 1;
        res.timeMicroseconds = (float)((double)(clock() - t0) / CLOCKS_PER_SEC * 1e6);
        return res;
    }

    if (!maze_walkable(m, start.x, start.y) || !maze_walkable(m, goal.x, goal.y)) {
        res.timeMicroseconds = (float)((double)(clock() - t0) / CLOCKS_PER_SEC * 1e6);
        return res;
    }

    static int gScore[MAZE_H][MAZE_W];
    static unsigned char closed[MAZE_H][MAZE_W];
    static Cell parent[MAZE_H][MAZE_W];
    static MinHeap heap;

    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            gScore[y][x] = 1000000;
            closed[y][x] = 0;
            parent[y][x] = (Cell){ -1, -1 };
        }
    }

    heap_init(&heap);
    gScore[start.y][start.x] = 0;
    heap_push(&heap, start, manhattan(start, goal), 0);

    int found = 0;
    while (heap.size > 0) {
        HeapNode cur = heap_pop(&heap);
        Cell c = cur.cell;

        if (closed[c.y][c.x])
            continue;
        closed[c.y][c.x] = 1;
        res.nodesExplored++;

        if (c.x == goal.x && c.y == goal.y) {
            found = 1;
            break;
        }

        for (int d = 0; d < 4; d++) {
            int nx = c.x + DIR_X[d];
            int ny = c.y + DIR_Y[d];

            if (!maze_walkable(m, nx, ny) || closed[ny][nx])
                continue;

            int tentativeG = cur.g + 1;
            if (tentativeG < gScore[ny][nx]) {
                gScore[ny][nx] = tentativeG;
                parent[ny][nx] = c;
                int f = tentativeG + manhattan((Cell){ nx, ny }, goal);
                heap_push(&heap, (Cell){ nx, ny }, f, tentativeG);
            }
        }
    }

    res.peakHeapSize = heap.peakSize;
    res.reachable = found;

    if (found) {
        /* Reconstruct path backwards from goal to start */
        Cell curr = goal;
        int len = 0;
        Cell step = goal;

        while (!(curr.x == start.x && curr.y == start.y)) {
            Cell p = parent[curr.y][curr.x];
            if (p.x == -1 && p.y == -1)
                break;
            step = curr; /* the node just before start will be step */
            curr = p;
            len++;
        }
        res.pathLength = len;
        res.nextStep = step;
    }

    res.timeMicroseconds = (float)((double)(clock() - t0) / CLOCKS_PER_SEC * 1e6);
    return res;
}
