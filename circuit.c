/*

  circuit.c

  Copyright 2017-2020 Matthew T. Pandina. All rights reserved.

  This file is part of Circuit Puzzle.

  Circuit Puzzle is free software: you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  Circuit Puzzle is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Circuit Puzzle.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdint.h>
#include <stdbool.h>
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>
#include <avr/pgmspace.h>
#include <uzebox.h>
#include <avr/interrupt.h>

#include "data/tileset.inc"
#include "data/sprites.inc"
#include "data/titlescreen.inc"
#include "data/patches.inc"
#include "data/mainsong.h"
#include "data/win_tileset.inc"

//#define OPTION_PERSIST_MUSIC_PREF
//#define OPTION_HIDE_CURSOR_DURING_LEVEL_COMPLETE
#define OPTION_HIDE_CURSOR_DURING_DRAG_AND_DROP
//#define OPTION_DEBUG_NETLIST
//#define OPTION_DEBUG_NETLIST_MATRIX
//#define OPTION_DEBUG_DISPLAY_PRUNED_BOARD
//#define OPTION_DEBUG_DISPLAY_GOAL_STATES
//#define OPTION_DEBUG_EPIC_WIN

#define EEPROM_ID 0x0400
#define EEPROM_SAVEGAME_VERSION 0x0001

struct EEPROM_SAVEGAME;
typedef struct EEPROM_SAVEGAME EEPROM_SAVEGAME;

struct EEPROM_SAVEGAME {
  uint16_t id;
  uint16_t version;
  uint8_t bits[8];
  char reserved[32 - 8];
} __attribute__ ((packed));

static void LoadHighScore(uint8_t* const bits)
{
  EEPROM_SAVEGAME save = {0};
  uint8_t retval = EepromReadBlock(EEPROM_ID, (struct EepromBlockStruct*)&save);
  if (retval == EEPROM_ERROR_BLOCK_NOT_FOUND || save.version == 0xFFFF) {
    save.id = EEPROM_ID;
    save.version = EEPROM_SAVEGAME_VERSION;
    for (uint8_t i = 0; i < 8; ++i)
      save.bits[i] = 0;
    EepromWriteBlock((struct EepromBlockStruct*)&save);
  }
  memcpy(bits, save.bits, 8);
}

static void SaveHighScore(const uint8_t* bits)
{
  EEPROM_SAVEGAME save = {0};
  save.id = EEPROM_ID;
  save.version = EEPROM_SAVEGAME_VERSION;
  memcpy(save.bits, bits, 8);
  EepromWriteBlock((struct EepromBlockStruct*)&save);
}

// we need to store 61 bits, 0 = music on/off, 1-60 = level passed
uint8_t bitarray[8];

void BitArray_setBit(uint8_t index)
{
  uint8_t offset = index / 8;
  uint8_t mask = 1 << (index % 8);
  bitarray[offset] |= mask;
}

void BitArray_clearBit(uint8_t index)
{
  uint8_t offset = index / 8;
  uint8_t mask = 1 << (index % 8);
  bitarray[offset] &= mask ^ (uint8_t)0xFF;
}

bool BitArray_readBit(uint8_t index)
{
  uint8_t offset = index / 8;
  uint8_t mask = 1 << (index % 8);
  return (bool)(bitarray[offset] & mask);
}

bool CompletedGame(void)
{
  return ((bitarray[0] & 0xFE) == 0xFE && bitarray[1] == 0xFF && bitarray[2] == 0xFF && bitarray[3] == 0xFF &&
          bitarray[4] == 0xFF && bitarray[5] == 0xFF && bitarray[6] == 0xFF && (bitarray[7] & 0x1F) == 0x1F);
}

void MyStopSong()
{
  StopSong();
#if defined(OPTION_PERSIST_MUSIC_PREF)
  BitArray_setBit(0);
  SaveHighScore(bitarray);
#endif
}

void MyResumeSong()
{
  ResumeSong();
#if defined(OPTION_PERSIST_MUSIC_PREF)
  BitArray_clearBit(0);
  SaveHighScore(bitarray);
#endif
}

#define UZEMH _SFR_IO8(25)
#define UZEMC _SFR_IO8(26)

#define TOKEN_WIDTH 3
#define TOKEN_HEIGHT 3
#define BOARD_WIDTH 5
#define BOARD_HEIGHT 5
#define BOARD_START_X 1
#define BOARD_START_Y 1
#define BOARD_H_SPACING 3
#define BOARD_V_SPACING 3
#define GOAL_WIDTH 4
#define GOAL_HEIGHT 3
#define GOAL_START_X 19
#define GOAL_START_Y 3
#define GOAL_H_SPACING 3
#define GOAL_V_SPACING 4
#define HAND_WIDTH 5
#define HAND_HEIGHT 2
#define HAND_START_X 1
#define HAND_START_Y 20
#define HAND_H_SPACING 4
#define HAND_V_SPACING 4
#define CONTROLS_LR_START_X (GOAL_START_X + 3)
#define CONTROLS_LR_START_Y (HAND_START_Y + 2)
#define CONTROLS_DPAD_START_X CONTROLS_LR_START_X
#define CONTROLS_DPAD_START_Y (CONTROLS_LR_START_Y + 2)
#define CONTROLS_A_START_X (CONTROLS_LR_START_X + 6)
#define CONTROLS_A_START_Y (CONTROLS_LR_START_Y + 2)
#define CONTROLS_B_START_X (CONTROLS_LR_START_X + 4)
#define CONTROLS_B_START_Y (CONTROLS_LR_START_Y + 3)

#define GAME_USER_RAM_TILES_COUNT 3
#define OVERLAY_SPRITE_START 0

#define PIECE_MASK   0x3F
#define FLAGS_MASK   0xC0
#define FLAG_ROTATE  0x40
#define FLAG_LOCKED  0x80

bool boardChanged = false;
bool switchChanged = false;
bool startAdvancesLevel = false;
bool startWinsGame = false;

void BoardChanged(void);
uint8_t GetLevelColor(uint8_t level);
void RamFont_Load2Digits(const uint8_t* ramfont, uint8_t ramfont_index, uint8_t number, uint8_t fg_color, uint8_t bg_color);

typedef struct {
  uint16_t held;
  uint16_t prev;
  uint16_t pressed;
  uint16_t released;
} __attribute__ ((packed)) BUTTON_INFO;

#define TILE_BACKGROUND 0
#define TILE_FOREGROUND 1
#define TILE_SHORT_VCC 2
#define TILE_SHORT_GND 3
#define TILE_SHORT_GND_ROT 4
#define TILE_VCC 5
#define TILE_GND 6
#define TILE_GND_ROT 7
#define TILE_BREADBOARD_TOP 8
#define TILE_BREADBOARD_BOTTOM 9
#define TILE_GOAL_MET 10
#define TILE_GOAL_UNMET 11
#define TILE_DPAD_LEFT 12
#define TILE_DPAD_RIGHT 13

// Defines for the pieces. Rotations are treated as different pieces.
#define P_BLANK 0

#define P_VCC_T 1
#define P_VCC_R 2
#define P_VCC_B 3
#define P_VCC_L 4

#define P_GND_LTR 5
#define P_GND_TRB 6
#define P_GND_RBL 7
#define P_GND_BLT 8

#define P_SW1_BL 9
#define P_SW1_LT 10
#define P_SW1_TR 11
#define P_SW1_RB 12

#define P_RLED_AB_CR 13
#define P_RLED_AL_CB 14
#define P_RLED_AT_CL 15
#define P_RLED_AR_CT 16

#define P_SW2_BT 17
#define P_SW2_LR 18
#define P_SW2_TB 19
#define P_SW2_RL 20

#define P_YLED_AL_CR 21
#define P_YLED_AT_CB 22
#define P_YLED_AR_CL 23
#define P_YLED_AB_CT 24

#define P_SW3_BR 25
#define P_SW3_LB 26
#define P_SW3_TL 27
#define P_SW3_RT 28

#define P_GLED_AB_CL 29
#define P_GLED_AL_CT 30
#define P_GLED_AT_CR 31
#define P_GLED_AR_CB 32

#define P_STRAIGHT_LR 33
#define P_STRAIGHT_TB 34

#define P_DBL_CORNER_TL_BR 35
#define P_DBL_CORNER_TR_BL 36

#define P_CORNER_BL 37
#define P_CORNER_TL 38
#define P_CORNER_TR 39
#define P_CORNER_BR 40

#define P_TPIECE_RBL 41
#define P_TPIECE_BLT 42
#define P_TPIECE_LTR 43
#define P_TPIECE_TRB 44

#define P_BRIDGE1_TB_LR 45
#define P_BRIDGE2_TB_LR 46

#define P_BLOCKER 47

// Unknown rotations (these only exist in the level definitions)
#define P_VCC_U 48
#define P_GND_U 49
#define P_SW1_U 50
#define P_RLED_U 51
#define P_SW2_U 52
#define P_YLED_U 53
#define P_SW3_U 54
#define P_GLED_U 55
#define P_STRAIGHT_U 56
#define P_DBL_CORNER_U 57
#define P_CORNER_U 58
#define P_TPIECE_U 59
#define P_BRIDGE_U 60


// Defines for the goals. There are no rotations.
#define P_GOAL_BLANK 0
#define P_GOAL_RLED_OFF 1
#define P_GOAL_YLED_OFF 2
#define P_GOAL_GLED_OFF 3

#define P_GOAL_RLED_ON 4
#define P_GOAL_YLED_ON 5
#define P_GOAL_GLED_ON 6

#define P_GOAL_SW1 7
#define P_GOAL_SW2 8
#define P_GOAL_SW3 9

////////////////////////////////////////////////////////////////////////
// For "mouse" acceleration

// Defined screen lower and upper boundaries
#define X_LB (TOKEN_WIDTH * TILE_WIDTH / 2)
#define X_UB (((SCREEN_TILES_H) * TILE_WIDTH) - (TOKEN_WIDTH * TILE_WIDTH / 2))
#define Y_LB (TOKEN_HEIGHT * TILE_HEIGHT / 2)
#define Y_UB (((SCREEN_TILES_V) * TILE_HEIGHT) - (TOKEN_HEIGHT * TILE_HEIGHT / 2))

// 1/32th of a second per frame (not really, but it makes the math faster, and the constants below have been adjusted to compensate)
#define CURSOR_FRAMES_PER_SECOND 32
#define CURSOR_FIXED_POINT_SHIFT 2
#define CURSOR_MAX_VELOCITY (64 << CURSOR_FIXED_POINT_SHIFT)
#define CURSOR_ACCELERATION 655
#define CURSOR_FRICTION 2800

// Converts from internal fixed point representation into screen pixels
#define CURSOR_NEAREST_SCREEN_PIXEL(p) (((p) + (1 << (CURSOR_FIXED_POINT_SHIFT - 1))) >> CURSOR_FIXED_POINT_SHIFT)

struct CURSOR;
typedef struct CURSOR CURSOR;

struct CURSOR {
  uint8_t tag;
  int16_t x;
  int16_t y;
  int16_t dx;
  int16_t dy;
} __attribute__ ((packed));

CURSOR cursor;

void cursor_init(CURSOR* const c, const uint8_t tag, const uint8_t tileIndex, const uint8_t x, const uint8_t y)
{
  c->tag = tag;
  sprites[tag].tileIndex = tileIndex;
  sprites[tag].x = x;
  sprites[tag].y = y;
  c->x = (int16_t)x << CURSOR_FIXED_POINT_SHIFT;
  c->y = (int16_t)y << CURSOR_FIXED_POINT_SHIFT;
  c->dx = c->dy = 0;
}

__attribute__((optimize("O3")))
void cursor_update(CURSOR* c, uint16_t held)
{
  bool wasLeft = (c->dx < 0);
  bool wasRight = (c->dx > 0);

  int16_t ddx = 0;

  if (held & BTN_LEFT)
    ddx -= CURSOR_ACCELERATION;    // cursor wants to go left
  else if (wasLeft)
    ddx += CURSOR_FRICTION; // cursor was going left, but not anymore

  if (held & BTN_RIGHT)
    ddx += CURSOR_ACCELERATION;    // cursor wants to go right
  else if (wasRight)
    ddx -= CURSOR_FRICTION; // cursor was going right, but not anymore

  if ((held & BTN_LEFT) || (held & BTN_RIGHT) || wasLeft || wasRight) { // smaller and faster than 'if (ddx)'
    // Integrate the X forces to calculate the new position (x,y) and the new velocity (dx,dy)
    c->x += (c->dx / CURSOR_FRAMES_PER_SECOND);
    c->dx += (ddx / CURSOR_FRAMES_PER_SECOND);
    if (c->dx < -CURSOR_MAX_VELOCITY)
      c->dx = -CURSOR_MAX_VELOCITY;
    else if (c->dx > CURSOR_MAX_VELOCITY)
      c->dx = CURSOR_MAX_VELOCITY;

    // Clamp horizontal velocity to zero if we detect that the cursor's direction has changed
    if ((wasLeft && (c->dx > 0)) || (wasRight && (c->dx < 0)))
      c->dx = 0; // clamp at zero to prevent friction from making the cursor jiggle side to side

    // Clamp X to within defined screen bounds
    if (c->x > (X_UB << CURSOR_FIXED_POINT_SHIFT)) {
      c->x = (X_UB << CURSOR_FIXED_POINT_SHIFT);
      c->dx = 0;
    } else if (c->x < (X_LB << CURSOR_FIXED_POINT_SHIFT)) {
      c->x = X_LB << CURSOR_FIXED_POINT_SHIFT;
      c->dx = 0;
    }
  }
  bool wasUp = (c->dy < 0);
  bool wasDown = (c->dy > 0);

  int16_t ddy = 0;

  if (held & BTN_UP)
    ddy -= CURSOR_ACCELERATION;    // cursor wants to go up
  else if (wasUp)
    ddy += CURSOR_FRICTION; // cursor was going up, but not anymore

  if (held & BTN_DOWN)
    ddy += CURSOR_ACCELERATION;    // cursor wants to go down
  else if (wasDown)
    ddy -= CURSOR_FRICTION; // cursor was going down, but not anymore

  if (held & BTN_UP || held & BTN_DOWN || wasUp || wasDown) { // smaller and faster than 'if (ddy)'
    // Integrate the Y forces to calculate the new position (x,y) and the new velocity (dx,dy)
    c->y += (c->dy / CURSOR_FRAMES_PER_SECOND);
    c->dy += (ddy / CURSOR_FRAMES_PER_SECOND);
    if (c->dy < -CURSOR_MAX_VELOCITY)
      c->dy = -CURSOR_MAX_VELOCITY;
    else if (c->dy > CURSOR_MAX_VELOCITY)
      c->dy = CURSOR_MAX_VELOCITY;

    // Clamp vertical velocity to zero if we detect that the cursor's direction has changed
    if ((wasUp && (c->dy > 0)) || (wasDown && (c->dy < 0)))
      c->dy = 0; // clamp at zero to prevent friction from making the cursor jiggle up and down

    // Clamp Y to within defined screen bounds
    if (c->y > (Y_UB << CURSOR_FIXED_POINT_SHIFT)) {
      c->y = Y_UB << CURSOR_FIXED_POINT_SHIFT;
      c->dy = 0;
    } else if (c->y < (Y_LB << CURSOR_FIXED_POINT_SHIFT)) {
      c->y = Y_LB << CURSOR_FIXED_POINT_SHIFT;
      c->dy = 0;
    }
  }

  // Set the cursor sprite to the screen pixel location that corresponds to our fixed point representation of x and y
  sprites[c->tag].x = CURSOR_NEAREST_SCREEN_PIXEL(c->x);
  sprites[c->tag].y = CURSOR_NEAREST_SCREEN_PIXEL(c->y);
}
////////////////////////////////////////////////////////////////////////

// We need to know the position of the X or checkbox when LoadLevel is called in order to update it in BoardChanged
uint8_t meetsRulesY = 0;
uint8_t currentLevel;

// XXX - Modify these arrays to use the #defines instead of hardcoded numbers

// The configuration of the playing board
uint8_t board[5][5] = {
  {  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0 },
};

// The pieces in the goal
uint8_t goal[3][4] = {
  { 0, 0, 0, 0 },
  { 0, 0, 0, 0 },
  { 0, 0, 0, 0 },
};

// To know when all 3 goals have been met when there is a switch in the level
bool met_goal[3] = { 0, 0, 0 };

// The pieces in your "hand" (that need to be placed on the board)
uint8_t hand[2][5] = {
  { 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0 },
};

// Used to prune pieces with loose ends before netlist generation
uint8_t pruned_board[5][5] = {
  {  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0 },
};

/* Each square may have an electron going in and/or out in any direction
      IN   OUT
   0b 0000 0000
       \\\\ \\\\__ top
        \\\\ \\\__ right
         \\\\ \\__ bottom
          \\\\ \__ left
           \\\\
            \\\\__ top
             \\\__ right
              \\__ bottom
               \__ left
*/
#define D_OUT_T 1
#define D_OUT_R 2
#define D_OUT_B 4
#define D_OUT_L 8

#define D_IN_T 16
#define D_IN_R 32
#define D_IN_B 64
#define D_IN_L 128

// The bitmap of where the electron is, and which direction(s) it is travelling
/* uint8_t directions[5][5] = { */
/*   {  0,  0,  0,  0,  0 }, */
/*   {  0,  0,  0,  0,  0 }, */
/*   {  0,  0,  0,  0,  0 }, */
/*   {  0,  0,  0,  0,  0 }, */
/*   {  0,  0,  0,  0,  0 }, */
/* }; */


#define NL_VV 0
#define NL_00 1
#define NL_RA 2
#define NL_RC 3
#define NL_YA 4
#define NL_YC 5
#define NL_GA 6
#define NL_GC 7

// Used to store the netlist (can be made more efficient later)
uint8_t pruned_netlist[8][8] = {
  { 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0 },
};

