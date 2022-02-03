#version 460
#extension GL_EXT_ray_tracing : require

struct HitPayload
{
	uint sampleCount;
    vec3 contribution;
    vec3 origin;
    vec3 direction;
    bool done;
};

layout(location = 0) rayPayloadInEXT HitPayload hitPayload;


void main()
{
    hitPayload.contribution *= vec3(1) * 5;
    //hitPayload.contribution += vec3(1);
    hitPayload.done = true;
}