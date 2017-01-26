#include <stdio.h>

void
foo(unsigned cond) {
  FILE *outfile;
  if (cond) {
    outfile = fopen("/tmp/doesntmatter1", "w");
    fprintf(outfile, "Fascinating!\n");
  } else {
    outfile = fopen("/tmp/doesntmatter2", "w");
    fprintf(outfile, "Amazing!\n");
  }
  fprintf(outfile, "Marvelous!\n");
  fclose(outfile);
}

