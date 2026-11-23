#include "bst.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ScoreNode *new_node(const char *name, int score, int level, int diff)
{
    ScoreNode *n = (ScoreNode *)malloc(sizeof(ScoreNode));
    if (!n)
        return NULL;
    strncpy(n->name, name, NAME_LEN - 1);
    n->name[NAME_LEN - 1] = '\0';
    n->score = score;
    n->level = level;
    n->diff = diff;
    n->left = n->right = NULL;
    return n;
}

ScoreNode *bst_insert(ScoreNode *root, const char *name, int score, int level, int diff,
                      ScoreNode **inserted)
{
    if (root == NULL) {
        ScoreNode *n = new_node(name, score, level, diff);
        if (inserted)
            *inserted = n;
        return n;
    }
    if (score > root->score)
        root->right = bst_insert(root->right, name, score, level, diff, inserted);
    else
        root->left = bst_insert(root->left, name, score, level, diff, inserted);
    return root;
}

/* Reverse in-order: right subtree, node, left subtree => descending scores */
static void top_rec(const ScoreNode *n, ScoreEntry *out, int maxN, int *k)
{
    if (n == NULL || *k >= maxN)
        return;
    top_rec(n->right, out, maxN, k);
    if (*k < maxN) {
        strcpy(out[*k].name, n->name);
        out[*k].score = n->score;
        out[*k].level = n->level;
        out[*k].diff = n->diff;
        (*k)++;
    }
    top_rec(n->left, out, maxN, k);
}

int bst_top(const ScoreNode *root, ScoreEntry *out, int maxN)
{
    int k = 0;
    top_rec(root, out, maxN, &k);
    return k;
}

int bst_count(const ScoreNode *root)
{
    if (root == NULL)
        return 0;
    return 1 + bst_count(root->left) + bst_count(root->right);
}

int bst_height(const ScoreNode *root)
{
    if (root == NULL)
        return 0;
    int l = bst_height(root->left);
    int r = bst_height(root->right);
    return 1 + (l > r ? l : r);
}

int bst_count_greater(const ScoreNode *root, int score)
{
    if (root == NULL)
        return 0;
    if (root->score > score)
        /* this node and its whole right subtree are greater */
        return 1 + bst_count(root->right) + bst_count_greater(root->left, score);
    return bst_count_greater(root->right, score);
}

void bst_free(ScoreNode *root)
{
    if (root == NULL)
        return;
    bst_free(root->left);
    bst_free(root->right);
    free(root);
}

static void save_rec(const ScoreNode *n, FILE *fp)
{
    if (n == NULL)
        return;
    fprintf(fp, "%s %d %d %d\n", n->name, n->score, n->level, n->diff);   /* pre-order */
    save_rec(n->left, fp);
    save_rec(n->right, fp);
}

int bst_save(const ScoreNode *root, const char *path)
{
    FILE *fp = fopen(path, "w");
    if (!fp)
        return 0;
    save_rec(root, fp);
    fclose(fp);
    return 1;
}

ScoreNode *bst_load(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return NULL;

    ScoreNode *root = NULL;
    char line[128];
    while (fgets(line, sizeof line, fp)) {
        char name[NAME_LEN];
        int score = 0, level = 1, diff = 1;
        int n = sscanf(line, "%15s %d %d %d", name, &score, &level, &diff);
        if (n >= 3)
            root = bst_insert(root, name, score, level, diff, NULL);
    }

    fclose(fp);
    return root;
}
