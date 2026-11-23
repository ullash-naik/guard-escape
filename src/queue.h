/*
 * queue.h - Two CIRCULAR QUEUES implemented on fixed-size arrays.
 *
 *   CellQueue : the BFS frontier. Guards use BFS to find the shortest
 *               path to the player.
 *   LogQueue  : the on-screen message feed. New messages are enqueued at
 *               the rear, old ones are dequeued from the front when they
 *               expire or when the queue is full.
 */
#ifndef GE_QUEUE_H
#define GE_QUEUE_H

#include "maze.h"

#define QUEUE_CAP (MAZE_W * MAZE_H)

typedef struct {
    Cell items[QUEUE_CAP];
    int front;      /* index of the next element to dequeue */
    int rear;       /* index where the next element will be enqueued */
    int count;      /* number of elements currently stored */
    int maxCount;   /* largest size the queue reached (shown in the game) */
} CellQueue;

void cq_init(CellQueue *q);
int  cq_is_empty(const CellQueue *q);
int  cq_is_full(const CellQueue *q);
int  cq_enqueue(CellQueue *q, Cell c);   /* returns 0 if full */
Cell cq_dequeue(CellQueue *q);           /* caller checks cq_is_empty first */

#define LOG_CAP  5
#define LOG_LEN  72
#define LOG_LIFE 4.0f   /* seconds a message stays on screen */

typedef struct {
    char  msgs[LOG_CAP][LOG_LEN];
    float age[LOG_CAP];
    int   front;
    int   count;
} LogQueue;

void        log_init(LogQueue *q);
void        log_push(LogQueue *q, const char *msg);   /* enqueue (drops oldest if full) */
void        log_update(LogQueue *q, float dt);        /* dequeue expired messages */
const char *log_get(const LogQueue *q, int i, float *age); /* i-th from front */

#endif
