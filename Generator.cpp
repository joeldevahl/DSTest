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
		float4(-1.0f,  1.0f, 0.0f, 1.0f ),
        float4( 1.0f,  1.0f, 0.0f, 1.0f ),
        float4(-1.0f, -1.0f, 0.0f, 1.0f),
        float4( 1.0f, -1.0f, 0.0f, 1.0f),
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
		{0, 6, 0, 4},
	};
	OutputDataToFile(L"clusters.raw", clusters, sizeof(clusters));

	Mesh meshes[1] =
	{
		{0, 1}
	};
	OutputDataToFile(L"meshes.raw", meshes, sizeof(meshes));

	{
		const int xCount = 230;
		const int yCount = 130;
		const int count = xCount * yCount;
		Material* materials = new Material[count];
		Instance* instances = new Instance[count];

		int i = 0;
		for (int y = 0; y < yCount; ++y)
		{
			for (int x = 0; x < xCount; ++x)
			{
				materials[i].Color.x = (float)x / (float)xCount;
				materials[i].Color.y = (float)y / (float)yCount;
				materials[i].Color.z = 0.0f;
				materials[i].Color.w = 1.0f;

				float scale = 3.0f;
				instances[i].Position.x = scale * (x - xCount / 2.0f);
				instances[i].Position.y = scale * (y - yCount / 2.0f);
				instances[i].Position.z = -400.0f;
				instances[i].MeshIndex = 0;
				instances[i].MaterialIndex = i;
				++i;
			}
		}

		OutputDataToFile(L"materials.raw", materials, count * sizeof(Material));
		OutputDataToFile(L"instances.raw", instances, count * sizeof(Instance));

		delete[] materials;
		delete[] instances;
	}
}
