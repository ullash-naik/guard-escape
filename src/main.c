/*
 * GUARD ESCAPE - a 3D stealth maze game written in C with raylib.
 *
 * Sneak through a locked building at night, collect every key and reach
 * the exit before the guards catch you.
 *
 * Data structures (see the other files):
 *   maze.c     - 2D array grid + array-based stack for maze generation & preset maps
 *   queue.c    - circular queues (BFS frontier, on-screen message log)
 *   bfs.c      - breadth-first search: guards chase you on the shortest path
 *   astar.c    - A* search using binary min-heap priority queue
 *   bst.c      - binary search tree leaderboard, saved to leaderboard.txt
 *   audio.c    - procedural audio engine (footsteps, coins, locker, weapons, drone)
 *   render3d.c - turns the 2D array into a lit 3D scene (no game rules)
 */
#include "game.h"
#include "audio.h"
#include "astar.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

Game G;
static BfsField startField;   /* BFS from the start tile, used for spawning */

static const char *CAM_NAMES[CAM_COUNT] = { "THIRD PERSON", "CHASE", "FIRST PERSON", "OVERVIEW" };

/*                 name      blurb                                                     colour
 *                 lives guards hear lose loseT pace  stun darts flash pickups loops score% */
const DiffConfig DIFFS[DIFF_COUNT] = {
    { "EASY",   "5 lives, fewer and slower guards, long stuns, plenty of ammo", { 80, 222, 132, 255 },
      5, 0, 4,  8, 1.5f, 1.25f, 6.0f, 3, 1, 4, 24, 75 },
    { "MEDIUM", "3 lives, the classic game",                                    { 255, 202, 64, 255 },
      3, 1, 6, 11, 2.5f, 1.00f, 4.5f, 2, 1, 3, 14, 100 },
    { "HARD",   "2 lives, more and faster guards with sharp ears, short stuns",  { 242, 72, 72, 255 },
      2, 2, 8, 12, 3.5f, 0.85f, 3.0f, 1, 0, 2,  8, 150 },
};

#define DIFF (DIFFS[G.difficulty])

static const char *MAP_NAMES[MAP_CHOICES] = {
    "Random Maze", "Classrooms", "Library", "Courtyard", "Offices", "Tour (all maps)"
};
static const char *MAP_BLURBS[MAP_CHOICES] = {
    "A new maze every level, carved with a stack (randomized DFS)",
    "Rows of classrooms joined by two long corridors",
    "Long shelves and tall stacks - lots of places to break line of sight",
    "Two ring corridors around an open yard with a fountain",
    "Meeting rooms on both sides, cubicles in the middle",
    "Each level uses the next map: random, classrooms, library, courtyard, offices",
};

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
static int cell_eq(Cell a, Cell b) { return a.x == b.x && a.y == b.y; }

static Vector2 cell_pos(Cell c) { return (Vector2){ (float)c.x, (float)c.y }; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int mini(int a, int b) { return a < b ? a : b; }

static void text_center(const char *t, int cx, int y, int size, Color c)
{
    DrawText(t, cx - MeasureText(t, size) / 2, y, size, c);
}

static void logf_msg(const char *fmt, int a, int b)
{
    char buf[LOG_LEN];
    snprintf(buf, sizeof buf, fmt, a, b);
    log_push(&G.log, buf);
}

static int scaled(int points) { return points * DIFF.scorePct / 100; }

static float guard_step_time(GuardMode m)
{
    if (m == G_CHASE)
        return fmaxf(0.18f, 0.32f - 0.02f * G.level) * DIFF.guardPace;
    if (m == G_INVESTIGATE)
        return fmaxf(0.26f, 0.38f - 0.02f * G.level) * DIFF.guardPace;
    return fmaxf(0.32f, 0.46f - 0.02f * G.level) * DIFF.guardPace;
}

static int line_of_sight(Cell a, Cell b)
{
    int dx = abs(b.x - a.x);
    int dy = abs(b.y - a.y);
    int x = a.x;
    int y = a.y;
    int n = 1 + dx + dy;
    int x_inc = (b.x > a.x) ? 1 : -1;
    int y_inc = (b.y > a.y) ? 1 : -1;
    int error = dx - dy;
    dx *= 2;
    dy *= 2;

    for (; n > 0; --n) {
        if (!maze_walkable(&G.maze, x, y))
            return 0;
        if (x == b.x && y == b.y)
            return 1;
        if (error > 0) {
            x += x_inc;
            error -= dy;
        } else {
            y += y_inc;
            error += dx;
        }
    }
    return 1;
}

static void update_fog_of_war(void)
{
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            int nx = G.player.x + dx, ny = G.player.y + dy;
            if (nx >= 0 && nx < MAZE_W && ny >= 0 && ny < MAZE_H && (dx * dx + dy * dy <= 10)) {
                G.explored[ny][nx] = 1;
            }
        }
    }
}

static int map_for_level(int level)
{
    if (G.mapChoice == MAP_TOUR)
        return (level - 1) % MAP_TOUR;
    return G.mapChoice;
}

static void build_map(int mapId, int loops)
{
    if (mapId == MAP_RANDOM || !maze_load_preset(&G.maze, mapId - 1))
        maze_generate(&G.maze, loops);
}

/* ------------------------------------------------------------------ */
/* Weapons: Stun Darts & Flash Grenades                                */
/* ------------------------------------------------------------------ */
static void stun_guard(Guard *g)
{
    g->stun = DIFF.stunTime;
    g->mode = G_PATROL;     /* when it wakes up it has lost your trail */
    g->lostTimer = 0.0f;
    g->alert = 0.0f;
    g->timer = 0.0f;
    G.score += scaled(25);
    audio_play_dart_hit();
}

static void fire_dart(void)
{
    if (G.dart.active)
        return;
    if (G.darts <= 0) {
        log_push(&G.log, "No stun darts - find a blue ammo box");
        return;
    }
    G.darts--;
    G.dart = (Dart){ 1, G.playerDraw, G.facing, 0.0f };
    audio_play_dart_fire();
}

static void throw_flash(void)
{
    if (G.flashes <= 0) {
        log_push(&G.log, "No flash grenades - find a green one");
        return;
    }
    G.flashes--;
    G.flashTimer = 0.7f;
    G.flashPos = G.playerDraw;
    audio_play_flash();
    int hit = 0;
    for (int i = 0; i < G.guardCount; i++) {
        Guard *g = &G.guards[i];
        int d = G.field.dist[g->pos.y][g->pos.x];
        if (d >= 0 && d <= FLASH_RADIUS) {
            stun_guard(g);
            hit++;
        }
    }
    if (hit)
        logf_msg("FLASH! %d guard(s) stunned", hit, 0);
    else
        log_push(&G.log, "FLASH! ...but no guard was close enough");
}

