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
};

struct Material
{
    float4 Color;
};
