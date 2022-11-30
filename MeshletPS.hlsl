#include "ShaderCommon.hlsl"

float4 main(float4 input : SV_Position, uint mesh_index : COLOR0) : SV_TARGET
{
	return float4(1.0, 1.0, 0.0, 1.0);
}
