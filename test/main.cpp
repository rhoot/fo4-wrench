#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>

extern "C" __declspec(dllexport) __declspec(noinline) int Test (int a, int b) {

    return a + b;
}

int main(int argc,
         char** argv) {
    LoadLibraryA("XInput1_3");
    Sleep(2000);
    printf("%d\n", Test(2, 3));
    getchar();

    return 0;
}