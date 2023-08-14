#include "ShaderCommon.hlsl"

//#define DEBUG_VISIBILITY_BUFFER
//#define DEBUG_DEPTH_BUFFER

float3 Barycentric(float3 p, float3 a, float3 b, float3 c)
{
	float3 v0 = b - a;
	float3 v1 = c - a;
	float3 v2 = p - a;

	float d00 = dot(v0, v0);
	float d01 = dot(v0, v1);
	float d11 = dot(v1, v1);
	float d20 = dot(v2, v0);
	float d21 = dot(v2, v1);
	float invDet = 1.0f / (d00 * d11 - d01 * d01);
	
	float v = (d11 * d20 - d01 * d21) * invDet;
	float w = (d00 * d21 - d01 * d20) * invDet;
	float u = 1.0f - v - w;

	return float3(u, v, w);
}

float3 ReprojectDepth(float depth, float2 uv) {
	float4 ndc = float4(uv * 2.0f - 1.0f, depth, 1.0f);
	ndc.y *= -1.0f;
	float4 wp = mul(constants.InverseProjectionMatrix, ndc);
	return wp.xyz / wp.w;
}

float3 TransformVertex(float3 v, float4x4 ModelMatrix, float4x4 ViewMatrix)
{
	float4 wv = mul(ModelMatrix, float4(v, 1.0f));
	wv = mul(ViewMatrix, wv);
	return wv.xyz / wv.w;
}

[numthreads(8, 8, 1)]
void main(uint2 dtid : SV_DispatchThreadID)
{
	ByteAddressBuffer visibleInstances = ResourceDescriptorHeap[VISIBLE_INSTANCES_SRV];
	ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[VISIBLE_CLUSTERS_SRV];
	Texture2D<uint> vBuffer = ResourceDescriptorHeap[VBUFFER_SRV];
	Texture2D<float> depthBuffer = ResourceDescriptorHeap[DEPTHBUFFER_SRV];
	RWTexture2D<float4> colorBuffer = ResourceDescriptorHeap[COLORBUFFER_UAV];

#if defined(DEBUG_VISIBILITY_BUFFER)
	uint v = vBuffer[dtid];
	uint primitiveIndex = v & 0x000000FF;
	uint visibleClusterIndex = v >> 8;
	colorBuffer[dtid] = float4(primitiveIndex / 123.0f, 0.0f, 0.0f, 1.0f);

#elif defined(DEBUG_DEPTH_BUFFER)
	float d = depthBuffer[dtid];

	float2 uv = dtid * float2(1.0f / 1280.0f, 1.0f / 720.0f);
	float3 wp = ReprojectDepth(d, uv);

	float3 a = abs(wp) - abs(floor(wp));
	float3 col = float3(
		a.x < 0.01f ? 1.0f : 0.0f,
		a.y < 0.01f ? 1.0f : 0.0f,
		a.z < 0.01f ? 1.0f : 0.0f);

	colorBuffer[dtid] = float4(a, 1.0f);

#else
	float d = depthBuffer[dtid];
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

	Instance instance = GetInstance(instanceIndex);
	Cluster cluster = GetCluster(clusterIndex);
	uint3 tri = GetTri(cluster.PrimitiveStart + primitiveIndex);
	float3 v0 = GetPosition(cluster.VertexStart + tri.x);
	float3 v1 = GetPosition(cluster.VertexStart + tri.y);
	float3 v2 = GetPosition(cluster.VertexStart + tri.z);

	float2 uv = dtid * float2(1.0f / 1280.0f, 1.0f / 720.0f);
	float3 vp = ReprojectDepth(d, uv);

	float3 vv0 = TransformVertex(v0, instance.ModelMatrix, constants.ViewMatrix);
	float3 vv1 = TransformVertex(v1, instance.ModelMatrix, constants.ViewMatrix);
	float3 vv2 = TransformVertex(v2, instance.ModelMatrix, constants.ViewMatrix);

	float3 b = Barycentric(vp, vv0, vv1, vv2);

	float3 n0 = GetNormal(cluster.VertexStart + tri.x);
	float3 n1 = GetNormal(cluster.VertexStart + tri.y);
	float3 n2 = GetNormal(cluster.VertexStart + tri.z);

	float3 n = n0 * b.x + n1 * b.y + n2 * b.z;

	float3 l = normalize(float3(10.0f, 10.0f, 10.0f));

	float a = 0.5f + dot(n, l) * 0.5;

    Material material = GetMaterial(instance.MaterialIndex);

	colorBuffer[dtid] = material.Color * a;
#endif
}
