/*
 * retarget.c - copies an animation from one skeleton onto another.
 *
 * Both characters use the same bone names (Mixamo rig) but a different
 * number of bones, so raylib cannot play one character's animation on
 * the other directly. For every bone of the target we find the source
 * bone with the same name (a linear search over the bone ARRAY), take
 * how much that bone rotated away from its rest pose, and apply the
 * same rotation to the target bone. Positions are rebuilt down the
 * bone hierarchy (a TREE stored as a parent-index array), so limbs
 * keep the target character's own proportions.
 */
#include "retarget.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Strip "mixamorig:" style prefixes so "mixamorig:Hips" == "mixamorig9:Hips" ==
 * "mixamorigHips" == "Hips" (Mixamo numbers the rig on some characters). */
static const char *base_name(const char *n)
{
    const char *p = strstr(n, "mixamorig");
    if (p) {
        p += 9;
        while (*p >= '0' && *p <= '9')
            p++;
        if (*p == ':' || *p == '_')
            p++;
        return p;
    }
    return n;
}

static int find_bone(const BoneInfo *bones, int count, const char *name)
{
    const char *want = base_name(name);
    for (int i = 0; i < count; i++)
        if (strcmp(base_name(bones[i].name), want) == 0)
            return i;
    return -1;
}

/* Mixamo files put the skeleton under a root node that is rotated and
 * scaled (cm -> m). raylib bakes that node into the rest pose but not
 * into the animation frames. We recover the missing transform from a
 * frame that equals the rest pose (the T-pose clip) and apply it to
 * every frame of every clip. */
void anim_fix_space(const Model *m, ModelAnimation *anims, int count, int refIndex)
{
    if (refIndex < 0 || refIndex >= count || m->boneCount == 0)
        return;
    Transform ref = anims[refIndex].framePoses[0][0];
    Transform bind = m->bindPose[0];
    Quaternion qA = QuaternionNormalize(QuaternionMultiply(bind.rotation, QuaternionInvert(ref.rotation)));
    float sA = fabsf(ref.scale.x) > 1e-9f ? bind.scale.x / ref.scale.x : 1.0f;
    Vector3 tA = Vector3Subtract(bind.translation, Vector3RotateByQuaternion(Vector3Scale(ref.translation, sA), qA));

    for (int a = 0; a < count; a++)
        for (int f = 0; f < anims[a].frameCount; f++)
            for (int b = 0; b < anims[a].boneCount; b++) {
                Transform *t = &anims[a].framePoses[f][b];
                t->translation = Vector3Add(Vector3RotateByQuaternion(Vector3Scale(t->translation, sA), qA), tA);
                t->rotation = QuaternionNormalize(QuaternionMultiply(qA, t->rotation));
                /* Rest pose and frames now share the same uniform scale, which
                 * cancels out. raylib 5.5's bone-matrix code mishandles
                 * non-unit scale (it scales the bone offset), so store 1. */
                t->scale = (Vector3){ 1.0f, 1.0f, 1.0f };
            }
}

/* Same job as anim_fix_space, for files without a T-pose clip (Mixamo
 * downloads). Scale comes from bone lengths, which never change with the
 * pose; orientation is the 90-degree turn that best lines up "up"
 * (hips->head) and "left" (right arm->left arm) with the rest pose. */
