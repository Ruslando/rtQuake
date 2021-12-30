#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_scalar_block_layout : enable
hitAttributeEXT vec2 hitCoordinate;

layout(location = 0) rayPayloadInEXT vec3 hitPayload;

struct ModelInfo{
	int vertex_offset;
	int vertex_tx_offset;
	int index_offset;
	int texture_index;
	int texture_fullbright_index;
};

struct Vertex{
	uvec3 vertex_pos;
	uvec3 vertex_tx;
};

struct Indices{
	uvec3 index;
};

layout(scalar, set = 0, binding = 3) readonly buffer VertexBuffer {uint v[];} vertexBuffer;
layout(scalar, set = 0, binding = 4) readonly buffer IndexBuffer {uint8_t i[];} indexBuffer;

layout(set = 0, binding = 5) uniform sampler2D textures[];

layout(scalar, set = 0, binding = 6) readonly buffer ModelInfoBuffer {ModelInfo[] m;} modelInfoBuffer;

Vertex getVertex(int index, int vertex_offset, int vertex_tx_offset);

uvec3 getInidices(int primitiveId, int index_offset){
	int primitive_index = primitiveId * 3;
	return uvec3(indexBuffer.i[index_offset + primitive_index],
	indexBuffer.i[index_offset + primitive_index + 1],
	indexBuffer.i[index_offset + primitive_index + 2]);
};

void main()
{	
	const int primitiveId = gl_PrimitiveID;
	const vec3 barycentrics = vec3(1.0 - hitCoordinate.x - hitCoordinate.y, hitCoordinate.x, hitCoordinate.y);
	
	ModelInfo modelInfo = modelInfoBuffer.m[gl_InstanceCustomIndexEXT];
	uvec3 indices = getInidices(primitiveId, modelInfo.index_offset);
	
	//Index indices1 = indexBuffer.i[primitiveId];
	//TextureIndex indices = ubo.ti[primitiveId];
	
	//Vertex v1 = vertexBuffer.v[indices1.index.x];
	//Vertex v2 = vertexBuffer.v[indices1.index.y];
	//Vertex v3 = vertexBuffer.v[indices1.index.z];
	
	//vec2 tex_coords = v1.in_texcoord1 * barycentrics.x + v2.in_texcoord1 * barycentrics.y + v3.in_texcoord1 * barycentrics.z;
	//uint sampler_index = indices.texture_index.x;
	//vec4 texture_result = texture(diffuse_tex[sampler_index], tex_coords);
	
	debugPrintfEXT("primitive: %i ;; custIdx: %i ;; indices: %v3u", primitiveId, gl_InstanceCustomIndexEXT, indices);
	
	hitPayload = vec3(0.5, 0.5, 0.5);
}
