#include "Enemies.h"

#include <stdlib.h>

#include "Tank.h"

namespace Enemies {
void EnemyTank::spawn(int8_t maxX, int8_t maxY, Tank &player) {
  if (rand() % 1000 > 950) {
    this->x = rand() % maxX;
    this->y = rand() % maxY;
    this->direction = (Game::Direction)(rand() % 4);
    this->isAlive = true;

    if (this->x == player.x && this->y == player.y) {
      if (this->x >= maxX) {
        this->x--;
      } else {
        this->x++;
      }
    }
  }
}

void EnemyTank::tick() {
  if (this->isAlive) {
    int8_t action = rand() % 60;
    switch (action) {
      case Tanks::MOVE_UP:
        this->move(Game::UP);
        break;
      case Tanks::MOVE_DOWN:
        this->move(Game::DOWN);
        break;
      case Tanks::MOVE_LEFT:
        this->move(Game::LEFT);
        break;
      case Tanks::MOVE_RIGHT:
        this->move(Game::RIGHT);
        break;
      case Tanks::SHOOT:
        this->shoot();
        break;
      default:
        break;
    }
  }
}

}  // namespace Enemies
