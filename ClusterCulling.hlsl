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

	// TODO: multiple clusters and what not
	uint val = (mesh.ClusterStart & 0x0000ffff) | (instanceIndex << 16);
	visibleClusters.Store(dtid.x * 4, val);
}