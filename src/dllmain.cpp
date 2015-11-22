// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

///
// Logging
///

#define LOG(fmt, ...)   Log::Write(__FUNCTION__, fmt, ##__VA_ARGS__)

namespace Log {
    
    static HANDLE s_handle;

    static void Open (const char filename[]) {
        s_handle = CreateFileA(filename,
                                  GENERIC_WRITE,
                                  0,
                                  nullptr,
                                  CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    }

    static void Close () {
        CloseHandle(s_handle);
    }

    void Write (const char func[], const char str[], ...) {
        char fmt[0x100];
        strcpy_s(fmt, func);
        strcat_s(fmt, ": ");
        strcat_s(fmt, str);
        strcat_s(fmt, "\r\n");

        char buffer[0x100];
        va_list args;
        va_start(args, str);
        auto len = (DWORD)vsprintf_s(buffer, fmt, args);
        va_end(args);

        WriteFile(s_handle, buffer, len, &len, nullptr);
    }

}


///
// Hooking
///

static void* HookVTable (void**      table,
                         size_t      index,
                         const void* replacement) {
    const auto addr = table + index;
    DWORD prevProtection;
    
    if (!VirtualProtect(addr, sizeof(*addr), PAGE_READWRITE, &prevProtection)) {
        LOG("Failed to make %p read/writable (%lu)", addr, GetLastError());
        return nullptr;
    }

    const auto prev = (void*)_InterlockedExchange64((LONG64*)addr, (LONG64)replacement);

    if (!VirtualProtect(addr, sizeof(addr), prevProtection, &prevProtection)) {
        LOG("Failed to restore protection flags for %p (%lu)", addr, GetLastError());
    }

    return prev;
}

static void* GetImageBase () {
    static auto s_base = GetModuleHandleA(nullptr);
    assert(s_base != nullptr);
    return s_base;
}

static IMAGE_SECTION_HEADER* FindSection (const char* name,
                                          size_t      length) {
    auto imageBase = GetImageBase();

    auto dosHeader = (const IMAGE_DOS_HEADER*)imageBase;
    assert(dosHeader->e_magic == IMAGE_DOS_SIGNATURE);
    
    auto ntHeaders = (const IMAGE_NT_HEADERS*)((uintptr_t)imageBase + dosHeader->e_lfanew);
    assert(ntHeaders->Signature == IMAGE_NT_SIGNATURE);

    auto section = IMAGE_FIRST_SECTION(ntHeaders);

    for (unsigned i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++section, ++i) {
        auto match = CompareStringA(LOCALE_INVARIANT,
                                    0,
                                    name,
                                    (int)length,
                                    (const char*)section->Name,
                                    (int)sizeof(section->Name));
        if (match == CSTR_EQUAL)
            return section;
    }

    return nullptr;
}

template <class T, class F>
struct VTableInvoker;

template <class T, class Ret, class... Args>
struct VTableInvoker<T, Ret(Args...)> {
    using FnType = Ret(*)(T&, Args...);

    FnType addr;
    T& obj;

    VTableInvoker(T& obj, size_t index)
        : addr((FnType)obj.vftable[index])
        , obj(obj)
    { }