static void update_dart(float dt)
{
    Dart *d = &G.dart;
    float left = DART_SPEED * dt;
    while (d->active && left > 0.0f) {
        float s = fminf(left, 0.2f);
        left -= s;
        d->pos.x += DIR_X[d->dir] * s;
        d->pos.y += DIR_Y[d->dir] * s;
        d->travelled += s;
        if (!maze_walkable(&G.maze, (int)floorf(d->pos.x + 0.5f), (int)floorf(d->pos.y + 0.5f)) ||
            d->travelled > DART_RANGE) {
            d->active = 0;
            break;
        }
        for (int i = 0; i < G.guardCount; i++) {
            Guard *g = &G.guards[i];
            float dx = g->draw.x - d->pos.x, dy = g->draw.y - d->pos.y;
            if (g->stun <= 0.0f && dx * dx + dy * dy < 0.45f * 0.45f) {
                stun_guard(g);
                logf_msg("Guard hit - stunned for %d seconds!", (int)DIFF.stunTime, 0);
                d->active = 0;
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Distraction Coin                                                   */
/* ------------------------------------------------------------------ */
static void update_coin(float dt)
{
    if (G.playerThrowTimer > 0.0f)
        G.playerThrowTimer -= dt;

    if (!G.coinActive)
        return;

    /* In-air flight */
    if (G.coinPos.y > 0.04f) {
        G.coinPos.x += G.coinVel.x * dt;
        G.coinPos.y += G.coinVel.y * dt;
        G.coinPos.z += G.coinVel.z * dt;
        G.coinVel.y -= 9.5f * dt;  /* gravity */

        /* Wall collision while flying: stop horizontal drift if hitting wall */
        int curX = (int)G.coinPos.x;
        int curZ = (int)G.coinPos.z;
        if (curX < 1 || curX >= MAZE_W - 1 || curZ < 1 || curZ >= MAZE_H - 1 ||
            !maze_walkable(&G.maze, curX, curZ)) {
            G.coinVel.x = 0.0f;
            G.coinVel.z = 0.0f;
        }

        /* Floor impact */
        if (G.coinPos.y <= 0.04f) {
            G.coinPos.y = 0.04f;
            G.coinVel = (Vector3){ 0, 0, 0 };
            G.coinImpactTimer = 1.0f;  /* trigger ground shockwave ripple */

            int cx = (int)G.coinPos.x;
            int cy = (int)G.coinPos.z;
            if (cx < 1) cx = 1;
            if (cx >= MAZE_W - 1) cx = MAZE_W - 2;
            if (cy < 1) cy = 1;
            if (cy >= MAZE_H - 1) cy = MAZE_H - 2;

            if (!maze_walkable(&G.maze, cx, cy)) {
                for (int d = 0; d < 4; d++) {
                    int nx = cx + DIR_X[d], ny = cy + DIR_Y[d];
                    if (maze_walkable(&G.maze, nx, ny)) {
                        cx = nx;
                        cy = ny;
                        break;
                    }
                }
                G.coinPos.x = cx + 0.5f;
                G.coinPos.z = cy + 0.5f;
            }
            G.coinCell = (Cell){ cx, cy };

            /* Play single, crisp, subtle coin landing sound once */
            audio_play_coin();

            int alerted = 0;
            for (int i = 0; i < G.guardCount; i++) {
                Guard *g = &G.guards[i];
                if (g->mode != G_CHASE && g->stun <= 0.0f) {
                    int dist = abs(g->pos.x - cx) + abs(g->pos.y - cy);
                    if (dist <= 12) {
                        g->mode = G_INVESTIGATE;
                        g->target = G.coinCell;
                        g->waitTimer = 3.2f;
                        g->alert = 0.9f;
                        alerted++;
                    }
                }
            }
            if (alerted) {
                log_push(&G.log, "Clink! Guards distracted by coin noise!");
            }
        }
    } else {
        /* Resting visibly on floor */
        G.coinPos.y = 0.04f;
        if (G.coinImpactTimer > 0.0f)
            G.coinImpactTimer -= dt;
        G.coinTimer += dt;
        if (G.coinTimer > 5.0f) {
            G.coinActive = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Level setup & Well-Distributed Spawning                            */
/* ------------------------------------------------------------------ */
static int spawn_taken(Cell c)
{
    if (cell_eq(c, G.start) || cell_eq(c, G.exit))
        return 1;
    for (int i = 0; i < G.keyCount; i++)
        if (cell_eq(G.keys[i].pos, c))
            return 1;
    for (int i = 0; i < G.guardCount; i++)
        if (cell_eq(G.guards[i].spawn, c))
            return 1;
    for (int i = 0; i < G.pickupCount; i++)
        if (cell_eq(G.pickups[i].pos, c))
            return 1;
    return 0;
}

static Cell pick_spawn(int minDist)
{
    for (int attempt = 0; attempt < 4000; attempt++) {
        int md = minDist - (attempt / 800) * 3;   /* relax if the maze is tight */
        if (md < 2)
            md = 2;
        Cell c = { 1 + rand() % (MAZE_W - 2), 1 + rand() % (MAZE_H - 2) };
        if (!maze_walkable(&G.maze, c.x, c.y))
            continue;
        if (startField.dist[c.y][c.x] < md || spawn_taken(c))
            continue;
        return c;
    }
    return (Cell){ MAZE_W - 2, 1 };
}

static void reset_positions(void)
{
    G.player = G.start;
    G.playerDraw = cell_pos(G.start);
    G.facing = maze_walkable(&G.maze, G.start.x + 1, G.start.y) ? 1 : 2;
    G.yaw = G.facing == 1 ? 0.0f : PI / 2;
    G.moveTimer = 0.0f;
    G.playerRunBlend = 0.0f;
    G.camSnap = 1;   /* hard-cut the camera so it doesn't fly across the maze */
    for (int i = 0; i < G.guardCount; i++) {
        Guard *g = &G.guards[i];
        g->pos = g->prev = g->spawn;
        g->draw = cell_pos(g->spawn);
        g->timer = -0.8f - 0.15f * i;     /* short grace period after a reset */
        g->lostTimer = 0.0f;
        g->alert = 0.0f;
        g->yaw = 0.0f;
        g->animTime = (float)(rand() % 100) / 37.0f;   /* guards don't step in sync */
        g->runBlend = 0.0f;
        g->mode = G_PATROL;
        g->stun = 0.0f;
    }
    G.dart.active = 0;
    G.flashTimer = 0.0f;
    bfs_compute(&G.maze, G.player, &G.field);
}

static void setup_level(void)
{
    G.mapId = map_for_level(G.level);
    build_map(G.mapId, DIFF.loops + G.level * 4);
    render_build_level();
    G.start = (Cell){ 1, 1 };
    G.exit = (Cell){ MAZE_W - 2, MAZE_H - 2 };
    bfs_compute(&G.maze, G.start, &startField);

    G.keyCount = 0;
    G.guardCount = 0;
    G.keysTaken = 0;
    G.pickupCount = 0;

    int nk = 2 + G.level;
    if (nk > MAX_KEYS)
        nk = MAX_KEYS;
    for (int i = 0; i < nk; i++) {
        Key k = { pick_spawn(6), 0, (float)(rand() % 100) / 10.0f };
        G.keys[G.keyCount++] = k;
    }

    /* weapon pickups: mostly stun darts, every third one a flash grenade */
    for (int i = 0; i < DIFF.pickups && i < MAX_PICKUPS; i++) {
        Pickup p = { pick_spawn(4), (i % 3 == 2) ? PICKUP_FLASH : PICKUP_DART, 0, (float)(rand() % 100) / 10.0f };
        G.pickups[G.pickupCount++] = p;
    }

    int ng = G.level + DIFF.extraGuards;
    if (ng > MAX_GUARDS)
        ng = MAX_GUARDS;
    for (int i = 0; i < ng; i++) {
        G.guards[G.guardCount].spawn = pick_spawn(10);
        G.guardCount++;
    }

    reset_positions();
    G.stamina = 1.0f;
    G.isSprinting = 0;
    G.exhausted = 0;
    G.coins = 3;
    G.coinActive = 0;
    G.coinImpactTimer = 0.0f;
    G.inLocker = 0;
    memset(G.explored, 0, sizeof(G.explored));
    update_fog_of_war();

    G.levelTime = 0.0f;
    G.exitWarned = 0;
    G.paused = 0;
    G.waveActive = 0;
    char lmsg[LOG_LEN];
    snprintf(lmsg, sizeof lmsg, "Level %d (%s) - find %d keys, then reach the exit", G.level, DIFF.name, G.keyCount);
    log_push(&G.log, lmsg);
}

static void new_game(void)
{
    G.level = 1;
    G.lives = DIFF.lives;
    G.score = 0;
    G.darts = DIFF.startDarts;
    G.flashes = DIFF.startFlashes;
    G.useAstar = 0;
    G.astarNodesExplored = 0;
    G.astarPeakHeap = 0;
    G.astarTimeUs = 0.0f;
    G.astarPathLen = 0;
    G.bfsNodesExplored = 0;
    G.bfsPeakQueue = 0;
    G.bfsTimeUs = 0.0f;
    G.hasActiveGame = 1;
    log_init(&G.log);
    setup_level();
    G.state = ST_PLAY;
}

static void menu_new_maze(void)
{
    build_map(G.mapChoice == MAP_TOUR ? MAP_RANDOM : G.mapChoice, 40);
    render_build_level();
    bfs_compute(&G.maze, (Cell){ 1 + 2 * (rand() % (MAZE_W / 2)), 1 + 2 * (rand() % (MAZE_H / 2)) }, &G.field);
}

static void menu_init(void)
{
    if (!G.hasActiveGame) {
        menu_new_maze();
        G.guardCount = 0;
    }
    G.menuTime = 0.0f;
    G.showBfs = 0;
    G.waveActive = 0;
    G.state = ST_MENU;
}

/* ------------------------------------------------------------------ */
/* Gameplay                                                            */
/* ------------------------------------------------------------------ */
static int guard_at(Cell c, int except)
{
    for (int i = 0; i < G.guardCount; i++)
        if (i != except && cell_eq(G.guards[i].pos, c))
            return 1;
    return 0;
}

static void on_player_enter_cell(void)
{
    /* Keys */
    for (int i = 0; i < G.keyCount; i++) {
        Key *k = &G.keys[i];
        if (!k->taken && cell_eq(k->pos, G.player)) {
            k->taken = 1;
            G.keysTaken++;
            G.score += scaled(100);
            if (G.keysTaken == G.keyCount) {
                log_push(&G.log, "All keys collected - the exit is OPEN!");
                audio_play_all_keys();
            } else {
                logf_msg("Key collected (%d/%d)", G.keysTaken, G.keyCount);
                audio_play_key();
            }
        }
    }

    /* Weapon Pickups */
    for (int i = 0; i < G.pickupCount; i++) {
        Pickup *p = &G.pickups[i];
        if (!p->taken && cell_eq(p->pos, G.player)) {
            p->taken = 1;
            if (p->type == PICKUP_DART) {
                G.darts = mini(G.darts + 2, MAX_DARTS);
                logf_msg("+2 STUN DARTS  (you have %d)", G.darts, 0);
            } else {
                G.flashes = mini(G.flashes + 1, MAX_FLASHES);
                logf_msg("+1 FLASH GRENADE  (you have %d)", G.flashes, 0);
            }
            audio_play_pickup();
            G.score += scaled(20);
        }
    }

    /* Exit check */
    if (cell_eq(G.player, G.exit)) {
        if (G.keysTaken == G.keyCount) {
            int timeBonus = (int)fmaxf(0.0f, 90.0f - G.levelTime) * 10;
            G.lastBonus = scaled(500 + timeBonus + G.lives * 100);
            G.score += G.lastBonus;
            G.state = ST_LEVEL_CLEAR;
            audio_play_level_clear();
        } else if (!G.exitWarned) {
            logf_msg("Exit locked - %d key(s) missing", G.keyCount - G.keysTaken, 0);
            G.exitWarned = 1;
        }
    } else {
        G.exitWarned = 0;
    }
}

static int try_move(int dir)
{
    if (G.inLocker)
        return 0;
    Cell next = { G.player.x + DIR_X[dir], G.player.y + DIR_Y[dir] };
    if (!maze_walkable(&G.maze, next.x, next.y))
        return 0;
    G.player = next;
    float step = G.isSprinting ? (PLAYER_STEP / 1.45f) : PLAYER_STEP;
    G.moveTimer = step;
    bfs_compute(&G.maze, G.player, &G.field);   /* new distance field */
    G.bfsNodesExplored = G.field.orderCount;
    G.bfsPeakQueue = G.field.maxQueue;
    G.bfsTimeUs = 14.0f;
    update_fog_of_war();
    on_player_enter_cell();
    return 1;
}

static void update_player(float dt)
{
    Cell lw; int ld;
    int nearLocker = render_near_locker(&lw, &ld);

    /* Locker interaction: SPACE ONLY when near/in locker */
    if (G.inLocker) {
        G.isSprinting = 0;
        if (IsKeyPressed(KEY_SPACE)) {
            G.inLocker = 0;
            audio_play_locker();
            log_push(&G.log, "Exited locker.");
        }
        return;
    }

    if (nearLocker && IsKeyPressed(KEY_SPACE)) {
        G.inLocker = 1;
        G.lockerCell = lw;
        G.lockerDir = ld;
        G.isSprinting = 0;
        audio_play_locker();
        log_push(&G.log, "Hidden in locker! Guards cannot detect you.");
        return;
    }

    /* Stun Dart: Z ONLY (or 1 as number shortcut). Never SPACE! */
    if (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_ONE)) {
        fire_dart();
    }

    /* Flash Grenade: E, or G, or 2, or K */
    if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_G) || IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_K)) {
        throw_flash();
    }

    /* Distraction coin toss: Q or T */
    if ((IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_T)) && G.coins > 0 && !G.coinActive) {
        G.coins--;
        G.coinActive = 1;
        G.coinTimer = 0.0f;
        G.playerThrowTimer = 0.35f;

        float fwdX = cosf(G.yaw);
        float fwdZ = sinf(G.yaw);

        /* Spawn coin in front of player at chest height */
        G.coinPos = (Vector3){
            G.playerDraw.x + 0.5f + fwdX * 0.30f,
            0.85f,
            G.playerDraw.y + 0.5f + fwdZ * 0.30f
        };
        float speed = 4.6f;
        G.coinVel = (Vector3){ fwdX * speed, 2.5f, fwdZ * speed };
        logf_msg("Tossed coin distraction! (%d left)", G.coins, 0);
    }

    int up = IsKeyDown(KEY_UP) || IsKeyDown(KEY_W);
    int down = IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S);
    int left = IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A);
    int right = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);

    /* Sprint & Stamina */
    int shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    int wantsMove = up || down || left || right;
    if (shiftDown && wantsMove && !G.exhausted && G.stamina > 0.05f) {
        G.isSprinting = 1;
        G.stamina -= dt * 0.32f;
        if (G.stamina <= 0.0f) {
            G.stamina = 0.0f;
            G.exhausted = 1;
            G.isSprinting = 0;
            audio_play_exhausted();
            log_push(&G.log, "Exhausted! Recovering stamina...");
        }
    } else {
        G.isSprinting = 0;
        if (G.exhausted) {
            G.stamina += dt * 0.22f;
            if (G.stamina >= 0.40f)
                G.exhausted = 0;
        } else if (G.stamina < 1.0f) {
            G.stamina += dt * 0.35f;
            if (G.stamina > 1.0f)
                G.stamina = 1.0f;
        }
    }

    if (CAM_IS_INSIDE(G.camMode)) {
        /* Third/first person: A/D turn 90 degrees, W/S walk forward/back */
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A))
            G.facing = (G.facing + 3) % 4;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))
            G.facing = (G.facing + 1) % 4;
        if (G.moveTimer > 0.0f) {
            G.moveTimer -= dt;
            return;
        }
        if (up)
            try_move(G.facing);
        else if (down)
            try_move((G.facing + 2) % 4);
        return;
    }

    if (G.moveTimer > 0.0f) {
        G.moveTimer -= dt;
        return;
    }

    /* Collect every pressed direction; take the first one that is open */
    int dirs[4], n = 0;
    if (up)    dirs[n++] = 0;
    if (right) dirs[n++] = 1;
    if (down)  dirs[n++] = 2;
    if (left)  dirs[n++] = 3;

    for (int i = 0; i < n; i++) {
        if (try_move(dirs[i])) {
            G.facing = dirs[i];
            return;
        }
    }
}

