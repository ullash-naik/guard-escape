/* posecheck.c - dev tool: compares the (fixed) T-pose clip with the rest pose, bone by bone. */
#include "raylib.h"
#include "raymath.h"
#include "../src/retarget.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *file = argc > 1 ? argv[1] : "assets_dl/Soldier.glb";
    int fix = argc > 2;
    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "posecheck");
    Model m = LoadModel(file);
    int n = 0;
    ModelAnimation *a = LoadModelAnimations(file, &n);
    int t = -1;
    for (int i = 0; i < n; i++)
        if (strcmp(a[i].name, "TPose") == 0) t = i;
    if (fix && argv[2][0] == 'a') anim_fix_space_auto(&m, a, n);   /* reference = first clip, not the T-pose */
    else if (fix) anim_fix_space(&m, a, n, t);
    {
        float worst = 0;
        for (int b = 0; b < m.boneCount; b++) {
            float d = Vector3Distance(m.bindPose[b].translation, a[t].framePoses[0][b].translation);
            if (d > worst) worst = d;
        }
        printf("first clip '%s'; worst T-pose vs rest distance after fix: %.3f m\n", a[0].name, worst);
    }
    for (int b = 0; b < m.boneCount; b++) {
        Transform bp = m.bindPose[b], ap = a[t].framePoses[0][b];
        float dt = Vector3Distance(bp.translation, ap.translation);
        float dot = fabsf(QuaternionNormalize(bp.rotation).x * QuaternionNormalize(ap.rotation).x +
                          bp.rotation.y * ap.rotation.y + bp.rotation.z * ap.rotation.z + bp.rotation.w * ap.rotation.w);
        float ang = 2.0f * acosf(fminf(1.0f, dot)) * RAD2DEG;
        if (b < 12 || dt > 0.02f || ang > 5.0f)
            printf("%2d %-28s parent %2d  dT %.3f  dAng %6.1f  bindS %.3f animS %.3f  bindT(%.2f %.2f %.2f) animT(%.2f %.2f %.2f)\n", b,
                   m.bones[b].name, m.bones[b].parent, dt, ang, bp.scale.x, ap.scale.x, bp.translation.x, bp.translation.y,
                   bp.translation.z, ap.translation.x, ap.translation.y, ap.translation.z);
    }
    CloseWindow();
    return 0;
}
