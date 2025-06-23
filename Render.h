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

inline float3 abs(float3 const& val)
{
	return float3(fabsf(val.x), fabsf(val.y), fabsf(val.z));
} 

inline CenterExtentsAABB TransformAABB(const CenterExtentsAABB& in, const float4x4& mat)
{
	float3 center = transform(in.Center, mat);

	float4x4 absmat = float4x4(
		fabsf(mat.m11), fabsf(mat.m12), fabsf(mat.m13), 0.0f,
		fabsf(mat.m21), fabsf(mat.m22), fabsf(mat.m23), 0.0f,
		fabsf(mat.m31), fabsf(mat.m32), fabsf(mat.m33), 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	);

	float3 extents = transform_normal(in.Extents, absmat);

	return CenterExtentsAABB{ center, extents };
}

Render* CreateRender(UINT width, UINT height);
void Destroy(Render* render);
 
void Initialize(Render* render, HWND hwnd, bool useWarp);
void Draw(Render* render);

void SetWorkGraph(Render* render, bool useWorkGraph);