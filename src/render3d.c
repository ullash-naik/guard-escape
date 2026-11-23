/*
 * render3d.c - draws the maze as a college corridor at night, in 3D.
 *
 * Nothing here changes the game rules: the 2D array, BFS and guards
 * work exactly as before. This file only turns that grid into a lit
 * 3D scene. One tile = one world unit; maze column x -> world X,
 * maze row y -> world Z.
 *
 * Realism features:
 *   - textured terrazzo floor, plastered walls, ceiling, lockers, doors,
 *     notice boards, windows, benches, extinguishers, posters, bins;
 *     surfaces use normal maps so their bumps react to light
 *   - per-pixel lighting: lantern, guard torches, flickering tube lights,
 *     moonlight through windows, exit light, shiny polished floor
 *   - animated human characters (idle / walk / run)
 *   - 4 cameras: third person (spring arm), chase, first person, overview
 *   - rendered at 1.5x resolution, then graded (vignette, tone curve)
 */
#include "game.h"
#include "raymath.h"
#include "retarget.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WALL_H      1.5f
#define CHAR_HEIGHT 1.12f   /* world units (1 tile = ~2 m, so ~1.75 m person) */
#define MAX_LAMPS   16
#define MAX_PROPS   480
#define SSAA        1.5f

/* ------------------------------------------------------------------ */
/* Shaders                                                             */
/* ------------------------------------------------------------------ */
static const char *VS_BODY =
    "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor;\n"
    "#ifdef NORMAL_MAP\n"
    "in vec4 vertexTangent;\n"
    "centroid out vec3 fragTangent; out float fragTanW;\n"
    "#endif\n"
    "uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matNormal;\n"
    "centroid out vec3 fragPos; centroid out vec3 fragNormal; out vec2 fragTexCoord; out vec4 fragColor;\n"
    "void main() {\n"
    "  fragPos = vec3(matModel * vec4(vertexPosition, 1.0));\n"
    "  fragNormal = vec3(matNormal * vec4(vertexNormal, 0.0));\n"
    "  fragTexCoord = vertexTexCoord;\n"
    "  fragColor = vertexColor;\n"
    "#ifdef NORMAL_MAP\n"
    "  fragTangent = vec3(matModel * vec4(vertexTangent.xyz, 0.0));\n"
    "  fragTanW = vertexTangent.w < 0.0 ? -1.0 : 1.0;\n"
    "#endif\n"
    "  gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *FS_BODY =
    "centroid in vec3 fragPos; centroid in vec3 fragNormal; in vec2 fragTexCoord; in vec4 fragColor;\n"
    "#ifdef NORMAL_MAP\n"
    "centroid in vec3 fragTangent; in float fragTanW;\n"
    "uniform sampler2D texture2;\n"
    "#endif\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 viewPos;\n"
    "uniform vec3 lanternPos; uniform float lanternRadius; uniform float ambient;\n"
    "uniform vec4 guardLights[6]; uniform vec3 guardTints[6]; uniform int guardCount;\n"
    "uniform vec4 lampLights[16]; uniform vec3 lampTints[16]; uniform int lampCount;\n"
    "uniform vec4 exitLight;\n"
    "uniform vec3 fogColor; uniform float fogDensity;\n"
    "uniform float specStrength; uniform float skirting; uniform float uvWorld; uniform float alphaCut;\n"
    "out vec4 finalColor;\n"
    "vec3 N; vec3 V; vec3 specAcc = vec3(0.0);\n"
    "vec3 pointLight(vec3 lp, float radius, vec3 color) {\n"
    "  vec3 L = lp - fragPos; float d = length(L);\n"
    "  if (d >= radius) return vec3(0.0);\n"
    "  float att = 1.0 - d / radius; att *= att;\n"
    "  vec3 l = L / max(d, 0.001);\n"
    "  float diff = 0.2 + 0.8 * clamp(dot(N, l), 0.0, 1.0);\n"
    "  vec3 h = normalize(l + V);\n"
    "  specAcc += color * att * pow(clamp(dot(N, h), 0.0, 1.0), 48.0) * specStrength;\n"
    "  return color * att * diff;\n"
    "}\n"
    "void main() {\n"
    "  vec3 n0 = fragNormal / max(length(fragNormal), 0.001);\n"
    "  vec2 uv = fragTexCoord;\n"
    "  if (uvWorld > 0.0) {\n"   /* map by world position: no stretching, seamless across tiles */
    "    vec3 an = abs(n0);\n"
    "    if (an.y > 0.5) uv = fragPos.xz * uvWorld;\n"
    "    else if (an.x > 0.5) uv = vec2(fragPos.z, -fragPos.y) * uvWorld;\n"
    "    else uv = vec2(fragPos.x, -fragPos.y) * uvWorld;\n"
    "  }\n"
    "  vec4 tex = texture(texture0, uv);\n"
    "  if (tex.a < alphaCut) discard;\n"
    "  vec3 base = tex.rgb * colDiffuse.rgb * fragColor.rgb;\n"
    "  N = n0;\n"
    "#ifdef NORMAL_MAP\n"
    "  vec3 T = fragTangent - N * dot(N, fragTangent);\n"
    "  T = T / max(length(T), 0.001);\n"
    "  vec3 B = cross(N, T) * fragTanW;\n"
    "  vec3 nm = texture(texture2, uv).rgb * 2.0 - 1.0;\n"
    "  N = normalize(mat3(T, B, N) * nm);\n"
    "#endif\n"
    "  V = normalize(viewPos - fragPos);\n"
    "  if (skirting > 0.0 && fragPos.y < skirting) base *= vec3(0.30, 0.22, 0.18);\n"
    /* soft contact darkening where walls meet the floor and ceiling */
    "  if (skirting > 0.0) base *= 0.75 + 0.25 * smoothstep(0.0, 0.35, fragPos.y) * smoothstep(0.0, 0.3, 1.5 - fragPos.y);\n"
    "  vec3 sunDir = normalize(vec3(0.45, 1.0, 0.3));\n"
    "  vec3 light = vec3(ambient * (0.6 + 0.4 * N.y) + ambient * 0.7 * max(dot(N, sunDir), 0.0));\n"
    "  light += pointLight(lanternPos, lanternRadius, vec3(1.0, 0.86, 0.64) * 1.7);\n"
    "  for (int i = 0; i < 6; i++) {\n"
    "    if (i >= guardCount) break;\n"
    "    light += pointLight(guardLights[i].xyz, 3.2, guardTints[i] * guardLights[i].w);\n"
    "  }\n"
    "  for (int i = 0; i < 16; i++) {\n"
    "    if (i >= lampCount) break;\n"
    "    light += pointLight(lampLights[i].xyz, 4.2, lampTints[i] * lampLights[i].w);\n"
    "  }\n"
    "  light += pointLight(exitLight.xyz, 3.8, vec3(0.85, 0.28, 1.0) * exitLight.w);\n"
    "  vec3 col = base * light + specAcc;\n"
    "  float dist = length(viewPos - fragPos);\n"
    "  float fog = 1.0 - exp(-fogDensity * fogDensity * dist * dist);\n"
    "  col = mix(col, fogColor, clamp(fog, 0.0, 1.0));\n"
    "  finalColor = vec4(col, 1.0);\n"
    "}\n";

/* Post-process: gentle filmic curve, cool shadows / warm highlights, vignette */
static const char *POST_FS =
    "#version 330\n"
    "in vec2 fragTexCoord; in vec4 fragColor;\n"
    "uniform sampler2D texture0; uniform float time;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "  vec3 c = texture(texture0, fragTexCoord).rgb;\n"
    "  c = c * (1.0 + c * 0.25) / (1.0 + c * 0.6);\n"
    "  float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "  c = mix(c * vec3(0.92, 0.97, 1.08), c * vec3(1.06, 1.0, 0.92), smoothstep(0.15, 0.7, l));\n"
    "  c = mix(vec3(l), c, 1.1);\n"
    "  vec2 q = fragTexCoord - 0.5;\n"
    "  c *= 1.0 - 0.55 * dot(q, q) * 1.6;\n"
    "  float g = fract(sin(dot(fragTexCoord * 913.0 + time, vec2(12.9898, 78.233))) * 43758.5453);\n"
    "  c += (g - 0.5) * 0.018;\n"
    "  finalColor = vec4(c, 1.0);\n"
    "}\n";

typedef struct {
    Shader sh;
    int viewPos, lantern, lanternR, ambient, guardLights, guardTints, guardCount, lamps, lampTints, lampCount, exitL,
        fogColor, fogDensity, spec, skirting, uvWorld, alphaCut;
} LitShader;

static LitShader litNM;     /* world surfaces with normal maps */
static LitShader litPlain;  /* characters and untextured shapes */
static Shader post;
static int postTime;
static RenderTexture2D sceneRT;

static void lit_load(LitShader *L, int normalMap)
{
    static char vs[4096], fs[10000];
    const char *def = normalMap ? "#define NORMAL_MAP\n" : "";
    snprintf(vs, sizeof vs, "#version 330\n%s%s", def, VS_BODY);
    snprintf(fs, sizeof fs, "#version 330\n%s%s", def, FS_BODY);
    L->sh = LoadShaderFromMemory(vs, fs);
    L->viewPos = GetShaderLocation(L->sh, "viewPos");
    L->lantern = GetShaderLocation(L->sh, "lanternPos");
    L->lanternR = GetShaderLocation(L->sh, "lanternRadius");
    L->ambient = GetShaderLocation(L->sh, "ambient");
    L->guardLights = GetShaderLocation(L->sh, "guardLights");
    L->guardTints = GetShaderLocation(L->sh, "guardTints");
    L->guardCount = GetShaderLocation(L->sh, "guardCount");
    L->lamps = GetShaderLocation(L->sh, "lampLights");
    L->lampTints = GetShaderLocation(L->sh, "lampTints");
    L->lampCount = GetShaderLocation(L->sh, "lampCount");
    L->exitL = GetShaderLocation(L->sh, "exitLight");
    L->fogColor = GetShaderLocation(L->sh, "fogColor");
    L->fogDensity = GetShaderLocation(L->sh, "fogDensity");
    L->spec = GetShaderLocation(L->sh, "specStrength");
    L->skirting = GetShaderLocation(L->sh, "skirting");
    L->uvWorld = GetShaderLocation(L->sh, "uvWorld");
    L->alphaCut = GetShaderLocation(L->sh, "alphaCut");
    if (normalMap)
        L->sh.locs[SHADER_LOC_VERTEX_TANGENT] = GetShaderLocationAttrib(L->sh, "vertexTangent");
}

static void lit_material(LitShader *L, float spec, float skirting, float uvWorld)
{
    SetShaderValue(L->sh, L->spec, &spec, SHADER_UNIFORM_FLOAT);
    SetShaderValue(L->sh, L->skirting, &skirting, SHADER_UNIFORM_FLOAT);
    SetShaderValue(L->sh, L->uvWorld, &uvWorld, SHADER_UNIFORM_FLOAT);
    float cut = 0.0f;   /* characters set their own cut-off in draw_human */
    SetShaderValue(L->sh, L->alphaCut, &cut, SHADER_UNIFORM_FLOAT);
}