const uint8_t levelData[] PROGMEM = {
  // LEVEL 01
  // Puzzle
  P_VCC_B, 0, 0, 0, 0,
  0, P_YLED_AL_CR, 0, 0, 0,
  0, 0, P_STRAIGHT_TB, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_YLED_ON, 0, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_CORNER_U, P_GND_U, 0, 0,
  0, 0, 0, 0, 0,

  // LEVEL 02
  // Puzzle
  0, 0, 0, 0, 0,
  0, P_VCC_B, P_CORNER_BR, P_CORNER_BL, 0,
  0, 0, P_BRIDGE2_TB_LR, P_CORNER_TL, 0,
  0, P_GND_RBL, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_GLED_ON, 0, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_GLED_U, 0, 0, 0,
  0, 0, 0, 0, 0,

  // LEVEL 03
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, P_CORNER_BL, 0, 0,
  0, P_VCC_T, 0, P_CORNER_TL, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_GLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_RLED_U, P_GLED_U, P_GND_U, 0, 0,
  0, 0, 0, 0, 0,

  // LEVEL 04
  // Puzzle
  0, 0, 0, 0, 0,
  0, P_CORNER_BR, 0, P_CORNER_BL, 0,
  0, 0, P_TPIECE_RBL, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_GLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_RLED_U, P_GLED_U, P_VCC_U, P_GND_U, 0,
  0, 0, 0, 0, 0,

  // LEVEL 05
  // Puzzle
  0, 0, 0, 0, 0,
  P_VCC_R, P_TPIECE_RBL, P_TPIECE_RBL, 0, 0,
  0, 0, P_GLED_AT_CR, 0, 0,
  0, 0, P_STRAIGHT_LR, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_CORNER_U, P_RLED_U, P_YLED_U, P_GND_U,
  0, 0, 0, 0, 0,

  // LEVEL 06
  // Puzzle
  0, P_GND_LTR, 0, P_VCC_B, 0,
  0, 0, 0, 0, P_GLED_AB_CL,
  0, P_BLOCKER, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_GLED_ON, 0, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_BRIDGE_U, P_CORNER_U, P_CORNER_U, P_CORNER_U, P_CORNER_U,
  0, 0, 0, 0, 0,

  // LEVEL 07
  // Puzzle
  0, P_RLED_AB_CR, 0, P_GLED_AB_CL, 0,
  0, 0, 0, 0, 0,
  0, 0, P_SW2_BT, 0, 0,
  0, 0, P_VCC_T, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_ON, P_GOAL_YLED_OFF, P_GOAL_GLED_OFF,
  P_GOAL_SW2, P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  P_GOAL_SW3, P_GOAL_RLED_OFF, P_GOAL_YLED_OFF, P_GOAL_GLED_ON,
  // Hand
  P_STRAIGHT_U, P_STRAIGHT_U, P_CORNER_U, P_CORNER_U, P_YLED_U,
  P_GND_U, 0, 0, 0, 0,

  // LEVEL 08
  // Puzzle
  P_GLED_AR_CB, 0, P_VCC_L, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  P_GND_BLT, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_TPIECE_U, P_TPIECE_U, P_RLED_U, P_YLED_U, 0,
  0, 0, 0, 0, 0,

  // LEVEL 09
  // Puzzle
  0, 0, 0, P_RLED_AL_CB, 0,
  0, 0, P_GLED_AT_CR, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_GLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_CORNER_U, P_TPIECE_U, P_VCC_U, P_GND_U,
  0, 0, 0, 0, 0,

  // LEVEL 10
  // Puzzle
  0, 0, 0, 0, 0,
  0, P_VCC_R, P_CORNER_BL, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, P_GLED_AT_CR, 0, 0,
  // Goal
  P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_TPIECE_U, P_STRAIGHT_U, P_YLED_AB_CT, P_GND_U,
  0, 0, 0, 0, 0,

  // LEVEL 11
  // Puzzle
  0, 0, 0, P_STRAIGHT_LR, 0,
  0, P_TPIECE_TRB, 0, 0, 0,
  0, 0, P_GND_BLT, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_CORNER_U, P_YLED_U, P_GLED_U, P_VCC_U,
  0, 0, 0, 0, 0,

  // LEVEL 12
  // Puzzle
  0, 0, P_BLOCKER, 0, 0,
  P_STRAIGHT_TB, 0, 0, 0, 0,
  0, 0, P_CORNER_BL, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_TPIECE_U, P_YLED_U, P_GLED_U, P_VCC_U,
  P_GND_U, 0, 0, 0, 0,

  // LEVEL 13
  // Puzzle
  0, 0, P_GLED_AR_CB, 0, 0,
  0, 0, 0, P_YLED_AR_CL, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_STRAIGHT_U, P_TPIECE_U, P_CORNER_U, P_CORNER_U, P_VCC_U,
  P_GND_U, 0, 0, 0, 0,

  // LEVEL 14
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, P_CORNER_BL, 0,
  0, P_SW2_LR, 0, P_GND_U, 0,
  0, P_CORNER_TR, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_ON, P_GOAL_YLED_OFF, P_GOAL_GLED_OFF,
  P_GOAL_SW2, P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  P_GOAL_SW3, P_GOAL_RLED_OFF, P_GOAL_YLED_OFF, P_GOAL_GLED_ON,
  // Hand
  P_STRAIGHT_U, P_STRAIGHT_U, P_RLED_U, P_YLED_U, P_GLED_U,
  P_VCC_U, 0, 0, 0, 0,

  // LEVEL 15
  // Puzzle
  0, 0, P_VCC_B, 0, 0,
  0, P_GLED_U, 0, 0, 0,
  0, 0, 0, 0, 0,
  P_GND_TRB, P_YLED_AR_CL, P_TPIECE_BLT, 0, 0,
  P_CORNER_TR, P_STRAIGHT_LR, P_RLED_AT_CL, 0, 0,
  // Goal
  P_GOAL_RLED_OFF, P_GOAL_YLED_OFF, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_TPIECE_U, P_CORNER_U, P_CORNER_U, P_CORNER_U, 0,
  0, 0, 0, 0, 0,

  // LEVEL 16
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  P_SW2_BT, P_BRIDGE1_TB_LR, P_RLED_AL_CB, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_OFF, P_GOAL_YLED_OFF, P_GOAL_GLED_OFF,
  P_GOAL_SW2, P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_ON,
  P_GOAL_SW3, P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  // Hand
  P_CORNER_U, P_CORNER_U, P_YLED_U, P_GLED_U, P_VCC_U,
  P_GND_U, 0, 0, 0, 0,

  // LEVEL 17
  // Puzzle
  0, 0, 0, P_YLED_U, P_CORNER_BL,
  0, P_BRIDGE1_TB_LR, 0, P_BLOCKER, 0,
  P_CORNER_TR, P_GND_LTR, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_TPIECE_U, P_CORNER_U, P_RLED_U, P_GLED_U, P_VCC_U,
  0, 0, 0, 0, 0,

  // LEVEL 18
  // Puzzle
  P_BLOCKER, 0, P_GND_TRB, 0, 0,
  0, 0, 0, 0, 0,
  0, P_SW2_LR, 0, 0, 0,
  0, 0, P_GLED_U, 0, 0,
  P_VCC_T, 0, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  P_GOAL_SW2, P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  P_GOAL_SW3, P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_ON,
  // Hand
  P_TPIECE_U, P_TPIECE_U, P_CORNER_U, P_CORNER_U, P_RLED_U,
  P_YLED_U, 0, 0, 0, 0,

  // LEVEL 19
  // Puzzle
  0, 0, P_GLED_AB_CL, 0, 0,
  0, P_GND_BLT, 0, P_RLED_U, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, P_VCC_T, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_TPIECE_U, P_BRIDGE_U, P_CORNER_U, P_CORNER_U, P_CORNER_U,
  P_YLED_U, 0, 0, 0, 0,

  // LEVEL 20
  // Puzzle
  0, 0, 0, 0, 0,
  P_TPIECE_TRB, P_CORNER_BL, 0, 0, 0,
  0, P_DBL_CORNER_U, 0, P_CORNER_BL, 0,
  0, P_GLED_U, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_GLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_STRAIGHT_U, P_RLED_U, P_VCC_U, P_GND_U,
  0, 0, 0, 0, 0,

  // LEVEL 21
  // Puzzle
  0, 0, 0, 0, 0,
  0, P_GLED_AR_CB, P_YLED_AR_CL, 0, 0,
  0, P_CORNER_U, 0, P_CORNER_U, P_VCC_U,
  0, 0, 0, 0, 0,
  0, P_GND_BLT, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_OFF, P_GOAL_GLED_OFF, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_CORNER_U, P_TPIECE_U, P_TPIECE_U, P_RLED_U,
  0, 0, 0, 0, 0,

  // LEVEL 22
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, P_CORNER_BL, 0, 0, 0,
  0, 0, 0, P_GND_RBL, 0,
  0, 0, P_STRAIGHT_LR, P_VCC_L, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_TPIECE_U, P_TPIECE_U, P_BRIDGE_U, P_RLED_AB_CR, P_YLED_AB_CT,
  P_GLED_AB_CL, 0, 0, 0, 0,

  // LEVEL 23
  // Puzzle
  0, 0, P_GND_TRB, 0, P_VCC_B,
  0, 0, 0, P_GLED_AR_CB, 0,
  0, 0, P_TPIECE_U, 0, 0,
  0, 0, P_CORNER_TR, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_OFF, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_TPIECE_U, P_BRIDGE_U, P_RLED_U, P_YLED_U,
  0, 0, 0, 0, 0,

  // LEVEL 24
  // Puzzle
  0, 0, 0, 0, 0,
  0, P_GND_LTR, 0, 0, 0,
  0, 0, P_DBL_CORNER_TL_BR, 0, 0,
  P_CORNER_TR, 0, 0, P_CORNER_U, 0,
  0, 0, P_VCC_U, 0, 0,
  // Goal
  P_GOAL_RLED_ON, 0, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_STRAIGHT_U, P_TPIECE_U, P_BRIDGE_U, P_CORNER_U, P_CORNER_U,
  P_CORNER_U, P_RLED_U, 0, 0, 0,

  // LEVEL 25
  // Puzzle
  0, 0, P_CORNER_BL, 0, 0,
  P_VCC_B, P_YLED_U, P_RLED_U, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_STRAIGHT_U, P_TPIECE_U, P_CORNER_U, P_CORNER_U, P_CORNER_U,
  P_GND_U, 0, 0, 0, 0,

  // LEVEL 26
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, P_GND_TRB, 0,
  P_RLED_AB_CR, 0, 0, P_TPIECE_TRB, 0,
  0, P_DBL_CORNER_U, P_STRAIGHT_U, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_GLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_CORNER_U, P_CORNER_U, P_CORNER_U, P_GLED_U,
  P_VCC_U, 0, 0, 0, 0,

  // LEVEL 27
  // Puzzle
  0, 0, P_CORNER_BR, 0, 0,
  0, P_CORNER_U, P_SW2_BT, P_TPIECE_U, 0,
  P_GND_RBL, 0, 0, 0, 0,
  0, 0, 0, P_VCC_U, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_ON, 0, 0,
  P_GOAL_SW2, P_GOAL_RLED_ON, 0, 0,
  P_GOAL_SW3, P_GOAL_RLED_ON, 0, 0,
  // Hand
  P_BRIDGE_U, P_CORNER_U, P_CORNER_U, P_TPIECE_U, P_RLED_U,
  0, 0, 0, 0, 0,

  // LEVEL 28
  // Puzzle
  0, 0, P_GLED_AR_CB, 0, 0,
  0, 0, 0, P_STRAIGHT_TB, 0,
  0, 0, 0, P_DBL_CORNER_U, 0,
  0, P_GND_RBL, 0, P_CORNER_U, P_YLED_AB_CT,
  0, 0, P_BLOCKER, 0, 0,
  // Goal
  P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_STRAIGHT_U, P_CORNER_U, P_CORNER_U, P_CORNER_U, P_TPIECE_U,
  P_TPIECE_U, P_VCC_U, 0, 0, 0,

  // LEVEL 29
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, P_GLED_AR_CB, 0,
  0, 0, 0, 0, 0,
  0, P_RLED_AR_CT, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_STRAIGHT_U, P_TPIECE_U, P_YLED_U, P_VCC_U,
  P_GND_U, 0, 0, 0, 0,

  // LEVEL 30
  // Puzzle
  0, P_GND_TRB, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, P_GLED_AT_CR, 0, P_BLOCKER,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_TPIECE_U, P_BRIDGE_U, P_CORNER_U, P_CORNER_U, P_RLED_U,
  P_YLED_U, P_VCC_U, 0, 0, 0,

  // LEVEL 31
  // Puzzle
  0, 0, 0, 0, 0,
  0, P_STRAIGHT_U, 0, P_VCC_T, 0,
  0, 0, P_BLOCKER, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_TPIECE_U, P_RLED_U, P_YLED_U, P_GLED_U,
  P_GND_U, 0, 0, 0, 0,

  // LEVEL 32
  // Puzzle
  0, 0, 0, 0, 0,
  P_VCC_U, 0, P_CORNER_U, 0, 0,
  0, 0, 0, 0, P_BLOCKER,
  0, 0, P_CORNER_U, 0, P_CORNER_BL,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_GLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_STRAIGHT_U, P_TPIECE_U, P_DBL_CORNER_U, P_RLED_U, P_GLED_U,
  P_GND_U, 0, 0, 0, 0,

  // LEVEL 33
  // Puzzle
  0, 0, P_GND_LTR, 0, 0,
  P_TPIECE_TRB, 0, 0, 0, 0,
  P_STRAIGHT_TB, 0, P_BLOCKER, 0, 0,
  P_GLED_AT_CR, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_TPIECE_U, P_BRIDGE_U, P_CORNER_U, P_RLED_U, P_YLED_U,
  P_VCC_U, 0, 0, 0, 0,

  // LEVEL 34
  // Puzzle
  0, P_BLOCKER, P_CORNER_BR, 0, P_VCC_U,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, P_CORNER_TR, P_STRAIGHT_LR, P_CORNER_TL, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_TPIECE_U, P_TPIECE_U, P_BRIDGE_U, P_RLED_U, P_YLED_U,
  P_GLED_U, P_GND_U, 0, 0, 0,

  // LEVEL 35
  // Puzzle
  0, 0, P_TPIECE_RBL, P_STRAIGHT_LR, 0,
  0, 0, 0, 0, P_VCC_U,
  P_GND_U, 0, P_BLOCKER, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_BRIDGE_U, P_CORNER_U, P_CORNER_U, P_CORNER_U, P_CORNER_U,
  P_CORNER_U, P_RLED_U, P_YLED_U, 0, 0,

  // LEVEL 36
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  P_GLED_U, 0, 0, 0, 0,
  0, 0, P_GND_RBL, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_GLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_CORNER_U, P_TPIECE_U, P_RLED_U, P_VCC_U,
  0, 0, 0, 0, 0,

  // LEVEL 37
  // Puzzle
  0, 0, P_BLOCKER, 0, 0,
  0, 0, P_CORNER_U, 0, 0,
  0, 0, P_GND_TRB, P_CORNER_U, P_YLED_AB_CT,
  0, 0, 0, P_TPIECE_U, P_CORNER_TL,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_OFF, P_GOAL_GLED_OFF, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_TPIECE_U, P_RLED_U, P_GLED_U, P_VCC_U,
  0, 0, 0, 0, 0,

  // LEVEL 38
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, P_VCC_R, 0, 0,
  0, 0, 0, 0, P_RLED_U,
  P_GND_U, P_YLED_U, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  P_GOAL_SW2, P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  P_GOAL_SW3, P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_ON,
  // Hand
  P_CORNER_U, P_CORNER_U, P_TPIECE_U, P_TPIECE_U, P_GLED_U,
  P_SW2_U, 0, 0, 0, 0,

  // LEVEL 39
  // Puzzle
  0, 0, P_CORNER_U, 0, 0,
  0, P_STRAIGHT_U, 0, 0, 0,
  P_CORNER_U, 0, 0, 0, 0,
  0, 0, P_CORNER_TR, P_VCC_L, 0,
  P_GLED_AT_CR, P_GND_RBL, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_DBL_CORNER_U, P_CORNER_U, P_TPIECE_U, P_TPIECE_U, P_RLED_U,
  P_YLED_U, 0, 0, 0, 0,

  // LEVEL 40
  // Puzzle
  0, P_CORNER_U, 0, P_VCC_U, 0,
  0, 0, 0, 0, 0,
  0, 0, P_BRIDGE1_TB_LR, 0, 0,
  0, 0, P_SW2_BT, P_CORNER_U, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_OFF, P_GOAL_YLED_OFF, P_GOAL_GLED_OFF,
  P_GOAL_SW2, P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  P_GOAL_SW3, P_GOAL_RLED_ON, P_GOAL_YLED_OFF, P_GOAL_GLED_ON,
  // Hand
  P_STRAIGHT_U, P_TPIECE_U, P_RLED_U, P_YLED_U, P_GLED_U,
  P_GND_U, 0, 0, 0, 0,

  // LEVEL 41
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, P_RLED_U, 0, P_CORNER_U,
  0, 0, P_DBL_CORNER_U, 0, 0,
  0, 0, P_GLED_U, P_GND_U, P_STRAIGHT_TB,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_GLED_OFF, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_BRIDGE_U, P_TPIECE_U, P_TPIECE_U, P_CORNER_U, P_CORNER_U,
  P_VCC_U, 0, 0, 0, 0,

  // LEVEL 42
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, P_RLED_AR_CT, 0, 0,
  0, 0, 0, 0, 0,
  0, P_TPIECE_LTR, 0, P_GLED_U, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_STRAIGHT_U, P_TPIECE_U, P_CORNER_U, P_CORNER_U, P_YLED_U,
  P_VCC_U, P_GND_U, 0, 0, 0,

  // LEVEL 43
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, P_BLOCKER, 0, 0,
  0, 0, 0, 0, 0,
  0, P_YLED_U, 0, 0, 0,
  0, 0, P_CORNER_U, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_OFF, P_GOAL_YLED_OFF, P_GOAL_GLED_OFF,
  P_GOAL_SW2, P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_ON,
  P_GOAL_SW3, P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  // Hand
  P_BRIDGE_U, P_CORNER_U, P_RLED_U, P_GLED_U, P_VCC_U,
  P_GND_U, P_SW2_U, 0, 0, 0,

  // LEVEL 44
  // Puzzle
  0, 0, 0, 0, 0,
  0, P_TPIECE_LTR, 0, 0, 0,
  P_GND_LTR, 0, P_YLED_AB_CT, 0, 0,
  0, 0, 0, P_TPIECE_U, 0,
  0, 0, 0, 0, P_VCC_U,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_OFF, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_STRAIGHT_U, P_BRIDGE_U, P_CORNER_U, P_CORNER_U, P_CORNER_U,
  P_CORNER_U, P_RLED_U, P_GLED_U, 0, 0,

  // LEVEL 45
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  P_CORNER_BR, 0, P_BRIDGE1_TB_LR, P_CORNER_U, P_VCC_U,
  0, P_GND_BLT, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_ON, P_GOAL_GLED_OFF, 0,
  P_GOAL_SW2, P_GOAL_RLED_ON, P_GOAL_GLED_ON, 0,
  P_GOAL_SW3, P_GOAL_RLED_OFF, P_GOAL_GLED_OFF, 0,
  // Hand
  P_TPIECE_U, P_CORNER_U, P_CORNER_U, P_CORNER_U, P_RLED_U,
  P_GLED_U, P_SW2_U, 0, 0, 0,

  // LEVEL 46
  // Puzzle
  P_VCC_R, P_TPIECE_RBL, 0, 0, 0,
  P_CORNER_BR, P_TPIECE_BLT, 0, 0, 0,
  0, 0, P_DBL_CORNER_U, P_CORNER_TL, 0,
  0, 0, P_CORNER_TL, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_CORNER_U, P_CORNER_U, P_BRIDGE_U, P_RLED_U, P_YLED_U,
  P_GLED_U, P_GND_U, 0, 0, 0,

  // LEVEL 47
  // Puzzle
  0, 0, 0, 0, 0,
  0, P_CORNER_U, P_DBL_CORNER_U, P_RLED_U, 0,
  0, 0, 0, 0, 0,
  0, P_CORNER_U, 0, 0, P_GND_RBL,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_OFF, P_GOAL_YLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_BRIDGE_U, P_CORNER_U, P_TPIECE_U, P_TPIECE_U, P_YLED_U,
  P_VCC_U, 0, 0, 0, 0,

  // LEVEL 48
  // Puzzle
  P_VCC_R, P_TPIECE_RBL, 0, 0, 0,
  P_CORNER_BR, P_TPIECE_LTR, 0, 0, 0,
  0, 0, P_DBL_CORNER_U, P_CORNER_TL, 0,
  0, 0, P_CORNER_TL, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_BRIDGE_U, P_CORNER_U, P_CORNER_U, P_RLED_U, P_YLED_U,
  P_GLED_U, P_GND_U, 0, 0, 0,

  // LEVEL 49
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  P_CORNER_BR, 0, P_BRIDGE1_TB_LR, 0, P_VCC_U,
  P_CORNER_TR, P_GND_BLT, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_ON, P_GOAL_GLED_OFF, 0,
  P_GOAL_SW2, P_GOAL_RLED_ON, P_GOAL_GLED_ON, 0,
  P_GOAL_SW3, P_GOAL_RLED_OFF, P_GOAL_GLED_OFF, 0,
  // Hand
  P_TPIECE_U, P_CORNER_U, P_CORNER_U, P_CORNER_U, P_RLED_U,
  P_GLED_U, P_SW2_U, 0, 0, 0,

  // LEVEL 50
  // Puzzle
  0, 0, P_CORNER_BR, P_GND_U, 0,
  0, 0, 0, 0, 0,
  P_VCC_R, 0, 0, 0, P_CORNER_U,
  0, 0, P_CORNER_TR, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_ON,
  P_GOAL_SW2, P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_ON,
  P_GOAL_SW3, P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  // Hand
  P_TPIECE_U, P_TPIECE_U, P_DBL_CORNER_U, P_BRIDGE_U, P_RLED_U,
  P_YLED_U, P_GLED_U, P_SW2_U, 0, 0,

  // LEVEL 51
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, P_GND_LTR, 0,
  0, 0, 0, 0, 0,
  0, P_SW2_U, 0, 0, 0,
  0, P_GLED_U, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_ON, P_GOAL_YLED_OFF, P_GOAL_GLED_OFF,
  P_GOAL_SW2, P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  P_GOAL_SW3, P_GOAL_RLED_ON, P_GOAL_YLED_OFF, P_GOAL_GLED_ON,
  // Hand
  P_STRAIGHT_U, P_TPIECE_U, P_TPIECE_U, P_CORNER_U, P_CORNER_U,
  P_CORNER_U, P_RLED_U, P_YLED_U, P_VCC_U, 0,

  // LEVEL 52
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, P_STRAIGHT_U, 0,
  0, P_CORNER_U, P_CORNER_U, P_CORNER_U, 0,
  0, 0, P_CORNER_U, P_STRAIGHT_U, P_RLED_U,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_OFF, P_GOAL_YLED_OFF, P_GOAL_GLED_ON,
  P_GOAL_SW2, P_GOAL_RLED_OFF, P_GOAL_YLED_OFF, P_GOAL_GLED_ON,
  P_GOAL_SW3, P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON,
  // Hand
  P_TPIECE_U, P_YLED_U, P_GLED_U, P_VCC_U, P_GND_U,
  P_SW2_U, 0, 0, 0, 0,

  // LEVEL 53
  // Puzzle
  0, P_GND_U, 0, 0, 0,
  P_CORNER_U, 0, P_CORNER_U, 0, 0,
  0, P_GLED_AR_CB, 0, 0, 0,
  0, 0, P_CORNER_U, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_STRAIGHT_U, P_BRIDGE_U, P_TPIECE_U, P_TPIECE_U, P_RLED_U,
  P_YLED_U, P_VCC_U, 0, 0, 0,

  // LEVEL 54
  // Puzzle
  0, 0, 0, 0, 0,
  0, P_BLOCKER, 0, 0, 0,
  0, 0, 0, P_CORNER_BL, 0,
  0, 0, 0, 0, 0,
  P_VCC_U, 0, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  P_GOAL_SW2, P_GOAL_RLED_ON, P_GOAL_YLED_OFF, P_GOAL_GLED_ON,
  P_GOAL_SW3, P_GOAL_RLED_ON, P_GOAL_YLED_OFF, P_GOAL_GLED_OFF,
  // Hand
  P_TPIECE_U, P_STRAIGHT_U, P_STRAIGHT_U, P_RLED_U, P_YLED_U,
  P_GLED_U, P_GND_U, P_SW2_U, 0, 0,

  // LEVEL 55
  // Puzzle
  0, 0, P_CORNER_BR, 0, 0,
  0, 0, 0, 0, P_CORNER_BL,
  0, 0, 0, P_YLED_AL_CR, 0,
  0, 0, P_SW2_BT, 0, 0,
  0, 0, P_VCC_T, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  P_GOAL_SW2, P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  P_GOAL_SW3, P_GOAL_RLED_OFF, P_GOAL_YLED_OFF, P_GOAL_GLED_ON,
  // Hand
  P_STRAIGHT_U, P_BRIDGE_U, P_TPIECE_U, P_TPIECE_U, P_CORNER_U,
  P_CORNER_U, P_CORNER_U, P_RLED_U, P_GLED_U, P_GND_U,

  // LEVEL 56
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, P_GLED_AB_CL, P_YLED_AB_CT,
  0, 0, 0, 0, 0,
  0, P_STRAIGHT_TB, 0, P_VCC_U, 0,
  0, P_RLED_AR_CT, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_STRAIGHT_U, P_DBL_CORNER_U, P_TPIECE_U, P_TPIECE_U, P_CORNER_U,
  P_CORNER_U, P_CORNER_U, P_CORNER_U, P_CORNER_U, P_GND_U,

  // LEVEL 57
  // Puzzle
  0, 0, P_STRAIGHT_LR, 0, 0,
  0, 0, 0, 0, 0,
  P_VCC_T, 0, P_TPIECE_BLT, P_YLED_U, 0,
  0, 0, 0, 0, P_GND_RBL,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_OFF, P_GOAL_YLED_ON, P_GOAL_GLED_OFF,
  P_GOAL_SW2, P_GOAL_RLED_ON, P_GOAL_YLED_OFF, P_GOAL_GLED_ON,
  P_GOAL_SW3, P_GOAL_RLED_OFF, P_GOAL_YLED_OFF, P_GOAL_GLED_ON,
  // Hand
  P_STRAIGHT_U, P_TPIECE_U, P_CORNER_U, P_CORNER_U, P_CORNER_U,
  P_CORNER_U, P_RLED_U, P_GLED_U, P_SW2_U, 0,

  // LEVEL 58
  // Puzzle
  0, 0, 0, 0, P_BLOCKER,
  0, 0, 0, P_VCC_U, 0,
  0, 0, P_CORNER_U, 0, P_STRAIGHT_TB,
  0, P_GND_TRB, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_STRAIGHT_U, P_DBL_CORNER_U, P_TPIECE_U, P_TPIECE_U, P_CORNER_U,
  P_CORNER_U, P_CORNER_U, P_YLED_U, P_GLED_U, 0,

  // LEVEL 59
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, P_CORNER_U, P_RLED_AL_CB, 0,
  0, 0, 0, 0, 0,
  0, P_BLOCKER, 0, 0, 0,
  0, 0, 0, P_GND_BLT, 0,
  // Goal
  P_GOAL_SW1, P_GOAL_RLED_ON, P_GOAL_GLED_ON, 0,
  P_GOAL_SW2, P_GOAL_RLED_OFF, P_GOAL_GLED_ON, 0,
  P_GOAL_SW3, P_GOAL_RLED_OFF, P_GOAL_GLED_ON, 0,
  // Hand
  P_BRIDGE_U, P_TPIECE_U, P_TPIECE_U, P_CORNER_U, P_CORNER_U,
  P_GLED_U, P_VCC_U, P_SW2_U, 0, 0,

  // LEVEL 60
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, P_CORNER_BL, 0,
  P_YLED_U, P_CORNER_U, 0, 0, P_GLED_AB_CL,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  P_GOAL_RLED_ON, P_GOAL_YLED_ON, P_GOAL_GLED_ON, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  P_STRAIGHT_U, P_TPIECE_U, P_BRIDGE_U, P_CORNER_U, P_CORNER_U,
  P_CORNER_U, P_RLED_U, P_VCC_U, P_GND_U, 0,

#if 0
  // LEVEL ?
  // Puzzle
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  // Goal
  0, 0, 0, 0,
  0, 0, 0, 0,
  0, 0, 0, 0,
  // Hand
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
#endif
};

#define LEVEL_SIZE (BOARD_WIDTH * BOARD_HEIGHT + GOAL_WIDTH * GOAL_HEIGHT + HAND_WIDTH * HAND_HEIGHT)
#define LEVELS (sizeof(levelData) / LEVEL_SIZE)

/* Some pieces are fixed in position, but may be rotated.  These are
   drawn facing their default directions, but with a rotation overlay
   sprite, rather than with a padlock overlay sprite. */
bool NeedsRotationOverlay(uint8_t piece)
{
  return ((piece == P_VCC_U) ||
          (piece == P_GND_U) ||
          (piece == P_SW1_U) ||
          (piece == P_RLED_U) ||
          (piece == P_SW2_U) ||
          (piece == P_YLED_U) ||
          (piece == P_SW3_U) ||
          (piece == P_GLED_U) ||
          (piece == P_STRAIGHT_U) ||
          (piece == P_DBL_CORNER_U) ||
          (piece == P_CORNER_U) ||
          (piece == P_TPIECE_U) ||
          (piece == P_BRIDGE_U));
}

/* If a piece is defined as having an unknown rotation, it needs to
   have its rotate bit set, and a default direction should be
   chosen. */
// XXX - Randomize this direction for more varied starting configurations?
uint8_t DefaultDirection(uint8_t piece)
{
  switch (piece) {
  case P_VCC_U:
    return P_VCC_T;
  case P_GND_U:
    return P_GND_LTR;
  case P_SW1_U:
    return P_SW1_BL;
  case P_RLED_U:
    return P_RLED_AB_CR;
  case P_SW2_U:
    return P_SW2_BT;
  case P_YLED_U:
    return P_YLED_AB_CT;
  case P_SW3_U:
    return P_SW3_BR;
  case P_GLED_U:
    return P_GLED_AB_CL;
  case P_STRAIGHT_U:
    return P_STRAIGHT_LR;
  case P_DBL_CORNER_U:
    return P_DBL_CORNER_TL_BR;
  case P_CORNER_U:
    return P_CORNER_BL;
  case P_TPIECE_U:
    return P_TPIECE_RBL;
  case P_BRIDGE_U:
    return P_BRIDGE1_TB_LR;
  default:
    return piece;
  }
}

/* Maps from a numeric piece number to a pointer to the equivalent
   tilemap. Unknown rotations are not included. They need to have a
   valid direction. */
const VRAM_PTR_TYPE* MapName(uint8_t piece)
{
  switch (piece) {
  case P_BLANK:
    return map_blank;
  case P_VCC_T:
    return map_vcc_t;
  case P_VCC_R:
    return map_vcc_r;
  case P_VCC_B:
    return map_vcc_b;
  case P_VCC_L:
    return map_vcc_l;

  case P_GND_LTR:
    return map_gnd_ltr;
  case P_GND_TRB:
    return map_gnd_trb;
  case P_GND_RBL:
    return map_gnd_rbl;
  case P_GND_BLT:
    return map_gnd_blt;

  case P_SW1_BL:
    return map_sw1_bl;
  case P_SW1_LT:
    return map_sw1_lt;
  case P_SW1_TR:
    return map_sw1_tr;
  case P_SW1_RB:
    return map_sw1_rb;

  case P_RLED_AB_CR:
    return map_rled_off_ab_cr;
  case P_RLED_AL_CB:
    return map_rled_off_al_cb;
  case P_RLED_AT_CL:
    return map_rled_off_at_cl;
  case P_RLED_AR_CT:
    return map_rled_off_ar_ct;

  case P_SW2_BT:
    return map_sw2_bt;
  case P_SW2_LR:
    return map_sw2_lr;
  case P_SW2_TB:
    return map_sw2_tb;
  case P_SW2_RL:
    return map_sw2_rl;

  case P_YLED_AL_CR:
    return map_yled_off_al_cr;
  case P_YLED_AT_CB:
    return map_yled_off_at_cb;
  case P_YLED_AR_CL:
    return map_yled_off_ar_cl;
  case P_YLED_AB_CT:
    return map_yled_off_ab_ct;

  case P_SW3_BR:
    return map_sw3_br;
  case P_SW3_LB:
    return map_sw3_lb;
  case P_SW3_TL:
    return map_sw3_tl;
  case P_SW3_RT:
    return map_sw3_rt;

  case P_GLED_AB_CL:
    return map_gled_off_ab_cl;
  case P_GLED_AL_CT:
    return map_gled_off_al_ct;
  case P_GLED_AT_CR:
    return map_gled_off_at_cr;
  case P_GLED_AR_CB:
    return map_gled_off_ar_cb;

  case P_STRAIGHT_LR:
    return map_straight_lr;
  case P_STRAIGHT_TB:
    return map_straight_tb;

  case P_DBL_CORNER_TL_BR:
    return map_dbl_corner_tl_br;
  case P_DBL_CORNER_TR_BL:
    return map_dbl_corner_tr_bl;

  case P_CORNER_BL:
    return map_corner_bl;
  case P_CORNER_TL:
    return map_corner_tl;
  case P_CORNER_TR:
    return map_corner_tr;
  case P_CORNER_BR:
    return map_corner_br;

  case P_TPIECE_RBL:
    return map_tpiece_rbl;
  case P_TPIECE_BLT:
    return map_tpiece_blt;
  case P_TPIECE_LTR:
    return map_tpiece_ltr;
  case P_TPIECE_TRB:
    return map_tpiece_trb;

  case P_BRIDGE1_TB_LR:
    return map_bridge1_tb_lr;
  case P_BRIDGE2_TB_LR:
    return map_bridge2_tb_lr;

  case P_BLOCKER:
    return map_blocker;

  default:
    return map_blank;
  }
}

/* Maps from a numeric piece number of an LED to a pointer to the
   equivalent tilemap with that LED on. Unknown rotations are not
   included. They need to have a valid direction. */
