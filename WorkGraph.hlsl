#include "ShaderCommon.hlsl"

GlobalRootSignature MeshNodesGlobalRS =
{
    "RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED), CBV(b0)"
};

LocalRootSignature MeshNodesLocalRS =
{
    "RootConstants(num32BitConstants=1, b1)"
};


struct InstanceCullingInputs
{
    uint gridSize : SV_DispatchGrid;
    uint numInstances;
};

struct ClusterCullingInputs
{
    uint gridSize : SV_DispatchGrid;
    uint instanceIndex;
    uint visibleInstanceIndex;
    uint numClusters;
};

struct MeshInputs
{
    uint instanceIndex;
    uint visibleInstanceIndex;
    uint clusterIndex;
    uint visibleClusterIndex;
};

[Shader("node")]
[NodeLaunch("thread")]
[NodeIsProgramEntry]
void FrameSetup(
	ThreadNodeInputRecord<Constants> inputData,
	[MaxRecords(1)] NodeOutput<InstanceCullingInputs> InstanceCulling
)
{
    RWByteAddressBuffer visibleInstancesCounter = ResourceDescriptorHeap[VISIBLE_INSTANCES_COUNTER_UAV];
	visibleInstancesCounter.Store(0, 0);

	RWByteAddressBuffer visibleClustersCounter = ResourceDescriptorHeap[VISIBLE_CLUSTERS_COUNTER_UAV];
	visibleClustersCounter.Store(0, 0);
	visibleClustersCounter.Store(4, 1);
	visibleClustersCounter.Store(8, 1);
    
	ThreadNodeOutputRecords<InstanceCullingInputs> record = InstanceCulling.GetThreadNodeOutputRecords(1);
    uint numInstances = constants.Counts.x;
	record.Get().gridSize = (numInstances + 63) / 64;
    record.Get().numInstances = numInstances;
	record.OutputComplete();
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(1024, 1, 1)]
[NumThreads(64, 1, 1)]
void InstanceCulling(
    uint tid : SV_DispatchThreadId,
    DispatchNodeInputRecord<InstanceCullingInputs> inputData,
    [MaxRecords(64)] NodeOutput<ClusterCullingInputs> ClusterCulling)
{
    InstanceCullingInputs inputs = inputData.Get();
    uint instanceIndex = tid;
    if(instanceIndex >= inputs.numInstances)
        return;

    Instance instance = GetInstance(instanceIndex);
    if (IsCulled(instance.Box))
		return;

	RWByteAddressBuffer visibleInstancesCounter = ResourceDescriptorHeap[VISIBLE_INSTANCES_COUNTER_UAV];
	uint visibleInstanceIndex = 0;
	visibleInstancesCounter.InterlockedAdd(0, 1, visibleInstanceIndex);
	
	if (visibleInstanceIndex >= MAX_ELEMENTS)
        return;

	RWByteAddressBuffer visibleInstances = ResourceDescriptorHeap[VISIBLE_INSTANCES_UAV];
	visibleInstances.Store(visibleInstanceIndex * 4, instanceIndex);

    Mesh mesh = GetMesh(instance.MeshIndex);

    ThreadNodeOutputRecords<ClusterCullingInputs> record = ClusterCulling.GetThreadNodeOutputRecords(1);
    record.Get().gridSize = (mesh.ClusterCount + 63) / 64;
	record.Get().instanceIndex = instanceIndex;
    record.Get().visibleInstanceIndex = visibleInstanceIndex;
    record.Get().numClusters = mesh.ClusterCount;
	record.OutputComplete();
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(1024, 1, 1)]
[NumThreads(64, 1, 1)]
void ClusterCulling(
    uint tid : SV_DispatchThreadId,
    DispatchNodeInputRecord<ClusterCullingInputs> inputData,
    [MaxRecords(64)] NodeOutput<MeshInputs> MeshNode)
{
    ClusterCullingInputs inputs = inputData.Get();
    uint clusterIndex = tid;
    if(clusterIndex >= inputs.numClusters)
        return;

    Instance instance = GetInstance(inputs.instanceIndex);
	Mesh mesh = GetMesh(instance.MeshIndex);
    Cluster cluster = GetCluster(mesh.ClusterStart + clusterIndex);

    CenterExtentsAABB box = TransformAABB(cluster.Box, instance.ModelMatrix);
	if (IsCulled(box))
		return;

    uint visibleClusterIndex = 0;
    RWByteAddressBuffer visibleClustersCounter = ResourceDescriptorHeap[VISIBLE_CLUSTERS_COUNTER_UAV];
	visibleClustersCounter.InterlockedAdd(0, 1, visibleClusterIndex);

	if (visibleClusterIndex >= MAX_ELEMENTS) 
        return;

	uint val = ((mesh.ClusterStart + clusterIndex) & 0x0000ffff) | (inputs.visibleInstanceIndex << 16);
    RWByteAddressBuffer visibleClusters = ResourceDescriptorHeap[VISIBLE_CLUSTERS_UAV];
	visibleClusters.Store(visibleClusterIndex * 4, val);

    ThreadNodeOutputRecords<MeshInputs> record = MeshNode.GetThreadNodeOutputRecords(1);
	record.Get().instanceIndex = inputs.instanceIndex;
    record.Get().visibleInstanceIndex = inputs.visibleInstanceIndex;
    record.Get().clusterIndex = clusterIndex;
    record.Get().visibleClusterIndex = visibleClusterIndex;
	record.OutputComplete();
}

struct PrimitiveAttributes
{
    uint PackedOutput : COLOR0;
};

struct VertexAttributes
{
    float4 Position : SV_Position;
};

[Shader("node")]
[NodeLaunch("mesh")]
[NumThreads(128, 1, 1)]
[NodeDispatchGrid(1, 1, 1)]
[OutputTopology("triangle")]
void MeshNode(
    uint gtid : SV_GroupThreadId,
    DispatchNodeInputRecord<MeshInputs> inputData,
    out indices uint3 tri[124],
    out primitives PrimitiveAttributes prims[124],
    out vertices VertexAttributes verts[64]
)
{
    MeshInputs inputs = inputData.Get();
    
    Cluster cluster = GetCluster(inputs.clusterIndex);
    Instance instance = GetInstance(inputs.instanceIndex);

    SetMeshOutputCounts(cluster.VertexCount, cluster.PrimitiveCount);
 
    if (gtid < cluster.PrimitiveCount)
    {
        tri[gtid] = GetTri(cluster.PrimitiveStart + gtid);
        prims[gtid].PackedOutput = (inputs.visibleClusterIndex << 8) | (gtid & 0x000000FF);
    }
    
    if (gtid < cluster.VertexCount)
    {
        float3 vert = GetPosition(cluster.VertexStart + gtid);

        float4 transformedVert = mul(instance.ModelMatrix, float4(vert, 1.0));

        verts[gtid].Position = mul(constants.DrawingCamera.ViewProjectionMatrix, transformedVert);
    }
}

