#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_scalar_block_layout : enable
hitAttributeEXT vec2 hitCoordinate;

layout(location = 0) rayPayloadInEXT vec3 hitPayload;

struct Vertex{
	vec3 vertex;
	vec2 in_texcoord1;
	vec2 in_texcoord2;
};

struct Index{
	uvec3 index;
};

struct TextureIndex{
	uvec3 texture_index;
};

layout(scalar, set = 0, binding = 3) readonly buffer VertexBuffer {Vertex[] v;} vertexBuffer;
layout(scalar, set = 0, binding = 4) readonly buffer IndexBuffer {Index[] i;} indexBuffer;

layout(set = 0, binding = 5) uniform sampler2D diffuse_tex[];
layout(set = 0, binding = 6) uniform sampler2D fullbright_tex;

layout(scalar, set = 0, binding = 7) readonly buffer UBO {TextureIndex[] ti; } ubo;

// Methods

void main()
{	
	const int primitiveId = gl_PrimitiveID;
	const vec3 barycentrics = vec3(1.0 - hitCoordinate.x - hitCoordinate.y, hitCoordinate.x, hitCoordinate.y);
	
	Index indices1 = indexBuffer.i[primitiveId];
	TextureIndex indices = ubo.ti[primitiveId];
	
	Vertex v1 = vertexBuffer.v[indices1.index.x];
	Vertex v2 = vertexBuffer.v[indices1.index.y];
	Vertex v3 = vertexBuffer.v[indices1.index.z];
	
	vec2 tex_coords = v1.in_texcoord1 * barycentrics.x + v2.in_texcoord1 * barycentrics.y + v3.in_texcoord1 * barycentrics.z;
	uint sampler_index = indices.texture_index.x;
	vec4 texture_result = texture(diffuse_tex[sampler_index], tex_coords);
	
	hitPayload = texture_result.xyz;
}
