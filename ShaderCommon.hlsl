struct Payload
{
    float3 Position;
	uint MeshIndex;
	uint MaterialIndex;
};

#define CB_ALIGN
#include "Common.h"

ConstantBuffer<Constants> constants : register(b0);

StructuredBuffer<Instance> GetInstanceBuffer() { return ResourceDescriptorHeap[0]; }
StructuredBuffer<Mesh> GetMeshBuffer() { return ResourceDescriptorHeap[1]; }
StructuredBuffer<Cluster> GetClusterBuffer() { return ResourceDescriptorHeap[2]; }
ByteAddressBuffer GetVertexDataBuffer() { return ResourceDescriptorHeap[3]; }
ByteAddressBuffer GetIndexDataBuffer() { return ResourceDescriptorHeap[4]; }
StructuredBuffer<Material> GetMaterialBuffer() { return ResourceDescriptorHeap[5]; }

Instance GetInstance(uint idx) { return GetInstanceBuffer()[idx]; }
Mesh GetMesh(uint idx) { return GetMeshBuffer()[idx]; }
Cluster GetCluster(uint idx) { return GetClusterBuffer()[idx]; }
float4 GetVertex(uint idx) { return asfloat(GetVertexDataBuffer().Load4(idx * 16)); }
uint GetIndex(uint idx) { return GetIndexDataBuffer().Load(idx * 4); } // TODO: 8 bit indices
uint3 GetTri(uint idx) { return GetIndexDataBuffer().Load3(idx * 3); }
Material GetMaterial(uint idx) { return GetMaterialBuffer()[idx]; }
