// Teenage Mutant Ninja Turtles' libUnityNatives.so: its build puts several
// small gameplay helpers there as well as the cabinet calls (those of the
// Raw Thrills Unity base, rawthrills/unity/unityRt.c).
#include <stdint.h>

void GetSprintInfo(uint8_t *doubleTap, uint8_t *hold, float *holdTime)
{
    if (doubleTap) *doubleTap = 0;
    if (hold) *hold = 0;
    if (holdTime) *holdTime = 0;
}
int BossInvincibilityHits(float health, float maxHealth) { (void)health; (void)maxHealth; return 1; }
uint8_t ThrowCheck(float dist, float forward, float right) { return dist >= 0 && forward >= -180 && forward <= 180 && right >= -180 && right <= 180; }
uint8_t ThrowCheckScreen(float dist, float forward, float right) { return ThrowCheck(dist, forward, right); }
uint8_t AllowAttack(uint8_t boss, float dmg5, float dmg8, float enemyDmg5, float health,
                    float playerHealth, float playerTime, float sinceDamage, float sinceDealt)
{
    (void)boss; (void)dmg5; (void)dmg8; (void)enemyDmg5; (void)health;
    (void)playerHealth; (void)playerTime; (void)sinceDamage; (void)sinceDealt;
    return 1;
}
uint8_t CheckWaveComplete(float sinceDropIn, int alive, uint8_t allSpawned)
{
    (void)sinceDropIn;
    return alive <= 0 && allSpawned;
}
float CurrentDamageMod(float cycle, float control, float gameMod)
{
    (void)cycle; (void)control;
    return gameMod;
}
uint8_t RestrainCheck(float dist, float between, float down)
{
    return dist >= 0 && between >= -180 && between <= 180 && down >= -180 && down <= 180;
}
uint8_t RestrainCheckMouser(float dist, float between, float up)
{
    return RestrainCheck(dist, between, up);
}
float GetRotZTarget(float angle) { return angle; }
float GetRotXTarget(uint8_t rect, float angle) { (void)rect; return angle; }
uint8_t GetNewHeight(float *base, float *velocity, float delta, uint8_t gravity)
{
    if (!base || !velocity || delta < 0) return 0;
    if (gravity) *velocity -= 9.81f * delta;
    *base += *velocity * delta;
    return 1;
}
float GetCircleY(float start, float offset, float original, float elevator, float mod)
{
    return start + (offset - original) + elevator * mod;
}
float GetAngleDelta(float maxSpeed, float delta, float angle)
{
    float step = maxSpeed * delta;
    if (step < 0) step = -step;
    if (angle > 180) angle -= 360;
    if (angle < -180) angle += 360;
    if (angle > step) return step;
    if (angle < -step) return -step;
    return angle;
}
