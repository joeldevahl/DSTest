#include "Generator.h"
#include "Render.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "meshoptimizer.h"

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>

struct CpuVertex
{
	float3 pos;
	float3 normal;
	float4 tangent;
	float2 texcoord;
};

struct MeshletLodLevel
{


	// data from metis
	std::vector<std::vector<uint32_t>> clustersIndices;

	std::vector<std::unordered_set<uint64_t>> edgeSets;
};

struct MeshletGeneratorContext
{
	// global flat index/vertex list
	std::vector<CpuVertex> vertices;
	std::vector<unsigned int> indices;

	std::vector<MeshletLodLevel> lods;
};

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

static uint64_t PackEdge(int v0, int v1)
{
	if (v0 < v1)
		return (uint64_t)v0 << 32 | (uint64_t)v1;
	else
		return (uint64_t)v1 << 32 | (uint64_t)v0;
}

static uint64_t PackCluster(int c0, int c1)
{
	// TODO: pack in cluster lod level as well
	if (c0 < c1)
		return (uint64_t)c0 << 32 | (uint64_t)c1;
	else
		return (uint64_t)c1 << 32 | (uint64_t)c0;
}

#define MAX_CLUSTERS_PER_CANDIDATE 8
#define PREFERED_CLUSTERS_PER_CANDIDATE 4

struct MergeCandidate
{
	int set[MAX_CLUSTERS_PER_CANDIDATE] = { 0 };
	int count = 0;
	int score = 0;

	void Push(int c)
	{
		assert(count < MAX_CLUSTERS_PER_CANDIDATE);
		set[count] = c;
		count += 1;
	}

	int Pop()
	{
		assert(count > 0);
		count -= 1;
		return set[count];
	}

	bool Contains(int c) const
	{
		for (int i = 0; i < count; ++i)
			if (set[i] == c)
				return true;
		return false;
	}
};

MergeCandidate SelectBestCandidateTree(const MergeCandidate& input, const std::vector<std::pair<int, int>>& sortedAdjacency, const std::unordered_map<uint64_t, int>& clusterAdjacencyMap)
{
	// If the limit is hit we end the search
	if (input.count >= PREFERED_CLUSTERS_PER_CANDIDATE)
		return input;

	// Go over all remaining possible clusters to add
	MergeCandidate output = input;
	for (auto& c : sortedAdjacency)
	{
		// If input candidate already contains the cluster it won't get added
		if (input.Contains(c.first))
			continue;

		// Go over all input clusters and get the total connectivity score for the selected cluster
		int score = 0;
		for (int i = 0; i < input.count; ++i)
		{
			auto iter = clusterAdjacencyMap.find(PackCluster(input.set[i], c.first));
			if (iter != clusterAdjacencyMap.end())
				score += iter->second;
		}

		// If there is any score we try to recursively build up a bigger set of clusters
		if (score > 0)
		{
			// Add the candidate to a local copy and accumulate score
			MergeCandidate candidate = input;
			candidate.Push(c.first);
			candidate.score += score;

			// Recursively find the best candidate
			candidate = SelectBestCandidateTree(candidate, sortedAdjacency, clusterAdjacencyMap);

			// Store the best candidate in the output
			if (candidate.score > output.score)
				output = candidate;
		} 
 	}

	return output;
}

typedef int32_t idx_t;
typedef float real_t;

typedef int (__stdcall *METIS_PartMeshDual_func)(idx_t* ne, idx_t* nn, idx_t* eptr, idx_t* eind,
	idx_t* vwgt, idx_t* vsize, idx_t* ncommon, idx_t* nparts,
	real_t* tpwgts, idx_t* options, idx_t* objval, idx_t* epart,
	idx_t* npart);

static METIS_PartMeshDual_func METIS_PartMeshDual = nullptr;

