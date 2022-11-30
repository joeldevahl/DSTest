#include "ShaderCommon.hlsl"

struct PrimitiveAttributes
{
    uint MaterialIndex : COLOR0;
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
    out indices uint3 tris[2],
    out primitives PrimitiveAttributes prims[2],
    out vertices VertexAttributes verts[4]
)
{
    SetMeshOutputCounts(4, 2);

	ByteAddressBuffer visibleInstances = ResourceDescriptorHeap[10];
	ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[11];
    uint packedClusterInstance = visibleClusters.Load(gid * 4);
    uint clusterIndex = packedClusterInstance & 0x0000ffff;
    uint visibleInstanceIndex = packedClusterInstance >> 16;
    uint instanceIndex = visibleInstances.Load(visibleInstanceIndex * 4);

    Cluster cluster = GetCluster(clusterIndex);
    Instance instance = GetInstance(instanceIndex);

    if (gtid < 2)
    {
		tris[gtid] = gtid == 0 ? GetTri(cluster.IndexStart + 0) : GetTri(cluster.IndexStart + 1);
        prims[gtid].MaterialIndex = instance.MaterialIndex;
    }

    if (gtid < 4)
    {
        float4 vert = GetVertex(gtid);

        vert.xyz += instance.Position;

        verts[gtid].Position = mul(vert, constants.MVP);
    }
}