void anim_fix_space_auto(const Model *m, ModelAnimation *anims, int count)
{
    if (count <= 0 || m->boneCount == 0 || anims[0].boneCount != m->boneCount)
        return;
    const Transform *f0 = anims[0].framePoses[0];
    const Transform *bind = m->bindPose;

    double ratio = 0.0;
    int n = 0;
    for (int b = 0; b < m->boneCount; b++) {
        int p = m->bones[b].parent;
        if (p < 0)
            continue;
        float lb = Vector3Distance(bind[b].translation, bind[p].translation);
        float la = Vector3Distance(f0[b].translation, f0[p].translation);
        if (lb > 1e-5f && la > 1e-5f) {
            ratio += lb / la;
            n++;
        }
    }
    float sA = n ? (float)(ratio / n) : 1.0f;

    int hips = find_bone(m->bones, m->boneCount, "Hips"), head = find_bone(m->bones, m->boneCount, "Head");
    int la = find_bone(m->bones, m->boneCount, "LeftArm"), ra = find_bone(m->bones, m->boneCount, "RightArm");
    Quaternion qA = QuaternionIdentity();
    if (hips >= 0 && head >= 0 && la >= 0 && ra >= 0) {
        Vector3 upB = Vector3Normalize(Vector3Subtract(bind[head].translation, bind[hips].translation));
        Vector3 latB = Vector3Normalize(Vector3Subtract(bind[la].translation, bind[ra].translation));
        Vector3 upA0 = Vector3Subtract(f0[head].translation, f0[hips].translation);
        Vector3 latA0 = Vector3Subtract(f0[la].translation, f0[ra].translation);
        float best = -10.0f;
        for (int ax = 0; ax < 4; ax++)
            for (int ay = 0; ay < 4; ay++)
                for (int az = 0; az < 4; az++) {
                    Quaternion q = QuaternionFromEuler(ax * PI / 2, ay * PI / 2, az * PI / 2);
                    float s = Vector3DotProduct(Vector3Normalize(Vector3RotateByQuaternion(upA0, q)), upB) +
                              Vector3DotProduct(Vector3Normalize(Vector3RotateByQuaternion(latA0, q)), latB);
                    if (s > best) {
                        best = s;
                        qA = q;
                    }
                }
    }
    int root = hips >= 0 ? hips : 0;
    Vector3 tA = Vector3Subtract(bind[root].translation,
                                 Vector3RotateByQuaternion(Vector3Scale(f0[root].translation, sA), qA));
    /* Height: line up the lowest foot with the rest pose (feet on the floor),
     * because the hips sit at different heights in different poses. */
    {
        const char *feet[4] = { "LeftFoot", "RightFoot", "LeftToeBase", "RightToeBase" };
        float minB = 1e9f, minA = 1e9f;
        for (int k = 0; k < 4; k++) {
            int fb = find_bone(m->bones, m->boneCount, feet[k]);
            if (fb < 0)
                continue;
            float yb = bind[fb].translation.y;
            float ya = Vector3RotateByQuaternion(Vector3Scale(f0[fb].translation, sA), qA).y;
            if (yb < minB) minB = yb;
            if (ya < minA) minA = ya;
        }
        if (minB < 1e8f && minA < 1e8f)
            tA.y = minB - minA;
    }

    for (int a = 0; a < count; a++)
        for (int f = 0; f < anims[a].frameCount; f++)
            for (int b = 0; b < anims[a].boneCount; b++) {
                Transform *t = &anims[a].framePoses[f][b];
                t->translation = Vector3Add(Vector3RotateByQuaternion(Vector3Scale(t->translation, sA), qA), tA);
                t->rotation = QuaternionNormalize(QuaternionMultiply(qA, t->rotation));
                t->scale = (Vector3){ 1.0f, 1.0f, 1.0f };
            }
}

int model_apply_skinmap(Model *m, const char *path)
{
    char *text = LoadFileText(path);
    if (!text)
        return 0;
    int map[256];
    char *p = text;
    int meshIndex, count, applied = 0;
    while (sscanf(p, "%d %d", &meshIndex, &count) == 2) {
        /* skip the two numbers we just read */
        for (int k = 0; k < 2; k++) {
            while (*p == ' ' || *p == '\r' || *p == '\n') p++;
            while (*p && *p != ' ' && *p != '\r' && *p != '\n') p++;
        }
        for (int i = 0; i < count && i < 256; i++)
            map[i] = (int)strtol(p, &p, 10);
        if (count > 0 && meshIndex >= 0 && meshIndex < m->meshCount) {
            Mesh *mesh = &m->meshes[meshIndex];
            if (mesh->boneIds) {
                for (int v = 0; v < mesh->vertexCount * 4; v++) {
                    int id = mesh->boneIds[v];
                    int to = id < count ? map[id] : -1;
                    if (to < 0) {   /* joint not in the skeleton: drop its weight */
                        to = 0;
                        if (mesh->boneWeights)
                            mesh->boneWeights[v] = 0.0f;
                    }
                    mesh->boneIds[v] = (unsigned char)to;
                }
                applied++;
            }
        }
        while (*p && *p != '\n') p++;   /* next line */
        if (!*p)
            break;
    }
    UnloadFileText(text);
    return applied;
}

void model_normalize_bind_scale(Model *m)
{
    for (int b = 0; b < m->boneCount; b++)
        m->bindPose[b].scale = (Vector3){ 1.0f, 1.0f, 1.0f };
}

