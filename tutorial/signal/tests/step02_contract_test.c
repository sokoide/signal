#include <stdio.h>
#include <stdlib.h>
int main(void) { int rc = system("./tutorial/signal/step02_sigaction >/dev/null"); puts(rc == 0 ? "PASS contract step 2" : "FAIL contract step 2"); return rc == 0 ? 0 : 1; }
