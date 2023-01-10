#include "ShaderCommon.hlsl"

[numthreads(128, 1, 1)]
void main(uint dtid : SV_DispatchThreadID)
{
	if (dtid >= constants.Counts.x)
		return;

	RWByteAddressBuffer visibleInstancesCounter = ResourceDescriptorHeap[VISIBLE_INSTANCES_COUNTER_UAV];
	uint offset = 0;
	visibleInstancesCounter.InterlockedAdd(0, 1, offset); // TODO: restructure this dispatch to do one instance per wave

	RWByteAddressBuffer visibleInstances = ResourceDescriptorHeap[VISIBLE_INSTANCES_UAV];
	visibleInstances.Store(offset * 4, dtid);
}