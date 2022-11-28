#include "ShaderCommon.hlsl"

struct PrimitiveAttributes
{
    uint mesh_id : COLOR0;
    uint material_id : COLOR1;
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
		tris[gtid] = gtid == 0 ? uint3(0, 1, 2) : uint3(1, 3, 2);
        prims[gtid].mesh_id = meshletPayload.mesh_id;
        prims[gtid].material_id = meshletPayload.material_id;
    }

    if (gtid < 4)
    {
        float4 vert;
        switch (gtid) {
        case 0: vert = float4(-1.0f,  1.0f, 0.0f, 1.0f); break; // top-left
        case 1: vert = float4( 1.0f,  1.0f, 0.0f, 1.0f); break; // top-right
        case 2: vert = float4(-1.0f, -1.0f, 0.0f, 1.0f); break; // bottom-left
        case 3: vert = float4( 1.0f, -1.0f, 0.0f, 1.0f); break; // bottom-right
        }

        vert.xyz += meshletPayload.offset;

        verts[gtid].pos = mul(vert, constants.mvp);
    }
}
