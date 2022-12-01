#include "ShaderCommon.hlsl"

[numthreads(8, 8, 1)]
void main(uint2 dtid : SV_DispatchThreadID)
{
	ByteAddressBuffer visibleInstances = ResourceDescriptorHeap[10];
	ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[11];
	Texture2D<uint> vBuffer = ResourceDescriptorHeap[12];
	RWTexture2D<float4> colorBuffer = ResourceDescriptorHeap[13];

	uint v = vBuffer[dtid];
	uint primitiveIndex = v & 0x000000FF;
	uint visibleClusterIndex = v >> 8;

	uint c = visibleClusters.Load(visibleClusterIndex * 4);
    uint clusterIndex = c & 0x0000ffff;
    uint visibleInstanceIndex = c >> 16;

    uint instanceIndex = visibleInstances.Load(visibleInstanceIndex * 4);
    Instance instance = GetInstance(instanceIndex);
    Material material = GetMaterial(instance.MaterialIndex);

	// TODO: use stencil or depth as mask
	if (v != 0)
		colorBuffer[dtid] = material.Color;
	else
		colorBuffer[dtid] = float4(0.0f, 0.2f, 0.4f, 1.0f);

}
