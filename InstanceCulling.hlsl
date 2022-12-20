#include "ShaderCommon.hlsl"

[numthreads(128, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	if (dtid.x >= constants.Counts.x)
		return;

	RWByteAddressBuffer visibleInstances = ResourceDescriptorHeap[VISIBLE_INSTANCES_UAV];
	visibleInstances.Store(dtid.x * 4, dtid.x);
}