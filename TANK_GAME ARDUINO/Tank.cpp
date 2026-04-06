#include "Tank.h";

#include <stdlib.h>

#include "Entity.h"

namespace Tanks {
void Tank::shoot() {
  if (this->bullet.isAlive == false) {
    this->bullet.x = this->x;
    this->bullet.y = this->y;
    this->bullet.direction = this->direction;
    this->bullet.isAlive = true;
  }
}

void Tank::destroy() {
  this->Entity::destroy();
  this->bullet.destroy();
}

}  // namespace Tanks