/* Average distance between each vertex and the bone that moves it most,
 * after rotating/scaling the vertex by (q, s). Lower = better aligned. */
static float alignment_error(const Model *m, Quaternion q, float s)
{
    double sum = 0.0;
    long count = 0;
    for (int k = 0; k < m->meshCount; k++) {
        const Mesh *mesh = &m->meshes[k];
        if (!mesh->boneIds || !mesh->boneWeights)
            continue;
        for (int v = 0; v < mesh->vertexCount; v += 7) {   /* sample every 7th vertex */
            int best = 0;
            for (int w = 1; w < 4; w++)
                if (mesh->boneWeights[v * 4 + w] > mesh->boneWeights[v * 4 + best])
                    best = w;
            int bone = mesh->boneIds[v * 4 + best];
            if (bone >= m->boneCount)
                continue;
            Vector3 p = { mesh->vertices[v * 3], mesh->vertices[v * 3 + 1], mesh->vertices[v * 3 + 2] };
            p = Vector3RotateByQuaternion(Vector3Scale(p, s), q);
            sum += Vector3Distance(p, m->bindPose[bone].translation);
            count++;
        }
    }
    return count ? (float)(sum / count) : 1e9f;
}

/* Some files store the mesh in a different orientation or unit than its
 * skeleton (Z-up, centimetres, facing backwards). Try every 90-degree
 * orientation and the usual unit scales, keep the one where vertices sit
 * closest to their bones, and re-upload the fixed mesh. */
void mesh_align_to_skeleton(Model *m)
{
    const float scales[3] = { 1.0f, 100.0f, 0.01f };
    Quaternion bestQ = QuaternionIdentity();
    float bestS = 1.0f, bestErr = alignment_error(m, bestQ, 1.0f);
    for (int si = 0; si < 3; si++)
        for (int ax = 0; ax < 4; ax++)
            for (int ay = 0; ay < 4; ay++)
                for (int az = 0; az < 4; az++) {
                    Quaternion q = QuaternionFromEuler(ax * PI / 2, ay * PI / 2, az * PI / 2);
                    float e = alignment_error(m, q, scales[si]);
                    if (e < bestErr * 0.98f) {
                        bestErr = e;
                        bestQ = q;
                        bestS = scales[si];
                    }
                }
    if (bestS == 1.0f && fabsf(bestQ.w) > 0.9999f)
        return;   /* already aligned */

    Quaternion q = bestQ;
    for (int k = 0; k < m->meshCount; k++) {
        Mesh *mesh = &m->meshes[k];
        for (int v = 0; v < mesh->vertexCount; v++) {
            Vector3 p = { mesh->vertices[v * 3], mesh->vertices[v * 3 + 1], mesh->vertices[v * 3 + 2] };
            p = Vector3RotateByQuaternion(Vector3Scale(p, bestS), q);
            mesh->vertices[v * 3] = p.x;
            mesh->vertices[v * 3 + 1] = p.y;
            mesh->vertices[v * 3 + 2] = p.z;
            if (mesh->normals) {
                Vector3 n = { mesh->normals[v * 3], mesh->normals[v * 3 + 1], mesh->normals[v * 3 + 2] };
                n = Vector3RotateByQuaternion(n, q);
                mesh->normals[v * 3] = n.x;
                mesh->normals[v * 3 + 1] = n.y;
                mesh->normals[v * 3 + 2] = n.z;
            }
        }
        if (mesh->animVertices)
            memcpy(mesh->animVertices, mesh->vertices, sizeof(float) * 3 * mesh->vertexCount);
        if (mesh->animNormals && mesh->normals)
            memcpy(mesh->animNormals, mesh->normals, sizeof(float) * 3 * mesh->vertexCount);
        UpdateMeshBuffer(*mesh, 0, mesh->vertices, sizeof(float) * 3 * mesh->vertexCount, 0);
        if (mesh->normals)
            UpdateMeshBuffer(*mesh, 2, mesh->normals, sizeof(float) * 3 * mesh->vertexCount, 0);
    }
}

