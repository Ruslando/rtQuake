#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_debug_printf : enable

layout(location = 0) rayPayloadInEXT vec3 hitPayload;

void main()
{
	float myfloat = 3.1415f;
	debugPrintfEXT("My float is %f", myfloat);
    hitPayload = vec3(0.0, 0.0, 0.0);
}