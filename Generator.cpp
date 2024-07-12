#include "Generator.h"
#include "Render.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "meshoptimizer.h"

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
    
static void ConvertNodeHierarchy(cgltf_data* data, std::vector<Instance>& instances, const std::vector<Mesh>& meshes, cgltf_node* node)
{
	if (node->mesh != nullptr)
	{
		UINT meshID = node->mesh - data->meshes;
		// TODO: wrong, since a mesh can have several primitives with different materials
		UINT materialID = node->mesh->primitives->material - data->materials;

		if (node->has_matrix)
		{
			// We don't handle this for now
			assert(false);
		}
		else
		{
			float3 scale(1.0f, 1.0f, 1.0f);
			quaternion rotation(0.0f, 0.0f, 0.0f, 1.0f);
			float3 translation(0.0f, 0.0f, 0.0f);

			if (node->has_scale)
				scale = float3(node->scale[0], node->scale[1], node->scale[2]);
			if (node->has_rotation)
				rotation = quaternion(node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]);
			if (node->has_translation)
				translation = float3(node->translation[0], node->translation[1], node->translation[2]);

			float4x4 scaleMat = make_float4x4_scale(scale);
			float4x4 rotationMat = make_float4x4_from_quaternion(rotation);
			float4x4 translationMat = make_float4x4_translation(translation);

			float4x4 modelMat = scaleMat * rotationMat * translationMat;


			instances.push_back(Instance{ modelMat, meshID, materialID, TransformAABB(meshes[meshID].Box, modelMat)});
		}
	}
		
	for (int c = 0; c < node->children_count; ++c)
		ConvertNodeHierarchy(data, instances, meshes, node->children[c]);
}

