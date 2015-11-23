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
// Detour
///

struct DetourInfo {
    struct Data {
        uint8_t trampoline[0x40];
    };

    union {
        uint8_t* buffer;
        Data*    data;
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

    // Allocate trampoline memory
    const size_t allocSize = 0x1000;
    static_assert(allocSize >= sizeof(DetourInfo::Data), "buffer too small");

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

    memcpy_s(info.data->trampoline, allocSize, src, length);
    memcpy_s(info.data->trampoline + length, allocSize - length, detour, ArraySize(detour));
    *(uintptr_t*)(info.data->trampoline + length + 3) = (uintptr_t)src + length;

    // Build a far jump to the destination
    uint8_t farJump[6];
    farJump[0] = 0xff;
    farJump[1] = 0x25;
    *(int32_t*)(farJump + 2) = RelativePtr<int32_t>((intptr_t)src, (intptr_t)(info.data->trampoline + length + 16), 6);
    *(uintptr_t*)(info.data->trampoline + length + 16) = (uintptr_t)dst;

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
// Logging
///

#define LOG(fmt, ...)   Log::Write(__FUNCTION__, fmt, ##__VA_ARGS__)

namespace Log {
    
    static HANDLE s_handle;

    // Main thread
    static void Open (const char filename[]) {
        s_handle = CreateFileA(filename,
                               GENERIC_WRITE,
                               0,
                               nullptr,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    }

    // Main thread
    static void Close () {
        if (s_handle)
            CloseHandle(s_handle);
    }

    // Random threads
    void Write (const char func[], const char str[], ...) {
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

}


///
// Init
///

namespace Init {
    
    static HANDLE s_event;
    static long   s_done;

    static void Wait () {
        if (!s_done) {
            WaitForSingleObject(s_event, INFINITE);
            _InterlockedExchange(&s_done, 1);
        }
    }

    static void Init () {
        s_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    }

    static void Flag () {
        SetEvent(s_event);
    }

    static void Destroy () {
        if (s_event) {
            CloseHandle(s_event);
            s_event = nullptr;
        }
    }

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
                                    (int)sizeof(section->Name));
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
    _InterlockedExchange64((LONG64*)prev, (LONG64)info.data->trampoline);
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

    static Movie::SetViewport::Fn*      s_origSetViewport;
    static Movie::SetViewScaleMode::Fn* s_origSetViewScaleMode;

    static const char* GetMovieFilename(const Movie& movie) {
        VfTable table(movie.movieDef->vftable);
        return table.Invoke<MovieDef::GetFilename>(*movie.movieDef);
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

        Init::Wait();
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
            vftable.Hook<Movie::SetViewport>(Movie_SetViewport_Hook, &s_origSetViewport);
            vftable.Hook<Movie::SetViewScaleMode>(Movie_SetViewScaleMode_Hook, &s_origSetViewScaleMode);
        }
    }
    
}


///
// DX Hooks
///

namespace Dx {

    struct DxData {
        ID3D11DeviceContext* context;
        ID3D11Device* device;
        HWND window;
        const char* className;
        HMODULE module;
    };

    struct MappedBuffers {
        ID3D11Buffer* buffer;
        D3D11_MAPPED_SUBRESOURCE* data;
    };

    using PSSetConstantBuffers = Function<16, void(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*)>;
    using Map = Function<14, HRESULT(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*)>;
    using Unmap = Function<15, void(ID3D11DeviceContext*, ID3D11Resource*, UINT)>;
    using CreateBuffer = Function<3, HRESULT(ID3D11Device*, D3D11_BUFFER_DESC*, D3D11_SUBRESOURCE_DATA*, ID3D11Buffer**)>;

    static PSSetConstantBuffers::Fn* s_psSetConstantBuffers;
    static Map::Fn* s_map;
    static Unmap::Fn* s_unmap;
    static CreateBuffer::Fn* s_createBuffer;

    static MappedBuffers s_mappedData[8];

    static DxData& Destroy (DxData& data) {
        if (data.device) {
            data.device->Release();
            data.device = nullptr;
        }
        if (data.context) {
            data.context->Release();
            data.context = nullptr;
        }
        if (data.window) {
            DestroyWindow(data.window);
            data.window = nullptr;
        }
        if (data.className) {
            UnregisterClassA(data.className, data.module);
            data.className = nullptr;
        }
        data.module = nullptr;
        return data;
    }

