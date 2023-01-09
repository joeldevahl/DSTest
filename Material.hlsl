#include "ShaderCommon.hlsl"

[numthreads(8, 8, 1)]
void main(uint2 dtid : SV_DispatchThreadID)
{
	ByteAddressBuffer visibleInstances = ResourceDescriptorHeap[VISIBLE_INSTANCES_SRV];
	ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[VISIBLE_CLUSTERS_SRV];
	Texture2D<uint> vBuffer = ResourceDescriptorHeap[VBUFFER_SRV];
	Texture2D<float> depthBuffer = ResourceDescriptorHeap[DEPTHBUFFER_SRV];
	RWTexture2D<float4> colorBuffer = ResourceDescriptorHeap[COLORBUFFER_UAV];

	uint d = depthBuffer[dtid];
	if (d >= 1.0f)
	{
		colorBuffer[dtid] = float4(0.0f, 0.2f, 0.4f, 1.0f);
		return;
	}

	uint v = vBuffer[dtid];
	uint primitiveIndex = v & 0x000000FF;
	uint visibleClusterIndex = v >> 8;

	uint c = visibleClusters.Load(visibleClusterIndex * 4);
    uint clusterIndex = c & 0x0000ffff;
    uint visibleInstanceIndex = c >> 16;

    uint instanceIndex = visibleInstances.Load(visibleInstanceIndex * 4);

	Cluster cluster = GetCluster(clusterIndex);
	uint3 tri = GetTri(cluster.IndexStart + primitiveIndex * 3);
	float3 v0 = GetVertex(cluster.VertexStart + tri.x);
	float3 v1 = GetVertex(cluster.VertexStart + tri.y);
	float3 v2 = GetVertex(cluster.VertexStart + tri.z);

	float3 n = normalize(cross(v1 - v0, v2 - v0));

	float3 l = normalize(float3(10.0f, 10.0f, 0.0f) - v0);


	float a = 0.5f + dot(n, l) * 0.5;

    Instance instance = GetInstance(instanceIndex);
    Material material = GetMaterial(instance.MaterialIndex);

	colorBuffer[dtid] = material.Color * a;
}
