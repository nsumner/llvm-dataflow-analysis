#include <stdio.h>


void
ralph(int a, int b, int c) {
  b = a * c / 2000;
  printf("%d, %d, %d\n", a, b, c);
}


void
joe(int i, int j, int k) {
  int l = 2 * k;
  int m;
  if (j == 100) {
    m = 10 * j;
  } else {
    m = i;
  }
  ralph(l, m, k);
  int o = m * 2;
  int q = 2;
  ralph(o, q, k);
  printf("%d, %d, %d, %d\n", q, m, o, l);
}


int
main() {
  joe(10, 100, 1000);
  return 0;
}