static Cell chase_step(int gi)
{
    Guard *g = &G.guards[gi];
    if (G.useAstar) {
        AStarResult res = astar_search(&G.maze, g->pos, G.player);
        G.astarNodesExplored = res.nodesExplored;
        G.astarPeakHeap = res.peakHeapSize;
        G.astarTimeUs = res.timeMicroseconds;
        G.astarPathLen = res.pathLength;
        if (res.reachable && !guard_at(res.nextStep, gi))
            return res.nextStep;
    }

    int cur = G.field.dist[g->pos.y][g->pos.x];
    Cell opts[4];
    int n = 0;
    for (int d = 0; d < 4; d++) {
        Cell c = { g->pos.x + DIR_X[d], g->pos.y + DIR_Y[d] };
        if (!maze_walkable(&G.maze, c.x, c.y) || guard_at(c, gi))
            continue;
        int dc = G.field.dist[c.y][c.x];
        if (dc >= 0 && dc < cur)
            opts[n++] = c;
    }
    return n ? opts[rand() % n] : g->pos;
}

static Cell patrol_step(int gi)
{
    Guard *g = &G.guards[gi];
    Cell opts[4];
    int n = 0;
    Cell straight = { g->pos.x + (g->pos.x - g->prev.x), g->pos.y + (g->pos.y - g->prev.y) };
    int hasStraight = 0;

    for (int d = 0; d < 4; d++) {
        Cell c = { g->pos.x + DIR_X[d], g->pos.y + DIR_Y[d] };
        if (!maze_walkable(&G.maze, c.x, c.y) || cell_eq(c, g->prev) || guard_at(c, gi))
            continue;
        if (cell_eq(c, straight))
            hasStraight = 1;
        opts[n++] = c;
    }
    if (hasStraight && rand() % 100 < 60)
        return straight;
    if (n)
        return opts[rand() % n];
    if (!guard_at(g->prev, gi))
        return g->prev;   /* dead end: turn around */
    return g->pos;
}

static Cell investigate_step(int gi)
{
    Guard *g = &G.guards[gi];
    if (cell_eq(g->pos, g->target) || (abs(g->pos.x - g->target.x) + abs(g->pos.y - g->target.y) <= 1))
        return g->pos;

    if (G.useAstar) {
        AStarResult res = astar_search(&G.maze, g->pos, g->target);
        if (res.reachable && !guard_at(res.nextStep, gi))
            return res.nextStep;
    }

    BfsField tf;
    bfs_compute(&G.maze, g->target, &tf);
    int cur = tf.dist[g->pos.y][g->pos.x];
    Cell opts[4];
    int n = 0;
    for (int d = 0; d < 4; d++) {
        Cell c = { g->pos.x + DIR_X[d], g->pos.y + DIR_Y[d] };
        if (maze_walkable(&G.maze, c.x, c.y) && !guard_at(c, gi) && tf.dist[c.y][c.x] >= 0 && tf.dist[c.y][c.x] < cur)
            opts[n++] = c;
    }
    return n ? opts[rand() % n] : g->pos;
}

static void update_guards(float dt)
{
    /* If player is safely inside a locker, guards cannot see or hear them */
    if (G.inLocker) {
        for (int i = 0; i < G.guardCount; i++) {
            Guard *g = &G.guards[i];
            if (g->mode == G_CHASE) {
                g->mode = G_PATROL;
                g->lostTimer = 0.0f;
            }
        }
    } else {
        /* Flashlight line-of-sight visual detection cone */
        for (int i = 0; i < G.guardCount; i++) {
            Guard *g = &G.guards[i];
            if (g->stun > 0.0f)
                continue;
            float fx = cosf(g->yaw);
            float fy = sinf(g->yaw);
            float dx = G.playerDraw.x - g->draw.x;
            float dy = G.playerDraw.y - g->draw.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist <= 4.2f && dist > 0.05f) {
                float nx = dx / dist, ny = dy / dist;
                float dot = fx * nx + fy * ny;
                if (dot > 0.84f && line_of_sight(g->pos, G.player)) {
                    if (g->mode != G_CHASE) {
                        g->mode = G_CHASE;
                        g->alert = 1.0f;
                        g->lostTimer = 0.0f;
                        g->spotAlert = 1.0f;
                        audio_play_spotlight_alert();
                        log_push(&G.log, "Spotted in guard's flashlight!");
                    }
                }
            }
        }
    }

    for (int i = 0; i < G.guardCount; i++) {
        Guard *g = &G.guards[i];
        if (g->stun > 0.0f) {
            g->stun -= dt;
            continue;
        }

        int d = G.field.dist[g->pos.y][g->pos.x];

        if (!G.inLocker && g->mode == G_PATROL && d >= 0 && d <= DIFF.detect) {
            g->mode = G_CHASE;
            g->lostTimer = 0.0f;
            g->alert = 1.0f;
            log_push(&G.log, "A guard heard you!");
            audio_play_alert();
        } else if (g->mode == G_CHASE) {
            if (G.inLocker || d < 0 || d > DIFF.loseDist) {
                g->lostTimer += dt;
                if (g->lostTimer > DIFF.loseTime || G.inLocker) {
                    g->mode = G_PATROL;
                    log_push(&G.log, "A guard lost your trail");
                }
            } else {
                g->lostTimer = 0.0f;
            }
        } else if (g->mode == G_INVESTIGATE) {
            if (!G.inLocker && d >= 0 && d <= 3) {
                g->mode = G_CHASE;
                g->lostTimer = 0.0f;
                g->alert = 1.0f;
                audio_play_alert();
            }
        }

        if (g->alert > 0.0f)
            g->alert -= dt * 1.2f;

        g->timer += dt;
        float step = guard_step_time(g->mode);
        if (g->timer < step)
            continue;
        g->timer -= step;
        if (g->timer > step)
            g->timer = 0.0f;

        Cell next = g->pos;
        if (g->mode == G_CHASE) {
            next = chase_step(i);
        } else if (g->mode == G_INVESTIGATE) {
            if (cell_eq(g->pos, g->target) || (abs(g->pos.x - g->target.x) + abs(g->pos.y - g->target.y) <= 1)) {
                g->waitTimer -= step;
                if (g->waitTimer <= 0.0f) {
                    g->mode = G_PATROL;
                    g->alert = 0.0f;
                    log_push(&G.log, "Guard finished inspecting coin area.");
                }
            } else {
                next = investigate_step(i);
            }
        } else {
            next = patrol_step(i);
        }

        g->prev = g->pos;
        g->pos = next;
    }
}

static void check_caught(void)
{
    if (G.inLocker)
        return;

    for (int i = 0; i < G.guardCount; i++) {
        Guard *g = &G.guards[i];
        if (g->stun > 0.0f)
            continue;   /* stunned guards cannot catch you */
        float dx = g->draw.x - G.playerDraw.x, dy = g->draw.y - G.playerDraw.y;
        if (cell_eq(g->pos, G.player) || dx * dx + dy * dy < 0.55f * 0.55f) {
            G.lives--;
            G.state = ST_CAUGHT;
            G.stateTimer = 1.7f;
            log_push(&G.log, "CAUGHT! Back to the start.");
            audio_play_caught();
            return;
        }
    }
}

/* Constant-speed glide toward a tile */
static void glide(Vector2 *p, Vector2 target, float tilesPerSecond, float dt)
{
    float dx = target.x - p->x, dy = target.y - p->y;
    float dist = sqrtf(dx * dx + dy * dy);
    float step = tilesPerSecond * dt;
    if (dist > 1.5f || dist <= step) {
        *p = target;
        return;
    }
    p->x += dx / dist * step;
    p->y += dy / dist * step;
}

static void update_draw_positions(float dt)
{
    float pSpeed = (G.isSprinting ? 1.45f : 1.0f) * (1.06f / PLAYER_STEP);
    glide(&G.playerDraw, cell_pos(G.player), pSpeed, dt);
    for (int i = 0; i < G.guardCount; i++) {
        Guard *g = &G.guards[i];
        glide(&g->draw, cell_pos(g->pos), 1.06f / guard_step_time(g->mode), dt);
    }
}

/* ------------------------------------------------------------------ */
/* 2D overlay drawing                                                  */
/* ------------------------------------------------------------------ */
static void draw_key_icon(Vector2 c, float s, Color col)
{
    DrawCircleV((Vector2){ c.x - 5 * s, c.y }, 5.0f * s, col);
    DrawCircleV((Vector2){ c.x - 5 * s, c.y }, 2.2f * s, C_HUD);
    DrawRectangle((int)(c.x - 1 * s), (int)(c.y - 1.5f * s), (int)(10 * s), (int)(3 * s), col);
    DrawRectangle((int)(c.x + 5 * s), (int)(c.y + 1 * s), (int)(2 * s), (int)(4 * s), col);
    DrawRectangle((int)(c.x + 8 * s), (int)(c.y + 1 * s), (int)(2 * s), (int)(3 * s), col);
}

static void draw_heart(int x, int y, Color col)
{
    DrawCircle(x - 4, y, 5, col);
    DrawCircle(x + 4, y, 5, col);
    DrawTriangle((Vector2){ (float)x - 9, (float)y + 1 }, (Vector2){ (float)x, (float)y + 11 },
                 (Vector2){ (float)x + 9, (float)y + 1 }, col);
}

