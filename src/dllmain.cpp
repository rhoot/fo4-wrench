// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

static HANDLE s_handle;


void WriteToLog (const char str[], ...) {
    char buffer[0x200];
    va_list args;
    va_start(args, str);
    DWORD len = (DWORD)vsprintf_s(buffer, str, args);
    va_end(args);

    WriteFile(s_handle, buffer, len, &len, nullptr);
}


BOOL APIENTRY DllMain (HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved) {
    REF(hModule, lpReserved);

    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            s_handle = CreateFileA("test.log", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            break;

        case DLL_PROCESS_DETACH:
            CloseHandle(s_handle);
            break;

        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }

    return TRUE;
}

