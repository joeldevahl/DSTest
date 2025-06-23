#include "ShaderCommon.hlsl"

struct RayPayload
{
    uint vBufferValue;
};

[shader("raygeneration")]
void RayGeneration()
{
    uint2 idx = DispatchRaysIndex().xy;
    float2 size = DispatchRaysDimensions().xy;

    // TODO: move out to separate compute step
    if (idx.x == 0 && idx.y == 0)
    {
        RWByteAddressBuffer visibleInstancesCounter = ResourceDescriptorHeap[VISIBLE_INSTANCES_COUNTER_UAV];
        RWByteAddressBuffer visibleInstances = ResourceDescriptorHeap[VISIBLE_INSTANCES_UAV];

        RWByteAddressBuffer visibleClustersCounter = ResourceDescriptorHeap[VISIBLE_CLUSTERS_COUNTER_UAV];
        RWByteAddressBuffer visibleClusters = ResourceDescriptorHeap[VISIBLE_CLUSTERS_UAV];

        uint outNumClusters = 0;

        uint numInstances = constants.Counts.x;
        for (uint ii = 0; ii < numInstances; ++ii)
        {
            Instance instance = GetInstance(ii);
            Mesh mesh = GetMesh(instance.MeshIndex);

           	if (ii < MAX_VISIBLE_INSTANCES)
	        {
		        visibleInstances.Store(ii * 4, ii);

                for (uint ic = 0; ic < mesh.ClusterCount; ++ic)
                {
                    uint offsetCluster = outNumClusters;

           		    if (offsetCluster < MAX_VISIBLE_CLUSTERS)
		            {
			            uint val = ((mesh.ClusterStart + ic) & 0x0000ffff) | (ii << 16);
			            visibleClusters.Store(offsetCluster * 4, val);
		            }

                    outNumClusters += 1;
                }
            }
        }

        visibleInstancesCounter.Store(0, numInstances);
        visibleClustersCounter.Store(0, outNumClusters);
    }

    float3 p[2];
    for (int z = 0; z <= 1; z += 1)
    {
        float2 ndc = idx / size;
        float4 clipPos = float4(ndc.x * 2.0f - 1.0f, ndc.y * 2.0f - 1.0f, z == 0 ? 0.0f : 1.0f, 1.0f);
        float4 worldPos = mul(constants.CullingCamera.InverseViewProjectionMatrix, clipPos);
        worldPos /= worldPos.w;
		p[z] = float3(worldPos.x, worldPos.y, worldPos.z);
    }

    RayDesc ray;
    ray.Origin = p[0];
    ray.TMin = 1e-5f;
    ray.Direction = normalize(p[1] - p[0]);
    ray.TMax = 1e10f;

    RayPayload payload;
    RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[TLAS_SRV];
    TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    RWTexture2D<uint> vBuffer = ResourceDescriptorHeap[VBUFFER_UAV];
    vBuffer[idx] = payload.vBufferValue;
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.vBufferValue = 0xFFFFFFFF;
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    // TODO: how to get visible cluster from InstanceID?
    payload.vBufferValue = (InstanceID() << 8) | (PrimitiveIndex() & 0x000000FF);
}
