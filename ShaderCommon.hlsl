struct Payload
{
    float3 Position;
	uint MeshIndex;
	uint MaterialIndex;
};

#define CB_ALIGN
#include "Common.h"

ConstantBuffer<Constants> constants : register(b0);

StructuredBuffer<Instance> GetInstanceBuffer() { return ResourceDescriptorHeap[INSTANCE_BUFFER_SRV]; }
StructuredBuffer<Mesh> GetMeshBuffer() { return ResourceDescriptorHeap[MESH_BUFFER_SRV]; }
StructuredBuffer<Cluster> GetClusterBuffer() { return ResourceDescriptorHeap[CLUSTER_BUFFER_SRV]; }
ByteAddressBuffer GetVertexDataBuffer() { return ResourceDescriptorHeap[VERTEX_DATA_BUFFER_SRV]; }
ByteAddressBuffer GetIndexDataBuffer() { return ResourceDescriptorHeap[INDEX_DATA_BUFFER_SRV]; }
StructuredBuffer<Material> GetMaterialBuffer() { return ResourceDescriptorHeap[MATERIAL_BUFFER_SRV]; }

Instance GetInstance(uint idx) { return GetInstanceBuffer()[idx]; }
Mesh GetMesh(uint idx) { return GetMeshBuffer()[idx]; }
Cluster GetCluster(uint idx) { return GetClusterBuffer()[idx]; }
float4 GetVertex(uint idx) { return asfloat(GetVertexDataBuffer().Load4(idx * 16)); }
uint GetIndex(uint idx) { return GetIndexDataBuffer().Load(idx * 4); } // TODO: 8 bit indices
uint3 GetTri(uint idx) { return GetIndexDataBuffer().Load3(idx * 3); }
Material GetMaterial(uint idx) { return GetMaterialBuffer()[idx]; }
