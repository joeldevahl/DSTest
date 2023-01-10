#define INSTANCE_BUFFER_SRV 0
#define MESH_BUFFER_SRV 1
#define CLUSTER_BUFFER_SRV 2
#define VERTEX_DATA_BUFFER_SRV 3
#define INDEX_DATA_BUFFER_SRV 4
#define MATERIAL_BUFFER_SRV 5

#define VISIBLE_INSTANCES_SRV 10
#define VISIBLE_INSTANCES_UAV 11
#define VISIBLE_CLUSTERS_SRV 12
#define VISIBLE_CLUSTERS_UAV 13
#define VISIBLE_INSTANCES_COUNTER_UAV 14
#define VISIBLE_CLUSTERS_COUNTER_UAV 15
#define VBUFFER_UAV 16
#define COLORBUFFER_UAV 17
#define DEPTHBUFFER_SRV 18

// HACKS TO REMOVE
#define VBUFFER_SRV 16

CB_ALIGN struct Constants
{
    float4x4 MVP;
    uint4 Counts;
};

struct Vertex
{
    float3 Position;
};

struct Instance
{
    float3 Position;
    uint MeshIndex;
    uint MaterialIndex;
};

struct Mesh
{
    uint ClusterStart;
    uint ClusterCount;
};

struct Cluster
{
    uint PrimitiveStart;
    uint PrimitiveCount;
    uint VertexStart;
    uint VertexCount;
};

struct Material
{
    float4 Color;
};
