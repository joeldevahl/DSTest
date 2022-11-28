#include "ShaderCommon.hlsl"

groupshared Payload meshletPayload;

[NumThreads(1, 1, 1)]
void main(uint3 gid : SV_GroupID, uint gtid : SV_GroupThreadID)
{
	if (gtid == 0)
	{
		float scale = 3.0f;
		meshletPayload.offset = float3(scale * (gid.x - constants.counts.x / 2.0f), scale * (gid.y - constants.counts.y / 2.0f), -400.0f);
		meshletPayload.mesh_id = (gid.x + gid.y * constants.counts.x) % constants.num_meshes;
		meshletPayload.material_id = (gid.x + gid.y * constants.counts.x) % constants.num_materials;
	}
	DispatchMesh(1, 1, 1, meshletPayload);
}
