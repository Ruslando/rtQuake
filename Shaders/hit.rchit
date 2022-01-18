#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_scalar_block_layout : enable

hitAttributeEXT vec2 hitCoordinate;

layout(location = 0) rayPayloadInEXT vec3 hitPayload;
layout(location = 1) rayPayloadEXT bool isShadowed;

struct Vertex{
	vec3 vertex_pos;
	vec2 vertex_tx;
	vec2 vertex_fb;
	int	tx_index;
	int fb_index;
	int material_index;
};

struct LightEntity{
	vec4 origin_radius;
	vec4 light_color;
	vec4 light_clamp;
};

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;

layout(scalar, set = 0, binding = 3) readonly buffer StaticVertexBuffer {Vertex[] sv;} staticVertexBuffer;
layout(scalar, set = 0, binding = 4) readonly buffer DynamicVertexBuffer {Vertex[] dv;} dynamicVertexBuffer;
layout(scalar, set = 0, binding = 5) readonly buffer StaticIndexBuffer {uint16_t[] si;} staticIndexBuffer;
layout(scalar, set = 0, binding = 6) readonly buffer DynamicIndexBuffer {uint32_t[] di;} dynamicIndexBuffer;
layout(set = 0, binding = 7) uniform sampler2D textures[];
//layout(scalar, set = 0, binding = 9) readonly buffer LightEntitiesBuffer {LightEntity[] l;} lightEntitiesBuffer;
//layout(set = 0, binding = 10) uniform LightEntityIndicesBuffer {uint16_t [] li; } lightEntityIndices;

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
		return uvec3(staticIndexBuffer.si[primitive_index],
		staticIndexBuffer.si[primitive_index + 1],
		staticIndexBuffer.si[primitive_index + 2]);
	}
	else{
		return uvec3(dynamicIndexBuffer.di[primitive_index],
		dynamicIndexBuffer.di[primitive_index + 1],
		dynamicIndexBuffer.di[primitive_index + 2]);
	}
};

void main()
{	
	const int primitiveId = gl_PrimitiveID;
	const int instanceId = gl_InstanceCustomIndexEXT;
	const vec3 barycentrics = vec3(1.0 - hitCoordinate.x - hitCoordinate.y, hitCoordinate.x, hitCoordinate.y);
	const vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT; // not precise
	
	uvec3 indices = getIndices(primitiveId, instanceId);
	
	Vertex v1 = getVertex(indices.x, instanceId);
	Vertex v2 = getVertex(indices.y, instanceId);
	Vertex v3 = getVertex(indices.z, instanceId);

	vec4 outColor = vec4(0.0);
	
	// texturing
	vec2 tex_coords = v1.vertex_tx * barycentrics.x + v2.vertex_tx * barycentrics.y + v3.vertex_tx * barycentrics.z;
	vec2 fb_coords = v1.vertex_fb * barycentrics.x + v2.vertex_fb * barycentrics.y + v3.vertex_fb * barycentrics.z;

	if(v1.tx_index != -1){
		outColor += texture(textures[v1.tx_index], tex_coords); // regular texture
	}
	if(v1.fb_index != -1){
		outColor += texture(textures[v1.fb_index], fb_coords); // fullbright texture
	}
	
	//outColor += texture_result.xyz;

//	vec3 geometricNormal = normalize(cross(v2.vertex_pos - v1.vertex_pos, v3.vertex_pos - v1.vertex_pos));
//	
//	LightEntity lightEntity;
//	vec3 lightDirection;
//	float lightDistance;
//	float lightIntensity;
//	float attenuation;
//	vec3 L ;
//	vec3 L2;
//
//	float NdotL;
//	float diffuse;
//	float sumNdotL = 0;
//
//	// TODO: save number of indices somewhere
//	for(int i = 1; i < 3; i++)
//	{
//		lightEntity = lightEntitiesBuffer.l[i];
//		lightDirection = worldPos - lightEntity.origin_radius.xyz;	// usually other way around
//		lightDistance = length(lightDirection);
//		lightIntensity = (250 * 100) / (lightDistance * lightDistance);
//		attenuation = 1;
//		L = normalize(lightDirection);
//		L2 = normalize(lightDirection *-1);
//
//		NdotL = dot(geometricNormal, L);
//		float diffuse = lightIntensity * NdotL;
//
//		if(dot(geometricNormal, L2) < 0){
//			float tMin   = 0.001;
//			float tMax   = lightDistance;
//			vec3  origin = worldPos;
//			vec3  rayDir = L2; // flip ray direction
//			uint  flags =
//				gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT;
//			isShadowed = true;
//			traceRayEXT(topLevelAS,  // acceleration structure
//					flags,       // rayFlags
//					0xFF,        // cullMask
//					0,           // sbtRecordOffset
//					0,           // sbtRecordStride
//					1,           // missIndex
//					origin,      // ray origin
//					tMin,        // ray min range
//					rayDir,      // ray direction
//					tMax,        // ray max range
//					1            // payload (location = 1)
//			);
//
//			if(isShadowed){
//				attenuation = 0.3;
//			}
//		}
//
//		sumNdotL += diffuse * attenuation;
//		
//	}
//
//	outColor *= sumNdotL;
	
	//debugPrintfEXT("primid: %i - instId: %i vertoff: %i - indoff: %i - txi: %i - fbi: %i", primitiveId, instanceId, modelInfo.vertex_offset, modelInfo.index_offset, modelInfo.texture_index, modelInfo.texture_fullbright_index);
	
	hitPayload = outColor.xyz;
}
