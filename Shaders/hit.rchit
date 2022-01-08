#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_scalar_block_layout : enable
hitAttributeEXT vec2 hitCoordinate;

layout(location = 0) rayPayloadInEXT vec3 hitPayload;

struct ModelInfo{
	int texture_index;
	int texture_fullbright_index;
};

struct BlasInfo{
	int vertex_offset;
	int index_offset;
};

struct Vertex{
	vec3 vertex_pos;
	vec2 vertex_tx;
	vec2 vertex_fb;
	int vertex_model;
};

struct Indices{
	uint index;
};

layout(scalar, set = 0, binding = 3) readonly buffer VertexBuffer {Vertex[] v;} vertexBuffer;
layout(scalar, set = 0, binding = 4) readonly buffer IndexBuffer {Indices[] i;} indexBuffer;

layout(set = 0, binding = 5) uniform sampler2D textures[];

layout(scalar, set = 0, binding = 6) readonly buffer ModelInfoBuffer {ModelInfo[] m;} modelInfoBuffer;
layout(scalar, set = 0, binding = 7) readonly buffer BlasInfoBuffer {BlasInfo[] b;} blasInfoBuffer;

Vertex getVertex(int index, int vertex_offset, int vertex_tx_offset);

uvec3 getInidices(int primitiveId, int index_offset){
	int primitive_index = primitiveId * 3;
	return uvec3(indexBuffer.i[index_offset + primitive_index].index,
	indexBuffer.i[index_offset + primitive_index + 1].index,
	indexBuffer.i[index_offset + primitive_index + 2].index);
};

void main()
{	
	const int primitiveId = gl_PrimitiveID;
	const int instanceId = gl_InstanceCustomIndexEXT;
	const vec3 barycentrics = vec3(1.0 - hitCoordinate.x - hitCoordinate.y, hitCoordinate.x, hitCoordinate.y);
	
	BlasInfo blasInfo = blasInfoBuffer.b[instanceId];
	
	uvec3 indices = getInidices(primitiveId, blasInfo.index_offset);
	
	
	Vertex v1 = vertexBuffer.v[indices.x + blasInfo.vertex_offset];
	Vertex v2 = vertexBuffer.v[indices.y + blasInfo.vertex_offset];
	Vertex v3 = vertexBuffer.v[indices.z + blasInfo.vertex_offset];
	
	ModelInfo modelInfo = modelInfoBuffer.m[v1.vertex_model];
	
	vec2 tex_coords = v1.vertex_tx * barycentrics.x + v2.vertex_tx * barycentrics.y + v3.vertex_tx * barycentrics.z;
	vec4 texture_result = texture(textures[modelInfo.texture_index], tex_coords); // regular texture
	if(modelInfo.texture_fullbright_index != -1){
		texture_result += texture(textures[modelInfo.texture_fullbright_index], tex_coords); // fullbright texture
	}
	
	//debugPrintfEXT("primid: %i - instId: %i vertoff: %i - indoff: %i - txi: %i - fbi: %i", primitiveId, instanceId, modelInfo.vertex_offset, modelInfo.index_offset, modelInfo.texture_index, modelInfo.texture_fullbright_index);
	
	hitPayload = texture_result.xyz;
}
