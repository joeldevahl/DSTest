#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct Render;

struct Vertex
{
    float Position[4];
};

struct Instance
{
    float Position[3];
    UINT MeshIndex;
};

struct Mesh
{
    UINT ClusterStart;
    UINT ClusterCount;
};

struct Cluster
{
    UINT IndexStart;
    UINT IndexCount;
};


Render* CreateRender(UINT width, UINT height);
void Destroy(Render* render);

void Initialize(Render* render, HWND hwnd);
void Draw(Render* render);
