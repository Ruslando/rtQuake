#version 460
#extension GL_EXT_ray_tracing : require

struct HitPayload
{
    vec3 contribution;
    vec3 position;
    vec3 normal;
    bool done;
};

layout(location = 0) rayPayloadInEXT HitPayload hitPayload;

void main()
{
	hitPayload.contribution *= vec3(0.9, 0.9, 1.0) * 10;
    //hitPayload.contribution *= vec3(0);
    hitPayload.done = true;
}