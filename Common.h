#define INSTANCE_BUFFER_SRV 0
#define MESH_BUFFER_SRV 1
#define CLUSTER_BUFFER_SRV 2
#define VERTEX_DATA_BUFFER_SRV 3
#define INDEX_DATA_BUFFER_SRV 4
#define MATERIAL_BUFFER_SRV 5

#define VISIBLE_INSTANCES_SRV 10
#define VISIBLE_CLUSTERS_SRV 11
#define VBUFFER_UAV 12
#define COLORBUFFER_UAV 13
#define DEPTHBUFFER_SRV 14

// HACKS TO REMOVE
#define VISIBLE_INSTANCES_UAV 10
#define VISIBLE_CLUSTERS_UAV 11
#define VBUFFER_SRV 12

CB_ALIGN struct Constants
{
    float4x4 MVP;
    uint4 Counts;
};

struct Vertex
{
    float4 Position;
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
    uint IndexStart;
    uint IndexCount;
    uint VertexStart;
    uint VertexCount;
};

struct Material
{
    float4 Color;
};
