#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_debug_printf : enable

hitAttributeEXT vec2 hitCoordinate;

layout(location = 0) rayPayloadInEXT vec3 hitPayload;

struct Vertex {
	u8vec4 position;
	i8vec4 normal;
};

struct Index {
	uint16_t index;
};

layout(set = 2, binding = 0) buffer VertexBuffer {
	Vertex vertex[];
} vertexBuffer;

layout(std430, set = 3, binding = 0) readonly buffer IndexBuffer {
	Index index[];
} indexBuffer;

void main()
{	
	const uvec3 indices = uvec3(indexBuffer.index[gl_PrimitiveID * 3].index, indexBuffer.index[gl_PrimitiveID * 3 + 1].index, indexBuffer.index[gl_PrimitiveID * 3 + 2].index);
	const vec3 barycentrics = vec3(1.0 - hitCoordinate.x - hitCoordinate.y, hitCoordinate.x, hitCoordinate.y);
	
	Vertex vertexA = vertexBuffer.vertex[indices.x];
	Vertex vertexB = vertexBuffer.vertex[indices.y];
	Vertex vertexC = vertexBuffer.vertex[indices.z];
	
	vec3 vertexPosA = vec3(u32vec4(vertexA.position));
	vec3 vertexPosB = vec3(u32vec4(vertexB.position));
	vec3 vertexPosC = vec3(u32vec4(vertexC.position));
	
	vec3 vertexNormA = vec3(i32vec4(vertexA.normal));
	vec3 vertexNormB = vec3(i32vec4(vertexB.normal));
	vec3 vertexNormC = vec3(i32vec4(vertexC.normal));
	
	vec3 geometricNormal = normalize(cross(vertexPosB - vertexPosA, vertexPosC - vertexPosA));
	const vec3 nrm = normalize(vertexNormA * barycentrics.x + vertexNormB * barycentrics.y + vertexNormC * barycentrics.z);
	
	float NdotL = dot(geometricNormal, gl_WorldRayDirectionEXT);
	hitPayload = vec3(0.0, 0.0, 1.0) * NdotL;
}
