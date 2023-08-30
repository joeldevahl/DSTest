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

CenterExtentsAABB TransformAABB(CenterExtentsAABB aabb, float4x4 mat)
{
	CenterExtentsAABB res;
	res.Center = mul(mat, aabb.Center);

	float3x3 absmat = float3x3(
		abs(mat._m00), abs(mat._m01), abs(mat._m02),
		abs(mat._m10), abs(mat._m11), abs(mat._m12),
		abs(mat._m20), abs(mat._m21), abs(mat._m22)
	);

	res.Extents = mul(absmat, aabb.Extents);

	return res;
}

bool IsBoxOutsidePlane(CenterExtentsAABB aabb, float4 p)
{
	float d = dot(p.xyz, aabb.Center);
	float r = dot(abs(p.xyz), aabb.Extents);
    return d + r < -p.w;
}

bool IsCulled(CenterExtentsAABB aabb)
{
	bool t0 = IsBoxOutsidePlane(aabb, constants.CullingCamera.FrustumPlanes[0]);
	bool t1 = IsBoxOutsidePlane(aabb, constants.CullingCamera.FrustumPlanes[1]);
	bool t2 = IsBoxOutsidePlane(aabb, constants.CullingCamera.FrustumPlanes[2]);
	bool t3 = IsBoxOutsidePlane(aabb, constants.CullingCamera.FrustumPlanes[3]);
	bool t4 = IsBoxOutsidePlane(aabb, constants.CullingCamera.FrustumPlanes[4]);
	bool t5 = IsBoxOutsidePlane(aabb, constants.CullingCamera.FrustumPlanes[5]);

	return t0 | t1 | t2 | t3 | t4 | t5;
}
