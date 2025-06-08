#include "ShaderCommon.hlsl"

[numthreads(8, 8, 1)]
void main(uint2 dtid : SV_DispatchThreadID)
{
	RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[TLAS_SRV];

	RayQuery<RAY_FLAG_CULL_NON_OPAQUE |
             RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;

    
    float4 rayOrigin = mul(constants.CullingCamera.ViewProjectionMatrix, float4(0.0f, 0.0f, 0.0f, 1.0f));
    rayOrigin *= 1.0f / rayOrigin.w;

    float2 screenCoord = float2(dtid.x / 1280.0, dtid.y / 720.0);
    float4 rayDirection = mul(constants.CullingCamera.InverseViewProjectionMatrix, float4(screenCoord.x, screenCoord.y, -1.0f, 1.0f));
    rayDirection *= 1.0f / rayDirection.w;
    rayDirection.xyz = normalize(rayDirection.xyz);

    RayDesc ray;
    ray.Origin = rayOrigin.xyz;
    ray.TMin = 0.0f;
    ray.Direction = rayDirection.xyz;
    ray.TMax = 100.0f;

    q.TraceRayInline(
        tlas,
        RAY_FLAG_NONE,
        0xffffffff,
        ray);

    q.Proceed();

    RWTexture2D<uint> vBuffer = ResourceDescriptorHeap[VBUFFER_UAV];
    //RWTexture2D<float> depthBuffer = ResourceDescriptorHeap[DEPTHBUFFER_UAV];
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        // Write depth and vbuffer data
        // q.CommittedInstanceIndex(),
        // q.CommittedPrimitiveIndex(),
        // q.CommittedGeometryIndex(),
        // q.CommittedRayT(),
        // q.CommittedTriangleBarycentrics(),
        // q.CommittedTriangleFrontFace() );

        vBuffer[dtid] = 0;
        //depthBuffer[dtid] = 1.0f;
    }
    else
    {
        vBuffer[dtid] = 0xffffffff;
        //depthBuffer[dtid] = 1.0f;
    }
}