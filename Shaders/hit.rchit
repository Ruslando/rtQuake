#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_debug_printf : enable

hitAttributeEXT vec2 hitCoordinate;

layout(location = 0) rayPayloadInEXT vec3 hitPayload;

layout(std430, set = 0, binding = 3) readonly buffer VertexBuffer {
	int8_t vertex[];
} vertexBuffer;

layout(std430, set = 0, binding = 3) readonly buffer TextureBuffer {
	vec2 coordinates[];
} stcoordinates;

layout(std430, set = 0, binding = 4) readonly buffer IndexBuffer {
	uint16_t index[];
} indexBuffer;

layout(set = 0, binding = 5) uniform sampler2D diffuse_tex;
layout(set = 0, binding = 6) uniform sampler2D fullbright_tex;

layout(set = 0, binding = 7) uniform UBO
{
	mat4 model_matrix;
	uint st_offset;
}ubo;


// Methods

vec3 get_vertex_pos(uint index)
{
	return u8vec3(vertexBuffer.vertex[index * 8], vertexBuffer.vertex[index * 8 + 1], vertexBuffer.vertex[index * 8 + 2]);
}

vec3 get_vertex_normal(uint index)
{
	return i8vec3(vertexBuffer.vertex[index * 8 + 4], vertexBuffer.vertex[index * 8 + 4 +  1], vertexBuffer.vertex[index * 8 + 4 + 2]);
}

vec2 get_st_coordinates(uint index)
{
	return stcoordinates.coordinates[index + (ubo.st_offset / 8)];
}


void main()
{	
	const uvec3 indices = uvec3(indexBuffer.index[gl_PrimitiveID * 3], indexBuffer.index[gl_PrimitiveID * 3 + 1], indexBuffer.index[gl_PrimitiveID * 3 + 2]);
	const vec3 barycentrics = vec3(1.0 - hitCoordinate.x - hitCoordinate.y, hitCoordinate.x, hitCoordinate.y);
	
	vec3 vertexPosA = get_vertex_pos(indices.x);
	vec3 vertexPosB = get_vertex_pos(indices.y);
	vec3 vertexPosC = get_vertex_pos(indices.z);
	
	vec2 vertex_st = get_st_coordinates(indices.x) * barycentrics.x
	+ get_st_coordinates(indices.y) * barycentrics.y
	+ get_st_coordinates(indices.z) * barycentrics.z;
	
	vec4 result = texture(diffuse_tex, vertex_st.xy);
	result += texture(fullbright_tex, vertex_st.xy);
	
	vec3 geometricNormal = normalize(cross(vertexPosB - vertexPosA, vertexPosC - vertexPosA));
	
	debugPrintfEXT("a: %v4f", ubo.model_matrix[0]);
	
	float NdotL = dot(geometricNormal, gl_WorldRayDirectionEXT);
	hitPayload = result.xyz;
}
