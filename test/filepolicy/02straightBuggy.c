#include <stdio.h>

void
foo() {
  FILE *outfile = fopen("/tmp/doesntmatter", "w");
  fclose(outfile);
  fprintf(outfile, "Marvelous!\n");
}

