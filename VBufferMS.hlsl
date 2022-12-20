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
    out indices uint3 tri[43],
    out primitives PrimitiveAttributes prims[43],
    out vertices VertexAttributes verts[128]
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

    uint primitiveCount = cluster.IndexCount / 3; // TODO: precalc?
    SetMeshOutputCounts(cluster.VertexCount, primitiveCount);

    if (gtid < primitiveCount)
    {
        tri[gtid] = GetTri(cluster.IndexStart + gtid * 4);
        prims[gtid].PackedOutput = (gid << 8) | (gtid & 0x000000FF);
    }

    if (gtid < cluster.VertexCount)
    {
        float4 vert = GetVertex(cluster.VertexStart + gtid);

        vert.xyz += instance.Position;

        verts[gtid].Position = mul(vert, constants.MVP);
    }
}
