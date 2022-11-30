#include "ShaderCommon.hlsl"

float4 main(float4 input : SV_Position, uint materialIndex : COLOR0) : SV_TARGET
{
	Material material = GetMaterial(materialIndex);
	return material.Color;
}
