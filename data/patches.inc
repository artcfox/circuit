#include "PCM_mouse_down.inc"
#include "PCM_mouse_up.inc"
//#include "PCM_win.inc"
#include "PCM_fanfare.inc"
#include "PCM_rotate.inc"
#include "PCM_zap.inc"
#include "PCM_switch.inc"

//const char a85e[] PROGMEM = {
//0,PC_WAVE,0,
//0,PC_ENV_VOL,24,
//0,PC_ENV_SPEED,1,
//16,PC_ENV_SPEED,-1,
//0,PATCH_END
//};
//
//const char a899[] PROGMEM = {
//0,PC_WAVE,0,
//0,PC_ENV_VOL,0,
//0,PC_ENV_SPEED,2,
//12,PC_ENV_SPEED,-2,
//24,PATCH_END
//};

const char patch04[] PROGMEM = {
0,PC_TREMOLO_LEVEL,70,
0,PC_TREMOLO_RATE,80,
0,PC_WAVE,0,
0,PC_ENV_VOL,48,  // was 48
0,PC_ENV_SPEED,4, // was 4
1,PC_NOTE_HOLD,0,
0,PC_ENV_SPEED,-8, // was -8
8,PC_NOTE_CUT,0,
0,PATCH_END
};

#define SFX_CHANNEL     4

#define SFX_MOUSE_DOWN  3
#define SFX_MOUSE_UP    4
#define SFX_WIN         5
#define SFX_ROTATE      6
#define SFX_ZAP         7
#define SFX_SWITCH      8

#define SFX_SPEED_MOUSE_DOWN  23
#define SFX_SPEED_MOUSE_UP    23
#define SFX_SPEED_WIN         23
#define SFX_SPEED_ROTATE      23
#define SFX_SPEED_ZAP         23
#define SFX_SPEED_SWITCH      23

#define SFX_VOL_MOUSE_DOWN  255
#define SFX_VOL_MOUSE_UP    255
#define SFX_VOL_WIN         192
#define SFX_VOL_ROTATE      128
#define SFX_VOL_ZAP         224
#define SFX_VOL_SWITCH      192

extern const char waves[];
const struct PatchStruct patches[] PROGMEM = {
//{0,NULL,a85e,0,0},
//{0,NULL,a899,0,0},
//{0,NULL,a899,0,0},
{0,NULL,patch04,0,0},
{0,NULL,patch04,0,0},
{0,NULL,patch04,0,0},
// For the below to work, the last 2 bytes of the PCM_Data arrays need to be 0
{2,PCM_mouse_down,NULL,sizeof(PCM_mouse_down)-2,sizeof(PCM_mouse_down)-1},
{2,PCM_mouse_up,NULL,sizeof(PCM_mouse_up)-2,sizeof(PCM_mouse_up)-1},
{2,PCM_fanfare,NULL,sizeof(PCM_fanfare)-2,sizeof(PCM_fanfare)-1},
{2,PCM_rotate,NULL,sizeof(PCM_rotate)-2,sizeof(PCM_rotate)-1},
{2,PCM_zap,NULL,sizeof(PCM_zap)-2,sizeof(PCM_zap)-1},
{2,PCM_switch,NULL,sizeof(PCM_switch)-2,sizeof(PCM_switch)-1},
};
