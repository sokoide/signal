#include <stdio.h>
#include <stdlib.h>
int main(void) { int rc = system("./tutorial/signal/step05_selfpipe >/dev/null"); puts(rc == 0 ? "PASS contract step 5" : "FAIL contract step 5"); return rc == 0 ? 0 : 1; }