    Ret Invoke (Args... args) {
        return (*addr)(obj, args...);
    }
};

///
// Scaleform hooks
///

enum class ViewScaleMode : uint32_t {
    NoScale,
    ShowAll,
    ExactFit,
    NoBorder,
};

struct Viewport {
    uint32_t bufferWidth;
    uint32_t bufferHeight;
    uint32_t left;
    uint32_t top;
    uint32_t width;
    uint32_t height;
    uint32_t clipLeft;
    uint32_t clipTop;
    uint32_t clipWidth;
    uint32_t clipHeight;
    uint32_t flags;
    float scale;
    float aspectRatio;
};

struct MovieDef {
    void** vftable;
};

struct Movie {
    void** vftable;
    uint8_t padding1[0x40];
    MovieDef* movieDef;
    uint8_t padding2[0xC0];
    float ViewportMatrix[8];
    //uint8_t padding2[]
    //Viewport viewport;
    //float pixelScale;
    //float viewScaleX;
    //float viewScaleY;
    //float viewOffsetX;
    //float viewOffsetY;
    //ViewScaleMode viewScaleMode;
    //uint32_t alignType;
};

using Movie_SetViewport_t = void(*)(Movie&, const Viewport&);
static Movie_SetViewport_t s_origSetViewport;

using Movie_SetViewScaleMode_t = void(*)(Movie&, ViewScaleMode mode);
static Movie_SetViewScaleMode_t s_origSetViewScaleMode;

static const char* GetMovieFilename(const Movie& movie) {
    auto invoker = VTableInvoker<MovieDef, const char*()>(*movie.movieDef, 12);
    return invoker.Invoke();
}

static void Movie_SetViewport_Hook (Movie&    movie,
                                    Viewport& viewport) {
    //static const char * s_files[] = {
    //    "Interface/ScopeMenu.swf",
    //};

    //auto filename = GetMovieFilename(movie);

    //for (auto & file : s_files) {
    //    if (strcmp(file, filename) == 0) {
    //        LOG("%s:", file);
    //        LOG("Viewport(%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %x, %f, %f)",
    //            viewport.bufferWidth,
    //            viewport.bufferHeight,
    //            viewport.left,
    //            viewport.top,
    //            viewport.width,
    //            viewport.height,
    //            viewport.clipLeft,
    //            viewport.clipTop,
    //            viewport.clipWidth,
    //            viewport.clipHeight,
    //            viewport.flags,
    //            viewport.scale,
    //            viewport.aspectRatio);
    //    }
    //}

    // 16:9-IFY!
    //const auto maxWidth = viewport.bufferHeight * 16 / 9;
    //const auto maxHeight = viewport.bufferWidth * 9 / 16;

    //if (viewport.width > maxWidth) {
    //    viewport.left += (viewport.width - maxWidth) / 2;
    //    viewport.width = maxWidth;
    //}

    //if (viewport.height > maxHeight) {
    //    viewport.top += (viewport.height - maxHeight) / 2;
    //    viewport.height = maxHeight;
    //}

    //LOG("Matrix(%f, %f, %f, %f, %f, %f, %f, %f), %s",
    //    movie.ViewportMatrix[0],
    //    movie.ViewportMatrix[1],
    //    movie.ViewportMatrix[2],
    //    movie.ViewportMatrix[3],
    //    movie.ViewportMatrix[4],
    //    movie.ViewportMatrix[5],
    //    movie.ViewportMatrix[6],
    //    movie.ViewportMatrix[7],
    //    filename);

    s_origSetViewport(movie, viewport);
}

struct ScaleModeMapping {
    const char *  filename;
    ViewScaleMode mode;
};

static void Movie_SetViewScaleMode_Hook (Movie&        movie,
                                         ViewScaleMode mode) {
    static ScaleModeMapping s_mappings[] = {
        { "Interface/HUDMenu.swf",       ViewScaleMode::ShowAll  },
        { "Interface/ScopeMenu.swf",     ViewScaleMode::ExactFit },
        { "Interface/FaderMenu.swf",     ViewScaleMode::ShowAll  },
        { "Interface/ButtonBarMenu.swf", ViewScaleMode::ShowAll  },
    };

    auto filename = GetMovieFilename(movie);
    auto overridden = false;

    for (auto & mapping : s_mappings) {
        if (strcmp(filename, mapping.filename) == 0) {
            LOG("Overriding scale mode: old=%u, new=%u, filename=%s", mode, mapping.mode, filename);
            mode = mapping.mode;
            overridden = true;
        }
    }

    if (!overridden)
        LOG("Using requested scale mode: mode=%u, filename=%s", mode, filename);

    s_origSetViewScaleMode(movie, mode);
}

static void SetupScaleformHooks () {
    const auto rdata = FindSection(".rdata", 6);
    const auto text = FindSection(".text", 5);

    if (rdata) {
        const auto vtableMovie = (void**)((uint8_t*)GetImageBase() + rdata->VirtualAddress + 0x2DDBB0);
        s_origSetViewport = (Movie_SetViewport_t)HookVTable(vtableMovie, 12, Movie_SetViewport_Hook);
        s_origSetViewScaleMode = (Movie_SetViewScaleMode_t)HookVTable(vtableMovie, 14, Movie_SetViewScaleMode_Hook);
    }
}

//static bool DataCompare (const uint8_t* data,
//                         const uint8_t* mask,
//                         const char*    sMask) {
//    for (; *sMask; ++sMask, ++data, ++mask)
//        if (*sMask == 'x' && *data != *mask)
//            return false;
//    return *sMask != 0;
//}
//
//
//static void* FindPattern (uintptr_t      address,
//                          uintptr_t      term,
//                          const uint8_t* mask,
//                          const char*    sMask) {
//    auto length = term - address;
//    for (size_t i = 0; i < length; ++i)
//        if (DataCompare((const uint8_t*)(address + i), mask, sMask))
//            return (void*)(address + i);
//    return nullptr;
//}
//
//
//static void* FindExact (uintptr_t      address,
//                        uintptr_t      term,
//                        const uint8_t* data,
//                        size_t         bytes) {
//    for (; address + bytes <= term; ++address) {
//        if (memcmp((void*)address, data, bytes) == 0)
//            return (void*)address;
//    }
//    return nullptr;
//}
//
//
//
//
//static const char* FindString(IMAGE_SECTION_HEADER* section,
//    const char*           string) {
//    auto length = strlen(string);
//    auto address = (uintptr_t)GetImageBase() + section->VirtualAddress;
//    auto term = address + section->SizeOfRawData;
//    return (const char*)FindExact(address, term, (const uint8_t*)string, length);
//}


//
//static const IMAGE_EXPORT_DIRECTORY* GetExports (const uint8_t* imageBase) {
//    auto dosHeader = (const IMAGE_DOS_HEADER*)imageBase;
//    assert(dosHeader->e_magic == IMAGE_DOS_SIGNATURE);
//
//    auto ntHeaders = (const IMAGE_NT_HEADERS*)(imageBase + dosHeader->e_lfanew);
//    assert(ntHeaders->Signature == IMAGE_NT_SIGNATURE);
//    assert(ntHeaders->OptionalHeader.NumberOfRvaAndSizes > 0);
//
//    return (const IMAGE_EXPORT_DIRECTORY*)(imageBase +
//        ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
//}
//
//
//static void* FindExport (const char exportName[]) {
//    auto imageBase = (const uint8_t*)GetModuleHandleA(nullptr);
//    assert(imageBase != nullptr);
//
//    auto exports = GetExports(imageBase);
//    auto names = (const uint32_t*)(imageBase + exports->AddressOfNames);
//    auto ordinals = (const uint16_t*)(imageBase + exports->AddressOfNameOrdinals);
//    auto functions = (const uint32_t*)(imageBase + exports->AddressOfFunctions);
//
//    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
//        auto name = (const char *)(imageBase + names[i]);
//
//        auto result = CompareStringA(LOCALE_INVARIANT,
//                                     0,
//                                     name,
//                                     -1,
//                                     exportName,
//                                     -1);
//
//        if (result == CSTR_EQUAL) {
//            auto ordinal = ordinals[i];
//            auto function = (void*)(imageBase + functions[ordinal]);
//            return function;
//        }
//    }
//
//    return nullptr;
//}
//
//template <class T>
//static void InstallHook() {
//    auto & hook = T::Get();
//    auto * name = T::GetExportName();
//    auto * src = FindExport(name);
//
//    if (!src) {
//        WriteToLog("Could not find export %s, hook failed.\n", name);
//        return;
//    }
//
//    WriteToLog("Hooking %s at %p\n", name, src);
//    hook.SetupHook(src, &T::Hook);
//    hook.Hook();
//}
//
//template <class T>
//struct Hook {
//    static PLH::X64Detour& Get () {
//        static PLH::X64Detour s_detour;
//        return s_detour;
//    }
//
//    template <typename Fn>
//    static Fn GetOriginal (Fn) {
//        auto & hook = Get();
//        union { void* v; Fn f; } o;
//        o.v = hook.GetOriginal<void*>();
//        return o.f;
//    }
//};
//
//#define EXPORT_NAME(s)    static const char * GetExportName() { return s; }
//
//struct GetXScaleHook : Hook<GetXScaleHook> {
//    EXPORT_NAME("?GetXScale@?$Matrix2x4@M@Render@Scaleform@@QEBAMXZ");
//
//    float m_f;
//
//    float Hook () const {
//        auto original = GetOriginal(&GetXScaleHook::Hook);
//        auto f = (this->*original)();
//        printf_s("orig: %f\n", f);
//        return f;
//    }
//};




//template <class T>
//struct Hook {
//    static PLH::X64Detour& Get () {
//        static PLH::X64Detour s_detour;
//        return s_detour;
//    }
//
//    template <typename Fn>
//    static Fn GetOriginal (Fn) {
//        auto & hook = Get();
//        union { void* v; Fn f; } o;
//        o.v = hook.GetOriginal<void*>();
//        return o.f;
//    }
//};
//
//#define HOOK_PATTERN(x) static const char * GetPattern () { return x; }
//#define HOOK_MASK(x)    static const char * GetMask () { return x; }
//
//struct Viewport {
//    int      BufferWidth, BufferHeight;
//    int      Left, Top;
//    int      Width, Height;
//    int      ScissorLeft, ScissorTop;
//    int      ScissorWidth, ScissorHeight;
//    unsigned Flags;
//    float Scale;
//    float AspectRatio;
//};
//
//static_assert(sizeof(Viewport) == 0x34, "bad size");
//
//struct CreateInstanceHook : Hook<CreateInstanceHook> {
//    HOOK_PATTERN("\x8D\x2E\xB6\x0D\xAD\x3A\x93\x00\x00\x00\x00\xAA\x97\xF0\xE7\xF5");
//    HOOK_MASK("xxxxxxx????xxxxx");
//
//    const uint32_t SET_VIEWPORT_INDEX = 12;
//
//    void SetViewportHook ()
//
//    void* Hook (const void* memParams,
//                bool        initFirstFrame,
//                const void* actionControl,
//                const void* queue) {
//        static long s_swapped = 0;
//        static PLH::VFuncSwap s_swap;
//
//        auto original = GetOriginal(&CreateInstanceHook::Hook);
//        auto movie = (this->*original)(memParams, initFirstFrame, actionControl, queue);
//
//        if (!_InterlockedExchange(&s_swapped, 1)) {
//            s_swap.SetupHook((BYTE**)movie, SET_VIEWPORT_INDEX, )
//        }
//    }
//};




BOOL APIENTRY DllMain (HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved) {
    REF(hModule, lpReserved);

    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH: {
            Log::Open("hook.log");
            //Sleep(10000);
            SetupScaleformHooks();

            //auto section = FindSection(".rdata", 6);
            //if (!section) {
            //    WriteToLog("Could not find .rdata section\n");
            //    break;
            //}

            //auto showAll  = FindString(section, "SHOW_ALL");
            //auto exactFit = FindString(section, "EXACT_FIT");
            //if (!showAll || !exactFit) {
            //    WriteToLog("Could not find either \"SHOW_ALL\" or \"EXACT_FIT\"\n");
            //    break;
            //}

            //WriteToLog("\"EXACT_FIT\" at %p\n", exactFit);
            //WriteToLog("\"SHOW_ALL\" at %p\n", showAll);

            //auto exactFitRef = FindExact((uintptr_t)GetImageBase() + section->VirtualAddress,
            //                             (uintptr_t)GetImageBase() + section->VirtualAddress + section->SizeOfRawData,
            //                             (const uint8_t*)&exactFit,
            //                             sizeof(exactFit));

            //WriteToLog("Patching pointer at %p to %p\n", exactFitRef, showAll);
            //*(const void**)exactFitRef = showAll;

            break;
        }

        case DLL_PROCESS_DETACH:
            Log::Close();
            break;

        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }

    return TRUE;
}