void GenerateClusters(std::vector<std::vector<uint32_t>>& outputClustersIndices, const std::vector<uint32_t>& inputIndices, uint32_t maxTriangles, uint32_t maxVertices)
{
	idx_t numElements = inputIndices.size() / 3;
	idx_t numNodes = inputIndices.size();
	std::vector<idx_t> eptr(numElements + 1); // TODO: reuse
	idx_t* eind = (idx_t*)inputIndices.data();
	idx_t numCommon = 1;
	std::vector<idx_t> epart(numElements); // TODO: reuse
	std::vector<idx_t> npart(numNodes); // TODO: reuse

	for (int i = 0; i < eptr.size(); ++i)
		eptr[i] = i * 3;

	bool done = false;
	idx_t numParts = std::max<idx_t>(numElements / maxTriangles, 1); // Stupid initial etimate
	while (!done) 
	{
		if (numParts > 1)
		{
			idx_t objval = 0;
			int ret = METIS_PartMeshDual(&numElements, &numNodes, eptr.data(), eind, NULL, NULL, &numCommon, &numParts, NULL, NULL, &objval, epart.data(), npart.data());
		}
		else
		{
			std::fill(epart.begin(), epart.end(), 0);
			std::fill(npart.begin(), npart.end(), 0);
		}

		std::vector<idx_t> clusterElemCounter(numParts); // TODO: reuse
		std::vector<idx_t> clusterNodeCounter(numParts); // TODO: reuse
		idx_t maxElemCount = 0;
		idx_t maxNodeCount = 0;
		for (int ei = 0; ei < epart.size(); ++ei)
		{
			idx_t e = epart[ei];
			clusterElemCounter[e] += 1;
			maxElemCount = std::max(maxElemCount, clusterElemCounter[e]);
		}
		for (int ni = 0; ni < npart.size(); ++ni)
		{
			idx_t n = npart[ni];

			if (n < numParts && n >= 0)
			{
				clusterNodeCounter[n] += 1;
				maxNodeCount = std::max(maxNodeCount, clusterNodeCounter[n]);
			}
			else
			{
				//TODO: figure out why n can be negative here
				//__debugbreak();
			}
		}

		// If we hit max tri/vert count we need to run again but with more partitions
		if (maxElemCount < maxTriangles && maxNodeCount < maxVertices)
			done = true;
		else
			numParts += std::max(1, numParts / 10);
	}

	uint32_t clusterOffset = outputClustersIndices.size();
	outputClustersIndices.resize(clusterOffset + numParts);
	for (int ei = 0; ei < epart.size(); ++ei)
	{
		idx_t clusterIndex = clusterOffset + epart[ei];
		idx_t indexOffset = eptr[ei];
		outputClustersIndices[clusterIndex].push_back(inputIndices[indexOffset + 0]);
		outputClustersIndices[clusterIndex].push_back(inputIndices[indexOffset + 2]);
		outputClustersIndices[clusterIndex].push_back(inputIndices[indexOffset + 1]);
	}
}

