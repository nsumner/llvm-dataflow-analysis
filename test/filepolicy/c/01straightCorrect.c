#include <stdio.h>

int
main() {
  FILE *outfile = fopen("/tmp/doesntmatter", "w");
  fprintf(outfile, "Marvelous!\n");
  fclose(outfile);
  return 0;
}

