// ====== OpenGL 2D Game Skeleton (GLUT, immediate mode) ======
// Matches the style of your samples: GLUT entry points, gluOrtho2D, global state.
// Tested with classic GLUT headers like in your snippets: <glut.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include <glut.h>



// ---------------- Window & Layout ----------------
const int W = 1000;
const int H = 700;
const int TOP_H = 90;
const int BOT_H = 120;

const int GAME_Y0 = BOT_H;
const int GAME_Y1 = H - TOP_H;

// ---------------- Game Config ----------------
const int   MAX_LIVES = 5;
const float PLAYER_SPEED = 240.0f;   // px/sec
const float SPEED_BOOST = 420.0f;    // boosted speed
const float POWERUP_DURATION = 4.0f; // seconds
const float SHIELD_DURATION = 4.0f; // seconds
const int   ROUND_TIME_SEC = 60;

const float PLACE_MIN_DIST = 26.0f;  // min distance between placed items

// ---------------- Utility ----------------
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float dist2(float x1, float y1, float x2, float y2) { float dx = x1 - x2, dy = y1 - y2; return dx * dx + dy * dy; }
bool inGameArea(float x, float y) { return (x >= 0 && x <= W && y >= GAME_Y0 && y <= GAME_Y1); }

// ---------------- Print (sample 4 compatible) ----------------
void print(int x, int y, const char* s) {
    glRasterPos2f((float)x, (float)y);
    for (const char* p = s; *p; ++p) glutBitmapCharacter(GLUT_BITMAP_9_BY_15, *p);
}

// ---------------- Object Types ----------------
enum ObjType { OBJ_OBSTACLE = 0, OBJ_COLLECT = 1, OBJ_PU_SPEED = 2, OBJ_PU_SHIELD = 3 };

struct Obj {
    float x, y, r;
    ObjType type;
};

std::vector<Obj> obstacles;
std::vector<Obj> collectibles;
std::vector<Obj> powerups; // contains both types (distinguished by .type)

// ---------------- Player & Target ----------------
struct Player {
    float x = W * 0.5f, y = GAME_Y0 + 40.0f;
    float r = 14.0f;
    float angleDeg = 90.0f; // faces up initially
    int lives = MAX_LIVES;
    int score = 0;
    bool shielded = false;
    float shieldUntil = 0.0f; // absolute time (seconds)
    float speedUntil = 0.0f;
} player;

struct Target {
    // Opposite side from player start; we’ll move along cubic Bezier horizontally
    float r = 16.0f;
    // Bezier control points (int[2] like your sample 4)
    int p0[2], p1[2], p2[2], p3[2];
    float t = 0.0f;   // 0..1
    int dir = +1;     // ping-pong over [0,1]
} target;

// Safe Bezier: writes result in out[2]
void bezierPoint(float t, const int* p0, const int* p1, const int* p2, const int* p3, int out[2]) {
    float u = 1.0f - t;
    float uu = u * u;
    float tt = t * t;
    float uuu = uu * u;
    float ttt = tt * t;
    float x = uuu * p0[0] + 3 * u * u * t * p1[0] + 3 * u * tt * p2[0] + ttt * p3[0];
    float y = uuu * p0[1] + 3 * u * u * t * p1[1] + 3 * u * tt * p2[1] + ttt * p3[1];
    out[0] = (int)x; out[1] = (int)y;
}

// ---------------- Game State ----------------
float nextHitTime = 0.0f; // when we’re allowed to take damage again

enum Phase { PHASE_EDIT = 0, PHASE_PLAY = 1, PHASE_WIN = 2, PHASE_LOSE = 3 };
Phase phase = PHASE_EDIT;

enum PlaceMode { PLACE_NONE = 0, PLACE_OBS = 1, PLACE_COL = 2, PLACE_PU_SPEED = 3, PLACE_PU_SHIELD = 4 };
PlaceMode placeMode = PLACE_NONE;

float timeSec = 0.0f;     // global time since program start
float roundStart = 0.0f;  // time when play started
int   timeLeft = ROUND_TIME_SEC;

