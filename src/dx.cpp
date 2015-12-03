// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#include "stdafx.h"

#include "dx.h"
#include "util.h"
#include "hooks.h"

namespace dx {

    using ID3D11DeviceContext_VSSetConstantBuffers_t = hooks::Function<7, void(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*)>;
    using ID3D11DeviceContext_Map_t = hooks::Function<14, HRESULT(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*)>;
    using ID3D11DeviceContext_Unmap_t = hooks::Function<15, void(ID3D11DeviceContext*, ID3D11Resource*, UINT)>;
    using IDXGISwapChain_ResizeBuffers_t = hooks::Function<13, HRESULT(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT)>;


    ///
    // Data
    ///

    static std::vector<OnDeviceCreate_t*> s_afterDeviceCreate;
    static std::vector<OnResourceMap_t*> s_afterResourceMap;
    static std::vector<OnResourceUnmap_t*> s_beforeResourceUnmap;
    static std::vector<OnViewportResize_t*> s_afterViewportResize;
    static std::vector<OnVsSetConstantBuffers_t*> s_afterVsSetConstantBuffers;

    static UnsafePtr<IDXGISwapChain> s_swapChain;

    static decltype(D3D11CreateDeviceAndSwapChain) * s_createDevice;
    static ID3D11DeviceContext_VSSetConstantBuffers_t::Fn* s_deviceContextVsSetConstantBuffers;
    static ID3D11DeviceContext_Map_t::Fn* s_deviceContextMap;
    static ID3D11DeviceContext_Unmap_t::Fn* s_deviceContextUnmap;
    static IDXGISwapChain_ResizeBuffers_t::Fn* s_swapChainResizeBuffers;


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
            for (auto cb : s_afterResourceMap) {
                cb(context, resource, mappedResource);
            }
        }

        return result;
    }

    static void DeviceContextUnmap (ID3D11DeviceContext* context,
                                    ID3D11Resource*      resource,
                                    UINT                 subResource)
    {
        for (auto cb : s_beforeResourceUnmap) {
            cb(context, resource);
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

        if (SUCCEEDED(result) && s_swapChain == swapChain) {
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
                for (auto cb : s_afterViewportResize) {
                    cb(Width, Height);
                }
            }
        }

        return result;
    }

    static void DeviceContextVsSetConstantBuffers (ID3D11DeviceContext* context,
                                                   UINT                 slotStart,
                                                   UINT                 numBuffers,
                                                   ID3D11Buffer* const* buffers)
    {
        s_deviceContextVsSetConstantBuffers(context, slotStart, numBuffers, buffers);

        for (auto cb : s_afterVsSetConstantBuffers) {
            cb(context, slotStart, numBuffers, buffers);
        }
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
        ScopedTimer timer(__FUNCTION__, "CreateDevice hook: %u.%u ms");

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

        // The `IsValid()` calls below are really lies. The vftables may be freed already. In these
        // cases, we really only using them to determine whether we've already detoured the
        // functions or not. We still should not make any calls using the tables!
        if (context) {
            if (!s_dcVftable.IsValid()) {
                s_dcVftable = *(void***)context;

                if (s_afterResourceMap.size()) {
                    s_dcVftable.Detour<ID3D11DeviceContext_Map_t>(DeviceContextMap, &s_deviceContextMap);
                }

                if (s_beforeResourceUnmap.size()) {
                    s_dcVftable.Detour<ID3D11DeviceContext_Unmap_t>(DeviceContextUnmap, &s_deviceContextUnmap);
                }

                if (s_afterVsSetConstantBuffers.size()) {
                    s_dcVftable.Detour<ID3D11DeviceContext_VSSetConstantBuffers_t>(DeviceContextVsSetConstantBuffers, &s_deviceContextVsSetConstantBuffers);
                }
            }
        } else if (!s_dcVftable.IsValid()) {
            ERR("No device context");
        }

        if (swapChain) {
            if (!s_scVftable.IsValid()) {
                s_scVftable = *(void***)swapChain;

                if (s_afterViewportResize.size()) {
                    s_scVftable.Detour<IDXGISwapChain_ResizeBuffers_t>(SwapChainResizeBuffers, &s_swapChainResizeBuffers);
                }
            }
        } else if (!s_scVftable.IsValid()) {
            ERR("No swap chain");
        }

        // Invoke DeviceCreate and Resize callbacks, in that order.
        for (auto cb : s_afterDeviceCreate) {
            cb(context, device, swapChain);
        }

        if (swapChain) {
            for (auto cb : s_afterViewportResize) {
                cb(pSwapChainDesc->BufferDesc.Width, pSwapChainDesc->BufferDesc.Height);
            }
        } else {
            ERR("Can't determine initial buffer sizes, no swap chain desc passed");
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
        if (callbacks.afterDeviceCreate) {
            s_afterDeviceCreate.emplace_back(callbacks.afterDeviceCreate);
        }
        if (callbacks.afterResourceMap) {
            s_afterResourceMap.emplace_back(callbacks.afterResourceMap);
        }
        if (callbacks.beforeResourceUnmap) {
            s_beforeResourceUnmap.emplace_back(callbacks.beforeResourceUnmap);
        }
        if (callbacks.afterViewportResize) {
            s_afterViewportResize.emplace_back(callbacks.afterViewportResize);
        }
        if (callbacks.afterVsSetConstantBuffers) {
            s_afterVsSetConstantBuffers.emplace_back(callbacks.afterVsSetConstantBuffers);
        }
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
