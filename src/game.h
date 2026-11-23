/*
 * game.h - shared game state, used by main.c (logic + menus)
 * and render3d.c (3D world drawing).
 */
#ifndef GE_GAME_H
#define GE_GAME_H

#include "raylib.h"

#include "bfs.h"
#include "bst.h"
#include "maze.h"
#include "queue.h"

/* ------------------------------------------------------------------ */
/* Layout & tuning                                                     */
/* ------------------------------------------------------------------ */
#define DEFAULT_W 1280
#define DEFAULT_H 760

static inline int screen_width(void)
{
    int w = GetScreenWidth();
    return w > 0 ? w : DEFAULT_W;
}

static inline int screen_height(void)
{
    int h = GetScreenHeight();
    return h > 0 ? h : DEFAULT_H;
}

#define SCREEN_W  screen_width()
#define SCREEN_H  screen_height()
#define HUD_H     60
#define FOOTER_H  38

#define MAX_GUARDS   6
#define MAX_KEYS     6
#define START_LIVES  3
#define TOP_N        10
#define SAVE_FILE    "leaderboard.txt"

#define PLAYER_STEP  0.17f   /* seconds per tile (guards chase at 0.28s and slower) */
#define DETECT_DIST  6       /* BFS distance at which a guard hears you */
#define LOSE_DIST    11      /* beyond this distance a guard starts losing you */
#define LOSE_TIME    2.5f    /* seconds out of range before giving up */
#define WAVE_TIME    1.6f    /* duration of the BFS replay animation */

/* Weapons & Pickups */
#define DART_SPEED   16.0f   /* tiles per second */
#define DART_RANGE   9.0f    /* tiles before the dart drops */
#define FLASH_RADIUS 4       /* flash grenade stuns guards within this many BFS steps */
#define MAX_DARTS    9
#define MAX_FLASHES  3
#define MAX_PICKUPS  6

/* Colours shared by 2D and 3D drawing */
#define C_BG        (Color){ 10, 11, 18, 255 }
#define C_HUD       (Color){ 18, 20, 31, 255 }
#define C_TEXT      (Color){ 232, 234, 242, 255 }
#define C_MUTED     (Color){ 138, 145, 172, 255 }
#define C_ACCENT    (Color){ 88, 220, 255, 255 }
#define C_GOLD      (Color){ 255, 202, 64, 255 }
#define C_DANGER    (Color){ 242, 72, 72, 255 }
#define C_PATROL    (Color){ 240, 150, 60, 255 }
#define C_GREEN     (Color){ 80, 222, 132, 255 }
#define C_HEAT_NEAR (Color){ 255, 206, 72, 255 }
#define C_HEAT_FAR  (Color){ 96, 64, 210, 255 }
#define C_STUN      (Color){ 110, 170, 255, 255 }
#define C_FLASH     (Color){ 150, 240, 120, 255 }
#define C_EXIT      (Color){ 218, 68, 255, 255 }
#define C_EXIT_GLOW (Color){ 245, 175, 255, 255 }

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */
typedef enum {
    ST_MENU, ST_HELP, ST_PLAY, ST_CAUGHT, ST_LEVEL_CLEAR, ST_NAME_ENTRY, ST_LEADERBOARD
} GameState;

typedef enum { G_PATROL, G_CHASE, G_INVESTIGATE } GuardMode;

typedef enum { DIFF_EASY, DIFF_MEDIUM, DIFF_HARD, DIFF_COUNT } Difficulty;

/* Everything a difficulty level changes */
typedef struct {
    const char *name, *blurb;
    Color color;
    int lives;
    int extraGuards;       /* guards on level n = n + extraGuards */
    int detect;            /* BFS distance at which a guard hears you */
    int loseDist;          /* beyond this distance a chasing guard starts losing you */
    float loseTime;        /* seconds out of range before giving up */
    float guardPace;       /* multiplies guard step time (>1 = slower guards) */
    float stunTime;        /* seconds a guard stays stunned */
    int startDarts;
    int startFlashes;
    int pickups;           /* pickups spawned per level */
    int loops;             /* extra loops in the maze (higher = more open) */
    int scorePct;          /* score multiplier % */
} DiffConfig;

extern const DiffConfig DIFFS[DIFF_COUNT];

enum { MAP_RANDOM = 0, MAP_TOUR = MAZE_PRESET_COUNT + 1, MAP_CHOICES };

typedef enum { CAM_THIRD_PERSON, CAM_CHASE, CAM_FIRST_PERSON, CAM_OVERVIEW, CAM_COUNT } CamMode;

