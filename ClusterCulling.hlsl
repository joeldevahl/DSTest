#include "ShaderCommon.hlsl"

[numthreads(128, 1, 1)]
void main(uint dtid : SV_DispatchThreadID)
{
	RWByteAddressBuffer visibleInstancesCounter = ResourceDescriptorHeap[VISIBLE_INSTANCES_COUNTER_UAV];
	if (dtid >= min(visibleInstancesCounter.Load(0), MAX_VISIBLE_INSTANCES))
		return;

	ByteAddressBuffer visibleInstances = ResourceDescriptorHeap[VISIBLE_INSTANCES_SRV];
	RWByteAddressBuffer visibleClusters = ResourceDescriptorHeap[VISIBLE_CLUSTERS_UAV];

	uint instanceIndex = visibleInstances.Load(dtid * 4);

	Instance instance = GetInstance(instanceIndex);
	Mesh mesh = GetMesh(instance.MeshIndex);

	RWByteAddressBuffer visibleClustersCounter = ResourceDescriptorHeap[VISIBLE_CLUSTERS_COUNTER_UAV];
	for (int i = 0; i < mesh.ClusterCount; ++i)
	{
		Cluster cluster = GetCluster(mesh.ClusterStart + i);

		CenterExtentsAABB box = TransformAABB(cluster.Box, instance.ModelMatrix);
		if (IsCulled(box))
			continue;

		uint offset = 0;
		visibleClustersCounter.InterlockedAdd(0, 1, offset); // TODO: restructure this dispatch to do one instance per wave

		if (offset < MAX_VISIBLE_CLUSTERS)
		{
			uint val = ((mesh.ClusterStart + i) & 0x0000ffff) | (dtid << 16);
			visibleClusters.Store(offset * 4, val);
		}
	}
}