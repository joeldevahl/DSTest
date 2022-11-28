struct Payload
{
    float3 offset;
	uint mesh_id;
	uint material_id;
};

struct Constants
{
    float4x4 mvp;
    uint4 counts;
    uint num_meshes;
    uint num_materials;
};

struct Mesh
{
    uint cluster_start;
    uint cluster_count;
};

struct Cluster
{
    uint vertex_start;
    uint vertex_count;
};

struct Material
{
    uint texture_id;
};


ConstantBuffer<Constants> constants : register(b0);
