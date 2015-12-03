// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#include "stdafx.h"

#include "config.h"
#include "dx.h"
#include "hooks.h"
#include "util.h"


///
// UI movie scale
///

namespace uiscale {

    enum class ViewScaleMode : uint32_t {
        NoScale = 0,
        ShowAll = 1,
        ExactFit = 2,
        NoBorder = 3,
        Count = 4
    };

    struct MovieDef {
        using GetFilename = hooks::Function<12, const char* (MovieDef&)>;

        void** vftable;
    };

    struct Movie {
        using SetViewScaleMode = hooks::Function<14, void(Movie&, ViewScaleMode)>;

        void** vftable;
        uint8_t padding1[0x40];
        MovieDef* movieDef;
        uint8_t padding2[0xC0];
        float ViewportMatrix[8];
    };


    ///
    // Data
    ///

    static Movie::SetViewScaleMode::Fn* s_movieSetViewScaleMode;

    static const char* s_names[] = { "NoScale",
                                     "ShowAll",
                                     "ExactFit",
                                     "NoBorder", };

    static_assert(ArraySize(s_names) == (size_t)ViewScaleMode::Count, "missing names");


    ///
    // Helpers
    ///

    static const char* ViewScaleName (ViewScaleMode mode)
    {
        return (size_t)mode < ArraySize(s_names)
               ? s_names[(size_t)mode]
               : nullptr;
    }

    static ViewScaleMode ViewScaleFromName (const char name[])
    {
        for (auto i = 0; i < ArraySize(s_names); ++i) {
            if (strcmp(s_names[i], name) == 0) {
                return (ViewScaleMode)i;
            }
        }
        return ViewScaleMode::Count;
    }

    static const char* MovieFilename (const Movie& movie)
    {
        hooks::VfTable table(movie.movieDef->vftable);
        return table.Invoke<MovieDef::GetFilename>(*movie.movieDef);
    }


    ///
    // Hooks
    ///

    static void MovieSetViewScaleMode (Movie&        movie,
                                       ViewScaleMode mode)
    {
        auto modeStr = ViewScaleName(mode);
        auto filename = MovieFilename(movie);
        auto newMode = mode;
        auto newModeStr = config::Get({"UiScale", filename});

        if (newModeStr) {
            newMode = ViewScaleFromName(newModeStr);
        }

        if (newMode != ViewScaleMode::Count && newMode != mode) {
            LOG("Overriding scale mode: old=%s, new=%s, filename=%s", modeStr, newModeStr, filename);
        } else {
            LOG("Using default scale mode: mode=%s, filename=%s", modeStr, filename);
        }

        s_movieSetViewScaleMode(movie, newMode);
    }

    static void OnDeviceCreate (ID3D11DeviceContext*, ID3D11Device*, IDXGISwapChain*)
    {
        const auto imageBase = (uintptr_t)GetModuleHandleA(nullptr);
        const auto text = hooks::FindSection(".text");

        if (text) {
            const auto textStart = imageBase + text->VirtualAddress;
            const auto textEnd = textStart + text->SizeOfRawData;

            // Find Scaleform's Movie constructor, and inject our own functions into its vftable.
            const auto mask = "xxxxxxxxx????xxx????xxxxx?????xxxxxxx";
            const auto pattern = "\x48\x83\xEC\x20"             // sub   rsp, 20h
                                 "\x33\xED"                     // xor   ebp, ebp
                                 "\x48\x8D\x05\x00\x00\x00\x00" // lea   rax, [rip+????]
                                 "\x4C\x8D\x35\x00\x00\x00\x00" // lea   r14, [rip+????]
                                 "\x4C\x89\x31"                 // mov   [rcx], r14
                                 "\xC7\x41\x00\x00\x00\x00\x00" // mov   dword ptr [rcx+8], 1
                                 "\x48\x89\x69\x18"             // mov   [rcx+18h], rbp
                                 "\x48\x89\x01";                // mov   [rcx], rax
            const auto ctor = hooks::FindPattern(textStart, textEnd, pattern, mask);

            if (ctor) {
                auto offset = *(const int32_t*)(ctor + 9);
                auto rip = ctor + 13;
                auto vtable = (void**)(rip + offset);

                hooks::VfTable vftable(vtable);
                vftable.Inject<Movie::SetViewScaleMode>(MovieSetViewScaleMode, &s_movieSetViewScaleMode);
            } else {
                ERR("Could not locate the Movie constructor");
            }
        } else {
            ERR("No .text segment");
        }
    }


