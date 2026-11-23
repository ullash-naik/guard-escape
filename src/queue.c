#include "queue.h"
#include <string.h>

/* ---------------- Circular queue of cells (BFS frontier) ---------------- */

void cq_init(CellQueue *q)
{
    q->front = 0;
    q->rear = 0;
    q->count = 0;
    q->maxCount = 0;
}

int cq_is_empty(const CellQueue *q) { return q->count == 0; }
int cq_is_full(const CellQueue *q)  { return q->count == QUEUE_CAP; }

int cq_enqueue(CellQueue *q, Cell c)
{
    if (cq_is_full(q))
        return 0;
    q->items[q->rear] = c;
    q->rear = (q->rear + 1) % QUEUE_CAP;      /* wrap around */
    q->count++;
    if (q->count > q->maxCount)
        q->maxCount = q->count;
    return 1;
}

Cell cq_dequeue(CellQueue *q)
{
    Cell c = q->items[q->front];
    q->front = (q->front + 1) % QUEUE_CAP;    /* wrap around */
    q->count--;
    return c;
}

/* ---------------- Circular queue of messages (game log) ---------------- */

void log_init(LogQueue *q)
{
    q->front = 0;
    q->count = 0;
}

static void log_dequeue(LogQueue *q)
{
    q->front = (q->front + 1) % LOG_CAP;
    q->count--;
}

void log_push(LogQueue *q, const char *msg)
{
    if (q->count == LOG_CAP)          /* full: drop the oldest message */
        log_dequeue(q);
    int rear = (q->front + q->count) % LOG_CAP;
    strncpy(q->msgs[rear], msg, LOG_LEN - 1);
    q->msgs[rear][LOG_LEN - 1] = '\0';
    q->age[rear] = 0.0f;
    q->count++;
}

void log_update(LogQueue *q, float dt)
{
    for (int i = 0; i < q->count; i++)
        q->age[(q->front + i) % LOG_CAP] += dt;
    while (q->count > 0 && q->age[q->front] > LOG_LIFE)
        log_dequeue(q);
}

const char *log_get(const LogQueue *q, int i, float *age)
{
    int idx = (q->front + i) % LOG_CAP;
    if (age)
        *age = q->age[idx];
    return q->msgs[idx];
}