// Input
bool keyW = false, keyA = false, keyS = false, keyD = false;
bool keyUp = false, keyLeft = false, keyDown = false, keyRight = false;

// Background anim
float bgShift = 0.0f;

// ---------------- Drawing Helpers (Primitives requirements) ----------------

// Simple quad
void drawQuad(float x, float y, float w, float h) {
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

// Circle (triangle fan)
void drawCircle(float cx, float cy, float r, int seg = 32) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= seg; i++) {
        float a = (float)i / seg * 6.2831853f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
}

// Heart icon: uses the CURRENT color the caller sets.
// (Two circles + triangle; no glColor calls inside.)
void drawHeart(float cx, float cy, float s) {
    // two circles
    drawCircle(cx - 0.3f * s, cy, 0.35f * s, 20);
    drawCircle(cx + 0.3f * s, cy, 0.35f * s, 20);
    // bottom triangle
    glBegin(GL_TRIANGLES);
    glVertex2f(cx - 0.75f * s, cy);
    glVertex2f(cx + 0.75f * s, cy);
    glVertex2f(cx, cy - 0.9f * s);
    glEnd();
}


// Player (>=4 primitives): circle body, triangle nose, line “visor”, point accent
// Fancy spaceship player: polygon hull + 2 fins (triangles) + cockpit (circle)
// + outline (line loop) + animated exhaust triangle. Shield ring kept.
void drawPlayer(const Player& p) {
    glPushMatrix();
    glTranslatef(p.x, p.y, 0);
    glRotatef(p.angleDeg, 0, 0, 1);

    float L = p.r * 2.2f;   // hull length along +X
    float H = p.r * 1.2f;   // hull half-height

    // --- HULL (polygon) ---
    glBegin(GL_POLYGON);
    glColor3f(0.18f, 0.65f, 0.95f); glVertex2f(+L * 0.55f, 0);        // nose
    glColor3f(0.10f, 0.40f, 0.80f); glVertex2f(+L * 0.10f, +H * 0.95f); // top shoulder
    glVertex2f(-L * 0.25f, +H * 0.70f);
    glVertex2f(-L * 0.55f, 0);
    glVertex2f(-L * 0.25f, -H * 0.70f);
    glVertex2f(+L * 0.10f, -H * 0.95f);
    glEnd();

    // --- FINS (two triangles) ---
    glColor3f(0.85f, 0.2f, 0.2f);
    glBegin(GL_TRIANGLES);
    // top fin
    glVertex2f(-L * 0.18f, +H * 0.65f);
    glVertex2f(-L * 0.60f, +H * 1.15f);
    glVertex2f(-L * 0.35f, +H * 0.40f);
    // bottom fin
    glVertex2f(-L * 0.18f, -H * 0.65f);
    glVertex2f(-L * 0.60f, -H * 1.15f);
    glVertex2f(-L * 0.35f, -H * 0.40f);
    glEnd();

    // --- COCKPIT (circle) ---
    glColor3f(1, 1, 1);
    drawCircle(+L * 0.18f, 0, p.r * 0.45f, 24);

    // --- OUTLINE (line loop) ---
    glColor3f(0.05f, 0.08f, 0.15f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(+L * 0.55f, 0);
    glVertex2f(+L * 0.10f, +H * 0.95f);
    glVertex2f(-L * 0.25f, +H * 0.70f);
    glVertex2f(-L * 0.55f, 0);
    glVertex2f(-L * 0.25f, -H * 0.70f);
    glVertex2f(+L * 0.10f, -H * 0.95f);
    glEnd();

    // --- EXHAUST FLAME (animated triangle) ---
    float flame = 6.0f + 4.0f * (0.5f + 0.5f * sinf(timeSec * 18.0f));
    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.75f, 0.2f); glVertex2f(-L * 0.55f, 4.0f);
    glColor3f(1.0f, 0.50f, 0.0f); glVertex2f(-L * 0.55f, -4.0f);
    glColor3f(1.0f, 0.25f, 0.0f); glVertex2f(-L * 0.55f - flame, 0.0f);
    glEnd();

    glPopMatrix();

    // Shield ring (unchanged)
    if (p.shielded) {
        glColor3f(0.8f, 0.8f, 1.0f);
        glLineWidth(2);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 40; i++) {
            float a = (float)i / 40 * 6.2831853f;
            glVertex2f(p.x + cosf(a) * (p.r + 7), p.y + sinf(a) * (p.r + 7));
        }
        glEnd();
        glLineWidth(1);
    }
}


