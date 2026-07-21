#include <stdio.h>
#include <stdlib.h>
int main(void) { int rc = system("./tutorial/signal/step01_basics >/dev/null"); puts(rc == 0 ? "PASS contract step 1" : "FAIL contract step 1"); return rc == 0 ? 0 : 1; }
