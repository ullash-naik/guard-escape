/* probe.c - dev tool: prints what raylib loads from each character model. */
#include "raylib.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "probe");
    Model models[8];
    ModelAnimation *anims[8];
    int animCount[8];
    for (int i = 1; i < argc && i < 9; i++) {
        Model m = LoadModel(argv[i]);
        BoundingBox b = GetModelBoundingBox(m);
        printf("%s\n  meshes %d, materials %d, bones %d, bbox min(%.2f %.2f %.2f) max(%.2f %.2f %.2f)\n", argv[i],
               m.meshCount, m.materialCount, m.boneCount, b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
        int vc = 0;
        for (int k = 0; k < m.meshCount; k++) vc += m.meshes[k].vertexCount;
        printf("  vertices %d\n", vc);
        for (int k = 0; k < m.materialCount; k++)
            printf("  material %d: diffuse texture id %u (%dx%d)\n", k, m.materials[k].maps[MATERIAL_MAP_DIFFUSE].texture.id,
                   m.materials[k].maps[MATERIAL_MAP_DIFFUSE].texture.width, m.materials[k].maps[MATERIAL_MAP_DIFFUSE].texture.height);
        int n = 0;
        ModelAnimation *a = LoadModelAnimations(argv[i], &n);
        for (int k = 0; k < n; k++)
            printf("  anim '%s': %d frames, %d bones, valid=%d\n", a[k].name, a[k].frameCount, a[k].boneCount,
                   IsModelAnimationValid(m, a[k]));
        models[i - 1] = m;
        anims[i - 1] = a;
        animCount[i - 1] = n;
    }
    /* Can animations from model j drive model i? (same skeleton) */
    for (int i = 0; i < argc - 1; i++)
        for (int j = 0; j < argc - 1; j++)
            if (i != j && animCount[j] > 0)
                printf("anims of %s on %s: valid=%d\n", argv[j + 1], argv[i + 1], IsModelAnimationValid(models[i], anims[j][0]));
    CloseWindow();
    return 0;
}