static void draw_log(void)
{
    for (int i = 0; i < G.log.count; i++) {
        float age;
        const char *msg = log_get(&G.log, i, &age);
        float a = clampf(LOG_LIFE - age, 0.0f, 1.0f);
        int y = SCREEN_H - FOOTER_H - 14 - (G.log.count - i) * 26;
        int w = MeasureText(msg, 18);
        DrawRectangle(12, y - 4, w + 18, 25, Fade(C_BG, 0.75f * a));
        DrawText(msg, 21, y, 18, Fade(C_TEXT, a));
    }
}

static void draw_footer_key(int *curX, int fy, const char *key, const char *action, Color keyColor)
{
    int x = *curX;
    DrawText(key, x, fy + 10, 15, keyColor);
    x += MeasureText(key, 15) + 5;
    DrawText(action, x, fy + 10, 15, (Color){ 230, 236, 248, 255 });
    x += MeasureText(action, 15) + 11;
    DrawText("|", x, fy + 10, 14, (Color){ 65, 75, 105, 255 });
    x += 11;
    *curX = x;
}

static void draw_footer(void)
{
    int fy = SCREEN_H - FOOTER_H;
    DrawRectangle(0, fy, SCREEN_W, FOOTER_H, (Color){ 12, 14, 24, 255 });
    DrawRectangle(0, fy, SCREEN_W, 1, (Color){ 45, 65, 105, 255 });

    int curX = 16;
    if (G.showBfs) {
        draw_footer_key(&curX, fy, "[M]", "CLOSE BFS MAP", C_ACCENT);
        draw_footer_key(&curX, fy, "[B]", "Replay Wave", C_STUN);
        draw_footer_key(&curX, fy, "[X]", G.useAstar ? "AI: A* Min-Heap" : "AI: BFS Queue", G.useAstar ? C_ACCENT : C_GOLD);
        draw_footer_key(&curX, fy, "[H]", "How to Play", (Color){ 255, 205, 75, 255 });
        draw_footer_key(&curX, fy, "[ESC]", "Menu / Pause", (Color){ 255, 120, 120, 255 });
    } else {
        draw_footer_key(&curX, fy, "[M]", "AI BFS MAP", C_ACCENT);
        draw_footer_key(&curX, fy, "[SHIFT]", "Sprint", C_GOLD);
        draw_footer_key(&curX, fy, "[Z]", "Dart", C_STUN);
        draw_footer_key(&curX, fy, "[E]", "Flash", C_FLASH);
        draw_footer_key(&curX, fy, "[Q]", "Coin", C_GOLD);
        draw_footer_key(&curX, fy, "[SPACE]", "Locker", (Color){ 60, 235, 140, 255 });
        draw_footer_key(&curX, fy, "[C]", "Cam", C_ACCENT);
        draw_footer_key(&curX, fy, "[X]", G.useAstar ? "AI: A*" : "AI: BFS", G.useAstar ? C_ACCENT : (Color){ 180, 195, 225, 255 });
        draw_footer_key(&curX, fy, "[H]", "How to Play", (Color){ 255, 205, 75, 255 });
        draw_footer_key(&curX, fy, "[ESC]", "Menu", (Color){ 255, 120, 120, 255 });
    }
}

static void draw_hud(void)
{
    /* Background bar */
    DrawRectangle(0, 0, SCREEN_W, HUD_H, (Color){ 12, 14, 24, 255 });
    DrawRectangle(0, HUD_H - 2, SCREEN_W, 2, (Color){ 45, 65, 105, 255 });
    DrawRectangle(0, HUD_H - 1, SCREEN_W, 1, Fade(C_ACCENT, 0.45f));

    /* 1. Title Badge (Zero overlap with Level!) */
    Rectangle titleR = { 10, 8, 150, 44 };
    DrawRectangleRounded(titleR, 0.28f, 6, (Color){ 20, 26, 42, 255 });
    DrawRectangleRoundedLinesEx(titleR, 0.28f, 6, 1.2f, Fade(C_ACCENT, 0.75f));
    text_center("GUARD ESCAPE", (int)(titleR.x + titleR.width / 2), (int)titleR.y + 13, 17, C_ACCENT);

    /* 2. Level & Score Badge */
    int px = 168;
    Rectangle progR = { (float)px, 8, 172, 44 };
    DrawRectangleRounded(progR, 0.28f, 6, (Color){ 20, 26, 42, 255 });
    DrawRectangleRoundedLinesEx(progR, 0.28f, 6, 1.0f, Fade((Color){ 60, 75, 110, 255 }, 0.7f));
    DrawText(TextFormat("LVL %d", G.level), px + 12, 19, 18, WHITE);
    DrawRectangle(px + 72, 14, 1, 32, (Color){ 45, 55, 80, 255 });
    DrawText(TextFormat("%d", G.score), px + 82, 19, 18, C_GOLD);

    /* 3. Golden Keys Badge */
    int kx = px + 180;
    int kw = 82;
    int allKeys = (G.keysTaken == G.keyCount && G.keyCount > 0);
    Rectangle keyR = { (float)kx, 8, (float)kw, 44 };
    DrawRectangleRounded(keyR, 0.28f, 6, allKeys ? Fade(C_EXIT, 0.25f) : (Color){ 20, 26, 42, 255 });
    DrawRectangleRoundedLinesEx(keyR, 0.28f, 6, 1.2f, allKeys ? C_EXIT : Fade(C_GOLD, 0.65f));
    draw_key_icon((Vector2){ (float)(kx + 16), 30 }, 1.15f, allKeys ? C_EXIT_GLOW : C_GOLD);
    const char *kStr = allKeys ? "EXIT!" : TextFormat("%d/%d", G.keysTaken, G.keyCount);
    DrawText(kStr, kx + 30, 19, 18, allKeys ? C_EXIT_GLOW : WHITE);

    /* 4. Stamina Gauge */
    int sx = kx + kw + 8;
    int sw = 84;
    Rectangle stamBox = { (float)sx, 8, (float)sw, 44 };
    DrawRectangleRounded(stamBox, 0.28f, 6, (Color){ 20, 26, 42, 255 });
    DrawRectangleRoundedLinesEx(stamBox, 0.28f, 6, 1.0f, Fade((Color){ 60, 75, 110, 255 }, 0.7f));
    DrawText("STAMINA", sx + 11, 13, 11, G.exhausted ? C_DANGER : (Color){ 170, 185, 215, 255 });
    int barY = 28, barW = sw - 20, barH = 11;
    DrawRectangle(sx + 10, barY, barW, barH, (Color){ 32, 38, 54, 255 });
    Color sCol = G.exhausted ? C_DANGER : (G.stamina > 0.45f ? C_GREEN : C_GOLD);
    DrawRectangle(sx + 10, barY, (int)(barW * G.stamina), barH, sCol);
    DrawRectangleLines(sx + 10, barY, barW, barH, Fade((Color){ 60, 75, 110, 255 }, 0.5f));

    /* 5. Lives (Hearts) */
    int lx = sx + sw + 8;
    int lw = DIFF.lives * 22 + 14;
    Rectangle livesBox = { (float)lx, 8, (float)lw, 44 };
    DrawRectangleRounded(livesBox, 0.28f, 6, (Color){ 20, 26, 42, 255 });
    DrawRectangleRoundedLinesEx(livesBox, 0.28f, 6, 1.0f, Fade((Color){ 60, 75, 110, 255 }, 0.7f));
    for (int i = 0; i < DIFF.lives; i++)
        draw_heart(lx + 10 + i * 22, 30, i < G.lives ? (Color){ 255, 65, 80, 255 } : (Color){ 55, 60, 80, 255 });

    /* 6. Ammo - Stun Darts */
    int ax = lx + lw + 8;
    Rectangle dartR = { (float)ax, 8, 108, 44 };
    DrawRectangleRounded(dartR, 0.28f, 6, (Color){ 20, 26, 42, 255 });
    DrawRectangleRoundedLinesEx(dartR, 0.28f, 6, 1.0f, Fade(C_STUN, 0.5f));
    DrawRectangle(ax + 10, 20, 9, 20, C_STUN);
    DrawRectangleLines(ax + 10, 20, 9, 20, WHITE);
    DrawText(TextFormat("x%d [Z]", G.darts), ax + 26, 19, 17, C_STUN);

    /* 7. Flash Grenades */
    int fx = ax + 116;
    Rectangle flashR = { (float)fx, 8, 92, 44 };
    DrawRectangleRounded(flashR, 0.28f, 6, (Color){ 20, 26, 42, 255 });
    DrawRectangleRoundedLinesEx(flashR, 0.28f, 6, 1.0f, Fade(C_FLASH, 0.5f));
    DrawCircle(fx + 15, 30, 8, C_FLASH);
    DrawCircleLines(fx + 15, 30, 8, WHITE);
    DrawText(TextFormat("x%d [E]", G.flashes), fx + 28, 19, 17, C_FLASH);

    /* 8. Distraction Coins */
    int cx = fx + 100;
    Rectangle coinR = { (float)cx, 8, 92, 44 };
    DrawRectangleRounded(coinR, 0.28f, 6, (Color){ 20, 26, 42, 255 });
    DrawRectangleRoundedLinesEx(coinR, 0.28f, 6, 1.0f, Fade(C_GOLD, 0.5f));
    DrawCircle(cx + 15, 30, 8, C_GOLD);
    DrawCircleLines(cx + 15, 30, 8, WHITE);
    DrawText("$", cx + 12, 23, 11, C_BG);
    DrawText(TextFormat("x%d [Q]", G.coins), cx + 28, 19, 17, C_GOLD);

    /* Right-aligned modules: Time, Camera Mode, Difficulty */
    int timeW = 112;
    int timeX = SCREEN_W - timeW - 10;
    Rectangle timeR = { (float)timeX, 8, (float)timeW, 44 };
    DrawRectangleRounded(timeR, 0.28f, 6, (Color){ 20, 26, 42, 255 });
    DrawRectangleRoundedLinesEx(timeR, 0.28f, 6, 1.0f, Fade(C_ACCENT, 0.55f));
    int secs = (int)G.levelTime;
    const char *tm = TextFormat("%02d:%02d", secs / 60, secs % 60);
    DrawText("TIME", timeX + 10, 20, 11, (Color){ 160, 175, 205, 255 });
    DrawText(tm, timeX + 44, 18, 19, WHITE);

    int camW = 132;
    int camX = timeX - camW - 8;
    Rectangle camR = { (float)camX, 10, (float)camW, 40 };
    DrawRectangleRounded(camR, 0.28f, 4, (Color){ 20, 26, 42, 255 });
    DrawRectangleRoundedLinesEx(camR, 0.28f, 4, 1.0f, Fade((Color){ 60, 75, 110, 255 }, 0.6f));
    text_center(TextFormat("CAM: %s", CAM_NAMES[G.camMode]), camX + camW / 2, 22, 14, (Color){ 225, 232, 245, 255 });

    int diffW = 76;
    int diffX = camX - diffW - 8;
    Rectangle diffR = { (float)diffX, 10, (float)diffW, 40 };
    DrawRectangleRounded(diffR, 0.28f, 4, Fade(DIFF.color, 0.22f));
    DrawRectangleRoundedLinesEx(diffR, 0.28f, 4, 1.2f, DIFF.color);
    text_center(DIFF.name, diffX + diffW / 2, 21, 15, DIFF.color);

    draw_footer();
}

