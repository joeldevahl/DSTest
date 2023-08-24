#include "ShaderCommon.hlsl"

float4 main(float3 position : POSITION) : SV_Position
{
	return mul(constants.DrawingCamera.ViewProjectionMatrix, float4(position, 1.0));
}
