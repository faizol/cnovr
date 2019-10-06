#version 430
#include "cnovr.glsl"

out vec4 colorOut;

in vec2 texcoords;
in vec3 localpos;
in vec3 localnorm;

layout(location = 8) uniform sampler2D textures[];

void main()
{
	colorOut = vec4( abs(texcoords.xyy)*1.0, 1.0);
	colorOut = colorOut * .2 + vec4( texture( textures[0], texcoords.xy ).xyz, 1.0);
}
