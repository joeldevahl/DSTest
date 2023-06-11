#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <directxmath.h> // for XMFLOAT4x4
using namespace DirectX;

struct Render;

typedef UINT uint;
typedef XMUINT4 uint4;
typedef XMFLOAT2 float2;
typedef XMFLOAT3 float3;
typedef XMFLOAT4 float4;
typedef XMFLOAT4X4 float4x4;
#define CB_ALIGN _declspec(align(256u)) 
#include "Common.h"

Render* CreateRender(UINT width, UINT height);
void Destroy(Render* render);

void Initialize(Render* render, HWND hwnd);
void Draw(Render* render);
