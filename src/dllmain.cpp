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
        return ((size_t)mode < ArraySize(s_names))
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
        auto newModeStr = config::Get({"Movies", filename, "ScaleMode"});

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

    static void OnDeviceCreate (ID3D11DeviceContext* context, ID3D11Device* device, IDXGISwapChain* swapChain)
    {
        const auto imageBase = (uintptr_t)GetModuleHandleA(nullptr);
        const auto text = hooks::FindSection(".text", 5);

        if (text) {
            const auto textStart = imageBase + text->VirtualAddress;
            const auto textEnd = textStart + text->SizeOfRawData;

            // Find Scaleform's Movie constructor, and inject our own functions into its vftable.
            const auto mask = "xxxxxxxxx????xxx????xxxxx?????xxxxxxx";
            const auto pattern = "\x48\x83\xEC\x20"             // sub   rsp, 20h
                                 "\x33\xED"    // xor   ebp, ebp
                                 "\x48\x8D\x05\x00\x00\x00\x00" // lea   rax, [rip+????]
                                 "\x4C\x8D\x35\x00\x00\x00\x00" // lea   r14, [rip+????]
                                 "\x4C\x89\x31" // mov   [rcx], r14
                                 "\xC7\x41\x00\x00\x00\x00\x00" // mov   dword ptr [rcx+8], 1
                                 "\x48\x89\x69\x18" // mov   [rcx+18h], rbp
                                 "\x48\x89\x01"; // mov   [rcx], rax
            const auto ctor = hooks::FindPattern(textStart, textEnd, pattern, mask);

            if (ctor) {
                auto offset = *(const int32_t*)(ctor + 9);
                auto rip = ctor + 13;
                auto vtable = (void**)(rip + offset);

                hooks::VfTable vftable(vtable);
                vftable.Hook<Movie::SetViewScaleMode>(MovieSetViewScaleMode, &s_movieSetViewScaleMode);
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

    struct MappedBuffers {
        ID3D11Buffer* buffer;
        D3D11_MAPPED_SUBRESOURCE data;
    };


    ///
    // Data
    ///

    static MappedBuffers s_mappedData[8];
    static float s_scale = 1.0f;


    ///
    // Hooks
    ///

    static void OnResourceMap (ID3D11DeviceContext* context, ID3D11Resource* resource, D3D11_MAPPED_SUBRESOURCE* mappedResource)
    {
        // TODO: Figure out a better way to identify the backdrop buffer
        ID3D11Buffer* buffer = nullptr;
        resource->QueryInterface(&buffer);

        if (buffer) {
            D3D11_BUFFER_DESC desc;
            buffer->GetDesc(&desc);

            const auto isBackdropBuffer = desc.ByteWidth == 0x230
                                          && desc.Usage == D3D11_USAGE_DYNAMIC
                                          && desc.BindFlags == D3D11_BIND_CONSTANT_BUFFER
                                          && desc.CPUAccessFlags == D3D11_CPU_ACCESS_WRITE
                                          && desc.MiscFlags == 0
                                          && desc.StructureByteStride == 0;

            const auto hasData = mappedResource
                                 && mappedResource->pData
                                 && mappedResource->RowPitch == 0x230
                                 && mappedResource->DepthPitch == 0x230;

            if (isBackdropBuffer && hasData) {
                for (auto& mapped : s_mappedData) {
                    if (!mapped.buffer) {
                        buffer->AddRef();
                        mapped.buffer = buffer;
                        mapped.data = *mappedResource;
                        break;
                    }
                }
            }

            buffer->Release();
        }
    }

    static void OnResourceUnmap (ID3D11DeviceContext* context, ID3D11Resource* resource)
    {
        ID3D11Buffer* buffer = nullptr;
        resource->QueryInterface(&buffer);

        if (buffer) {
            for (auto& mapped : s_mappedData) {
                if (mapped.buffer == buffer) {
                    const auto floats = (float*)mapped.data.pData;
                    const auto count = int(floats[8] + 0.5f);

                    for (auto i = 0; i < count; ++i) {
                        auto v = floats + 12 + i * 4;
                        v[0] = (v[0] - 0.5f) * s_scale + 0.5f;
                        v[2] = (v[2] - 0.5f) * s_scale + 0.5f;
                    }

                    mapped.buffer->Release();
                    mapped.buffer = nullptr;
                    break;
                }
            }
        }
    }

    static void OnViewportResize (uint32_t width, uint32_t height)
    {
        s_scale = ((float)width / (float)height) / (16.f / 9.f);
    }


    ///
    // Init
    ///

    static void Init ()
    {
        if (!config::GetBool({"Features", "BackdropFix"})) {
            return;
        }

        dx::Callbacks callbacks;
        callbacks.afterResourceMap = OnResourceMap;
        callbacks.beforeResourceUnmap = OnResourceUnmap;
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
    config::Set({"Features", "BackdropFix"}, true);
    config::Set({"Features", "UiScale"}, true);
    config::Set({"Movies", "Interface/HUDMenu.swf", "ScaleMode"}, "ShowAll");
    config::Set({"Movies", "Interface/FaderMenu.swf", "ScaleMode"}, "ShowAll");
    config::Set({"Movies", "Interface/ButtonBarMenu.swf", "ScaleMode"}, "ShowAll");

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
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            InitLog();
            InitConfig();

            uiscale::Init();
            backdrop::Init();

            // Modules must be initialized before DX, as the DX initialization requires us to have
            // already registered for callbacks.
            dx::Init();
            break;

        case DLL_PROCESS_DETACH:
            logging::Close();
            break;

        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }

    return TRUE;
}
