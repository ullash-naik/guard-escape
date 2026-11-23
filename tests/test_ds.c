/*
 * test_ds.c - Console tests for the data structures (no graphics needed).
 * Build & run with: build.bat test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/bfs.h"
#include "../src/bst.h"
#include "../src/maze.h"
#include "../src/queue.h"
#include "../src/astar.h"

static int passed = 0, failed = 0;

#define CHECK(cond, msg)                                   \
    do {                                                   \
        if (cond) { passed++; printf("  PASS  %s\n", msg); } \
        else { failed++; printf("  FAIL  %s\n", msg); }    \
    } while (0)

static CellQueue q;
static BfsField f;
static Maze m;

static void test_queue(void)
{
    printf("Circular queue\n");
    cq_init(&q);
    CHECK(cq_is_empty(&q), "new queue is empty");
    for (int i = 0; i < QUEUE_CAP; i++)
        cq_enqueue(&q, (Cell){ i, 0 });
    CHECK(cq_is_full(&q), "queue is full after QUEUE_CAP enqueues");
    CHECK(!cq_enqueue(&q, (Cell){ 0, 0 }), "enqueue on full queue is rejected");
    for (int i = 0; i < 10; i++)
        cq_dequeue(&q);
    for (int i = 0; i < 10; i++)
        cq_enqueue(&q, (Cell){ 1000 + i, 0 });   /* forces wrap-around */
    CHECK(q.rear == 10, "rear index wrapped around to the start of the array");
    CHECK(cq_dequeue(&q).x == 10, "FIFO order is kept after wrap-around");

    LogQueue lq;
    log_init(&lq);
    for (int i = 0; i < LOG_CAP + 2; i++) {
        char buf[16];
        sprintf(buf, "msg%d", i);
        log_push(&lq, buf);
    }
    CHECK(lq.count == LOG_CAP, "log queue keeps only LOG_CAP messages");
    CHECK(strcmp(log_get(&lq, 0, NULL), "msg2") == 0, "oldest messages were dropped first");
    log_update(&lq, LOG_LIFE + 1.0f);
    CHECK(lq.count == 0, "expired messages are dequeued");
}

static void test_bfs(void)
{
    printf("Maze + BFS\n");
    srand(42);
    maze_generate(&m, 0);
    bfs_compute(&m, (Cell){ 1, 1 }, &f);

    int floors = 0, reached = 0;
    for (int y = 0; y < MAZE_H; y++)
        for (int x = 0; x < MAZE_W; x++)
            if (m.grid[y][x] == TILE_FLOOR) {
                floors++;
                if (f.dist[y][x] >= 0)
                    reached++;
            }
    CHECK(floors == reached, "every floor tile is reachable (maze is connected)");
    CHECK(f.orderCount == floors, "BFS visits each cell exactly once");
    CHECK(f.dist[1][1] == 0, "source distance is 0");

    int monotone = 1;
    for (int i = 1; i < f.orderCount; i++)
        if (f.dist[f.order[i].y][f.order[i].x] < f.dist[f.order[i - 1].y][f.order[i - 1].x])
            monotone = 0;
    CHECK(monotone, "cells are dequeued in non-decreasing distance (FIFO property)");

    Cell c = { MAZE_W - 2, MAZE_H - 2 };
    int steps = 0, target = f.dist[c.y][c.x];
    while (!(c.x == 1 && c.y == 1) && steps < 2000) {
        c = bfs_step_toward_source(&m, &f, c);
        steps++;
    }
    CHECK(steps == target, "following the BFS field takes exactly the shortest distance");
    printf("        (exit is %d steps away, peak queue size %d)\n", target, f.maxQueue);
}

