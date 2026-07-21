#include <stdio.h>
#include <stdlib.h>
int main(void) { int rc = system("./tutorial/signal/step06_sigwait >/dev/null"); puts(rc == 0 ? "PASS contract step 6" : "FAIL contract step 6"); return rc == 0 ? 0 : 1; }
