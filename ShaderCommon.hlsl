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
ByteAddressBuffer GetPositionDataBuffer() { return ResourceDescriptorHeap[POSITION_DATA_BUFFER_SRV]; }
ByteAddressBuffer GetNormalDataBuffer() { return ResourceDescriptorHeap[NORMAL_DATA_BUFFER_SRV]; }
ByteAddressBuffer GetTangentDataBuffer() { return ResourceDescriptorHeap[TANGENT_DATA_BUFFER_SRV]; }
ByteAddressBuffer GetTexcoordDataBuffer() { return ResourceDescriptorHeap[TEXCOORD_DATA_BUFFER_SRV]; }
ByteAddressBuffer GetIndexDataBuffer() { return ResourceDescriptorHeap[INDEX_DATA_BUFFER_SRV]; }
StructuredBuffer<Material> GetMaterialBuffer() { return ResourceDescriptorHeap[MATERIAL_BUFFER_SRV]; }

Instance GetInstance(uint idx) { return GetInstanceBuffer()[idx]; }
Mesh GetMesh(uint idx) { return GetMeshBuffer()[idx]; }
Cluster GetCluster(uint idx) { return GetClusterBuffer()[idx]; }
float3 GetPosition(uint idx) { return asfloat(GetPositionDataBuffer().Load3(idx * 12)); }
float3 GetNormal(uint idx) { return asfloat(GetNormalDataBuffer().Load3(idx * 12)); }
float4 GetTangent(uint idx) { return asfloat(GetTangentDataBuffer().Load4(idx * 16)); }
float2 GetTexcoord(uint idx) { return asfloat(GetTexcoordDataBuffer().Load2(idx * 8)); }
uint3 GetTri(uint idx) { return GetIndexDataBuffer().Load3(idx * 12); }
Material GetMaterial(uint idx) { return GetMaterialBuffer()[idx]; }
