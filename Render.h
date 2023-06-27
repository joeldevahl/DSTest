#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <WindowsNumerics.h>
using namespace Windows::Foundation::Numerics;
using namespace DirectX;
typedef UINT uint;
typedef DirectX::XMUINT4 uint4;

struct Render;
#define CB_ALIGN _declspec(align(256u)) 
#include "Common.h"

inline CenterExtentsAABB MinMaxToCenterExtents(const MinMaxAABB& mm)
{
	return CenterExtentsAABB{
		(mm.Min + mm.Max) * 0.5f,
		(mm.Max - mm.Min) * 0.5f
    };
}

Render* CreateRender(UINT width, UINT height);
void Destroy(Render* render);

void Initialize(Render* render, HWND hwnd);
void Draw(Render* render);
