#include "Generator.h"
#include "Render.h"

static void OutputDataToFile(LPCWSTR filename, void* data, DWORD size)
{

    HANDLE file = CreateFile(filename, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	WriteFile(file, data, size, nullptr, nullptr);
	CloseHandle(file);
}

void Generate()
{
	Vertex vertices[4] =
	{
		{ -1.0f,  1.0f, 0.0f, 1.0f },
        {  1.0f,  1.0f, 0.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 1.0f },
        {  1.0f, -1.0f, 0.0f, 1.0f },
	};
	OutputDataToFile(L"vertices.raw", vertices, sizeof(vertices));

	UINT indices[6] =
	{
		0, 1, 2,
		1, 3, 2,
	};
	OutputDataToFile(L"indices.raw", indices, sizeof(indices));

	Cluster clusters[1] =
	{
		{0, 6},
	};
	OutputDataToFile(L"clusters.raw", clusters, sizeof(clusters));

	Mesh meshes[1] =
	{
		{0, 1}
	};
	OutputDataToFile(L"meshes.raw", meshes, sizeof(meshes));

	Instance instances[1] =
	{
		{ {0.0f, 0.0f, 0.0f}, 0 },
	};
	OutputDataToFile(L"instances.raw", instances, sizeof(instances));
}