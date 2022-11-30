#include "ShaderCommon.hlsl"

struct PrimitiveAttributes
{
    uint mesh_index : COLOR0;
};

struct VertexAttributes
{
    float4 pos : SV_Position;
};

[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void main(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    in payload Payload meshletPayload,
    out indices uint3 tris[2],
    out primitives PrimitiveAttributes prims[2],
    out vertices VertexAttributes verts[4]
)
{
    SetMeshOutputCounts(4, 2);

    if (gtid < 2)
    {
		tris[gtid] = gtid == 0 ? GetTri(0) : GetTri(1);
        prims[gtid].mesh_index = meshletPayload.mesh_index;
    }

    if (gtid < 4)
    {
        float4 vert = GetVertex(gtid);

        vert.xyz += meshletPayload.position;

        verts[gtid].pos = mul(vert, constants.mvp);
    }
}
