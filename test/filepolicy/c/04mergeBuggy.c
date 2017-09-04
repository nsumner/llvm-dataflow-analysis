#include <stdio.h>

extern unsigned cond;

int
main() {
  FILE *outfile = fopen("/tmp/doesntmatter", "w");
  if (cond) {
    fprintf(outfile, "Marvelous!\n");
  } else {
    fclose(outfile);
  }
  fprintf(outfile, "Spectacular!\n");
  return 0;
}

