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
layout(location = 2) rayPayloadEXT bool isShadowed;

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
layout(scalar, set = 0, binding = 8) readonly buffer LightEntitiesBuffer {LightEntity[] l;} lightEntitiesBuffer;
layout(scalar, set = 0, binding = 9) readonly buffer LightEntityIndicesBuffer {uint16_t[] li;} lightEntityIndices;

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

float getRelativeLuminance(vec3 tex_color){
	return tex_color.x * 0.2126 + tex_color.y * 0.7152 + tex_color.z * 0.0722;
}

vec4 applyLuminance(vec4 color){
	float maxvalue = max(max(color.x, color.y),color.z);
	float luminance_factor = 25 * maxvalue;
//	if(color.x == maxvalue){
//		return vec4(color.x * luminance_factor, color.y, color.z, color.w);
//	}
//
//	if(color.y == maxvalue){
//		return vec4(color.x, color.y * luminance_factor, color.z, color.w);
//	}
//
//	if(color.z == maxvalue){
//		return vec4(color.x, color.y, color.z * luminance_factor, color.w);
//	}

	return color * luminance_factor;
}

void main()
{	
	const int primitiveId = gl_PrimitiveID;
	const int instanceId = gl_InstanceCustomIndexEXT;
	const vec3 barycentrics = vec3(1.0 - hitCoordinate.x - hitCoordinate.y, hitCoordinate.x, hitCoordinate.y);
	
	uvec3 indices = getIndices(primitiveId, instanceId);
	
	Vertex v1 = getVertex(indices.x, instanceId);
	Vertex v2 = getVertex(indices.y, instanceId);
	Vertex v3 = getVertex(indices.z, instanceId);

	vec4 outColor = vec4(0.0);
	
	// texturing
	vec2 tex_coords = v1.vertex_tx * barycentrics.x + v2.vertex_tx * barycentrics.y + v3.vertex_tx * barycentrics.z;
	vec2 fb_coords = v1.vertex_fb * barycentrics.x + v2.vertex_fb * barycentrics.y + v3.vertex_fb * barycentrics.z;

	vec4 txcolor = vec4(0.0);
	vec4 fbcolor = vec4(0.0);

	if(v1.tx_index != -1){
		txcolor = texture(textures[v1.tx_index], tex_coords); // regular texture
	}
	if(v1.fb_index != -1){
		fbcolor = texture(textures[v1.fb_index], tex_coords); // fullbright texture
	}
	
	vec3 position = v1.vertex_pos * barycentrics.x + v2.vertex_pos * barycentrics.y + v3.vertex_pos * barycentrics.z;
	vec3 geometricNormal = normalize(cross(v2.vertex_pos - v1.vertex_pos, v3.vertex_pos - v1.vertex_pos));
	if(instanceId != 0){
		geometricNormal *= -1;
	}
	// vertices seem to be clock-wise on dynamic models, so the normal has to be inverted
	
	//debugPrintfEXT("pos calc: %v3f - world pos: %v3f", position, worldPos);

	//Light
	
	uint lightIndex;
	LightEntity lightEntity;
	vec3 lightDirection;
	float lightDistance;
	float lightIntensity;
	float attenuation;
	
	vec3 L ;

	float NdotL;
	float diffuse;
	vec3 sumLightColor = vec3(1);
	bool hitLight = false;
	bool hitSky = false;

	if(getRelativeLuminance(fbcolor.xyz) > 0.5){
		outColor = applyLuminance(fbcolor);
		hitLight = true;
	}

//
//	if(v1.material_index == 2){
//		hitSky = false;
//	}

	// if sky was hit
//	if(hitSky){
////		hitPayload.contribution += vec3(1, 1, 1);
////		hitPayload.done = true;
//	}

	//if light was hit and not the sky
	if(hitLight && !hitSky){
		hitPayload.contribution *= outColor.xyz;
		hitPayload.done = true;
	}

	// if neither sky or a light source was hit (indirect lighting)
	if(!hitLight){
		outColor = txcolor + fbcolor;

		outColor.xyz *= sumLightColor;

		hitPayload.contribution *= outColor.xyz;
		hitPayload.position = position;
		hitPayload.normal = geometricNormal;

	}
	
}
