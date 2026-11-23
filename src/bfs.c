#include "bfs.h"

void bfs_compute(const Maze *m, Cell source, BfsField *f)
{
    static CellQueue q;   /* static: large, reused between calls */

    for (int y = 0; y < MAZE_H; y++)
        for (int x = 0; x < MAZE_W; x++)
            f->dist[y][x] = BFS_UNREACHED;

    f->orderCount = 0;
    f->maxDist = 0;
    f->enqueued = 0;
    f->maxQueue = 0;
    cq_init(&q);

    if (!maze_walkable(m, source.x, source.y))
        return;

    f->dist[source.y][source.x] = 0;
    cq_enqueue(&q, source);
    f->enqueued = 1;

    while (!cq_is_empty(&q)) {
        Cell c = cq_dequeue(&q);
        f->order[f->orderCount++] = c;

        int d = f->dist[c.y][c.x];
        if (d > f->maxDist)
            f->maxDist = d;

        for (int k = 0; k < 4; k++) {
            int nx = c.x + DIR_X[k];
            int ny = c.y + DIR_Y[k];
            if (maze_walkable(m, nx, ny) && f->dist[ny][nx] == BFS_UNREACHED) {
                f->dist[ny][nx] = d + 1;      /* mark visited when enqueued */
                cq_enqueue(&q, (Cell){ nx, ny });
                f->enqueued++;
            }
        }
    }

    f->maxQueue = q.maxCount;
}

Cell bfs_step_toward_source(const Maze *m, const BfsField *f, Cell from)
{
    int cur = f->dist[from.y][from.x];
    if (cur <= 0)
        return from;

    for (int k = 0; k < 4; k++) {
        int nx = from.x + DIR_X[k];
        int ny = from.y + DIR_Y[k];
        if (maze_walkable(m, nx, ny) && f->dist[ny][nx] == cur - 1)
            return (Cell){ nx, ny };
    }
    return from;
}