/* ------------------------------------------------------------------ */
/* Resources                                                           */
/* ------------------------------------------------------------------ */
static Model wallM, floorM, metalM, woodM, plainCube, sphereM, cylM;

typedef struct {
    int ok;
    Model model;
    ModelAnimation idle, walk, run;
    float scale;       /* model units -> world units */
    float faceAng;     /* direction the model faces in its own space (radians, atan2(x, z)) */
} Character;

static Character playerChar, guardChar;
static int usingMixamo;

/* One-frame "scratch" animations that hold a blended pose for each character */
static ModelAnimation playerPose, guardPose;
static Transform poseA[256], poseB[256];

static Texture2D load_tex(const char *path)
{
    Texture2D t = LoadTexture(path);
    if (t.id) {
        GenTextureMipmaps(&t);
        SetTextureFilter(t, TEXTURE_FILTER_ANISOTROPIC_8X);
        SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    }
    return t;
}

static Model textured_cube(const char *name)
{
    Mesh mesh = GenMeshCube(1.0f, 1.0f, 1.0f);
    GenMeshTangents(&mesh);
    Model m = LoadModelFromMesh(mesh);
    char diff[256], nor[256];
    snprintf(diff, sizeof diff, "assets/textures/%s_diff.png", name);
    snprintf(nor, sizeof nor, "assets/textures/%s_nor.png", name);
    Texture2D d = load_tex(diff), n = load_tex(nor);
    if (d.id && n.id) {
        m.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = d;
        m.materials[0].maps[MATERIAL_MAP_NORMAL].texture = n;
        m.materials[0].shader = litNM.sh;
    } else {
        m.materials[0].shader = litPlain.sh;   /* texture missing: plain colour */
    }
    return m;
}

/* ---------------- Characters ---------------- */
static int find_clip(ModelAnimation *a, int n, const char *name)
{
    if (!name)
        return n > 0 ? 0 : -1;
    for (int i = 0; i < n; i++)
        if (strcmp(a[i].name, name) == 0)
            return i;
    return -1;
}

static int bone_index(const Model *m, const char *suffix)
{
    for (int i = 0; i < m->boneCount; i++) {
        const char *n = m->bones[i].name;
        size_t ln = strlen(n), ls = strlen(suffix);
        if (ln >= ls && strcmp(n + ln - ls, suffix) == 0)
            return i;
    }
    return -1;
}

/* Loads a character model and measures its size and facing from the skeleton. */
static int char_load_model(Character *c, const char *file)
{
    if (!FileExists(file))
        return 0;
    c->model = LoadModel(file);
    if (c->model.boneCount == 0 || c->model.meshCount == 0)
        return 0;
    char mapPath[512];
    snprintf(mapPath, sizeof mapPath, "%s/%s.skinmap", GetDirectoryPath(file), GetFileNameWithoutExt(file));
    if (FileExists(mapPath))
        model_apply_skinmap(&c->model, mapPath);   /* multi-skin Mixamo files, see tools/unify_skins.ps1 */
    mesh_align_to_skeleton(&c->model);
    model_normalize_bind_scale(&c->model);

    BoundingBox bb = GetModelBoundingBox(c->model);
    float h = bb.max.y - bb.min.y;
    c->scale = h > 1e-4f ? CHAR_HEIGHT / h : 1.0f;

    /* facing = cross(left, up): a character's left arm points to its left */
    int la = bone_index(&c->model, "LeftArm"), ra = bone_index(&c->model, "RightArm");
    c->faceAng = 0.0f;
    if (la >= 0 && ra >= 0) {
        Vector3 left = Vector3Subtract(c->model.bindPose[la].translation, c->model.bindPose[ra].translation);
        Vector3 fwd = Vector3CrossProduct(left, (Vector3){ 0, 1, 0 });
        c->faceAng = atan2f(fwd.x, fwd.z);
    }
    for (int i = 0; i < c->model.materialCount; i++)
        c->model.materials[i].shader = litPlain.sh;
    return 1;
}

/* Loads one clip from `file`, puts it in rest-pose space and copies it onto
 * both characters by bone name. */
static int add_clip(const char *file, const char *clipName, ModelAnimation *forPlayer, ModelAnimation *forGuard)
{
    if (!FileExists(file))
        return 0;
    int n = 0;
    ModelAnimation *clips = LoadModelAnimations(file, &n);
    int ci = find_clip(clips, n, clipName);
    if (ci < 0) {
        if (clips)
            UnloadModelAnimations(clips, n);
        return 0;
    }
    /* If the clip was made for the guard's own skeleton (Mixamo downloads),
     * reuse the loaded guard instead of loading the whole file again. */
    int ownSkeleton = IsModelAnimationValid(guardChar.model, clips[ci]);
    Model src = ownSkeleton ? guardChar.model : LoadModel(file);
    if (src.boneCount == 0) {
        UnloadModelAnimations(clips, n);
        return 0;
    }
    anim_fix_space_auto(&src, clips, n);
    if (!ownSkeleton)
        model_normalize_bind_scale(&src);
    *forPlayer = retarget_animation(&playerChar.model, &src, &clips[ci]);
    *forGuard = retarget_animation(&guardChar.model, &src, &clips[ci]);
    UnloadModelAnimations(clips, n);
    if (!ownSkeleton)
        UnloadModel(src);
    return 1;
}

static int load_character_set(const char *playerFile, const char *guardFile, const char *idleFile,
                              const char *idleName, const char *walkFile, const char *walkName, const char *runFile,
                              const char *runName)
{
    if (!char_load_model(&playerChar, playerFile) || !char_load_model(&guardChar, guardFile))
        return 0;
    int ok = add_clip(idleFile, idleName, &playerChar.idle, &guardChar.idle) &&
             add_clip(walkFile, walkName, &playerChar.walk, &guardChar.walk) &&
             add_clip(runFile, runName, &playerChar.run, &guardChar.run);
    playerChar.ok = guardChar.ok = ok;
    return ok;
}

static void make_pose_holder(ModelAnimation *a, const Model *m)
{
    memset(a, 0, sizeof *a);
    a->boneCount = m->boneCount;
    a->frameCount = 1;
    a->bones = m->bones;   /* shared with the model, never freed here */
    a->framePoses = (Transform **)malloc(sizeof(Transform *));
    a->framePoses[0] = (Transform *)calloc((size_t)m->boneCount, sizeof(Transform));
}

static void load_characters(void)
{
    /* Preferred: realistic Mixamo characters converted by tools/import_mixamo.bat */
    if (load_character_set("assets/characters/student.glb", "assets/characters/guard.glb",
                           FileExists("assets/characters/anim_look.glb") ? "assets/characters/anim_look.glb"
                                                                         : "assets/characters/anim_idle.glb",
                           "mixamo.com", "assets/characters/anim_walk.glb", "mixamo.com",
                           "assets/characters/anim_run.glb", "mixamo.com")) {
        usingMixamo = 1;
        return;
    }
    /* Fallback: the bundled models */
    load_character_set("assets/characters/Michelle.glb", "assets/characters/Soldier.glb",
                       "assets/characters/Soldier.glb", "Idle", "assets/characters/Soldier.glb", "Walk",
                       "assets/characters/Soldier.glb", "Run");
}

int renderAssetsMissing;

/* Finds the folder that contains assets/: next to the exe, the current
 * folder, their parents, then the project folder recorded at build time
 * (so a copied exe still works on this computer). */
static int find_assets_folder(void)
{
    char tries[8][512];
    int n = 0;
    snprintf(tries[n++], 512, "%s", GetApplicationDirectory());
    snprintf(tries[n++], 512, "%s", GetWorkingDirectory());
    snprintf(tries[n++], 512, "%s/..", GetApplicationDirectory());
    snprintf(tries[n++], 512, "%s/..", GetWorkingDirectory());
    snprintf(tries[n++], 512, "%s/guard-escape", GetApplicationDirectory());
    snprintf(tries[n++], 512, "%s/1/guard-escape", GetApplicationDirectory());
#ifdef PROJECT_DIR
    snprintf(tries[n++], 512, "%s", PROJECT_DIR);
#endif
    for (int i = 0; i < n; i++)
        if (DirectoryExists(TextFormat("%s/assets/textures", tries[i]))) {
            ChangeDirectory(tries[i]);
            return 1;
        }
    return 0;
}

void render_init(void)
{
    renderAssetsMissing = !find_assets_folder();
    lit_load(&litNM, 1);
    lit_load(&litPlain, 0);
    post = LoadShaderFromMemory(NULL, POST_FS);
    postTime = GetShaderLocation(post, "time");
    sceneRT = LoadRenderTexture((int)(SCREEN_W * SSAA), (int)(SCREEN_H * SSAA));
    SetTextureFilter(sceneRT.texture, TEXTURE_FILTER_BILINEAR);

    wallM = textured_cube("beige_wall_001");
    floorM = textured_cube("terrazzo_tiles");
    metalM = textured_cube("painted_metal_shutter");
    woodM = textured_cube("old_wood_floor");

    plainCube = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    sphereM = LoadModelFromMesh(GenMeshSphere(1.0f, 12, 16));
    cylM = LoadModelFromMesh(GenMeshCylinder(1.0f, 1.0f, 16));
    plainCube.materials[0].shader = litPlain.sh;
    sphereM.materials[0].shader = litPlain.sh;
    cylM.materials[0].shader = litPlain.sh;

    load_characters();
    if (playerChar.ok && guardChar.ok && playerChar.model.boneCount <= 256 && guardChar.model.boneCount <= 256) {
        make_pose_holder(&playerPose, &playerChar.model);
        make_pose_holder(&guardPose, &guardChar.model);
    } else {
        playerChar.ok = guardChar.ok = 0;
    }

    G.camSnap = 1;
    G.wallScale = 1.0f;
}

static void pose_holder_free(ModelAnimation *a)
{
    if (a->framePoses) {
        free(a->framePoses[0]);
        free(a->framePoses);
        a->framePoses = NULL;
    }
}

static void char_unload(Character *c)
{
    if (c->ok) {
        UnloadModelAnimation(c->idle);
        UnloadModelAnimation(c->walk);
        UnloadModelAnimation(c->run);
    }
    if (c->model.meshCount)
        UnloadModel(c->model);
}