/* Tank controls (W/S walk, A/D turn) in the views that look along the corridor */
#define CAM_IS_INSIDE(m) ((m) == CAM_THIRD_PERSON || (m) == CAM_FIRST_PERSON)

typedef struct {
    int active;
    Vector2 pos;
    int dir;
    float travelled;
} Dart;

typedef enum { PICKUP_DART, PICKUP_FLASH, PICKUP_COUNT } PickupType;

typedef struct {
    Cell pos;
    PickupType type;
    int taken;
    float phase;
} Pickup;

typedef struct {
    Cell pos, prev, spawn;
    Vector2 draw;          /* smooth position in tile units (x, y = maze column, row) */
    float timer;           /* time since last step */
    float lostTimer;       /* time spent out of range while chasing */
    float alert;           /* "!" flash amount */
    float yaw;             /* smoothed facing angle for the 3D model */
    float animTime;        /* animation clock */
    float runBlend;        /* 0 = walking pose, 1 = running pose (cross-fade) */
    GuardMode mode;
    Cell target;           /* investigation target for coin distraction */
    float waitTimer;       /* pause at investigation target */
    float spotAlert;       /* flashlight spotlight line-of-sight alert */
    float stun;            /* seconds left while stunned */
} Guard;

typedef struct {
    Cell pos;
    int taken;
    float phase;
} Key;

typedef struct {
    GameState state;
    Maze maze;
    BfsField field;              /* BFS distance field from the player */

    Cell player, start, exit;
    Vector2 playerDraw;          /* smooth position in tile units */
    int facing;                  /* 0 up, 1 right, 2 down, 3 left */
    float yaw;                   /* smoothed facing angle (radians) */
    float playerAnim;            /* player animation clock */
    float playerRunBlend;        /* 0 = idle pose, 1 = running pose (cross-fade) */
    float moveTimer;

    /* Sprint & Stamina */
    float stamina;               /* 0.0f to 1.0f */
    int isSprinting;             /* 1 = sprinting at 1.45x speed */
    int exhausted;               /* 1 = exhausted penalty */

    /* Distraction Coin */
    int coins;                   /* remaining throwable coins (3 per level) */
    int coinActive;              /* 1 if coin is in flight or landed */
    Vector3 coinPos;
    Vector3 coinVel;
    float coinTimer;
    Cell coinCell;
    float playerThrowTimer;      /* arm throw gesture timer */
    float coinImpactTimer;       /* shockwave ripple on floor landing */

    /* Locker Hiding */
    int inLocker;                /* 1 = hiding inside a locker */
    Cell lockerCell;
    int lockerDir;

    /* Fog of War */
    unsigned char explored[MAZE_H][MAZE_W];

    /* Pathfinding Viva Comparison Mode: BFS vs A* */
    int useAstar;                /* 0 = BFS, 1 = A* */
    int astarNodesExplored;
    int astarPeakHeap;
    float astarTimeUs;
    int astarPathLen;
    int bfsNodesExplored;
    int bfsPeakQueue;
    float bfsTimeUs;

    /* Difficulty & Map Mode */
    Difficulty difficulty;
    int mapChoice;               /* menu selection: MAP_RANDOM, 1..MAZE_PRESET_COUNT, MAP_TOUR */
    int mapId;                   /* map used by current level */

    /* Weapons & Pickups */
    Dart dart;
    Pickup pickups[MAX_PICKUPS];
    int pickupCount;
    int darts, flashes;
    float flashTimer;
    Vector2 flashPos;

    Guard guards[MAX_GUARDS];
    int guardCount;
    Key keys[MAX_KEYS];
    int keyCount, keysTaken;

    int level, lives, score, lastBonus;
    float levelTime, stateTimer;
    int exitWarned, paused;
    int hasActiveGame;

    int showBfs, fog;
    int waveActive;
    float waveTime;

    CamMode camMode;
    Camera3D cam;
    int camSnap;                 /* 1 = hard-cut the camera next frame (respawn) */
    float wallScale;             /* walls sink while the BFS view is on */

    LogQueue log;

    ScoreNode *board;
    ScoreNode *lastNode;
    int lastRank;
    char name[NAME_LEN];
    int nameLen;

    float menuTime;
} Game;

extern Game G;

/* render3d.c */
extern int renderAssetsMissing;   /* 1 if the assets/ folder could not be found */
void render_init(void);
void render_unload(void);
void render_build_level(void);   /* places lamps and props for the current maze */
void render_update_camera(float dt);
void render_world(void);          /* 3D world + world-space labels */
void render_menu_background(void);
void render_minimap(int x, int y, int tile);
int render_near_locker(Cell *outWall, int *outDir);

#endif
