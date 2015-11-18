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


static const IMAGE_EXPORT_DIRECTORY* GetExports (const uint8_t* imageBase) {
    auto dosHeader = (const IMAGE_DOS_HEADER*)imageBase;
    assert(dosHeader->e_magic == IMAGE_DOS_SIGNATURE);

    auto ntHeaders = (const IMAGE_NT_HEADERS*)(imageBase + dosHeader->e_lfanew);
    assert(header->Signature == IMAGE_NT_SIGNATURE);
    assert(header->OptionalHeader.NumberOfRvaAndSizes > 0);

    return (const IMAGE_EXPORT_DIRECTORY*)(imageBase +
        ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
}


static const void* FindGetXScale () {
    auto imageBase = (const uint8_t*)GetModuleHandleA(nullptr);
    assert(imageBase != nullptr);

    auto exports = GetExports(imageBase);
    auto names = (const uint32_t*)(imageBase + exports->AddressOfNames);
    auto ordinals = (const uint16_t*)(imageBase + exports->AddressOfNameOrdinals);
    auto functions = (const uint32_t*)(imageBase + exports->AddressOfFunctions);

    for (DWORD i = 0; i < exports->NumberOfFunctions; i++) {
        WriteToLog("%08x\n", functions[i]);
    }

    for (DWORD i = 0; i < exports->NumberOfNames; i++) {
        auto name = (const char *)(imageBase + names[i]);

        auto result = CompareStringA(LOCALE_INVARIANT,
                                     0,
                                     name,
                                     -1,
                                     "?GetXScale@?$Matrix2x4@M@Render@Scaleform@@QEBAMXZ",
                                     -1);

        if (result == CSTR_EQUAL) {
            auto ordinal = ordinals[i];
            auto function = (void*)(imageBase + functions[ordinal]);
            return function;
        }
    }

    return nullptr;
}


static void ApplyHook () {
    auto getXScale = FindGetXScale();
    if (!getXScale) {
        WriteToLog("Unable to find GetXScale\n");
        return;
    }

    WriteToLog("Found GetXScale at %p\n", getXScale);
}


BOOL APIENTRY DllMain (HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved) {
    REF(hModule, lpReserved);

    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            s_handle = CreateFileA("test.log", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            ApplyHook();
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