void render_unload(void)
{
    UnloadModel(wallM);
    UnloadModel(floorM);
    UnloadModel(metalM);
    UnloadModel(woodM);
    UnloadModel(plainCube);
    UnloadModel(sphereM);
    UnloadModel(cylM);
    pose_holder_free(&playerPose);
    pose_holder_free(&guardPose);
    char_unload(&playerChar);
    char_unload(&guardChar);
    UnloadRenderTexture(sceneRT);
    UnloadShader(post);
    UnloadShader(litNM.sh);
    UnloadShader(litPlain.sh);
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static Color mix(Color a, Color b, float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
}

static Color shade(Color c, float k)
{
    return (Color){ (unsigned char)clampf(c.r * k, 0, 255), (unsigned char)clampf(c.g * k, 0, 255),
                    (unsigned char)clampf(c.b * k, 0, 255), c.a };
}

static Vector3 tile_world(float x, float y, float h) { return (Vector3){ x + 0.5f, h, y + 0.5f }; }

/* Frame-rate independent smoothing factor (camera-systems skill: never a fixed lerp) */
static float smooth_k(float rate, float dt) { return 1.0f - expf(-rate * dt); }

static float angle_diff(float a, float b)
{
    float d = fmodf(b - a + PI, 2.0f * PI);
    if (d < 0)
        d += 2.0f * PI;
    return d - PI;
}

static int in_front_of_camera(Vector3 p)
{
    Vector3 f = Vector3Subtract(G.cam.target, G.cam.position);
    Vector3 d = Vector3Subtract(p, G.cam.position);
    return Vector3DotProduct(f, d) > 0.1f;
}

static void box(Model m, Vector3 center, Vector3 size, Color tint)
{
    DrawModelEx(m, center, (Vector3){ 0, 1, 0 }, 0.0f, size, tint);
}

/* 1 if a point (world x, z) is inside or within `margin` of a wall tile */
static int world_blocked(float x, float z, float margin)
{
    for (int dz = -1; dz <= 1; dz += 2)
        for (int dx = -1; dx <= 1; dx += 2) {
            int tx = (int)floorf(x + dx * margin), tz = (int)floorf(z + dz * margin);
            if (!maze_walkable(&G.maze, tx, tz))
                return 1;
        }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Level dressing: lamps and props on wall faces                       */
/* ------------------------------------------------------------------ */
typedef enum {
    PROP_LOCKERS, PROP_NOTICE, PROP_DOOR, PROP_WINDOW, PROP_EXTINGUISHER, PROP_BENCH, PROP_POSTER, PROP_BIN
} PropType;

typedef struct {
    Cell wall;
    int dir;        /* direction from the wall tile into the corridor */
    PropType type;
    int seed;
} Prop;

typedef struct {
    Vector3 pos;    /* light position */
    Vector3 tube;   /* tube centre (tube lights only) */
    Vector3 tint;
    float power;
    int dir;
    int flicker;
    int isWindow;
    float seed;
} Lamp;

static float tppArm = 2.0f;   /* current third-person spring-arm length */
static int player_is_moving(void);
static Prop props[MAX_PROPS];
static int propCount;
static Lamp lamps[MAX_LAMPS];
static int lampCount;

static int wall_face_open(int x, int y, int d)
{
    return G.maze.grid[y][x] == TILE_WALL && maze_walkable(&G.maze, x + DIR_X[d], y + DIR_Y[d]);
}

static int is_outer_wall(int x, int y) { return x == 0 || y == 0 || x == MAZE_W - 1 || y == MAZE_H - 1; }

/* 1 if a prop attached to `wall` facing `dir` is directly obstructing or penetrating the camera */
static int prop_blocks_camera(Cell wall, int dir)
{
    int cx = (int)floorf(G.cam.position.x);
    int cz = (int)floorf(G.cam.position.z);
    if (cx == wall.x && cz == wall.y)
        return 1;

    float fx = wall.x + 0.5f + DIR_X[dir] * 0.5f;
    float fz = wall.y + 0.5f + DIR_Y[dir] * 0.5f;
    float dx = G.cam.position.x - fx;
    float dz = G.cam.position.z - fz;
    float distSq = dx * dx + dz * dz;
    if (distSq < 0.85f * 0.85f) {
        float dotNormal = dx * DIR_X[dir] + dz * DIR_Y[dir];
        if (dotNormal < 0.40f)
            return 1;
    }
    return 0;
}

/* Cutaway: in the chase view, walls between the camera (south) and the
 * player are lowered so they never hide the character. */
static int wall_cut(int x, int y)
{
    if (G.camMode != CAM_CHASE || G.state == ST_MENU)
        return 0;
    float dx = (x + 0.5f) - (G.playerDraw.x + 0.5f), dz = (y + 0.5f) - (G.playerDraw.y + 0.5f);
    return dz > 0.2f && dz < 2.8f && fabsf(dx) < 1.6f;
}

void render_build_level(void)
{
    propCount = 0;
    lampCount = 0;

    /* Tube lights: one per region of the maze, on a corridor-facing wall. */
    for (int ry = 0; ry < 2; ry++)
        for (int rx = 0; rx < 5 && lampCount < 10; rx++)
            for (int tries = 0; tries < 60; tries++) {
                int x = rx * MAZE_W / 5 + rand() % (MAZE_W / 5);
                int y = ry * MAZE_H / 2 + rand() % (MAZE_H / 2);
                int d = rand() % 4;
                if (x <= 0 || y <= 0 || x >= MAZE_W - 1 || y >= MAZE_H - 1 || !wall_face_open(x, y, d))
                    continue;
                Lamp *l = &lamps[lampCount++];
                Vector3 face = { x + 0.5f + DIR_X[d] * 0.5f, 0.0f, y + 0.5f + DIR_Y[d] * 0.5f };
                l->tube = (Vector3){ face.x + DIR_X[d] * 0.05f, WALL_H - 0.2f, face.z + DIR_Y[d] * 0.05f };
                l->pos = (Vector3){ face.x + DIR_X[d] * 0.45f, WALL_H - 0.3f, face.z + DIR_Y[d] * 0.45f };
                l->tint = (Vector3){ 0.82f, 0.92f, 1.0f };
                l->power = 1.15f;
                l->dir = d;
                l->flicker = rand() % 3 == 0;
                l->isWindow = 0;
                l->seed = (float)(rand() % 1000) / 37.0f;
                break;
            }

    /* Equal distribution of hiding spots: exactly 1 green locker per corridor lane,
     * systematically balanced across Left (x: 2..10), Center (x: 11..19), and Right (x: 20..28) */
    static const int laneSectors[10] = { 1, 0, 2, 1, 0, 2, 0, 1, 2, 1 };
    /* Sector 0: Left   [2, 10]
     * Sector 1: Center [11, 19]
     * Sector 2: Right  [20, 28] */

    for (int laneY = 1; laneY < MAZE_H - 1; laneY += 2) {
        int laneIdx = (laneY - 1) / 2;
        int targetSector = (laneIdx >= 0 && laneIdx < 10) ? laneSectors[laneIdx] : (laneIdx % 3);

        int minX = 2, maxX = MAZE_W - 3;
        if (targetSector == 0) {
            minX = 2; maxX = 10;
        } else if (targetSector == 1) {
            minX = 11; maxX = 19;
        } else {
            minX = 20; maxX = MAZE_W - 3;
        }

        typedef struct { Cell wall; int dir; } Cand;
        Cand candsSector[64];
        Cand candsAll[256];
        int nSector = 0, nAll = 0;

        for (int x = 1; x < MAZE_W - 1; x++) {
            if (G.maze.grid[laneY][x] == TILE_FLOOR) {
                /* Never mount locker directly facing player start (1,1) or exit (MAZE_W-2, MAZE_H-2) */
                if ((x == 1 && laneY == 1) || (x == MAZE_W - 2 && laneY == MAZE_H - 2))
                    continue;

                for (int d = 0; d < 4; d++) {
                    int wx = x - DIR_X[d], wy = laneY - DIR_Y[d];
                    if (wx >= 0 && wy >= 0 && wx < MAZE_W && wy < MAZE_H &&
                        G.maze.grid[wy][wx] == TILE_WALL && wall_face_open(wx, wy, d)) {
                        Cand c = (Cand){ { wx, wy }, d };
                        if (nAll < 256)
                            candsAll[nAll++] = c;
                        if (wx >= minX && wx <= maxX) {
                            if (nSector < 64)
                                candsSector[nSector++] = c;
                        }
                    }
                }
            }
        }

        Cand chosen;
        if (nSector > 0) {
            chosen = candsSector[rand() % nSector];
        } else if (nAll > 0) {
            chosen = candsAll[rand() % nAll];
        } else {
            continue;
        }

        if (propCount < MAX_PROPS) {
            props[propCount++] = (Prop){ chosen.wall, chosen.dir, PROP_LOCKERS, rand() };
        }
    }

    /* Props on corridor-facing wall faces (doors, notices, extinguishers, benches, posters, bins) */
    for (int y = 0; y < MAZE_H; y++)
        for (int x = 0; x < MAZE_W; x++)
            for (int d = 0; d < 4 && propCount < MAX_PROPS; d++) {
                int nx = x + DIR_X[d], ny = y + DIR_Y[d];
                if (nx < 0 || ny < 0 || nx >= MAZE_W || ny >= MAZE_H || !wall_face_open(x, y, d))
                    continue;

                /* Skip if this wall face already has a locker */
                int alreadyHasProp = 0;
                for (int pi = 0; pi < propCount; pi++) {
                    if (props[pi].wall.x == x && props[pi].wall.y == y && props[pi].dir == d) {
                        alreadyHasProp = 1;
                        break;
                    }
                }
                if (alreadyHasProp)
                    continue;

                int r = rand() % 100;
                PropType t;
                if (is_outer_wall(x, y)) {
                    if (r < 45) t = PROP_WINDOW;   /* outside walls get windows */
                    else if (r < 55) t = PROP_BENCH;
                    else continue;
                } else if (r < 18) t = PROP_NOTICE;
                else if (r < 32) t = PROP_DOOR;
                else if (r < 37) t = PROP_EXTINGUISHER;
                else if (r < 46) t = PROP_BENCH;
                else if (r < 54) t = PROP_POSTER;
                else if (r < 60) t = PROP_BIN;
                else continue;
                props[propCount++] = (Prop){ { x, y }, d, t, rand() };

                /* some windows let moonlight in */
                if (t == PROP_WINDOW && lampCount < MAX_LAMPS && rand() % 3 == 0) {
                    Lamp *l = &lamps[lampCount++];
                    l->pos = (Vector3){ x + 0.5f + DIR_X[d] * 1.0f, 1.0f, y + 0.5f + DIR_Y[d] * 1.0f };
                    l->tint = (Vector3){ 0.45f, 0.6f, 1.0f };
                    l->power = 0.7f;
                    l->dir = d;
                    l->flicker = 0;
                    l->isWindow = 1;
                    l->seed = 0;
                }
            }
}

/* Box on a wall face: along = offset along the wall, w = width along the wall,
 * y0/h = bottom and height, dep = depth out of the wall, out = gap from the wall */
static void face_box(Model m, Cell wall, int d, float along, float w, float y0, float h, float dep, float out, Color tint)
{
    float fx = wall.x + 0.5f + DIR_X[d] * 0.5f, fz = wall.y + 0.5f + DIR_Y[d] * 0.5f;
    float sx = (float)-DIR_Y[d], sz = (float)DIR_X[d];   /* sideways axis along the wall */
    Vector3 c = { fx + DIR_X[d] * (out + dep / 2) + sx * along, y0 + h / 2, fz + DIR_Y[d] * (out + dep / 2) + sz * along };
    Vector3 size = DIR_X[d] != 0 ? (Vector3){ dep, h, w } : (Vector3){ w, h, dep };
    box(m, c, size, tint);
}

static void face_cyl(Cell wall, int d, float along, float y0, float h, float r, float out, Color tint)
{
    float fx = wall.x + 0.5f + DIR_X[d] * 0.5f, fz = wall.y + 0.5f + DIR_Y[d] * 0.5f;
    float sx = (float)-DIR_Y[d], sz = (float)DIR_X[d];
    Vector3 c = { fx + DIR_X[d] * (out + r) + sx * along, y0, fz + DIR_Y[d] * (out + r) + sz * along };
    DrawModelEx(cylM, c, (Vector3){ 0, 1, 0 }, 0.0f, (Vector3){ r, h, r }, tint);
}

static void draw_props(float t)
{
    static const Color paper[4] = { { 240, 240, 230, 255 }, { 250, 230, 150, 255 }, { 240, 180, 190, 255 },
                                    { 170, 210, 240, 255 } };
    static const Color posterBg[4] = { { 40, 90, 160, 255 }, { 170, 50, 50, 255 }, { 40, 130, 90, 255 },
                                       { 200, 140, 30, 255 } };

    /* textured props */
    lit_material(&litNM, 0.5f, 0.0f, 0.0f);
    for (int i = 0; i < propCount; i++) {
        Prop *p = &props[i];
        if (wall_cut(p->wall.x, p->wall.y) || prop_blocks_camera(p->wall, p->dir))
            continue;
        if (p->type == PROP_LOCKERS) {
            /* Distinctive Emerald Stealth Green locker body with neon accent */
            Color body = (Color){ 34, 168, 118, 255 };
            Color frame = (Color){ 18, 95, 68, 255 };
            face_box(metalM, p->wall, p->dir, 0.0f, 0.94f, 0.0f, 1.08f, 0.25f, 0.0f, body);
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.025f, 0.02f, 1.04f, 0.26f, 0.005f, frame);
            /* Glowing neon status light above locker indicating active hiding station */
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.48f, 1.02f, 0.045f, 0.26f, 0.01f, (Color){ 50, 255, 170, 255 });
            /* Gold door handles */
            face_box(plainCube, p->wall, p->dir, 0.16f, 0.04f, 0.48f, 0.14f, 0.28f, 0.015f, C_GOLD);
            face_box(plainCube, p->wall, p->dir, -0.16f, 0.04f, 0.48f, 0.14f, 0.28f, 0.015f, C_GOLD);
        }
    }
    lit_material(&litNM, 0.15f, 0.0f, 0.0f);
    for (int i = 0; i < propCount; i++) {
        Prop *p = &props[i];
        if (wall_cut(p->wall.x, p->wall.y) || prop_blocks_camera(p->wall, p->dir))
            continue;
        if (p->type == PROP_DOOR)
            face_box(woodM, p->wall, p->dir, 0.0f, 0.62f, 0.0f, 1.18f, 0.05f, 0.0f, (Color){ 175, 120, 80, 255 });
        else if (p->type == PROP_NOTICE)
            face_box(woodM, p->wall, p->dir, 0.0f, 0.76f, 0.55f, 0.5f, 0.04f, 0.0f, (Color){ 200, 150, 95, 255 });
        else if (p->type == PROP_BENCH) {
            face_box(woodM, p->wall, p->dir, 0.0f, 0.8f, 0.24f, 0.04f, 0.24f, 0.02f, (Color){ 190, 130, 85, 255 });
            face_box(woodM, p->wall, p->dir, 0.0f, 0.8f, 0.42f, 0.12f, 0.03f, 0.0f, (Color){ 170, 115, 75, 255 });
        }
    }

    /* plain-coloured details */
    lit_material(&litPlain, 0.25f, 0.0f, 0.0f);
    for (int i = 0; i < propCount; i++) {
        Prop *p = &props[i];
        if (wall_cut(p->wall.x, p->wall.y) || prop_blocks_camera(p->wall, p->dir))
            continue;
        switch (p->type) {
        case PROP_LOCKERS:
            for (int s = -1; s <= 1; s += 2)
                face_box(plainCube, p->wall, p->dir, s * 0.15f, 0.015f, 0.04f, 0.98f, 0.01f, 0.24f, (Color){ 30, 34, 42, 255 });
            for (int k = -1; k <= 1; k++) {
                face_box(plainCube, p->wall, p->dir, k * 0.3f + 0.09f, 0.03f, 0.5f, 0.1f, 0.02f, 0.24f, (Color){ 200, 200, 205, 255 });
                face_box(plainCube, p->wall, p->dir, k * 0.3f, 0.16f, 0.86f, 0.08f, 0.01f, 0.24f, (Color){ 40, 44, 55, 255 });
            }
            break;
        case PROP_DOOR:
            face_box(plainCube, p->wall, p->dir, 0.22f, 0.07f, 0.55f, 0.03f, 0.05f, 0.05f, (Color){ 210, 200, 160, 255 });
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.3f, 0.82f, 0.2f, 0.01f, 0.05f, (Color){ 140, 170, 190, 255 });
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.7f, 1.18f, 0.04f, 0.06f, 0.0f, (Color){ 90, 70, 55, 255 });
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.14f, 1.27f, 0.07f, 0.01f, 0.0f, (Color){ 235, 235, 225, 255 });
            break;
        case PROP_NOTICE:
            for (int k = 0; k < 4; k++) {
                float along = -0.24f + k * 0.16f;
                float y0 = 0.62f + ((p->seed >> (k * 2)) & 3) * 0.05f;
                face_box(plainCube, p->wall, p->dir, along, 0.12f, y0, 0.16f, 0.01f, 0.04f, paper[(p->seed + k) & 3]);
            }
            break;
        case PROP_WINDOW:
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.78f, 0.5f, 0.06f, 0.06f, 0.0f, (Color){ 210, 210, 205, 255 });   /* sill */
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.78f, 1.22f, 0.05f, 0.04f, 0.0f, (Color){ 120, 120, 125, 255 });
            for (int s = -1; s <= 1; s++)
                face_box(plainCube, p->wall, p->dir, s * 0.37f, 0.04f, 0.55f, 0.67f, 0.04f, 0.0f, (Color){ 120, 120, 125, 255 });
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.74f, 0.87f, 0.03f, 0.04f, 0.0f, (Color){ 120, 120, 125, 255 });
            for (int b = -2; b <= 2; b++)   /* window grill */
                face_box(plainCube, p->wall, p->dir, b * 0.13f, 0.012f, 0.55f, 0.67f, 0.012f, 0.035f, (Color){ 60, 60, 66, 255 });
            break;
        case PROP_EXTINGUISHER:
            face_cyl(p->wall, p->dir, 0.0f, 0.3f, 0.36f, 0.06f, 0.02f, (Color){ 200, 30, 30, 255 });
            face_cyl(p->wall, p->dir, 0.0f, 0.66f, 0.05f, 0.025f, 0.055f, (Color){ 30, 30, 30, 255 });
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.2f, 0.88f, 0.2f, 0.01f, 0.0f, (Color){ 200, 30, 30, 255 });
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.12f, 0.94f, 0.08f, 0.012f, 0.0f, (Color){ 240, 240, 240, 255 });
            break;
        case PROP_BENCH:
            for (int s = -1; s <= 1; s += 2)
                face_box(plainCube, p->wall, p->dir, s * 0.34f, 0.04f, 0.0f, 0.24f, 0.2f, 0.04f, (Color){ 50, 50, 56, 255 });
            break;
        case PROP_POSTER: {
            Color bg = posterBg[p->seed & 3];
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.36f, 0.55f, 0.5f, 0.008f, 0.0f, bg);
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.28f, 0.92f, 0.06f, 0.006f, 0.008f, (Color){ 245, 245, 240, 255 });
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.22f, 0.68f, 0.18f, 0.006f, 0.008f, shade(bg, 1.6f));
            face_box(plainCube, p->wall, p->dir, 0.0f, 0.26f, 0.6f, 0.03f, 0.006f, 0.008f, (Color){ 245, 245, 240, 255 });
            break;
        }
        case PROP_BIN:
            face_cyl(p->wall, p->dir, 0.25f, 0.0f, 0.28f, 0.1f, 0.03f, (Color){ 70, 110, 80, 255 });
            break;
        }
    }

    /* glowing window glass (moonlight) */
    for (int i = 0; i < propCount; i++) {
        Prop *p = &props[i];
        if (p->type != PROP_WINDOW || wall_cut(p->wall.x, p->wall.y) || prop_blocks_camera(p->wall, p->dir))
            continue;
        float fx = p->wall.x + 0.5f + DIR_X[p->dir] * 0.51f, fz = p->wall.y + 0.5f + DIR_Y[p->dir] * 0.51f;
        Vector3 size = DIR_X[p->dir] != 0 ? (Vector3){ 0.01f, 0.67f, 0.74f } : (Vector3){ 0.74f, 0.67f, 0.01f };
        float k = 0.85f + 0.1f * sinf(t * 0.5f + p->seed);
        DrawCubeV((Vector3){ fx, 0.885f, fz }, size, shade((Color){ 70, 100, 150, 255 }, k));
    }
}