static void draw_bfs_row(int panelX, int panelW, int *cy, const char *label, const char *valStr, Color valColor)
{
    DrawText(label, panelX + 16, *cy, 14, (Color){ 185, 195, 220, 255 });
    int vw = MeasureText(valStr, 14);
    DrawText(valStr, panelX + panelW - 16 - vw, *cy, 14, valColor);
    *cy += 24;
}

static void draw_bfs_data_overlay(void)
{
    if (!G.showBfs)
        return;

    int panelX = 16;
    int panelY = HUD_H + 16;
    int panelW = 390;
    int panelH = 396;

    Rectangle r = { (float)panelX, (float)panelY, (float)panelW, (float)panelH };
    DrawRectangleRounded(r, 0.04f, 6, Fade((Color){ 10, 14, 26, 255 }, 0.94f));
    DrawRectangleRoundedLinesEx(r, 0.04f, 6, 1.5f, Fade(C_ACCENT, 0.75f));

    /* Header */
    int cy = panelY + 14;
    DrawText("AI BREADTH-FIRST SEARCH", panelX + 16, cy, 18, C_ACCENT);
    DrawText("CIRCULAR QUEUE & DISTANCE FIELD", panelX + 16, cy + 22, 12, (Color){ 165, 180, 210, 255 });
    DrawRectangle(panelX + 16, cy + 38, panelW - 32, 1, Fade(C_ACCENT, 0.35f));
    cy += 48;

    int walkableCount = 0;
    for (int y = 0; y < MAZE_H; y++)
        for (int x = 0; x < MAZE_W; x++)
            if (G.maze.grid[y][x] == TILE_FLOOR)
                walkableCount++;
    if (walkableCount <= 0) walkableCount = 1;

    float coverage = (float)G.field.orderCount / (float)walkableCount * 100.0f;
    if (coverage > 100.0f) coverage = 100.0f;

    draw_bfs_row(panelX, panelW, &cy, "Algorithm Model", "FIFO Circular Queue", C_GOLD);
    draw_bfs_row(panelX, panelW, &cy, "Time Complexity", "O(V + E) Optimal", (Color){ 80, 222, 132, 255 });
    draw_bfs_row(panelX, panelW, &cy, "Player Origin", TextFormat("(%d, %d)", G.player.x, G.player.y), C_ACCENT);
    draw_bfs_row(panelX, panelW, &cy, "Explored Nodes", TextFormat("%d / %d (%.0f%%)", G.field.orderCount, walkableCount, coverage), WHITE);
    draw_bfs_row(panelX, panelW, &cy, "Queue Enqueues", TextFormat("%d operations", G.field.enqueued), WHITE);
    draw_bfs_row(panelX, panelW, &cy, "Peak Queue Depth", TextFormat("%d elements", G.field.maxQueue), C_GOLD);
    draw_bfs_row(panelX, panelW, &cy, "Farthest Tile", TextFormat("%d steps", G.field.maxDist), WHITE);

    int exitDist = G.field.dist[G.exit.y][G.exit.x];
    const char *exStatus = (G.keysTaken == G.keyCount) ? "OPEN" : "LOCKED";
    draw_bfs_row(panelX, panelW, &cy, "Distance to Exit", TextFormat("%d steps [%s]", exitDist, exStatus),
                 (G.keysTaken == G.keyCount) ? C_EXIT : C_GOLD);

    /* Nearest Key */
    int minKeyDist = 999;
    for (int i = 0; i < G.keyCount; i++) {
        if (!G.keys[i].taken) {
            int kd = G.field.dist[G.keys[i].pos.y][G.keys[i].pos.x];
            if (kd >= 0 && kd < minKeyDist) minKeyDist = kd;
        }
    }
    if (minKeyDist < 999)
        draw_bfs_row(panelX, panelW, &cy, "Nearest Key", TextFormat("%d steps away", minKeyDist), C_GOLD);
    else
        draw_bfs_row(panelX, panelW, &cy, "Nearest Key", "ALL COLLECTED!", C_EXIT);

    /* Nearest Guard */
    int minGDist = 999;
    for (int i = 0; i < G.guardCount; i++) {
        int gd = G.field.dist[G.guards[i].pos.y][G.guards[i].pos.x];
        if (gd >= 0 && gd < minGDist) minGDist = gd;
    }
    if (minGDist < 999)
        draw_bfs_row(panelX, panelW, &cy, "Nearest Guard", TextFormat("%d steps away", minGDist), (minGDist <= 3) ? C_DANGER : C_ACCENT);

    /* Footer hint */
    DrawRectangle(panelX + 16, cy + 4, panelW - 32, 1, Fade(C_ACCENT, 0.35f));
    cy += 12;
    DrawText("[B] Replay Wave   |   [M / V] Close Map Mode", panelX + 18, cy, 13, C_ACCENT);
}

static void draw_play_scene(void)
{
    render_world();
    if (G.camMode != CAM_OVERVIEW)
        render_minimap(SCREEN_W - MAZE_W * 6 - 18, HUD_H + 18, 6);
    draw_bfs_data_overlay();
    draw_log();
    draw_hud();

    /* Interactive Locker Prompt */
    if (G.inLocker) {
        text_center("[SPACE] EXIT LOCKER  (HIDDEN FROM GUARDS)", SCREEN_W / 2, SCREEN_H - 72, 22, C_GOLD);
    } else {
        Cell lw; int ld;
        if (render_near_locker(&lw, &ld)) {
            text_center("[SPACE] HIDE IN LOCKER", SCREEN_W / 2, SCREEN_H - 72, 22, (Color){ 40, 230, 130, 255 });
        }
    }
}

static void draw_panel(int w, int h)
{
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.55f));
    Rectangle r = { (SCREEN_W - w) / 2.0f, (SCREEN_H - h) / 2.0f, (float)w, (float)h };
    DrawRectangleRounded(r, 0.08f, 8, C_HUD);
    DrawRectangleRoundedLinesEx(r, 0.08f, 8, 2.0f, Fade(C_ACCENT, 0.5f));
}

/* ------------------------------------------------------------------ */
/* Screens                                                             */
/* ------------------------------------------------------------------ */
static void draw_menu(void)
{
    render_menu_background();
    DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, Fade(C_BG, 0.35f), Fade(C_BG, 0.9f));

    int cx = SCREEN_W / 2;
    text_center("GUARD ESCAPE", cx, 96, 80, C_ACCENT);
    text_center("Sneak out. Grab the keys. Stun the guards. Don't get caught.", cx, 186, 22, C_TEXT);

    /* Difficulty chips */
    text_center("DIFFICULTY   (LEFT / RIGHT  or  1 2 3)", cx, 236, 16, C_MUTED);
    for (int i = 0; i < DIFF_COUNT; i++) {
        Rectangle r = { cx - 240.0f + i * 164, 258, 152, 40 };
        int sel = i == (int)G.difficulty;
        DrawRectangleRounded(r, 0.3f, 6, sel ? Fade(DIFFS[i].color, 0.92f) : Fade(C_HUD, 0.92f));
        DrawRectangleRoundedLinesEx(r, 0.3f, 6, 1.5f, Fade(DIFFS[i].color, sel ? 1.0f : 0.45f));
        text_center(DIFFS[i].name, (int)(r.x + r.width / 2), (int)r.y + 11, 20, sel ? C_BG : DIFFS[i].color);
    }
    text_center(DIFF.blurb, cx, 306, 16, C_TEXT);

    /* Map selector */
    text_center("MAP   (UP / DOWN)", cx, 340, 16, C_MUTED);
    Rectangle mr = { cx - 240.0f, 362, 480, 40 };
    DrawRectangleRounded(mr, 0.3f, 6, Fade(C_HUD, 0.92f));
    DrawRectangleRoundedLinesEx(mr, 0.3f, 6, 1.5f, Fade(C_ACCENT, 0.6f));
    DrawText("<", (int)mr.x + 16, (int)mr.y + 9, 22, C_ACCENT);
    DrawText(">", (int)(mr.x + mr.width) - 28, (int)mr.y + 9, 22, C_ACCENT);
    text_center(TextFormat("%s   (%d/%d)", MAP_NAMES[G.mapChoice], G.mapChoice + 1, MAP_CHOICES), cx, (int)mr.y + 10,
                20, C_TEXT);
    text_center(MAP_BLURBS[G.mapChoice], cx, 410, 16, C_TEXT);

    if (G.hasActiveGame) {
        const char *items[] = {
            TextFormat("[ENTER / C]  Continue Run (Level %d | Score %d)", G.level, G.score),
            "[N]  New Game",
            "[L]  Leaderboard",
            "[H]  How it works",
            "[F11]  Fullscreen",
            "[ESC]  Quit"
        };
        for (int i = 0; i < 6; i++) {
            Rectangle r = { cx - 220.0f, 442.0f + i * 42, 440, 36 };
            DrawRectangleRounded(r, 0.3f, 6, i == 0 ? Fade(C_ACCENT, 0.92f) : (i == 1 ? Fade((Color){ 32, 44, 72, 255 }, 0.92f) : Fade(C_HUD, 0.92f)));
            DrawRectangleRoundedLinesEx(r, 0.3f, 6, 1.5f, i == 0 ? C_ACCENT : (i == 1 ? Fade(C_GOLD, 0.65f) : Fade(C_ACCENT, 0.35f)));
            text_center(items[i], cx, (int)r.y + 8, 19, i == 0 ? C_BG : (i == 1 ? C_GOLD : C_TEXT));
        }
    } else {
        const char *items[] = { "[ENTER]  Play", "[L]  Leaderboard", "[H]  How it works", "[F11]  Maximize / Fullscreen", "[ESC]  Quit" };
        for (int i = 0; i < 5; i++) {
            Rectangle r = { cx - 180.0f, 446.0f + i * 48, 360, 40 };
            DrawRectangleRounded(r, 0.3f, 6, i == 0 ? Fade(C_ACCENT, 0.92f) : Fade(C_HUD, 0.92f));
            if (i != 0)
                DrawRectangleRoundedLinesEx(r, 0.3f, 6, 1.5f, Fade(C_ACCENT, 0.35f));
            text_center(items[i], cx, (int)r.y + 10, 20, i == 0 ? C_BG : C_TEXT);
        }
    }

    if (renderAssetsMissing) {
        const char *w1 = "Assets folder not found - characters and textures are missing.";
        const char *w2 = "Run GuardEscape.exe from inside the guard-escape folder (next to 'assets').";
        DrawRectangle(cx - 420, 14, 840, 62, Fade(C_DANGER, 0.85f));
        text_center(w1, cx, 22, 20, WHITE);
        text_center(w2, cx, 48, 18, WHITE);
    }
    text_center("Arrays  |  Stack  |  Circular Queue  |  Binary Min-Heap (A*)  |  BFS  |  BST", cx, SCREEN_H - 46, 16, C_MUTED);
}

