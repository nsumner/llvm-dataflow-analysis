#include <stdio.h>

void
foo() {
  FILE *outfile = fopen("/tmp/doesntmatter", "w");

  for (unsigned i = 0, e = 10; i != e; ++i) {
    fprintf(outfile, "Marvelous!\n");
    fclose(outfile);
    outfile = fopen("/tmp/doesntmatter", "w");
  }

  fprintf(outfile, "Done!\n");
  fclose(outfile);
}

