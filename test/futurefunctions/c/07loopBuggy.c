#include <stdio.h>

extern unsigned conds[10];

int
main() {
  FILE *outfile = fopen("/tmp/doesntmatter", "w");

  for (unsigned i = 0, e = 10; i != e; ++i) {
    fprintf(outfile, "Marvelous!\n");
    fclose(outfile);
    outfile = fopen("/tmp/doesntmatter", "w");
    if (conds[i]) {
      fclose(outfile);
    }
  }

  fprintf(outfile, "Done!\n");
  fclose(outfile);
  return 0;
}

