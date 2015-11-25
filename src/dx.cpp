// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#include "stdafx.h"

#include "dx.h"
#include "util.h"
#include "hooks.h"

namespace dx {

    using ID3D11DeviceContext_Map_t = hooks::Function<14, HRESULT(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*)>;
    using ID3D11DeviceContext_Unmap_t = hooks::Function<15, void(ID3D11DeviceContext*, ID3D11Resource*, UINT)>;
    using IDXGISwapChain_ResizeBuffers_t = hooks::Function<13, HRESULT(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT)>;

    ///
    // Data
    ///

    static std::vector<Callbacks> s_callbacks;

    static decltype(D3D11CreateDeviceAndSwapChain) * s_createDevice;
    static ID3D11DeviceContext_Map_t::Fn* s_deviceContextMap;
    static ID3D11DeviceContext_Unmap_t::Fn* s_deviceContextUnmap;
    static IDXGISwapChain_ResizeBuffers_t::Fn* s_swapChainResizeBuffers;

    // WARNING: This pointer must NOT be dereferenced, as we did not take a reference to it. It's
    // *only* used for testing whether other swap chains are referring to the global one.
    static IDXGISwapChain* s_swapChain;


    ///
    // Hooks
    ///

    static HRESULT DeviceContextMap (ID3D11DeviceContext*      context,
                                     ID3D11Resource*           resource,
                                     UINT                      subResource,
                                     D3D11_MAP                 mapType,
                                     UINT                      mapFlags,
                                     D3D11_MAPPED_SUBRESOURCE* mappedResource)
    {
        auto result = s_deviceContextMap(context, resource, subResource, mapType, mapFlags, mappedResource);

        if (SUCCEEDED(result)) {
            for (auto& cb : s_callbacks) {
                if (cb.afterResourceMap) {
                    cb.afterResourceMap(context, resource, mappedResource);
                }
            }
        }

        return result;
    }

    static void DeviceContextUnmap (ID3D11DeviceContext* context,
                                    ID3D11Resource*      resource,
                                    UINT                 subResource)
    {
        for (auto& cb : s_callbacks) {
            if (cb.beforeResourceUnmap) {
                cb.beforeResourceUnmap(context, resource);
            }
        }

        return s_deviceContextUnmap(context, resource, subResource);
    }

    static HRESULT SwapChainResizeBuffers (IDXGISwapChain* swapChain,
                                           UINT            BufferCount,
                                           UINT            Width,
                                           UINT            Height,
                                           DXGI_FORMAT     NewFormat,
                                           UINT            SwapChainFlags)
    {
        auto result = s_swapChainResizeBuffers(swapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

        if (SUCCEEDED(result)) {
            if (!Width || !Height) {
                DXGI_SWAP_CHAIN_DESC desc;

                if (SUCCEEDED(swapChain->GetDesc(&desc))) {
                    Width = desc.BufferDesc.Width;
                    Height = desc.BufferDesc.Height;
                } else {
                    ERR("Could not get swap chain description");
                }
            }

            if (Width && Height) {
                for (auto& cb : s_callbacks) {
                    if (cb.afterViewportResize) {
                        cb.afterViewportResize(Width, Height);
                    }
                }
            }
        }

        return result;
    }

    static HRESULT WINAPI CreateDeviceAndSwapChain (IDXGIAdapter*               pAdapter,
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
        ID3D11DeviceContext* context = nullptr;
        ID3D11Device* device = nullptr;
        IDXGISwapChain* swapChain = nullptr;

        auto result = s_createDevice(pAdapter,
                                     DriverType,
                                     Software,
                                     Flags,
                                     pFeatureLevels,
                                     FeatureLevels,
                                     SDKVersion,
                                     pSwapChainDesc,
                                     &swapChain,
                                     &device,
                                     pFeatureLevel,
                                     &context);

        if (FAILED(result)) {
            ERR("Failed to create D3D device: %#lx", result);
            return result;
        }

        // These VfTables needs to be static so we keep the trampoline memory valid even after
        // leaving this function. It is *not* safe to use after leaving this function however
        // (the pointed to virtual table has been deallocated), and as such should not be
        // globally accessible.
        static hooks::VfTable s_dcVftable;
        static hooks::VfTable s_scVftable;

        s_swapChain = swapChain;

        // Determine what detours we need to apply.
        auto hookMap = false;
        auto hookUnmap = false;
        auto hookResize = false;

        for (auto& cb : s_callbacks) {
            hookMap = hookMap || cb.afterResourceMap != nullptr;
            hookUnmap = hookUnmap || cb.beforeResourceUnmap != nullptr;
            hookResize = hookResize || cb.afterViewportResize != nullptr;
        }

        // The `IsValid()` calls below are really lies. The vftables may be freed already. In these
        // cases, we really only using them to determine whether we've already detoured the
        // functions or not. We still should not make any calls using the tables!
        if (context) {
            if (!s_dcVftable.IsValid()) {
                s_dcVftable = *(void***)context;

                if (hookMap) {
                    s_dcVftable.Detour<ID3D11DeviceContext_Map_t>(DeviceContextMap, &s_deviceContextMap);
                }

                if (hookUnmap) {
                    s_dcVftable.Detour<ID3D11DeviceContext_Unmap_t>(DeviceContextUnmap, &s_deviceContextUnmap);
                }
            }
        } else if (!s_dcVftable.IsValid()) {
            ERR("No device context");
        }

        if (swapChain) {
            if (!s_scVftable.IsValid()) {
                s_scVftable = *(void***)swapChain;

                if (hookResize) {
                    s_scVftable.Detour<IDXGISwapChain_ResizeBuffers_t>(SwapChainResizeBuffers, &s_swapChainResizeBuffers);
                }
            }
        } else if (!s_scVftable.IsValid()) {
            ERR("No swap chain");
        }

        // Invoke DeviceCreate and Resize callbacks, in that order. This ensures that DeviceCreate
        // callbacks don't rely on the size being readily available, in case Bethesda ever changes
        // their mind about passing in the swap chain desc.
        if (!pSwapChainDesc) {
            ERR("Can't determine initial buffer sizes, no swap chain desc passed");
        }

        for (auto& cb : s_callbacks) {
            if (cb.afterDeviceCreate) {
                cb.afterDeviceCreate(context, device, swapChain);
            }
            if (cb.afterViewportResize && pSwapChainDesc) {
                cb.afterViewportResize(pSwapChainDesc->BufferDesc.Width, pSwapChainDesc->BufferDesc.Height);
            }
        }

        // Forward the interfaces we stole. ◔_◔
        if (ppSwapChain) {
            *ppSwapChain = swapChain;
        } else {
            swapChain->Release();
        }

        if (ppDevice) {
            *ppDevice = device;
        } else {
            device->Release();
        }

        if (ppImmediateContext) {
            *ppImmediateContext = context;
        } else {
            context->Release();
        }

        return result;
    }


    ///
    // Exports
    ///

    void Register (const Callbacks& callbacks)
    {
        s_callbacks.emplace_back(callbacks);
    }

    void Init ()
    {
        auto d3d = GetModuleHandleA("d3d11");
        if (!d3d) {
            ERR("No d3d available.");
            return;
        }

        auto proc = (decltype(s_createDevice))GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain");
        if (!proc) {
            ERR("Could not find `D3D11CreateDeviceAndSwapChain'");
            return;
        }

        static auto s_detour = hooks::Detour(proc, &CreateDeviceAndSwapChain, &s_createDevice);
    }

} // namespace dx
