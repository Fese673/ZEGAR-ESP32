#include <cstdio>
#include <cstring>

#include "AppRuntime.h"
#include "LCDMirror.h"
#include "TANK-GAMES/TankGame.h"
#ifndef TANK_GAME_DEBUG
#define TANK_GAME_DEBUG 0
#endif

#if TANK_GAME_DEBUG
#define TANK_GAME_LOG(fmt, ...) Serial.printf("[TANK] " fmt "\r\n", ##__VA_ARGS__)
#else
#define TANK_GAME_LOG(fmt, ...)
#endif

namespace TankGame {
namespace {

constexpr uint8_t kEnemyCount = 3;
constexpr uint8_t kWinScore = 6;
constexpr unsigned long kLogicTickMs = 110UL;
constexpr unsigned long kBulletTickMs = 320UL;
constexpr unsigned long kStartDelayMs = 1800UL;
constexpr unsigned long kVictoryHoldMs = 5000UL;
constexpr unsigned long kDefeatHoldMs = 5000UL;
constexpr unsigned long kFireHoldMs = 2000UL;
constexpr unsigned long kEnemyThinkMs = 140UL;
constexpr unsigned long kEnemyShotCooldownMs = 850UL;
constexpr uint8_t kEnemyRespawnChance = 5;
constexpr uint8_t kEnemyWanderChance = 18;

constexpr uint8_t kGlyphTankUp = 1;
constexpr uint8_t kGlyphTankRight = 2;
constexpr uint8_t kGlyphTankDown = 3;
constexpr uint8_t kGlyphTankLeft = 4;
constexpr uint8_t kGlyphBullet = 5;

enum class Mode : uint8_t {
  Idle,
  Starting,
  Running,
  VictoryHold,
  DefeatHold,
};

enum class Direction : uint8_t {
  Up = 0,
  Right = 1,
  Down = 2,
  Left = 3,
};

struct Bullet {
  int8_t x = 0;
  int8_t y = 0;
  Direction direction = Direction::Up;
  bool alive = false;
  unsigned long spawnedAtMs = 0;
  unsigned long lastAdvanceMs = 0;
};

struct TankEntity {
  int8_t x = 0;
  int8_t y = 0;
  Direction direction = Direction::Up;
  bool alive = false;
  unsigned long lastThinkMs = 0;
  unsigned long lastShotMs = 0;
  Bullet bullet;
};

struct SpawnPoint {
  int8_t x;
  int8_t y;
  Direction direction;
};

struct State {
  Mode mode = Mode::Idle;
  TankEntity player;
  TankEntity enemies[kEnemyCount];
  int32_t score = 0;
  bool fireLatched = false;
  unsigned long lastLogicMs = 0;
  unsigned long startUntilMs = 0;
  unsigned long terminalUntilMs = 0;
};

State s_state;

byte spriteTankUp[] = {
  0b00000,
  0b00100,
  0b00100,
  0b11111,
  0b11011,
  0b10001,
  0b00000,
  0b00000};

byte spriteTankRight[] = {
  0b00000,
  0b11100,
  0b01100,
  0b01111,
  0b01100,
  0b11100,
  0b00000,
  0b00000};

byte spriteTankDown[] = {
  0b00000,
  0b10001,
  0b11111,
  0b11111,
  0b00100,
  0b00100,
  0b00000,
  0b00000};

byte spriteTankLeft[] = {
  0b00000,
  0b00111,
  0b00110,
  0b11110,
  0b00110,
  0b00111,
  0b00000,
  0b00000};

byte spriteBullet[] = {
  0b00000,
  0b00000,
  0b00000,
  0b00100,
  0b00000,
  0b00000,
  0b00000,
  0b00000};

static int clampInt(int value, int minValue, int maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

static int absInt(int value) {
  return (value < 0) ? -value : value;
}

static bool inBounds(int x, int y) {
  return x >= 0 && x < LCD_COLS && y >= 0 && y < LCD_ROWS;
}

static Direction turnClockwise(Direction direction) {
  switch (direction) {
    case Direction::Up:
      return Direction::Right;
    case Direction::Right:
      return Direction::Down;
    case Direction::Down:
      return Direction::Left;
    case Direction::Left:
    default:
      return Direction::Up;
  }
}

static Direction turnCounterClockwise(Direction direction) {
  switch (direction) {
    case Direction::Up:
      return Direction::Left;
    case Direction::Left:
      return Direction::Down;
    case Direction::Down:
      return Direction::Right;
    case Direction::Right:
    default:
      return Direction::Up;
  }
}

static Direction oppositeDirection(Direction direction) {
  switch (direction) {
    case Direction::Up:
      return Direction::Down;
    case Direction::Right:
      return Direction::Left;
    case Direction::Down:
      return Direction::Up;
    case Direction::Left:
    default:
      return Direction::Right;
  }
}

static Direction directionTowardPlayer(const TankEntity& tank) {
  const int dx = static_cast<int>(s_state.player.x) - static_cast<int>(tank.x);
  const int dy = static_cast<int>(s_state.player.y) - static_cast<int>(tank.y);

  if (absInt(dx) >= absInt(dy)) {
    return (dx >= 0) ? Direction::Right : Direction::Left;
  }

  return (dy >= 0) ? Direction::Down : Direction::Up;
}

static uint8_t enemyDifficultyTier() {
  return static_cast<uint8_t>(clampInt(static_cast<int>(s_state.score / 2), 0, 4));
}

static unsigned long enemyThinkDelayMs() {
  const unsigned long tier = enemyDifficultyTier();
  const unsigned long reduction = tier * 15UL;
  return (kEnemyThinkMs > reduction) ? (kEnemyThinkMs - reduction) : 60UL;
}

static unsigned long enemyShotCooldownMs() {
  const unsigned long tier = enemyDifficultyTier();
  const unsigned long reduction = tier * 80UL;
  return (kEnemyShotCooldownMs > reduction) ? (kEnemyShotCooldownMs - reduction) : 400UL;
}

static uint8_t enemyWanderChance() {
  const uint8_t tier = enemyDifficultyTier();
  const uint8_t bonus = static_cast<uint8_t>(tier * 4U);
  return static_cast<uint8_t>(kEnemyWanderChance + bonus);
}

static int8_t deltaX(Direction direction) {
  switch (direction) {
    case Direction::Left:
      return -1;
    case Direction::Right:
      return 1;
    default:
      return 0;
  }
}

static int8_t deltaY(Direction direction) {
  switch (direction) {
    case Direction::Up:
      return -1;
    case Direction::Down:
      return 1;
    default:
      return 0;
  }
}

static uint8_t glyphForDirection(Direction direction) {
  switch (direction) {
    case Direction::Up:
      return kGlyphTankUp;
    case Direction::Right:
      return kGlyphTankRight;
    case Direction::Down:
      return kGlyphTankDown;
    case Direction::Left:
    default:
      return kGlyphTankLeft;
  }
}

static void fillBlankRow(char* row) {
  memset(row, ' ', LCD_COLS);
  row[LCD_COLS] = '\0';
}

static void fillBlankBoard(char rows[LCD_ROWS][LCD_COLS + 1]) {
  for (uint8_t row = 0; row < LCD_ROWS; ++row) {
    fillBlankRow(rows[row]);
  }
}

static void placeCell(char rows[LCD_ROWS][LCD_COLS + 1], int x, int y, uint8_t glyph) {
  if (!inBounds(x, y)) {
    return;
  }
  rows[y][x] = static_cast<char>(glyph);
}

static bool isCellFreeForSpawn(int x, int y);

static bool trySpawnEnemyAt(TankEntity& enemy, int x, int y, Direction direction) {
  if (!isCellFreeForSpawn(x, y)) {
    return false;
  }

  enemy.x = static_cast<int8_t>(x);
  enemy.y = static_cast<int8_t>(y);
  enemy.direction = direction;
  enemy.alive = true;
  enemy.lastThinkMs = 0;
  enemy.lastShotMs = 0;
  enemy.bullet.alive = false;
  enemy.bullet.spawnedAtMs = 0;
  return true;
}

static bool isLiveTankAt(int x, int y, const TankEntity* skipTank = nullptr) {
  if (s_state.player.alive && &s_state.player != skipTank && s_state.player.x == x && s_state.player.y == y) {
    return true;
  }

  for (uint8_t index = 0; index < kEnemyCount; ++index) {
    const TankEntity& enemy = s_state.enemies[index];
    if (&enemy == skipTank) {
      continue;
    }
    if (enemy.alive && enemy.x == x && enemy.y == y) {
      return true;
    }
  }

  return false;
}

static bool isCellFreeForSpawn(int x, int y) {
  if (!inBounds(x, y)) {
    return false;
  }

  if (s_state.player.alive && s_state.player.x == x && s_state.player.y == y) {
    return false;
  }

  if (s_state.player.bullet.alive && s_state.player.bullet.x == x && s_state.player.bullet.y == y) {
    return false;
  }

  for (uint8_t index = 0; index < kEnemyCount; ++index) {
    const TankEntity& enemy = s_state.enemies[index];
    if (enemy.alive && enemy.x == x && enemy.y == y) {
      return false;
    }
    if (enemy.bullet.alive && enemy.bullet.x == x && enemy.bullet.y == y) {
      return false;
    }
  }

  return true;
}

static bool canMoveTankTo(const TankEntity* movingTank, int x, int y) {
  if (!inBounds(x, y)) {
    return false;
  }
  return !isLiveTankAt(x, y, movingTank);
}

static bool tryMoveTankInDirection(TankEntity& tank, Direction direction) {
  if (!tank.alive) {
    return false;
  }

  tank.direction = direction;

  const int targetX = static_cast<int>(tank.x) + deltaX(direction);
  const int targetY = static_cast<int>(tank.y) + deltaY(direction);

  if (!canMoveTankTo(&tank, targetX, targetY)) {
    return false;
  }

  tank.x = static_cast<int8_t>(targetX);
  tank.y = static_cast<int8_t>(targetY);
  return true;
}

static bool tryRandomMove(TankEntity& tank) {
  const Direction directions[] = {
      Direction::Up,
      Direction::Right,
      Direction::Down,
      Direction::Left,
  };

  const uint8_t start = static_cast<uint8_t>(random(4));
  for (uint8_t offset = 0; offset < 4; ++offset) {
    if (tryMoveTankInDirection(tank, directions[(start + offset) % 4])) {
      return true;
    }
  }

  return false;
}

static bool hasClearShotToPlayer(const TankEntity& tank) {
  if (!s_state.player.alive) {
    return false;
  }

  if (tank.x == s_state.player.x) {
    const int step = (s_state.player.y > tank.y) ? 1 : -1;
    for (int y = static_cast<int>(tank.y) + step; y != s_state.player.y; y += step) {
      if (isLiveTankAt(tank.x, y, &tank)) {
        return false;
      }
    }
    return true;
  }

  if (tank.y == s_state.player.y) {
    const int step = (s_state.player.x > tank.x) ? 1 : -1;
    for (int x = static_cast<int>(tank.x) + step; x != s_state.player.x; x += step) {
      if (isLiveTankAt(x, tank.y, &tank)) {
        return false;
      }
    }
    return true;
  }

  return false;
}

static bool chasePlayer(TankEntity& tank) {
  const int dx = static_cast<int>(s_state.player.x) - static_cast<int>(tank.x);
  const int dy = static_cast<int>(s_state.player.y) - static_cast<int>(tank.y);

  if (dx == 0 && dy == 0) {
    return tryRandomMove(tank);
  }

  const bool horizontalPreferred = absInt(dx) >= absInt(dy);
  const Direction primary = horizontalPreferred ? (dx > 0 ? Direction::Right : Direction::Left)
                                                : (dy > 0 ? Direction::Down : Direction::Up);
  const Direction secondary = horizontalPreferred ? (dy > 0 ? Direction::Down : Direction::Up)
                                                  : (dx > 0 ? Direction::Right : Direction::Left);
  const Direction fallback = oppositeDirection(primary);

  if (tryMoveTankInDirection(tank, primary)) {
    return true;
  }

  if (tryMoveTankInDirection(tank, secondary)) {
    return true;
  }

  if (tryMoveTankInDirection(tank, fallback)) {
    return true;
  }

  return tryRandomMove(tank);
}

static bool trySpawnEnemy(TankEntity& enemy) {
  for (uint8_t attempt = 0; attempt < 32; ++attempt) {
    const int x = static_cast<int>(random(LCD_COLS));
    const int y = static_cast<int>(random(LCD_ROWS));

    if (trySpawnEnemyAt(enemy, x, y, static_cast<Direction>(random(4)))) {
      return true;
    }
  }

  return false;
}

static void spawnInitialEnemies() {
  constexpr SpawnPoint kSpawnPoints[kEnemyCount] = {
      {3, 0, Direction::Down},
      {10, 1, Direction::Down},
      {16, 2, Direction::Down},
  };

  for (uint8_t index = 0; index < kEnemyCount; ++index) {
    s_state.enemies[index] = TankEntity{};
    if (!trySpawnEnemyAt(s_state.enemies[index],
                         kSpawnPoints[index].x,
                         kSpawnPoints[index].y,
                         kSpawnPoints[index].direction)) {
      (void)trySpawnEnemy(s_state.enemies[index]);
    }
  }
}

static bool fireBullet(TankEntity& tank, unsigned long nowMs, bool playTone) {
  if (!tank.alive || tank.bullet.alive) {
    return false;
  }

  tank.bullet.x = tank.x;
  tank.bullet.y = tank.y;
  tank.bullet.direction = tank.direction;
  tank.bullet.spawnedAtMs = nowMs;
  tank.bullet.lastAdvanceMs = nowMs;
  tank.bullet.alive = true;

  if (playTone) {
    noTone(BUZZER_PIN);
    tone(BUZZER_PIN, 620, 70);
  }

  return true;
}

static void advanceBullet(Bullet& bullet, unsigned long nowMs) {
  if (!bullet.alive) {
    return;
  }

  if (nowMs - bullet.lastAdvanceMs < kBulletTickMs) {
    return;
  }

  bullet.lastAdvanceMs = nowMs;
  bullet.x = static_cast<int8_t>(bullet.x + deltaX(bullet.direction));
  bullet.y = static_cast<int8_t>(bullet.y + deltaY(bullet.direction));

  if (!inBounds(bullet.x, bullet.y)) {
    bullet.alive = false;
    return;
  }
}

static void moveTankForward(TankEntity& tank) {
  if (!tank.alive) {
    return;
  }

  const int targetX = clampInt(static_cast<int>(tank.x) + deltaX(tank.direction), 0, LCD_COLS - 1);
  const int targetY = clampInt(static_cast<int>(tank.y) + deltaY(tank.direction), 0, LCD_ROWS - 1);

  if (targetX == tank.x && targetY == tank.y) {
    return;
  }

  if (!canMoveTankTo(&tank, targetX, targetY)) {
    return;
  }

  tank.x = static_cast<int8_t>(targetX);
  tank.y = static_cast<int8_t>(targetY);
}

static void moveTankToDirection(TankEntity& tank, Direction direction) {
  if (tank.direction != direction) {
    tank.direction = direction;
    return;
  }

  moveTankForward(tank);
}

static void maybeRespawnEnemy(TankEntity& enemy) {
  if (enemy.alive) {
    return;
  }

  if (random(100) >= kEnemyRespawnChance) {
    return;
  }

  (void)trySpawnEnemy(enemy);
}

static void updateEnemyAction(TankEntity& enemy, unsigned long nowMs) {
  if (!enemy.alive) {
    return;
  }

  const unsigned long thinkDelayMs = enemyThinkDelayMs();
  if (nowMs - enemy.lastThinkMs < thinkDelayMs) {
    return;
  }

  enemy.lastThinkMs = nowMs;

  const bool clearShot = hasClearShotToPlayer(enemy);
  const uint8_t difficulty = enemyDifficultyTier();
  const uint8_t action = static_cast<uint8_t>(random(60));

  if (action < 4U) {
    Direction direction = static_cast<Direction>(action);
    if (difficulty > 0 && random(100) < static_cast<int>(difficulty * 12U)) {
      direction = directionTowardPlayer(enemy);
    }

    moveTankToDirection(enemy, direction);
    return;
  }

  if (action == 4U || (difficulty > 2 && action == 5U)) {
    if (clearShot && (nowMs - enemy.lastShotMs >= enemyShotCooldownMs())) {
      if (fireBullet(enemy, nowMs, false)) {
        enemy.lastShotMs = nowMs;
        return;
      }
    }

    if (difficulty > 0 && random(100) < 50U) {
      enemy.direction = directionTowardPlayer(enemy);
    }
    return;
  }

  if (clearShot && difficulty > 1 && action < static_cast<uint8_t>(8U + difficulty)) {
    if ((nowMs - enemy.lastShotMs >= enemyShotCooldownMs()) && fireBullet(enemy, nowMs, false)) {
      enemy.lastShotMs = nowMs;
    }
    return;
  }

  if (random(100) < enemyWanderChance()) {
    if (difficulty > 1) {
      moveTankToDirection(enemy, directionTowardPlayer(enemy));
    } else {
      const Direction direction = static_cast<Direction>(random(4));
      moveTankToDirection(enemy, direction);
    }
  }
}

static void updateHeldFire(unsigned long nowMs) {
  const unsigned long holdMs = encoder_button_hold_ms();

  if (holdMs == 0) {
    s_state.fireLatched = false;
    return;
  }

  if (holdMs < kFireHoldMs || s_state.fireLatched) {
    return;
  }

  s_state.fireLatched = true;
  if (fireBullet(s_state.player, nowMs, true)) {
    s_state.lastLogicMs = 0;
  }
}

static void enterVictory(unsigned long nowMs) {
  s_state.mode = Mode::VictoryHold;
  s_state.terminalUntilMs = nowMs + kVictoryHoldMs;
  s_state.player.bullet.alive = false;

  for (uint8_t index = 0; index < kEnemyCount; ++index) {
    s_state.enemies[index].bullet.alive = false;
  }

  noTone(BUZZER_PIN);
  tone(BUZZER_PIN, 1760, 120);
  draw();
}

static void enterDefeat(unsigned long nowMs) {
  s_state.mode = Mode::DefeatHold;
  s_state.terminalUntilMs = nowMs + kDefeatHoldMs;
  s_state.player.alive = false;
  s_state.player.bullet.alive = false;

  for (uint8_t index = 0; index < kEnemyCount; ++index) {
    s_state.enemies[index].bullet.alive = false;
  }

  noTone(BUZZER_PIN);
  tone(BUZZER_PIN, 220, 180);
  draw();
}

static void resolveBulletHits(unsigned long nowMs) {
  if (s_state.mode != Mode::Running) {
    return;
  }

  if (s_state.player.bullet.alive) {
    for (uint8_t index = 0; index < kEnemyCount; ++index) {
      TankEntity& enemy = s_state.enemies[index];
      if (!enemy.alive) {
        continue;
      }

      if (enemy.x == s_state.player.bullet.x && enemy.y == s_state.player.bullet.y) {
        enemy.alive = false;
        enemy.bullet.alive = false;
        s_state.player.bullet.alive = false;
        ++s_state.score;
        noTone(BUZZER_PIN);
        tone(BUZZER_PIN, 300, 70);
        if (s_state.score >= kWinScore) {
          enterVictory(nowMs);
        }
        return;
      }
    }
  }

  if (s_state.mode != Mode::Running || !s_state.player.alive) {
    return;
  }

  for (uint8_t index = 0; index < kEnemyCount; ++index) {
    TankEntity& enemy = s_state.enemies[index];
    if (!enemy.alive || !enemy.bullet.alive) {
      continue;
    }

    if (enemy.bullet.x == s_state.player.x && enemy.bullet.y == s_state.player.y) {
      enemy.bullet.alive = false;
      enterDefeat(nowMs);
      return;
    }
  }
}

static void writeRows(char rows[LCD_ROWS][LCD_COLS + 1]) {
  for (uint8_t row = 0; row < LCD_ROWS; ++row) {
    LCD_SET(0, row);
    LCD_PRINT(rows[row]);
  }
  LCD_DUMP();
}

static void buildRunningFrame(char rows[LCD_ROWS][LCD_COLS + 1]) {
  fillBlankBoard(rows);

  for (uint8_t index = 0; index < kEnemyCount; ++index) {
    const TankEntity& enemy = s_state.enemies[index];
    if (!enemy.bullet.alive) {
      if (enemy.alive) {
        placeCell(rows, enemy.x, enemy.y, glyphForDirection(enemy.direction));
      }
      continue;
    }

    placeCell(rows, enemy.bullet.x, enemy.bullet.y, kGlyphBullet);
    if (enemy.alive) {
      placeCell(rows, enemy.x, enemy.y, glyphForDirection(enemy.direction));
    }
  }

  if (s_state.player.bullet.alive) {
    placeCell(rows, s_state.player.bullet.x, s_state.player.bullet.y, kGlyphBullet);
  }

  for (uint8_t index = 0; index < kEnemyCount; ++index) {
    const TankEntity& enemy = s_state.enemies[index];
    if (!enemy.alive) {
      continue;
    }

    placeCell(rows, enemy.x, enemy.y, glyphForDirection(enemy.direction));
  }

  if (s_state.player.alive) {
    placeCell(rows, s_state.player.x, s_state.player.y, glyphForDirection(s_state.player.direction));
  }
}

static void centerText(char* row, const char* text) {
  fillBlankRow(row);

  if (text == nullptr) {
    return;
  }

  const size_t len = strlen(text);
  const size_t copyLen = (len > LCD_COLS) ? LCD_COLS : len;
  const size_t start = (LCD_COLS - copyLen) / 2;
  memcpy(row + start, text, copyLen);
}

static void buildTerminalFrame(char rows[LCD_ROWS][LCD_COLS + 1], bool victory) {
  fillBlankBoard(rows);

  centerText(rows[0], "TANK GAME");
  centerText(rows[1], victory ? "MISSION CLEAR" : "GAME OVER");

  char scoreLine[LCD_COLS + 1];
  snprintf(scoreLine,
           sizeof(scoreLine),
           "SCORE %02ld / %02u",
           static_cast<long>(s_state.score),
           static_cast<unsigned>(kWinScore));
  centerText(rows[2], scoreLine);
  centerText(rows[3], "RETURN IN 5S");
}

static void buildStartingFrame(char rows[LCD_ROWS][LCD_COLS + 1], unsigned long nowMs) {
  fillBlankBoard(rows);
  centerText(rows[0], "TANK GAME");
  centerText(rows[1], "GET READY");

  const unsigned long remainingMs = (s_state.startUntilMs > nowMs) ? (s_state.startUntilMs - nowMs) : 0;
  const unsigned long remainingSecs = (remainingMs + 999UL) / 1000UL;
  char countdownLine[LCD_COLS + 1];
  snprintf(countdownLine, sizeof(countdownLine), "START IN %1lu SEC", remainingSecs);
  centerText(rows[2], countdownLine);
  centerText(rows[3], "ROTATE TO AIM");
}

static void drawStarting(unsigned long nowMs) {
  char rows[LCD_ROWS][LCD_COLS + 1];
  buildStartingFrame(rows, nowMs);
  writeRows(rows);
}

static void drawRunning() {
  char rows[LCD_ROWS][LCD_COLS + 1];
  buildRunningFrame(rows);
  writeRows(rows);
}

static void drawTerminal(bool victory) {
  char rows[LCD_ROWS][LCD_COLS + 1];
  buildTerminalFrame(rows, victory);
  writeRows(rows);
}

static void resetBattlefield() {
  const unsigned long nowMs = millis();
  s_state = State{};
  s_state.mode = Mode::Starting;
  s_state.score = 0;
  s_state.fireLatched = false;
  s_state.lastLogicMs = nowMs;
  s_state.startUntilMs = nowMs + kStartDelayMs;
  s_state.terminalUntilMs = 0;

  s_state.player = TankEntity{};
  s_state.player.alive = true;
  const int initialPlayerX = LCD_COLS / 2;
  const int safePlayerX = ((initialPlayerX == 3 || initialPlayerX == 10 || initialPlayerX == 16)
                               ? (initialPlayerX - 1)
                               : initialPlayerX);
  s_state.player.x = static_cast<int8_t>(clampInt(safePlayerX, 0, LCD_COLS - 1));
  s_state.player.y = static_cast<int8_t>(LCD_ROWS - 1);
  s_state.player.direction = Direction::Up;

  for (uint8_t index = 0; index < kEnemyCount; ++index) {
    s_state.enemies[index] = TankEntity{};
  }

  spawnInitialEnemies();
  TANK_GAME_LOG("resetBattlefield mode=Starting player=(%d,%d) startUntil=%lu", s_state.player.x, s_state.player.y, s_state.startUntilMs);
}

static void serviceRunning(unsigned long nowMs) {
  updateHeldFire(nowMs);

  if (nowMs - s_state.lastLogicMs < kLogicTickMs) {
    return;
  }

  s_state.lastLogicMs = nowMs;

  if (s_state.player.bullet.alive) {
    advanceBullet(s_state.player.bullet, nowMs);
  }

  for (uint8_t index = 0; index < kEnemyCount; ++index) {
    TankEntity& enemy = s_state.enemies[index];

    if (!enemy.alive) {
      maybeRespawnEnemy(enemy);
      continue;
    }

    updateEnemyAction(enemy, nowMs);

    if (enemy.bullet.alive) {
      advanceBullet(enemy.bullet, nowMs);
    }
  }

  resolveBulletHits(nowMs);

  if (s_state.mode != Mode::Running) {
    return;
  }

  drawRunning();
}

}  // namespace

void begin() {
  randomSeed(static_cast<uint32_t>(micros()));

  lcd.createChar(kGlyphTankUp, spriteTankUp);
  lcd.createChar(kGlyphTankRight, spriteTankRight);
  lcd.createChar(kGlyphTankDown, spriteTankDown);
  lcd.createChar(kGlyphTankLeft, spriteTankLeft);
  lcd.createChar(kGlyphBullet, spriteBullet);

  noTone(BUZZER_PIN);
  LCD_CLEAR();
  resetBattlefield();
  draw();
}

void stop() {
  s_state = State{};
  noTone(BUZZER_PIN);
}

void handleEvent(EncoderEvent event) {
  if (s_state.mode != Mode::Running) {
    return;
  }

  if (event == ENC_LEFT || event == ENC_RIGHT) {
    const Direction rotated = (event == ENC_RIGHT) ? turnClockwise(s_state.player.direction)
                                                    : turnCounterClockwise(s_state.player.direction);
    s_state.player.direction = rotated;
    drawRunning();
    return;
  }

  if (event != ENC_CLICK) {
    return;
  }

  moveTankForward(s_state.player);
  drawRunning();
}

bool service(unsigned long nowMs) {
  if (s_state.mode == Mode::Idle) {
    return false;
  }

  if (s_state.mode == Mode::Starting) {
    if (nowMs >= s_state.startUntilMs) {
      s_state.mode = Mode::Running;
      s_state.lastLogicMs = nowMs;
      drawRunning();
    } else {
      drawStarting(nowMs);
    }
    return false;
  }

  if (s_state.mode == Mode::Running) {
    serviceRunning(nowMs);
    return false;
  }

  if (nowMs >= s_state.terminalUntilMs) {
    return true;
  }

  return false;
}

void draw() {
  switch (s_state.mode) {
    case Mode::Starting:
      drawStarting(millis());
      break;
    case Mode::Running:
      drawRunning();
      break;
    case Mode::VictoryHold:
      drawTerminal(true);
      break;
    case Mode::DefeatHold:
      drawTerminal(false);
      break;
    case Mode::Idle:
    default:
      break;
  }
}

}  // namespace TankGame