void Generate(const char* filename, const char* filenameBin, int outputLod)
{
	HMODULE metisDll = LoadLibrary(L"metis.dll");
	METIS_PartMeshDual = (METIS_PartMeshDual_func)GetProcAddress(metisDll, "METIS_PartMeshDual");

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

			// Generate interleaved vertex buffer for processing
			std::vector<CpuVertex> temp_vertices(positions_accessor->count);
			memset(temp_vertices.data(), 0, sizeof(CpuVertex) * temp_vertices.size());
			for (int i = 0; i < positions_accessor->count; ++i)
			{
				int o2 = 2 * i;
				int o3 = 3 * i;
				int o4 = 4 * i;
				float3 pos = float3{ position_ptr[o3], position_ptr[o3 + 1], position_ptr[o3 + 2] };

				float3 normal = float3{ 0.0f, 0.0f, 0.0f };
				if (normal_ptr)
					normal = float3{ normal_ptr[o3], normal_ptr[o3 + 1], normal_ptr[o3 + 2] };

				float4 tangent = float4{ 0.0f, 0.0f, 0.0f, 0.0f };
				if (tangent_ptr)
					tangent = float4{ tangent_ptr[o4], tangent_ptr[o4 + 1], tangent_ptr[o4 + 2], tangent_ptr[o4 + 3] };

				float2 texcoord = float2{ 0.0f, 0.0f };
				if (texcoord_ptr)
					texcoord = float2{ texcoord_ptr[o2], texcoord_ptr[o2 + 1] };

				temp_vertices[i] = CpuVertex{ pos, normal, tangent, texcoord };
			}

			// Start mesh optimization
			MeshletGeneratorContext context;
			if (0)
			{
				size_t index_count = temp_indices.size();
				std::vector<unsigned int> remap(index_count);
				size_t vertex_count = meshopt_generateVertexRemap(remap.data(), temp_indices.data(), index_count, temp_vertices.data(), temp_vertices.size(), sizeof(CpuVertex));

				context.vertices.resize(vertex_count);
				context.indices.resize(index_count);
				meshopt_remapIndexBuffer(context.indices.data(), temp_indices.data(), index_count, remap.data());
				meshopt_remapVertexBuffer(context.vertices.data(), temp_vertices.data(), temp_vertices.size(), sizeof(CpuVertex), remap.data());
				meshopt_optimizeVertexCache(context.indices.data(), context.indices.data(), index_count, vertex_count);
				meshopt_optimizeOverdraw(context.indices.data(), context.indices.data(), index_count, (float*)context.vertices.data(), vertex_count, sizeof(CpuVertex), 1.05f);
				meshopt_optimizeVertexFetch(context.vertices.data(), context.indices.data(), index_count, context.vertices.data(), vertex_count, sizeof(CpuVertex));
			}
			else
			{
				context.vertices = temp_vertices;
				context.indices = temp_indices;
			}

			// Start clustering
			const size_t maxVertices = 64;
			const size_t maxTriangles = 124;

			{
				context.lods.reserve(16); // Just in case.
				MeshletLodLevel& lod0 = context.lods.emplace_back();
				GenerateClusters(lod0.clustersIndices, context.indices, maxTriangles, maxVertices);
				lod0.edgeSets.resize(lod0.clustersIndices.size());
			}


			bool done = false;
			int ilod = 0;
			while (!done && ilod < outputLod)
			{
				auto& prevLod = context.lods.at(ilod);
				ilod += 1;
				assert(ilod < 16); // We can only handle so many steps for now

				// Loop over all meshlets to figure out their external edges
				for (int ml = 0; ml < prevLod.clustersIndices.size(); ++ml)
				{
					auto& indices = prevLod.clustersIndices[ml];

					// First loop over all triangles to count the nuber of times each edge occurs
					std::unordered_map<uint64_t, int> edgeCounterMap;
					for (int t = 0; t < indices.size() / 3; ++t)
					{
						int o = 3 * t;
						int i0 = indices[o + 0];
						int i1 = indices[o + 2];
						int i2 = indices[o + 1];

						uint64_t pe0 = PackEdge(i0, i1);
						uint64_t pe1 = PackEdge(i0, i2);
						uint64_t pe2 = PackEdge(i1, i2);

						auto iter0 = edgeCounterMap.find(pe0);
						if (iter0 != edgeCounterMap.end())
							iter0->second += 1;
						else
							edgeCounterMap[pe0] = 1;

						auto iter1 = edgeCounterMap.find(pe1);
						if (iter1 != edgeCounterMap.end())
							iter1->second += 1;
						else
							edgeCounterMap[pe1] = 1;

						auto iter2 = edgeCounterMap.find(pe2);
						if (iter2 != edgeCounterMap.end())
							iter2->second += 1;
						else
							edgeCounterMap[pe2] = 1;
					}

					// Then loop over all found edges to get the ones that have occured only once
					// This is the external edges of the clusters
					auto& meshletEdgeSet = prevLod.edgeSets[ml];
					for (auto iter : edgeCounterMap)
					{
						if (iter.second == 1)
							meshletEdgeSet.insert(iter.first);
					}
				}

				// Loop over all meshlets to figure out which ones are connected
				std::unordered_map<uint64_t, int> clusterAdjacencyMap; // Maps the pair <c0, c1> to common edge count
				std::vector<std::pair<int, int>> clusterAdjacencyCount(prevLod.clustersIndices.size()); // Contains the pair <cluster, counter> counting how many neighbours a cluster have
				for (int ml = 0; ml < clusterAdjacencyCount.size(); ++ml)
				{
					clusterAdjacencyCount[ml].first = ml;
					clusterAdjacencyCount[ml].second = 0;
				}
				for (int ml = 0; ml < prevLod.clustersIndices.size(); ++ml)
				{
					auto& edgeSet = prevLod.edgeSets[ml];

					for (int ml_inner = ml + 1; ml_inner < prevLod.clustersIndices.size(); ++ml_inner)
					{
						// For each edge test it in the other clusters set
						auto& edgeSetInner = prevLod.edgeSets[ml_inner];
						int count = 0;
						for (uint64_t edge : edgeSetInner)
						{
							if (edgeSet.contains(edge))
								count += 1;
						}

						// If we have any edges we can go ahead and add it to the map
						// Also store the count so we know how "strong" this connection is
						if (count > 0)
						{
							clusterAdjacencyMap[PackCluster(ml, ml_inner)] = count;
							clusterAdjacencyCount[ml].second += 1;
							clusterAdjacencyCount[ml_inner].second += 1;
						}
					}
				}

				// Sort the clusters by connection count
				// Here we should probably have a triangle size metric as well
				std::vector<std::pair<int, int>> sortedAdjacency(clusterAdjacencyCount.size());
				std::partial_sort_copy(clusterAdjacencyCount.begin(), clusterAdjacencyCount.end(), sortedAdjacency.begin(), sortedAdjacency.end(), [](std::pair<int, int> l, std::pair<int, int> r) { return l.second > r.second; });

				std::vector<MergeCandidate> mergeLists;
				while (sortedAdjacency.size() > 0)
				{
					// Create a new candidate and populate it with the least connected cluster
					MergeCandidate candidate;
					candidate.Push(sortedAdjacency.back().first);

					// Get the best possible scoring merge candidate using this specific candidate starting point
					MergeCandidate output = SelectBestCandidateTree(candidate, sortedAdjacency, clusterAdjacencyMap);
					mergeLists.push_back(output);

					// Remove all the selected clusters from future searches
					std::erase_if(sortedAdjacency, [output](const auto& v) {
						for (int c = 0; c < output.count; ++c)
							if (v.first == output.set[c])
								return true;
						return false;
					});
				}

				if (mergeLists.size() < 10) // TODO: end heuristic
					break;

				MeshletLodLevel& currLod = context.lods.emplace_back();

				// We now have all the clusters to merge. Process them group by group
				for (auto& l : mergeLists)
				{
					// Generate a new index list from the selected clusters, generating a merged mesh
					std::vector<unsigned int> mergedIndices;
					for(int c = 0; c < l.count; ++c)
					{
						int ml = l.set[c];
						auto& indices = prevLod.clustersIndices[ml];

						for (int i = 0; i < indices.size(); ++i)
						{
							int idx = indices[i];
							mergedIndices.push_back(idx);
						}
					}

					// Simplify the merged mesh
					float threshold = 0.5f; // TODO: pick to get down to half the tris and half the clusters
					size_t targetIndexCount = size_t(mergedIndices.size() * threshold);
					float targetError = 1e-2f;
					unsigned int options = meshopt_SimplifyLockBorder;

					std::vector<unsigned int> simplifiedIndices(mergedIndices.size());
					float lod_error = 0.f;
					simplifiedIndices.resize(meshopt_simplify(simplifiedIndices.data(),
						mergedIndices.data(),
						mergedIndices.size(),
						(float*)context.vertices.data(),
						context.vertices.size(), 
						sizeof(CpuVertex),
						targetIndexCount,
						targetError,
						options,
						&lod_error));

					GenerateClusters(currLod.clustersIndices, mergedIndices, maxTriangles, maxVertices);
				}
				currLod.edgeSets.resize(currLod.clustersIndices.size());
			}

			{
				MeshletLodLevel& lod = context.lods.at(std::min(outputLod, (int)context.lods.size() - 1));
				for (int i = 0; i < lod.clustersIndices.size(); ++i)
				{
					auto& indices = lod.clustersIndices[i];
					size_t indexCount = indices.size();
					std::vector<unsigned int> remap(context.vertices.size());

					size_t vertexCount = meshopt_generateVertexRemap(remap.data(), indices.data(), indexCount, context.vertices.data(), context.vertices.size(), sizeof(CpuVertex));

					std::vector<CpuVertex> clusterVertices(vertexCount);
					std::vector<unsigned int> clusterIndices(indexCount);

					meshopt_remapIndexBuffer(clusterIndices.data(), (unsigned int*)indices.data(), indexCount, remap.data());
					meshopt_remapVertexBuffer(clusterVertices.data(), context.vertices.data(), context.vertices.size(), sizeof(CpuVertex), remap.data());

					UINT outputVerticesOffset = out_positions.size();
					MinMaxAABB clusterBounds = MinMaxAABB{
						float3 {FLT_MAX, FLT_MAX, FLT_MAX},
						float3 {FLT_MIN, FLT_MIN, FLT_MIN},
					};

					for (int v = 0; v < vertexCount; ++v)
					{
						CpuVertex& vert = clusterVertices[v];
						out_positions.push_back(vert.pos);
						out_normals.push_back(vert.normal);
						out_tangents.push_back(vert.tangent);
						out_texcoords.push_back(vert.texcoord);

						clusterBounds.Min = min(clusterBounds.Min, vert.pos);
						clusterBounds.Max = max(clusterBounds.Max, vert.pos);
					}

					UINT outputTriangleOffset = out_indices.size() / 3;
					UINT triangleCount = indexCount / 3;
					for (int i = 0; i < indexCount; ++i)
					{
						out_indices.push_back(clusterIndices[i]);
					}

					out_clusters.push_back(Cluster{
						outputTriangleOffset,
						(UINT)indexCount / 3,
						outputVerticesOffset,
						(UINT)vertexCount,
						MinMaxToCenterExtents(clusterBounds),
						});

					meshBounds.Min = min(meshBounds.Min, clusterBounds.Min);
					meshBounds.Max = max(meshBounds.Max, clusterBounds.Max);
				}
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
