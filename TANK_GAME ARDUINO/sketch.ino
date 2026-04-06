#include <LiquidCrystal.h>

#include "Enemies.h"
#include "Sprites.h"
#include "Tank.h"

#define LCD_COLUMNS 20
#define LCD_LINES 4
#define BTN_LEFT A0
#define BTN_UP A1
#define BTN_DOWN A2
#define BTN_RIGHT A3
#define BUZZER A4
#define BTN_SHOOT A5

#define RS 12
#define EN 11
#define D4 5
#define D5 4
#define D6 3
#define D7 2

const int TONE_SHOOT = 600;
const int TONE_HIT = 300;
const int TONE_BEGIN = 1400;

LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);

Tanks::Tank player;
Game::Direction playerDirection;
bool isPlayerShooting = false;

Enemies::EnemyTank enemyList[3] = {
    Enemies::EnemyTank(),
    Enemies::EnemyTank(),
    Enemies::EnemyTank()};

int32_t score = 0;

bool isButtonPressed = false;

char currentFrame[LCD_LINES][LCD_COLUMNS];
char nextFrame[LCD_LINES][LCD_COLUMNS];

void setup() {
  lcd.begin(LCD_COLUMNS, LCD_LINES);
  resetGame();

  // Register buttons
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_SHOOT, INPUT_PULLUP);

  // Register buzzer
  pinMode(BUZZER, OUTPUT);

  // Register custom chars
  lcd.createChar(Tanks::MOVE_UP, Sprites::spriteTankUp);
  lcd.createChar(Tanks::MOVE_DOWN, Sprites::spriteTankDown);
  lcd.createChar(Tanks::MOVE_LEFT, Sprites::spriteTankLeft);
  lcd.createChar(Tanks::MOVE_RIGHT, Sprites::spriteTankRight);
  lcd.createChar(Tanks::SHOOT, Sprites::spriteBullet);
}

void resetGame() {
  score = 0;

  // Reset frames
  for (int i = 0; i < LCD_LINES; i++) {
    for (int j = 0; j < LCD_COLUMNS; j++) {
      nextFrame[i][j] = ' ';
      currentFrame[i][j] = ' ';
    }
  }

  // Initiate player
  player.isAlive = true;

  // Initiate enemys
  for (auto &e : enemyList) {
    e.isAlive = false;
    e.bullet.isAlive = false;
  }

  // Start message
  lcd.clear();
  tone(BUZZER, TONE_BEGIN, 200);
  lcd.setCursor(LCD_COLUMNS / 2 - 7, LCD_LINES / 2 - 1);
  lcd.print(F("TANK GAME 3000"));
  delay(3000);
  lcd.clear();
}

void gameOver() {
  tone(BUZZER, TONE_BEGIN, 200);
  lcd.setCursor(LCD_COLUMNS / 2 - 5, LCD_LINES / 2 - 1);
  lcd.print(F("GAME OVER"));
  lcd.setCursor(LCD_COLUMNS / 2 - 7, LCD_LINES - 1);
  lcd.print(F("HIGH SCORE:"));
  lcd.print(score);
  delay(2000);
}

void readUserInput() {
  if (isButtonPressed) {
    isButtonPressed = false;
    return;
  }

  if (digitalRead(BTN_UP) == LOW) {
    playerDirection = Game::UP;
    isButtonPressed = true;
  };

  if (digitalRead(BTN_DOWN) == LOW) {
    playerDirection = Game::DOWN;
    isButtonPressed = true;
  };

  if (digitalRead(BTN_LEFT) == LOW) {
    playerDirection = Game::LEFT;
    isButtonPressed = true;
  }

  if (digitalRead(BTN_RIGHT) == LOW) {
    playerDirection = Game::RIGHT;
    isButtonPressed = true;
  }

  if (digitalRead(BTN_SHOOT) == LOW) {
    isPlayerShooting = true;
    isButtonPressed = true;
  }
}

void gameLogic() {
  executePlayerAction();
  playerCollisionCheck();
  tickEnemy();
  bulletCollisionCheck();
}

void executePlayerAction() {
  if (isButtonPressed) {
    if (isPlayerShooting) {
      if (!player.bullet.isAlive) {
        tone(BUZZER, TONE_SHOOT, 100);
        player.shoot();
      }
      isPlayerShooting = false;
    } else {
      player.move(playerDirection);
    }
  }
  player.bullet.move(player.bullet.direction);
}

void playerCollisionCheck() {
  for (auto e : enemyList) {
    player.entityCollisionAvoidance(&e);
  };
  player.wallCollisionAvoidance(LCD_COLUMNS, LCD_LINES);
  player.bullet.wallCollisionAvoidance(LCD_COLUMNS, LCD_LINES);
}

void tickEnemy() {
  for (auto &e : enemyList) {
    if (e.isAlive) {
      e.tick();
      e.bullet.move(e.bullet.direction);
      e.entityCollisionAvoidance(&player);
      e.wallCollisionAvoidance(LCD_COLUMNS, LCD_LINES);
      e.bullet.wallCollisionAvoidance(LCD_COLUMNS, LCD_LINES);
    } else {
      e.spawn(LCD_COLUMNS, LCD_LINES, player);
    }
  }
}

void bulletCollisionCheck() {
  // Check if bullet hits enemy
  if (player.bullet.isAlive) {
    for (auto &e : enemyList) {
      if (e.isAlive) {
        if (player.bullet.x == e.x &&
            player.bullet.y == e.y) {
          score++;
          player.bullet.destroy();
          e.destroy();
          tone(BUZZER, TONE_HIT, 100);
        }
      }
    }
  }

  // Check if bullet hits player
  for (auto &e : enemyList) {
    if (e.isAlive) {
      if (e.bullet.isAlive) {
        if (e.bullet.x == player.x &&
            e.bullet.y == player.y) {
          e.bullet.destroy();
          player.destroy();
          gameOver();
          return;
        }
      }
    }
  }
}

void drawFrame() {
  for (int i = 0; i < LCD_LINES; i++) {
    for (int j = 0; j < LCD_COLUMNS; j++) {
      if (currentFrame[i][j] != nextFrame[i][j]) {
        lcd.setCursor(j, i);
        lcd.write(nextFrame[i][j]);
        currentFrame[i][j] = nextFrame[i][j];
      }
    }
  }
}

void generateNextFrame() {
  for (int i = 0; i < LCD_LINES; i++) {
    for (int j = 0; j < LCD_COLUMNS; j++) {
      nextFrame[i][j] = ' ';
    }
  }
  for (auto &e : enemyList) {
    if (e.bullet.isAlive) {
      nextFrame[e.bullet.y][e.bullet.x] = Tanks::SHOOT;
    }

    if (e.isAlive) {
      nextFrame[e.y][e.x] = e.direction;
    }
  }

  if (player.bullet.isAlive) {
    nextFrame[player.bullet.y][player.bullet.x] = Tanks::SHOOT;
  }

  nextFrame[player.y][player.x] = player.direction;
}

void loop() {
  if (player.isAlive) {
    readUserInput();
    generateNextFrame();
    drawFrame();
    gameLogic();
  } else {
    if (digitalRead(BTN_SHOOT) == LOW) {
      resetGame();
    }
  };
  delay(100);
}
