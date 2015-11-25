// Copyright (c) 2015, Johan Sk�ld
// License: https://opensource.org/licenses/ISC

#include "stdafx.h"

#include "config.h"
#include "detours.h"
#include "util.h"

///
// Hooking
///

static bool DataCompare (const uint8_t* buffer,
                         const uint8_t* data,
                         const uint8_t* sMask)
{
    for (; *sMask; ++sMask, ++buffer, ++data) {
        if (*sMask == 'x' && *buffer != *data) {
            return false;
        }
    }
    return true;
}


static uintptr_t FindPattern (uintptr_t   address,
                              uintptr_t   term,
                              const char* data,
                              const char* sMask)
{
    auto start = (const uint8_t*)address;
    auto end = (const uint8_t*)term;
    for (auto ptr = start; ptr < end; ++ptr) {
        if (DataCompare(ptr, (const uint8_t*)data, (const uint8_t*)sMask)) {
            return (uintptr_t)ptr;
        }
    }
    return 0;
}

static IMAGE_SECTION_HEADER* FindSection (const char* name,
                                          size_t      length)
{
    auto imageBase = GetModuleHandleA(nullptr);

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
                                    (int)ArraySize(section->Name));
        if (match == CSTR_EQUAL) {
            return section;
        }
    }

    return nullptr;
}

class VfTable
{
    void** m_vftable;
    std::vector<DetourInfo> m_detours;

    public:
        VfTable ();
        VfTable (void** vftable);
        bool IsValid () const;
        VfTable& operator= (void** vftable);

        template <class F>
        void Detour (typename F::Fn* replacement, typename F::Fn** prev);

        template <class F>
        void Hook (typename F::Fn* replacement, typename F::Fn** prev);

        template <class F, class... Args>
        typename F::Ret Invoke (Args&& ... args);
};

VfTable::VfTable ()
    : m_vftable(nullptr) { }

VfTable::VfTable (void** vftable)
    : m_vftable(vftable) { }

bool VfTable::IsValid () const
{
    return m_vftable != nullptr;
}

VfTable& VfTable::operator= (void** vftable)
{
    m_vftable = vftable;
    return *this;
}

template <class F>
void VfTable::Detour (typename F::Fn* replacement, typename F::Fn** prev)
{
    auto src = (typename F::Fn*)m_vftable[F::INDEX];
    auto info = ::Detour(src, replacement);
    _InterlockedExchange64((LONG64*)prev, (LONG64)info.trampoline);
    m_detours.emplace_back(std::move(info));
}

template <class F>
void VfTable::Hook (typename F::Fn* replacement, typename F::Fn** prev)
{
    const auto addr = m_vftable + F::INDEX;
    DWORD prevProtection;

    if (!VirtualProtect(addr, sizeof(*addr), PAGE_READWRITE, &prevProtection)) {
        LOG("Failed to make %p read/writable (%lu)", addr, GetLastError());
        return;
    }

    _InterlockedExchange64((LONG64*)prev, (LONG64)*addr);
    _InterlockedExchange64((LONG64*)addr, (LONG64)replacement);

    if (!VirtualProtect(addr, sizeof(addr), prevProtection, &prevProtection)) {
        LOG("Failed to restore protection flags for %p (%lu)", addr, GetLastError());
    }
}

template <class F, class... Args>
typename F::Ret VfTable::Invoke (Args&& ... args)
{
    auto fn = (typename F::Fn*)m_vftable[F::INDEX];
    return fn(std::forward<Args>(args) ...);
}

template <size_t I, class F>
struct Function;

template <size_t I, class R, class... A>
struct Function<I, R(A...)> {
    static const size_t INDEX = I;
    using Ret = R;
    using Fn = R(A...);
};


///
// Scaleform hooks
///

namespace scaleform {

    enum class ViewScaleMode : uint32_t {
        NoScale = 0,
        ShowAll = 1,
        ExactFit = 2,
        NoBorder = 3,
        Count = 4
    };

    struct MovieDef {
        using GetFilename = Function<12, const char* (MovieDef&)>;

        void** vftable;
    };

    struct Movie {
        using SetViewScaleMode = Function<14, void (Movie&, ViewScaleMode)>;

        void** vftable;
        uint8_t padding1[0x40];
        MovieDef* movieDef;
        uint8_t padding2[0xC0];
        float ViewportMatrix[8];
    };

    static Movie::SetViewScaleMode::Fn* s_origSetViewScaleMode;

    static const char* GetMovieFilename (const Movie& movie)
    {
        VfTable table(movie.movieDef->vftable);
        return table.Invoke<MovieDef::GetFilename>(*movie.movieDef);
    }

