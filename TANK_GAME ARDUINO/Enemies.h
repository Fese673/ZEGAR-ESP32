#ifndef ENEMIES_H
#define ENEMIES_H

#include <stdint.h>

#include "Tank.h"

namespace Enemies {

class EnemyTank : public Tanks::Tank {
 public:
  void spawn(int8_t xMax, int8_t yMax, Tank &player);
  void tick();
};

}  // namespace Enemies

#endif