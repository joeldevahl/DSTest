#include "ShaderCommon.hlsl"

float4 main(float4 input : SV_Position, uint material_index : COLOR0) : SV_TARGET
{
	Material material = GetMaterial(material_index);
	return material.color;
}
