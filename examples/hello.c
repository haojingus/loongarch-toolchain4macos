/* hello.c — minimal smoke test for the LoongArch cross-toolchain.
 *
 * Build:
 *   loongarch64-linux-gnu-gcc -O2 hello.c -o hello
 *   scp hello target:/tmp/ && ssh target /tmp/hello
 *
 * Expected interpreter (老世界 ABI):
 *   $ readelf -l hello | grep INTERP
 *   [Requesting program interpreter: /lib64/ld.so.1]
 */
#include <stdio.h>

int main(void) {
    printf("hello loongarch\n");
    return 0;
}
