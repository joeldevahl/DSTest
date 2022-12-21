#include "ShaderCommon.hlsl"

[numthreads(128, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	if (dtid.x >= constants.Counts.x)
		return;

	ByteAddressBuffer visibleInstances = ResourceDescriptorHeap[VISIBLE_INSTANCES_SRV];
	RWByteAddressBuffer visibleClusters = ResourceDescriptorHeap[VISIBLE_CLUSTERS_UAV];

	uint instanceIndex = visibleInstances.Load(dtid.x * 4);

	Instance instance = GetInstance(instanceIndex);
	Mesh mesh = GetMesh(instance.MeshIndex);

	RWByteAddressBuffer visibleClustersCounter = ResourceDescriptorHeap[VISIBLE_CLUSTERS_COUNTER_UAV];
	uint offset = 0;
	visibleClustersCounter.InterlockedAdd(0, mesh.ClusterCount, offset); // TODO: restructure this dispatch to do one instance per wave

	for (int i = 0; i < mesh.ClusterCount; ++i)
	{
		uint val = ((mesh.ClusterStart + i) & 0x0000ffff) | (instanceIndex << 16);

		visibleClusters.Store((offset + i) * 4, val);
	}
}