void Generate(const char* filename, const char* filenameBin)
{
	std::vector<float3> out_positions;
	std::vector<float3> out_normals;
	std::vector<float4> out_tangents;
	std::vector<float2> out_texcoords;
	std::vector<UINT> out_indices;
	std::vector<Cluster> out_clusters;
	std::vector<Mesh> out_meshes;
	std::vector<Material> out_materials;
	std::vector<Instance> out_instances;

	cgltf_options options = {};
	cgltf_data* data = nullptr;
	cgltf_result result = cgltf_parse_file(&options, filename, &data);
	assert(result == cgltf_result_success);

	result = cgltf_load_buffers(&options, data, filenameBin);
	assert(result == cgltf_result_success);

	for (int m = 0; m < data->meshes_count; ++m)
	{
		UINT cluster_start = out_clusters.size();
		MinMaxAABB meshBounds = MinMaxAABB{
			float3 {FLT_MAX, FLT_MAX, FLT_MAX},
			float3 {FLT_MIN, FLT_MIN, FLT_MIN},
		};

		for (int p = 0; p < data->meshes[m].primitives_count; ++p)
		{
			cgltf_primitive& primitive = data->meshes[m].primitives[p];
			assert(primitive.type == cgltf_primitive_type_triangles);

			cgltf_attribute* positions = nullptr;
			cgltf_attribute* normals = nullptr;
			cgltf_attribute* tangents = nullptr;
			cgltf_attribute* texcoords = nullptr;
			for (int a = 0; a < primitive.attributes_count; ++a)
			{
				if (primitive.attributes[a].type == cgltf_attribute_type_position)
					positions = &primitive.attributes[a];
				else if (primitive.attributes[a].type == cgltf_attribute_type_normal)
					normals = &primitive.attributes[a];
				else if (primitive.attributes[a].type == cgltf_attribute_type_tangent)
					tangents = &primitive.attributes[a];
				else if (primitive.attributes[a].type == cgltf_attribute_type_texcoord)
					texcoords = &primitive.attributes[a];
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

			float* normal_ptr = nullptr;
			if (normals)
			{
				cgltf_accessor* normals_accessor = normals->data;
				assert(normals_accessor != nullptr);
				assert(normals_accessor->type == cgltf_type_vec3);
				assert(normals_accessor->component_type == cgltf_component_type_r_32f);
				assert(normals_accessor->stride == 3 * sizeof(float));
				cgltf_buffer* normal_buffer = normals_accessor->buffer_view->buffer;
				char* normal_ptr_raw = reinterpret_cast<char*>(normal_buffer->data) + normals_accessor->buffer_view->offset + normals_accessor->offset;
				normal_ptr = reinterpret_cast<float*>(normal_ptr_raw);
			}

			float* tangent_ptr = nullptr;
			if (tangents)
			{
				cgltf_accessor* tangents_accessor = tangents->data;
				assert(tangents_accessor != nullptr);
				assert(tangents_accessor->type == cgltf_type_vec4);
				assert(tangents_accessor->component_type == cgltf_component_type_r_32f);
				assert(tangents_accessor->stride == 4 * sizeof(float));
				cgltf_buffer* tangent_buffer = tangents_accessor->buffer_view->buffer;
				char* tangent_ptr_raw = reinterpret_cast<char*>(tangent_buffer->data) + tangents_accessor->buffer_view->offset + tangents_accessor->offset;
				tangent_ptr = reinterpret_cast<float*>(tangent_ptr_raw);
			}

			float* texcoord_ptr = nullptr;
			if (texcoords)
			{
				cgltf_accessor* texcoords_accessor = texcoords->data;
				assert(texcoords_accessor != nullptr);
				assert(texcoords_accessor->type == cgltf_type_vec2);
				assert(texcoords_accessor->component_type == cgltf_component_type_r_32f);
				assert(texcoords_accessor->stride == 2 * sizeof(float));
				cgltf_buffer* texcoord_buffer = texcoords_accessor->buffer_view->buffer;
				char* texcoord_ptr_raw = reinterpret_cast<char*>(texcoord_buffer->data) + texcoords_accessor->buffer_view->offset + texcoords_accessor->offset;
				texcoord_ptr = reinterpret_cast<float*>(texcoord_ptr_raw);
			}

			cgltf_accessor* indices_accessor = primitive.indices;
			assert(indices_accessor != nullptr);
			assert(indices_accessor->type == cgltf_type_scalar);
			cgltf_buffer* index_buffer = indices_accessor->buffer_view->buffer;
			char* index_ptr_raw = reinterpret_cast<char*>(index_buffer->data) + indices_accessor->buffer_view->offset + indices_accessor->offset;


			std::vector<UINT> temp_indices(indices_accessor->count);
			if (indices_accessor->component_type == cgltf_component_type_r_16u)
			{
				assert(indices_accessor->stride == sizeof(UINT16));
				UINT16* index_ptr = reinterpret_cast<UINT16*>(index_ptr_raw);
				for (int i = 0; i < indices_accessor->count; ++i)
				{
					temp_indices[i] = index_ptr[i];
				}
			}
			else
			{
				assert(indices_accessor->component_type == cgltf_component_type_r_32u);
				UINT32* index_ptr = reinterpret_cast<UINT32*>(index_ptr_raw);
				for (int i = 0; i < indices_accessor->count; ++i)
				{
					temp_indices[i] = index_ptr[i];
				}
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

				UINT outputVerticesOffset = out_positions.size();
				MinMaxAABB clusterBounds = MinMaxAABB{
					float3 {FLT_MAX, FLT_MAX, FLT_MAX},
					float3 {FLT_MIN, FLT_MIN, FLT_MIN},
				};

				for (int v = 0; v < meshlet.vertex_count; ++v)
				{
					int o2 = 2 * meshlet_vertices[meshlet.vertex_offset + v];
					int o3 = 3 * meshlet_vertices[meshlet.vertex_offset + v];
					int o4 = 3 * meshlet_vertices[meshlet.vertex_offset + v];
					float3 pos = float3{ position_ptr[o3], position_ptr[o3 + 1], position_ptr[o3 + 2] };
					out_positions.push_back(pos);

					clusterBounds.Min = min(clusterBounds.Min, pos);
					clusterBounds.Max = max(clusterBounds.Max, pos);

					float3 normal = float3 { 0.0f, 0.0f, 0.0f };
					if (normal_ptr)
						normal = float3 { normal_ptr[o3], normal_ptr[o3 + 1], normal_ptr[o3 + 2] };
					out_normals.push_back(normal);

					float4 tangent = float4 { 0.0f, 0.0f, 0.0f, 0.0f };
					if (tangent_ptr)
						tangent = float4 { tangent_ptr[o4], tangent_ptr[o4 + 1], tangent_ptr[o4 + 2], tangent_ptr[o4 + 3] };
					out_tangents.push_back(tangent);

					float2 texcoord = float2 { 0.0f, 0.0f };
					if (texcoord_ptr)
						texcoord = float2 { texcoord_ptr[o2], texcoord_ptr[o2 + 1] };
					out_texcoords.push_back(texcoord);
				}

				UINT outputTriangleOffset = out_indices.size() / 3;
				for (int t = 0; t < meshlet.triangle_count; ++t)
				{
					int o = meshlet.triangle_offset + 3 * t;
					out_indices.push_back(meshlet_triangles[o + 0]);
					out_indices.push_back(meshlet_triangles[o + 2]);
					out_indices.push_back(meshlet_triangles[o + 1]);
				}

				out_clusters.push_back(Cluster{
					outputTriangleOffset,
					meshlet.triangle_count,
					outputVerticesOffset,
					meshlet.vertex_count,
					MinMaxToCenterExtents(clusterBounds),
				});

				meshBounds.Min = min(meshBounds.Min, clusterBounds.Min);
				meshBounds.Max = max(meshBounds.Max, clusterBounds.Max);
			}
		}

		out_meshes.push_back(Mesh{
			cluster_start,
			(UINT)out_clusters.size() - cluster_start,
			MinMaxToCenterExtents(meshBounds),
		});
	}
	
	for (int m = 0; m < data->materials_count; ++m)
	{
		cgltf_material* mat = data->materials + m;

		float4 color = { 1.0f, 1.0f, 0.0f, 1.0f };
		float metallic = 0.0f;
		float roughness = 1.0f;

		if (mat->has_pbr_metallic_roughness)
		{
			color = float4(mat->pbr_metallic_roughness.base_color_factor[0],
				mat->pbr_metallic_roughness.base_color_factor[1],
				mat->pbr_metallic_roughness.base_color_factor[2],
				mat->pbr_metallic_roughness.base_color_factor[3]);
		}

		out_materials.push_back(Material{ color, metallic, roughness });
	}

	if (data->materials_count == 0)
	{
		float4 color = { 1.0f, 1.0f, 0.0f, 1.0f };
		float metallic = 0.0f;
		float roughness = 1.0f;
		out_materials.push_back(Material{ color, metallic, roughness });
	}

	assert(data->scene != nullptr);

	for (int n = 0; n < data->scene->nodes_count; ++n)
		ConvertNodeHierarchy(data, out_instances, out_meshes, data->scene->nodes[n]);

	OutputDataToFile(L"positions.raw", out_positions);
	OutputDataToFile(L"normals.raw", out_normals);
	OutputDataToFile(L"tangents.raw", out_tangents);
	OutputDataToFile(L"texcoords.raw", out_texcoords);
	OutputDataToFile(L"indices.raw", out_indices);
	OutputDataToFile(L"clusters.raw", out_clusters);
	OutputDataToFile(L"meshes.raw", out_meshes);
	OutputDataToFile(L"materials.raw", out_materials);
	OutputDataToFile(L"instances.raw", out_instances);

	cgltf_free(data);
}
