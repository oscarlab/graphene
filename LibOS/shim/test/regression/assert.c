#include <assert.h>

/* Test if the correct error codes is propagated. This should report 134 (128 + 6 where 6 is
 * SIGABRT) as its return code. */

int main(int argc, char* arvg[]) {
    assert(0);
}
