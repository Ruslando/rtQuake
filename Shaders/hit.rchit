#version 460
#extension GL_EXT_ray_tracing : require

// InEXT
struct RayClosestHitPayload
{
	bool rayHit;
	int primitiveId;
	int instanceId;
	vec2 hitCoordinates;
	vec3 barycentrics;
	float rayHitDistance;
};

// buffers
layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;

hitAttributeEXT vec2 hitCoordinate;

layout(location = 0) rayPayloadInEXT RayClosestHitPayload rayClosestHitPayload;

void main()
{	
	rayClosestHitPayload.rayHit = true;
	rayClosestHitPayload.primitiveId = gl_PrimitiveID;
	rayClosestHitPayload.instanceId = gl_InstanceCustomIndexEXT;
	rayClosestHitPayload.hitCoordinates = vec2(hitCoordinate.x, hitCoordinate.y);
	rayClosestHitPayload.barycentrics = vec3(1.0 - hitCoordinate.x - hitCoordinate.y, hitCoordinate.x, hitCoordinate.y);
	rayClosestHitPayload.rayHitDistance = gl_HitTEXT;
}
