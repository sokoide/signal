#include <stdio.h>
#include <stdlib.h>
int main(void) { int rc = system("./tutorial/signal/step04_sigsuspend >/dev/null"); puts(rc == 0 ? "PASS contract step 4" : "FAIL contract step 4"); return rc == 0 ? 0 : 1; }
