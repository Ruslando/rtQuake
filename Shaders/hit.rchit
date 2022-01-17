#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_scalar_block_layout : enable

struct HitPayload
{
    vec3 contribution;
    vec3 position;
    vec3 normal;
    bool done;
};

hitAttributeEXT vec2 hitCoordinate;

layout(location = 0) rayPayloadInEXT HitPayload  hitPayload;
layout(location = 1) rayPayloadEXT vec3 attribs;

struct ModelInfo{
	int texture_index;
	int texture_fullbright_index;
};

struct Vertex{
	vec3 vertex_pos;
	vec2 vertex_tx;
	vec2 vertex_fb;
	int vertex_model;
};

struct Indices{
	uint16_t index;
};

struct LightEntity{
	vec4 origin_radius;
	vec4 light_color;
	vec4 light_clamp;
};

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;

layout(scalar, set = 0, binding = 3) readonly buffer StaticVertexBuffer {Vertex[] sv;} staticVertexBuffer;
layout(scalar, set = 0, binding = 4) readonly buffer DynamicVertexBuffer {Vertex[] dv;} dynamicVertexBuffer;
layout(scalar, set = 0, binding = 5) readonly buffer StaticIndexBuffer {Indices[] si;} staticIndexBuffer;
layout(scalar, set = 0, binding = 6) readonly buffer DynamicIndexBuffer {Indices[] di;} dynamicIndexBuffer;
layout(set = 0, binding = 7) uniform sampler2D textures[];
layout(scalar, set = 0, binding = 8) readonly buffer ModelInfoBuffer {ModelInfo[] m;} modelInfoBuffer;
layout(scalar, set = 0, binding = 9) readonly buffer LightEntitiesBuffer {LightEntity[] l;} lightEntitiesBuffer;
layout(set = 0, binding = 10) uniform LightEntityIndicesBuffer {uint16_t [] li; } lightEntityIndices;

Vertex getVertex(uint index, int instanceId){
	if(instanceId == 0){
		return staticVertexBuffer.sv[index];
	}
	else{
		return dynamicVertexBuffer.dv[index];
	}
}

uvec3 getIndices(int primitiveId, int instanceId){
	int primitive_index = primitiveId * 3;

	if(instanceId == 0){
		return uvec3(staticIndexBuffer.si[primitive_index].index,
		staticIndexBuffer.si[primitive_index + 1].index,
		staticIndexBuffer.si[primitive_index + 2].index);
	}
	else{
		return uvec3(dynamicIndexBuffer.di[primitive_index].index,
		dynamicIndexBuffer.di[primitive_index + 1].index,
		dynamicIndexBuffer.di[primitive_index + 2].index);
	}
};

void main()
{	
	const int primitiveId = gl_PrimitiveID;
	const int instanceId = gl_InstanceCustomIndexEXT;
	const vec3 barycentrics = vec3(1.0 - hitCoordinate.x - hitCoordinate.y, hitCoordinate.x, hitCoordinate.y);
	
	uvec3 indices = getIndices(primitiveId, instanceId);
	
	Vertex v1 = getVertex(indices.x, instanceId);
	Vertex v2 = getVertex(indices.y, instanceId);
	Vertex v3 = getVertex(indices.z, instanceId);
	
	ModelInfo modelInfo = modelInfoBuffer.m[v1.vertex_model];

	vec3 outColor = vec3(0.0, 0.0, 0.0);
	
	// texturing
	vec2 tex_coords = v1.vertex_tx * barycentrics.x + v2.vertex_tx * barycentrics.y + v3.vertex_tx * barycentrics.z;
	vec4 texture_result = texture(textures[modelInfo.texture_index], tex_coords); // regular texture
	if(modelInfo.texture_fullbright_index != -1){
		texture_result += texture(textures[modelInfo.texture_fullbright_index], tex_coords); // fullbright texture
	}
	
	outColor += texture_result.xyz;
	
	vec3 position = v1.vertex_pos * barycentrics.x + v2.vertex_pos * barycentrics.y + v3.vertex_pos * barycentrics.z;
	vec3 geometricNormal = normalize(cross(v2.vertex_pos - v1.vertex_pos, v3.vertex_pos - v1.vertex_pos)) * -1;
	// vertices seem to be clock-wise, so the normal has to be inverted
	
	//debugPrintfEXT("pos calc: %v3f - world pos: %v3f", position, worldPos);

	//Light
	
	LightEntity lightEntity;
	vec3 lightDirection;
	float lightDistance;
	float lightIntensity;
	float attenuation;
	vec3 L ;

	float NdotL;
	float diffuse;
	float sumNdotL = 0;

	// TODO: save number of indices somewhere
	for(int i = 0; i < 16; i++)
	{
		lightEntity = lightEntitiesBuffer.l[i];
		lightDirection = lightEntity.origin_radius.xyz - position; // TODO: check why this gives weird results although its correct
		lightDistance = length(lightDirection);
		lightIntensity = (250 * 100) / (lightDistance * lightDistance);
		attenuation = 1;
		L = normalize(lightDirection);

		NdotL = dot(geometricNormal, L);
		float diffuse = lightIntensity * NdotL;

		sumNdotL += diffuse * attenuation;
	}

	outColor *= sumNdotL;

	// fill payload data
	hitPayload.contribution *= outColor;
	hitPayload.position = position; // adding slight offset to start position, so that ray doesnt hit the object at the start position
	hitPayload.normal = geometricNormal;
}
