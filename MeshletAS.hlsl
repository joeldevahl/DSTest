#include "ShaderCommon.hlsl"

groupshared Payload meshletPayload;

[NumThreads(1, 1, 1)]
void main(uint3 gid : SV_GroupID, uint gtid : SV_GroupThreadID)
{
	if (gtid == 0)
	{
		Instance instance = GetInstance(gid.x);
		meshletPayload.position = instance.position;
		meshletPayload.mesh_index = instance.mesh_index;
		meshletPayload.material_index = instance.material_index;
	}
	DispatchMesh(1, 1, 1, meshletPayload);

}