const VRAM_PTR_TYPE* LedOnMapName(uint8_t piece)
{
  switch (piece) {
  case P_RLED_AB_CR:
    return map_rled_on_ab_cr;
  case P_RLED_AL_CB:
    return map_rled_on_al_cb;
  case P_RLED_AT_CL:
    return map_rled_on_at_cl;
  case P_RLED_AR_CT:
    return map_rled_on_ar_ct;

  case P_YLED_AL_CR:
    return map_yled_on_al_cr;
  case P_YLED_AT_CB:
    return map_yled_on_at_cb;
  case P_YLED_AR_CL:
    return map_yled_on_ar_cl;
  case P_YLED_AB_CT:
    return map_yled_on_ab_ct;

  case P_GLED_AB_CL:
    return map_gled_on_ab_cl;
  case P_GLED_AL_CT:
    return map_gled_on_al_ct;
  case P_GLED_AT_CR:
    return map_gled_on_at_cr;
  case P_GLED_AR_CB:
    return map_gled_on_ar_cb;

  default:
    return MapName(piece); // for safety
  }
}

/* Maps from a numeric goal piece number to a pointer to the
   equivalent tilemap. */
const VRAM_PTR_TYPE* MapGoalName(uint8_t piece)
{
  switch (piece) {
  case P_GOAL_BLANK:
    return map_flat;
  case P_GOAL_RLED_OFF:
    return map_goal_rled_off;
  case P_GOAL_YLED_OFF:
    return map_goal_yled_off;
  case P_GOAL_GLED_OFF:
    return map_goal_gled_off;
  case P_GOAL_RLED_ON:
    return map_goal_rled_on;
  case P_GOAL_YLED_ON:
    return map_goal_yled_on;
  case P_GOAL_GLED_ON:
    return map_goal_gled_on;
  case P_GOAL_SW1:
    return map_sw1_bl;
  case P_GOAL_SW2:
    return map_sw2_bt;
  case P_GOAL_SW3:
    return map_sw3_br;
  default:
    return map_flat;
  }
}

/*
 * BCD_addConstant
 *
 * Adds a constant (binary number) to a BCD number
 *
 * num [in, out]
 *   The BCD number
 *
 * digits [in]
 *   The number of digits in the BCD number, num
 *
 * x [in]
 *   The binary value to be added to the BCD number
 *
 *   Note: The largest value that can be safely added to a BCD number
 *         is BCD_ADD_CONSTANT_MAX. If the result would overflow num,
 *         then num will be clamped to its maximum value (all 9's).
 *
 * Returns:
 *   A boolean that is true if num has been clamped to its maximum
 *   value (all 9's), or false otherwise.
 */
#define BCD_ADD_CONSTANT_MAX 244
static bool BCD_addConstant(uint8_t* const num, const uint8_t digits, uint8_t x)
{
  for (uint8_t i = 0; i < digits; ++i) {
    uint8_t val = num[i] + x;
    if (val < 10) { // speed up the common cases
      num[i] = val;
      x = 0;
      break;
    } else if (val < 20) {
      num[i] = val - 10;
      x = 1;
    } else if (val < 30) {
      num[i] = val - 20;
      x = 2;
    } else if (val < 40) {
      num[i] = val - 30;
      x = 3;
    } else { // handle the rest of the cases (up to 255 - 9) with a loop
      for (uint8_t j = 5; j < 26; ++j) {
        if (val < (j * 10)) {
          num[i] = val - ((j - 1) * 10);
          x = (j - 1);
          break;
        }
      }
    }
  }

  if (x > 0) {
    for (uint8_t i = 0; i < digits; ++i)
      num[i] = 9;
    return true;
  }

  return false;
}

#define LOCK_OVERLAY 0
#define ALT_LOCK_OVERLAY 1
#define ROTATE_OVERLAY 2
#define ALT_ROTATE_OVERLAY 3
#define CURSOR_SPRITE 4

// The highest sprite index is for the "mouse cursor" and the 9 highest below that are reserved for drag-and-drop to maintain proper z-ordering
#define RESERVED_SPRITES 10

uint8_t OverlayOffset(int8_t piece)
{
  // Sometimes the lock or rotation overlay would hide important info on a tile, so we might need to display it offset to avoid hiding the +/- on an LED
  if (piece == P_RLED_AL_CB ||
      piece == P_RLED_AR_CT ||
      piece == P_YLED_AT_CB ||
      piece == P_YLED_AB_CT ||
      piece == P_GLED_AB_CL ||
      piece == P_GLED_AT_CR)
    return TILE_WIDTH * 2;
  else
    return 0;
}

// Compressed ram font data for digits 0-9
// run ramfont/main ramfont-digits.png to generate
const uint8_t rf_digits[] PROGMEM = {
  0x7c, 0xc2, 0xc2, 0xc2, 0xe2, 0xfe, 0x7c, 0x00,
  0x10, 0x38, 0x38, 0x38, 0x38, 0x38, 0x10, 0x00,
  0x7c, 0xe6, 0xc4, 0x60, 0x18, 0xfc, 0x7e, 0x00,
  0x7c, 0x60, 0x30, 0xfc, 0xc0, 0xfe, 0x7c, 0x00,
  0x60, 0x70, 0x68, 0x64, 0xfe, 0xfc, 0x60, 0x00,
  0x7e, 0x06, 0x7c, 0xc0, 0xc0, 0xfe, 0x7c, 0x00,
  0x3c, 0x62, 0x02, 0x7e, 0xc2, 0xfe, 0x7c, 0x00,
  0x7c, 0xc2, 0xc0, 0x70, 0x18, 0x1c, 0x1c, 0x00,
  0x7c, 0x66, 0x3c, 0x7c, 0xc6, 0xfe, 0x7c, 0x00,
  0x7c, 0xe2, 0xc2, 0xfc, 0xc0, 0xc2, 0x7c, 0x00,
};

static void LoadLevel(const uint8_t level)
{
  cursor_init(&cursor, MAX_SPRITES - 1, CURSOR_SPRITE,
              HAND_START_X * TILE_WIDTH + (TILE_WIDTH >> 1),
              HAND_START_Y * TILE_HEIGHT + (TILE_HEIGHT >> 1));

  for (uint8_t i = OVERLAY_SPRITE_START; i < MAX_SPRITES - 1; ++i)
    sprites[i].y = SCREEN_TILES_V * TILE_HEIGHT; // OFF_SCREEN;

  // Draw the main breadboard
  for (uint8_t v = 1; v < 16; ++v)
    SetTile(0, v, TILE_FOREGROUND);
  for (uint8_t h = 0; h < VRAM_TILES_H; ++h) {
    SetTile(h, 0, TILE_BREADBOARD_TOP);
    SetTile(h, 16, TILE_BREADBOARD_BOTTOM);
  }
  Fill(GOAL_START_X - 3, 1, VRAM_TILES_H - (GOAL_START_X - 3), 15, TILE_FOREGROUND);

  // Draw a colored strip that corresponds to the level of difficulty
  uint8_t color = GetLevelColor(level);
  uint8_t* ramTile = GetUserRamTile(0);
  memset(ramTile, color, 64);

  for (uint8_t h = GOAL_START_X + 9; h < GOAL_START_X + 12; ++h)
    SetRamTile(h, 1, 0);

  DrawMap(GOAL_START_X - 2, GOAL_START_Y - 2, map_circuit);

  DrawMap(HAND_START_X, HAND_START_Y - 2, map_addtogrid);

  RamFont_Load2Digits(rf_digits, 1, level, 0x00, 0xBF);
  SetRamTile(GOAL_START_X + 6, GOAL_START_Y - 2, 2); // ram tile 2 = 10's place,
  SetRamTile(GOAL_START_X + 7, GOAL_START_Y - 2, 1); // ram tile 1 = 1's place

  const uint16_t levelOffset = (level - 1) * LEVEL_SIZE;

#define BOARD_OFFSET_IN_LEVEL 0
#define GOAL_OFFSET_IN_LEVEL (BOARD_WIDTH * BOARD_HEIGHT)
#define HAND_OFFSET_IN_LEVEL (GOAL_OFFSET_IN_LEVEL + (GOAL_WIDTH * GOAL_HEIGHT))

  // The cursor should always start over a piece that can be rotated and picked up
  DrawMap(CONTROLS_LR_START_X, CONTROLS_LR_START_Y, map_controls_lr);
  DrawMap(CONTROLS_DPAD_START_X, CONTROLS_DPAD_START_Y, map_controls_dpad);

  // We only want to display this if there is a switch in the level
  uint8_t first_goal_piece = (uint8_t)pgm_read_byte(&levelData[(levelOffset + GOAL_OFFSET_IN_LEVEL) + 0 * GOAL_WIDTH + 0]);
  if (first_goal_piece == P_GOAL_SW1) // Easy way to tell if there is a switch in the level
    DrawMap(CONTROLS_B_START_X, CONTROLS_B_START_Y, map_controls_b);
  else
    DrawMap(CONTROLS_B_START_X, CONTROLS_B_START_Y, map_controls_b_off);

  // B must always get drawn before A, because they overlap
  DrawMap(CONTROLS_A_START_X, CONTROLS_A_START_Y, map_controls_a);

  uint8_t currentSprite = OVERLAY_SPRITE_START;
  for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x) {
      uint8_t piece = (uint8_t)pgm_read_byte(&levelData[(levelOffset + BOARD_OFFSET_IN_LEVEL) + y * BOARD_WIDTH + x]);
      bool rotationBit = NeedsRotationOverlay(piece);
      piece = DefaultDirection(piece);
      if (rotationBit)
        board[y][x] = piece | FLAG_ROTATE; // set the rotation bit (this presumes the highest piece number is < FLAG_ROTATE)
      else
        board[y][x] = piece | FLAG_LOCKED; // set the lock bit

      DrawMap(BOARD_START_X + x * BOARD_H_SPACING, BOARD_START_Y + y * BOARD_V_SPACING, MapName(piece));

      // Any pieces that are part of the inital setup can't be moved, so add either a lock or rotate icon
      if ((piece != P_BLANK) && (currentSprite < MAX_SPRITES - RESERVED_SPRITES - GAME_USER_RAM_TILES_COUNT)) {

        // If the overlay needs to be offset so it doesn't cover the +/- on a token, we need to use a different icon (that is shifted to the right by 1 pixel)
        uint8_t offset = OverlayOffset(piece);
        if (offset) {
          if (rotationBit)
            sprites[currentSprite].tileIndex = ALT_ROTATE_OVERLAY;
          else
            sprites[currentSprite].tileIndex = ALT_LOCK_OVERLAY;
        } else {
          if (rotationBit)
            sprites[currentSprite].tileIndex = ROTATE_OVERLAY;
          else
            sprites[currentSprite].tileIndex = LOCK_OVERLAY;
        }

        sprites[currentSprite].x = (((BOARD_START_X + (TOKEN_WIDTH - 1)) + x * BOARD_H_SPACING) * TILE_WIDTH) - offset;
        sprites[currentSprite].y = ((BOARD_START_Y + (TOKEN_HEIGHT - 1)) + y * BOARD_V_SPACING) * TILE_HEIGHT;

        ++currentSprite;
      }
    }

  // Draw Goal
  // Figure out the last occupied goal line, and then draw the "Meets Rules" below that

  // This is now a global, so the BoardChanged function can use it to update the X or checkmark when the board changes
  meetsRulesY = GOAL_START_Y + GOAL_HEIGHT * GOAL_V_SPACING;
  for (uint8_t y = 0; y < GOAL_HEIGHT; ++y) {
    bool occupied = false;
    for (uint8_t x = 0; x < GOAL_WIDTH; ++x) {
      uint8_t piece = (uint8_t)pgm_read_byte(&levelData[(levelOffset + GOAL_OFFSET_IN_LEVEL) + y * GOAL_WIDTH + x]);
      goal[y][x] = piece;
      //uint8_t indent = (x == 0 && piece < P_GOAL_SW1) ? 1 : 0; // if the first piece isn't a switch, indent it by 1
      if (piece != P_GOAL_BLANK)
        DrawMap(/*indent + */(GOAL_START_X + x * GOAL_H_SPACING), GOAL_START_Y + y * GOAL_V_SPACING, MapGoalName(piece));
      occupied |= piece;
    }
    if (occupied) {
      if (goal[0][0] != P_GOAL_SW1) // Avoids flicker of X when no switch and changing levels
        SetTile(GOAL_START_X - 2, (GOAL_START_Y + 1) + y * GOAL_V_SPACING, TILE_GOAL_UNMET);
    } else {
      meetsRulesY -= GOAL_V_SPACING;
    }
  }

  // This assumes there are no "gaps" in the goals, which there shouldn't be
  SetTile(GOAL_START_X - 2, meetsRulesY, TILE_GOAL_UNMET);
  DrawMap(GOAL_START_X, meetsRulesY, map_meetsrules);

  // Draw Hand
  for (uint8_t h = 0; h < HAND_WIDTH * HAND_H_SPACING + 1; ++h) {
    SetTile(h, 19, TILE_BREADBOARD_TOP);
    SetTile(h, 23, TILE_FOREGROUND);
    SetTile(h, 27, TILE_BREADBOARD_BOTTOM);
  }
  for (uint8_t v = 20; v < 27; ++v) {
    SetTile(HAND_H_SPACING * 0, v, TILE_FOREGROUND);
    SetTile(HAND_H_SPACING * 1, v, TILE_FOREGROUND);
    SetTile(HAND_H_SPACING * 2, v, TILE_FOREGROUND);
    SetTile(HAND_H_SPACING * 3, v, TILE_FOREGROUND);
    SetTile(HAND_H_SPACING * 4, v, TILE_FOREGROUND);
    SetTile(HAND_H_SPACING * 5, v, TILE_FOREGROUND);
  }

  for (uint8_t y = 0; y < HAND_HEIGHT; ++y)
    for (uint8_t x = 0; x < HAND_WIDTH; ++x) {
      uint8_t piece = (uint8_t)pgm_read_byte(&levelData[(levelOffset + HAND_OFFSET_IN_LEVEL) + y * HAND_WIDTH + x]);
      piece = DefaultDirection(piece);
      hand[y][x] = piece;
      DrawMap(HAND_START_X + x * HAND_H_SPACING, HAND_START_Y + y * HAND_V_SPACING, MapName(piece));
    }

  boardChanged = true;
}

const uint8_t rotateClockwise[] PROGMEM =
  {
   P_BLANK,

   P_VCC_R,
   P_VCC_B,
   P_VCC_L,
   P_VCC_T,

   P_GND_TRB,
   P_GND_RBL,
   P_GND_BLT,
   P_GND_LTR,

   P_SW1_LT,
   P_SW1_TR,
   P_SW1_RB,
   P_SW1_BL,

   P_RLED_AL_CB,
   P_RLED_AT_CL,
   P_RLED_AR_CT,
   P_RLED_AB_CR,

   P_SW2_LR,
   P_SW2_TB,
   P_SW2_RL,
   P_SW2_BT,

   P_YLED_AT_CB,
   P_YLED_AR_CL,
   P_YLED_AB_CT,
   P_YLED_AL_CR,

   P_SW3_LB,
   P_SW3_TL,
   P_SW3_RT,
   P_SW3_BR,

   P_GLED_AL_CT,
   P_GLED_AT_CR,
   P_GLED_AR_CB,
   P_GLED_AB_CL,

   P_STRAIGHT_TB,
   P_STRAIGHT_LR,

   P_DBL_CORNER_TR_BL,
   P_DBL_CORNER_TL_BR,

   P_CORNER_TL,
   P_CORNER_TR,
   P_CORNER_BR,
   P_CORNER_BL,

   P_TPIECE_BLT,
   P_TPIECE_LTR,
   P_TPIECE_TRB,
   P_TPIECE_RBL,

   P_BRIDGE2_TB_LR,
   P_BRIDGE1_TB_LR,

   P_BLOCKER,

   P_VCC_U,
   P_GND_U,
   P_SW1_U,
   P_RLED_U,
   P_SW2_U,
   P_YLED_U,
   P_SW3_U,
   P_GLED_U,
   P_STRAIGHT_U,
   P_DBL_CORNER_U,
   P_CORNER_U,
   P_TPIECE_U,
   P_BRIDGE_U,
};

const uint8_t rotateCounterClockwise[] PROGMEM =
  {
   P_BLANK,

   P_VCC_L,
   P_VCC_T,
   P_VCC_R,
   P_VCC_B,

   P_GND_BLT,
   P_GND_LTR,
   P_GND_TRB,
   P_GND_RBL,

   P_SW1_RB,
   P_SW1_BL,
   P_SW1_LT,
   P_SW1_TR,

   P_RLED_AR_CT,
   P_RLED_AB_CR,
   P_RLED_AL_CB,
   P_RLED_AT_CL,

   P_SW2_RL,
   P_SW2_BT,
   P_SW2_LR,
   P_SW2_TB,

   P_YLED_AB_CT,
   P_YLED_AL_CR,
   P_YLED_AT_CB,
   P_YLED_AR_CL,

   P_SW3_RT,
   P_SW3_BR,
   P_SW3_LB,
   P_SW3_TL,

   P_GLED_AR_CB,
   P_GLED_AB_CL,
   P_GLED_AL_CT,
   P_GLED_AT_CR,

   P_STRAIGHT_TB,
   P_STRAIGHT_LR,

   P_DBL_CORNER_TR_BL,
   P_DBL_CORNER_TL_BR,

   P_CORNER_BR,
   P_CORNER_BL,
   P_CORNER_TL,
   P_CORNER_TR,

   P_TPIECE_TRB,
   P_TPIECE_RBL,
   P_TPIECE_BLT,
   P_TPIECE_LTR,

   P_BRIDGE2_TB_LR,
   P_BRIDGE1_TB_LR,

   P_BLOCKER,

   P_VCC_U,
   P_GND_U,
   P_SW1_U,
   P_RLED_U,
   P_SW2_U,
   P_YLED_U,
   P_SW3_U,
   P_GLED_U,
   P_STRAIGHT_U,
   P_DBL_CORNER_U,
   P_CORNER_U,
   P_TPIECE_U,
   P_BRIDGE_U,
};

// If we are picking up a piece with the mouse cursor, this will be set to the piece, otherwise it will be -1
int8_t old_piece = -1;
// If we are picking up a piece with the mouse cursor, these will store the x and y that the piece came from
int8_t old_x = -1;
int8_t old_y = -1; // If this is > BOARD_HEIGHT then it refers to hand

// For keeping track of the currently selected drop zone
int8_t sel_start_x = -1;
int8_t sel_start_y = -1;

// Compressed ram font data for other characters: *RESTCIUMNOK(LOCK_ICON)
// run ramfont/main ramfont-popup.png to generate
const uint8_t rf_popup[] PROGMEM = {
  0x00, 0x00, 0x18, 0x3c, 0x3c, 0x18, 0x00, 0x00,
  0x3e, 0x63, 0x61, 0x3f, 0x0d, 0x79, 0x71, 0x00,
  0x3e, 0x63, 0x01, 0x3f, 0x01, 0x7f, 0x7e, 0x00,
  0x1e, 0x31, 0x01, 0x3e, 0x60, 0x73, 0x3e, 0x00,
  0x3e, 0x7f, 0x09, 0x08, 0x0c, 0x0c, 0x0c, 0x00,
  0x3e, 0x63, 0x01, 0x01, 0x63, 0x7f, 0x3e, 0x00,
  0x08, 0x18, 0x18, 0x18, 0x1c, 0x1c, 0x1c, 0x00,
  0x20, 0x61, 0x61, 0x61, 0x73, 0x3f, 0x1e, 0x00,
  0x31, 0x7b, 0x6f, 0x65, 0x61, 0x63, 0x23, 0x00,
  0x23, 0x67, 0x6d, 0x79, 0x71, 0x63, 0x23, 0x00,
  0x38, 0x66, 0x61, 0x61, 0x71, 0x7f, 0x3e, 0x00,
  0x32, 0x1b, 0x0b, 0x1f, 0x3b, 0x73, 0x73, 0x00,
  0x18, 0x24, 0x24, 0x7e, 0x56, 0x6a, 0x7e, 0x00,
};

// Compressed ram font data for popup border
// run ramfont/main ramfont-popup-border.png to generate
const uint8_t rf_popup_border[] PROGMEM = {
  0xff, 0xff, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
  0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xff, 0x7f, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
  0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
  0x03, 0x03, 0x03, 0x03, 0x03, 0xff, 0xff, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00,
  0x60, 0x60, 0x60, 0x60, 0x60, 0x7f, 0x7f, 0x00,
};

// Loads 'len' compressed 'ramfont' tiles into user ram tiles starting at 'user_ram_tile_start' using 'fg_color' and 'bg_color'
void RamFont_Load(const uint8_t* ramfont, uint8_t user_ram_tile_start, uint8_t len, uint8_t fg_color, uint8_t bg_color)
{
  //SetUserRamTilesCount(len); // commented out to avoid flickering of the current level, call manually before this function is called
  for (uint8_t tile = 0; tile < len; ++tile) {
    uint8_t* ramTile = GetUserRamTile(user_ram_tile_start + tile);
    for (uint8_t row = 0; row < 8; ++row) {
      uint8_t rowstart = row * 8;
      uint8_t data = (uint8_t)pgm_read_byte(&ramfont[tile * 8 + row]);
      uint8_t bit = 0;
      for (uint8_t bitmask = 1; bitmask != 0; bitmask <<= 1) {
        if (data & bitmask)
          ramTile[rowstart + bit] = fg_color;
        else
          ramTile[rowstart + bit] = bg_color;
        ++bit;
      }
    }
  }
}

// Ensure that 4 adjacent letters will pixel fade in differently
const uint8_t sparkle_effect[][64] PROGMEM =
{
 { 6, 33, 27, 42, 39, 47, 5, 22, 35, 36, 17, 23, 20, 11, 63, 10, 8, 14, 12, 60, 61, 9, 38, 43, 15, 0, 1, 50, 19, 37, 52, 51,
   54, 24, 16, 30, 59, 53, 58, 34, 2, 40, 4, 25, 31, 57, 7, 41, 28, 3, 18, 21, 29, 56, 48, 26, 13, 44, 32, 49, 45, 46, 62, 55, },
 { 26, 35, 44, 21, 60, 22, 52, 18, 53, 54, 58, 36, 20, 55, 25, 10, 42, 1, 2, 28, 37, 31, 0, 8, 51, 41, 5, 30, 59, 14, 39, 38,
   47, 24, 17, 27, 56, 32, 23, 13, 40, 49, 50, 15, 61, 43, 19, 3, 34, 4, 48, 33, 7, 63, 29, 11, 62, 45, 57, 9, 6, 46, 16, 12, },
 { 40, 57, 39, 22, 14, 43, 42, 3, 60, 52, 24, 46, 53, 6, 13, 54, 51, 55, 16, 33, 63, 21, 31, 28, 18, 25, 32, 9, 11, 36, 38,
   15, 7, 61, 49, 17, 45, 20, 0, 50, 34, 10, 47, 41, 23, 19, 5, 59, 44, 2, 35, 62, 26, 29, 58, 37, 30, 27, 4, 48, 1, 12, 8, 56, },
 { 24, 4, 37, 59, 20, 61, 42, 17, 6, 12, 9, 32, 5, 15, 33, 21, 57, 60, 31, 29, 2, 16, 62, 7, 45, 1, 3, 43, 27, 63, 53, 11, 36,
   41, 39, 40, 19, 58, 8, 56, 25, 48, 55, 28, 0, 50, 14, 44, 26, 18, 38, 52, 54, 49, 51, 46, 13, 22, 35, 23, 30, 47, 34, 10, },
};

// Instead of uncompressing all pixels at once for the RAM font, unveil it randomly pixel-by-pixel until it is fully displayed
void RamFont_SparkleLoad(const uint8_t*ramfont, const uint8_t user_ram_tile_start, const uint8_t len, const uint8_t fg_color)
{
  uint8_t shift[8];
  uint8_t bit = 0;
  for (uint8_t bitmask = 1; bitmask != 0; bitmask <<= 1)
    shift[bit++] = bitmask;

  // Loop over all the tiles
  for (uint8_t pixel = 0; pixel < 64; ++pixel) {
    for (uint8_t tile = 0; tile < len; ++tile) {
      uint8_t* ramTile = GetUserRamTile(user_ram_tile_start + tile);
      uint8_t target_pixel = (uint8_t)pgm_read_byte(&sparkle_effect[tile % 4][pixel]);
      uint8_t row = target_pixel / 8;
      uint8_t offset = target_pixel % 8;
      uint8_t data = (uint8_t)pgm_read_byte(&ramfont[tile * 8 + row]);
      if (data & shift[offset])
        ramTile[target_pixel] = fg_color;
    }
    if (pixel % 2) // speed it up
      WaitVsync(1);
  }
}

void RamFont_Print(uint8_t x, uint8_t y, const uint8_t* message, uint8_t len)
{
  for (uint8_t i = 0; i < len; ++i) {
    int8_t tileno = (int8_t)pgm_read_byte(&message[i]);
    if (tileno >= 0)
      SetRamTile(x + i, y, tileno);
  }
}

uint8_t GetLevelColor(uint8_t level)
{
  if ((level >= 1) && (level <= 15))
    return 0x20;  // TILE_GREEN
  else if ((level >= 16) && (level <= 30))
    return 0x2F; // TILE_YELLOW
  else if ((level >= 31) && (level <= 45))
    return 0xD0; // TILE_BLUE
  else if ((level >= 46) && (level <= 60))
    return 0x0E; // TILE_RED
  return 0xFF;
}

void RamFont_Load2Digits(const uint8_t* ramfont, uint8_t ramfont_index, uint8_t number, uint8_t fg_color, uint8_t bg_color)
{
  uint8_t digits[2] = {0};
  BCD_addConstant(digits, 2, number);

  for (uint8_t tile = 0; tile < 2; ++tile) {
    uint8_t* ramTile = GetUserRamTile(tile + ramfont_index);
    for (uint8_t row = 0; row < 8; ++row) {
      uint8_t rowstart = row * 8;
      uint8_t data = (uint8_t)pgm_read_byte(&ramfont[digits[tile] * 8 + row]);
      uint8_t bit = 0;
      for (uint8_t bitmask = 1; bitmask != 0; bitmask <<= 1) {
        if (data & bitmask)
          ramTile[rowstart + bit] = fg_color;
        else
          ramTile[rowstart + bit] = bg_color;
        ++bit;
      }
    }
  }
}