// Obstacle (>=2 primitives): a filled quad + an X line over it
void drawObstacle(const Obj& o) {
    glColor3f(0.6f, 0.2f, 0.2f);
    drawQuad(o.x - o.r, o.y - o.r, 2 * o.r, 2 * o.r);
    glColor3f(0.1f, 0.0f, 0.0f);
    glBegin(GL_LINES);
    glVertex2f(o.x - o.r, o.y - o.r); glVertex2f(o.x + o.r, o.y + o.r);
    glVertex2f(o.x + o.r, o.y - o.r); glVertex2f(o.x - o.r, o.y + o.r);
    glEnd();
}

// Collectible (>=3 primitives): triangle + line + point; bobbing handled outside
void drawCollectible(const Obj& c) {
    glColor3f(1, 0.84f, 0);
    glBegin(GL_TRIANGLES);
    glVertex2f(c.x, c.y + c.r);
    glVertex2f(c.x - c.r * 0.8f, c.y - c.r * 0.6f);
    glVertex2f(c.x + c.r * 0.8f, c.y - c.r * 0.6f);
    glEnd();
    glColor3f(0.2f, 0.2f, 0);
    glBegin(GL_LINES);
    glVertex2f(c.x, c.y + c.r * 0.2f);
    glVertex2f(c.x, c.y - c.r * 0.8f);
    glEnd();
    glPointSize(3);
    glBegin(GL_POINTS);
    glVertex2f(c.x, c.y);
    glEnd();
}

