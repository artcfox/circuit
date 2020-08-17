#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "netlist_node.h"

/* This is copied and pasted from the switch statement in circuit.c,
   and then massaged to have the appropriate bits tacked on to each
   element */
uint32_t unsorted_netlists[] =
{
  0x01020000 | NETLIST_Y_ON,
  0x03420690 | NETLIST_Y_ON,
  0x01020001 | NETLIST_Y_ON,
  0x01038007 | NETLIST_Y_ON,
  0x05422850 | NETLIST_Y_ON,
  0x017a0019 | NETLIST_Y_ON,
  0x07026200 | NETLIST_Y_ON,
  0x01038000 | NETLIST_Y_ON,
  0x0703e200 | NETLIST_Y_ON,
  0x01038004 | NETLIST_Y_ON,
  0x011a4000 | NETLIST_Y_ON,
  0x01024000 | NETLIST_Y_ON,
  0x03330248 | NETLIST_Y_ON,
  0x03028a00 | NETLIST_Y_ON,
  0x010b0404 | NETLIST_Y_ON,
  0x010b0500 | NETLIST_Y_ON,
  0x01020840 | NETLIST_Y_ON,
  0x011a0001 | NETLIST_Y_ON,
  0x011b80c1 | NETLIST_Y_ON,
  0x01024001 | NETLIST_Y_ON,
  0x01024cc1 | NETLIST_Y_ON,
  0x07026201 | NETLIST_Y_ON,
  /* return Y_BIT; */

  0x00408000 | NETLIST_G_ON,
  0x00c48004 | NETLIST_G_ON,
  0x004aa000 | NETLIST_G_ON,
  0x005ea408 | NETLIST_G_ON,
  0x0040c000 | NETLIST_G_ON,
  0x004c9000 | NETLIST_G_ON,
  0x00528200 | NETLIST_G_ON,
  0x00548100 | NETLIST_G_ON,
  0x0058c000 | NETLIST_G_ON,
  0x0058c440 | NETLIST_G_ON,
  0x00c4c004 | NETLIST_G_ON,
  0x00468008 | NETLIST_G_ON,
  0x00468002 | NETLIST_G_ON,
  0x005e9402 | NETLIST_G_ON,
  0x00588400 | NETLIST_G_ON,
  0x00529602 | NETLIST_G_ON, // Tilda
  0x0054a508 | NETLIST_G_ON, // Tilda
  0x004aa142 | NETLIST_G_ON, // Tilda
  0x004aa040 | NETLIST_G_ON,
  0x00548108 | NETLIST_G_ON,
  /* return G_BIT; */

  0x04080000 | NETLIST_R_ON,
  0x048c1000 | NETLIST_R_ON,
  0x0528240c | NETLIST_R_ON,
  0x04098080 | NETLIST_R_ON,
  0x044c0802 | NETLIST_R_ON,
  0x044c0a02 | NETLIST_R_ON,
  0x04098000 | NETLIST_R_ON,
  0x040e0200 | NETLIST_R_ON,
  0x040c8004 | NETLIST_R_ON,
  0x04098001 | NETLIST_R_ON,
  0x040c8204 | NETLIST_R_ON,
  0x040a8010 | NETLIST_R_ON,
  0x04a81412 | NETLIST_R_ON,
  0x0408000c | NETLIST_R_ON,
  /* return R_BIT; */

  0x04008080 | NETLIST_R_ON | NETLIST_G_ON,
  0x04488800 | NETLIST_R_ON | NETLIST_G_ON,
  0x04cc9804 | NETLIST_R_ON | NETLIST_G_ON,
  0x00480400 | NETLIST_R_ON | NETLIST_G_ON,
  0x00cc0404 | NETLIST_R_ON | NETLIST_G_ON,
  0x04809290 | NETLIST_R_ON | NETLIST_G_ON,
  0x00c8240c | NETLIST_R_ON | NETLIST_G_ON,
  0x040083b4 | NETLIST_R_ON | NETLIST_G_ON,
  /* return R_BIT | G_BIT; */

  0x050a2000 | NETLIST_R_ON | NETLIST_Y_ON,
  0x04020200 | NETLIST_R_ON | NETLIST_Y_ON,
  0x01081000 | NETLIST_R_ON | NETLIST_Y_ON,
  0x04030248 | NETLIST_R_ON | NETLIST_Y_ON,
  0x01099000 | NETLIST_R_ON | NETLIST_Y_ON,
  0x01681019 | NETLIST_R_ON | NETLIST_Y_ON,
  0x01081001 | NETLIST_R_ON | NETLIST_Y_ON,
  /* return R_BIT | Y_BIT; */

  0x01428010 | NETLIST_G_ON | NETLIST_Y_ON,
  0x00420008 | NETLIST_G_ON | NETLIST_Y_ON,
  0x01008004 | NETLIST_G_ON | NETLIST_Y_ON,
  0x01108184 | NETLIST_G_ON | NETLIST_Y_ON,
  0x01188404 | NETLIST_G_ON | NETLIST_Y_ON,
  0x00426648 | NETLIST_G_ON | NETLIST_Y_ON,
  0x0118c444 | NETLIST_G_ON | NETLIST_Y_ON,
  0x0142c010 | NETLIST_G_ON | NETLIST_Y_ON,
  0x01188044 | NETLIST_G_ON | NETLIST_Y_ON,
  /* return G_BIT | Y_BIT; */

  0x054aa810 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x014a0410 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x00481008 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x04420a48 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x01089804 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x01080404 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x04020088 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x00420600 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x004a2408 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x044a0808 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x04008204 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x044a0908 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x01009080 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x0508a004 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x0500a184 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x01481412 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  0x04428a00 | NETLIST_R_ON | NETLIST_Y_ON | NETLIST_G_ON,
  /* return R_BIT | Y_BIT | G_BIT; */
};

