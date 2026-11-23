/*
 * bst.h - Leaderboard stored as a BINARY SEARCH TREE keyed by score.
 *
 *   insert         : O(h)  - walk left/right comparing scores
 *   top-N ranking  : reverse in-order traversal (right, root, left)
 *                    visits scores from highest to lowest
 *   rank of score  : O(n) worst case - counts scores greater than it
 *   save to file   : PRE-order traversal, so loading the file and
 *                    inserting in the same order rebuilds the same tree
 */
#ifndef GE_BST_H
#define GE_BST_H

#define NAME_LEN 16

typedef struct ScoreNode {
    char name[NAME_LEN];
    int  score;
    int  level;
    int  diff;                 /* 0 easy, 1 medium, 2 hard */
    struct ScoreNode *left;    /* scores <= this score */
    struct ScoreNode *right;   /* scores >  this score */
} ScoreNode;

typedef struct {
    char name[NAME_LEN];
    int  score;
    int  level;
    int  diff;
} ScoreEntry;

/* Inserts a score and returns the (possibly new) root.
 * If `inserted` is not NULL it receives the new node. */
ScoreNode *bst_insert(ScoreNode *root, const char *name, int score, int level, int diff,
                      ScoreNode **inserted);

int  bst_top(const ScoreNode *root, ScoreEntry *out, int maxN); /* highest first */
int  bst_count(const ScoreNode *root);
int  bst_height(const ScoreNode *root);
int  bst_count_greater(const ScoreNode *root, int score);
void bst_free(ScoreNode *root);

int        bst_save(const ScoreNode *root, const char *path); /* returns 1 on success */
ScoreNode *bst_load(const char *path);                         /* NULL if no file */

#endif