// Powerup A (speed): diamond + outline (>=2 primitives)
void drawPowerupSpeed(const Obj& p) {
    glColor3f(0.2f, 1.0f, 0.4f);
    glBegin(GL_POLYGON);
    glVertex2f(p.x, p.y + p.r);
    glVertex2f(p.x + p.r, p.y);
    glVertex2f(p.x, p.y - p.r);
    glVertex2f(p.x - p.r, p.y);
    glEnd();
    glColor3f(0, 0.3f, 0.1f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(p.x, p.y + p.r);
    glVertex2f(p.x + p.r, p.y);
    glVertex2f(p.x, p.y - p.r);
    glVertex2f(p.x - p.r, p.y);
    glEnd();
}

// Powerup B (shield): star (two triangles) + circle outline (>=2 primitives)
void drawPowerupShield(const Obj& p) {
    glColor3f(0.7f, 0.7f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(p.x, p.y + p.r);
    glVertex2f(p.x + p.r * 0.9f, p.y - p.r * 0.2f);
    glVertex2f(p.x - p.r * 0.9f, p.y - p.r * 0.2f);
    glEnd();
    glBegin(GL_TRIANGLES);
    glVertex2f(p.x, p.y - p.r);
    glVertex2f(p.x + p.r * 0.9f, p.y + p.r * 0.2f);
    glVertex2f(p.x - p.r * 0.9f, p.y + p.r * 0.2f);
    glEnd();
    glColor3f(0.2f, 0.2f, 0.6f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 24; i++) {
        float a = (float)i / 24 * 6.2831853f;
        glVertex2f(p.x + cosf(a) * (p.r + 3), p.y + sinf(a) * (p.r + 3));
    }
    glEnd();
}

void drawPowerup(const Obj& p) {
    if (p.type == OBJ_PU_SPEED)  drawPowerupSpeed(p);
    else                      drawPowerupShield(p);
}

// Target: circle + crosshair
void drawTarget(const Target& t) {
    glColor3f(1, 0.3f, 0.3f);
    drawCircle((float)t.p0[0], (float)t.p0[1], t.r, 28); // we’ll pass current pos in p0 during display
    glColor3f(0.4f, 0, 0);
    glBegin(GL_LINES);
    glVertex2f(t.p0[0] - t.r, t.p0[1]); glVertex2f(t.p0[0] + t.r, t.p0[1]);
    glVertex2f(t.p0[0], t.p0[1] - t.r); glVertex2f(t.p0[0], t.p0[1] + t.r);
    glEnd();
}

// ---------------- Placement & Overlap ----------------
bool overlapsAny(float x, float y, float r) {
    float r2 = (r + PLACE_MIN_DIST) * (r + PLACE_MIN_DIST);
    for (auto& o : obstacles)   if (dist2(x, y, o.x, o.y) < r2) return true;
    for (auto& c : collectibles)if (dist2(x, y, c.x, c.y) < r2) return true;
    for (auto& p : powerups)    if (dist2(x, y, p.x, p.y) < r2) return true;
    // also avoid placing on player or target current pos
    if (dist2(x, y, player.x, player.y) < r2) return true;
    int curT[2]; bezierPoint(target.t, target.p0, target.p1, target.p2, target.p3, curT);
    if (dist2(x, y, (float)curT[0], (float)curT[1]) < r2) return true;
    return false;
}

// ---------------- Panels ----------------
void drawPanels() {
    // moving background stripes (animation requirement)
    bgShift += 0.2f;
    if (bgShift > 20) bgShift -= 20;

    // Game area subtle moving stripes
    glColor3f(0.95f, 0.98f, 1.0f);
    drawQuad(0, GAME_Y0, W, GAME_Y1 - GAME_Y0);
    glColor3f(0.9f, 0.95f, 1.0f);
    for (int i = -5; i < W / 40 + 5; i++) {
        float x = i * 40.0f + fmodf(bgShift, 40.0f);
        drawQuad(x, GAME_Y0, 8, GAME_Y1 - GAME_Y0);
    }

    // Top panel
    glColor3f(0.15f, 0.15f, 0.2f);
    drawQuad(0, H - TOP_H, W, TOP_H);
    // Bottom panel
    glColor3f(0.15f, 0.15f, 0.2f);
    drawQuad(0, 0, W, BOT_H);

    // HUD: Hearts
    for (int i = 0; i < MAX_LIVES; i++) {
        float cx = 20.0f + i * 30.0f;
        float cy = H - 45.0f;
        if (i < player.lives) glColor3f(1, 0, 0);              // full
        else                 glColor3f(0.35f, 0.15f, 0.15f);   // “empty”/dim
        drawHeart(cx, cy, 12.0f);
    }


    // HUD: Score & Time
    char buf[128];
    glColor3f(1, 1, 1);
    sprintf(buf, "Score: %d", player.score);
    print(W / 2 - 40, H - 30, buf);
    sprintf(buf, "Time: %d", timeLeft);
    print(W - 130, H - 30, buf);

    // Palette icons (bottom): obstacle, collectible, PU speed, PU shield
    // Obstacle icon
    Obj tmp; tmp.x = 80; tmp.y = BOT_H * 0.5f; tmp.r = 18; tmp.type = OBJ_OBSTACLE;
    drawObstacle(tmp);
    print(55, 18, "Obstacle");

    // Collectible icon
    Obj tc; tc.x = 240; tc.y = BOT_H * 0.5f; tc.r = 16; tc.type = OBJ_COLLECT;
    drawCollectible(tc);
    print(210, 18, "Collectible");

    // Speed PU
    Obj ts; ts.x = 400; ts.y = BOT_H * 0.5f; ts.r = 16; ts.type = OBJ_PU_SPEED;
    drawPowerupSpeed(ts);
    print(365, 18, "Speed PU");

    // Shield PU
    Obj th; th.x = 560; th.y = BOT_H * 0.5f; th.r = 16; th.type = OBJ_PU_SHIELD;
    drawPowerupShield(th);
    print(525, 18, "Shield PU");

    // Current mode hint
    const char* m = "Place: None";
    if (placeMode == PLACE_OBS) m = "Place: Obstacle";
    else if (placeMode == PLACE_COL) m = "Place: Collectible";
    else if (placeMode == PLACE_PU_SPEED) m = "Place: Speed PU";
    else if (placeMode == PLACE_PU_SHIELD) m = "Place: Shield PU";
    print(W - 220, 18, m);
    print(W - 120, 38, "Press R to start");
}

// ---------------- Collision & Movement ----------------
float currentSpeed() {
    float now = timeSec;
    if (now < player.speedUntil) return SPEED_BOOST;
    return PLAYER_SPEED;
}

bool intersectCircleCircle(float x1, float y1, float r1, float x2, float y2, float r2) {
    float rr = (r1 + r2) * (r1 + r2);
    return dist2(x1, y1, x2, y2) <= rr;
}

void tryMove(float dx, float dy, float dt) {
    // attempt to move player by (vx*dt, vy*dt) and resolve obstacle collisions
    float nx = player.x + dx, ny = player.y + dy;
    // clamp to game area
    nx = clampf(nx, player.r, W - player.r);
    ny = clampf(ny, GAME_Y0 + player.r, GAME_Y1 - player.r);

    // check obstacles
    bool blocked = false;
    for (const auto& o : obstacles) {
        // treat obstacle as square; collide if circle center inside expanded square
        float half = o.r;
        float cx = clampf(nx, o.x - half, o.x + half);
        float cy = clampf(ny, o.y - half, o.y + half);
        if (dist2(nx, ny, cx, cy) < player.r * player.r) {
            blocked = true;
            break;
        }
    }

    if (blocked) {
        if (!player.shielded && timeSec >= nextHitTime) {
            player.lives = std::max(0, player.lives - 1);
            nextHitTime = timeSec + 0.5f;  // half-second i-frames
            if (player.lives == 0) { phase = PHASE_LOSE; }
        }
        // stay in place
    }
    else {
        player.x = nx; player.y = ny;
    }
}

// ---------------- Game Loop ----------------
void updateGame(float dt) {
    if (phase != PHASE_PLAY) return;

    // countdown
    float elapsed = timeSec - roundStart;
    int remain = ROUND_TIME_SEC - (int)elapsed;
    timeLeft = (remain > 0 ? remain : 0);
    if (timeLeft <= 0) { phase = PHASE_LOSE; return; }

    // expire powerups
    if (timeSec > player.shieldUntil) player.shielded = false;

    // input to velocity
    float vx = 0, vy = 0;
    float spd = currentSpeed();
    if (keyW || keyUp)    vy += spd;
    if (keyS || keyDown)  vy -= spd;
    if (keyA || keyLeft)  vx -= spd;
    if (keyD || keyRight) vx += spd;

    // rotate to face movement
    if (vx != 0 || vy != 0) {
        player.angleDeg = atan2f(vy, vx) * 57.29578f;
    }

    // integrate
    tryMove(vx * dt, vy * dt, dt);

    // collectibles
    for (size_t i = 0; i < collectibles.size();) {
        if (intersectCircleCircle(player.x, player.y, player.r, collectibles[i].x, collectibles[i].y, collectibles[i].r)) {
            player.score += 5;
            collectibles.erase(collectibles.begin() + i);
        }
        else i++;
    }

    // powerups
    for (size_t i = 0; i < powerups.size();) {
        if (intersectCircleCircle(player.x, player.y, player.r, powerups[i].x, powerups[i].y, powerups[i].r)) {
            if (powerups[i].type == OBJ_PU_SPEED) {
                player.speedUntil = timeSec + POWERUP_DURATION;
            }
            else { // shield
                player.shielded = true;
                player.shieldUntil = timeSec + SHIELD_DURATION;
            }
            powerups.erase(powerups.begin() + i);
        }
        else i++;
    }

    // target
    int curT[2]; bezierPoint(target.t, target.p0, target.p1, target.p2, target.p3, curT);
    if (intersectCircleCircle(player.x, player.y, player.r, (float)curT[0], (float)curT[1], target.r)) {
        phase = PHASE_WIN;
    }
}

void updateTarget(float dt) {
    // ping-pong t in [0,1]
    float speedT = 0.35f; // Bezier speed
    target.t += target.dir * speedT * dt;
    if (target.t > 1.0f) { target.t = 1.0f; target.dir = -1; }
    if (target.t < 0.0f) { target.t = 0.0f; target.dir = +1; }
}

// ---------------- Display ----------------
void Display() {
    glClear(GL_COLOR_BUFFER_BIT);

    drawPanels();

    // Draw placed objects (with gentle bob animation)
    float bob = sinf(timeSec * 2.2f) * 4.0f;

    for (const auto& o : obstacles) drawObstacle(o);

    for (const auto& c : collectibles) {
        Obj tmp = c; tmp.y += bob * 0.25f;
        drawCollectible(tmp);
    }

    for (const auto& p : powerups) {
        Obj tmp = p; tmp.y += bob * 0.35f;
        drawPowerup(tmp);
    }

    // Target current position
    int cur[2]; bezierPoint(target.t, target.p0, target.p1, target.p2, target.p3, cur);
    Target drawT = target; drawT.p0[0] = cur[0]; drawT.p0[1] = cur[1];
    drawTarget(drawT);

    // Player
    drawPlayer(player);

    // End screens
    if (phase == PHASE_WIN) {
        glColor3f(0, 0, 0); drawQuad(0, GAME_Y0, W, GAME_Y1 - GAME_Y0);
        glColor3f(0, 1, 0);
        print(W / 2 - 40, (GAME_Y0 + GAME_Y1) / 2 + 10, "YOU WIN!");
        char b[64]; sprintf(b, "Final Score: %d", player.score);
        print(W / 2 - 60, (GAME_Y0 + GAME_Y1) / 2 - 10, b);
    }
    else if (phase == PHASE_LOSE) {
        glColor3f(0, 0, 0); drawQuad(0, GAME_Y0, W, GAME_Y1 - GAME_Y0);
        glColor3f(1, 0, 0);
        print(W / 2 - 40, (GAME_Y0 + GAME_Y1) / 2 + 10, "YOU LOSE");
        char b[64]; sprintf(b, "Final Score: %d", player.score);
        print(W / 2 - 60, (GAME_Y0 + GAME_Y1) / 2 - 10, b);
    }

    glFlush();
}

// ---------------- Input ----------------
void startRound() {
    // Player at lower center; target opposite at near top
    player.x = W * 0.5f; player.y = GAME_Y0 + 40.0f;
    player.angleDeg = 90; player.lives = MAX_LIVES; player.shielded = false;
    player.score = 0;
    player.speedUntil = player.shieldUntil = 0;

    // Target Bezier horizontally across the top band
    int yTop = H - TOP_H - 60;
    target.p0[0] = 100; target.p0[1] = yTop;
    target.p1[0] = 300; target.p1[1] = yTop + 80;
    target.p2[0] = 700; target.p2[1] = yTop - 80;
    target.p3[0] = 900; target.p3[1] = yTop;
    target.t = 0.0f; target.dir = +1;

    // reset time
    roundStart = timeSec;
    timeLeft = ROUND_TIME_SEC;

    phase = PHASE_PLAY;
}

void Keyboard(unsigned char key, int x, int y) {
    if (key == 'r' || key == 'R') {
        startRound();
        glutPostRedisplay();
        return;
    }
    if (key == 'w') keyW = true;
    if (key == 's') keyS = true;
    if (key == 'a') keyA = true;
    if (key == 'd') keyD = true;
}
void KeyboardUp(unsigned char key, int x, int y) {
    if (key == 'w') keyW = false;
    if (key == 's') keyS = false;
    if (key == 'a') keyA = false;
    if (key == 'd') keyD = false;
}
void Special(int key, int x, int y) {
    if (key == GLUT_KEY_UP)    keyUp = true;
    if (key == GLUT_KEY_DOWN)  keyDown = true;
    if (key == GLUT_KEY_LEFT)  keyLeft = true;
    if (key == GLUT_KEY_RIGHT) keyRight = true;
}
void SpecialUp(int key, int x, int y) {
    if (key == GLUT_KEY_UP)    keyUp = false;
    if (key == GLUT_KEY_DOWN)  keyDown = false;
    if (key == GLUT_KEY_LEFT)  keyLeft = false;
    if (key == GLUT_KEY_RIGHT) keyRight = false;
}

void Mouse(int button, int state, int x, int y) {
    if (state != GLUT_DOWN || button != GLUT_LEFT_BUTTON) return;

    // flip y to OpenGL coords (like your sample 4)
    y = H - y;

    // If clicked in bottom palette: choose mode
    if (y <= BOT_H) {
        if (dist2((float)x, (float)y, 80.0f, BOT_H * 0.5f) < 35 * 35) placeMode = PLACE_OBS;
        else if (dist2((float)x, (float)y, 240.0f, BOT_H * 0.5f) < 35 * 35) placeMode = PLACE_COL;
        else if (dist2((float)x, (float)y, 400.0f, BOT_H * 0.5f) < 35 * 35) placeMode = PLACE_PU_SPEED;
        else if (dist2((float)x, (float)y, 560.0f, BOT_H * 0.5f) < 35 * 35) placeMode = PLACE_PU_SHIELD;
        else placeMode = PLACE_NONE;
        glutPostRedisplay();
        return;
    }

    // If clicked in game area while in edit phase: place objects
    if (phase == PHASE_EDIT && inGameArea((float)x, (float)y)) {
        Obj o; o.x = (float)x; o.y = (float)y; o.r = 16.0f;
        if (placeMode == PLACE_OBS) {
            o.type = OBJ_OBSTACLE; o.r = 18.0f;
            if (!overlapsAny(o.x, o.y, o.r)) obstacles.push_back(o);
        }
        else if (placeMode == PLACE_COL) {
            o.type = OBJ_COLLECT; o.r = 14.0f;
            if (!overlapsAny(o.x, o.y, o.r)) collectibles.push_back(o);
        }
        else if (placeMode == PLACE_PU_SPEED) {
            o.type = OBJ_PU_SPEED; o.r = 14.0f;
            if (!overlapsAny(o.x, o.y, o.r)) powerups.push_back(o);
        }
        else if (placeMode == PLACE_PU_SHIELD) {
            o.type = OBJ_PU_SHIELD; o.r = 14.0f;
            if (!overlapsAny(o.x, o.y, o.r)) powerups.push_back(o);
        }
        glutPostRedisplay();
    }
}

// ---------------- Timer (like your sample 3) ----------------
void Timer(int) {
    static int lastMs = 0;
    int nowMs = glutGet(GLUT_ELAPSED_TIME);
    if (lastMs == 0) lastMs = nowMs;
    float dt = (nowMs - lastMs) / 1000.0f;
    lastMs = nowMs;

    timeSec += dt;

    // Animate target even in edit so you can see it move
    updateTarget(dt);
    if (phase == PHASE_PLAY) updateGame(dt);

    glutPostRedisplay();
    glutTimerFunc(16, Timer, 0); // ~60 FPS
}

// ---------------- Main ----------------
void initScene() {
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gluOrtho2D(0.0, (GLdouble)W, 0.0, (GLdouble)H);

    // Initial player bottom center; target top band Bezier set at startRound()
    // Begin in EDIT mode (place objects first)
    placeMode = PLACE_NONE;
    phase = PHASE_EDIT;
    timeLeft = ROUND_TIME_SEC;
}

void DisplayWrapper() { Display(); }

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_SINGLE | GLUT_RGB);
    glutInitWindowSize(W, H);
    glutCreateWindow("OpenGL 2D Game - GLUT Skeleton");

    glutDisplayFunc(DisplayWrapper);
    glutKeyboardFunc(Keyboard);
    glutKeyboardUpFunc(KeyboardUp);
    glutSpecialFunc(Special);
    glutSpecialUpFunc(SpecialUp);
    glutMouseFunc(Mouse);
    glutTimerFunc(0, Timer, 0);

    initScene();

    glutMainLoop();
    return 0;
}
