#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct Render;

Render* CreateRender(UINT width, UINT height);
void Destroy(Render* render);

void Initialize(Render* render, HWND hwnd);
void Draw(Render* render);