    static DxData Init () {
        static const char s_className[] = "FO4_21:9_Inject";
        static const char s_windowName[] = "";

        DxData data;
        memset(&data, 0, sizeof(data));

        auto d3d11 = GetModuleHandleA("d3d11");
        if (!d3d11) {
            LOG("ERR: d3d11 not loaded");
            return Destroy(data);
        }

        using D3D11CreateDeviceAndSwapChain_t = decltype(D3D11CreateDeviceAndSwapChain);
        auto createDeviceAndSwapChain = (D3D11CreateDeviceAndSwapChain_t*)GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain");
        if (!createDeviceAndSwapChain) {
            LOG("ERR: Cannot find D3D11CreateDeviceAndSwapChain");
            return Destroy(data);
        }

        // Register window class
        data.module = GetModuleHandleA(nullptr);

        WNDCLASSEXA wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = data.module;
        wc.hCursor = LoadCursor(nullptr, nullptr);
        wc.lpszClassName = s_className;
        wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

        if (!RegisterClassExA(&wc)) {
            LOG("Failed to register window class");
            return Destroy(data);
        }

        data.className = s_className;

        // Create window
        data.window = CreateWindowExA(0,
                                      s_className,
                                      s_windowName,
                                      WS_POPUP,
                                      CW_USEDEFAULT,
                                      CW_USEDEFAULT,
                                      CW_USEDEFAULT,
                                      CW_USEDEFAULT,
                                      nullptr,
                                      nullptr,
                                      data.module,
                                      nullptr);

        if (!data.window) {
            LOG("Cannot create window");
            return Destroy(data);
        }
        
        // Init DX
        DXGI_SWAP_CHAIN_DESC scd;
        memset(&scd, 0, sizeof(scd));
        scd.BufferCount = 1;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = data.window;
        scd.SampleDesc.Count = 1;
        scd.Windowed = TRUE;
        scd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        scd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        auto featureLevel = D3D_FEATURE_LEVEL_11_0;
        auto result = createDeviceAndSwapChain(nullptr,
                                                D3D_DRIVER_TYPE_HARDWARE,
                                                nullptr,
                                                0,
                                                &featureLevel,
                                                1,
                                                D3D11_SDK_VERSION,
                                                &scd,
                                                nullptr,
                                                &data.device,
                                                nullptr,
                                                &data.context);

        if (FAILED(result)) {
            LOG("Failed to initialize d3d11: %lx", result);
            return Destroy(data);
        }

        return data;
    }

    static bool AreEqual (const D3D11_BUFFER_DESC&a, const D3D11_BUFFER_DESC& b) {
        return memcmp(&a, &b, sizeof(D3D11_BUFFER_DESC)) == 0;
    }

    static void DeviceContext_PSSetConstantBuffers_Hook (ID3D11DeviceContext* context,
                                                         UINT                 startSlot,
                                                         UINT                 numBuffers,
                                                         ID3D11Buffer* const* buffers) {
        static D3D11_BUFFER_DESC s_lastDesc = { 0, D3D11_USAGE_DEFAULT, 0, 0, 0, 0 };

        if (startSlot == 0 && numBuffers == 1) {
            D3D11_BUFFER_DESC desc;
            buffers[0]->GetDesc(&desc);

            if (!AreEqual(desc, s_lastDesc)) {
                LOG("D3D11_BUFFER_DESC(%u, %u, %u, %u, %u, %u)",
                    desc.ByteWidth,
                    desc.Usage,
                    desc.BindFlags,
                    desc.CPUAccessFlags,
                    desc.MiscFlags,
                    desc.StructureByteStride);
                s_lastDesc = desc;
            }
        }

        Init::Wait();
        s_psSetConstantBuffers(context, startSlot, numBuffers, buffers);
    }

    static HRESULT DeviceContext_Map_Hook (ID3D11DeviceContext*      context,
                                           ID3D11Resource*           resource,
                                           UINT                      subResource,
                                           D3D11_MAP                 mapType,
                                           UINT                      mapFlags,
                                           D3D11_MAPPED_SUBRESOURCE* mappedResource) {
        Init::Wait();
        LOG("%p(%p, %p, %u, %u, %u, %p)", s_map, context, resource, subResource, mapType, mapFlags, mappedResource);
        auto result = s_map(context, resource, subResource, mapType, mapFlags, mappedResource);

        //if (SUCCEEDED(result)) {
        //    // TODO: Figure out a better way to identify the backdrop buffer
        //    ID3D11Buffer* buffer = nullptr;
        //    resource->QueryInterface(&buffer);

        //    if (buffer) {
        //        D3D11_BUFFER_DESC desc;
        //        buffer->GetDesc(&desc);

        //        const auto isBackdropBuffer = desc.ByteWidth == 0x230
        //                                   && desc.Usage == D3D11_USAGE_DYNAMIC
        //                                   && desc.BindFlags == D3D11_BIND_CONSTANT_BUFFER
        //                                   && desc.CPUAccessFlags == D3D11_CPU_ACCESS_WRITE
        //                                   && desc.MiscFlags == 0
        //                                   && desc.StructureByteStride == 0;

        //        if (isBackdropBuffer) {
        //            for (auto& mapped : s_mappedData) {
        //                if (!mapped.buffer) {
        //                    buffer->AddRef();
        //                    mapped.buffer = buffer;
        //                    mapped.data = mappedResource;
        //                    break;
        //                }
        //            }
        //        }

        //        buffer->Release();
        //    }
        //}

        return result;
    }