void TileToRam(uint16_t toff, uint16_t roff, uint16_t len, const char* tiles, uint8_t* ramTile)
{   // copy len number of tiles from tiles to ram tiles, use Abs below to give an absolute offset.
   toff = toff << 6; // multiply by 64 to convert from ram tile index to actual address for pixel 0
   roff = roff << 6;
   len  = len << 6;
   while (len--)
     ramTile[roff++] = pgm_read_byte(tiles + toff++);
}

/* void TriggerCommon(Track* track,u8 patch,u8 volume,u8 note); */
/* void BB_triggerFx(uint8_t patch) */
/* { //use the 5th channel exclusively to allow music (uses 1-4) to be uninterrupted */
/*   Track* track = &tracks[4]; */
/*   tracks[4].flags |= TRACK_FLAGS_PRIORITY; */
/*   track->patchCommandStreamPos = NULL; */
/*   TriggerCommon(track, patch, 255, 80); */
/*   track->flags |= TRACK_FLAGS_PLAYING; */
/* } */

// Defines for the ram fonts used in the popup menu
#define RF_ASTERISK (GAME_USER_RAM_TILES_COUNT)
#define RF_R (GAME_USER_RAM_TILES_COUNT + 1)
#define RF_E (GAME_USER_RAM_TILES_COUNT + 2)
#define RF_S (GAME_USER_RAM_TILES_COUNT + 3)
#define RF_T (GAME_USER_RAM_TILES_COUNT + 4)
#define RF_C (GAME_USER_RAM_TILES_COUNT + 5)
#define RF_I (GAME_USER_RAM_TILES_COUNT + 6)
#define RF_U (GAME_USER_RAM_TILES_COUNT + 7)
#define RF_M (GAME_USER_RAM_TILES_COUNT + 8)
#define RF_N (GAME_USER_RAM_TILES_COUNT + 9)
#define RF_O (GAME_USER_RAM_TILES_COUNT + 10)
#define RF_K (GAME_USER_RAM_TILES_COUNT + 11)

#define RF_UNSOLVED (GAME_USER_RAM_TILES_COUNT + 12)

#define RF_B_TL (GAME_USER_RAM_TILES_COUNT + 13)
#define RF_B_T (GAME_USER_RAM_TILES_COUNT + 14)
#define RF_B_TR (GAME_USER_RAM_TILES_COUNT + 15)
#define RF_B_L (GAME_USER_RAM_TILES_COUNT + 16)
#define RF_B_R (GAME_USER_RAM_TILES_COUNT + 17)
#define RF_B_BL (GAME_USER_RAM_TILES_COUNT + 18)
#define RF_B_B (GAME_USER_RAM_TILES_COUNT + 19)
#define RF_B_BR (GAME_USER_RAM_TILES_COUNT + 20)

#define RF_OnesPlace (GAME_USER_RAM_TILES_COUNT + 21)
#define RF_TensPlace (GAME_USER_RAM_TILES_COUNT + 22)
#define RF_SLIDER_ON_L (GAME_USER_RAM_TILES_COUNT + 23)
#define RF_SLIDER_ON_R (GAME_USER_RAM_TILES_COUNT + 24)
/* #define RF_OFF_L 24 */
/* #define RF_OFF_R 25 */

#define SPRITE_INDEX_SLIDER_ON 5
#define SPRITE_INDEX_SLIDER_OFF 7

const uint8_t pgm_P_RETURN[] PROGMEM         = { RF_R, RF_E, RF_T, RF_U, RF_R, RF_N };
const uint8_t pgm_P_RESET_TOKENS[] PROGMEM   = { RF_R, RF_E, RF_S, RF_E, RF_T, RAM_TILES_COUNT, RF_T, RF_O, RF_K, RF_E, RF_N, RF_S };
const uint8_t pgm_P_CIRCUIT[] PROGMEM        = { RF_C, RF_I, RF_R, RF_C, RF_U, RF_I, RF_T };
const uint8_t pgm_P_MUSIC[] PROGMEM          = { RF_M, RF_U, RF_S, RF_I, RF_C };
const uint8_t pgm_P_SLIDER_ON[] PROGMEM       = { RF_SLIDER_ON_L, RF_SLIDER_ON_R };
/* const uint8_t pgm_P_SLIDER_OFF[] PROGMEM      = { RF_OFF_L, RF_OFF_R }; */

bool IsSwitch(uint8_t piece)
{
  return (piece == P_SW1_BL ||
          piece == P_SW1_LT ||
          piece == P_SW1_TR ||
          piece == P_SW1_RB ||
          piece == P_SW2_BT ||
          piece == P_SW2_LR ||
          piece == P_SW2_TB ||
          piece == P_SW2_RL ||
          piece == P_SW3_BR ||
          piece == P_SW3_LB ||
          piece == P_SW3_TL ||
          piece == P_SW3_RT);
}

uint8_t ChangeSwitch(uint8_t piece)
{
  switch (piece) {
  case P_SW1_BL:
    return P_SW2_BT;
  case P_SW1_LT:
    return P_SW2_LR;
  case P_SW1_TR:
    return P_SW2_TB;
  case P_SW1_RB:
    return P_SW2_RL;
  case P_SW2_BT:
    return P_SW3_BR;
  case P_SW2_LR:
    return P_SW3_LB;
  case P_SW2_TB:
    return P_SW3_TL;
  case P_SW2_RL:
    return P_SW3_RT;
  case P_SW3_BR:
    return P_SW1_BL;
  case P_SW3_LB:
    return P_SW1_LT;
  case P_SW3_TL:
    return P_SW1_TR;
  case P_SW3_RT:
    return P_SW1_RB;
  default:
    return piece;
  }
}

// Given an x and y in board coordinates, returns the sprite index
// used for a lock/rotation overlay
int8_t FindSpriteIndexForOverlay(uint8_t level, uint8_t x, uint8_t y)
{
  const uint16_t levelOffset = (level - 1) * LEVEL_SIZE;
  uint8_t currentSprite = OVERLAY_SPRITE_START;
  for (uint8_t yy = 0; yy < BOARD_HEIGHT; ++yy) {
    for (uint8_t xx = 0; xx < BOARD_WIDTH; ++xx) {
      uint8_t orig_piece = (uint8_t)pgm_read_byte(&levelData[(levelOffset + BOARD_OFFSET_IN_LEVEL) + yy * BOARD_WIDTH + xx]);
      if (orig_piece != P_BLANK && x == xx && y == yy)
        return currentSprite;
      if ((orig_piece != P_BLANK) && (currentSprite < (MAX_SPRITES - RESERVED_SPRITES)))
        ++currentSprite;
    }
  }
  return -1;
}

#define DIRECTION_MASK 0x03
#define D_T 0
#define D_R 1
#define D_B 2
#define D_L 3

const uint8_t isValidNeighborFromDirection[48][4] PROGMEM =
  {
// P_BLANK 0
   {0, 0, 0, 0},
// P_VCC_T 1
   {1, 0, 0, 0},
// P_VCC_R 2
   {0, 1, 0, 0},
// P_VCC_B 3
   {0, 0, 1, 0},
// P_VCC_L 4
   {0, 0, 0, 1},

// P_GND_LTR 5
   {1, 1, 0, 1},
// P_GND_TRB 6
   {1, 1, 1, 0},
// P_GND_RBL 7
   {0, 1, 1, 1},
// P_GND_BLT 8
   {1, 0, 1, 1},

// P_SW1_BL 9
   {0, 0, 1, 1},
// P_SW1_LT 10
   {1, 0, 0, 1},
// P_SW1_TR 11
   {1, 1, 0, 0},
// P_SW1_RB 12
   {0, 1, 1, 0},

// P_RLED_AB_CR 13
   {0, 1, 1, 0},
// P_RLED_AL_CB 14
   {0, 0, 1, 1},
// P_RLED_AT_CL 15
   {1, 0, 0, 1},
// P_RLED_AR_CT 16
   {1, 1, 0, 0},

// P_SW2_BT 17
   {1, 0, 1, 0},
// P_SW2_LR 18
   {0, 1, 0, 1},
// P_SW2_TB 19
   {1, 0, 1, 0},
// P_SW2_RL 20
   {0, 1, 0, 1},

// P_YLED_AL_CR 21
   {0, 1, 0, 1},
// P_YLED_AT_CB 22
   {1, 0, 1, 0},
// P_YLED_AR_CL 23
   {0, 1, 0, 1},
// P_YLED_AB_CT 24
   {1, 0, 1, 0},

// P_SW3_BR 25
   {0, 1, 1, 0},
// P_SW3_LB 26
   {0, 0, 1, 1},
// P_SW3_TL 27
   {1, 0, 0, 1},
// P_SW3_RT 28
   {1, 1, 0, 0},

// P_GLED_AB_CL 29
   {0, 0, 1, 1},
// P_GLED_AL_CT 30
   {1, 0, 0, 1},
// P_GLED_AT_CR 31
   {1, 1, 0, 0},
// P_GLED_AR_CB 32
   {0, 1, 1, 0},

// P_STRAIGHT_LR 33
   {0, 1, 0, 1},
// P_STRAIGHT_TB 34
   {1, 0, 1, 0},

// P_DBL_CORNER_TL_BR 35
   {1, 1, 1, 1},
// P_DBL_CORNER_TR_BL 36
   {1, 1, 1, 1},

// P_CORNER_BL 37
   {0, 0, 1, 1},
// P_CORNER_TL 38
   {1, 0, 0, 1},
// P_CORNER_TR 39
   {1, 1, 0, 0},
// P_CORNER_BR 40
   {0, 1, 1, 0},

// P_TPIECE_RBL 41
   {0, 1, 1, 1},
// P_TPIECE_BLT 42
   {1, 0, 1, 1},
// P_TPIECE_LTR 43
   {1, 1, 0, 1},
// P_TPIECE_TRB 44
   {1, 1, 1, 0},

// P_BRIDGE1_TB_LR 45
   {1, 1, 1, 1},
// P_BRIDGE2_TB_LR 46
   {1, 1, 1, 1},

// P_BLOCKER 47
   {0, 0, 0, 0},
};

const uint8_t isValidNeighborFromDirectionMeetsRules[48][4] PROGMEM =
  {
// P_BLANK 0
   {0, 0, 0, 0},
// P_VCC_T 1
   {1, 0, 0, 0},
// P_VCC_R 2
   {0, 1, 0, 0},
// P_VCC_B 3
   {0, 0, 1, 0},
// P_VCC_L 4
   {0, 0, 0, 1},

// P_GND_LTR 5
   {1, 1, 0, 1},
// P_GND_TRB 6
   {1, 1, 1, 0},
// P_GND_RBL 7
   {0, 1, 1, 1},
// P_GND_BLT 8
   {1, 0, 1, 1},

// P_SW1_BL 9
   {1, 1, 1, 1}, // THESE ARE ALL 1
// P_SW1_LT 10
   {1, 1, 1, 1}, // THESE ARE ALL 1
// P_SW1_TR 11
   {1, 1, 1, 1}, // THESE ARE ALL 1
// P_SW1_RB 12
   {1, 1, 1, 1}, // THESE ARE ALL 1

// P_RLED_AB_CR 13
   {0, 1, 1, 0},
// P_RLED_AL_CB 14
   {0, 0, 1, 1},
// P_RLED_AT_CL 15
   {1, 0, 0, 1},
// P_RLED_AR_CT 16
   {1, 1, 0, 0},

// P_SW2_BT 17
   {1, 1, 1, 1}, // THESE ARE ALL 1
// P_SW2_LR 18
   {1, 1, 1, 1}, // THESE ARE ALL 1
// P_SW2_TB 19
   {1, 1, 1, 1}, // THESE ARE ALL 1
// P_SW2_RL 20
   {1, 1, 1, 1}, // THESE ARE ALL 1

// P_YLED_AL_CR 21
   {0, 1, 0, 1},
// P_YLED_AT_CB 22
   {1, 0, 1, 0},
// P_YLED_AR_CL 23
   {0, 1, 0, 1},
// P_YLED_AB_CT 24
   {1, 0, 1, 0},

// P_SW3_BR 25
   {1, 1, 1, 1}, // THESE ARE ALL 1
// P_SW3_LB 26
   {1, 1, 1, 1}, // THESE ARE ALL 1
// P_SW3_TL 27
   {1, 1, 1, 1}, // THESE ARE ALL 1
// P_SW3_RT 28
   {1, 1, 1, 1}, // THESE ARE ALL 1

// P_GLED_AB_CL 29
   {0, 0, 1, 1},
// P_GLED_AL_CT 30
   {1, 0, 0, 1},
// P_GLED_AT_CR 31
   {1, 1, 0, 0},
// P_GLED_AR_CB 32
   {0, 1, 1, 0},

// P_STRAIGHT_LR 33
   {0, 1, 0, 1},
// P_STRAIGHT_TB 34
   {1, 0, 1, 0},

// P_DBL_CORNER_TL_BR 35
   {1, 1, 1, 1},
// P_DBL_CORNER_TR_BL 36
   {1, 1, 1, 1},

// P_CORNER_BL 37
   {0, 0, 1, 1},
// P_CORNER_TL 38
   {1, 0, 0, 1},
// P_CORNER_TR 39
   {1, 1, 0, 0},
// P_CORNER_BR 40
   {0, 1, 1, 0},

// P_TPIECE_RBL 41
   {0, 1, 1, 1},
// P_TPIECE_BLT 42
   {1, 0, 1, 1},
// P_TPIECE_LTR 43
   {1, 1, 0, 1},
// P_TPIECE_TRB 44
   {1, 1, 1, 0},

// P_BRIDGE1_TB_LR 45
   {1, 1, 1, 1},
// P_BRIDGE2_TB_LR 46
   {1, 1, 1, 1},

// P_BLOCKER 47
   {0, 0, 0, 0},
};

#define PRUNEBOARD_FLAG_NORMAL 0
#define PRUNEBOARD_FLAG_MEETS_RULES 1

uint8_t CountValidTopNeighbor(uint8_t flags, uint8_t x, uint8_t y)
{
  if (y > 0) {
    uint8_t piece = pruned_board[y - 1][x];
    if (flags == 0)
      return pgm_read_byte(&isValidNeighborFromDirection[piece][D_B]);
    else
      return pgm_read_byte(&isValidNeighborFromDirectionMeetsRules[piece][D_B]);
  } else
    return 0;
}

uint8_t CountValidRightNeighbor(uint8_t flags, uint8_t x, uint8_t y)
{
  if (x < BOARD_WIDTH - 1) {
    uint8_t piece = pruned_board[y][x + 1];
    if (flags == 0)
      return pgm_read_byte(&isValidNeighborFromDirection[piece][D_L]);
    else
      return pgm_read_byte(&isValidNeighborFromDirectionMeetsRules[piece][D_L]);
  } else
    return 0;
}

uint8_t CountValidBottomNeighbor(uint8_t flags, uint8_t x, uint8_t y)
{
  if (y < BOARD_HEIGHT - 1) {
    uint8_t piece = pruned_board[y + 1][x];
    if (flags == 0)
      return pgm_read_byte(&isValidNeighborFromDirection[piece][D_T]);
    else
      return pgm_read_byte(&isValidNeighborFromDirectionMeetsRules[piece][D_T]);
  } else
    return 0;
}

uint8_t CountValidLeftNeighbor(uint8_t flags, uint8_t x, uint8_t y)
{
  if (x > 0) {
    uint8_t piece = pruned_board[y][x - 1];
    if (flags == 0)
      return pgm_read_byte(&isValidNeighborFromDirection[piece][D_R]);
    else
      return pgm_read_byte(&isValidNeighborFromDirectionMeetsRules[piece][D_R]);
  } else
    return 0;
}

bool PruneBoard(uint8_t flags)
{
  bool meetsRules = true;

  // Copy the state of the board into pruned_board, because this is what we will prune, and what the netlist generator will run from
  for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x)
      pruned_board[y][x] = board[y][x] & PIECE_MASK;

  // Keep looping until we reach a steady state where no pieces were removed or degenerated
  uint8_t piecesRemoved;
  do {
    piecesRemoved = 0;
    // Loop over the pruned_board and ensure each piece has enough valid neighbors in the right places, otherwise that piece will degenerate
    for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
      for (uint8_t x = 0; x < BOARD_WIDTH; ++x) {
        uint8_t piece = pruned_board[y][x];
        uint8_t count = 0;
        switch (piece) {
          // -------------------- BLANK
        case P_BLANK:
          break;

          // -------------------- VCC
        case P_VCC_T:
          count += CountValidTopNeighbor(flags, x, y);
          if (count == 0) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_VCC_R:
          count += CountValidRightNeighbor(flags, x, y);
          if (count == 0) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_VCC_B:
          count += CountValidBottomNeighbor(flags, x, y);
          if (count == 0) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_VCC_L:
          count += CountValidLeftNeighbor(flags, x, y);
          if (count == 0) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;

          // -------------------- GND
        case P_GND_LTR:
          count += CountValidLeftNeighbor(flags, x, y);
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          if (count == 0) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_GND_TRB:
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          count += CountValidBottomNeighbor(flags, x, y);
          if (count == 0) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_GND_RBL:
          count += CountValidRightNeighbor(flags, x, y);
          count += CountValidBottomNeighbor(flags, x, y);
          count += CountValidLeftNeighbor(flags, x, y);
          if (count == 0) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_GND_BLT:
          count += CountValidBottomNeighbor(flags, x, y);
          count += CountValidLeftNeighbor(flags, x, y);
          count += CountValidTopNeighbor(flags, x, y);
          if (count == 0) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;

          // -------------------- SW1
        case P_SW1_BL:
          if (flags & PRUNEBOARD_FLAG_MEETS_RULES) {
            count += CountValidTopNeighbor(flags, x, y);
            count += CountValidRightNeighbor(flags, x, y);
          }
          count += CountValidBottomNeighbor(flags, x, y);
          count += CountValidLeftNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_SW1_LT:
          if (flags & PRUNEBOARD_FLAG_MEETS_RULES) {
            count += CountValidRightNeighbor(flags, x, y);
            count += CountValidBottomNeighbor(flags, x, y);
          }
          count += CountValidLeftNeighbor(flags, x, y);
          count += CountValidTopNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_SW1_TR:
          if (flags & PRUNEBOARD_FLAG_MEETS_RULES) {
            count += CountValidBottomNeighbor(flags, x, y);
            count += CountValidLeftNeighbor(flags, x, y);
          }
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_SW1_RB:
          if (flags & PRUNEBOARD_FLAG_MEETS_RULES) {
            count += CountValidTopNeighbor(flags, x, y);
            count += CountValidLeftNeighbor(flags, x, y);
          }
          count += CountValidRightNeighbor(flags, x, y);
          count += CountValidBottomNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;

          // -------------------- RLED
        case P_RLED_AB_CR:
          count += CountValidBottomNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_RLED_AL_CB:
          count += CountValidLeftNeighbor(flags, x, y);
          count += CountValidBottomNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_RLED_AT_CL:
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidLeftNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_RLED_AR_CT:
          count += CountValidRightNeighbor(flags, x, y);
          count += CountValidTopNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;

          // -------------------- SW2
        case P_SW2_BT:
          if (flags & PRUNEBOARD_FLAG_MEETS_RULES) {
            count += CountValidRightNeighbor(flags, x, y);
            count += CountValidLeftNeighbor(flags, x, y);
          }
          count += CountValidBottomNeighbor(flags, x, y);
          count += CountValidTopNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_SW2_LR:
          if (flags & PRUNEBOARD_FLAG_MEETS_RULES) {
            count += CountValidTopNeighbor(flags, x, y);
            count += CountValidBottomNeighbor(flags, x, y);
          }
          count += CountValidLeftNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_SW2_TB:
          if (flags & PRUNEBOARD_FLAG_MEETS_RULES) {
            count += CountValidRightNeighbor(flags, x, y);
            count += CountValidLeftNeighbor(flags, x, y);
          }
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidBottomNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_SW2_RL:
          if (flags & PRUNEBOARD_FLAG_MEETS_RULES) {
            count += CountValidTopNeighbor(flags, x, y);
            count += CountValidBottomNeighbor(flags, x, y);
          }
          count += CountValidRightNeighbor(flags, x, y);
          count += CountValidLeftNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;

          // -------------------- YLED
        case P_YLED_AL_CR:
          count += CountValidLeftNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_YLED_AT_CB:
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidBottomNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_YLED_AR_CL:
          count += CountValidRightNeighbor(flags, x, y);
          count += CountValidLeftNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_YLED_AB_CT:
          count += CountValidBottomNeighbor(flags, x, y);
          count += CountValidTopNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;

          // -------------------- SW3
        case P_SW3_BR:
          if (flags & PRUNEBOARD_FLAG_MEETS_RULES) {
            count += CountValidTopNeighbor(flags, x, y);
            count += CountValidLeftNeighbor(flags, x, y);
          }
          count += CountValidBottomNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_SW3_LB:
          if (flags & PRUNEBOARD_FLAG_MEETS_RULES) {
            count += CountValidTopNeighbor(flags, x, y);
            count += CountValidRightNeighbor(flags, x, y);
          }
          count += CountValidLeftNeighbor(flags, x, y);
          count += CountValidBottomNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_SW3_TL:
          if (flags & PRUNEBOARD_FLAG_MEETS_RULES) {
            count += CountValidRightNeighbor(flags, x, y);
            count += CountValidBottomNeighbor(flags, x, y);
          }
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidLeftNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_SW3_RT:
          if (flags & PRUNEBOARD_FLAG_MEETS_RULES) {
            count += CountValidBottomNeighbor(flags, x, y);
            count += CountValidLeftNeighbor(flags, x, y);
          }
          count += CountValidRightNeighbor(flags, x, y);
          count += CountValidTopNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;

          // -------------------- GLED
        case P_GLED_AB_CL:
          count += CountValidBottomNeighbor(flags, x, y);
          count += CountValidLeftNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_GLED_AL_CT:
          count += CountValidLeftNeighbor(flags, x, y);
          count += CountValidTopNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_GLED_AT_CR:
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_GLED_AR_CB:
          count += CountValidRightNeighbor(flags, x, y);
          count += CountValidBottomNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;

          // -------------------- STRAIGHT
        case P_STRAIGHT_LR:
          count += CountValidLeftNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_STRAIGHT_TB:
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidBottomNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;

          // -------------------- DBL CORNER
        case P_DBL_CORNER_TL_BR: {
          bool removeTL = false;
          bool removeBR = false;
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidLeftNeighbor(flags, x, y);
          if (count < 2) {
            removeTL = true; // degenerate
          }
          count = 0; // reset the count
          count += CountValidBottomNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          if (count < 2) {
            removeBR = true; // degenerate
          }
          if (removeTL && removeBR)
            pruned_board[y][x] = P_BLANK;
          else if (removeTL)
            pruned_board[y][x] = P_CORNER_BR;
          else if (removeBR)
            pruned_board[y][x] = P_CORNER_TL;
          if (removeTL || removeBR)
            ++piecesRemoved;
        }
          break;
        case P_DBL_CORNER_TR_BL: {
          bool removeTR = false;
          bool removeBL = false;
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          if (count < 2) {
            removeTR = true; // degenerate
          }
          count = 0; // reset the count
          count += CountValidBottomNeighbor(flags, x, y);
          count += CountValidLeftNeighbor(flags, x, y);
          if (count < 2) {
            removeBL = true; // degenerate
          }
          if (removeTR && removeBL)
            pruned_board[y][x] = P_BLANK;
          else if (removeTR)
            pruned_board[y][x] = P_CORNER_BL; // degenerate into the other corner
          else if (removeBL)
            pruned_board[y][x] = P_CORNER_TR; // degenerate into the other corner
          if (removeTR || removeBL)
            ++piecesRemoved;
        }
          break;

          // -------------------- CORNER
        case P_CORNER_BL:
          count += CountValidBottomNeighbor(flags, x, y);
          count += CountValidLeftNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_CORNER_TL:
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidLeftNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_CORNER_TR:
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;
        case P_CORNER_BR:
          count += CountValidBottomNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          if (count < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          }
          break;

          // -------------------- TPIECE
        case P_TPIECE_RBL: {
          uint8_t countR = CountValidRightNeighbor(flags, x, y);
          uint8_t countB = CountValidBottomNeighbor(flags, x, y);
          uint8_t countL = CountValidLeftNeighbor(flags, x, y);
          if (countR + countB + countL < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          } else if (countR == 0) {
            pruned_board[y][x] = P_CORNER_BL; // degenerate into corner
            ++piecesRemoved;
          } else if (countB == 0) {
            pruned_board[y][x] = P_STRAIGHT_LR; // degenerate into straight
            ++piecesRemoved;
          } else if (countL == 0) {
            pruned_board[y][x] = P_CORNER_BR; // degenerate into corner
            ++piecesRemoved;
          }
        }
          break;
        case P_TPIECE_BLT: {
          uint8_t countB = CountValidBottomNeighbor(flags, x, y);
          uint8_t countL = CountValidLeftNeighbor(flags, x, y);
          uint8_t countT = CountValidTopNeighbor(flags, x, y);
          if (countB + countL + countT < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          } else if (countB == 0) {
            pruned_board[y][x] = P_CORNER_TL; // degenerate into corner
            ++piecesRemoved;
          } else if (countL == 0) {
            pruned_board[y][x] = P_STRAIGHT_TB; // degenerate into straight
            ++piecesRemoved;
          } else if (countT == 0) {
            pruned_board[y][x] = P_CORNER_BL; // degenerate into corner
            ++piecesRemoved;
          }
        }
          break;
        case P_TPIECE_LTR: {
          uint8_t countL = CountValidLeftNeighbor(flags, x, y);
          uint8_t countT = CountValidTopNeighbor(flags, x, y);
          uint8_t countR = CountValidRightNeighbor(flags, x, y);
          if (countL + countT + countR < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          } else if (countL == 0) {
            pruned_board[y][x] = P_CORNER_TR; // degenerate into corner
            ++piecesRemoved;
          } else if (countT == 0) {
            pruned_board[y][x] = P_STRAIGHT_LR; // degenerate into straight
            ++piecesRemoved;
          } else if (countR == 0) {
            pruned_board[y][x] = P_CORNER_TL; // degenerate into corner
            ++piecesRemoved;
          }
        }
          break;
        case P_TPIECE_TRB: {
          uint8_t countT = CountValidTopNeighbor(flags, x, y);
          uint8_t countR = CountValidRightNeighbor(flags, x, y);
          uint8_t countB = CountValidBottomNeighbor(flags, x, y);
          if (countT + countR + countB < 2) {
            pruned_board[y][x] = P_BLANK;
            ++piecesRemoved;
          } else if (countT == 0) {
            pruned_board[y][x] = P_CORNER_BR; // degenerate into corner
            ++piecesRemoved;
          } else if (countR == 0) {
            pruned_board[y][x] = P_STRAIGHT_TB; // degenerate into straight
            ++piecesRemoved;
          } else if (countB == 0) {
            pruned_board[y][x] = P_CORNER_TR; // degenerate into corner
            ++piecesRemoved;
          }
        }
          break;

          // -------------------- BRIDGE
        case P_BRIDGE1_TB_LR:
        case P_BRIDGE2_TB_LR: {
          bool removeTB = false;
          bool removeLR = false;
          count += CountValidTopNeighbor(flags, x, y);
          count += CountValidBottomNeighbor(flags, x, y);
          if (count < 2) {
            removeTB = true; // degenerate
          }
          count = 0; // reset the count
          count += CountValidLeftNeighbor(flags, x, y);
          count += CountValidRightNeighbor(flags, x, y);
          if (count < 2) {
            removeLR = true; // degenerate
          }
          if (removeTB && removeLR)
            pruned_board[y][x] = P_BLANK;
          else if (removeTB)
            pruned_board[y][x] = P_STRAIGHT_LR; // degenerate into the other straight
          else if (removeLR)
            pruned_board[y][x] = P_STRAIGHT_TB; // degenerate into the other straight
          if (removeTB || removeLR)
            ++piecesRemoved;
        }
          break;

        case P_BLOCKER:
          pruned_board[y][x] = P_BLANK;
          break;

        default:
          // This should never happen
          break;
        }
      }
    if (piecesRemoved)
      meetsRules = false;
  } while (piecesRemoved);

  return meetsRules;
}

