#include "ShaderCommon.hlsl"

groupshared Payload meshletPayload;

[NumThreads(1, 1, 1)]
void main(uint3 gid : SV_GroupID, uint gtid : SV_GroupThreadID)
{
	if (gtid == 0)
	{
		Instance instance = GetInstance(gid.x);
		meshletPayload.Position = instance.Position;
		meshletPayload.MeshIndex = instance.MeshIndex;
		meshletPayload.MaterialIndex = instance.MaterialIndex;
	}
	DispatchMesh(1, 1, 1, meshletPayload);

}