    ///
    // Init
    ///

    static void Init ()
    {
        if (!config::GetBool({"Features", "UiScale"})) {
            return;
        }

        // We can't apply the hooks until later. At the point this is called, Steam still has the
        // executable encrypted, so we can't scan for instructions.
        dx::Callbacks callbacks;
        callbacks.afterDeviceCreate = OnDeviceCreate;
        dx::Register(callbacks);
    }

} // namespace uiscale


///
// Backdrop fix
///

namespace backdrop {

    struct StringData {
        uint8_t padding1[16];
        size_t length;
        char str[1];
    };

    struct ReaderStream {
        uint8_t padding1[0x28];
        StringData* filename;
    };

    struct BSResourceNiBinaryStream {
        uint8_t padding1[16];
        ReaderStream* stream;
    };

    struct BSStream {
        uint8_t padding1[0x398];
        BSResourceNiBinaryStream* resource;
    };

    struct BufferData {
        uint8_t padding1[8];
        uint8_t* data;
    };

    struct TriShapeBuffers {
        uint8_t padding1[8];
        BufferData* vertexBuffer;
    };

    struct BSTriShape {
        using Parse_t = hooks::Function<27, bool(BSTriShape&, BSStream&)>;

        uint8_t padding1[0x148];
        TriShapeBuffers* buffers;
        uint8_t padding2[20];
        uint32_t numVerts;
    };


    ///
    // Data
    ///

    static BSTriShape::Parse_t::Fn* s_origTriShapeParse;
    static float s_scale = 1.0f;


    ///
    // Hooks
    ///

    static void OnViewportResize (uint32_t width, uint32_t height)
    {
        const auto aspect = (float)width / (float)height;
        const auto aspect16x9 = 16.f / 9.f;
        s_scale = aspect / aspect16x9;
        LOG("Ratio=%f, applying scale=%f", aspect, s_scale);
    }


    static bool BSTriShapeParse (BSTriShape& shape, BSStream& stream)
    {
        auto result = s_origTriShapeParse(shape, stream);

        if (result && shape.numVerts == 4) {
            const auto filename = stream.resource->stream->filename;
            const auto isHudGlass = strncmp("Meshes\\Interface\\Objects\\HUDGlassFlat.nif", filename->str, filename->length) == 0;

            if (isHudGlass) {
                for (auto i = 0; i < 4; ++i) {
                    auto x = (uint16_t*)(shape.buffers->vertexBuffer->data + i * 20);
                    float f;
                    float32(&f, *x);
                    float16(x, f * s_scale);
                    LOG("scaled vertex %d from %f to %f", i, f, f * s_scale);
                }
            }
        }

        return result;
    }

