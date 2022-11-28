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

};

struct Material
{
    uint texture_id;
};

ConstantBuffer<Constants> constants : register(b0);
StructuredBuffer<Mesh> meshes : register(t0);
StructuredBuffer<Material> materials : register(t1);
Texture2D textures[] : register(t2);
