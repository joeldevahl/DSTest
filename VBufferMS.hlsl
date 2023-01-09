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
    out indices uint3 tri[64],
    out primitives PrimitiveAttributes prims[64],
    out vertices VertexAttributes verts[124]
)
{
    uint id = gid + dispatchParameters.offset;

	ByteAddressBuffer visibleClustersCounter = ResourceDescriptorHeap[VISIBLE_CLUSTERS_COUNTER_UAV];
    if (id >= visibleClustersCounter.Load(0))
        return;

	ByteAddressBuffer visibleInstances = ResourceDescriptorHeap[VISIBLE_INSTANCES_SRV];
	ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[VISIBLE_CLUSTERS_SRV];
    uint packedClusterInstance = visibleClusters.Load(id * 4);
    uint clusterIndex = packedClusterInstance & 0x0000ffff;
    uint visibleInstanceIndex = packedClusterInstance >> 16;
    uint instanceIndex = visibleInstances.Load(visibleInstanceIndex * 4);

    Cluster cluster = GetCluster(clusterIndex);
    Instance instance = GetInstance(instanceIndex);

    uint primitiveCount = cluster.IndexCount / 3; // TODO: precalc?
    SetMeshOutputCounts(cluster.VertexCount, primitiveCount);

    if (gtid < primitiveCount)
    {
        tri[gtid] = GetTri(cluster.IndexStart + gtid * 3);
        prims[gtid].PackedOutput = (id << 8) | (gtid & 0x000000FF);
    }

    if (gtid < cluster.VertexCount)
    {
        float3 vert = GetVertex(cluster.VertexStart + gtid);

        vert.xyz += instance.Position;

        verts[gtid].Position = mul(float4(vert, 1.0), constants.MVP);
    }
}