    static void OnDeviceCreate (ID3D11DeviceContext*, ID3D11Device*, IDXGISwapChain*)
    {
        auto imageBase = GetModuleHandleA(nullptr);
        auto segment = hooks::FindSection(".text");

        if (!segment) {
            ERR("No .text segment found?");
            return;
        }

        auto pattern = "\xE8\x00\x00\x00\x00"            // call ????????
                       "\x48\x8D\x05\x00\x00\x00\x00"    // lea  rax, ????????
                       "\xC6\x87\x58\x01\x00\x00\x03"    // mov  byte ptr [rdi+158h], 3
                       "\x48\x89\x07"                    // mov  [rdi], rax
                       "\x33\xC0"                        // xor  eax, eax
                       "\x89\x87\x60\x01\x00\x00"        // mov  [rdi+160h], eax
                       "\x66\x89\x87\x64\x01\x00\x00";   // mov  [rdi+164h], ax

        auto mask = "x????xxx????xxxxxxxxxxxxxxxxxxxxxxxxx";

        auto location = hooks::FindPattern((uintptr_t)imageBase + segment->VirtualAddress,
                                           (uintptr_t)imageBase + segment->VirtualAddress + segment->SizeOfRawData,
                                           pattern,
                                           mask);

        if (!location) {
            ERR("Unable to find the BSTriShape vftable.");
            return;
        }

        auto rip = location + 12;
        auto rva = *(int32_t*)(location + 8);
        hooks::VfTable vtable = (void**)(rip + rva);
        vtable.Inject<BSTriShape::Parse_t>(BSTriShapeParse, &s_origTriShapeParse);
    }


    ///
    // Init
    ///

    static void Init ()
    {
        if (!config::GetBool({"Features", "BackdropFix"})) {
            return;
        }

        // Steam still has the .text section encrypted at this point, so we're delay hooking until
        // we've got a DX device.
        dx::Callbacks callbacks;
        callbacks.afterDeviceCreate = OnDeviceCreate;
        callbacks.afterViewportResize = OnViewportResize;
        dx::Register(callbacks);
    }

} // namespace backdrop


///
// Main
///

template <size_t N>
static size_t BuildPath (const wchar_t name[], wchar_t (&path)[N])
{
    wchar_t* sysPath = nullptr;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &sysPath))) {
        auto len = swprintf(path, N, L"%s\\My Games\\Fallout4\\%s", sysPath, name);
        CoTaskMemFree(sysPath);
        return len;
    }

    return 0;
}

static void LogConfig ()
{
    struct ConfigLogger : config::Enumerator {
        char buffer[0x100];

        const char* CombinePath (const char* const path[], size_t count)
        {
            buffer[0] = 0;
            for (size_t i = 0; i < count; ++i) {
                strcat_s(buffer, path[i]);
                strcat_s(buffer, ":");
            }
            return buffer;
        }

        void OnBool (const char* const path[], size_t count, bool value) override
        {
            LOG("Config(%s %s)", CombinePath(path, count), value ? "true" : "false");
        }
        void OnString (const char* const path[], size_t count, const char str[]) override
        {
            LOG("Config(%s `%s')", CombinePath(path, count), str);
        }
    };

    ConfigLogger e;
    config::Enumerate(e);
}

static void InitConfig ()
{
    config::Set({"XInput", "Path"}, "%WINDIR%\\system32\\XInput1_3.dll");
    config::Set({"Features", "BackdropFix"}, true);
    config::Set({"Features", "UiScale"}, true);

    wchar_t path[MAX_PATH];
    auto len = BuildPath(L"Wrench.toml", path);
    if (len && len < ArraySize(path)) {
        config::Load(path);
    }

    LogConfig();
}

static void InitLog ()
{
    wchar_t logPath[MAX_PATH];
    auto len = BuildPath(L"Wrench.log", logPath);
    if (len && len < ArraySize(logPath)) {
        logging::Open(logPath);
    }
}

BOOL APIENTRY DllMain (HMODULE hModule,
                       DWORD   ul_reason_for_call,
                       LPVOID  lpReserved)
{
    REF(hModule, lpReserved);

    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: {
            ScopedTimer timer("Started in %u.%u ms");
            InitLog();
            InitConfig();

            uiscale::Init();
            backdrop::Init();

            // Modules must be initialized before DX, as the DX initialization requires us to have
            // already registered for callbacks.
            dx::Init();
            break;
        }

        case DLL_PROCESS_DETACH:
            logging::Close();
            break;

        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }

    return TRUE;
}
