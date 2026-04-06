#include "Entity.h"

namespace Game {
void Entity::move(Direction direction) {
  if (this->isAlive) {
    if (this->direction != direction) {
      this->direction = direction;
    } else {
      switch (direction) {
        case UP:
          y--;
          break;
        case DOWN:
          y++;
          break;
        case LEFT:
          x--;
          break;
        case RIGHT:
          x++;
          break;
        default:
          break;
      }
    }
  }
}

void Entity::entityCollisionAvoidance(Entity *otherEntity) {
  if (otherEntity->isAlive) {
    if (this->x == otherEntity->x && this->y == otherEntity->y) {
      switch (this->direction) {
        case UP:
          this->y++;
          break;
        case DOWN:
          this->y--;
          break;
        case LEFT:
          this->x++;
          break;
        case RIGHT:
          this->x--;
          break;
        default:
          break;
      }
    }
  }
}

void Entity::wallCollisionAvoidance(int8_t xMax, int8_t yMax) {
  if (this->x < 0) {
    this->x = 0;
  } else if (this->x >= xMax) {
    this->x -= 1;
  }

  if (this->y < 0) {
    this->y = 0;
  } else if (this->y >= yMax) {
    this->y -= 1;
  }
}

void Entity::destroy() {
  this->isAlive = false;
  this->x = 0;
  this->y = 0;
  this->direction = UP;
}

}  // namespace Game