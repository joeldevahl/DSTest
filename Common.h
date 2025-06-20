#define INSTANCE_BUFFER_SRV 0
#define MESH_BUFFER_SRV 1
#define CLUSTER_BUFFER_SRV 2
#define POSITION_DATA_BUFFER_SRV 3
#define NORMAL_DATA_BUFFER_SRV 4
#define TANGENT_DATA_BUFFER_SRV 5
#define TEXCOORD_DATA_BUFFER_SRV 6
#define INDEX_DATA_BUFFER_SRV 7
#define MATERIAL_BUFFER_SRV 8

#define VISIBLE_INSTANCES_SRV 10
#define VISIBLE_INSTANCES_UAV 11
#define VISIBLE_CLUSTERS_SRV 12
#define VISIBLE_CLUSTERS_UAV 13
#define VISIBLE_INSTANCES_COUNTER_UAV 14
#define VISIBLE_CLUSTERS_COUNTER_UAV 15
#define VBUFFER_UAV 16
#define VBUFFER_SRV 17
#define COLORBUFFER_UAV 18
#define DEPTHBUFFER_SRV 19
//#define DEPTHBUFFER_UAV 20

#define IMGUI_FONT_SRV 21

#define WORK_GRAPH_UAV 22

#define TLAS_SRV 23

#define MAX_ELEMENTS 65535

// Debug Mode
#define DEBUG_MODE_NONE 0
#define DEBUG_MODE_SHOW_TRIANGLES 1
#define DEBUG_MODE_SHOW_CLUSTERS 2
#define DEBUG_MODE_SHOW_INSTANCES 3
#define DEBUG_MODE_SHOW_MATERIALS 4
#define DEBUG_MODE_SHOW_DEPTH_BUFFER 5

struct MinMaxAABB
{
    float3 Min;
    float3 Max;
};

struct CenterExtentsAABB
{
    float3 Center;
    float3 Extents; // half extents
};

struct Camera
{
    float4x4 ViewMatrix;
    float4x4 ViewProjectionMatrix;
    float4x4 InverseProjectionMatrix;
    float4x4 InverseViewProjectionMatrix;
    float4 FrustumPlanes[6];
};

CB_ALIGN struct Constants
{
    Camera CullingCamera;
    Camera DrawingCamera;
    uint4 Counts;
    uint DebugMode;
    uint Padding0;
    uint Padding1;
    uint Padding2;
};

struct Instance
{
    float4x4 ModelMatrix;
    uint MeshIndex;
    uint MaterialIndex;

    CenterExtentsAABB Box;
};

struct Mesh
{
    uint ClusterStart;
    uint ClusterCount;

    // TODO: these do not need to be uploaded to GPU
    CenterExtentsAABB Box;
};

struct Cluster
{
    uint PrimitiveStart;
    uint PrimitiveCount;
    uint VertexStart;
    uint VertexCount;

    CenterExtentsAABB Box; // TODO: OOBB?
};

struct Material
{
    float4 Color;
    float Metallic;
    float Roughness;
};

