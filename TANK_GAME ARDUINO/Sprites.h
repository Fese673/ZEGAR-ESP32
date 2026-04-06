#ifndef SPRITES_H
#define SPRITES_H

namespace Sprites {

byte spriteTankUp[] = {
    B00000,
    B00100,
    B00100,
    B11111,
    B11011,
    B10001,
    B00000,
    B00000};

byte spriteTankDown[] = {
    B00000,
    B10001,
    B11111,
    B11111,
    B00100,
    B00100,
    B00000,
    B00000};

byte spriteTankLeft[] = {
    B00000,
    B00111,
    B00110,
    B11110,
    B00110,
    B00111,
    B00000,
    B00000};

byte spriteTankRight[] = {
    B00000,
    B11100,
    B01100,
    B01111,
    B01100,
    B11100,
    B00000,
    B00000};

byte spriteBullet[] = {
    B00000,
    B00000,
    B00000,
    B00100,
    B00000,
    B00000,
    B00000,
    B00000};

}  // namespace Sprites

#endif