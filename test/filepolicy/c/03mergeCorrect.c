#include <stdio.h>

extern unsigned cond;

int
main() {
  FILE *outfile = fopen("/tmp/doesntmatter", "w");
  if (cond) {
    fprintf(outfile, "Marvelous!\n");
  } else {
    fprintf(outfile, "Spectacular!\n");
  }
  fclose(outfile);
  return 0;
}