static float lamp_level(const Lamp *l, float t)
{
    if (!l->flicker)
        return 1.0f;
    float n = sinf(t * 13.0f + l->seed) * sinf(t * 7.3f + l->seed * 2.1f) + 0.3f * sinf(t * 31.0f + l->seed);
    return n < -0.55f ? 0.15f : 1.0f;
}

static void draw_lamps(float t)
{
    for (int i = 0; i < lampCount; i++) {
        Lamp *l = &lamps[i];
        if (l->isWindow)
            continue;
        Cell wcell = { (int)(l->tube.x - DIR_X[l->dir] * 0.5f), (int)(l->tube.z - DIR_Y[l->dir] * 0.5f) };
        if (wall_cut(wcell.x, wcell.y) || prop_blocks_camera(wcell, l->dir))
            continue;
        float k = lamp_level(l, t);
        Color c = mix((Color){ 70, 80, 90, 255 }, (Color){ 235, 245, 255, 255 }, k);
        Vector3 size = DIR_X[l->dir] != 0 ? (Vector3){ 0.07f, 0.07f, 0.62f } : (Vector3){ 0.62f, 0.07f, 0.07f };
        DrawCubeV(l->tube, size, c);
        Vector3 bracket = { l->tube.x - DIR_X[l->dir] * 0.03f, l->tube.y + 0.05f, l->tube.z - DIR_Y[l->dir] * 0.03f };
        Vector3 bsize = DIR_X[l->dir] != 0 ? (Vector3){ 0.06f, 0.04f, 0.7f } : (Vector3){ 0.7f, 0.04f, 0.06f };
        DrawCubeV(bracket, bsize, (Color){ 60, 62, 70, 255 });
    }
}

