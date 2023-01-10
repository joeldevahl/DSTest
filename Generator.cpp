#include "Generator.h"
#include "Render.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "meshopt.h"

#include <vector>

template<class T>
static void OutputDataToFile(LPCWSTR filename, const std::vector<T>& arr)
{
	const void* data = arr.data();
	DWORD size = arr.size() * sizeof(T);
    HANDLE file = CreateFile(filename, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	WriteFile(file, data, size, nullptr, nullptr);
	CloseHandle(file);
}

static void ConvertNodeHierarchy(cgltf_data* data, std::vector<Instance>& instances, cgltf_node* node)
{
	if (node->mesh != nullptr)
	{
		UINT meshID = node->mesh - data->meshes;
		instances.push_back(Instance{ {node->translation[0], node->translation[1], node->translation[2] }, 0, meshID });
	}
		
	for (int c = 0; c < node->children_count; ++c)
		ConvertNodeHierarchy(data, instances, node->children[c]);
}

void Generate()
{
	std::vector<Vertex> vertices;
	std::vector<UINT> indices;
	std::vector<Cluster> clusters;
	std::vector<Mesh> meshes;
	std::vector<Material> materials;
	std::vector<Instance> instances;

	cgltf_options options = {};
	cgltf_data* data = nullptr;
	cgltf_result result = cgltf_parse_file(&options, "untitled.gltf", &data);
	assert(result == cgltf_result_success);

	result = cgltf_load_buffers(&options, data, "untitled.bin");
	assert(result == cgltf_result_success);

	for (int m = 0; m < data->meshes_count; ++m)
	{
		UINT cluster_start = clusters.size();
		for (int p = 0; p < data->meshes[m].primitives_count; ++p)
		{
			cgltf_primitive& primitive = data->meshes[m].primitives[p];
			assert(primitive.type == cgltf_primitive_type_triangles);

			cgltf_attribute* positions = nullptr;
			for (int a = 0; a < primitive.attributes_count; ++a)
			{
				if (primitive.attributes[a].type == cgltf_attribute_type_position)
					positions = &primitive.attributes[a];
			}
			assert(positions != nullptr);

			cgltf_accessor* positions_accessor = positions->data;
			assert(positions_accessor != nullptr);
			assert(positions_accessor->type == cgltf_type_vec3);
			assert(positions_accessor->component_type == cgltf_component_type_r_32f);
			assert(positions_accessor->stride == 3 * sizeof(float));
			cgltf_buffer* position_buffer = positions_accessor->buffer_view->buffer;
			char* position_ptr_raw = reinterpret_cast<char*>(position_buffer->data) + positions_accessor->buffer_view->offset + positions_accessor->offset;
			float* position_ptr = reinterpret_cast<float*>(position_ptr_raw);

			cgltf_accessor* indices_accessor = primitive.indices;
			assert(indices_accessor != nullptr);
			assert(indices_accessor->type == cgltf_type_scalar);
			assert(indices_accessor->component_type == cgltf_component_type_r_16u);
			assert(indices_accessor->stride == sizeof(UINT16));
			cgltf_buffer* index_buffer = indices_accessor->buffer_view->buffer;
			char* index_ptr_raw = reinterpret_cast<char*>(index_buffer->data) + indices_accessor->buffer_view->offset + indices_accessor->offset;
			UINT16* index_ptr = reinterpret_cast<UINT16*>(index_ptr_raw);
	
			std::vector<UINT> temp_indices(indices_accessor->count);
			for (int i = 0; i < indices_accessor->count; ++i)
			{
				temp_indices[i] = index_ptr[i];
			}

			const size_t max_vertices = 64;
			const size_t max_triangles = 124;
			const float cone_weight = 0.0f;

			size_t max_meshlets = meshopt_buildMeshletsBound(temp_indices.size(), max_vertices, max_triangles);
			std::vector<meshopt_Meshlet> meshlets(max_meshlets);
			std::vector<unsigned int> meshlet_vertices(max_meshlets * max_vertices);
			std::vector<unsigned char> meshlet_triangles(max_meshlets * max_triangles * 3);
			size_t meshlet_count = meshopt_buildMeshlets(meshlets.data(),
				meshlet_vertices.data(),
				meshlet_triangles.data(),
				temp_indices.data(),
				temp_indices.size(),
				position_ptr,
				positions_accessor->count,
				positions_accessor->stride,
				max_vertices, max_triangles, cone_weight);

			for (int ml = 0; ml < meshlet_count; ++ml)
			{
				meshopt_Meshlet& meshlet = meshlets[ml];
				for (int v = 0; v < meshlet.vertex_count; ++v)
				{
					int o = 3 * meshlet_vertices[v];
					vertices.push_back(Vertex{ float3 { position_ptr[o], position_ptr[o + 1], position_ptr[o + 2] } });
				}

				int indexCount = meshlet.triangle_count * 3;
				for (int i = 0; i < indexCount; ++i)
				{
					indices.push_back(meshlet_triangles[i]);
				}

				clusters.push_back(Cluster{ meshlet.triangle_offset, (UINT)indexCount / 3, meshlet.vertex_offset, meshlet.vertex_count });
			}
		}

		meshes.push_back(Mesh{ cluster_start, (UINT)clusters.size() });
	}

	materials.push_back(Material{ {1.0f, 1.0f, 0.0f, 1.0f} });

	assert(data->scene != nullptr);

	for (int n = 0; n < data->scene->nodes_count; ++n)
		ConvertNodeHierarchy(data, instances, data->scene->nodes[n]);

	OutputDataToFile(L"vertices.raw", vertices);
	OutputDataToFile(L"indices.raw", indices);
	OutputDataToFile(L"clusters.raw", clusters);
	OutputDataToFile(L"meshes.raw", meshes);
	OutputDataToFile(L"materials.raw", materials);
	OutputDataToFile(L"instances.raw", instances);

	cgltf_free(data);
}