static void test_presets(void)
{
    printf("Hand-made maps\n");
    for (int p = 0; p < MAZE_PRESET_COUNT; p++) {
        maze_load_preset(&m, p);
        int border = 1, floors = 0, reached = 0;
        for (int y = 0; y < MAZE_H; y++)
            for (int x = 0; x < MAZE_W; x++) {
                int edge = x == 0 || y == 0 || x == MAZE_W - 1 || y == MAZE_H - 1;
                if (edge && m.grid[y][x] != TILE_WALL)
                    border = 0;
            }
        bfs_compute(&m, (Cell){ 1, 1 }, &f);
        for (int y = 0; y < MAZE_H; y++)
            for (int x = 0; x < MAZE_W; x++)
                if (m.grid[y][x] == TILE_FLOOR) {
                    floors++;
                    reached += f.dist[y][x] >= 0;
                }
        char msg[96];
        snprintf(msg, sizeof msg, "map %d: closed border, start/exit open, every floor reachable", p);
        CHECK(border && maze_walkable(&m, 1, 1) && f.dist[MAZE_H - 2][MAZE_W - 2] > 0 && floors == reached, msg);
    }
    CHECK(!maze_load_preset(&m, MAZE_PRESET_COUNT), "out-of-range map index is rejected");
}

static void test_bst(void)
{
    printf("Binary search tree leaderboard\n");
    int scores[] = { 500, 300, 800, 200, 400, 700, 900, 400 };
    const char *names[] = { "Asha", "Ravi", "Meera", "Kiran", "Dev", "Sana", "Arjun", "Nikhil" };
    ScoreNode *root = NULL;
    for (int i = 0; i < 8; i++)
        root = bst_insert(root, names[i], scores[i], 1, i % 3, NULL);

    CHECK(bst_count(root) == 8, "8 nodes inserted");
    CHECK(bst_height(root) == 4, "height is 4 for this insertion order");

    ScoreEntry top[10];
    int n = bst_top(root, top, 10);
    int sorted = n == 8;
    for (int i = 1; i < n; i++)
        if (top[i].score > top[i - 1].score)
            sorted = 0;
    CHECK(sorted, "reverse in-order traversal gives scores high to low");
    CHECK(strcmp(top[0].name, "Arjun") == 0, "highest score is ranked first");
    CHECK(bst_count_greater(root, 700) == 2, "2 scores are greater than 700");

    CHECK(bst_save(root, "test_board.txt"), "tree saved to file");
    ScoreNode *loaded = bst_load("test_board.txt");
    ScoreEntry top2[10];
    int n2 = bst_top(loaded, top2, 10);
    int same = n2 == n && bst_height(loaded) == bst_height(root);
    for (int i = 0; i < n2 && same; i++)
        if (top2[i].score != top[i].score || strcmp(top2[i].name, top[i].name) != 0)
            same = 0;
    CHECK(same, "pre-order file reload rebuilds the identical tree");
    int diffsKept = n2 == n;
    for (int i = 0; i < n2 && diffsKept; i++)
        if (top2[i].diff != top[i].diff)
            diffsKept = 0;
    CHECK(diffsKept, "difficulties preserved through save/load cycle");

    bst_free(root);
    bst_free(loaded);
    remove("test_board.txt");
}

static void test_astar(void)
{
    printf("A* Pathfinding (Binary Min-Heap Priority Queue)\n");
    srand(1234);
    maze_generate(&m, 0);

    Cell start = { 1, 1 };
    Cell goal = { MAZE_W - 2, MAZE_H - 2 };

    /* Compute standard BFS for baseline shortest distance */
    bfs_compute(&m, start, &f);
    int bfsDist = f.dist[goal.y][goal.x];

    /* Run A* search */
    AStarResult astar = astar_search(&m, start, goal);

    CHECK(astar.reachable == 1, "A* successfully found path to goal");
    CHECK(astar.pathLength == bfsDist, "A* path length matches BFS optimal shortest distance");
    CHECK(astar.nodesExplored <= f.orderCount, "A* explores fewer or equal nodes than BFS (heuristic pruning)");
    CHECK(astar.peakHeapSize > 0, "Binary Min-Heap maintained active frontier nodes");
    printf("        (Path length %d, BFS visited %d cells, A* visited %d nodes, Heap peak %d, time %.1fus)\n",
           astar.pathLength, f.orderCount, astar.nodesExplored, astar.peakHeapSize, astar.timeMicroseconds);
}

int main(void)
{
    test_queue();
    test_bfs();
    test_presets();
    test_bst();
    test_astar();
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
