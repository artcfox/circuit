#pragma once

#define NETLIST_NETLIST_MASK     0x07FFFFFF
#define NETLIST_LED_STATES_MASK  0xE0000000
#define NETLIST_R_ON             0x20000000
#define NETLIST_Y_ON             0x40000000
#define NETLIST_G_ON             0x80000000

struct netlist {
  uint32_t netlist_and_led_states;
};