/* ------------------------------------------------------------------ */
/* Camera                                                              */
/* ------------------------------------------------------------------ */
void render_update_camera(float dt)
{
    static const float FACE_YAW[4] = { -PI / 2, 0.0f, PI / 2, PI };
    Vector3 pos, target;
    float fovy;
    int snap = 0;

    G.yaw += angle_diff(G.yaw, FACE_YAW[G.facing]) * smooth_k(10.0f, dt);
    G.playerAnim += dt;
    G.playerRunBlend += ((player_is_moving() ? 1.0f : 0.0f) - G.playerRunBlend) * smooth_k(9.0f, dt);
    for (int i = 0; i < G.guardCount; i++) {
        Guard *g = &G.guards[i];
        int dx = g->pos.x - g->prev.x, dy = g->pos.y - g->prev.y;
        if (dx || dy)
            g->yaw += angle_diff(g->yaw, atan2f((float)dy, (float)dx)) * smooth_k(8.0f, dt);
        g->animTime += dt;
        g->runBlend += ((g->mode == G_CHASE ? 1.0f : 0.0f) - g->runBlend) * smooth_k(4.0f, dt);
    }

    Vector3 p = tile_world(G.playerDraw.x, G.playerDraw.y, 0.0f);
    float cy = cosf(G.yaw), sy = sinf(G.yaw);

    switch (G.camMode) {
    case CAM_OVERVIEW:
        target = (Vector3){ MAZE_W / 2.0f, 0.0f, MAZE_H / 2.0f + 1.2f };
        pos = (Vector3){ MAZE_W / 2.0f, 23.0f, MAZE_H / 2.0f + 17.5f };
        fovy = 45.0f;
        break;
    case CAM_FIRST_PERSON:
        pos = (Vector3){ p.x + cy * 0.08f, 1.0f, p.z + sy * 0.08f };
        target = (Vector3){ pos.x + cy, 0.92f, pos.z + sy };
        fovy = 72.0f;
        snap = 1;
        break;
    case CAM_THIRD_PERSON: {
        /* Over-the-shoulder spring arm: start at the shoulder pivot and move
         * back until just before the camera would enter a wall. */
        Vector3 right = { -sy, 0.0f, cy };
        Vector3 pivot = { p.x + right.x * 0.18f, 1.02f, p.z + right.z * 0.18f };
        float want = 1.65f, d = 0.0f;
        while (d < want) {
            float nd = d + 0.05f;
            if (world_blocked(pivot.x - cy * nd, pivot.z - sy * nd, 0.10f))
                break;
            d = nd;
        }
        /* Stay in the corridor if there is room; only back into the wall
         * behind if space is very cramped in a dead end. */
        if (d < 0.50f)
            d = 0.95f;
        /* the shorter the arm, the higher the camera, so it looks over the head */
        pos = (Vector3){ pivot.x - cy * d, fminf(1.18f + (want - d) * 0.16f, WALL_H - 0.08f), pivot.z - sy * d };
        tppArm = d;
        target = (Vector3){ p.x + cy * 2.0f + right.x * 0.12f, 0.82f, p.z + sy * 2.0f + right.z * 0.12f };
        fovy = 62.0f;
        snap = 1;   /* the player position and yaw are already smoothed */
        break;
    }
    default: { /* CAM_CHASE: high behind the player, north always "up" on screen */
        float lead = 0.6f;
        target = (Vector3){ p.x + DIR_X[G.facing] * lead, 0.4f, p.z + DIR_Y[G.facing] * lead };
        pos = (Vector3){ target.x, 6.4f, target.z + 3.9f };   /* walls in front get cut away */
        fovy = 50.0f;
        break;
    }
    }

    /* Respawns hard-cut; inside views follow the smoothed player directly. */
    if (G.inLocker) {
        float lx = G.lockerCell.x + 0.5f + DIR_X[G.lockerDir] * 0.45f;
        float lz = G.lockerCell.y + 0.5f + DIR_Y[G.lockerDir] * 0.45f;
        pos = (Vector3){ lx, 1.05f, lz };
        target = (Vector3){ lx + DIR_X[G.lockerDir] * 2.0f, 1.00f, lz + DIR_Y[G.lockerDir] * 2.0f };
        fovy = 70.0f;
        snap = 1;
    }

    if (G.camSnap || snap) {
        G.cam.position = pos;
        G.cam.target = target;
        G.cam.fovy = fovy;
        G.camSnap = 0;
    } else {
        float k = smooth_k(7.0f, dt);
        G.cam.position = Vector3Lerp(G.cam.position, pos, k);
        G.cam.target = Vector3Lerp(G.cam.target, target, k);
        G.cam.fovy += (fovy - G.cam.fovy) * k;
    }
    G.cam.up = (Vector3){ 0, 1, 0 };
    G.cam.projection = CAMERA_PERSPECTIVE;

    float wantWalls = (G.showBfs || G.waveActive) ? 0.2f : 1.0f;
    G.wallScale += (wantWalls - G.wallScale) * smooth_k(8.0f, dt);
}

