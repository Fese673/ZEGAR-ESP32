#ifndef TANK_H
#define TANK_H

#include "Bullet.h"
#include "Entity.h"

namespace Tanks {
class Tank : public Game::Entity {
 public:
  Bullets::Bullet bullet;

  void shoot();
  void destroy();
};

enum TankAction : uint8_t {
  MOVE_UP,
  MOVE_DOWN,
  MOVE_LEFT,
  MOVE_RIGHT,
  SHOOT
};

}  // namespace Tanks

#endif