static void draw_help_key_entry(int x, int *y, const char *key, const char *title, const char *desc, Color keyCol)
{
    int curY = *y;
    int kw = MeasureText(key, 17) + 20;
    Rectangle kr = { (float)x, (float)curY, (float)kw, 28 };
    DrawRectangleRounded(kr, 0.35f, 4, Fade(keyCol, 0.22f));
    DrawRectangleRoundedLinesEx(kr, 0.35f, 4, 1.4f, keyCol);
    DrawText(key, x + 10, curY + 6, 17, keyCol);

    DrawText(title, x + kw + 12, curY + 4, 20, WHITE);
    DrawText(desc, x + 6, curY + 38, 17, (Color){ 220, 230, 245, 255 });
    *y += 70;
}

static void draw_help_dsa_entry(int x, int *y, const char *tag, const char *name, const char *line1, const char *line2, Color tagCol)
{
    int curY = *y;
    int tw = MeasureText(tag, 15) + 18;
    Rectangle tr = { (float)x, (float)curY, (float)tw, 26 };
    DrawRectangleRounded(tr, 0.35f, 4, Fade(tagCol, 0.22f));
    DrawRectangleRoundedLinesEx(tr, 0.35f, 4, 1.4f, tagCol);
    DrawText(tag, x + 9, curY + 5, 15, tagCol);

    DrawText(name, x + tw + 12, curY + 3, 20, WHITE);
    DrawText(line1, x + 6, curY + 36, 17, (Color){ 230, 240, 255, 255 });
    DrawText(line2, x + 6, curY + 60, 16, (Color){ 180, 205, 235, 255 });
    *y += 94;
}

static void draw_help(void)
{
    render_menu_background();
    DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, Fade(C_BG, 0.88f), Fade(C_BG, 0.96f));

    /* Top Title Header */
    text_center("HOW TO PLAY & SYSTEM ARCHITECTURE", SCREEN_W / 2, 14, 36, C_ACCENT);
    text_center("Master stealth infiltration, tactical combat tools, and core Data Structures & Algorithms (PBL / Viva)",
                SCREEN_W / 2, 52, 18, (Color){ 205, 220, 245, 255 });
    DrawRectangle(32, 74, SCREEN_W - 64, 1, Fade(C_ACCENT, 0.4f));

    int marginX = 32;
    int cardY = 82;
    int cardH = SCREEN_H - cardY - 44;
    int gap = 20;
    int cardW = (SCREEN_W - marginX * 2 - gap) / 2;

    int leftX = marginX;
    int rightX = marginX + cardW + gap;

    /* LEFT CARD: Tactical Controls & Gameplay Mechanics */
    Rectangle leftR = { (float)leftX, (float)cardY, (float)cardW, (float)cardH };
    DrawRectangleRounded(leftR, 0.03f, 6, Fade((Color){ 14, 18, 30, 255 }, 0.94f));
    DrawRectangleRoundedLinesEx(leftR, 0.03f, 6, 1.5f, Fade(C_ACCENT, 0.65f));

    int ly = cardY + 14;
    DrawText("OPERATIVE CONTROLS & COMBAT WEAPONS", leftX + 16, ly, 22, C_ACCENT);
    DrawRectangle(leftX + 16, ly + 28, cardW - 32, 1, Fade(C_ACCENT, 0.35f));
    ly += 38;

    draw_help_key_entry(leftX + 16, &ly, "WASD / ARROWS", "Corridor Movement & Turning",
                        "Responsive 3D movement along corridors and corners.", WHITE);

    draw_help_key_entry(leftX + 16, &ly, "LEFT SHIFT", "Sprint Boost (1.45x Speed)",
                        "Burst sprint via stamina meter; alerts nearby guards!", C_GOLD);

    draw_help_key_entry(leftX + 16, &ly, "SPACE", "Hide in Locker / Exit Locker",
                        "100% stealth sanctuary. Guards cannot detect you inside.", (Color){ 60, 235, 140, 255 });

    draw_help_key_entry(leftX + 16, &ly, "Z", "Fire Stun Dart (Combat Weapon)",
                        "Fires a stun dart straight ahead. Freezes guard for 5s.", C_STUN);

    draw_help_key_entry(leftX + 16, &ly, "E", "Flash Grenade (Area of Effect)",
                        "Stuns all guards within 4 steps, even around corners!", C_FLASH);

    draw_help_key_entry(leftX + 16, &ly, "Q", "Toss Distraction Coin",
                        "Tosses coin down hallway; clinking sound lures guards.", C_GOLD);

    draw_help_key_entry(leftX + 16, &ly, "M", "AI BFS Queue Map & Diagnostics",
                        "Opens live tactical BFS distance field and queue stats.", C_ACCENT);

    draw_help_key_entry(leftX + 16, &ly, "X / C", "A* vs BFS Toggle  |  Camera Views",
                        "[X] toggles A* / BFS pathfinding. [C] cycles 3D cameras.", (Color){ 200, 215, 255, 255 });

    /* RIGHT CARD: Data Structures & Algorithms (PBL / Viva Guide) */
    Rectangle rightR = { (float)rightX, (float)cardY, (float)cardW, (float)cardH };
    DrawRectangleRounded(rightR, 0.03f, 6, Fade((Color){ 14, 18, 30, 255 }, 0.94f));
    DrawRectangleRoundedLinesEx(rightR, 0.03f, 6, 1.5f, Fade(C_GOLD, 0.65f));

    int ry = cardY + 14;
    DrawText("DATA STRUCTURES & ALGORITHMS (PBL / VIVA GUIDE)", rightX + 16, ry, 22, C_GOLD);
    DrawRectangle(rightX + 16, ry + 28, cardW - 32, 1, Fade(C_GOLD, 0.35f));
    ry += 38;

    draw_help_dsa_entry(rightX + 16, &ry, "STACK & 2D MATRIX", "Maze Generation (DFS Backtracking)",
                        "grid[21][31] stores maze; recursive DFS carves paths.",
                        "Call stack backtracking ensures solvable corridors.", C_ACCENT);

    draw_help_dsa_entry(rightX + 16, &ry, "CIRCULAR QUEUE", "Breadth-First Search (BFS Graph)",
                        "Array-backed FIFO ring buffer maps player distance.",
                        "Guarantees optimal O(V+E) shortest paths; FIFO log.", C_GOLD);

    draw_help_dsa_entry(rightX + 16, &ry, "BINARY MIN-HEAP", "A* Pathfinding Priority Queue",
                        "Min-heap ordered by f(n)=g+h Manhattan heuristic.",
                        "Guards extract lowest-cost cell in O(log N) time.", (Color){ 80, 222, 132, 255 });

    draw_help_dsa_entry(rightX + 16, &ry, "BINARY SEARCH TREE", "Leaderboard Hall of Fame",
                        "Dynamic BST stores scores; reverse in-order ranks.",
                        "Pre-order serialization saves to disk and restores.", C_ACCENT);

    draw_help_dsa_entry(rightX + 16, &ry, "PROCEDURAL AUDIO", "Real-Time Waveform Synthesis",
                        "Synthesizes raw audio waveforms directly in memory.",
                        "Generates footsteps, locker creaks, coins, and zaps.", C_GOLD);

    draw_help_dsa_entry(rightX + 16, &ry, "GAME OBJECTIVE", "Find All Keys -> Escape via Portal",
                        "Collect all golden keys scattered across corridors.",
                        "Reach the violet exit portal before guards catch you!", C_EXIT);

    /* Bottom Action Prompt */
    text_center("[ENTER] / [ESC] / [H]   Return to Game", SCREEN_W / 2, SCREEN_H - 22, 20, C_ACCENT);
}

typedef struct {
    const ScoreNode *node;
    int depth;
    int inOrderIdx;
    float x, y;
} LayoutNode;

static void collect_tree_inorder(const ScoreNode *n, int depth, LayoutNode *arr, int *count, int maxN)
{
    if (!n || *count >= maxN) return;
    if (n->left)
        collect_tree_inorder(n->left, depth + 1, arr, count, maxN);
    if (*count < maxN) {
        int idx = *count;
        arr[idx].node = n;
        arr[idx].depth = depth;
        arr[idx].inOrderIdx = idx;
        (*count)++;
    }
    if (n->right)
        collect_tree_inorder(n->right, depth + 1, arr, count, maxN);
}