/* ------------------------------------------------------------------ */
/* Shader uniforms                                                     */
/* ------------------------------------------------------------------ */
static void set_lights_one(LitShader *L, Vector3 lantern, float lanternRadius, float ambient, float fogDensity, float t)
{
    float view[3] = { G.cam.position.x, G.cam.position.y, G.cam.position.z };
    float lp[3] = { lantern.x, lantern.y, lantern.z };
    float fogc[3] = { C_BG.r / 255.0f, C_BG.g / 255.0f, C_BG.b / 255.0f };
    float gl[MAX_GUARDS * 4] = { 0 }, gt[MAX_GUARDS * 3] = { 0 }, lm[MAX_LAMPS * 4] = { 0 }, lt[MAX_LAMPS * 3] = { 0 };

    int gc = G.state == ST_MENU ? 0 : G.guardCount;
    for (int i = 0; i < gc; i++) {
        Guard *g = &G.guards[i];
        int chase = g->mode == G_CHASE;
        gl[i * 4 + 0] = g->draw.x + 0.5f + cosf(g->yaw) * 0.3f;
        gl[i * 4 + 1] = 0.7f;
        gl[i * 4 + 2] = g->draw.y + 0.5f + sinf(g->yaw) * 0.3f;
        gl[i * 4 + 3] = chase ? 1.4f : 0.8f;
        gt[i * 3 + 0] = 1.0f;
        gt[i * 3 + 1] = chase ? 0.18f : 0.55f;
        gt[i * 3 + 2] = chase ? 0.15f : 0.2f;
    }
    for (int i = 0; i < lampCount; i++) {
        lm[i * 4 + 0] = lamps[i].pos.x;
        lm[i * 4 + 1] = lamps[i].pos.y;
        lm[i * 4 + 2] = lamps[i].pos.z;
        lm[i * 4 + 3] = lamps[i].power * lamp_level(&lamps[i], t);
        lt[i * 3 + 0] = lamps[i].tint.x;
        lt[i * 3 + 1] = lamps[i].tint.y;
        lt[i * 3 + 2] = lamps[i].tint.z;
    }
    int open = G.keysTaken == G.keyCount && G.state != ST_MENU;
    float ex[4] = { G.exit.x + 0.5f, 0.8f, G.exit.y + 0.5f, open ? 1.3f : 0.0f };

    SetShaderValue(L->sh, L->viewPos, view, SHADER_UNIFORM_VEC3);
    SetShaderValue(L->sh, L->lantern, lp, SHADER_UNIFORM_VEC3);
    SetShaderValue(L->sh, L->lanternR, &lanternRadius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(L->sh, L->ambient, &ambient, SHADER_UNIFORM_FLOAT);
    SetShaderValueV(L->sh, L->guardLights, gl, SHADER_UNIFORM_VEC4, MAX_GUARDS);
    SetShaderValueV(L->sh, L->guardTints, gt, SHADER_UNIFORM_VEC3, MAX_GUARDS);
    SetShaderValue(L->sh, L->guardCount, &gc, SHADER_UNIFORM_INT);
    SetShaderValueV(L->sh, L->lamps, lm, SHADER_UNIFORM_VEC4, MAX_LAMPS);
    SetShaderValueV(L->sh, L->lampTints, lt, SHADER_UNIFORM_VEC3, MAX_LAMPS);
    SetShaderValue(L->sh, L->lampCount, &lampCount, SHADER_UNIFORM_INT);
    SetShaderValue(L->sh, L->exitL, ex, SHADER_UNIFORM_VEC4);
    SetShaderValue(L->sh, L->fogColor, fogc, SHADER_UNIFORM_VEC3);
    SetShaderValue(L->sh, L->fogDensity, &fogDensity, SHADER_UNIFORM_FLOAT);
}

static void set_lights(Vector3 lantern, float lanternRadius, float ambient, float fogDensity, float t)
{
    set_lights_one(&litNM, lantern, lanternRadius, ambient, fogDensity, t);
    set_lights_one(&litPlain, lantern, lanternRadius, ambient, fogDensity, t);
}

/* ------------------------------------------------------------------ */
/* Maze geometry                                                       */
/* ------------------------------------------------------------------ */
static void draw_maze_geometry(float wallScale, int dressing, float t)
{
    float h = WALL_H * wallScale;
    int inside = CAM_IS_INSIDE(G.camMode) && G.state != ST_MENU;

    lit_material(&litNM, 0.55f, 0.0f, 0.5f);   /* polished terrazzo: strong highlights */
    for (int y = 0; y < MAZE_H; y++)
        for (int x = 0; x < MAZE_W; x++)
            if (G.maze.grid[y][x] != TILE_WALL)
                box(floorM, tile_world((float)x, (float)y, -0.05f), (Vector3){ 1.0f, 0.1f, 1.0f }, WHITE);

    lit_material(&litNM, 0.06f, wallScale > 0.9f ? 0.13f : 0.0f, 0.55f);   /* plaster + skirting */
    for (int y = 0; y < MAZE_H; y++)
        for (int x = 0; x < MAZE_W; x++)
            if (G.maze.grid[y][x] == TILE_WALL) {
                float wh = wall_cut(x, y) ? 0.3f : h;
                box(wallM, tile_world((float)x, (float)y, wh / 2.0f), (Vector3){ 1.0f, wh, 1.0f },
                    (Color){ 238, 230, 216, 255 });
            }

    lit_material(&litPlain, 0.0f, 0.0f, 0.0f);
    if (inside && wallScale > 0.9f) {
        /* ceiling panels with a thin grid between them */
        for (int y = 0; y < MAZE_H; y++)
            for (int x = 0; x < MAZE_W; x++)
                if (G.maze.grid[y][x] != TILE_WALL) {
                    box(plainCube, tile_world((float)x, (float)y, WALL_H + 0.03f), (Vector3){ 1.0f, 0.06f, 1.0f },
                        (Color){ 60, 60, 64, 255 });
                    box(plainCube, tile_world((float)x, (float)y, WALL_H - 0.005f), (Vector3){ 0.94f, 0.01f, 0.94f },
                        (Color){ 205, 205, 198, 255 });
                }
    } else {
        /* wall tops: a slightly darker cap so the maze reads clearly from above */
        for (int y = 0; y < MAZE_H; y++)
            for (int x = 0; x < MAZE_W; x++)
                if (G.maze.grid[y][x] == TILE_WALL) {
                    float wh = wall_cut(x, y) ? 0.3f : h;
                    box(plainCube, tile_world((float)x, (float)y, wh + 0.005f), (Vector3){ 1.0f, 0.01f, 1.0f },
                        (Color){ 150, 140, 128, 255 });
                }
    }

    if (dressing && wallScale > 0.9f) {
        draw_props(t);
        draw_lamps(t);
    }
}

/* Paints the BFS distance field on the floor. During the replay only the
 * first `reveal` dequeued cells are shown and the newest ones (the queue's
 * frontier) rise up as glowing columns. */
static void draw_bfs_3d(const BfsField *f, int reveal, float brightness)
{
    int maxD = f->maxDist > 0 ? f->maxDist : 1;
    for (int i = 0; i < reveal && i < f->orderCount; i++) {
        Cell c = f->order[i];
        int d = f->dist[c.y][c.x];
        Color col = shade(mix(C_HEAT_NEAR, C_HEAT_FAR, (float)d / maxD), brightness);
        float hh = 0.04f;
        if (reveal < f->orderCount && i > reveal - 24) {
            float k = 1.0f - (float)(reveal - i) / 24.0f;
            col = mix(col, WHITE, 0.6f * k);
            hh = 0.04f + 0.55f * k;
        }
        DrawCube(tile_world((float)c.x, (float)c.y, hh / 2.0f + 0.005f), 0.86f, hh, 0.86f, col);
    }
}

/* ------------------------------------------------------------------ */
/* Characters                                                          */
/* ------------------------------------------------------------------ */
/* Soft contact shadow: three stacked translucent discs, darker in the middle */
static void draw_shadow(Vector3 p, float r)
{
    rlDisableDepthMask();
    for (int i = 0; i < 3; i++) {
        float rr = r * (1.0f - i * 0.28f);
        DrawCylinder((Vector3){ p.x, 0.004f + i * 0.001f, p.z }, rr, rr, 0.002f, 20, Fade(BLACK, 0.22f));
    }
    rlEnableDepthMask();
}

/* yaw = direction of travel in the maze plane, (cos, sin) = (X, Z).
 * Rotation about Y that turns the model's own forward onto it. */
static float model_angle_deg(const Character *c, float yaw)
{
    float travel = atan2f(cosf(yaw), sinf(yaw));   /* atan2(x, z) of the travel direction */
    return (travel - c->faceAng) * RAD2DEG;
}

/* Pose of a clip at a fractional frame: blends the two nearest frames, so
 * motion is smooth at any frame rate instead of stepping frame to frame. */
static void sample_clip(const ModelAnimation *c, float frame, Transform *out)
{
    int n = c->frameCount;
    float f = fmodf(frame, (float)n);
    if (f < 0)
        f += (float)n;
    int i0 = (int)f, i1 = (i0 + 1) % n;
    float a = f - (float)i0;
    for (int b = 0; b < c->boneCount; b++) {
        Transform A = c->framePoses[i0][b], B = c->framePoses[i1][b];
        out[b].translation = Vector3Lerp(A.translation, B.translation, a);
        out[b].rotation = QuaternionSlerp(A.rotation, B.rotation, a);
        out[b].scale = (Vector3){ 1, 1, 1 };
    }
}

/* Cross-fade: pose = clip A (at frameA) blended toward clip B (at frameB) by w,
 * bone by bone (positions lerped, rotations slerped), then drawn. */
static void draw_human_blend(Character *c, ModelAnimation *holder, const ModelAnimation *a, float frameA,
                             const ModelAnimation *b, float frameB, float w, Vector3 p, float yaw, Color tint)
{
    Transform *out = holder->framePoses[0];
    sample_clip(a, frameA, poseA);
    if (w > 0.001f) {
        sample_clip(b, frameB, poseB);
        for (int i = 0; i < holder->boneCount; i++) {
            out[i].translation = Vector3Lerp(poseA[i].translation, poseB[i].translation, w);
            out[i].rotation = QuaternionSlerp(poseA[i].rotation, poseB[i].rotation, w);
            out[i].scale = (Vector3){ 1, 1, 1 };
        }
    } else {
        memcpy(out, poseA, sizeof(Transform) * holder->boneCount);
    }
    UpdateModelAnimation(c->model, *holder, 0);
    DrawModelEx(c->model, p, (Vector3){ 0, 1, 0 }, model_angle_deg(c, yaw), (Vector3){ c->scale, c->scale, c->scale },
                tint);
}

static int player_is_moving(void)
{
    float dx = G.playerDraw.x - G.player.x, dy = G.playerDraw.y - G.player.y;
    return (G.moveTimer > 0.0f || dx * dx + dy * dy > 0.0004f) && !G.paused && !G.waveActive && G.state == ST_PLAY;
}

/* Stunned guard: electric ring at the feet and stars circling the head */
static void draw_stun_fx(Vector3 p, float stun, float t)
{
    float k = clampf(stun, 0.0f, 1.0f);   /* fades out in the last second */
    for (int i = 0; i < 3; i++) {
        float a = t * 4.0f + i * 2.094f;
        Vector3 s = { p.x + cosf(a) * 0.2f, CHAR_HEIGHT + 0.08f + 0.03f * sinf(t * 6.0f + i), p.z + sinf(a) * 0.2f };
        DrawSphere(s, 0.035f, Fade((Color){ 255, 235, 120, 255 }, k));
    }
    float r = 0.36f + 0.04f * sinf(t * 18.0f);
    DrawCircle3D(p, r, (Vector3){ 1, 0, 0 }, 90.0f, Fade(C_STUN, 0.75f * k));
    DrawCircle3D(p, r * 0.7f, (Vector3){ 1, 0, 0 }, 90.0f, Fade((Color){ 230, 245, 255, 255 }, 0.9f * k));
}

static void draw_guard(Guard *g, float t)
{
    Vector3 p = tile_world(g->draw.x, g->draw.y, 0.0f);
    int chase = g->mode == G_CHASE;
    int stunned = g->stun > 0.0f;
    float fx = cosf(g->yaw), fz = sinf(g->yaw);
    draw_shadow(p, 0.3f);

    if (guardChar.ok) {
        lit_material(&litPlain, 0.15f, 0.0f, 0.0f);
        int frozen = G.paused || G.waveActive || G.state != ST_PLAY || stunned;
        float f = g->animTime * 60.0f;
        Color tint = stunned ? (Color){ 150, 180, 255, 255 } : WHITE;
        if (frozen)
            draw_human_blend(&guardChar, &guardPose, &guardChar.idle, f, NULL, 0, 0.0f, p, g->yaw, tint);
        else   /* walk <-> run cross-fade when a guard starts or stops chasing */
            draw_human_blend(&guardChar, &guardPose, &guardChar.walk, f * 1.3f, &guardChar.run, f * 1.1f, g->runBlend,
                             p, g->yaw, tint);
    } else {
        Color body = stunned ? C_STUN : (chase ? C_DANGER : C_PATROL);
        DrawModelEx(cylM, p, (Vector3){ 0, 1, 0 }, 0.0f, (Vector3){ 0.27f, 0.55f, 0.27f }, body);
        DrawModelEx(sphereM, (Vector3){ p.x, 0.75f, p.z }, (Vector3){ 0, 1, 0 }, 0.0f, (Vector3){ 0.21f, 0.21f, 0.21f }, body);
    }
    if (stunned) {
        draw_stun_fx(p, g->stun, t);
        return;   /* torch and vision cone are off while stunned */
    }
    /* flashlight glow at hand height, ahead of the guard */
    float sx = -fz, sz = fx;
    DrawSphere((Vector3){ p.x + fx * 0.24f - sx * 0.13f, 0.58f, p.z + fz * 0.24f - sz * 0.13f }, 0.035f,
               chase ? (Color){ 255, 110, 100, 255 } : (Color){ 255, 225, 150, 255 });
}

static void draw_player(float t)
{
    Vector3 p = tile_world(G.playerDraw.x, G.playerDraw.y, 0.0f);
    draw_shadow(p, 0.28f);
    if (G.camMode != CAM_THIRD_PERSON)
        DrawCircle3D((Vector3){ p.x, 0.02f, p.z }, 0.42f + 0.03f * sinf(t * 3.0f), (Vector3){ 1, 0, 0 }, 90.0f,
                     Fade(C_ACCENT, 0.8f));

    if (playerChar.ok) {
        lit_material(&litPlain, 0.2f, 0.0f, 0.0f);
        float f = G.playerAnim * 60.0f;
        /* idle <-> run cross-fade: no snapping when you start or stop */
        draw_human_blend(&playerChar, &playerPose, &playerChar.idle, f, &playerChar.run, f, G.playerRunBlend, p, G.yaw,
                         WHITE);
    } else {
        DrawSphere((Vector3){ p.x, 0.36f, p.z }, 0.27f, C_ACCENT);
    }
}

static void draw_keys(float t)
{
    lit_material(&litPlain, 0.9f, 0.0f, 0.0f);
    for (int i = 0; i < G.keyCount; i++) {
        if (G.keys[i].taken)
            continue;
        Vector3 p = tile_world((float)G.keys[i].pos.x, (float)G.keys[i].pos.y,
                               0.45f + sinf(t * 3.0f + G.keys[i].phase) * 0.06f);
        draw_shadow((Vector3){ p.x, 0, p.z }, 0.16f);
        float a = t * 90.0f + G.keys[i].phase * 40.0f;
        DrawModelEx(cylM, (Vector3){ p.x, p.y - 0.11f, p.z }, (Vector3){ 0, 1, 0 }, a, (Vector3){ 0.02f, 0.22f, 0.02f },
                    (Color){ 255, 205, 80, 255 });
        DrawModelEx(sphereM, (Vector3){ p.x, p.y + 0.15f, p.z }, (Vector3){ 0, 1, 0 }, a, (Vector3){ 0.07f, 0.07f, 0.02f },
                    (Color){ 255, 215, 90, 255 });
        float c = cosf(a * DEG2RAD), s = sinf(a * DEG2RAD);
        DrawModelEx(plainCube, (Vector3){ p.x + c * 0.04f, p.y - 0.08f, p.z - s * 0.04f }, (Vector3){ 0, 1, 0 }, a,
                    (Vector3){ 0.07f, 0.03f, 0.015f }, (Color){ 255, 205, 80, 255 });
        DrawCircle3D(p, 0.2f, (Vector3){ 1, 0, 0 }, 90.0f, Fade(C_GOLD, 0.4f + 0.3f * sinf(t * 5.0f)));
    }
}

static void draw_pickups(float t)
{
    lit_material(&litPlain, 0.7f, 0.0f, 0.0f);
    for (int i = 0; i < G.pickupCount; i++) {
        Pickup *k = &G.pickups[i];
        if (k->taken)
            continue;
        float bob = 0.32f + sinf(t * 2.5f + k->phase) * 0.05f;
        Vector3 p = tile_world((float)k->pos.x, (float)k->pos.y, bob);
        float a = t * 70.0f + k->phase * 40.0f;
        draw_shadow((Vector3){ p.x, 0, p.z }, 0.17f);
        if (k->type == PICKUP_DART) {
            /* ammo box: blue body with silver banding */
            DrawModelEx(plainCube, p, (Vector3){ 0, 1, 0 }, a, (Vector3){ 0.28f, 0.18f, 0.22f }, (Color){ 70, 130, 230, 255 });
            DrawModelEx(plainCube, (Vector3){ p.x, p.y + 0.02f, p.z }, (Vector3){ 0, 1, 0 }, a,
                        (Vector3){ 0.29f, 0.04f, 0.23f }, (Color){ 220, 225, 235, 255 });
        } else {
            /* flash grenade: olive cylinder with silver cap and pin */
            DrawModelEx(cylM, p, (Vector3){ 0, 1, 0 }, a, (Vector3){ 0.12f, 0.22f, 0.12f }, (Color){ 90, 150, 80, 255 });
            DrawModelEx(sphereM, (Vector3){ p.x, p.y + 0.12f, p.z }, (Vector3){ 0, 1, 0 }, 0.0f,
                        (Vector3){ 0.09f, 0.06f, 0.09f }, (Color){ 220, 225, 235, 255 });
            DrawModelEx(plainCube, (Vector3){ p.x + 0.05f, p.y + 0.14f, p.z }, (Vector3){ 0, 1, 0 }, a,
                        (Vector3){ 0.08f, 0.02f, 0.02f }, (Color){ 240, 240, 240, 255 });
        }
    }
}

static void draw_dart(void)
{
    if (!G.dart.active)
        return;
    Vector3 p = tile_world(G.dart.pos.x, G.dart.pos.y, 0.38f);
    float a = atan2f(DIR_Y[G.dart.dir], DIR_X[G.dart.dir]) * RAD2DEG;
    DrawModelEx(cylM, p, (Vector3){ 0, 1, 0 }, -a + 90.0f, (Vector3){ 0.04f, 0.32f, 0.04f }, C_STUN);
    DrawSphere((Vector3){ p.x + DIR_X[G.dart.dir] * 0.15f, p.y, p.z + DIR_Y[G.dart.dir] * 0.15f }, 0.05f,
               (Color){ 230, 245, 255, 255 });
}

static void draw_exit(void)
{
    Vector3 p = tile_world((float)G.exit.x, (float)G.exit.y, 0.0f);
    if (G.keysTaken == G.keyCount) {
        DrawCube((Vector3){ p.x, 0.02f, p.z }, 0.92f, 0.04f, 0.92f, C_EXIT);
        DrawCube((Vector3){ p.x, 0.04f, p.z }, 0.68f, 0.02f, 0.68f, C_EXIT_GLOW);
        DrawSphere((Vector3){ p.x, 0.08f, p.z }, 0.12f, WHITE);
    } else {
        lit_material(&litPlain, 0.6f, 0.0f, 0.0f);
        for (int b = -2; b <= 2; b++)
            DrawModelEx(cylM, (Vector3){ p.x + b * 0.18f, 0.0f, p.z }, (Vector3){ 0, 1, 0 }, 0.0f,
                        (Vector3){ 0.035f, WALL_H * G.wallScale, 0.035f }, (Color){ 150, 150, 165, 255 });
        DrawSphere((Vector3){ p.x, 0.62f, p.z + 0.06f }, 0.08f, (Color){ 230, 70, 70, 255 });
    }
}

static void draw_exit_beam(float t)
{
    if (G.keysTaken != G.keyCount)
        return;
    Vector3 p = tile_world((float)G.exit.x, (float)G.exit.y, 0.0f);
    float pulse = 0.5f + 0.5f * sinf(t * 4.0f);
    float hgt = CAM_IS_INSIDE(G.camMode) ? WALL_H - 0.02f : 3.2f;
    rlDisableDepthMask();
    DrawCylinder(p, 0.38f, 0.44f, hgt, 20, Fade(C_EXIT, 0.22f + 0.12f * pulse));
    DrawCylinder(p, 0.18f, 0.22f, hgt, 16, Fade(C_EXIT_GLOW, 0.40f + 0.18f * pulse));
    rlEnableDepthMask();
}

static void draw_guard_paths(void)
{
    for (int i = 0; i < G.guardCount; i++) {
        Guard *g = &G.guards[i];
        Color pc = g->mode == G_CHASE ? C_DANGER : shade(C_PATROL, 0.6f);
        Cell c = g->pos;
        for (int s = 0; s < 80; s++) {
            Cell n = bfs_step_toward_source(&G.maze, &G.field, c);
            if (n.x == c.x && n.y == c.y)
                break;
            DrawCylinderEx(tile_world((float)c.x, (float)c.y, 0.14f), tile_world((float)n.x, (float)n.y, 0.14f),
                           0.06f, 0.06f, 6, pc);
            c = n;
        }
    }
}

int render_near_locker(Cell *outWall, int *outDir)
{
    float px = G.playerDraw.x + 0.5f;
    float pz = G.playerDraw.y + 0.5f;
    for (int i = 0; i < propCount; i++) {
        Prop *p = &props[i];
        if (p->type != PROP_LOCKERS)
            continue;
        float fx = p->wall.x + 0.5f + DIR_X[p->dir] * 0.5f;
        float fz = p->wall.y + 0.5f + DIR_Y[p->dir] * 0.5f;
        float dx = px - fx;
        float dz = pz - fz;
        if (dx * dx + dz * dz < 0.78f * 0.78f) {
            if (outWall) *outWall = p->wall;
            if (outDir) *outDir = p->dir;
            return 1;
        }
    }
    return 0;
}

static void draw_guard_vision_cone(const Guard *g)
{
    if (g->stun > 0.0f)
        return;
    Vector3 origin = { g->draw.x + 0.5f, 0.025f, g->draw.y + 0.5f };
    float len = 4.3f;
    float halfFov = 26.0f * DEG2RAD;
    float aCenter = g->yaw;
    Color beamCol = (g->mode == G_CHASE) ? Fade(C_DANGER, 0.32f) :
                    (g->mode == G_INVESTIGATE ? Fade(C_GOLD, 0.28f) : Fade((Color){ 255, 235, 170, 255 }, 0.20f));

    rlDisableDepthMask();
    int segments = 12;
    for (int i = 0; i < segments; i++) {
        float a1 = aCenter - halfFov + (2.0f * halfFov * i) / segments;
        float a2 = aCenter - halfFov + (2.0f * halfFov * (i + 1)) / segments;
        Vector3 p1 = { origin.x + cosf(a1) * len, 0.025f, origin.z + sinf(a1) * len };
        Vector3 p2 = { origin.x + cosf(a2) * len, 0.025f, origin.z + sinf(a2) * len };
        DrawTriangle3D(origin, p1, p2, beamCol);
        DrawTriangle3D(origin, p2, p1, beamCol);
        DrawLine3D(origin, p1, Fade(beamCol, 0.5f));
    }
    rlEnableDepthMask();
}

/* ------------------------------------------------------------------ */
/* Public drawing                                                      */
/* ------------------------------------------------------------------ */
static void draw_scene_3d(float t)
{
    Vector3 pp = tile_world(G.playerDraw.x, G.playerDraw.y, 0.95f);
    pp.x += cosf(G.yaw) * 0.15f;
    pp.z += sinf(G.yaw) * 0.15f;

    /* Lights off = stealth: lantern, torches, tube lights and moonlight.
     * The overview is a map, so it keeps enough light to read the maze. */
    float ambient = G.fog ? 0.1f : 0.5f;
    if (G.fog && G.camMode == CAM_OVERVIEW)
        ambient = 0.28f;
    if (G.fog && CAM_IS_INSIDE(G.camMode))
        ambient = 0.07f;
    if (G.showBfs || G.waveActive)
        ambient = 0.6f;
    float fogDensity = CAM_IS_INSIDE(G.camMode) ? 0.1f : 0.0f;
    set_lights(pp, CAM_IS_INSIDE(G.camMode) ? 5.0f : 6.0f, ambient, fogDensity, t);

    BeginMode3D(G.cam);
    draw_maze_geometry(G.wallScale, 1, t);

    if (G.waveActive)
        draw_bfs_3d(&G.field, (int)(G.waveTime / WAVE_TIME * G.field.orderCount), 1.0f);
    else if (G.showBfs) {
        draw_bfs_3d(&G.field, G.field.orderCount, 0.8f);
        draw_guard_paths();
    }

    draw_exit();
    draw_keys(t);
    draw_pickups(t);
    draw_dart();
    for (int i = 0; i < G.guardCount; i++) {
        draw_guard(&G.guards[i], t);
        draw_guard_vision_cone(&G.guards[i]);
    }
    if (G.coinActive) {
        if (G.coinPos.y > 0.045f) {
            /* In-flight coin: spinning gold disc with sparkling trajectory tail */
            Vector3 cTop = { G.coinPos.x, G.coinPos.y + 0.015f, G.coinPos.z };
            Vector3 cBot = { G.coinPos.x, G.coinPos.y - 0.015f, G.coinPos.z };
            DrawCylinderEx(cBot, cTop, 0.11f, 0.11f, 12, C_GOLD);
            DrawSphere(G.coinPos, 0.06f, (Color){ 255, 245, 180, 255 });

            /* Trajectory trail sparks in flight */
            for (int s = 1; s <= 3; s++) {
                Vector3 spark = {
                    G.coinPos.x - G.coinVel.x * 0.035f * s,
                    G.coinPos.y - G.coinVel.y * 0.035f * s,
                    G.coinPos.z - G.coinVel.z * 0.035f * s
                };
                DrawSphere(spark, 0.035f - s * 0.008f, Fade(C_GOLD, 0.65f - s * 0.18f));
            }
            float shadowR = 0.16f * (1.0f - clampf(G.coinPos.y / 1.5f, 0.0f, 0.5f));
            DrawCircle3D((Vector3){ G.coinPos.x, 0.015f, G.coinPos.z }, shadowR, (Vector3){ 1, 0, 0 }, 90.0f, Fade(BLACK, 0.60f));
        } else {
            /* Resting on floor: golden coin with expanding landing shockwave & beacon ring */
            Vector3 cBot = { G.coinPos.x, 0.01f, G.coinPos.z };
            Vector3 cTop = { G.coinPos.x, 0.035f, G.coinPos.z };
            DrawCylinderEx(cBot, cTop, 0.11f, 0.11f, 12, C_GOLD);
            DrawSphere((Vector3){ G.coinPos.x, 0.035f, G.coinPos.z }, 0.06f, (Color){ 255, 245, 180, 255 });

            /* Landing shockwave burst ripple (visible immediately after impact) */
            if (G.coinImpactTimer > 0.0f) {
                float shockProgress = 1.0f - G.coinImpactTimer;
                float shockR = 0.20f + shockProgress * 0.70f;
                DrawCircle3D((Vector3){ G.coinPos.x, 0.02f, G.coinPos.z }, shockR, (Vector3){ 1, 0, 0 }, 90.0f, Fade(C_GOLD, 0.75f * G.coinImpactTimer));
                DrawCircle3D((Vector3){ G.coinPos.x, 0.02f, G.coinPos.z }, shockR * 0.6f, (Vector3){ 1, 0, 0 }, 90.0f, Fade(WHITE, 0.50f * G.coinImpactTimer));
            }

            /* Continuous glowing floor beacon rings */
            float pRing1 = fmodf(t * 1.5f, 1.0f);
            float pRing2 = fmodf(t * 1.5f + 0.5f, 1.0f);
            DrawCircle3D((Vector3){ G.coinPos.x, 0.02f, G.coinPos.z }, 0.22f + pRing1 * 0.35f, (Vector3){ 1, 0, 0 }, 90.0f, Fade(C_GOLD, 0.60f * (1.0f - pRing1)));
            DrawCircle3D((Vector3){ G.coinPos.x, 0.02f, G.coinPos.z }, 0.22f + pRing2 * 0.35f, (Vector3){ 1, 0, 0 }, 90.0f, Fade(C_GOLD, 0.60f * (1.0f - pRing2)));

            /* Subtle vertical beacon column so coin is unmistakably visible */
            rlDisableDepthMask();
            DrawCylinder((Vector3){ G.coinPos.x, 0.02f, G.coinPos.z }, 0.16f, 0.20f, 0.50f, 12, Fade(C_GOLD, 0.18f + 0.08f * sinf(t * 4.0f)));
            rlEnableDepthMask();
        }
    }
    /* hide the player when in locker or when the camera is (nearly) inside them */
    if (!G.inLocker && G.camMode != CAM_FIRST_PERSON && !(G.camMode == CAM_THIRD_PERSON && tppArm < 0.4f))
        draw_player(t);
    draw_exit_beam(t);
    EndMode3D();
}

void render_world(void)
{
    float t = (float)GetTime();
    int rw = (int)(SCREEN_W * SSAA);
    int rh = (int)(SCREEN_H * SSAA);
    if (rw > 4096) rw = 4096;
    if (rh > 4096) rh = 4096;
    if (rw > 0 && rh > 0 && (sceneRT.texture.width != rw || sceneRT.texture.height != rh)) {
        UnloadRenderTexture(sceneRT);
        sceneRT = LoadRenderTexture(rw, rh);
    }

    /* Render at 1.5x size into a texture, then scale down through the grading shader */
    BeginTextureMode(sceneRT);
    ClearBackground(C_BG);
    draw_scene_3d(t);
    EndTextureMode();

    SetShaderValue(post, postTime, &t, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(post);
    DrawTexturePro(sceneRT.texture, (Rectangle){ 0, 0, (float)sceneRT.texture.width, -(float)sceneRT.texture.height },
                   (Rectangle){ 0, 0, (float)SCREEN_W, (float)SCREEN_H }, (Vector2){ 0, 0 }, 0.0f, WHITE);
    EndShaderMode();

    /* World-space labels drawn in 2D on top */
    if (G.showBfs && !G.waveActive) {
        for (int i = 0; i < G.field.orderCount; i++) {
            Cell c = G.field.order[i];
            float dx = (float)c.x - G.playerDraw.x;
            float dy = (float)c.y - G.playerDraw.y;
            if (dx * dx + dy * dy > 13.0f * 13.0f)
                continue;
            Vector3 w = tile_world((float)c.x, (float)c.y, 0.08f);
            if (!in_front_of_camera(w))
                continue;
            Vector2 s = GetWorldToScreen(w, G.cam);
            if (s.y < HUD_H + 8 || s.y > SCREEN_H - FOOTER_H - 8)
                continue;
            int d = G.field.dist[c.y][c.x];
            const char *txt = TextFormat("%d", d);
            int tw = MeasureText(txt, 11);
            int bw = tw + 8, bh = 15;
            DrawRectangle((int)s.x - bw / 2, (int)s.y - bh / 2, bw, bh, Fade((Color){ 10, 14, 26, 255 }, 0.90f));
            DrawRectangleLines((int)s.x - bw / 2, (int)s.y - bh / 2, bw, bh, Fade(C_ACCENT, 0.65f));
            DrawText(txt, (int)s.x - tw / 2, (int)s.y - 5, 11, WHITE);
        }
    }
    for (int i = 0; i < G.guardCount; i++) {
        Guard *g = &G.guards[i];
        if (g->stun > 0.0f) {
            Vector3 w = tile_world(g->draw.x, g->draw.y, CHAR_HEIGHT + 0.3f);
            if (!in_front_of_camera(w))
                continue;
            Vector2 s = GetWorldToScreen(w, G.cam);
            const char *txt = TextFormat("zZ %.0f", ceilf(g->stun));
            DrawText(txt, (int)s.x - MeasureText(txt, 20) / 2, (int)s.y - 22, 20, C_STUN);
            continue;
        }
        if (g->mode != G_CHASE)
            continue;
        Vector3 w = tile_world(g->draw.x, g->draw.y, CHAR_HEIGHT + 0.25f);
        if (!in_front_of_camera(w))
            continue;
        Vector2 s = GetWorldToScreen(w, G.cam);
        int size = 24 + (int)(clampf(g->alert, 0, 1) * 14);
        DrawText("!", (int)s.x - MeasureText("!", size) / 2, (int)s.y - size, size, C_DANGER);
    }
    if (G.flashTimer > 0.0f)   /* the flash also briefly whites out the screen */
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade((Color){ 240, 255, 235, 255 }, 0.55f * (G.flashTimer / 0.7f)));
}

void render_menu_background(void)
{
    float t = G.menuTime;
    float a = t * 0.12f;
    Vector3 c = { MAZE_W / 2.0f, 0.0f, MAZE_H / 2.0f };
    G.cam.target = c;
    G.cam.position = (Vector3){ c.x + cosf(a) * 24.0f, 17.0f, c.z + sinf(a) * 24.0f };
    G.cam.up = (Vector3){ 0, 1, 0 };
    G.cam.fovy = 50.0f;
    G.cam.projection = CAMERA_PERSPECTIVE;

    Vector3 src = tile_world((float)G.field.order[0].x, (float)G.field.order[0].y, 1.2f);
    set_lights(src, 9.0f, 0.3f, 0.035f, (float)GetTime());

    BeginMode3D(G.cam);
    draw_maze_geometry(0.7f, 0, (float)GetTime());
    float cycle = fmodf(t, 3.2f);
    draw_bfs_3d(&G.field, (int)(clampf(cycle / 2.2f, 0, 1) * G.field.orderCount), 0.85f);
    EndMode3D();
}

/* 2D overhead map: the raw 2D array, drawn small in a corner */
void render_minimap(int x0, int y0, int tile)
{
    int w = MAZE_W * tile, h = MAZE_H * tile;
    DrawRectangle(x0 - 4, y0 - 4, w + 8, h + 8, Fade(C_BG, 0.92f));
    DrawRectangleLines(x0 - 4, y0 - 4, w + 8, h + 8, Fade(C_ACCENT, 0.5f));

    /* Display entire maze map clearly without hiding anything */
    int maxD = G.field.maxDist > 0 ? G.field.maxDist : 1;
    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            if (G.maze.grid[y][x] == TILE_WALL) {
                DrawRectangle(x0 + x * tile, y0 + y * tile, tile, tile, (Color){ 72, 82, 124, 255 });
            } else {
                if (G.showBfs && G.field.dist[y][x] >= 0) {
                    Color heat = mix(C_HEAT_NEAR, C_HEAT_FAR, (float)G.field.dist[y][x] / (float)maxD);
                    DrawRectangle(x0 + x * tile, y0 + y * tile, tile, tile, Fade(heat, 0.85f));
                } else {
                    DrawRectangle(x0 + x * tile, y0 + y * tile, tile, tile, (Color){ 20, 24, 36, 255 });
                }
            }
        }
    }

    /* Distinct Green Lockers on Minimap */
    for (int i = 0; i < propCount; i++) {
        Prop *p = &props[i];
        if (p->type == PROP_LOCKERS) {
            DrawRectangle(x0 + p->wall.x * tile, y0 + p->wall.y * tile, tile, tile, (Color){ 36, 220, 130, 255 });
            DrawRectangleLines(x0 + p->wall.x * tile, y0 + p->wall.y * tile, tile, tile, (Color){ 16, 110, 65, 255 });
        }
    }

    /* All Keys (Always Visible) */
    for (int i = 0; i < G.keyCount; i++) {
        Cell kp = G.keys[i].pos;
        if (!G.keys[i].taken) {
            DrawRectangle(x0 + kp.x * tile, y0 + kp.y * tile, tile, tile, C_GOLD);
            DrawRectangleLines(x0 + kp.x * tile, y0 + kp.y * tile, tile, tile, (Color){ 160, 110, 20, 255 });
        }
    }

    /* Exit Door (Always Visible) */
    if (G.keysTaken == G.keyCount) {
        float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 6.0f);
        DrawRectangle(x0 + G.exit.x * tile, y0 + G.exit.y * tile, tile, tile, C_EXIT);
        DrawRectangleLines(x0 + G.exit.x * tile, y0 + G.exit.y * tile, tile, tile, Fade(WHITE, 0.6f + 0.4f * pulse));
    } else {
        DrawRectangle(x0 + G.exit.x * tile, y0 + G.exit.y * tile, tile, tile, (Color){ 90, 80, 115, 255 });
        DrawRectangleLines(x0 + G.exit.x * tile, y0 + G.exit.y * tile, tile, tile, (Color){ 140, 110, 170, 255 });
    }

    /* Pickups: blue dot for darts, green dot for flash grenade */
    for (int i = 0; i < G.pickupCount; i++) {
        Pickup *k = &G.pickups[i];
        if (!k->taken)
            DrawCircle(x0 + k->pos.x * tile + tile / 2, y0 + k->pos.y * tile + tile / 2, tile * 0.45f,
                       k->type == PICKUP_DART ? C_STUN : C_FLASH);
    }

    /* Flying stun dart on minimap */
    if (G.dart.active) {
        int dx = x0 + (int)(G.dart.pos.x * tile);
        int dy = y0 + (int)(G.dart.pos.y * tile);
        DrawCircle(dx, dy, tile * 0.5f, C_STUN);
    }

    /* Distraction Coin Position if Active (pulsing radar marker) */
    if (G.coinActive) {
        int cx = x0 + (int)(G.coinPos.x * tile);
        int cy = y0 + (int)(G.coinPos.z * tile);
        float p = fmodf((float)GetTime() * 2.5f, 1.0f);
        DrawCircle(cx, cy, tile * 0.7f, C_GOLD);
        DrawCircleLines(cx, cy, tile * (0.7f + p * 1.1f), Fade(C_GOLD, 1.0f - p));
    }

    /* All Guards Live on Minimap with Line of Sight Direction */
    for (int i = 0; i < G.guardCount; i++) {
        Guard *g = &G.guards[i];
        Color gc = (g->stun > 0.0f) ? C_STUN : ((g->mode == G_CHASE) ? C_DANGER : (g->mode == G_INVESTIGATE ? C_GOLD : C_PATROL));
        int gx = x0 + (int)((g->draw.x + 0.5f) * tile);
        int gy = y0 + (int)((g->draw.y + 0.5f) * tile);
        DrawCircle(gx, gy, tile * 0.65f, gc);
        DrawLine(gx, gy, gx + (int)(cosf(g->yaw) * tile * 1.6f), gy + (int)(sinf(g->yaw) * tile * 1.6f), gc);
    }

    /* Player Live with Heading */
    int px = x0 + (int)((G.playerDraw.x + 0.5f) * tile);
    int py = y0 + (int)((G.playerDraw.y + 0.5f) * tile);
    DrawLine(px, py, px + DIR_X[G.facing] * tile * 2, py + DIR_Y[G.facing] * tile * 2, WHITE);
    DrawCircle(px, py, tile * 0.7f, G.inLocker ? (Color){ 36, 220, 130, 255 } : C_ACCENT);
}