#define DECIDE_NOW  0
#define DECIDE_INIT 1
#define DECIDE_NEXT 2

/*
 * The decide function avoids having to call many iterations of the
 * slow rand() function to trace out the branches in the circuit, by
 * carefully returning binary numbers in a methodical fashion.
 *
 * Example usage below:

     int main(int argc, char *argv[])
     {
       // initialize the decider
       decide(DECIDE_INIT);

       for (uint8_t a = 0; a < 4; ++a) {
         for (uint8_t i = 0; i < 2; ++i) {
           uint8_t d = decide(DECIDE_NOW);
           printf("%d\n", d);
         }
         puts("next\n");
         decide(DECIDE_NEXT);
       }
       return 0;
     }
 */
uint8_t decide(uint8_t flags)
{
  static uint8_t generation = 0;
  static uint8_t bitmask = 1;

  if (!flags) {
    // decide something
    uint8_t decision = (generation & bitmask);
    bitmask <<= 1;
    return decision ? 1 : 0;
  }

  if (flags & DECIDE_INIT) {
    generation = 0;
    bitmask = 1;
    return 0;
  }

  if (flags & DECIDE_NEXT) {
    ++generation;
    bitmask = 1;
  }

  return 0;
}

// nl_source will be NL_VV, NL_00, NL_RA, NL_RC, etc...
// d will be D_T, D_R, D_B, or D_L (masked with DIRECTION_MASK to ensure it is in range)
// returns NL_VV, NL_00, NL_RA, etc... depending on what it finds
uint8_t SimulateElectron(uint8_t nl_src, int8_t x, int8_t y, uint8_t d)
{
  uint8_t nl_dest = nl_src; // in case we are in a loop

  //  memset(directions, 0, sizeof(directions));

  uint8_t decision = 0;
  bool halt = false;
  uint8_t ttl = 0;
  while (!halt && (++ttl != 0) && x >= 0 && x <= BOARD_WIDTH - 1 && y >= 0 && y <= BOARD_HEIGHT - 1) {
    uint8_t piece = pruned_board[y][x];
    switch (piece) {
    case P_BLANK:
      halt = true;
      break;

    case P_VCC_T: // technically direction doesn't matter, because previously validated by PruneBoard
      if (d == D_IN_T)
        return NL_VV;
      halt = true;
      break;
    case P_VCC_R:
      if (d == D_IN_R)
        return NL_VV;
      halt = true;
      break;
    case P_VCC_B:
      if (d == D_IN_B)
        return NL_VV;
      halt = true;
      break;
    case P_VCC_L:
      if (d == D_IN_L)
        return NL_VV;
      halt = true;
      break;

    case P_GND_LTR: // technically direction doesn't matter, because previously validated by PruneBoard
      if (d == D_IN_T || d == D_IN_R || d == D_IN_L)
        return NL_00;
      halt = true;
      break;
    case P_GND_TRB:
      if (d == D_IN_T || d == D_IN_R || d == D_IN_B)
        return NL_00;
      halt = true;
      break;
    case P_GND_RBL:
      if (d == D_IN_R || d == D_IN_B || d == D_IN_L)
        return NL_00;
      halt = true;
      break;
    case P_GND_BLT:
      if (d == D_IN_T || d == D_IN_B || d == D_IN_L)
        return NL_00;
      halt = true;
      break;

      // we need to pay attention to direction so we know where it exits
    case P_SW1_BL:
      switch (d) {
      case D_IN_B:
        //directions[y][x] |= (D_IN_B | D_OUT_L);
        d = D_IN_R;
        x--;
        break;
      case D_IN_L:
        //directions[y][x] |= (D_IN_L | D_OUT_B);
        d = D_IN_T;
        y++;
        break;
      default: // these default cases should not be needed, but I put them here for safety
        halt = true;
        break;
      }
      break;
    case P_SW1_LT:
      switch (d) {
      case D_IN_T:
        //directions[y][x] |= (D_IN_T | D_OUT_L);
        d = D_IN_R;
        x--;
        break;
      case D_IN_L:
        //directions[y][x] |= (D_IN_L | D_OUT_T);
        d = D_IN_B;
        y--;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_SW1_TR:
      switch (d) {
      case D_IN_T:
        //directions[y][x] |= (D_IN_T | D_OUT_R);
        d = D_IN_L;
        x++;
        break;
      case D_IN_R:
        //directions[y][x] |= (D_IN_R | D_OUT_T);
        d = D_IN_B;
        y--;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_SW1_RB:
      switch (d) {
      case D_IN_R:
        //directions[y][x] |= (D_IN_R | D_OUT_B);
        d = D_IN_T;
        y++;
        break;
      case D_IN_B:
        //directions[y][x] |= (D_IN_B | D_OUT_R);
        d = D_IN_L;
        x++;
        break;
      default:
        halt = true;
        break;
      }
      break;

      // direction matters here, because we need to know whether we hit an anode or cathode
    case P_RLED_AB_CR:
      switch (d) {
      case D_IN_R:
        return NL_RC;
        break;
      case D_IN_B:
        return NL_RA;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_RLED_AL_CB:
      switch (d) {
      case D_IN_B:
        return NL_RC;
        break;
      case D_IN_L:
        return NL_RA;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_RLED_AT_CL:
      switch (d) {
      case D_IN_T:
        return NL_RA;
        break;
      case D_IN_L:
        return NL_RC;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_RLED_AR_CT:
      switch (d) {
      case D_IN_T:
        return NL_RC;
        break;
      case D_IN_R:
        return NL_RA;
        break;
      default:
        halt = true;
        break;
      }
      break;

    case P_SW2_BT:
      switch (d) {
      case D_IN_T:
        //directions[y][x] |= (D_IN_T | D_OUT_B);
        d = D_IN_T;
        y++;
        break;
      case D_IN_B:
        //directions[y][x] |= (D_IN_B | D_OUT_T);
        d = D_IN_B;
        y--;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_SW2_LR:
      switch (d) {
      case D_IN_R:
        //directions[y][x] |= (D_IN_R | D_OUT_L);
        d = D_IN_R;
        x--;
        break;
      case D_IN_L:
        //directions[y][x] |= (D_IN_L | D_OUT_R);
        d = D_IN_L;
        x++;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_SW2_TB:
      switch (d) {
      case D_IN_T:
        //directions[y][x] |= (D_IN_T | D_OUT_B);
        d = D_IN_T;
        y++;
        break;
      case D_IN_B:
        //directions[y][x] |= (D_IN_B | D_OUT_T);
        d = D_IN_B;
        y--;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_SW2_RL:
      switch (d) {
      case D_IN_R:
        //directions[y][x] |= (D_IN_R | D_OUT_L);
        d = D_IN_R;
        x--;
        break;
      case D_IN_L:
        //directions[y][x] |= (D_IN_L | D_OUT_R);
        d = D_IN_L;
        x++;
        break;
      default:
        halt = true;
        break;
      }
      break;

      // direction matters here, because we need to know whether we hit an anode or cathode
    case P_YLED_AL_CR:
      switch (d) {
      case D_IN_R:
        return NL_YC;
        break;
      case D_IN_L:
        return NL_YA;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_YLED_AT_CB:
      switch (d) {
      case D_IN_T:
        return NL_YA;
        break;
      case D_IN_B:
        return NL_YC;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_YLED_AR_CL:
      switch (d) {
      case D_IN_R:
        return NL_YA;
        break;
      case D_IN_L:
        return NL_YC;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_YLED_AB_CT:
      switch (d) {
      case D_IN_T:
        return NL_YC;
        break;
      case D_IN_B:
        return NL_YA;
        break;
      default:
        halt = true;
        break;
      }
      break;

    case P_SW3_BR:
      switch (d) {
      case D_IN_R:
        //directions[y][x] |= (D_IN_R | D_OUT_B);
        d = D_IN_T;
        y++;
        break;
      case D_IN_B:
        //directions[y][x] |= (D_IN_B | D_OUT_R);
        d = D_IN_L;
        x++;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_SW3_LB:
      switch (d) {
      case D_IN_B:
        //directions[y][x] |= (D_IN_B | D_OUT_L);
        d = D_IN_R;
        x--;
        break;
      case D_IN_L:
        //directions[y][x] |= (D_IN_L | D_OUT_B);
        d = D_IN_T;
        y++;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_SW3_TL:
      switch (d) {
      case D_IN_T:
        //directions[y][x] |= (D_IN_T | D_OUT_L);
        d = D_IN_R;
        x--;
        break;
      case D_IN_L:
        //directions[y][x] |= (D_IN_L | D_OUT_T);
        d = D_IN_B;
        y--;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_SW3_RT:
      switch (d) {
      case D_IN_T:
        //directions[y][x] |= (D_IN_T | D_OUT_R);
        d = D_IN_L;
        x++;
        break;
      case D_IN_R:
        //directions[y][x] |= (D_IN_R | D_OUT_T);
        d = D_IN_B;
        y--;
        break;
      default:
        halt = true;
        break;
      }
      break;

      // direction matters here, because we need to know whether we hit an anode or cathode
    case P_GLED_AB_CL:
      switch (d) {
      case D_IN_B:
        return NL_GA;
        break;
      case D_IN_L:
        return NL_GC;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_GLED_AL_CT:
      switch (d) {
      case D_IN_T:
        return NL_GC;
        break;
      case D_IN_L:
        return NL_GA;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_GLED_AT_CR:
      switch (d) {
      case D_IN_T:
        return NL_GA;
        break;
      case D_IN_R:
        return NL_GC;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_GLED_AR_CB:
      switch (d) {
      case D_IN_R:
        return NL_GA;
        break;
      case D_IN_B:
        return NL_GC;
        break;
      default:
        halt = true;
        break;
      }
      break;

    case P_STRAIGHT_LR:
      switch (d) {
      case D_IN_R:
        //directions[y][x] |= (D_IN_R | D_OUT_L);
        d = D_IN_R;
        x--;
        break;
      case D_IN_L:
        //directions[y][x] |= (D_IN_L | D_OUT_R);
        d = D_IN_L;
        x++;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_STRAIGHT_TB:
      switch (d) {
      case D_IN_T:
        //directions[y][x] |= (D_IN_T | D_OUT_B);
        d = D_IN_T;
        y++;
        break;
      case D_IN_B:
        //directions[y][x] |= (D_IN_B | D_OUT_T);
        d = D_IN_B;
        y--;
        break;
      default:
        halt = true;
        break;
      }
      break;

    case P_DBL_CORNER_TL_BR:
      switch (d) {
      case D_IN_T:
        //directions[y][x] |= (D_IN_T | D_OUT_L);
        d = D_IN_R;
        x--;
        break;
      case D_IN_R:
        //directions[y][x] |= (D_IN_R | D_OUT_B);
        d = D_IN_T;
        y++;
        break;
      case D_IN_B:
        //directions[y][x] |= (D_IN_B | D_OUT_R);
        d = D_IN_L;
        x++;
        break;
      case D_IN_L:
        //directions[y][x] |= (D_IN_L | D_OUT_T);
        d = D_IN_B;
        y--;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_DBL_CORNER_TR_BL:
      switch (d) {
      case D_IN_T:
        //directions[y][x] |= (D_IN_T | D_OUT_R);
        d = D_IN_L;
        x++;
        break;
      case D_IN_R:
        //directions[y][x] |= (D_IN_R | D_OUT_T);
        d = D_IN_B;
        y--;
        break;
      case D_IN_B:
        //directions[y][x] |= (D_IN_B | D_OUT_L);
        d = D_IN_R;
        x--;
        break;
      case D_IN_L:
        //directions[y][x] |= (D_IN_L | D_OUT_B);
        d = D_IN_T;
        y++;
        break;
      default:
        halt = true;
        break;
      }
      break;

    case P_CORNER_BL:
      switch (d) {
      case D_IN_B:
        //directions[y][x] |= (D_IN_B | D_OUT_L);
        d = D_IN_R;
        x--;
        break;
      case D_IN_L:
        //directions[y][x] |= (D_IN_L | D_OUT_B);
        d = D_IN_T;
        y++;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_CORNER_TL:
      switch (d) {
      case D_IN_T:
        //directions[y][x] |= (D_IN_T | D_OUT_L);
        d = D_IN_R;
        x--;
        break;
      case D_IN_L:
        //directions[y][x] |= (D_IN_L | D_OUT_T);
        d = D_IN_B;
        y--;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_CORNER_TR:
      switch (d) {
      case D_IN_T:
        //directions[y][x] |= (D_IN_T | D_OUT_R);
        d = D_IN_L;
        x++;
        break;
      case D_IN_R:
        //directions[y][x] |= (D_IN_R | D_OUT_T);
        d = D_IN_B;
        y--;
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_CORNER_BR:
      switch (d) {
      case D_IN_R:
        //directions[y][x] |= (D_IN_R | D_OUT_B);
        d = D_IN_T;
        y++;
        break;
      case D_IN_B:
        //directions[y][x] |= (D_IN_B | D_OUT_R);
        d = D_IN_L;
        x++;
        break;
      default:
        halt = true;
        break;
      }
      break;

    case P_TPIECE_RBL:
      decision = decide(DECIDE_NOW);
      switch (d) {
      case D_IN_R:
        if (decision) { // continue to L
          //directions[y][x] |= (D_IN_R | D_OUT_L);
          d = D_IN_R;
          x--;
        } else { // turn to B
          //directions[y][x] |= (D_IN_R | D_OUT_B);
          d = D_IN_T;
          y++;
        }
        break;
      case D_IN_B:
        if (decision) { // turn to R
          //directions[y][x] |= (D_IN_B | D_OUT_R);
          d = D_IN_L;
          x++;
        } else { // turn to L
          //directions[y][x] |= (D_IN_B | D_OUT_L);
          d = D_IN_R;
          x--;
        }
        break;
      case D_IN_L:
        if (decision) { // turn to B
          //directions[y][x] |= (D_IN_L | D_OUT_B);
          d = D_IN_T;
          y++;
        } else { // continue to R
          //directions[y][x] |= (D_IN_L | D_OUT_R);
          d = D_IN_L;
          x++;
        }
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_TPIECE_BLT:
      decision = decide(DECIDE_NOW);
      switch (d) {
      case D_IN_T:
        if (decision) { // turn to L
          //directions[y][x] |= (D_IN_T | D_OUT_L);
          d = D_IN_R;
          x--;
        } else { // continue to B
          //directions[y][x] |= (D_IN_T | D_OUT_B);
          d = D_IN_T;
          y++;
        }
        break;
      case D_IN_B:
        if (decision) { // continue to T
          //directions[y][x] |= (D_IN_B | D_OUT_T);
          d = D_IN_B;
          y--;
        } else { // turn to L
          //directions[y][x] |= (D_IN_B | D_OUT_L);
          d = D_IN_R;
          x--;
        }
        break;
      case D_IN_L:
        if (decision) { // turn to B
          //directions[y][x] |= (D_IN_L | D_OUT_B);
          d = D_IN_T;
          y++;
        } else { // turn to T
          //directions[y][x] |= (D_IN_L | D_OUT_T);
          d = D_IN_B;
          y--;
        }
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_TPIECE_LTR:
      decision = decide(DECIDE_NOW);
      switch (d) {
      case D_IN_T:
        if (decision) { // turn to L
          //directions[y][x] |= (D_IN_T | D_OUT_L);
          d = D_IN_R;
          x--;
        } else { // turn to R
          //directions[y][x] |= (D_IN_T | D_OUT_R);
          d = D_IN_L;
          x++;
        }
        break;
      case D_IN_R:
        if (decision) { // turn to T
          //directions[y][x] |= (D_IN_R | D_OUT_T);
          d = D_IN_B;
          y--;
        } else { // continue to L
          //directions[y][x] |= (D_IN_R | D_OUT_L);
          d = D_IN_R;
          x--;
        }
        break;
      case D_IN_L:
        if (decision) { // continue to R
          //directions[y][x] |= (D_IN_L | D_OUT_R);
          d = D_IN_L;
          x++;
        } else { // turn to T
          //directions[y][x] |= (D_IN_L | D_OUT_T);
          d = D_IN_B;
          y--;
        }
        break;
      default:
        halt = true;
        break;
      }
      break;
    case P_TPIECE_TRB:
      decision = decide(DECIDE_NOW);
      switch (d) {
      case D_IN_T:
        if (decision) { // continue to B
          //directions[y][x] |= (D_IN_T | D_OUT_B);
          d = D_IN_T;
          y++;
        } else { // turn to R
          //directions[y][x] |= (D_IN_T | D_OUT_R);
          d = D_IN_L;
          x++;
        }
        break;
      case D_IN_R:
        if (decision) { // turn to T
          //directions[y][x] |= (D_IN_R | D_OUT_T);
          d = D_IN_B;
          y--;
        } else { // turn to B
          //directions[y][x] |= (D_IN_R | D_OUT_B);
          d = D_IN_T;
          y++;
        }
        break;
      case D_IN_B:
        if (decision) { // turn to R
          //directions[y][x] |= (D_IN_B | D_OUT_R);
          d = D_IN_L;
          x++;
        } else { // continue to T
          //directions[y][x] |= (D_IN_B | D_OUT_T);
          d = D_IN_B;
          y--;
        }
        break;
      default:
        halt = true;
        break;
      }
      break;

    case P_BRIDGE1_TB_LR:
    case P_BRIDGE2_TB_LR:
      switch (d) {
      case D_IN_T:
        //directions[y][x] |= (D_IN_T | D_OUT_B);
        d = D_IN_T;
        y++;
        break;
      case D_IN_R:
        //directions[y][x] |= (D_IN_R | D_OUT_L);
        d = D_IN_R;
        x--;
        break;
      case D_IN_B:
        //directions[y][x] |= (D_IN_B | D_OUT_T);
        d = D_IN_B;
        y--;
        break;
      case D_IN_L:
        //directions[y][x] |= (D_IN_L | D_OUT_R);
        d = D_IN_L;
        x++;
        break;
      default:
        halt = true;
        break;
      }
      break;

    case P_BLOCKER:
    default:
      halt = true;
      break;
    }
  }
  return nl_dest;
}

void SimulateElectrons(uint8_t nl_src, int8_t x, int8_t y, uint8_t d) {
  decide(DECIDE_INIT);
  for (uint8_t e = 0; e < 4; ++e) { // send electrons in every possible path
    for (uint8_t i = 0; i < 2; ++i) { // using the fewest number of electrons
      uint8_t result = SimulateElectron(nl_src, x, y, d);
      pruned_netlist[result][nl_src] = pruned_netlist[nl_src][result] = 1;
    }
    decide(DECIDE_NEXT);
  }
}

#define R_BIT 1
#define Y_BIT 2
#define G_BIT 4

#define NELEMS(x) (sizeof(x)/sizeof(x[0]))
#define NETLIST_NETLIST_MASK     0x07FFFFFF
#define NETLIST_LED_STATES_MASK  0xE0000000
#define NETLIST_R_ON             0x20000000
#define NETLIST_Y_ON             0x40000000
#define NETLIST_G_ON             0x80000000

// This data is generated by running the oracle2/main program to take
// all of the netlist data I gathered manually and permute all of the
// LED colors to fill out the dataset
// circuit/oracle2$ ./main > sorted_netlists_and_led_states.inc
#include "oracle2/sorted_netlists_and_led_states.inc"

uint8_t ConsultOracle(uint32_t nl)
{
  int16_t low = 0;
  int16_t high = NELEMS(sorted_netlists_and_led_states) - 1;

  while (low <= high) {
    int16_t mid = (low + high) / 2;

    uint32_t netlist_and_led_states = (uint32_t)pgm_read_dword(&sorted_netlists_and_led_states[mid]);
    uint32_t netlist = netlist_and_led_states & NETLIST_NETLIST_MASK;

    if (netlist < nl) {
      low = mid + 1;
    } else if (netlist > nl) {
      high = mid - 1;
    } else {
      // We found it, so extract the led state, and return it
      uint8_t led_states = (uint8_t)((netlist_and_led_states & NETLIST_LED_STATES_MASK) >> 29);
      return led_states;
    }
  }
  // Not found, so default all LEDs to off
  return 0;
}

bool CurrentLevelHasSwitch(void)
{
  return (goal[0][0] == P_GOAL_SW1);
}

// If there is not a switch in the level, just pass -1 in for switchPosition
// If the goal state is requested for a level with a switch, but a switch position wasn't passed in, return 0xFF
// (which should not match the goalState from the netlist lookup)
uint8_t GoalStatesForCurrentLevel(int8_t switchPosition)
{
  bool currentLevelHasSwitch = CurrentLevelHasSwitch();
  if (currentLevelHasSwitch && !(switchPosition == 1 || switchPosition == 2 || switchPosition == 3))
    return 0xFF; // error condition that shouldn't match anything in the netlist lookup

  uint8_t goalState = 0;

  if (currentLevelHasSwitch) {
    for (uint8_t i = 1; i < 4; ++i) {
      uint8_t goalPiece = goal[switchPosition - 1][i];
      switch (goalPiece) {
      case P_GOAL_RLED_ON:
        goalState |= R_BIT;
        break;
      case P_GOAL_YLED_ON:
        goalState |= Y_BIT;
        break;
      case P_GOAL_GLED_ON:
        goalState |= G_BIT;
        break;
      }
    }
  } else {
    for (uint8_t i = 0; i < 3; ++i) {
      uint8_t goalPiece = goal[0][i];
      switch (goalPiece) {
      case P_GOAL_RLED_ON:
        goalState |= R_BIT;
        break;
      case P_GOAL_YLED_ON:
        goalState |= Y_BIT;
        break;
      case P_GOAL_GLED_ON:
        goalState |= G_BIT;
        break;
      }
    }
  }

  return goalState;
}

// RAM Font data for letters ABCDEFGHI-KLMNOPQRSTUVWXYZ,.
const uint8_t rf_help[] PROGMEM = {
  0x30, 0x78, 0xec, 0xe4, 0xfe, 0xc2, 0xc2, 0x00,
  0x3e, 0x62, 0x32, 0x7e, 0xe2, 0xf2, 0x7e, 0x00,
  0x7c, 0xc6, 0x02, 0x02, 0xc6, 0xfe, 0x7c, 0x00,
  0x3c, 0x62, 0xc2, 0xc2, 0xe2, 0xfe, 0x7e, 0x00,
  0x7c, 0xc6, 0x02, 0x7e, 0x02, 0xfe, 0xfc, 0x00,
  0x7c, 0xc6, 0x02, 0x7e, 0x06, 0x06, 0x06, 0x00,
  0x7c, 0xc6, 0x02, 0x02, 0xf2, 0xe6, 0xbc, 0x00,
  0x42, 0xc2, 0xc2, 0xfe, 0xc2, 0xc6, 0xc6, 0x00,
  0x10, 0x30, 0x30, 0x30, 0x38, 0x38, 0x38, 0x00,
  0x00, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x00,
  0x64, 0x36, 0x16, 0x3e, 0x76, 0xe6, 0xe6, 0x00,
  0x04, 0x06, 0x02, 0x02, 0x82, 0xfe, 0x7c, 0x00,
  0x62, 0xf6, 0xde, 0xca, 0xc2, 0xc6, 0x46, 0x00,
  0x46, 0xce, 0xda, 0xf2, 0xe2, 0xc6, 0x46, 0x00,
  0x70, 0xcc, 0xc2, 0xc2, 0xe2, 0xfe, 0x7c, 0x00,
  0x7c, 0xc6, 0xe2, 0x7e, 0x06, 0x06, 0x04, 0x00,
  0x7c, 0xe2, 0xc2, 0xc2, 0x7a, 0xe6, 0xdc, 0x00,
  0x7c, 0xc6, 0xc2, 0x7e, 0x1a, 0xf2, 0xe2, 0x00,
  0x3c, 0x62, 0x02, 0x7c, 0xc0, 0xe6, 0x7c, 0x00,
  0x7c, 0xfe, 0x12, 0x10, 0x18, 0x18, 0x18, 0x00,
  0x40, 0xc2, 0xc2, 0xc2, 0xe6, 0x7e, 0x3c, 0x00,
  0x40, 0xc2, 0xc2, 0xc4, 0x64, 0x38, 0x18, 0x00,
  0x40, 0xc2, 0xd2, 0xda, 0xda, 0xfe, 0x6c, 0x00,
  0x80, 0xc6, 0x6e, 0x38, 0x38, 0xec, 0xc6, 0x00,
  0x80, 0x86, 0xcc, 0x78, 0x30, 0x1c, 0x0c, 0x00,
  0x7c, 0xc0, 0x60, 0x10, 0x0c, 0xfe, 0x7c, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x0c,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00,
};

// RAM Font data for (c)20-
const uint8_t rf_title_extra[] PROGMEM = {
  0x3c, 0x42, 0x99, 0x85, 0x99, 0x42, 0x3c, 0x00,
  0x7c, 0xe6, 0xc4, 0x60, 0x18, 0xfc, 0x7e, 0x00,
  0x7c, 0xc2, 0xc2, 0xc2, 0xe2, 0xfe, 0x7c, 0x00,
  0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x00, 0x00,
};

// RAM Font data for PRESTAFONXCIU
const uint8_t rf_win[] PROGMEM = {
  0x7c, 0xc6, 0xe2, 0x7e, 0x06, 0x06, 0x04, 0x00,
  0x7c, 0xc6, 0xc2, 0x7e, 0x1a, 0xf2, 0xe2, 0x00,
  0x7c, 0xc6, 0x02, 0x7e, 0x02, 0xfe, 0xfc, 0x00,
  0x3c, 0x62, 0x02, 0x7c, 0xc0, 0xe6, 0x7c, 0x00,
  0x7c, 0xfe, 0x12, 0x10, 0x18, 0x18, 0x18, 0x00,
  0x30, 0x78, 0xec, 0xe4, 0xfe, 0xc2, 0xc2, 0x00,
  0x7c, 0xc6, 0x02, 0x7e, 0x06, 0x06, 0x06, 0x00,
  0x70, 0xcc, 0xc2, 0xc2, 0xe2, 0xfe, 0x7c, 0x00,
  0x46, 0xce, 0xda, 0xf2, 0xe2, 0xc6, 0x46, 0x00,
  0x80, 0xc6, 0x6e, 0x38, 0x38, 0xec, 0xc6, 0x00,
  0x7c, 0xc6, 0x02, 0x02, 0xc6, 0xfe, 0x7c, 0x00,
  0x10, 0x30, 0x30, 0x30, 0x38, 0x38, 0x38, 0x00,
  0x40, 0xc2, 0xc2, 0xc2, 0xe6, 0x7e, 0x3c, 0x00,
};

#define W_P (GAME_USER_RAM_TILES_COUNT + 0)
#define W_R (GAME_USER_RAM_TILES_COUNT + 1)
#define W_E (GAME_USER_RAM_TILES_COUNT + 2)
#define W_S (GAME_USER_RAM_TILES_COUNT + 3)
#define W_T (GAME_USER_RAM_TILES_COUNT + 4)
#define W_A (GAME_USER_RAM_TILES_COUNT + 5)
#define W_F (GAME_USER_RAM_TILES_COUNT + 6)
#define W_O (GAME_USER_RAM_TILES_COUNT + 7)
#define W_N (GAME_USER_RAM_TILES_COUNT + 8)
#define W_X (GAME_USER_RAM_TILES_COUNT + 9)
#define W_C (GAME_USER_RAM_TILES_COUNT + 10)
#define W_I (GAME_USER_RAM_TILES_COUNT + 11)
#define W_U (GAME_USER_RAM_TILES_COUNT + 12)
// For Epic Win, a W gets loaded into the 'X' position
#define W_W (GAME_USER_RAM_TILES_COUNT + 9)

const uint8_t pgm_W_PRESS_START[] PROGMEM = { RAM_TILES_COUNT, W_P, W_R, W_E, W_S, W_S, RAM_TILES_COUNT, W_S, W_T, W_A, W_R, W_T, RAM_TILES_COUNT, W_F, W_O, W_R, RAM_TILES_COUNT, W_N, W_E, W_X, W_T, RAM_TILES_COUNT, W_C, W_I, W_R, W_C, W_U, W_I, W_T };
const uint8_t pgm_W_EPIC_WIN[] PROGMEM = { W_P, W_R, W_E, W_S, W_S, RAM_TILES_COUNT, W_S, W_T, W_A, W_R, W_T, RAM_TILES_COUNT, W_F, W_O, W_R, RAM_TILES_COUNT, W_E, W_P, W_I, W_C, RAM_TILES_COUNT, W_W, W_I, W_N };

const uint8_t fade[] PROGMEM = { 0x09, 0x12, 0x1B, 0x24, 0x2D, 0x36, 0x3F };
const uint8_t win_fade[] PROGMEM = { 0x07, 0x1F, 0x3F, 0x38, 0xC8, 0x8C };

void CancelStartAdvancesLevel(void)
{
  if (startAdvancesLevel) {
    for (uint8_t i = HAND_START_X + MAP_ADDTOGRID_WIDTH; i < HAND_START_X + sizeof(pgm_W_PRESS_START); ++i)
      SetTile(i, HAND_START_Y - 2, TILE_BACKGROUND);
    DrawMap(HAND_START_X, HAND_START_Y - 2, map_addtogrid);
    SetUserRamTilesCount(GAME_USER_RAM_TILES_COUNT);
    startAdvancesLevel = false;
  }
}

void BoardChanged(void)
{
  //cli();
  //__asm__ __volatile__ ("wdr");

  // The algorithm works with or without pruning the board first
  // Change the 1 to a 0 to experiment with the runtimes of each
#if 1
  PruneBoard(PRUNEBOARD_FLAG_NORMAL);
#else
  for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x)
      pruned_board[y][x] = board[y][x] & PIECE_MASK;
#endif

  memset(pruned_netlist, 0, sizeof(pruned_netlist));
  /* for (uint8_t i = 0; i < 8; ++i) // set the diagonals, not strictly necessary */
  /*   pruned_netlist[i][i] = 1; */

  // Keep track of where the pieces of interest are in case their tiles need to be changed
  int8_t vccx = -1;
  int8_t vccy = -1;
  int8_t gndx = -1;
  int8_t gndy = -1;
  int8_t rx = -1;
  int8_t ry = -1;
  int8_t yx = -1;
  int8_t yy = -1;
  int8_t gx = -1;
  int8_t gy = -1;

  // If the switch is on the board, keep track of its switch position for goal-matching purposes
  int8_t switch_position = -1;

  // We have to use the 'board' array, because the LEDs might not exist on 'pruned_board'
  for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x) {
      uint8_t piece = board[y][x] & PIECE_MASK;
      switch (piece) {

        // VCC
      case P_VCC_T:
      case P_VCC_R:
      case P_VCC_B:
      case P_VCC_L:
        vccx = x;
        vccy = y;
        break;

        // GND
      case P_GND_LTR:
      case P_GND_TRB:
      case P_GND_RBL:
      case P_GND_BLT:
        gndx = x;
        gndy = y;
        break;

        // SW1
      case P_SW1_BL:
      case P_SW1_LT:
      case P_SW1_TR:
      case P_SW1_RB:
        switch_position = 1;
        break;

        // RED LED
      case P_RLED_AT_CL:
      case P_RLED_AR_CT:
      case P_RLED_AB_CR:
      case P_RLED_AL_CB:
        rx = x;
        ry = y;
        break;

        // SW2
      case P_SW2_BT:
      case P_SW2_LR:
      case P_SW2_TB:
      case P_SW2_RL:
        switch_position = 2;
        break;

        // YELLOW LED
      case P_YLED_AT_CB:
      case P_YLED_AR_CL:
      case P_YLED_AB_CT:
      case P_YLED_AL_CR:
        yx = x;
        yy = y;
        break;

        // SW3
      case P_SW3_BR:
      case P_SW3_LB:
      case P_SW3_TL:
      case P_SW3_RT:
        switch_position = 3;
        break;

        // GREEN LED
      case P_GLED_AT_CR:
      case P_GLED_AR_CB:
      case P_GLED_AB_CL:
      case P_GLED_AL_CT:
        gx = x;
        gy = y;
        break;
      }
    }

  // Find where all the pieces of interest are on 'pruned_board', and call SimulateElectrons
  // from that x, y, and direction the electrons need to go into the adjacent piece
  for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x) {
      uint8_t piece = pruned_board[y][x];
      switch (piece) {

        // VCC
      case P_VCC_T:
        SimulateElectrons(NL_VV, x, y - 1, D_IN_B);
        break;
      case P_VCC_R:
        SimulateElectrons(NL_VV, x + 1, y, D_IN_L);
        break;
      case P_VCC_B:
        SimulateElectrons(NL_VV, x, y + 1, D_IN_T);
        break;
      case P_VCC_L:
        SimulateElectrons(NL_VV, x - 1, y, D_IN_R);
        break;

        // RED LED
      case P_RLED_AT_CL:
        SimulateElectrons(NL_RA, x, y - 1, D_IN_B);
        SimulateElectrons(NL_RC, x - 1, y, D_IN_R);
        break;
      case P_RLED_AR_CT:
        SimulateElectrons(NL_RA, x + 1, y, D_IN_L);
        SimulateElectrons(NL_RC, x, y - 1, D_IN_B);
        break;
      case P_RLED_AB_CR:
        SimulateElectrons(NL_RA, x, y + 1, D_IN_T);
        SimulateElectrons(NL_RC, x + 1, y, D_IN_L);
        break;
      case P_RLED_AL_CB:
        SimulateElectrons(NL_RA, x - 1, y, D_IN_R);
        SimulateElectrons(NL_RC, x, y + 1, D_IN_T);
        break;

        // YELLOW LED
      case P_YLED_AT_CB:
        SimulateElectrons(NL_YA, x, y - 1, D_IN_B);
        SimulateElectrons(NL_YC, x, y + 1, D_IN_T);
        break;
      case P_YLED_AR_CL:
        SimulateElectrons(NL_YA, x + 1, y, D_IN_L);
        SimulateElectrons(NL_YC, x - 1, y, D_IN_R);
        break;
      case P_YLED_AB_CT:
        SimulateElectrons(NL_YA, x, y + 1, D_IN_T);
        SimulateElectrons(NL_YC, x, y - 1, D_IN_B);
        break;
      case P_YLED_AL_CR:
        SimulateElectrons(NL_YA, x - 1, y, D_IN_R);
        SimulateElectrons(NL_YC, x + 1, y, D_IN_L);
        break;

        // GREEN LED
      case P_GLED_AT_CR:
        SimulateElectrons(NL_GA, x, y - 1, D_IN_B);
        SimulateElectrons(NL_GC, x + 1, y, D_IN_L);
        break;
      case P_GLED_AR_CB:
        SimulateElectrons(NL_GA, x + 1, y, D_IN_L);
        SimulateElectrons(NL_GC, x, y + 1, D_IN_T);
        break;
      case P_GLED_AB_CL:
        SimulateElectrons(NL_GA, x, y + 1, D_IN_T);
        SimulateElectrons(NL_GC, x - 1, y, D_IN_R);
        break;
      case P_GLED_AL_CT:
        SimulateElectrons(NL_GA, x - 1, y, D_IN_R);
        SimulateElectrons(NL_GC, x, y - 1, D_IN_B);
        break;

      }
    }

  // print netlist
#if defined(OPTION_DEBUG_NETLIST_MATRIX)
  UZEMC = '\n';
  for (uint8_t y = 0; y < 8; ++y) {
    for (uint8_t x = 0; x < 8; ++x) {
     UZEMC = pruned_netlist[y][x] ? '1' : '0'; UZEMC = ' ';
    }
    UZEMC = '\n';
  }
#endif

  // Pack the netlist into a single 27 bit number
  const uint8_t packedNetlistY[] =
    {
     NL_GA,
     NL_YC, NL_YC,
     NL_YA, NL_YA, NL_YA,
     NL_RC, NL_RC, NL_RC, NL_RC,
     NL_RA, NL_RA, NL_RA, NL_RA, NL_RA,
     NL_00, NL_00, NL_00, NL_00, NL_00, NL_00,
     NL_VV, NL_VV, NL_VV, NL_VV, NL_VV, NL_VV, /*NL_VV,*/ // highest bit assumed to be 0, we don't store short circuits
    };
  const uint8_t packedNetlistX[] =
    {
     NL_GC,
     NL_GC, NL_GA,
     NL_GC, NL_GA, NL_YC,
     NL_GC, NL_GA, NL_YC, NL_YA,
     NL_GC, NL_GA, NL_YC, NL_YA, NL_RC,
     NL_GC, NL_GA, NL_YC, NL_YA, NL_RC, NL_RA,
     NL_GC, NL_GA, NL_YC, NL_YA, NL_RC, NL_RA, /*NL_VV,*/ // highest bit assumed to be 0, we don't store short circuits
    };

  bool isShort = false;
    // If we always check for a short, the 28th bit can always be 0, and then we only need to store 2^27 3-bit LED states
  if (pruned_netlist[NL_00][NL_VV]) {
    isShort = true;
    TriggerNote(SFX_CHANNEL, SFX_ZAP, SFX_SPEED_ZAP, SFX_VOL_ZAP);

    // If there is a short, draw the + and - of the VCC and GND tokens in red
    if (vccx >= 0 && vccy >= 0)
      SetTile(BOARD_START_X + vccx * BOARD_H_SPACING + 1, BOARD_START_Y + vccy * BOARD_V_SPACING + 1, TILE_SHORT_VCC);
    if (gndx >= 0 && gndy >= 0) {
      uint8_t piece = board[gndy][gndx] & PIECE_MASK;
      if (piece == P_GND_LTR || piece == P_GND_RBL)
        SetTile(BOARD_START_X + gndx * BOARD_H_SPACING + 1, BOARD_START_Y + gndy * BOARD_V_SPACING + 1, TILE_SHORT_GND);
      else
        SetTile(BOARD_START_X + gndx * BOARD_H_SPACING + 1, BOARD_START_Y + gndy * BOARD_V_SPACING + 1, TILE_SHORT_GND_ROT);
    }

  } else {
    // If there is not a short, ensure the + and - of the VCC and GND tokens is white
    if (vccx >= 0 && vccy >= 0)
      SetTile(BOARD_START_X + vccx * BOARD_H_SPACING + 1, BOARD_START_Y + vccy * BOARD_V_SPACING + 1, TILE_VCC);
    if (gndx >= 0 && gndy >= 0) {
      uint8_t piece = board[gndy][gndx] & PIECE_MASK;
      if (piece == P_GND_LTR || piece == P_GND_RBL)
        SetTile(BOARD_START_X + gndx * BOARD_H_SPACING + 1, BOARD_START_Y + gndy * BOARD_V_SPACING + 1, TILE_GND);
      else
        SetTile(BOARD_START_X + gndx * BOARD_H_SPACING + 1, BOARD_START_Y + gndy * BOARD_V_SPACING + 1, TILE_GND_ROT);
    }
  }

  typedef union {
    uint32_t dword;
    uint8_t byte[4];
  } dword;

  dword packed_netlist;
  packed_netlist.dword = 0;
  if (!isShort) {
    uint32_t bitmask = 1;
    for (uint8_t i = 0; i < 27; ++i) {
      if (pruned_netlist[packedNetlistY[i]][packedNetlistX[i]])
        packed_netlist.dword |= bitmask;
      bitmask <<= 1;
    }
  }
  // Output the netlist
  /* uint8_t bits26_17 = (uint8_t)((packed_netlist.dword & 0xFF000000) >> 24); */
  /* uint8_t bits23_16 = (uint8_t)((packed_netlist.dword & 0x00FF0000) >> 16); */
  /* uint8_t bits15_8 = (uint8_t)((packed_netlist.dword & 0x0000FF00) >> 8); */
  /* uint8_t bits7_0 = (uint8_t)(packed_netlist.dword & 0x000000FF); */
  /* UZEMC = '0'; UZEMC = 'x'; UZEMH = bits26_17; UZEMH = bits23_16; UZEMH = bits15_8; UZEMH = bits7_0; UZEMC = '\n'; */

  //__asm__ __volatile__ ("wdr");
  //sei();

  static uint32_t prev_netlist = 0;
  if (packed_netlist.dword != prev_netlist) {
    prev_netlist = packed_netlist.dword;
#if defined(OPTION_DEBUG_NETLIST)
    UZEMC = '0'; UZEMC = 'x'; UZEMH = packed_netlist.byte[3]; UZEMH = packed_netlist.byte[2]; UZEMH = packed_netlist.byte[1]; UZEMH = packed_netlist.byte[0]; UZEMC = '\n';
#endif
  }

  //cli();
  //__asm__ __volatile__ ("wdr");
  uint8_t ledStates = ConsultOracle(packed_netlist.dword);
  //__asm__ __volatile__ ("wdr");
  //sei();
  uint8_t goalStates;

  WaitVsync(1); // Prevent tearing of the LED tiles under the mouse cursor if the oracle took too long and the LED state needs to be changed

  if (rx >= 0 && ry >= 0) {
    if (ledStates & R_BIT) {
      uint8_t piece = pruned_board[ry][rx];
      DrawMap(BOARD_START_X + rx * BOARD_H_SPACING, BOARD_START_Y + ry * BOARD_V_SPACING, LedOnMapName(piece));
    } else {
      uint8_t piece = board[ry][rx] & PIECE_MASK;
      DrawMap(BOARD_START_X + rx * BOARD_H_SPACING, BOARD_START_Y + ry * BOARD_V_SPACING, MapName(piece));
    }
  }

  if (yx >=0 && yy >= 0) {
    if (ledStates & Y_BIT) {
      uint8_t piece = pruned_board[yy][yx];
      DrawMap(BOARD_START_X + yx * BOARD_H_SPACING, BOARD_START_Y + yy * BOARD_V_SPACING, LedOnMapName(piece));
    } else {
      uint8_t piece = board[yy][yx] & PIECE_MASK;
      DrawMap(BOARD_START_X + yx * BOARD_H_SPACING, BOARD_START_Y + yy * BOARD_V_SPACING, MapName(piece));
    }
  }

  if (gx >= 0 && gy >= 0) {
    if (ledStates & G_BIT) {
      uint8_t piece = pruned_board[gy][gx];
      DrawMap(BOARD_START_X + gx * BOARD_H_SPACING, BOARD_START_Y + gy * BOARD_V_SPACING, LedOnMapName(piece));
    } else {
      uint8_t piece = board[gy][gx] & PIECE_MASK;
      DrawMap(BOARD_START_X + gx * BOARD_H_SPACING, BOARD_START_Y + gy * BOARD_V_SPACING, MapName(piece));
    }
  }

#if defined(OPTION_DEBUG_DISPLAY_PRUNED_BOARD)
  // Display pruned_board
  for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x) {
      uint8_t piece = pruned_board[y][x];
      DrawMap(BOARD_START_X + BOARD_WIDTH * BOARD_H_SPACING + x * BOARD_H_SPACING, BOARD_START_Y + y * BOARD_V_SPACING, MapName(piece));
    }
#endif

  // See if we meet the rules
  for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x)
      pruned_board[y][x] = board[y][x] & PIECE_MASK;

  bool meetsRules = true;

  // The rules can't be met if there is a short circuit
  if (isShort) {
    meetsRules = false;
    goto skip_expensive_rule_checks;
  }

  // The rules can't be met if you have a piece picked up
  if (old_piece != -1) {
    meetsRules = false;
    goto skip_expensive_rule_checks;
  }

  // The rules can't be met if you have pieces in your hand
  for (uint8_t y = 0; y < HAND_HEIGHT; ++y)
    for (uint8_t x = 0; x < HAND_WIDTH; ++x)
      if (hand[y][x] != 0) {
        meetsRules = false;
        goto skip_expensive_rule_checks;
      }

  // The rules can't be met if there are invalid "loose ends"
  if (PruneBoard(PRUNEBOARD_FLAG_MEETS_RULES) == false)
    meetsRules = false;

 skip_expensive_rule_checks:

  if (boardChanged && startAdvancesLevel)
    CancelStartAdvancesLevel();

  // If the goal doesn't match, we haven't met the rules yet
  // When the goal matches and we have met the rules, play the win sound,
  // disable input for a bit (except the start button) and replace the
  // words "ADD TO GRID" with "PRESS START FOR NEXT CIRCUIT"
  // Might even be able to leave those words on screen, unless you pick
  // up a tile, in which case, it should revert back to "ADD TO GRID"
  // and the win condition should reset. As long as you just move the
  // switch position, the words should remain, and the win tone should
  // not play more than once.
  //
  // 3 RAM tiles for level display, 1 for challenge color
  // 16 for dragging and dropping
  // PRESTAFONXCIU
  // As long as we aren't dragging and dropping anything during the win
  // condition (which we shouldn't be) we can use those 16 ram tiles to
  // display that text.

  // Even though a particular configuration doesn't meet the rules, we
  // still want to put a checkbox next to a goal where the LED state
  // matches, even though it's not a win condition
  goalStates = GoalStatesForCurrentLevel(switch_position);

#if defined(OPTION_DEBUG_DISPLAY_GOAL_STATES)
  UZEMC = 'G'; UZEMC = 'S'; UZEMC = ':'; UZEMH = goalStates; UZEMC = '\n';
#endif

  if (CurrentLevelHasSwitch()) {
    if (boardChanged)
      for (uint8_t i = 0; i < 3; ++i) // change tiles here to avoid flicker
        SetTile(GOAL_START_X - 2, GOAL_START_Y + GOAL_V_SPACING * i + 1, TILE_FOREGROUND);

    if (switch_position != -1) {
      if (ledStates == goalStates) {
        met_goal[switch_position - 1] = true;
        SetTile(GOAL_START_X - 2, GOAL_START_Y + GOAL_V_SPACING * (switch_position - 1) + 1, TILE_GOAL_MET);
      } else {
        met_goal[switch_position - 1] = false;
        SetTile(GOAL_START_X - 2, GOAL_START_Y + GOAL_V_SPACING * (switch_position - 1) + 1, TILE_GOAL_UNMET);
      }
    }
  } else {
    if (ledStates == goalStates) {
      SetTile(GOAL_START_X - 2, GOAL_START_Y + 1, TILE_GOAL_MET);
    } else {
      SetTile(GOAL_START_X - 2, GOAL_START_Y + 1, TILE_GOAL_UNMET);
    }
  }

  bool levelComplete = false;
  if (CurrentLevelHasSwitch()) {
    // Ensure all goals have been met along with the rules
    if (met_goal[0] && met_goal[1] && met_goal[2] && meetsRules) {
      SetTile(GOAL_START_X - 2, meetsRulesY, TILE_GOAL_MET);
      levelComplete = true;
    } else {
      SetTile(GOAL_START_X - 2, meetsRulesY, TILE_GOAL_UNMET);
    }
  } else {
    if ((ledStates == goalStates) && meetsRules) {
      SetTile(GOAL_START_X - 2, meetsRulesY, TILE_GOAL_MET);
      levelComplete = true;
    } else {
      SetTile(GOAL_START_X - 2, meetsRulesY, TILE_GOAL_UNMET);
    }
  }

  if (boardChanged)
    boardChanged = false;
  if (switchChanged)
    switchChanged = false;

  if (levelComplete) {
    if (!startAdvancesLevel) {
      // Play the win sound
      TriggerNote(SFX_CHANNEL, SFX_WIN, SFX_SPEED_WIN, SFX_VOL_WIN);

      // Erase the ADD TO GRID
      for (uint8_t i = HAND_START_X; i < HAND_START_X + MAP_ADDTOGRID_WIDTH; ++i)
        SetTile(i, HAND_START_Y - 2, TILE_BACKGROUND);

      // If you completed an uncompleted level, saving to EEPROM might take a while
      bool maybeCompletedLastUncompletedLevel = false;
      if (!BitArray_readBit(currentLevel)) {
        maybeCompletedLastUncompletedLevel = true;
        BitArray_setBit(currentLevel);
        SaveHighScore(bitarray);
      }
      if (maybeCompletedLastUncompletedLevel && CompletedGame())
        startWinsGame = true;

#if defined(OPTION_HIDE_CURSOR_DURING_LEVEL_COMPLETE)
      // Hide the cursor
      sprites[MAX_SPRITES - 1].flags |= SPRITE_OFF;
#endif

      // Print a message showing how to advance the level
      // This takes enough time that this needs to happen after TriggerNote plays the win sound, because in order
      // to trigger the win, a drop or rotate sound had to play before, and it sounds too weird with a pause between

#define VSYNC_PER_FADE 3
#define VSYNC_PER_WIN_FADE 3

      SetUserRamTilesCount(GAME_USER_RAM_TILES_COUNT + 13);
      if (startWinsGame) {
        RamFont_Load(rf_win, GAME_USER_RAM_TILES_COUNT, sizeof(rf_win) / 8, 0x00, 0x00);
        RamFont_Load(rf_help + ('W' - 'A') * 8, W_W, 1, 0x00, 0x00);
        RamFont_Print(HAND_START_X + 3, HAND_START_Y - 2, pgm_W_EPIC_WIN, sizeof(pgm_W_EPIC_WIN));

        for (uint8_t i = 0; i < 3; ++i) {
          uint8_t col = 0;
          for (;;) {
            WaitVsync(VSYNC_PER_WIN_FADE);
            RamFont_Load(rf_win, GAME_USER_RAM_TILES_COUNT, sizeof(rf_win) / 8, pgm_read_byte(&win_fade[col]), 0x00);
            RamFont_Load(rf_help + ('W' - 'A') * 8, W_W, 1, pgm_read_byte(&win_fade[col]), 0x00);
            if ((++col == sizeof(win_fade)) || ReadJoypad(0) & BTN_START)
              break;
          }
          if (ReadJoypad(0) & BTN_START)
            break;
        }
        RamFont_Load(rf_win, GAME_USER_RAM_TILES_COUNT, sizeof(rf_win) / 8, 0xF0, 0x00);
        RamFont_Load(rf_help + ('W' - 'A') * 8, W_W, 1, 0xF0, 0x00);
      } else {
        RamFont_Load(rf_win, GAME_USER_RAM_TILES_COUNT, sizeof(rf_win) / 8, 0x00, 0x00);
        RamFont_Print(HAND_START_X, HAND_START_Y - 2, pgm_W_PRESS_START, sizeof(pgm_W_PRESS_START));

        uint8_t col = 0;
        for (;;) {
          WaitVsync(VSYNC_PER_FADE);
          RamFont_Load(rf_win, GAME_USER_RAM_TILES_COUNT, sizeof(rf_win) / 8, pgm_read_byte(&fade[col]), 0x00);
          if ((++col == sizeof(fade)) || ReadJoypad(0) & BTN_START)
            break;
        }
      }

#if defined(OPTION_HIDE_CURSOR_DURING_LEVEL_COMPLETE)
      // Show the cursor
      sprites[MAX_SPRITES - 1].flags &= (sprites[MAX_SPRITES - 1].flags ^ SPRITE_OFF);
#endif
    }

    startAdvancesLevel = true;
  }

}

#define T_(x) ((x) - 'A')

const uint8_t pgm_T_CIRCUIT[] PROGMEM = { T_('C'), T_('I'), T_('R'), T_('C'), T_('U'), T_('I'), T_('T') };
const uint8_t pgm_T_PUZZLE[] PROGMEM = { T_('P'), T_('U'), T_('Z'), T_('Z'), T_('L'), T_('E') };
const uint8_t pgm_T_START_GAME[] PROGMEM = { T_('S'), T_('T'), T_('A'), T_('R'), T_('T'), RAM_TILES_COUNT, T_('G'), T_('A'), T_('M'), T_('E') };
const uint8_t pgm_T_HOW_TO_PLAY[] PROGMEM = { T_('H'), T_('O'), T_('W'), RAM_TILES_COUNT, T_('T'), T_('O'), RAM_TILES_COUNT, T_('P'), T_('L'), T_('A'), T_('Y') };
const uint8_t pgm_T_REVIEW_ENDING[] PROGMEM = { T_('R'), T_('E'), T_('V'), T_('I'), T_('E'), T_('W'), RAM_TILES_COUNT, T_('E'), T_('N'), T_('D'), T_('I'), T_('N'), T_('G') };
const uint8_t pgm_T_UZEBOX_GAME[] PROGMEM = { T_('U'), T_('Z'), T_('E'), T_('B'), T_('O'), T_('X'), RAM_TILES_COUNT, T_('G'), T_('A'), T_('M'), T_('E'), RAM_TILES_COUNT, T_('F'), T_('J'), T_('Q'), T_('J'), T_('Q'), RAM_TILES_COUNT, T_('M'), T_('A'), T_('T'), T_('T'), RAM_TILES_COUNT, T_('P'), T_('A'), T_('N'), T_('D'), T_('I'), T_('N'), T_('A') };
const uint8_t pgm_T_INVENTED_BY[] PROGMEM = { T_('I'), T_('N'), T_('V'), T_('E'), T_('N'), T_('T'), T_('E'), T_('D'), RAM_TILES_COUNT, T_('B'), T_('Y'), RAM_TILES_COUNT, T_('D'), T_('A'), T_('V'), T_('I'), T_('D'), RAM_TILES_COUNT, T_('Y'), T_('A'), T_('K'), T_('O'), T_('S') };
const uint8_t pgm_T_PUZZLES_BY1[] PROGMEM = { T_('P'), T_('U'), T_('Z'), T_('Z'), T_('L'), T_('E'), T_('S'), RAM_TILES_COUNT, T_('B'), T_('Y'), RAM_TILES_COUNT, T_('W'), T_('E'), T_('I'), T_('\\'), T_('H'), T_('W'), T_('A'), RAM_TILES_COUNT, T_('H'), T_('U'), T_('A'), T_('N'), T_('G'), T_('[') };
const uint8_t pgm_T_PUZZLES_BY2[] PROGMEM = { T_('T'), T_('Y'), T_('L'), T_('E'), T_('R'), RAM_TILES_COUNT, T_('S'), T_('O'), T_('M'), T_('E'), T_('R') };
const uint8_t pgm_T_CONGRATULATIONS[] PROGMEM = { T_('C'), T_('O'), T_('N'), T_('G'), T_('R'), T_('A'), T_('T'), T_('U'), T_('L'), T_('A'), T_('T'), T_('I'), T_('O'), T_('N'), T_('S') };
const uint8_t pgm_T_YOU_SOLVED[] PROGMEM = { T_('Y'), T_('O'), T_('U'), RAM_TILES_COUNT, T_('S'), T_('O'), T_('L'), T_('V'), T_('E'), T_('D'), RAM_TILES_COUNT, T_('A'), T_('L'), T_('L'), RAM_TILES_COUNT, T_('C'), T_('I'), T_('R'), T_('C'), T_('U'), T_('I'), T_('T'), T_('S') };

// Generated using ~/uzebox/bin/bin2hex data/HELP2.TXT (and terminating with a 0x00)
const char HELP_TXT[] PROGMEM = {
  0x20, 0x46, 0x4f, 0x52, 0x20, 0x45, 0x41, 0x43, 0x48, 0x20, 0x43, 0x49,
  0x52, 0x43, 0x55, 0x49, 0x54, 0x2c, 0x20, 0x41, 0x52, 0x52, 0x41, 0x4e,
  0x47, 0x45, 0x20, 0x54, 0x48, 0x45, 0x0a, 0x20, 0x54, 0x4f, 0x4b, 0x45,
  0x4e, 0x53, 0x20, 0x54, 0x4f, 0x20, 0x46, 0x4f, 0x52, 0x4d, 0x20, 0x41,
  0x20, 0x43, 0x4f, 0x4e, 0x54, 0x49, 0x4e, 0x55, 0x4f, 0x55, 0x53, 0x0a,
  0x20, 0x50, 0x41, 0x54, 0x48, 0x57, 0x41, 0x59, 0x20, 0x46, 0x52, 0x4f,
  0x4d, 0x20, 0x56, 0x43, 0x43, 0x20, 0x54, 0x4f, 0x20, 0x47, 0x4e, 0x44,
  0x20, 0x54, 0x48, 0x41, 0x54, 0x0a, 0x20, 0x4c, 0x49, 0x47, 0x48, 0x54,
  0x53, 0x20, 0x55, 0x50, 0x20, 0x54, 0x48, 0x45, 0x20, 0x44, 0x45, 0x53,
  0x49, 0x47, 0x4e, 0x41, 0x54, 0x45, 0x44, 0x20, 0x4c, 0x45, 0x44, 0x53,
  0x2e, 0x0a, 0x0a, 0x20, 0x41, 0x4c, 0x4c, 0x20, 0x54, 0x4f, 0x4b, 0x45,
  0x4e, 0x53, 0x20, 0x4d, 0x55, 0x53, 0x54, 0x20, 0x42, 0x45, 0x20, 0x55,
  0x53, 0x45, 0x44, 0x2c, 0x20, 0x41, 0x4e, 0x44, 0x0a, 0x20, 0x54, 0x48,
  0x45, 0x52, 0x45, 0x20, 0x43, 0x41, 0x4e, 0x20, 0x42, 0x45, 0x20, 0x4e,
  0x4f, 0x20, 0x4c, 0x4f, 0x4f, 0x53, 0x45, 0x20, 0x45, 0x4e, 0x44, 0x53,
  0x2c, 0x0a, 0x20, 0x45, 0x58, 0x43, 0x45, 0x50, 0x54, 0x20, 0x4f, 0x4e,
  0x20, 0x54, 0x48, 0x45, 0x20, 0x53, 0x57, 0x49, 0x54, 0x43, 0x48, 0x20,
  0x41, 0x4e, 0x44, 0x20, 0x47, 0x4e, 0x44, 0x0a, 0x20, 0x54, 0x4f, 0x4b,
  0x45, 0x4e, 0x53, 0x2e, 0x20, 0x54, 0x48, 0x45, 0x52, 0x45, 0x20, 0x49,
  0x53, 0x20, 0x41, 0x20, 0x42, 0x55, 0x49, 0x4c, 0x54, 0x4A, 0x49, 0x4e,
  0x0a, 0x20, 0x52, 0x55, 0x4c, 0x45, 0x20, 0x43, 0x48, 0x45, 0x43, 0x4b,
  0x45, 0x52, 0x20, 0x54, 0x4f, 0x20, 0x48, 0x45, 0x4c, 0x50, 0x2e, 0x0a,
  0x0a, 0x20, 0x54, 0x48, 0x45, 0x20, 0x4e, 0x4f, 0x52, 0x4d, 0x41, 0x4c,
  0x20, 0x52, 0x55, 0x4c, 0x45, 0x53, 0x20, 0x4f, 0x46, 0x20, 0x45, 0x4c,
  0x45, 0x43, 0x54, 0x52, 0x49, 0x43, 0x41, 0x4c, 0x0a, 0x20, 0x43, 0x55,
  0x52, 0x52, 0x45, 0x4e, 0x54, 0x20, 0x41, 0x4e, 0x44, 0x20, 0x4c, 0x45,
  0x44, 0x53, 0x20, 0x41, 0x50, 0x50, 0x4c, 0x59, 0x2e, 0x20, 0x41, 0x56,
  0x4f, 0x49, 0x44, 0x0a, 0x20, 0x43, 0x52, 0x45, 0x41, 0x54, 0x49, 0x4e,
  0x47, 0x20, 0x53, 0x48, 0x4f, 0x52, 0x54, 0x20, 0x43, 0x49, 0x52, 0x43,
  0x55, 0x49, 0x54, 0x53, 0x2e, 0x00
};

void EpicWin(BUTTON_INFO* buttons)
{
  ClearVram();
  SetTileTable(win_tileset);

  // If the song is not playing, unconditionally start it from the beginning
  if (!IsSongPlaying()) {
    StopSong();
    InitMusicPlayer(patches);
    StartSong(midisong);
  }

  startWinsGame = false;

  SetUserRamTilesCount(RAM_TILES_COUNT);
  RamFont_Load(rf_help, 0, sizeof(rf_help) / 8, 0x00, 0xF0);
  RamFont_Print((SCREEN_TILES_H - sizeof(pgm_T_CONGRATULATIONS)) / 2, 10, pgm_T_CONGRATULATIONS, sizeof(pgm_T_CONGRATULATIONS));
  RamFont_Print((SCREEN_TILES_H - sizeof(pgm_T_YOU_SOLVED)) / 2, 13, pgm_T_YOU_SOLVED, sizeof(pgm_T_YOU_SOLVED));

  uint8_t frameCounter = 0;
  for (;;) {
    WaitVsync(1);
    if (frameCounter == 0)
      DrawMap((SCREEN_TILES_H - 5) / 2, SCREEN_TILES_V - 8, map_win_left);

    if (frameCounter == 17)
      DrawMap((SCREEN_TILES_H - 5) / 2, SCREEN_TILES_V - 8, map_win_right);

        buttons->prev = buttons->held;
        buttons->held = ReadJoypad(0);
        buttons->pressed = buttons->held & (buttons->held ^ buttons->prev);
        buttons->released = buttons->prev & (buttons->held ^ buttons->prev);

    if (buttons->pressed & BTN_START)
      break;

    if (buttons->pressed & BTN_SELECT) {
      if (IsSongPlaying())
        MyStopSong();
      else
        MyResumeSong();
    }

    ++frameCounter;
    if (frameCounter == 35)
      frameCounter = 0;
  }

  ClearVram();
  SetTileTable(tileset);

  SetUserRamTilesCount(GAME_USER_RAM_TILES_COUNT);
  StopSong();
  InitMusicPlayer(patches);
  StartSong(midisong);
  StopSong();
  TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);
}

int main()
{
  ClearVram();
  SetTileTable(titlescreen);

  BUTTON_INFO buttons;
  memset(&buttons, 0, sizeof(BUTTON_INFO));
  InitMusicPlayer(patches);

  StartSong(midisong);
  StopSong();

  LoadHighScore(bitarray);

#if defined(OPTION_DEBUG_EPIC_WIN)
  // Pretend we completed all the levels except one
  for (uint8_t i = 1; i < 61; ++i)
    BitArray_setBit(i);
  BitArray_clearBit(2);
#endif

  // When the game is first launched, play the switch sound
  TriggerNote(SFX_CHANNEL, SFX_SWITCH, SFX_SPEED_SWITCH, SFX_VOL_SWITCH);

 title_screen:
  ClearVram();
  SetTileTable(titlescreen);

  // Load the entire alphabet + extras
  SetUserRamTilesCount(RAM_TILES_COUNT);
  RamFont_Load(rf_help, 0, sizeof(rf_help) / 8, 0xFF, 0x00);

  // Since the title screen doesn't use F, J, or Q, load the copyright
  // symbol, the digit 2, and the digit 0 into those positions. Also
  // load a special - into the '\\' position
  RamFont_Load(rf_title_extra, T_('F'), 1, 0xFF, 0x00);
  RamFont_Load(rf_title_extra + 8, T_('J'), 1, 0xFF, 0x00);
  RamFont_Load(rf_title_extra + 16, T_('Q'), 1, 0xFF, 0x00);
  RamFont_Load(rf_title_extra + 24, T_('\\'), 1, 0xFF, 0x00);

  DrawMap(14, 3, map_title_big);
  RamFont_Print(6, 5, pgm_T_CIRCUIT, sizeof(pgm_T_CIRCUIT));
  RamFont_Print(18, 8, pgm_T_PUZZLE, sizeof(pgm_T_PUZZLE));
  RamFont_Print(11, 14, pgm_T_START_GAME, sizeof(pgm_T_START_GAME));
  RamFont_Print(11, 16, pgm_T_HOW_TO_PLAY, sizeof(pgm_T_HOW_TO_PLAY));

  // This secret menu option will only be revealed when you have beaten all the levels
  uint8_t max_selection;
  if (CompletedGame()) {
    RamFont_Print(11, 18, pgm_T_REVIEW_ENDING, sizeof(pgm_T_REVIEW_ENDING));
    max_selection = 2;
  } else {
    max_selection = 1;
  }

  RamFont_Print(1, 22, pgm_T_UZEBOX_GAME, sizeof(pgm_T_UZEBOX_GAME));
  RamFont_Print(3, 24, pgm_T_INVENTED_BY, sizeof(pgm_T_INVENTED_BY));
  RamFont_Print(4, 25, pgm_T_PUZZLES_BY1, sizeof(pgm_T_PUZZLES_BY1));
  RamFont_Print(15, 26, pgm_T_PUZZLES_BY2, sizeof(pgm_T_PUZZLES_BY2));

  /* BEGIN TITLE SCREEN SCOPE */ {
    int8_t prev_selection;
    int8_t selection = 0;

#define TILE_T_BG 0
#define TILE_T_SELECTION 1

    for (;;) {
      // Draw the menu selection indicator
      SetTile(9, 14 + 2 * selection, TILE_T_SELECTION);
      prev_selection = selection;

      // Read the current state of the player's controller
      buttons.prev = buttons.held;
      buttons.held = ReadJoypad(0);
      buttons.pressed = buttons.held & (buttons.held ^ buttons.prev);
      buttons.released = buttons.prev & (buttons.held ^ buttons.prev);

      if (buttons.pressed & BTN_START)
        break;

      if (buttons.pressed & BTN_UP) {
        if (selection > 0) {
          selection--;
          TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);
          SetTile(9, 14 + 2 * prev_selection, TILE_T_BG);
          SetTile(9, 14 + 2 * selection, TILE_T_SELECTION);
          prev_selection = selection;
        }
      } else if (buttons.pressed & BTN_DOWN) {
        if (selection < max_selection) {
          selection++;
          TriggerNote(SFX_CHANNEL, SFX_MOUSE_UP, SFX_SPEED_MOUSE_UP, SFX_VOL_MOUSE_UP);
          SetTile(9, 14 + 2 * prev_selection, TILE_T_BG);
          SetTile(9, 14 + 2 * selection, TILE_T_SELECTION);
          prev_selection = selection;
        }
      }

      WaitVsync(1);
    }

    TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);

    if (selection == 0)
      goto start_game;
    else if (selection == 2) {
      EpicWin(&buttons);
      goto title_screen;
    }
  } /* END TITLE SCREEN SCOPE */

  /* BEGIN HOW TO PLAY SCOPE */ {
    ClearVram();
    SetTileTable(tileset);

    // Load the entire alphabet + extras
    SetUserRamTilesCount(RAM_TILES_COUNT);
    RamFont_Load(rf_help, 0, sizeof(rf_help) / 8, 0x00, 0x00);

    int16_t in = 0;
    uint8_t letter = 0;
    uint8_t prev_letter = 0;
    for (uint16_t out = 0; out < SCREEN_TILES_H * SCREEN_TILES_V; ++out, ++in) {
      uint8_t x = out % SCREEN_TILES_H;
      prev_letter = letter;
      letter = pgm_read_byte(&HELP_TXT[in]);
      uint8_t output;
      switch (letter) {
      case 0x00:
        out = SCREEN_TILES_H * SCREEN_TILES_V;
        continue;
        break;
      case 0x0A:
        out += SCREEN_TILES_H + SCREEN_TILES_H - 1 - x;
        if (prev_letter == 0x0A)
          out -= SCREEN_TILES_H;
        continue;
        break;
      case ' ':
        output = RAM_TILES_COUNT;
        break;
      case ',':
        output = '[' - 'A';
        break;
      case '.':
        output = '\\' - 'A';
        break;
      default:
        output = letter - 'A';
      }
      vram[out + SCREEN_TILES_H * 2] = output;
    }

    TriggerNote(SFX_CHANNEL, SFX_ZAP, SFX_SPEED_ZAP, SFX_VOL_ZAP);
    RamFont_SparkleLoad(rf_help, 0, sizeof(rf_help) / 8, 0xFF);

    for (;;) {
      // Read the current state of the player's controller
      buttons.prev = buttons.held;
      buttons.held = ReadJoypad(0);
      buttons.pressed = buttons.held & (buttons.held ^ buttons.prev);
      buttons.released = buttons.prev & (buttons.held ^ buttons.prev);

      if (buttons.pressed & BTN_START) {
        TriggerNote(SFX_CHANNEL, SFX_ZAP, SFX_SPEED_ZAP, SFX_VOL_ZAP);
        RamFont_SparkleLoad(rf_help, 0, sizeof(rf_help) / 8, 0x00);
        goto title_screen;
      }

      WaitVsync(1);
    }
  } /* END HOW TO PLAY SCOPE */

 start_game:
  ClearVram();
  SetTileTable(tileset);
  SetSpritesTileBank(0, mysprites);
  SetSpritesTileBank(1, tileset);
  SetUserRamTilesCount(GAME_USER_RAM_TILES_COUNT);

#if defined(OPTION_PERSIST_MUSIC_PREF)
  if (!BitArray_readBit(0))
#endif
    ResumeSong();

  currentLevel = 1;
  LoadLevel(currentLevel);

  for (;;) {
    WaitVsync(1);

    // Read the current state of the player's controller
    buttons.prev = buttons.held;
    buttons.held = ReadJoypad(0);
    buttons.pressed = buttons.held & (buttons.held ^ buttons.prev);
    buttons.released = buttons.prev & (buttons.held ^ buttons.prev);

    // Allow song to be paused/unpaused without going to the popup menu
    if (buttons.pressed & BTN_SELECT) {
      if (IsSongPlaying())
        MyStopSong();
      else
        MyResumeSong();
    }

    // Move the "mouse" cursor using a fixed point representation that has acceleration
    cursor_update(&cursor, buttons.held);

    // Avoid repeated calculations by performing them once up front, and setting some state variables
    bool cursorIsOverBoard = false;
    bool cursorIsOverHand = false;
    uint8_t tx = sprites[MAX_SPRITES - 1].x / TILE_WIDTH;
    uint8_t ty = sprites[MAX_SPRITES - 1].y / TILE_HEIGHT;
    int8_t x = -1;
    int8_t y = -1;

    // Figure out if the mouse cursor is over the board
    if ((ty >= BOARD_START_Y) && (ty < (BOARD_START_Y + (BOARD_HEIGHT - 1) * BOARD_V_SPACING + TOKEN_HEIGHT)) &&
        (tx >= BOARD_START_X) && (tx < (BOARD_START_X + (BOARD_WIDTH - 1) * BOARD_H_SPACING + TOKEN_WIDTH))) {

      // Calculate the x array index, as if the token width was equal to the horizontal board spacing
      int8_t provisional_x = (tx - BOARD_START_X) / BOARD_H_SPACING;

#if (BOARD_H_SPACING > TOKEN_WIDTH)
      // Determine if we are over the token, or over the spacing
      uint8_t hmod = ((tx - BOARD_START_X) % BOARD_H_SPACING);
      if (hmod < TOKEN_WIDTH)
#endif
        x = provisional_x;

      // Calculate the y array index, as if the token width was equal to the vertical board spacing
      int8_t provisional_y = (ty - BOARD_START_Y) / BOARD_V_SPACING;

#if (BOARD_V_SPACING > TOKEN_HEIGHT)
      // Determine if we are over the token, or over the spacing
      uint8_t vmod = ((ty - BOARD_START_Y) % BOARD_V_SPACING);
      if (vmod < TOKEN_HEIGHT)
#endif
        y = provisional_y;

      if (x != -1 && y != -1)
        cursorIsOverBoard = true; // when this is true, x and y index into the board[][] array where the mouse cursor is

      // Otherwise figure out if the mouse cursor is over the hand (add to grid section)
    } else if ((ty >= HAND_START_Y) && (ty < (HAND_START_Y + (HAND_HEIGHT - 1) * HAND_V_SPACING + TOKEN_HEIGHT)) &&
               (tx >= HAND_START_X) && (tx < (HAND_START_X + (HAND_WIDTH - 1) * HAND_H_SPACING + TOKEN_HEIGHT))) {

      // Calculate the x array index, as if the token width was equal to the horizontal hand spacing
      int8_t provisional_x = (tx - HAND_START_X) / HAND_H_SPACING;

#if (HAND_H_SPACING > TOKEN_WIDTH)
      // Determine if we are over the token, or over the spacing
      uint8_t hmod = ((tx - HAND_START_X) % HAND_H_SPACING);
      if (hmod < TOKEN_WIDTH)
#endif
        x = provisional_x;

      // Calculate the y array index, as if the token width was equal to the vertical hand spacing
      int8_t provisional_y = (ty - HAND_START_Y) / HAND_V_SPACING;

#if (HAND_V_SPACING > TOKEN_HEIGHT)
      // Determine if we are over the token, or over the spacing
      uint8_t vmod = ((ty - HAND_START_Y) % HAND_V_SPACING);
      if (vmod < TOKEN_HEIGHT)
#endif
        y = provisional_y;

      if (x != -1 && y != -1)
        cursorIsOverHand = true; // when this is true, x and y index into the hand[][] array where the mouse cursor is

    }


    // -------------------- UPDATE CONTEXT SENSITIVE HELP --------------------
    if (old_piece != -1) { // if we have a valid piece picked up
      DrawMap(CONTROLS_LR_START_X, CONTROLS_LR_START_Y, map_controls_lr);
      DrawMap(CONTROLS_A_START_X, CONTROLS_A_START_Y, map_controls_a);
    } else if (cursorIsOverBoard) {
      uint8_t piece = board[y][x] & PIECE_MASK;
      if (piece != P_BLANK) {
        bool canRotate = board[y][x] & FLAG_ROTATE;
        bool isLocked = board[y][x] & FLAG_LOCKED;
        if (canRotate || !isLocked)
          DrawMap(CONTROLS_LR_START_X, CONTROLS_LR_START_Y, map_controls_lr);
        else
          DrawMap(CONTROLS_LR_START_X, CONTROLS_LR_START_Y, map_controls_lr_off);

        if (isLocked || canRotate)
          DrawMap(CONTROLS_A_START_X, CONTROLS_A_START_Y, map_controls_a_off);
        else {
          DrawMap(CONTROLS_A_START_X, CONTROLS_A_START_Y, map_controls_a);
        }
      } else {
        DrawMap(CONTROLS_LR_START_X, CONTROLS_LR_START_Y, map_controls_lr_off);
        DrawMap(CONTROLS_A_START_X, CONTROLS_A_START_Y, map_controls_a_off);
      }
    } else if (cursorIsOverHand) {
      uint8_t piece = hand[y][x];
      if (piece != P_BLANK) {
        DrawMap(CONTROLS_LR_START_X, CONTROLS_LR_START_Y, map_controls_lr);
        DrawMap(CONTROLS_A_START_X, CONTROLS_A_START_Y, map_controls_a);
      } else {
        DrawMap(CONTROLS_LR_START_X, CONTROLS_LR_START_Y, map_controls_lr_off);
        DrawMap(CONTROLS_A_START_X, CONTROLS_A_START_Y, map_controls_a_off);
      }
    } else {
      DrawMap(CONTROLS_LR_START_X, CONTROLS_LR_START_Y, map_controls_lr_off);
      DrawMap(CONTROLS_A_START_X, CONTROLS_A_START_Y, map_controls_a_off);
    }
    // ----------------------------------------


    // -------------------- PROCESS IN PROGRESS DRAG --------------------
    if (old_piece != -1) { // If we have a valid piece picked up
      MoveSprite(MAX_SPRITES - RESERVED_SPRITES,
                 sprites[MAX_SPRITES - 1].x - (TOKEN_WIDTH * TILE_WIDTH / 2),
                 sprites[MAX_SPRITES - 1].y - (TOKEN_HEIGHT * TILE_HEIGHT / 2),
                 TOKEN_WIDTH, TOKEN_HEIGHT);

      // See if we need to erase the previously selected dropzone
      if (sel_start_x != -1 && sel_start_y != -1)
        if (!((ty >= sel_start_y) && (ty < sel_start_y + TOKEN_HEIGHT) &&
              (tx >= sel_start_x) && (tx < sel_start_x + TOKEN_WIDTH))) {
          DrawMap(sel_start_x, sel_start_y, map_blank);
          sel_start_x = sel_start_y = -1;
        }

      // Highlight blank squares when we're hovering over them
      if (cursorIsOverBoard) {
        if ((board[y][x] & PIECE_MASK) == P_BLANK) {
          DrawMap(BOARD_START_X + x * BOARD_H_SPACING, BOARD_START_Y + y * BOARD_V_SPACING, map_blank_sel);
          sel_start_x = BOARD_START_X + x * BOARD_H_SPACING;
          sel_start_y = BOARD_START_Y + y * BOARD_V_SPACING;
        }
      } else if (cursorIsOverHand) {
        if (hand[y][x] == P_BLANK) {
          DrawMap(HAND_START_X + x * HAND_H_SPACING, HAND_START_Y + y * HAND_V_SPACING, map_blank_sel);
          sel_start_x = HAND_START_X + x * HAND_H_SPACING;
          sel_start_y = HAND_START_Y + y * HAND_V_SPACING;
        }
      }
    }
    // ----------------------------------------

    // -------------------- PROCESS SWITCH POSITION CHANGES
    if (buttons.pressed & BTN_B) {
      // Scan the board for it
      for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
        for (uint8_t x = 0; x < BOARD_WIDTH; ++x) {
          uint8_t flags = board[y][x] & FLAGS_MASK;
          uint8_t piece = board[y][x] & PIECE_MASK;
          if (IsSwitch(piece)) {
            // switch the switch and redraw it
            piece = ChangeSwitch(piece);
            // place it back on the board and redraw it
            board[y][x] = flags | piece;
            DrawMap(BOARD_START_X + x * BOARD_H_SPACING, BOARD_START_Y + y * BOARD_V_SPACING, MapName(piece));
            TriggerNote(SFX_CHANNEL, SFX_SWITCH, SFX_SPEED_SWITCH, SFX_VOL_SWITCH);

            switchChanged = true; // the board has changed, but the change was only due to the switch changing its switch position
          }
        }

      // Scan the hand for it
      for (uint8_t y = 0; y < HAND_HEIGHT; ++y)
        for (uint8_t x = 0; x < HAND_WIDTH; ++x) {
          uint8_t piece = hand[y][x];
          if (IsSwitch(piece)) {
            // switch the switch and redraw it
            piece = ChangeSwitch(piece);
            // place it back on the hand and redraw it
            hand[y][x] = piece;
            DrawMap(HAND_START_X + x * HAND_H_SPACING, HAND_START_Y + y * HAND_V_SPACING, MapName(piece));
            TriggerNote(SFX_CHANNEL, SFX_SWITCH, SFX_SPEED_SWITCH, SFX_VOL_SWITCH);
          }
        }

      // Look to see if it's picked up
      if (old_piece != -1) {
        if (IsSwitch(old_piece)) {
          old_piece = ChangeSwitch(old_piece);
          // place it back, and redraw it
          MapSprite2(MAX_SPRITES - RESERVED_SPRITES, MapName(old_piece), SPRITE_BANK1);
          TriggerNote(SFX_CHANNEL, SFX_SWITCH, SFX_SPEED_SWITCH, SFX_VOL_SWITCH);
        }
      }
    }
    // ----------------------------------------


    // -------------------- PROCESS ROTATION --------------------
    const uint8_t* rotation_lut = 0;
    if (buttons.pressed & BTN_SR)
      rotation_lut = rotateClockwise;
    else if (buttons.pressed & BTN_SL)
      rotation_lut = rotateCounterClockwise;
    if (rotation_lut) {

      if (old_piece == -1) { // nothing being dragged and dropped
        if (cursorIsOverBoard) {
          if (!(board[y][x] & FLAG_LOCKED) && (board[y][x] & PIECE_MASK) != P_BLANK) { // respect lock bit
            // Save the rotate bit, if set
            uint8_t flags = board[y][x] & FLAGS_MASK;
            board[y][x] = flags | pgm_read_byte(&rotation_lut[board[y][x] & PIECE_MASK]);
            DrawMap(BOARD_START_X + x * BOARD_H_SPACING, BOARD_START_Y + y * BOARD_V_SPACING, MapName(board[y][x] & PIECE_MASK));
            TriggerNote(SFX_CHANNEL, SFX_ROTATE, SFX_SPEED_ROTATE, SFX_VOL_ROTATE);

            boardChanged = true;

            // Move rotation overlay sprite if it would hide something important
            // The only way the sprite would have rotate bit set is if it was part of the original level
            // so we should be able to re-read the level data, to figure out which sprite index is used
            // for a given x and y and then replace that sprite again according to the new rotated piece
            int8_t spriteIndex = FindSpriteIndexForOverlay(currentLevel, x, y);
            if (spriteIndex != -1) {
              uint8_t piece = board[y][x] & PIECE_MASK;
              // If the overlay needs to be offset, we need to use a different icon (that is shifted to the right by 1 pixel)
              uint8_t offset = OverlayOffset(piece);
              if (offset) {
                if (flags & FLAG_ROTATE)
                  sprites[spriteIndex].tileIndex = ALT_ROTATE_OVERLAY;
                else
                  sprites[spriteIndex].tileIndex = ALT_LOCK_OVERLAY;
              } else {
                if (flags & FLAG_ROTATE)
                  sprites[spriteIndex].tileIndex = ROTATE_OVERLAY;
                else
                  sprites[spriteIndex].tileIndex = LOCK_OVERLAY;
              }
              sprites[spriteIndex].x = (((BOARD_START_X + (TOKEN_WIDTH - 1)) + x * BOARD_H_SPACING) * TILE_WIDTH) - offset;
            }

          }
        } else if (cursorIsOverHand) {
          if (hand[y][x] != P_BLANK) {
            hand[y][x] = pgm_read_byte(&rotation_lut[hand[y][x]]);
            DrawMap(HAND_START_X + x * HAND_H_SPACING, HAND_START_Y + y * HAND_V_SPACING, MapName(hand[y][x]));
            TriggerNote(SFX_CHANNEL, SFX_ROTATE, SFX_SPEED_ROTATE, SFX_VOL_ROTATE);
          }
        }
      } else {
        old_piece = pgm_read_byte(&rotation_lut[old_piece]);
        MapSprite2(MAX_SPRITES - RESERVED_SPRITES, MapName(old_piece), SPRITE_BANK1);
        TriggerNote(SFX_CHANNEL, SFX_ROTATE, SFX_SPEED_ROTATE, SFX_VOL_ROTATE);
      }

    }
    // ----------------------------------------

    // -------------------- INITIATE DRAG --------------------
    if (buttons.pressed & BTN_A) {
      if (cursorIsOverBoard) {
        if (!(board[y][x] & FLAG_LOCKED) && !(board[y][x] & FLAG_ROTATE) && ((board[y][x] & PIECE_MASK) != P_BLANK)) { // respect lock bit
          old_piece = board[y][x];
          old_x = x;
          old_y = y;
          DrawMap(BOARD_START_X + x * BOARD_H_SPACING, BOARD_START_Y + y * BOARD_V_SPACING, map_blank_sel); // select the drop zone under it now
          sel_start_x = BOARD_START_X + x * BOARD_H_SPACING; // store the coordinates of the selected tile
          sel_start_y = BOARD_START_Y + y * BOARD_V_SPACING;
          board[y][x] = P_BLANK;

#if defined(OPTION_HIDE_CURSOR_DURING_DRAG_AND_DROP)
          // Hide cursor sprite
          sprites[MAX_SPRITES - 1].flags |= SPRITE_OFF;
#endif

          // If a user could press start to advance the level, but tries to pick something up instead, remove the level advance
          // because we need to use those ram tiles for picking up the piece
          CancelStartAdvancesLevel();

          MapSprite2(MAX_SPRITES - RESERVED_SPRITES, MapName(old_piece), SPRITE_BANK1);
          MoveSprite(MAX_SPRITES - RESERVED_SPRITES, sprites[MAX_SPRITES - 1].x - (TOKEN_WIDTH * TILE_WIDTH / 2), sprites[MAX_SPRITES - 1].y - (TOKEN_HEIGHT * TILE_HEIGHT / 2), TOKEN_WIDTH, TOKEN_HEIGHT);
          TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);

          boardChanged = true;
        }
      } else if (cursorIsOverHand) {
        if ((hand[y][x] != P_BLANK)) {
          old_piece = hand[y][x];
          old_x = x;
          old_y = BOARD_HEIGHT + y; // this piece came from hand (if old_y >= BOARD_HEIGHT it is considered in the hand)
          DrawMap(HAND_START_X + x * HAND_H_SPACING, HAND_START_Y + y * HAND_V_SPACING, map_blank_sel); // select the drop zone under it now
          sel_start_x = HAND_START_X + x * HAND_H_SPACING; // store the coordinates of the selected tile
          sel_start_y = HAND_START_Y + y * HAND_V_SPACING;
          hand[y][x] = P_BLANK;

#if defined(OPTION_HIDE_CURSOR_DURING_DRAG_AND_DROP)
          // Hide cursor sprite
          sprites[MAX_SPRITES - 1].flags |= SPRITE_OFF;
#endif

          MapSprite2(MAX_SPRITES - RESERVED_SPRITES, MapName(old_piece), SPRITE_BANK1);
          MoveSprite(MAX_SPRITES - RESERVED_SPRITES, sprites[MAX_SPRITES - 1].x - (TOKEN_WIDTH * TILE_WIDTH / 2), sprites[MAX_SPRITES - 1].y - (TOKEN_HEIGHT * TILE_HEIGHT / 2), TOKEN_WIDTH, TOKEN_HEIGHT);
          TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);
        }
      }
      // -------------------- INITIATE DROP --------------------
    } else if (buttons.released & BTN_A) {
      if ((old_piece != -1) && (old_y != -1)) { // valid piece is being held
        // Figure out where to drop it
        if (cursorIsOverBoard) {
          if (((board[y][x] & PIECE_MASK) == P_BLANK)) {
            old_x = x;
            old_y = y;
          }
        } else if (cursorIsOverHand) {
          if ((hand[y][x] == P_BLANK)) {
            old_x = x;
            old_y = BOARD_HEIGHT + y; // by convention if old_y >= BOARD_HEIGHT it is considered in the hand
          }
        }

        // Drop it like it's hot
        for (uint8_t i = 0; i < TOKEN_WIDTH * TOKEN_HEIGHT; ++i)
          sprites[i + MAX_SPRITES - RESERVED_SPRITES].y = SCREEN_TILES_V * TILE_HEIGHT; // OFF_SCREEN;

        if (old_y >= BOARD_HEIGHT) { // by convention of old_y >= BOARD_HEIGHT it is considered in the hand
          DrawMap(HAND_START_X + old_x * HAND_H_SPACING, HAND_START_Y + (old_y - BOARD_HEIGHT) * HAND_V_SPACING, MapName(old_piece));
          hand[old_y - BOARD_HEIGHT][old_x] = old_piece; // subtract BOARD_HEIGHT to get the correct offset into the hand array
        } else {
          DrawMap(BOARD_START_X + old_x * BOARD_H_SPACING, BOARD_START_Y + old_y * BOARD_V_SPACING, MapName(old_piece));
          board[old_y][old_x] = old_piece;

          boardChanged = true;
        }

#if defined(OPTION_HIDE_CURSOR_DURING_DRAG_AND_DROP)
        // Show cursor sprite
        sprites[MAX_SPRITES - 1].flags &= (sprites[MAX_SPRITES - 1].flags ^ SPRITE_OFF);
#endif

        old_piece = old_x = old_y = sel_start_x = sel_start_y = -1;
        TriggerNote(SFX_CHANNEL, SFX_MOUSE_UP, SFX_SPEED_MOUSE_UP, SFX_VOL_MOUSE_UP);
      }
    }
    // ----------------------------------------

    // If the current level includes a switch, and the board change wasn't due to just the switch position changing
    // we should clear all of "met goals" for the switch, and let BoardChanged fill it back in for
    // the current switch position, because the board change may have invalidated previously met goals
    if (boardChanged && CurrentLevelHasSwitch())
      for (uint8_t i = 0; i < 3; ++i)
        met_goal[i] = false;

    if (boardChanged || switchChanged)
      BoardChanged();

    // -------------------- PROCESS POPUP MENU --------------------
    // If we pressed the START button with no other buttons held down
    if (buttons.pressed & BTN_START && buttons.held == BTN_START) {
#define MENU_WIDTH 18
#define MENU_HEIGHT 6
#define MENU_START_X 7
#define MENU_START_Y 15
#define TILE_MENU_BG TILE_BACKGROUND

      // Check to see if we are advancing a level
      if (startAdvancesLevel) {
        if (startWinsGame) {
          EpicWin(&buttons);
          goto title_screen;
        }
        currentLevel++;
        if (currentLevel > 60)
          currentLevel = 1;
        for (uint8_t i = HAND_START_X + MAP_ADDTOGRID_WIDTH; i < HAND_START_X + sizeof(pgm_W_PRESS_START); ++i)
          SetTile(i, HAND_START_Y - 2, TILE_BACKGROUND);
        SetUserRamTilesCount(GAME_USER_RAM_TILES_COUNT);
        LoadLevel(currentLevel);
        startAdvancesLevel = false;
        continue;
      }

      // Hide all the overlay sprites
      for (uint8_t i = OVERLAY_SPRITE_START; i < MAX_SPRITES - 1; ++i)
        sprites[i].flags |= SPRITE_OFF;

      // Play a sound effect that indicates the popup menu, unfortunately if music is playing, a TriggerFx won't work
      TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);
      //BB_triggerFx(8); // This will work if music is playing, but PCM sounds are way nicer

      // save what is behind the popup menu
      uint8_t backing[MENU_HEIGHT][MENU_WIDTH];
      for (uint8_t y = 0; y < MENU_HEIGHT; ++y)
        for (uint8_t x = 0; x < MENU_WIDTH; ++x)
          backing[y][x] = GetTile(MENU_START_X + x, MENU_START_Y + y);

      // Put all the stuff we'll need to display for the menu into user ram tiles
      // Reserve some extra tiles, but keep the sprites that are displaying the current level visible
      // By reserving all the tiles but two, it keeps the overlay sprites from showing through the popup menu

      WaitVsync(1); // Ensures the sprites have a chance to hide before we reuse their ram tiles, avoiding glitches
      SetUserRamTilesCount(RAM_TILES_COUNT);

      // Load the popup menu into ram tiles starting at 0
      uint8_t rf_popup_len = sizeof(rf_popup) / 8;
      RamFont_Load(rf_popup, GAME_USER_RAM_TILES_COUNT, rf_popup_len, 0xFF, 0x00);

      // load the popup menu border after that (starting at the user ram tile: rf_popup_len) with a different fg color
      uint8_t rf_popup_border_len = sizeof(rf_popup_border) / 8;
      RamFont_Load(rf_popup_border, GAME_USER_RAM_TILES_COUNT + rf_popup_len, rf_popup_border_len, 0xA4, 0x00);

      // Make the top right and bottom left pixels of the border "transparent"
      uint8_t bgTile;
      char bgTilePixel;
      uint8_t* ramTile;

      bgTile = GetTile(MENU_START_X + MENU_WIDTH - 1, MENU_START_Y);
      bgTilePixel = pgm_read_byte(tileset + bgTile * 64 + 7); // 7 is top right pixel
      ramTile = GetUserRamTile(RF_B_TR); // top right corner in rf_popup
      ramTile[7] = bgTilePixel; // top right pixel of ramTile

      bgTile = GetTile(MENU_START_X, MENU_START_Y + MENU_HEIGHT - 1);
      bgTilePixel = pgm_read_byte(tileset + bgTile * 64 + 56); // 56 is bottom left pixel
      ramTile = GetUserRamTile(RF_B_BL); // bottom left corner in rf_popup
      ramTile[56] = bgTilePixel; // bottom left pixel of ramTile

      // Draw the current level number in the color corresponding to its difficulty
      RamFont_Load2Digits(rf_digits, GAME_USER_RAM_TILES_COUNT + rf_popup_len + rf_popup_border_len, currentLevel, GetLevelColor(currentLevel), 0x00);
      TileToRam(SPRITE_INDEX_SLIDER_ON, 0, 2, mysprites, GetUserRamTile(RF_SLIDER_ON_L));

      // Draw the menu background
      Fill(MENU_START_X + 1, MENU_START_Y + 1, MENU_WIDTH - 2, MENU_HEIGHT - 2, TILE_MENU_BG);
      SetRamTile(MENU_START_X, MENU_START_Y, RF_B_TL);
      for (uint8_t i = MENU_START_X + 1; i < MENU_START_X + MENU_WIDTH - 1; ++i)
        SetRamTile(i, MENU_START_Y, RF_B_T);
      SetRamTile(MENU_START_X + MENU_WIDTH - 1, MENU_START_Y, RF_B_TR);
      for (uint8_t i = MENU_START_Y + 1; i < MENU_START_Y + MENU_HEIGHT - 1; ++i) {
        SetRamTile(MENU_START_X, i, RF_B_L);
        SetRamTile(MENU_START_X + MENU_WIDTH - 1, i, RF_B_R);
      }
      SetRamTile(MENU_START_X, MENU_START_Y + MENU_HEIGHT - 1, RF_B_BL);
      for (uint8_t i = MENU_START_X + 1; i < MENU_START_X + MENU_WIDTH - 1; ++i)
        SetRamTile(i, MENU_START_Y + MENU_HEIGHT - 1, RF_B_B);
      SetRamTile(MENU_START_X + MENU_WIDTH - 1, MENU_START_Y + MENU_HEIGHT - 1, RF_B_BR);

      RamFont_Print(MENU_START_X + 5, MENU_START_Y + 1, pgm_P_RETURN, sizeof(pgm_P_RETURN));
      RamFont_Print(MENU_START_X + 5, MENU_START_Y + 2, pgm_P_RESET_TOKENS, sizeof(pgm_P_RESET_TOKENS));
      RamFont_Print(MENU_START_X + 5, MENU_START_Y + 3, pgm_P_CIRCUIT, sizeof(pgm_P_CIRCUIT));
      RamFont_Print(MENU_START_X + 5, MENU_START_Y + 4, pgm_P_MUSIC, sizeof(pgm_P_MUSIC));

      SetRamTile(MENU_START_X + 5 + 9, MENU_START_Y + 3, RF_OnesPlace);
      SetRamTile(MENU_START_X + 5 + 8, MENU_START_Y + 3, RF_TensPlace);
      if (!BitArray_readBit(currentLevel))
        SetRamTile(MENU_START_X + 5 + 11, MENU_START_Y + 3, RF_UNSOLVED);

      if (IsSongPlaying()) {
        // if we needed multiple sliders in the same menu, you wouldn't want to use TileToRam, instead you'd want
        // to have both the on and off states of the slider loaded into different ram tiles at the same time
        TileToRam(SPRITE_INDEX_SLIDER_ON, 0, 2, mysprites, GetUserRamTile(RF_SLIDER_ON_L));
        //RamFont_Print(MENU_START_X + 6 + 6, MENU_START_Y + 5, pgm_P_SLIDER_ON, sizeof(pgm_P_SLIDER_ON);
      } else {
        TileToRam(SPRITE_INDEX_SLIDER_OFF, 0, 2, mysprites, GetUserRamTile(RF_SLIDER_ON_L));
        //RamFont_Print(MENU_START_X + 6 + 6, MENU_START_Y + 5, pgm_P_SLIDER_ON, sizeof(pgm_P_SLIDER_ON));
      }
      RamFont_Print(MENU_START_X + 5 + 6, MENU_START_Y + 4, pgm_P_SLIDER_ON, sizeof(pgm_P_SLIDER_ON));

      int8_t prev_selection;
      int8_t selection = 0;
      bool confirmed = false;

      uint8_t selectedLevel = currentLevel;

      // The popup menu has its own run loop
      for (;;) {

        SetRamTile(MENU_START_X + 2, MENU_START_Y + 1 + selection, RF_ASTERISK);
        prev_selection = selection;

        // Read the current state of the player's controller
        buttons.prev = buttons.held;
        buttons.held = ReadJoypad(0);
        buttons.pressed = buttons.held & (buttons.held ^ buttons.prev);
        buttons.released = buttons.prev & (buttons.held ^ buttons.prev);

        if (buttons.pressed & BTN_START) {
          confirmed = true;
          break;
        }

        if (buttons.pressed & BTN_UP) {
          if (selection > 0) {
            selection--;
            TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);
            for (uint8_t x = MENU_START_X + 1; x <= MENU_START_X + 3; ++x)
              SetTile(x, MENU_START_Y + 1 + prev_selection, TILE_MENU_BG);
            SetRamTile(MENU_START_X + 2, MENU_START_Y + 1 + selection, RF_ASTERISK);
            prev_selection = selection;
          }
        } else if (buttons.pressed & BTN_DOWN) {
          if (selection < 3) {
            selection++;
            TriggerNote(SFX_CHANNEL, SFX_MOUSE_UP, SFX_SPEED_MOUSE_UP, SFX_VOL_MOUSE_UP);
            for (uint8_t x = MENU_START_X + 1; x <= MENU_START_X + 3; ++x)
              SetTile(x, MENU_START_Y + 1 + prev_selection, TILE_MENU_BG);
            SetRamTile(MENU_START_X + 2, MENU_START_Y + 1 + selection, RF_ASTERISK);
            prev_selection = selection;
          }
        }
        if (selection == 2) {
          SetTile(MENU_START_X + 1, MENU_START_Y + 1 + selection, TILE_DPAD_LEFT);
          SetTile(MENU_START_X + 3, MENU_START_Y + 1 + selection, TILE_DPAD_RIGHT);
        }

        if ((selection == 2) && ((buttons.pressed & BTN_LEFT) || (buttons.pressed & BTN_RIGHT))) {
          if (buttons.pressed & BTN_LEFT) {
            if (selectedLevel > 1) {
              selectedLevel--;
            } else {
              selectedLevel = 60;
            }
            RamFont_Load2Digits(rf_digits, GAME_USER_RAM_TILES_COUNT + rf_popup_len + rf_popup_border_len, selectedLevel, GetLevelColor(selectedLevel), 0x00);
            if (!BitArray_readBit(selectedLevel))
              SetRamTile(MENU_START_X + 5 + 11, MENU_START_Y + 3, RF_UNSOLVED);
            else
              SetTile(MENU_START_X + 5 + 11, MENU_START_Y + 3, TILE_BACKGROUND);

            TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);
          } else if (buttons.pressed & BTN_RIGHT) {
            if (selectedLevel < 60) {
              selectedLevel++;
            } else {
              selectedLevel = 1;
            }
            RamFont_Load2Digits(rf_digits, GAME_USER_RAM_TILES_COUNT + rf_popup_len + rf_popup_border_len, selectedLevel, GetLevelColor(selectedLevel), 0x00);
            if (!BitArray_readBit(selectedLevel))
              SetRamTile(MENU_START_X + 5 + 11, MENU_START_Y + 3, RF_UNSOLVED);
            else
              SetTile(MENU_START_X + 5 + 11, MENU_START_Y + 3, TILE_BACKGROUND);

            TriggerNote(SFX_CHANNEL, SFX_MOUSE_UP, SFX_SPEED_MOUSE_UP, SFX_VOL_MOUSE_UP);
          }
        }

        if (selection == 3) {
          if (IsSongPlaying()) {
            SetTile(MENU_START_X + 3, MENU_START_Y + 1 + selection, TILE_MENU_BG);
            SetTile(MENU_START_X + 1, MENU_START_Y + 1 + selection, TILE_DPAD_LEFT);
          } else {
            SetTile(MENU_START_X + 1, MENU_START_Y + 1 + selection, TILE_MENU_BG);
            SetTile(MENU_START_X + 3, MENU_START_Y + 1 + selection, TILE_DPAD_RIGHT);
          }
        }

        if ((selection == 3) && ((buttons.pressed & BTN_LEFT) || (buttons.pressed & BTN_RIGHT))) {
          if ((buttons.pressed & BTN_LEFT) && IsSongPlaying()) {
            TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);
            TileToRam(SPRITE_INDEX_SLIDER_OFF, 0, 2, mysprites, GetUserRamTile(RF_SLIDER_ON_L));
            //RamFont_Print(MENU_START_X + 6 + 6, MENU_START_Y + 2 + selection, pgm_P_SLIDER_ON, sizeof(pgm_P_SLIDER_ON));
            StopSong();
          } else if ((buttons.pressed & BTN_RIGHT) && !IsSongPlaying()) {
            TriggerNote(SFX_CHANNEL, SFX_MOUSE_UP, SFX_SPEED_MOUSE_UP, SFX_VOL_MOUSE_UP);
            TileToRam(SPRITE_INDEX_SLIDER_ON, 0, 2, mysprites, GetUserRamTile(RF_SLIDER_ON_L));
            //RamFont_Print(MENU_START_X + 6 + 6, MENU_START_Y + 2 + selection, pgm_P_SLIDER_ON, sizeof(pgm_P_SLIDER_ON));
            ResumeSong();
          }
        }

        WaitVsync(1);
      }

#if defined(OPTION_PERSIST_MUSIC_PREF)
      if (IsSongPlaying()) {
        if (BitArray_readBit(0)) {
          BitArray_clearBit(0);
          SaveHighScore(bitarray);
        }
      } else {
        if (!BitArray_readBit(0)) {
          BitArray_setBit(0);
          SaveHighScore(bitarray);
        }
      }
#endif

      SetUserRamTilesCount(GAME_USER_RAM_TILES_COUNT);

      // restore what was behind the popup menu
      for (uint8_t y = 0; y < MENU_HEIGHT; ++y)
        for (uint8_t x = 0; x < MENU_WIDTH; ++x)
          SetTile(MENU_START_X + x, MENU_START_Y + y, backing[y][x]);

      TriggerNote(SFX_CHANNEL, SFX_MOUSE_UP, SFX_SPEED_MOUSE_UP, SFX_VOL_MOUSE_UP);
      //BB_triggerFx(7);

      if (confirmed && selection == 1)
        LoadLevel(currentLevel);
      else if (confirmed && selectedLevel != currentLevel) {
        currentLevel = selectedLevel;
        LoadLevel(currentLevel);
      }

      // Show all the overlay sprites
      for (uint8_t i = OVERLAY_SPRITE_START; i < MAX_SPRITES - 1; ++i)
        sprites[i].flags &= (sprites[i].flags ^ SPRITE_OFF);

    }
    // ----------------------------------------

  }
}