static void draw_tree_visualizer(float cardX, float cardY, float cardW, float cardH, float t)
{
    LayoutNode arr[32];
    int count = 0;
    collect_tree_inorder(G.board, 0, arr, &count, 32);

    if (count == 0) {
        text_center("BST is empty - play a round to register the first score node!",
                    (int)(cardX + cardW / 2.0f), (int)(cardY + cardH / 2.0f - 10), 16, C_MUTED);
        return;
    }

    int maxDepth = 0;
    for (int i = 0; i < count; i++) {
        if (arr[i].depth > maxDepth)
            maxDepth = arr[i].depth;
    }

    float treeTop = cardY + 80.0f;
    float treeBottom = cardY + cardH - 105.0f;
    float availH = treeBottom - treeTop;
    float yStep = (maxDepth > 0) ? (availH / (maxDepth > 5 ? 5.0f : (float)maxDepth)) : 0.0f;
    if (yStep > 75.0f) yStep = 75.0f;
    if (yStep < 48.0f) yStep = 48.0f;

    float xStep = (cardW - 130.0f) / (count > 1 ? (float)(count - 1) : 1.0f);
    if (count == 1) {
        arr[0].x = cardX + cardW / 2.0f;
        arr[0].y = treeTop + 45.0f;
    } else {
        for (int i = 0; i < count; i++) {
            arr[i].x = cardX + 65.0f + i * xStep;
            arr[i].y = treeTop + 25.0f + arr[i].depth * yStep;
        }
    }

    /* Draw parent-child connection lines */
    for (int i = 0; i < count; i++) {
        const ScoreNode *n = arr[i].node;
        if (n->left) {
            for (int j = 0; j < count; j++) {
                if (arr[j].node == n->left) {
                    DrawLineEx((Vector2){ arr[i].x, arr[i].y }, (Vector2){ arr[j].x, arr[j].y }, 2.5f, Fade(C_ACCENT, 0.45f));
                    float mx = (arr[i].x + arr[j].x) * 0.5f;
                    float my = (arr[i].y + arr[j].y) * 0.5f;
                    DrawText("< L", (int)mx - 10, (int)my - 6, 10, Fade(C_ACCENT, 0.65f));
                    break;
                }
            }
        }
        if (n->right) {
            for (int j = 0; j < count; j++) {
                if (arr[j].node == n->right) {
                    DrawLineEx((Vector2){ arr[i].x, arr[i].y }, (Vector2){ arr[j].x, arr[j].y }, 2.5f, Fade(C_ACCENT, 0.45f));
                    float mx = (arr[i].x + arr[j].x) * 0.5f;
                    float my = (arr[i].y + arr[j].y) * 0.5f;
                    DrawText("R >", (int)mx - 8, (int)my - 6, 10, Fade(C_ACCENT, 0.65f));
                    break;
                }
            }
        }
    }

    /* Draw nodes */
    for (int i = 0; i < count; i++) {
        int isMine = (arr[i].node == G.lastNode);
        float nx = arr[i].x, ny = arr[i].y;
        float r = 22.0f;

        if (isMine) {
            float pulse = 0.5f + 0.5f * sinf(t * 5.0f);
            DrawCircleLines((int)nx, (int)ny, r + 4.0f + pulse * 3.0f, Fade(C_GOLD, 0.7f + 0.3f * pulse));
            DrawCircle((int)nx, (int)ny, r, C_GOLD);
            DrawCircleLines((int)nx, (int)ny, r, WHITE);
            const char *sc = TextFormat("%d", arr[i].node->score);
            DrawText(sc, (int)nx - MeasureText(sc, 13) / 2, (int)ny - 6, 13, C_BG);
        } else {
            DrawCircleLines((int)nx, (int)ny, r + 1.0f, Fade(C_ACCENT, 0.80f));
            DrawCircle((int)nx, (int)ny, r, (Color){ 22, 28, 46, 255 });
            const char *sc = TextFormat("%d", arr[i].node->score);
            DrawText(sc, (int)nx - MeasureText(sc, 13) / 2, (int)ny - 6, 13, C_TEXT);
        }

        /* Operative name badge pill */
        char nm[12];
        strncpy(nm, arr[i].node->name, 10);
        nm[10] = '\0';
        int nmW = MeasureText(nm, 11);
        Rectangle pill = { nx - nmW / 2.0f - 6.0f, ny + r + 3.0f, (float)nmW + 12.0f, 16.0f };
        DrawRectangleRounded(pill, 0.4f, 4, Fade(C_BG, 0.90f));
        DrawRectangleRoundedLinesEx(pill, 0.4f, 4, 1.0f, isMine ? C_GOLD : Fade(C_ACCENT, 0.35f));
        DrawText(nm, (int)nx - nmW / 2, (int)ny + (int)r + 6, 11, isMine ? C_GOLD : C_MUTED);
    }
}

static void draw_leaderboard(void)
{
    float t = (float)GetTime();
    render_menu_background();
    DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, Fade(C_BG, 0.76f), Fade(C_BG, 0.94f));

    /* Top Navigation Header */
    DrawText("LEADERBOARD", 45, 26, 36, C_ACCENT);
    DrawRectangleRounded((Rectangle){ 320, 31, 230, 26 }, 0.3f, 4, Fade((Color){ 24, 32, 54, 255 }, 0.9f));
    DrawRectangleRoundedLinesEx((Rectangle){ 320, 31, 230, 26 }, 0.3f, 4, 1.0f, Fade(C_GOLD, 0.5f));
    DrawText("HALL OF FAME // TOP RUNS", 332, 37, 13, C_GOLD);

    const char *subTag = "DSA ARCHITECTURE: BINARY SEARCH TREE PERSISTENCE";
    DrawText(subTag, SCREEN_W - 45 - MeasureText(subTag, 14), 37, 14, C_MUTED);
    DrawRectangle(45, 70, SCREEN_W - 90, 2, Fade(C_ACCENT, 0.35f));

    int cardL_x = 45;
    int cardL_y = 85;
    int cardL_w = 480;
    int cardL_h = SCREEN_H - 150;

    int cardR_x = 545;
    int cardR_y = 85;
    int cardR_w = SCREEN_W - cardR_x - 45;
    int cardR_h = SCREEN_H - 150;

    /* Left Card: Rankings */
    DrawRectangleRounded((Rectangle){ (float)cardL_x, (float)cardL_y, (float)cardL_w, (float)cardL_h }, 0.03f, 8, Fade((Color){ 12, 16, 26, 255 }, 0.88f));
    DrawRectangleRoundedLinesEx((Rectangle){ (float)cardL_x, (float)cardL_y, (float)cardL_w, (float)cardL_h }, 0.03f, 8, 1.5f, Fade(C_ACCENT, 0.45f));

    DrawText("GLOBAL RANKINGS", cardL_x + 18, cardL_y + 16, 20, C_ACCENT);
    DrawText("Sorted by score via Reverse In-Order Traversal", cardL_x + 18, cardL_y + 40, 13, C_MUTED);

    /* Table Column Header Pill */
    float hdrY = cardL_y + 62;
    DrawRectangleRounded((Rectangle){ cardL_x + 12, hdrY, cardL_w - 24, 26 }, 0.25f, 4, Fade((Color){ 24, 32, 52, 255 }, 0.90f));
    DrawText("RANK", cardL_x + 20, (int)hdrY + 6, 13, C_MUTED);
    DrawText("OPERATIVE", cardL_x + 75, (int)hdrY + 6, 13, C_MUTED);
    DrawText("SCORE", cardL_x + 235, (int)hdrY + 6, 13, C_MUTED);
    DrawText("LVL", cardL_x + 315, (int)hdrY + 6, 13, C_MUTED);
    DrawText("MODE", cardL_x + 375, (int)hdrY + 6, 13, C_MUTED);

    ScoreEntry top[TOP_N];
    int n = bst_top(G.board, top, TOP_N);

    if (n == 0) {
        DrawText("No operatives logged yet - play to rank!", cardL_x + 24, cardL_y + 110, 16, C_TEXT);
    }

    float rowY0 = cardL_y + 96;
    for (int i = 0; i < n; i++) {
        float ry = rowY0 + i * 38;
        if (ry + 34 > cardL_y + cardL_h - 10)
            break;
        Rectangle r = { cardL_x + 12, ry, cardL_w - 24, 34 };
        int me = G.lastNode && strcmp(top[i].name, G.lastNode->name) == 0 && top[i].score == G.lastNode->score;

        Color c = (i == 0) ? C_GOLD : ((i == 1) ? (Color){ 200, 215, 235, 255 } : ((i == 2) ? (Color){ 215, 140, 80, 255 } : C_TEXT));

        if (i == 0) {
            DrawRectangleRounded(r, 0.2f, 4, Fade(C_GOLD, 0.16f));
            DrawRectangle(r.x, r.y, 4, r.height, C_GOLD);
        } else if (i % 2 == 1) {
            DrawRectangleRounded(r, 0.2f, 4, Fade((Color){ 22, 28, 44, 255 }, 0.45f));
        }

        DrawText(TextFormat("#%d", i + 1), (int)r.x + 8, (int)r.y + 8, 16, c);
        DrawText(top[i].name, (int)r.x + 63, (int)r.y + 8, 16, c);
        DrawText(TextFormat("%d", top[i].score), (int)r.x + 223, (int)r.y + 8, 16, c);
        DrawText(TextFormat("L%d", top[i].level), (int)r.x + 303, (int)r.y + 8, 16, C_MUTED);

        int dIdx = top[i].diff;
        if (dIdx < 0 || dIdx >= DIFF_COUNT) dIdx = DIFF_MEDIUM;
        DrawRectangleRounded((Rectangle){ r.x + 355, r.y + 6, 60, 22 }, 0.3f, 4, Fade(DIFFS[dIdx].color, 0.20f));
        DrawText(DIFFS[dIdx].name, (int)r.x + 365, (int)r.y + 9, 13, DIFFS[dIdx].color);

        if (me) {
            float p = 0.5f + 0.5f * sinf(t * 5.0f);
            DrawRectangleRoundedLinesEx(r, 0.2f, 4, 1.8f, Fade(C_GOLD, 0.6f + 0.4f * p));
        }
    }

    /* Right Card: BST Structure Visualizer */
    DrawRectangleRounded((Rectangle){ (float)cardR_x, (float)cardR_y, (float)cardR_w, (float)cardR_h }, 0.03f, 8, Fade((Color){ 12, 16, 26, 255 }, 0.88f));
    DrawRectangleRoundedLinesEx((Rectangle){ (float)cardR_x, (float)cardR_y, (float)cardR_w, (float)cardR_h }, 0.03f, 8, 1.5f, Fade(C_ACCENT, 0.45f));

    DrawText("LIVE BINARY SEARCH TREE TOPOLOGY", cardR_x + 18, cardR_y + 16, 20, C_ACCENT);
    DrawText(TextFormat("Total Nodes: %d  |  Tree Height: %d  |  Left <= Root < Right",
                        bst_count(G.board), bst_height(G.board)),
             cardR_x + 18, cardR_y + 40, 13, C_MUTED);

    draw_tree_visualizer((float)cardR_x, (float)cardR_y, (float)cardR_w, (float)cardR_h, t);

    /* Footer info */
    DrawText("Pre-order serialized to leaderboard.txt  -  Reload restores identical tree invariant",
             45, SCREEN_H - 42, 15, C_MUTED);

    Rectangle btnPlay = { SCREEN_W - 355, SCREEN_H - 44, 140, 32 };
    DrawRectangleRounded(btnPlay, 0.3f, 4, Fade(C_ACCENT, 0.90f));
    DrawText("[ENTER] PLAY", (int)btnPlay.x + 16, (int)btnPlay.y + 7, 15, C_BG);

    Rectangle btnMenu = { SCREEN_W - 195, SCREEN_H - 44, 150, 32 };
    DrawRectangleRounded(btnMenu, 0.3f, 4, Fade((Color){ 20, 26, 44, 255 }, 0.85f));
    DrawRectangleRoundedLinesEx(btnMenu, 0.3f, 4, 1.2f, Fade(C_ACCENT, 0.40f));
    DrawText("[ESC] MAIN MENU", (int)btnMenu.x + 12, (int)btnMenu.y + 7, 15, C_TEXT);
}