    static void Movie_SetViewScaleMode_Hook (Movie&        movie,
                                             ViewScaleMode mode)
    {
        static const char* s_names[(size_t)ViewScaleMode::Count] = {
            "NoScale",
            "ShowAll",
            "ExactFit",
            "NoBorder",
        };

        auto oldModeStr = (size_t)mode < ArraySize(s_names) ? s_names[(size_t)mode] : "Invalid";
        auto filename = GetMovieFilename(movie);
        auto modeStr = config::Get({"Movies", filename, "ScaleMode"});
        auto newMode = mode;

        if (modeStr) {
            for (auto i = 0; i < ArraySize(s_names); ++i) {
                if (strcmp(modeStr, s_names[i]) == 0) {
                    newMode = (ViewScaleMode)i;
                    break;
                }
            }
        }

        if (mode != newMode) {
            auto newModeStr = s_names[(size_t)newMode];
            LOG("Overriding scale mode: old=%s, new=%s, filename=%s", oldModeStr, newModeStr, filename);
        } else {
            LOG("Using default scale mode: mode=%s, filename=%s", oldModeStr, filename);
        }

        s_origSetViewScaleMode(movie, newMode);
    }

    // New thread
    static void SetupHooks ()
    {
        const auto imageBase = (uintptr_t)GetModuleHandleA(nullptr);
        const auto text = FindSection(".text", 5);

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
            const auto ctor = FindPattern(textStart, textEnd, pattern, mask);

            if (ctor) {
                auto offset = *(const int32_t*)(ctor + 9);
                auto rip = ctor + 13;
                auto vtable = (void**)(rip + offset);

                VfTable vftable(vtable);
                vftable.Hook<Movie::SetViewScaleMode>(Movie_SetViewScaleMode_Hook, &s_origSetViewScaleMode);
            } else {
                ERR("Could not locate the Movie constructor");
            }
        } else {
            ERR("No .text segment?");
        }
    }

} // namespace scaleform


///
// DX Hooks
///

namespace dx {

    struct MappedBuffers {
        ID3D11Buffer* buffer;
        D3D11_MAPPED_SUBRESOURCE data;
    };

    using Map_t = Function<14, HRESULT(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*)>;
    using Unmap_t = Function<15, void (ID3D11DeviceContext*, ID3D11Resource*, UINT)>;
    using ResizeBuffers_t = Function<13, HRESULT(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT)>;
    using D3D11CreateDeviceAndSwapChain_t = decltype(D3D11CreateDeviceAndSwapChain);

    static D3D11CreateDeviceAndSwapChain_t* s_createDevice;
    static Map_t::Fn* s_map;
    static Unmap_t::Fn* s_unmap;
    static ResizeBuffers_t::Fn* s_resizeBuffers;

    static uint32_t s_width;
    static uint32_t s_height;
    static float s_scale;
    static MappedBuffers s_mappedData[8];

    // WARNING: This pointer must NOT be dereferenced, as we did not take a reference to it. It's
    // *only* used for testing whether other swap chains are referring to the global one.
    static IDXGISwapChain* s_swapChain;

    static HRESULT DeviceContext_Map_Hook (ID3D11DeviceContext*      context,
                                           ID3D11Resource*           resource,
                                           UINT                      subResource,
                                           D3D11_MAP                 mapType,
                                           UINT                      mapFlags,
                                           D3D11_MAPPED_SUBRESOURCE* mappedResource)
    {
        auto result = s_map(context, resource, subResource, mapType, mapFlags, mappedResource);

        if (SUCCEEDED(result)) {
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

        return result;
    }

    static void DeviceContext_Unmap_Hook (ID3D11DeviceContext* context,
                                          ID3D11Resource*      resource,
                                          UINT                 subResource)
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

        s_unmap(context, resource, subResource);
    }

