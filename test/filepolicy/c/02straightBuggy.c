#include <stdio.h>

int
main() {
  FILE *outfile = fopen("/tmp/doesntmatter", "w");
  fclose(outfile);
  fprintf(outfile, "Marvelous!\n");
  return 0;
}

