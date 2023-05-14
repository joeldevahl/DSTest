#include "ShaderCommon.hlsl"

[numthreads(128, 1, 1)]
void main(uint dtid : SV_DispatchThreadID)
{
	if (dtid == 0)
	{
		RWByteAddressBuffer visibleInstancesCounter = ResourceDescriptorHeap[VISIBLE_INSTANCES_COUNTER_UAV];
		visibleInstancesCounter.Store(0, 0);

		RWByteAddressBuffer visibleClustersCounter = ResourceDescriptorHeap[VISIBLE_CLUSTERS_COUNTER_UAV];
		visibleClustersCounter.Store(0, 0);
		visibleClustersCounter.Store(4, 1);
		visibleClustersCounter.Store(8, 1);
	}
}