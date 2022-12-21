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
	Vertex vertices_base[] =
	{
		float4(-1.0f,  1.0f,  1.0f, 1.0f),
        float4( 1.0f,  1.0f,  1.0f, 1.0f),
        float4(-1.0f, -1.0f,  1.0f, 1.0f),
        float4( 1.0f, -1.0f,  1.0f, 1.0f),

		float4(-1.0f,  1.0f, -1.0f, 1.0f),
        float4( 1.0f,  1.0f, -1.0f, 1.0f),
        float4(-1.0f, -1.0f, -1.0f, 1.0f),
        float4( 1.0f, -1.0f, -1.0f, 1.0f),
	};

	Vertex vertices[] =
	{
		// front cluster
		vertices_base[0],
		vertices_base[1],
		vertices_base[2],
		vertices_base[3],

		// back cluster
		vertices_base[5],
		vertices_base[4],
		vertices_base[7],
		vertices_base[6],

		// left cluster
		vertices_base[4],
		vertices_base[0],
		vertices_base[6],
		vertices_base[2],

		// right cluster
		vertices_base[1],
		vertices_base[5],
		vertices_base[3],
		vertices_base[7],

		// top cluster
		vertices_base[4],
		vertices_base[5],
		vertices_base[0],
		vertices_base[1],

		// bottom cluster
		vertices_base[2],
		vertices_base[3],
		vertices_base[6],
		vertices_base[7],
	};
	OutputDataToFile(L"vertices.raw", vertices, sizeof(vertices));

	UINT indices[] =
	{
		// front
		0, 1, 2,
		1, 3, 2,

		// back
		0, 1, 2,
		1, 3, 2,

		// left
		0, 1, 2,
		1, 3, 2,

		// right
		0, 1, 2,
		1, 3, 2,

		// top
		0, 1, 2,
		1, 3, 2,

		// bottom
		0, 1, 2,
		1, 3, 2,
	};
	OutputDataToFile(L"indices.raw", indices, sizeof(indices));

	Cluster clusters[] =
	{
		{ 0, 6,  0, 4},
		{ 6, 6,  4, 4},
		{12, 6,  8, 4},
		{18, 6, 12, 4},
		{24, 6, 16, 4},
		{30, 6, 20, 4},
	};
	OutputDataToFile(L"clusters.raw", clusters, sizeof(clusters));

	Mesh meshes[1] =
	{
		{0, 6}
	};
	OutputDataToFile(L"meshes.raw", meshes, sizeof(meshes));

	{
		const int xCount = 10;
		const int yCount = 10;
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
				instances[i].Position.z = -50.0f;
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