    static void DeviceContext_Unmap_Hook (ID3D11DeviceContext* context,
                                          ID3D11Resource*      resource,
                                          UINT                 subResource) {
        //ID3D11Buffer* buffer = nullptr;
        //resource->QueryInterface(&buffer);

        //if (buffer) {
        //    for (auto& mapped : s_mappedData) {
        //        if (mapped.buffer == buffer) {
        //            const auto floats = (float*)mapped.data->pData;
        //            LOG("%f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f",
        //                floats[8],
        //                floats[9],
        //                floats[10],
        //                floats[11],
        //                floats[12],
        //                floats[13],
        //                floats[14],
        //                floats[15],
        //                floats[16],
        //                floats[17],
        //                floats[18],
        //                floats[19]);
        //            mapped.buffer->Release();
        //            mapped.buffer = nullptr;
        //            mapped.data = nullptr;
        //            break;
        //        }
        //    }
        //}

        Init::Wait();
        s_unmap(context, resource, subResource);
    }

    static HRESULT Device_CreateBuffer_Hook (ID3D11Device*           device,
                                             D3D11_BUFFER_DESC*      desc,
                                             D3D11_SUBRESOURCE_DATA* data,
                                             ID3D11Buffer**          buffer) {
        //const auto isBackdropBuffer = desc->ByteWidth == 0x230
        //                           && desc->Usage == D3D11_USAGE_DYNAMIC
        //                           && desc->BindFlags == D3D11_BIND_CONSTANT_BUFFER
        //                           && desc->CPUAccessFlags == D3D11_CPU_ACCESS_WRITE
        //                           && desc->MiscFlags == 0
        //                           && desc->StructureByteStride == 0;

        //if (isBackdropBuffer) {
        //    if (!data) {
        //        LOG("No data");
        //    } else {
        //        const auto floats = (float*)data->pSysMem;
        //        LOG("%f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f",
        //            floats[8],
        //            floats[9],
        //            floats[10],
        //            floats[11],
        //            floats[12],
        //            floats[13],
        //            floats[14],
        //            floats[15],
        //            floats[16],
        //            floats[17],
        //            floats[18],
        //            floats[19]);
        //    }
        //}

        Init::Wait();
        return s_createBuffer(device, desc, data, buffer);
    }

    // Init thread
    static void SetupHooks () {
        auto data = Init();

        if (data.context) {
            // This VfTable needs to be static so we keep the trampoline memory valid even after
            // leaving this function. It is *not* safe to use after leaving this function however
            // (the pointed to virtual table has been deallocated), and as such should not be
            // globally accessible.
            static VfTable s_contextTable(*(void***)data.context);
            // s_psSetConstantBuffers = s_contextTable.Detour<PSSetConstantBuffers>(DeviceContext_PSSetConstantBuffers_Hook);
            //s_contextTable.Detour<Map>(DeviceContext_Map_Hook, &s_map);
            s_contextTable.Detour<Unmap>(DeviceContext_Unmap_Hook, &s_unmap);
        }

        if (data.device) {
            // Same deal as for data.context above.
            static VfTable s_deviceTable(*(void***)data.device);
            //s_deviceTable.Detour<CreateBuffer>(Device_CreateBuffer_Hook, &s_createBuffer);
        }

        Destroy(data);
    }

}

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

namespace Test {

    static DetourInfo s_testDetour;

    static int Test (int a, int b) {
        auto prev = (void*)s_testDetour.data->trampoline;
        auto func = (decltype(Test)*)prev;
        return func(a, b);
    }
    
    static void SetupHooks () {
        auto module = GetModuleHandleA(nullptr);
        auto proc   = (decltype(Test)*)GetProcAddress(module, "Test");

        if (proc) {
            s_testDetour = Detour(proc, Test);
        }
    }

}

DWORD WINAPI InitThread (void*) {
    Scaleform::SetupHooks();
    Dx::SetupHooks();
    Init::Flag();
    return 0;
}


BOOL APIENTRY DllMain (HMODULE hModule,
                       DWORD   ul_reason_for_call,
                       LPVOID  lpReserved) {
    REF(hModule, lpReserved);

    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            Init::Init();
            Log::Open("hook.log");
            //Test::SetupHooks();
            CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
            Sleep(2000);
            break;

        case DLL_PROCESS_DETACH:
            Log::Close();
            Init::Destroy();
            break;

        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }

    return TRUE;
}

