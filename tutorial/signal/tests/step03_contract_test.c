#include <stdio.h>
#include <stdlib.h>
int main(void) { int rc = system("./tutorial/signal/step03_mask_pending >/dev/null"); puts(rc == 0 ? "PASS contract step 3" : "FAIL contract step 3"); return rc == 0 ? 0 : 1; }