#define NELEMS(x) (sizeof(x)/sizeof(x[0]))

#define NL_VV 0
#define NL_00 1
#define NL_RA 2
#define NL_RC 3
#define NL_YA 4
#define NL_YC 5
#define NL_GA 6
#define NL_GC 7

#define SWAPCOLORS_FLAG_RED_YELLOW    1
#define SWAPCOLORS_FLAG_RED_GREEN     2
#define SWAPCOLORS_FLAG_YELLOW_GREEN  3

uint32_t SwapColors(uint32_t netlist_and_led_states, uint8_t flag)
{
  // the diagonal needs to start cleared
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

  uint32_t netlist = netlist_and_led_states & NETLIST_NETLIST_MASK;
  uint32_t led_states = netlist_and_led_states & NETLIST_FLAG_MASK;

  // Turn the netlist back into the matrix version
  uint32_t bitmask = 1;
  for (uint8_t i = 0; i < 27; ++i) {
    if (netlist & bitmask)
      pruned_netlist[packedNetlistY[i]][packedNetlistX[i]] = pruned_netlist[packedNetlistX[i]][packedNetlistY[i]] = 1;
    bitmask <<= 1;
  }

  // print the netlist
  /* putchar('\n'); */
  /* for (uint8_t y = 0; y < 8; ++y) { */
  /*   for (uint8_t x = 0; x < 8; ++x) { */
  /*     putchar(pruned_netlist[y][x] ? '1' : '0'); putchar(' '); */
  /*   } */
  /*   putchar('\n'); */
  /* } */

  ////  printf("0x%08x -> ", netlist_and_led_states); // was previously printing just netlist

  // permute it
  uint32_t new_led_states = 0;

  // ---------------------------------------- SWAPPING RED AND YELLOW
  if (flag == SWAPCOLORS_FLAG_RED_YELLOW) {

    // swap the red anode and yellow anode rows
    for (uint8_t i = 0; i < 8; ++i) {
      uint8_t scratch = pruned_netlist[NL_RA][i];
      pruned_netlist[NL_RA][i] = pruned_netlist[NL_YA][i];
      pruned_netlist[NL_YA][i] = scratch;
    }
    // swap the red cathode and yellow cathode rows
    for (uint8_t i = 0; i < 8; ++i) {
      uint8_t scratch = pruned_netlist[NL_RC][i];
      pruned_netlist[NL_RC][i] = pruned_netlist[NL_YC][i];
      pruned_netlist[NL_YC][i] = scratch;
    }
    // swap the red anode and yellow anode cols
    for (uint8_t i = 0; i < 8; ++i) {
      uint8_t scratch = pruned_netlist[i][NL_RA];
      pruned_netlist[i][NL_RA] = pruned_netlist[i][NL_YA];
      pruned_netlist[i][NL_YA] = scratch;
    }
    // swap the red cathode and yellow cathode cols
    for (uint8_t i = 0; i < 8; ++i) {
      uint8_t scratch = pruned_netlist[i][NL_RC];
      pruned_netlist[i][NL_RC] = pruned_netlist[i][NL_YC];
      pruned_netlist[i][NL_YC] = scratch;
    }

    // reflect the matrix across the diagonal
    for (uint8_t y = 0; y < 8; ++y)
      for (uint8_t x = 0; x < 8; ++x)
        if (pruned_netlist[y][x] || pruned_netlist[x][y])
          pruned_netlist[y][x] = pruned_netlist[x][y] = 1;

    // swap LED on states
    bool redOn = led_states & NETLIST_R_ON;
    bool yellowOn = led_states & NETLIST_Y_ON;
    bool greenOn = led_states & NETLIST_G_ON;

    new_led_states |= redOn ? NETLIST_Y_ON : 0;
    new_led_states |= yellowOn ? NETLIST_R_ON : 0;
    new_led_states |= greenOn ? NETLIST_G_ON : 0;
  }
  // ---------------------------------------- SWAPPING RED AND GREEN
  else if (flag == SWAPCOLORS_FLAG_RED_GREEN) {

    // swap the red anode and green anode rows
    for (uint8_t i = 0; i < 8; ++i) {
      uint8_t scratch = pruned_netlist[NL_RA][i];
      pruned_netlist[NL_RA][i] = pruned_netlist[NL_GA][i];
      pruned_netlist[NL_GA][i] = scratch;
    }
    // swap the red cathode and green cathode rows
    for (uint8_t i = 0; i < 8; ++i) {
      uint8_t scratch = pruned_netlist[NL_RC][i];
      pruned_netlist[NL_RC][i] = pruned_netlist[NL_GC][i];
      pruned_netlist[NL_GC][i] = scratch;
    }
    // swap the red anode and green anode cols
    for (uint8_t i = 0; i < 8; ++i) {
      uint8_t scratch = pruned_netlist[i][NL_RA];
      pruned_netlist[i][NL_RA] = pruned_netlist[i][NL_GA];
      pruned_netlist[i][NL_GA] = scratch;
    }
    // swap the red cathode and green cathode cols
    for (uint8_t i = 0; i < 8; ++i) {
      uint8_t scratch = pruned_netlist[i][NL_RC];
      pruned_netlist[i][NL_RC] = pruned_netlist[i][NL_GC];
      pruned_netlist[i][NL_GC] = scratch;
    }

    // reflect the matrix across the diagonal
    for (uint8_t y = 0; y < 8; ++y)
      for (uint8_t x = 0; x < 8; ++x)
        if (pruned_netlist[y][x] || pruned_netlist[x][y])
          pruned_netlist[y][x] = pruned_netlist[x][y] = 1;

    // swap LED on states
    bool redOn = led_states & NETLIST_R_ON;
    bool yellowOn = led_states & NETLIST_Y_ON;
    bool greenOn = led_states & NETLIST_G_ON;

    new_led_states |= redOn ? NETLIST_G_ON : 0;
    new_led_states |= yellowOn ? NETLIST_Y_ON : 0;
    new_led_states |= greenOn ? NETLIST_R_ON : 0;
  }
  // ---------------------------------------- SWAPPING YELLOW AND GREEN
  else if (flag == SWAPCOLORS_FLAG_YELLOW_GREEN) {

    // swap the yellow anode and green anode rows
    for (uint8_t i = 0; i < 8; ++i) {
      uint8_t scratch = pruned_netlist[NL_YA][i];
      pruned_netlist[NL_YA][i] = pruned_netlist[NL_GA][i];
      pruned_netlist[NL_GA][i] = scratch;
    }
    // swap the yellow cathode and green cathode rows
    for (uint8_t i = 0; i < 8; ++i) {
      uint8_t scratch = pruned_netlist[NL_YC][i];
      pruned_netlist[NL_YC][i] = pruned_netlist[NL_GC][i];
      pruned_netlist[NL_GC][i] = scratch;
    }
    // swap the yellow anode and green anode cols
    for (uint8_t i = 0; i < 8; ++i) {
      uint8_t scratch = pruned_netlist[i][NL_YA];
      pruned_netlist[i][NL_YA] = pruned_netlist[i][NL_GA];
      pruned_netlist[i][NL_GA] = scratch;
    }
    // swap the yellow cathode and green cathode cols
    for (uint8_t i = 0; i < 8; ++i) {
      uint8_t scratch = pruned_netlist[i][NL_YC];
      pruned_netlist[i][NL_YC] = pruned_netlist[i][NL_GC];
      pruned_netlist[i][NL_GC] = scratch;
    }

    // reflect the matrix across the diagonal
    for (uint8_t y = 0; y < 8; ++y)
      for (uint8_t x = 0; x < 8; ++x)
        if (pruned_netlist[y][x] || pruned_netlist[x][y])
          pruned_netlist[y][x] = pruned_netlist[x][y] = 1;

    // swap LED on states
    bool redOn = led_states & NETLIST_R_ON;
    bool yellowOn = led_states & NETLIST_Y_ON;
    bool greenOn = led_states & NETLIST_G_ON;

    new_led_states |= redOn ? NETLIST_R_ON : 0;
    new_led_states |= yellowOn ? NETLIST_G_ON : 0;
    new_led_states |= greenOn ? NETLIST_Y_ON : 0;
  }
  else {
    abort(); // this should never happen
  }

  /* printf("Turned into: \n"); */

  // print the netlist
  /* putchar('\n'); */
  /* for (uint8_t y = 0; y < 8; ++y) { */
  /*   for (uint8_t x = 0; x < 8; ++x) { */
  /*     putchar(pruned_netlist[y][x] ? '1' : '0'); putchar(' '); */
  /*   } */
  /*   putchar('\n'); */
  /* } */

  // repack the netlist into a single uint32_t
  uint32_t repacked_netlist = new_led_states; // was 0 before
  {
    uint32_t bitmask = 1;
    for (uint8_t i = 0; i < 27; ++i) {
      if (pruned_netlist[packedNetlistY[i]][packedNetlistX[i]])
        repacked_netlist |= bitmask;
      bitmask <<= 1;
    }
////    printf("0x%08x\n", repacked_netlist);
  }

  // scan the original list to see if we had that permutation
  // UNCOMMENT FOR DEBUGGING
  /* for (size_t i = 0; i < NELEMS(unsorted_netlists); ++i) { */
  /*   uint32_t nl = unsorted_netlists[i]; */
  /*   if (repacked_netlist == nl) */
  /*     printf("FOUND PERMUTATION!\n"); */
  /* } */

  // XXX change this to return the permuted netlist_and_led_states
  return repacked_netlist;
}

