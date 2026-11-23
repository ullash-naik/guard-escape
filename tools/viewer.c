/* viewer.c - dev tool: renders a character in its rest pose and animated, to PNGs.
 * usage: viewer <out-prefix> <model.glb> <anim.glb>   (run from the guard-escape folder) */
#include "raylib.h"
#include "../src/retarget.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "viewer";
    const char *file = argc > 2 ? argv[2] : "assets/characters/student.glb";
    const char *animFile = argc > 3 ? argv[3] : "assets/characters/anim_walk.glb";
    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "viewer");

    Model m = LoadModel(file);
    char mapPath[512];
    snprintf(mapPath, sizeof mapPath, "%s/%s.skinmap", GetDirectoryPath(file), GetFileNameWithoutExt(file));
    if (argc > 4 && strcmp(argv[4], "nomap") == 0)
        printf("skinmap skipped\n");
    else
        printf("skinmap meshes changed: %d\n", model_apply_skinmap(&m, mapPath));
    for (int k = 0; k < m.meshCount; k++) {
        BoundingBox b = GetMeshBoundingBox(m.meshes[k]);
        int maxId = 0;
        for (int v = 0; v < m.meshes[k].vertexCount * 4; v++)
            if (m.meshes[k].boneIds && m.meshes[k].boneIds[v] > maxId) maxId = m.meshes[k].boneIds[v];
        printf("mesh %d: %d verts, mat %d, bbox y %.2f..%.2f, max bone id %d\n", k, m.meshes[k].vertexCount,
               m.meshMaterial[k], b.min.y, b.max.y, maxId);
    }
    mesh_align_to_skeleton(&m);
    model_normalize_bind_scale(&m);

    int n = 0;
    ModelAnimation *clips = LoadModelAnimations(animFile, &n);
    int ci = n > 1 ? 1 : 0;
    Model src = LoadModel(animFile);
    anim_fix_space_auto(&src, clips, n);
    model_normalize_bind_scale(&src);
    ModelAnimation a = retarget_animation(&m, &src, &clips[ci]);

    RenderTexture2D rt = LoadRenderTexture(700, 700);
    Camera3D cam = { { 0.0f, 1.1f, 3.6f }, { 0.0f, 0.9f, 0.0f }, { 0, 1, 0 }, 45.0f, CAMERA_PERSPECTIVE };
    for (int shot = 0; shot < 2; shot++) {
        if (shot == 1)
            UpdateModelAnimation(m, a, a.frameCount / 3);
        BeginTextureMode(rt);
        ClearBackground((Color){ 60, 64, 80, 255 });
        BeginMode3D(cam);
        DrawGrid(10, 0.5f);
        DrawModel(m, (Vector3){ 0, 0, 0 }, 1.0f, WHITE);
        EndMode3D();
        EndTextureMode();
        Image img = LoadImageFromTexture(rt.texture);
        ImageFlipVertical(&img);
        ExportImage(img, TextFormat("%s_%d.png", out, shot));
        UnloadImage(img);
    }
    CloseWindow();
    return 0;
}
