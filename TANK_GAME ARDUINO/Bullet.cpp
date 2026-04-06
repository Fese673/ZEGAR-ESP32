#include "Bullet.h"

namespace Bullets {
void Bullet::wallCollisionAvoidance(int8_t xMax, int8_t yMax) {
  if (this->x < 0 || this->x >= xMax ||
      this->y < 0 || this->y >= yMax) {
    this->isAlive = false;
  }
}
}  // namespace Bullets