ModelAnimation retarget_animation(const Model *target, const Model *source, const ModelAnimation *src)
{
    ModelAnimation out = { 0 };
    int nb = target->boneCount;
    out.boneCount = nb;
    out.frameCount = src->frameCount;
    memcpy(out.name, src->name, sizeof out.name);
    out.name[sizeof out.name - 1] = '\0';
    out.bones = (BoneInfo *)malloc(sizeof(BoneInfo) * nb);
    memcpy(out.bones, target->bones, sizeof(BoneInfo) * nb);

    int *map = (int *)malloc(sizeof(int) * nb);
    int *order = (int *)malloc(sizeof(int) * nb);   /* parents before children */
    char *done = (char *)calloc(nb, 1);
    for (int b = 0; b < nb; b++)
        map[b] = find_bone(source->bones, source->boneCount, target->bones[b].name);

    int k = 0;
    while (k < nb) {
        for (int b = 0; b < nb; b++) {
            int p = target->bones[b].parent;
            if (!done[b] && (p < 0 || done[p])) {
                order[k++] = b;
                done[b] = 1;
            }
        }
    }

    /* Scale root motion by the ratio of hip heights. */
    int th = find_bone(target->bones, nb, "Hips"), sh = find_bone(source->bones, source->boneCount, "Hips");
    float ratio = 1.0f;
    if (th >= 0 && sh >= 0 && fabsf(source->bindPose[sh].translation.y) > 1e-6f)
        ratio = target->bindPose[th].translation.y / source->bindPose[sh].translation.y;

    /* The two characters may face different directions. Measure each one's
     * left-to-right shoulder line and find the turn (about Y) between them;
     * source motion is re-expressed in the target's frame with it. */
    Quaternion face = QuaternionIdentity();
    {
        int tl = find_bone(target->bones, nb, "LeftArm"), tr = find_bone(target->bones, nb, "RightArm");
        int sl = find_bone(source->bones, source->boneCount, "LeftArm");
        int sr = find_bone(source->bones, source->boneCount, "RightArm");
        if (tl >= 0 && tr >= 0 && sl >= 0 && sr >= 0) {
            Vector3 lt = Vector3Subtract(target->bindPose[tl].translation, target->bindPose[tr].translation);
            Vector3 ls = Vector3Subtract(source->bindPose[sl].translation, source->bindPose[sr].translation);
            float ang = atan2f(ls.z, ls.x) - atan2f(lt.z, lt.x);
            face = QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, ang);
        }
    }
    Quaternion faceInv = QuaternionInvert(face);

    Quaternion *delta = (Quaternion *)malloc(sizeof(Quaternion) * nb);
    out.framePoses = (Transform **)malloc(sizeof(Transform *) * out.frameCount);

    for (int f = 0; f < out.frameCount; f++) {
        Transform *pose = (Transform *)malloc(sizeof(Transform) * nb);
        for (int i = 0; i < nb; i++) {
            int b = order[i];
            int p = target->bones[b].parent;
            Transform bind = target->bindPose[b];
            int m = map[b];

            if (m >= 0) {
                Quaternion animRot = src->framePoses[f][m].rotation;
                Quaternion restRot = source->bindPose[m].rotation;
                Quaternion d = QuaternionMultiply(animRot, QuaternionInvert(restRot));
                /* conjugate into the target's facing: face * d * face^-1 */
                delta[b] = QuaternionNormalize(QuaternionMultiply(QuaternionMultiply(face, d), faceInv));
            } else {
                delta[b] = p >= 0 ? delta[p] : QuaternionIdentity();
            }

            pose[b].rotation = QuaternionNormalize(QuaternionMultiply(delta[b], bind.rotation));
            pose[b].scale = bind.scale;

            if (p < 0) {
                Vector3 moved = bind.translation;
                if (m >= 0) {
                    Vector3 off = Vector3Subtract(src->framePoses[f][m].translation, source->bindPose[m].translation);
                    off = Vector3RotateByQuaternion(off, face);
                    /* In-place animation: strip horizontal root displacement (X and Z) so the character
                     * never drifts away from the tile position, glitches forward/back, or clips into walls.
                     * Keep vertical displacement (Y) for natural hip bobbing while running/walking. */
                    off.x = 0.0f;
                    off.z = 0.0f;
                    moved = Vector3Add(bind.translation, Vector3Scale(off, ratio));
                }
                pose[b].translation = moved;
            } else {
                Vector3 offset = Vector3Subtract(bind.translation, target->bindPose[p].translation);
                pose[b].translation = Vector3Add(pose[p].translation, Vector3RotateByQuaternion(offset, delta[p]));
            }
        }
        out.framePoses[f] = pose;
    }

    free(map);
    free(order);
    free(done);
    free(delta);
    return out;
}
