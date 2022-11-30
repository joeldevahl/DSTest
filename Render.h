#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct Render;

struct float3 { float x, y, z; };
struct float4 { float x, y, z, w; };
typedef UINT uint;
#include "Common.h"

Render* CreateRender(UINT width, UINT height);
void Destroy(Render* render);

void Initialize(Render* render, HWND hwnd);
void Draw(Render* render);
