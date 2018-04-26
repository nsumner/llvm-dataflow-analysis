#include <stdio.h>

extern unsigned cond;

int
main() {
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
  return 0;
}