/* ------------------------------------------------------------------ */
/* Main loop                                                           */
/* ------------------------------------------------------------------ */
int main(void)
{
    srand((unsigned)time(NULL));
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(DEFAULT_W, DEFAULT_H, "Guard Escape 3D - DSA in C");
    SetWindowMinSize(960, 540);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    /* Loading character models takes a moment */
    BeginDrawing();
    ClearBackground(C_BG);
    text_center("GUARD ESCAPE", SCREEN_W / 2, SCREEN_H / 2 - 70, 64, C_ACCENT);
    text_center("Loading characters and textures...", SCREEN_W / 2, SCREEN_H / 2 + 20, 22, C_MUTED);
    EndDrawing();
    render_init();
    audio_init();
    G.board = bst_load(SAVE_FILE);
    G.difficulty = DIFF_MEDIUM;
    G.mapChoice = MAP_RANDOM;
    G.fog = 1;
    G.camMode = CAM_THIRD_PERSON;
    menu_init();

    int quit = 0;
    while (!WindowShouldClose() && !quit) {
        float dt = GetFrameTime();
        if (dt > 0.05f)
            dt = 0.05f;

        if (IsKeyPressed(KEY_F11) || ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER))) {
            ToggleFullscreen();
        }

        audio_update(dt);

        switch (G.state) {
        case ST_MENU:
            G.menuTime += dt;
            int left = IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A);
            int right = IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D);
            if (left || right) {
                audio_play_menu_click();
                G.difficulty = (G.difficulty + (left ? DIFF_COUNT - 1 : 1)) % DIFF_COUNT;
            }
            if (IsKeyPressed(KEY_ONE)) { audio_play_menu_click(); G.difficulty = DIFF_EASY; }
            if (IsKeyPressed(KEY_TWO)) { audio_play_menu_click(); G.difficulty = DIFF_MEDIUM; }
            if (IsKeyPressed(KEY_THREE)) { audio_play_menu_click(); G.difficulty = DIFF_HARD; }

            int up = IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W);
            int down = IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
            if (up || down) {
                audio_play_menu_click();
                G.mapChoice = (G.mapChoice + (up ? MAP_CHOICES - 1 : 1)) % MAP_CHOICES;
                if (!G.hasActiveGame)
                    menu_new_maze();
            }
            if (G.hasActiveGame) {
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_C) || IsKeyPressed(KEY_SPACE)) {
                    audio_play_menu_click();
                    G.paused = 0;
                    G.camSnap = 1;
                    if (G.keysTaken == G.keyCount && cell_eq(G.player, G.exit)) {
                        G.level++;
                        setup_level();
                    } else {
                        render_build_level();
                    }
                    G.state = ST_PLAY;
                    log_push(&G.log, "Resumed active run.");
                } else if (IsKeyPressed(KEY_N)) {
                    audio_play_menu_click();
                    new_game();
                } else if (IsKeyPressed(KEY_L)) { audio_play_menu_click(); G.state = ST_LEADERBOARD; }
                else if (IsKeyPressed(KEY_H)) { audio_play_menu_click(); G.state = ST_HELP; }
                else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_Q)) quit = 1;
            } else {
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_N)) {
                    audio_play_menu_click();
                    new_game();
                } else if (IsKeyPressed(KEY_L)) { audio_play_menu_click(); G.state = ST_LEADERBOARD; }
                else if (IsKeyPressed(KEY_H)) { audio_play_menu_click(); G.state = ST_HELP; }
                else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_Q)) quit = 1;
            }
            break;

        case ST_HELP:
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_H)) {
                audio_play_menu_click();
                if (G.hasActiveGame) {
                    G.paused = 0;
                    G.camSnap = 1;
                    G.state = ST_PLAY;
                } else {
                    menu_init();
                }
            }
            break;

        case ST_PLAY:
            if (IsKeyPressed(KEY_ESCAPE)) {
                audio_play_menu_click();
                G.paused = 1;
                G.state = ST_MENU;
                G.menuTime = 0.0f;
                break;
            }
            if (IsKeyPressed(KEY_P)) G.paused = !G.paused;
            if (IsKeyPressed(KEY_V) || IsKeyPressed(KEY_M)) {
                G.showBfs = !G.showBfs;
                audio_play_menu_click();
                if (G.showBfs)
                    log_push(&G.log, "AI Map: BFS queue & distance field active.");
                else
                    log_push(&G.log, "AI Map: Standard 3D view restored.");
            }
            if (IsKeyPressed(KEY_H)) {
                audio_play_menu_click();
                G.paused = 1;
                G.state = ST_HELP;
                break;
            }
            if (IsKeyPressed(KEY_F)) G.fog = !G.fog;
            if (IsKeyPressed(KEY_C)) G.camMode = (G.camMode + 1) % CAM_COUNT;
            if (IsKeyPressed(KEY_X)) {
                G.useAstar = !G.useAstar;
                audio_play_menu_click();
                if (G.useAstar)
                    log_push(&G.log, "AI Pathfinder: A* with Binary Min-Heap Priority Queue");
                else
                    log_push(&G.log, "AI Pathfinder: Classic BFS with Circular Queue");
            }
            if (IsKeyPressed(KEY_B) && !G.waveActive) {
                G.waveActive = 1;
                G.waveTime = 0.0f;
                log_push(&G.log, "BFS replay: the queue explores outward from you");
            }
            log_update(&G.log, dt);
            if (G.flashTimer > 0.0f)
                G.flashTimer -= dt;

            if (G.waveActive) {
                G.waveTime += dt;
                if (G.waveTime > WAVE_TIME + 0.9f)
                    G.waveActive = 0;
            } else if (!G.paused) {
                G.levelTime += dt;
                update_coin(dt);
                update_player(dt);
                if (G.state == ST_PLAY) update_guards(dt);
                if (G.state == ST_PLAY) update_dart(dt);
                update_draw_positions(dt);
                if (G.state == ST_PLAY) check_caught();
            }
            render_update_camera(dt);
            break;

        case ST_CAUGHT:
            log_update(&G.log, dt);
            G.stateTimer -= dt;
            render_update_camera(dt);
            if (G.stateTimer <= 0.0f) {
                if (G.lives > 0) {
                    reset_positions();
                    G.state = ST_PLAY;
                } else {
                    G.hasActiveGame = 0;
                    G.nameLen = 0;
                    G.name[0] = '\0';
                    G.state = ST_NAME_ENTRY;
                }
            }
            break;

        case ST_LEVEL_CLEAR:
            update_draw_positions(dt);
            render_update_camera(dt);
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                audio_play_menu_click();
                G.level++;
                setup_level();
                G.state = ST_PLAY;
            } else if (IsKeyPressed(KEY_ESCAPE)) {
                audio_play_menu_click();
                G.paused = 1;
                G.state = ST_MENU;
                G.menuTime = 0.0f;
            }
            break;

        case ST_NAME_ENTRY: {
            int ch = GetCharPressed();
            while (ch > 0) {
                if (ch < 128 && (isalnum(ch) || ch == '_') && G.nameLen < NAME_LEN - 1) {
                    G.name[G.nameLen++] = (char)ch;
                    G.name[G.nameLen] = '\0';
                }
                ch = GetCharPressed();
            }
            if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && G.nameLen > 0)
                G.name[--G.nameLen] = '\0';
            if (IsKeyPressed(KEY_ENTER)) {
                const char *nm = G.nameLen ? G.name : "PLAYER";
                G.board = bst_insert(G.board, nm, G.score, G.level, G.difficulty, &G.lastNode);
                G.lastRank = bst_count_greater(G.board, G.score) + 1;
                bst_save(G.board, SAVE_FILE);
                G.state = ST_LEADERBOARD;
            }
            break;
        }

        case ST_LEADERBOARD:
            G.menuTime += dt;
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                audio_play_menu_click();
                if (G.hasActiveGame) {
                    G.paused = 0;
                    G.camSnap = 1;
                    G.state = ST_PLAY;
                } else {
                    new_game();
                }
            }
            else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_L)) { audio_play_menu_click(); menu_init(); }
            break;
        }

        BeginDrawing();
        ClearBackground(C_BG);
        switch (G.state) {
        case ST_MENU:        draw_menu(); break;
        case ST_HELP:        draw_help(); break;
        case ST_LEADERBOARD: draw_leaderboard(); break;
        case ST_PLAY:
            draw_play_scene();
            if (G.waveActive)
                text_center(TextFormat("BFS REPLAY  -  %d cells visited, peak queue size %d",
                                       G.field.orderCount, G.field.maxQueue),
                            SCREEN_W / 2, HUD_H + 16, 22, WHITE);
            if (G.paused) {
                draw_panel(360, 140);
                text_center("PAUSED", SCREEN_W / 2, SCREEN_H / 2 - 34, 40, C_ACCENT);
                text_center("Press P to continue", SCREEN_W / 2, SCREEN_H / 2 + 16, 20, C_MUTED);
            }
            break;
        case ST_CAUGHT:
            draw_play_scene();
            draw_panel(420, 160);
            text_center("CAUGHT!", SCREEN_W / 2, SCREEN_H / 2 - 50, 52, C_DANGER);
            text_center(G.lives > 0 ? TextFormat("Lives left: %d", G.lives) : "No lives left...",
                        SCREEN_W / 2, SCREEN_H / 2 + 16, 22, C_TEXT);
            break;
        case ST_LEVEL_CLEAR:
            draw_play_scene();
            draw_panel(480, 230);
            text_center(TextFormat("LEVEL %d ESCAPED!", G.level), SCREEN_W / 2, SCREEN_H / 2 - 82, 40, C_GREEN);
            text_center(TextFormat("Bonus +%d", G.lastBonus), SCREEN_W / 2, SCREEN_H / 2 - 22, 26, C_GOLD);
            text_center(TextFormat("Total score %d", G.score), SCREEN_W / 2, SCREEN_H / 2 + 14, 22, C_TEXT);
            text_center(G.mapChoice == MAP_TOUR
                            ? TextFormat("ENTER - next level: %s (more guards!)", MAP_NAMES[map_for_level(G.level + 1)])
                            : "ENTER - next level (more guards!)",
                        SCREEN_W / 2, SCREEN_H / 2 + 60, 20, C_MUTED);
            break;
        case ST_NAME_ENTRY: {
            draw_panel(520, 260);
            int cy = SCREEN_H / 2;
            text_center("GAME OVER", SCREEN_W / 2, cy - 104, 44, C_DANGER);
            text_center(TextFormat("Score %d  -  reached level %d on %s", G.score, G.level, DIFF.name), SCREEN_W / 2,
                        cy - 48, 22, C_TEXT);
            text_center("Type your name and press ENTER", SCREEN_W / 2, cy - 12, 18, C_MUTED);
            Rectangle box = { SCREEN_W / 2.0f - 160, (float)cy + 20, 320, 48 };
            DrawRectangleRounded(box, 0.25f, 6, C_BG);
            DrawRectangleRoundedLinesEx(box, 0.25f, 6, 2.0f, C_ACCENT);
            const char *shown = TextFormat("%s%s", G.name, ((int)(GetTime() * 2) % 2) ? "_" : " ");
            text_center(shown, SCREEN_W / 2, cy + 33, 24, C_TEXT);
            break;
        }
        }
        EndDrawing();
    }

    bst_free(G.board);
    render_unload();
    audio_shutdown();
    CloseWindow();
    return 0;
}
