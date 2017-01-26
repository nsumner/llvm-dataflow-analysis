#include <stdio.h>

void
foo(unsigned cond) {
  FILE *outfile = fopen("/tmp/doesntmatter", "w");
  if (cond) {
    fprintf(outfile, "Marvelous!\n");
  } else {
    fprintf(outfile, "Spectacular!\n");
  }
  fclose(outfile);
}

