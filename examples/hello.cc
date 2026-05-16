// hello.cc — C++ smoke test for the LoongArch cross-toolchain.
//
// Build (single-file deployable, ~10MB with libstdc++/libgcc baked in):
//   loongarch64-linux-gnu-g++ -O2 -static-libstdc++ -static-libgcc hello.cc -o hello++
//
// Why static-link libstdc++: Loongnix 20 ships libstdc++ 6.0.25 (from GCC 8.3),
// which lacks GLIBCXX_3.4.32 that GCC 14 produces. Dynamic-link would crash with
// "version 'GLIBCXX_3.4.32' not found" on the target.

#include <iostream>

int main() {
    std::cout << "hello loongarch++\n";
    return 0;
}
