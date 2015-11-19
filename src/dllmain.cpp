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
    assert(ntHeaders->Signature == IMAGE_NT_SIGNATURE);
    assert(ntHeaders->OptionalHeader.NumberOfRvaAndSizes > 0);

    return (const IMAGE_EXPORT_DIRECTORY*)(imageBase +
        ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
}


static void* FindExport (const char exportName[]) {
    auto imageBase = (const uint8_t*)GetModuleHandleA(nullptr);
    assert(imageBase != nullptr);

    auto exports = GetExports(imageBase);
    auto names = (const uint32_t*)(imageBase + exports->AddressOfNames);
    auto ordinals = (const uint16_t*)(imageBase + exports->AddressOfNameOrdinals);
    auto functions = (const uint32_t*)(imageBase + exports->AddressOfFunctions);

    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        auto name = (const char *)(imageBase + names[i]);

        auto result = CompareStringA(LOCALE_INVARIANT,
                                     0,
                                     name,
                                     -1,
                                     exportName,
                                     -1);

        if (result == CSTR_EQUAL) {
            auto ordinal = ordinals[i];
            auto function = (void*)(imageBase + functions[ordinal]);
            return function;
        }
    }

    return nullptr;
}

template <class T>
static void InstallHook() {
    auto & hook = T::Get();
    auto * name = T::GetExportName();
    auto * src = FindExport(name);

    if (!src) {
        WriteToLog("Could not find export %s, hook failed.\n", name);
        return;
    }

    hook.SetupHook(src, &T::Hook);
    hook.Hook();
}

template <class T>
struct Hook {
    static PLH::X64Detour& Get () {
        static PLH::X64Detour s_detour;
        return s_detour;
    }

    template <typename Fn>
    static Fn GetOriginal (Fn) {
        auto & hook = Get();
        union { void* v; Fn f; } o;
        o.v = hook.GetOriginal<void*>();
        return o.f;
    }
};

#define EXPORT_NAME(s)    static const char * GetExportName() { return s; }

struct GetXScaleHook : Hook<GetXScaleHook> {
    EXPORT_NAME("?GetXScale@?$Matrix2x4@M@Render@Scaleform@@QEBAMXZ");

    float m_f;

    float Hook () const {
        auto original = GetOriginal(&GetXScaleHook::Hook);
        auto f = (this->*original)();
        printf_s("orig: %f\n", f);
        return 5.0f;
    }
};


BOOL APIENTRY DllMain (HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved) {
    REF(hModule, lpReserved);

    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            s_handle = CreateFileA("test.log", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            InstallHook<GetXScaleHook>();
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

