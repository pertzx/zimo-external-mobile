#pragma once

#include <d3d11.h>
#include <cstdint>
#include <cstddef>

namespace ClothIcons
{
    void Init(ID3D11Device* device);
    void Shutdown();

    void Request(uint32_t itemID);
    void RequestMany(const uint32_t* ids, size_t count);

    ID3D11ShaderResourceView* Get(uint32_t itemID);
    void Pump();
}
