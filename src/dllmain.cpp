// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

///
// Util
///

template <class T, size_t N>
constexpr size_t ArraySize (const T(&)[N]) {
    return N;
}


///
// Logging
///

#define LOG(fmt, ...)   Log::Write(__FUNCTION__, fmt, ##__VA_ARGS__)
#define ERR(fmt, ...)   Log::Write(__FUNCTION__, "ERR: " fmt, ##__VA_ARGS__)

namespace Log {

    static HANDLE s_handle;

    // Main thread
    static void Open(const char filename[]) {
        s_handle = CreateFileA(filename,
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    }

    // Main thread
    static void Close() {
        if (s_handle)
            CloseHandle(s_handle);
    }

    // Random threads
    void Write(const char func[], const char str[], ...) {
        if (s_handle) {
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

} // namespace Log


///
// Detour
///

struct DetourInfo {
    union {
        uint8_t* buffer;
        void*    trampoline;
    };

    DetourInfo ();
    DetourInfo (const DetourInfo&) = delete;
    DetourInfo (DetourInfo&& source);
    ~DetourInfo ();

    DetourInfo& operator= (const DetourInfo&) = delete;
    DetourInfo& operator= (DetourInfo&& source);
    void Reset ();
};

DetourInfo::DetourInfo()
    : buffer(nullptr) {
}

DetourInfo::DetourInfo (DetourInfo&& source)
    : buffer(source.buffer) {
    source.buffer = nullptr;
}

DetourInfo::~DetourInfo () {
    Reset();
}

DetourInfo& DetourInfo::operator= (DetourInfo&& source) {
    buffer = source.buffer;
    source.buffer = nullptr;
    return *this;
}

void DetourInfo::Reset () {
    if (buffer) {
        VirtualFree(buffer, 0, MEM_RELEASE);
        buffer = nullptr;
    }
}

static void LogAsm (void* addr, size_t size) {
    ud_t ud;
    ud_init(&ud);
    ud_set_input_buffer(&ud, (const uint8_t*)addr, size);
    ud_set_mode(&ud, 64);
    ud_set_syntax(&ud, UD_SYN_INTEL);

    LOG("Assembly at %p (%zu)", addr, size);

    uint32_t count = 0;
    while (ud_insn_off(&ud) < size) {
        if (!ud_disassemble(&ud))
            break;
        LOG("  %s", ud_insn_asm(&ud));
        ++count;
    }

    LOG("%u instructions, %llu bytes", count, ud_insn_off(&ud));
}

static size_t AsmLength (void* addr, size_t minSize) {
    // TODO: I should probably set this up in a way so that it doesn't potentially dig into
    // unassigned memory. Specifically, `ud_set_input_buffer` should get a more reasonable
    // second parameter...
    //
    // Also, this function has the problem where a function can actually be too small, and
    // it'll just happily keep reading whatever's after it, unless it's an invalid opcode.

    ud_t ud;
    ud_init(&ud);
    ud_set_input_buffer(&ud, (const uint8_t*)addr, minSize + 16);
    ud_set_mode(&ud, 64);
    ud_set_syntax(&ud, nullptr);

    while (ud_insn_off(&ud) < minSize) {
        if (!ud_disassemble(&ud))
            break;
    }

    return ud_insn_off(&ud);
}

static void* FollowJumps (void* addr) {
    ud_t ud;
    ud_init(&ud);
    ud_set_mode(&ud, 64);
    ud_set_syntax(&ud, nullptr);

    while (true) {
        ud_set_input_buffer(&ud, (const uint8_t*)addr, 16);
        if (!ud_disassemble(&ud)) {
            ERR("Could not disassemble instruction at %p", addr);
            return addr;
        }

        const auto instruction = ud_insn_mnemonic(&ud);
        if (instruction != UD_Ijmp)
            return addr;

        const auto param = ud_insn_opr(&ud, 0);
        if (param->type != UD_OP_JIMM) {
            ERR("Invalid operand for jump: %u", param->type);
            return addr;
        }

        const auto rip = (intptr_t)addr + (intptr_t)ud_insn_len(&ud);
        addr = (void*)(rip + param->lval.sdword);
    }
}

template <class T>
static T RelativePtr (intptr_t src, intptr_t dst, size_t extra) {
    if (dst < src)
        return (T)(0 - (src - dst) - extra);
    return (T)(dst - (src + extra));
}

template <class T>
static DetourInfo Detour (T* src, T* dst) {
    // Based on http://www.unknowncheats.me/forum/c-and-c/134871-64-bit-detour-function.html

    static_assert(sizeof(void*) == 8, "x64 only");
    DetourInfo info;

    // MSVC really enjoys creating functions that contain nothing but a jump. That jump alone is
    // not big enough that we can detour the function. For functions where the very first
    // instruction is a jump, we therefore follow them and detour the final function.
    src = (T*)FollowJumps(src);

    // Allocate trampoline memory
    const size_t allocSize = 0x1000;

    MEMORY_BASIC_INFORMATION mbi;
    for (auto addr = (uintptr_t)src; addr > (uintptr_t)src - 0x80000000; addr = (uintptr_t)mbi.BaseAddress - 1) {
        if (!VirtualQuery((void*)addr, &mbi, sizeof(mbi))) {
            break;
        }

        if (mbi.State != MEM_FREE)
            continue;

        // TODO: Fix bug where this will fail if the current block is too big
        info.buffer = (uint8_t*)VirtualAlloc(mbi.BaseAddress, allocSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (info.buffer)
            break;
    }

    if (!info.buffer)
        return std::move(info);

    // Save the original code, and apply the detour:
    //   push   rax
    //   movabs rax, 0xcccccccccccccccc
    //   xchg   rax, [rsp]
    //   ret
    uint8_t detour[] = { 0x50, 0x48, 0xb8, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x48, 0x87, 0x04, 0x24, 0xC3 };
    const auto length = AsmLength(src, 16);
    if (length < 16) {
        info.Reset();
        return std::move(info);
    }

    memcpy_s(info.buffer, allocSize, src, length);
    memcpy_s(info.buffer + length, allocSize - length, detour, ArraySize(detour));
    *(uintptr_t*)(info.buffer + length + 3) = (uintptr_t)src + length;

    // Build a far jump to the destination
    uint8_t farJump[6];
    farJump[0] = 0xff;
    farJump[1] = 0x25;
    *(int32_t*)(farJump + 2) = RelativePtr<int32_t>((intptr_t)src, (intptr_t)(info.buffer + length + 16), 6);
    *(uintptr_t*)(info.buffer + length + 16) = (uintptr_t)dst;

    // Patch the source
    DWORD protection = 0;
    VirtualProtect(src, 6, PAGE_EXECUTE_READWRITE, &protection);
    memcpy(src, farJump, ArraySize(farJump));
    memset((uint8_t*)src + 6, 0x90, length - 6);
    VirtualProtect(src, 6, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), src, length);

    return std::move(info);
}


///
// Hooking
///

static bool DataCompare (const uint8_t* buffer,
                         const char*    data,
                         const char*    sMask) {
    for (; *sMask; ++sMask, ++buffer, ++data)
        if (*sMask == 'x' && *buffer != *data)
            return false;
    return *sMask != 0;
}


static uintptr_t FindPattern (uintptr_t   address,
                              uintptr_t   term,
                              const char* data,
                              const char* sMask) {
    auto length = term - address;
    for (size_t i = 0; i < length; ++i)
        if (DataCompare((const uint8_t*)(address + i), data, sMask))
            return address + i;
    return 0;
}

static IMAGE_SECTION_HEADER* FindSection (const char* name,
                                          size_t      length) {
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
        if (match == CSTR_EQUAL)
            return section;
    }

    return nullptr;
}

class VfTable {
    void ** m_vftable;
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
    typename F::Ret Invoke (Args&&... args);
};

VfTable::VfTable () 
    : m_vftable(nullptr)
{ }

VfTable::VfTable (void** vftable)
    : m_vftable(vftable)
{ }

bool VfTable::IsValid () const {
    return m_vftable != nullptr;
}

VfTable& VfTable::operator= (void** vftable) {
    m_vftable = vftable;
    return *this;
}

template <class F>
void VfTable::Detour (typename F::Fn* replacement, typename F::Fn** prev) {
    auto src = (typename F::Fn*)m_vftable[F::INDEX];
    auto info = ::Detour(src, replacement);
    _InterlockedExchange64((LONG64*)prev, (LONG64)info.trampoline);
    m_detours.emplace_back(std::move(info));
}

template <class F>
void VfTable::Hook (typename F::Fn* replacement, typename F::Fn** prev) {
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
typename F::Ret VfTable::Invoke (Args&&... args) {
    auto fn = (typename F::Fn*)m_vftable[F::INDEX];
    return fn(std::forward<Args>(args)...);
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

namespace Scaleform {

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
        using GetFilename = Function<12, const char*(MovieDef&)>;

        void** vftable;
    };

    struct Movie {
        using SetViewport = Function<12, void(Movie&, Viewport&)>;
        using SetViewScaleMode = Function<14, void(Movie&, ViewScaleMode)>;

        void** vftable;
        uint8_t padding1[0x40];
        MovieDef* movieDef;
        uint8_t padding2[0xC0];
        float ViewportMatrix[8];
    };

    static Movie::SetViewport::Fn*      s_origSetViewport;
    static Movie::SetViewScaleMode::Fn* s_origSetViewScaleMode;

    static const char* GetMovieFilename(const Movie& movie) {
        VfTable table(movie.movieDef->vftable);
        return table.Invoke<MovieDef::GetFilename>(*movie.movieDef);
    }

    static void Movie_SetViewport_Hook (Movie&    movie,
                                        Viewport& viewport) {
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

    // New thread
    static void SetupHooks () {
        const auto imageBase = (uintptr_t)GetModuleHandleA(nullptr);
        const auto rdata = FindSection(".rdata", 6);
        const auto text = FindSection(".text", 5);

        if (text) {
            auto textStart = imageBase + text->VirtualAddress;
            auto textTerm = textStart + text->SizeOfRawData;

            // MovieDef vtable
            auto addr = FindPattern(textStart,
                                    textTerm,
                                    "\x40\x53\x48\x83\xEC\x20\x48\x8D\x05\x00\x00\x00\x00\x48\x8B\xD9\x48\x89\x01",
                                    "xxxxxxxxx????xxxxxx");
            if (!addr) {
                LOG("Failed to find MovieDef vtable");
            } else {
                auto offset = *(int32_t*)(addr + 9);
                auto vtable = (void**)(addr + 13 + offset);
                LOG("MovieDef vtable at %p", vtable);
                REF(vtable);
            }
        }

        if (rdata) {
            // Movie vtable
            const auto vtable = (void**)(imageBase + rdata->VirtualAddress + 0x2DDBB0);
            VfTable vftable(vtable);
            vftable.Hook<Movie::SetViewScaleMode>(Movie_SetViewScaleMode_Hook, &s_origSetViewScaleMode);
        }
    }
    
} // namespace Scaleform


///
// DX Hooks
///

namespace Dx {

    struct MappedBuffers {
        ID3D11Buffer* buffer;
        D3D11_MAPPED_SUBRESOURCE data;
    };

    using Map_t = Function<14, HRESULT(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*)>;
    using Unmap_t = Function<15, void(ID3D11DeviceContext*, ID3D11Resource*, UINT)>;
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
                                           D3D11_MAPPED_SUBRESOURCE* mappedResource) {
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
                                          UINT                 subResource) {
        ID3D11Buffer* buffer = nullptr;
        resource->QueryInterface(&buffer);

        if (buffer) {
            for (auto& mapped : s_mappedData) {
                if (mapped.buffer == buffer) {
                    const auto floats = (float*)mapped.data.pData;
                    //LOG("%p, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f",
                    //    buffer,
                    //    floats[8],
                    //    floats[9],
                    //    floats[10],
                    //    floats[11],
                    //    floats[12],
                    //    floats[13],
                    //    floats[14],
                    //    floats[15],
                    //    floats[16],
                    //    floats[17],
                    //    floats[18],
                    //    floats[19]);

                    auto count = int(floats[8] + 0.5f);
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
                                                 UINT            SwapChainFlags) {
        auto result = s_resizeBuffers(swapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

        if (swapChain == s_swapChain && SUCCEEDED(result)) {
            DXGI_SWAP_CHAIN_DESC desc;
            RECT rect;

            if (SUCCEEDED(swapChain->GetDesc(&desc)) && GetClientRect(desc.OutputWindow, &rect)) {
                s_width = Width ? Width : rect.right;
                s_height = Height ? Height : rect.bottom;
                s_scale = ((float)s_width / (float)s_height) / (16.f / 9.f);
                LOG("Width=%u, Height=%u, Scale=%f", s_width, s_height, s_scale);
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
                                                  ID3D11DeviceContext**       ppImmediateContext) {
        //Init::Wait();
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

        return result;
    }

    // Init thread
    static void SetupHooks () {
        static DetourInfo s_detour;

        // We probably maybe shouldn't do this in DllMain, but whatevs... Works on my machine! (?°?°.)?

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

        s_detour = Detour(proc, CreateDeviceAndSwapChain_Hook);
        s_createDevice = (D3D11CreateDeviceAndSwapChain_t*)s_detour.trampoline;
    }

} // namespace Dx


///
// Test
///

namespace Test {

    static DetourInfo s_testDetour;

    static int Test (int a, int b) {
        auto prev = (decltype(Test)*)s_testDetour.trampoline;
        return prev(a, b);
    }
    
    static void SetupHooks () {
        auto module = GetModuleHandleA(nullptr);
        auto proc   = (decltype(Test)*)GetProcAddress(module, "Test");

        if (proc) {
            s_testDetour = Detour(proc, Test);
        }
    }

} // namespace Test


///
// Main
///

BOOL APIENTRY DllMain (HMODULE hModule,
                       DWORD   ul_reason_for_call,
                       LPVOID  lpReserved) {
    REF(hModule, lpReserved);

    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            Log::Open("hook.log");
            Scaleform::SetupHooks();
            Dx::SetupHooks();
            break;

        case DLL_PROCESS_DETACH:
            Log::Close();
            break;

        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }

    return TRUE;
}

