#include "ShaderCommon.hlsl"

[numthreads(128, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	if (dtid.x >= constants.Counts.x)
		return;

	ByteAddressBuffer visibleInstances = ResourceDescriptorHeap[10];
	RWByteAddressBuffer visibleClusters = ResourceDescriptorHeap[11];

	uint instanceIndex = visibleInstances.Load(dtid.x * 4);

	Instance instance = GetInstance(instanceIndex);
	Mesh mesh = GetMesh(instance.MeshIndex);

	// TODO: materials and multiple clusters and what not
	visibleClusters.Store(dtid.x * 4, mesh.ClusterStart); // TODO: pack instance as well
}