#include "ShaderCommon.hlsl"

#define Pi 3.14159265359f

float3 Fresnel(in float3 specAlbedo, in float3 h, in float3 l)
{
	float3 fresnel = specAlbedo + (1.0f - specAlbedo) * pow((1.0f - saturate(dot(l, h))), 5.0f);

	// Fade out spec entirely when lower than 0.1% albedo
	fresnel *= saturate(dot(specAlbedo, 333.0f));

	return fresnel;
}

float GGXV1(in float m2, in float nDotX)
{
	return 1.0f / (nDotX + sqrt(m2 + (1 - m2) * nDotX * nDotX));
}

float GGXVisibility(in float m2, in float nDotL, in float nDotV)
{
	return GGXV1(m2, nDotL) * GGXV1(m2, nDotV);
}

float GGXSpecular(in float m, in float3 n, in float3 h, in float3 v, in float3 l)
{
	float nDotH = saturate(dot(n, h));
	float nDotL = saturate(dot(n, l));
	float nDotV = saturate(dot(n, v));

	float nDotH2 = nDotH * nDotH;
	float m2 = m * m;

	// Calculate the distribution term
	float x = nDotH * nDotH * (m2 - 1) + 1;
	float d = m2 / (Pi * x * x);

	// Calculate the matching visibility term
	float vis = GGXVisibility(m2, nDotL, nDotV);

	return d * vis;
}

float3 CalcLighting(in float3 normal, in float3 lightDir, in float3 peakIrradiance,
	in float3 diffuseAlbedo, in float3 specularAlbedo, in float roughness,
	in float3 positionWS, in float3 cameraPosWS, in float3 msEnergyCompensation)
{
	float3 lighting = diffuseAlbedo * (1.0f / 3.14159f);

	float3 view = normalize(cameraPosWS - positionWS);
	const float nDotL = saturate(dot(normal, lightDir));
	if (nDotL > 0.0f)
	{
		const float nDotV = saturate(dot(normal, view));
		float3 h = normalize(view + lightDir);

		float3 fresnel = Fresnel(specularAlbedo, h, lightDir);

		float specular = GGXSpecular(roughness, normal, h, view, lightDir);
		lighting += specular * fresnel * msEnergyCompensation;
	}

	return lighting * nDotL * peakIrradiance;
}

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
	float4 wp = mul(constants.DrawingCamera.InverseProjectionMatrix, ndc);
	return wp.xyz / wp.w;
}

float3 TransformVertex(float3 v, float4x4 ModelMatrix, float4x4 ViewMatrix)
{
	float4 wv = mul(ModelMatrix, float4(v, 1.0f));
	wv = mul(ViewMatrix, wv);
	return wv.xyz / wv.w;
}

uint HashInt(uint x)
{
    x += 1;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

float4 DebugColor(uint input)
{
    uint hash = HashInt(input);
    
    return float4((hash & 127) / 127.0f, (hash & 16383) / 16383.0f, (hash & 2097151) / 2097151.0f, 1.0f);

}

[numthreads(8, 8, 1)]
void main(uint2 dtid : SV_DispatchThreadID)
{
	ByteAddressBuffer visibleInstances = ResourceDescriptorHeap[VISIBLE_INSTANCES_SRV];
	ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[VISIBLE_CLUSTERS_SRV];
	Texture2D<uint> vBuffer = ResourceDescriptorHeap[VBUFFER_SRV];
	Texture2D<float> depthBuffer = ResourceDescriptorHeap[DEPTHBUFFER_SRV];
	RWTexture2D<float4> colorBuffer = ResourceDescriptorHeap[COLORBUFFER_UAV];
	
    if (constants.DebugMode == DEBUG_MODE_SHOW_TRIANGLES)
    {
        float d = depthBuffer[dtid];
        if (d >= 1.0f)
        {
            colorBuffer[dtid] = float4(0.0f, 0.2f, 0.4f, 1.0f);
            return;
        }
        
        uint v = vBuffer[dtid];
        uint primitiveIndex = v & 0x000000FF;
        colorBuffer[dtid] = DebugColor(primitiveIndex);
    }
    else if (constants.DebugMode == DEBUG_MODE_SHOW_CLUSTERS)
    {
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
		
        colorBuffer[dtid] = DebugColor(clusterIndex);
    }
    else if (constants.DebugMode == DEBUG_MODE_SHOW_INSTANCES)
    {
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
		
        colorBuffer[dtid] = DebugColor(instanceIndex);
    }
    else if (constants.DebugMode == DEBUG_MODE_SHOW_MATERIALS)
    {
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
        
        colorBuffer[dtid] = DebugColor(instance.MaterialIndex);
    }
    else if (constants.DebugMode == DEBUG_MODE_SHOW_DEPTH_BUFFER)
    {
        float d = depthBuffer[dtid];

        float2 uv = dtid * float2(1.0f / 1280.0f, 1.0f / 720.0f);
        float3 wp = ReprojectDepth(d, uv);

        float3 a = abs(wp) - abs(floor(wp));
        float3 col = float3(
		a.x < 0.01f ? 1.0f : 0.0f,
		a.y < 0.01f ? 1.0f : 0.0f,
		a.z < 0.01f ? 1.0f : 0.0f);

        colorBuffer[dtid] = float4(a, 1.0f);
    }
	else
    {
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

        float3 vv0 = TransformVertex(v0, instance.ModelMatrix, constants.DrawingCamera.ViewMatrix);
        float3 vv1 = TransformVertex(v1, instance.ModelMatrix, constants.DrawingCamera.ViewMatrix);
        float3 vv2 = TransformVertex(v2, instance.ModelMatrix, constants.DrawingCamera.ViewMatrix);

        float3 b = Barycentric(vp, vv0, vv1, vv2);

        float3 n0 = GetNormal(cluster.VertexStart + tri.x);
        float3 n1 = GetNormal(cluster.VertexStart + tri.y);
        float3 n2 = GetNormal(cluster.VertexStart + tri.z);

        float3 n = n0 * b.x + n1 * b.y + n2 * b.z;
        float3 p = vv0 * b.x + vv1 + b.y + vv2 * b.z;

        float3 l = normalize(float3(10.0f, 10.0f, 10.0f));

        Material material = GetMaterial(instance.MaterialIndex);

        float3 color = CalcLighting(
		n,
		l,
		float3(1.0f, 1.0f, 1.0f),
		material.Color.xyz,
		material.Color.zyz,
		material.Roughness,
		p,
		float3(0.0f, 0.0f, 0.0f),
		float3(1.0f, 1.0f, 1.0f));
        colorBuffer[dtid] = float4(color, 1.0f);
    }
}