    static HRESULT SwapChain_ResizeBuffers_Hook (IDXGISwapChain* swapChain,
                                                 UINT            BufferCount,
                                                 UINT            Width,
                                                 UINT            Height,
                                                 DXGI_FORMAT     NewFormat,
                                                 UINT            SwapChainFlags)
    {
        auto result = s_resizeBuffers(swapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

        if (swapChain == s_swapChain && SUCCEEDED(result)) {
            DXGI_SWAP_CHAIN_DESC desc;
            RECT rect;

            if (SUCCEEDED(swapChain->GetDesc(&desc)) && GetClientRect(desc.OutputWindow, &rect)) {
                s_width = Width ? Width : rect.right;
                s_height = Height ? Height : rect.bottom;
                s_scale = ((float)s_width / (float)s_height) / (16.f / 9.f);
            } else {
                ERR("Failed to update aspect ratio");
            }
        }

        return result;
    }

    static HRESULT CreateDeviceAndSwapChain_Hook (IDXGIAdapter*               pAdapter,
                                                  D3D_DRIVER_TYPE             DriverType,
                                                  HMODULE                     Software,
                                                  UINT                        Flags,
                                                  const D3D_FEATURE_LEVEL*    pFeatureLevels,
                                                  UINT                        FeatureLevels,
                                                  UINT                        SDKVersion,
                                                  const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
                                                  IDXGISwapChain**            ppSwapChain,
                                                  ID3D11Device**              ppDevice,
                                                  D3D_FEATURE_LEVEL*          pFeatureLevel,
                                                  ID3D11DeviceContext**       ppImmediateContext)
    {
        auto result = s_createDevice(pAdapter,
                                     DriverType,
                                     Software,
                                     Flags,
                                     pFeatureLevels,
                                     FeatureLevels,
                                     SDKVersion,
                                     pSwapChainDesc,
                                     ppSwapChain,
                                     ppDevice,
                                     pFeatureLevel,
                                     ppImmediateContext);

        if (FAILED(result)) {
            ERR("Failed to create D3D device: %#lx", result);
            return result;
        }

        // These VfTables needs to be static so we keep the trampoline memory valid even after
        // leaving this function. It is *not* safe to use after leaving this function however
        // (the pointed to virtual table has been deallocated), and as such should not be
        // globally accessible.
        static VfTable s_dcVftable;
        static VfTable s_scVftable;

        s_swapChain = ppSwapChain ? *ppSwapChain : nullptr;

        // The `IsValid()` calls below are really lies. The vftables may be freed already. In these
        // cases, we really only using them to determine whether we've already detoured the
        // functions or not. We still should not make any calls using the tables!
        if (ppImmediateContext && *ppImmediateContext) {
            if (!s_dcVftable.IsValid()) {
                s_dcVftable = *(void***)*ppImmediateContext;
                s_dcVftable.Detour<Map_t>(DeviceContext_Map_Hook, &s_map);
                s_dcVftable.Detour<Unmap_t>(DeviceContext_Unmap_Hook, &s_unmap);
            }
        } else if (!s_dcVftable.IsValid()) {
            ERR("No device context");
        }

        if (pSwapChainDesc) {
            s_width = pSwapChainDesc->BufferDesc.Width;
            s_height = pSwapChainDesc->BufferDesc.Height;
            s_scale = ((float)s_width / (float)s_height) / (16.f / 9.f);
        } else {
            ERR("Unable to determine aspect ratio");
        }

        if (ppSwapChain && *ppSwapChain) {
            if (!s_scVftable.IsValid()) {
                s_scVftable = *(void***)*ppSwapChain;
                s_scVftable.Detour<ResizeBuffers_t>(SwapChain_ResizeBuffers_Hook, &s_resizeBuffers);
            }
        } else if (!s_scVftable.IsValid()) {
            ERR("No swap chain");
        }

        // Normally we'd set up scaleform hooks from DllMain but at that point the game code is
        // still encrypted by the steam DRM, so any attempt at finding patterns will fail.
        scaleform::SetupHooks();

        return result;
    }

    static void SetupHooks ()
    {
        auto d3d = GetModuleHandleA("d3d11");
        if (!d3d) {
            ERR("No d3d available.");
            return;
        }

        auto proc = (D3D11CreateDeviceAndSwapChain_t*)GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain");
        if (!proc) {
            ERR("Could not find `D3D11CreateDeviceAndSwapChain'");
            return;
        }

        static auto s_detour = Detour(proc, CreateDeviceAndSwapChain_Hook);
        s_createDevice = (D3D11CreateDeviceAndSwapChain_t*)s_detour.trampoline;
    }

} // namespace dx


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

static void InitConfig ()
{
    config::Set({"Movies", "Interface/HUDMenu.swf", "ScaleMode"}, "ShowAll");
    config::Set({"Movies", "Interface/FaderMenu.swf", "ScaleMode"}, "ShowAll");
    config::Set({"Movies", "Interface/ButtonBarMenu.swf", "ScaleMode"}, "ShowAll");

    wchar_t path[MAX_PATH];
    auto len = BuildPath(L"Wrench.toml", path);
    if (len && len < ArraySize(path)) {
        config::Load(path);
    }

    config::Enumerate([] (auto path, auto count, auto value) {
        char buffer[0x100] = {0};

        for (auto i = 0; i < count; ++i) {
            strcat_s(buffer, path[i]);
            strcat_s(buffer, ":");
        }

        LOG("Config(%s `%s')", buffer, value);
    });
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
            dx::SetupHooks();
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
