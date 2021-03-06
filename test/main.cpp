﻿// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>

#pragma optimize("", off)
extern "C" __declspec(dllexport) __declspec(noinline) int Test (int a, int b)
{
    int numA = a;
    int numB = b;
    int added = a + b;
    return added;
}
#pragma optimize("", on)

int main (int    argc,
          char** argv)
{
    LoadLibraryA("XInput1_3");
    Sleep(2000);
    printf("%d\n", Test(2, 3));
    getchar();

    return 0;
}
