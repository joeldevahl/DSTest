#include "ShaderCommon.hlsl"

float4 main(float4 input : SV_Position, uint mesh_id : COLOR0, uint material_id : COLOR1) : SV_TARGET
{
	float mesh = ((float)mesh_id) / (float)constants.num_meshes;
	float mat = ((float)material_id) / (float)constants.num_materials;
	return float4(mesh, mat, 0.0, 1.0);
}
