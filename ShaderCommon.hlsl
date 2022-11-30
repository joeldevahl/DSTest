struct Payload
{
    float3 offset;
	uint mesh_id;
	uint material_id;
};

struct Constants
{
    float4x4 mvp;
    uint4 counts;
    uint num_meshes;
    uint num_materials;
};

struct Vertex
{
    float3 position;
};

struct Instance
{
    float3 position;
    uint mesh_index;
};

struct Mesh
{
    uint cluster_start;
    uint cluster_count;
};

struct Cluster
{
    uint index_start;
    uint index_count;
};

struct Material
{
    uint texture_id;
};

ConstantBuffer<Constants> constants : register(b0);

StructuredBuffer<Instance> GetInstanceBuffer() { return ResourceDescriptorHeap[0]; }
StructuredBuffer<Mesh> GetMeshBuffer() { return ResourceDescriptorHeap[1]; }
StructuredBuffer<Cluster> GetClusterBuffer() { return ResourceDescriptorHeap[2]; }
ByteAddressBuffer GetVertexDataBuffer() { return ResourceDescriptorHeap[3]; }
ByteAddressBuffer GetIndexDataBuffer() { return ResourceDescriptorHeap[4]; }

Instance GetInstance(uint idx) { return GetInstanceBuffer()[idx]; }
Mesh GetMesh(uint idx) { return GetMeshBuffer()[idx]; }
Cluster GetCluster(uint idx) { return GetClusterBuffer()[idx]; }
float4 GetVertex(uint idx) { return asfloat(GetVertexDataBuffer().Load4(idx * 16)); }
uint GetIndex(uint idx) { return GetIndexDataBuffer().Load(idx * 4); } // TODO: 8 bit indices
uint3 GetTri(uint idx)
{
    ByteAddressBuffer buffer = GetIndexDataBuffer();
    return buffer.Load3(idx * 12);
}
