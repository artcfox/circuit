#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

void PrintShuffled(void)
{
  uint8_t randomized[64];
  for (uint8_t i = 0; i < 64; ++i)
    randomized[i] = i;
  for (uint8_t i = 0; i < 200; ++i) {
    uint8_t pos1 = rand() % 64;
    uint8_t pos2 = rand() % 64;
    uint8_t tmp = randomized[pos1];
    randomized[pos1] = randomized[pos2];
    randomized[pos2] = tmp;
  }

  printf("    { ");
  for (uint8_t i = 0; i < 64; ++i)
    printf("%u, ", randomized[i]);
  printf("},\n");
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  
  printf("const uint8_t sparkle_effect[][64] PROGMEM =\n  {\n");
  PrintShuffled();
  PrintShuffled();
  PrintShuffled();
  PrintShuffled();
  printf("  };\n");
  return 0;
}
