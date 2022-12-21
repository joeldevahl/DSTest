#include "ShaderCommon.hlsl"

[numthreads(128, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	if (dtid.x >= constants.Counts.x)
		return;

	// TODO: remove this hack and clear counter another way
	if (dtid.x == 0)
	{
		RWByteAddressBuffer visibleClustersCounter = ResourceDescriptorHeap[VISIBLE_CLUSTERS_COUNTER_UAV];
		visibleClustersCounter.Store(0, 0);
	}

	RWByteAddressBuffer visibleInstances = ResourceDescriptorHeap[VISIBLE_INSTANCES_UAV];
	visibleInstances.Store(dtid.x * 4, dtid.x);
}