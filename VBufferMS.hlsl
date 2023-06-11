#include "ShaderCommon.hlsl"

struct PrimitiveAttributes
{
    uint PackedOutput : COLOR0;
};

struct VertexAttributes
{
    float4 Position : SV_Position;
};

[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void main(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    out indices uint3 tri[124],
    out primitives PrimitiveAttributes prims[124],
    out vertices VertexAttributes verts[64]
)
{
	ByteAddressBuffer visibleInstances = ResourceDescriptorHeap[VISIBLE_INSTANCES_SRV];
	ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[VISIBLE_CLUSTERS_SRV];
    uint packedClusterInstance = visibleClusters.Load(gid * 4);
    uint clusterIndex = packedClusterInstance & 0x0000ffff;
    uint visibleInstanceIndex = packedClusterInstance >> 16;
    uint instanceIndex = visibleInstances.Load(visibleInstanceIndex * 4);

    Cluster cluster = GetCluster(clusterIndex);
    Instance instance = GetInstance(instanceIndex);

    SetMeshOutputCounts(cluster.VertexCount, cluster.PrimitiveCount);
 
	if (gtid < cluster.PrimitiveCount)
	{
		tri[gtid] = GetTri(cluster.PrimitiveStart + gtid);
		prims[gtid].PackedOutput = (gid << 8) | (gtid & 0x000000FF);
	}
    
	if (gtid < cluster.VertexCount)
	{
		float3 vert = GetPosition(cluster.VertexStart + gtid);

		float4 transformedVert = mul(float4(vert, 1.0), instance.ModelMatrix);

		verts[gtid].Position = mul(transformedVert, constants.ViewProjectionMatrix);
	}
}
