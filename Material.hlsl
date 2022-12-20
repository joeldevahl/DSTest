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
    Instance instance = GetInstance(instanceIndex);
    Material material = GetMaterial(instance.MaterialIndex);

	colorBuffer[dtid] = material.Color;
}
