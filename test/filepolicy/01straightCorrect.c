#include <stdio.h>

void
foo() {
  FILE *outfile = fopen("/tmp/doesntmatter", "w");
  fprintf(outfile, "Marvelous!\n");
  fclose(outfile);
}

