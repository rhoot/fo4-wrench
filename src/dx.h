// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#pragma once

#include <cstdint>

///
// Forward declares
///

struct D3D11_MAPPED_SUBRESOURCE;
struct ID3D11DeviceContext;
struct ID3D11Device;
struct ID3D11Resource;
struct IDXGISwapChain;


///
// Dx
///

namespace dx {

    using OnDeviceCreate_t = void(ID3D11DeviceContext*, ID3D11Device*, IDXGISwapChain*);
    using OnResourceMap_t = void(ID3D11DeviceContext*, ID3D11Resource*, D3D11_MAPPED_SUBRESOURCE*);
    using OnResourceUnmap_t = void(ID3D11DeviceContext*, ID3D11Resource*);
    using OnViewportResize_t = void(uint32_t, uint32_t);
    using OnVsSetConstantBuffers_t = void(ID3D11DeviceContext*, size_t, size_t, ID3D11Buffer * const*);

    struct Callbacks {
        OnDeviceCreate_t* afterDeviceCreate = nullptr;
        OnResourceMap_t* afterResourceMap = nullptr;
        OnResourceUnmap_t* beforeResourceUnmap = nullptr;
        OnViewportResize_t* afterViewportResize = nullptr;
        OnVsSetConstantBuffers_t* afterVsSetConstantBuffers = nullptr;
    };

    void Register (const Callbacks& callbacks);
    void Init ();

} // namespace dx