void Insert(rbtree_t* tree, uint32_t netlist)
{
  struct netlist n = { netlist };
  rbtree_setinsert(tree, netlist_node_new(&n));
}

/* This will generate a sorted list of netlists, with the solution
   bits in the top 3 MSB, so the ConsultOracle2 function can find
   things using binary search */
int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  // Initialize the Red-Black Tree used for sorting the netlists (ignoring the solution bits)
  netlist_node_t myNil;
  rbtree_node_t *myNilRef = (rbtree_node_t *)&myNil;
  rbtree_t tree;
  rbtree_init(&tree, myNilRef, sizeof(netlist_node_t), netlist_node_compare);

////  puts("\nUnsorted\n");

  int inputCount = 0;
  for (size_t i = 0; i < NELEMS(unsorted_netlists); ++i) {
    //printf("0x%08x,\n", unsorted_netlists[i]);
    /* struct netlist n = { unsorted_netlists[i] }; */
    /* rbtree_setinsert(&tree, netlist_node_new(&n)); */

    // ryg
    Insert(&tree, unsorted_netlists[i]);
    uint32_t yrg = SwapColors(unsorted_netlists[i], SWAPCOLORS_FLAG_RED_YELLOW);
    Insert(&tree, yrg);
    uint32_t gyr = SwapColors(unsorted_netlists[i], SWAPCOLORS_FLAG_RED_GREEN);
    Insert(&tree, gyr);
    uint32_t rgy = SwapColors(unsorted_netlists[i], SWAPCOLORS_FLAG_YELLOW_GREEN);
    Insert(&tree, rgy);
    uint32_t ygr = SwapColors(rgy, SWAPCOLORS_FLAG_RED_GREEN);
    Insert(&tree, ygr);
    uint32_t gry = SwapColors(yrg, SWAPCOLORS_FLAG_RED_GREEN);
    Insert(&tree, gry);
    inputCount++;
  }

  // XXX - TODO - Here is where we should loop over unsorted_netlists, swapping the colors around to ensure we have full coverage

////  puts("\nUnique and Sorted\n");

  puts("const uint32_t sorted_netlists_and_led_states[] PROGMEM =");
  puts("{");

  int outputCount = 0;
  // Iterate through the sorted netlists
  for (rbtree_node_t *itr = rbtree_minimum(&tree);
       itr != myNilRef;
       itr = rbtree_successor(&tree, itr)) {

    uint32_t netlist = ((netlist_node_t *)itr)->n.netlist_and_flags;
    printf("  0x%08x,\n", netlist);
    outputCount++;
  }

  puts("};");

  // Just for fun, dump the state of the red-black tree
  #include "rbtree/rbtree+debug.h"
  FILE *output = fopen("output.dot", "w");
  rbtree_print_dot(&tree, output, netlist_print_dot, 0, 0);
  fclose(output);

  // Cleanup the Red-Black Tree
  rbtree_postorderwalk(&tree, netlist_node_delete, 0);
  rbtree_destroy(&tree);

  fprintf(stderr, "\nPermuting known netlists\n");
  fprintf(stderr, "Input Count: %d Output Count: %d\n\n", inputCount, outputCount);
  return 0;
}
