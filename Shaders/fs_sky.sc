$input v_texcoord0, v_texcoord1, v_normal, v_viewdist

// One sky layer: two independently animated textures composited by a mask,
// then modulated by a lightmap.
//
// The mask gives each texture a coverage weight: (1 - mask) for Tex1 and
// mask for Tex2. Layer 1 uses a black mask, so it resolves to Tex1 and forms
// the opaque base. Later layers set Tex1 to "_alpha_zero" so the mask decides
// where Tex2 shows through.
//
// Colour must be weighted by each texture's OWN alpha as well as its coverage.
// "_alpha_zero" is white with alpha 3/255, so a plain mix() of the RGB drags
// every partly-masked pixel towards white and the layer turns milky. Weighting
// by alpha lets an invisible texture contribute no colour, which is what a
// transparent placeholder is for.
#include <bgfx_shader.sh>

SAMPLER2D(s_tex1, 0);
SAMPLER2D(s_tex2, 1);
SAMPLER2D(s_mask, 2);
SAMPLER2D(s_lmap, 3);

uniform vec4 u_tex1Xform;   // panU, panV, tileU, tileV  (pan already scaled by time)
uniform vec4 u_tex2Xform;
uniform vec4 u_skyRot;      // rot1, rot2, unused, unused (radians)

vec2 AnimateUv(vec2 uv, vec4 xform, float rot)
{
	// Rotate about the UV centre, then tile and scroll.
	vec2 centred = uv - vec2_splat(0.5);
	float s = sin(rot);
	float c = cos(rot);
	vec2 rotated = vec2(centred.x * c - centred.y * s, centred.x * s + centred.y * c);
	return (rotated + vec2_splat(0.5)) * xform.zw + xform.xy;
}

void main()
{
	vec4 t1 = texture2D(s_tex1, AnimateUv(v_texcoord0, u_tex1Xform, u_skyRot.x));
	vec4 t2 = texture2D(s_tex2, AnimateUv(v_texcoord0, u_tex2Xform, u_skyRot.y));

	// The mask and lightmap are fitted to the dome via the second UV channel;
	// only the two animated layer textures use the first.
	//
	// The mask value is colour TIMES alpha: gradient masks like _bulb_masker
	// are white with the gradient in ALPHA, cloud masks are DXT1 gradients in
	// RGB with alpha 1, and _black is black with alpha 1. Reading either
	// channel alone breaks one of those families; the product fits all.
	vec4 maskTexel = texture2D(s_mask, v_texcoord1);
	float mask = maskTexel.r * maskTexel.a;
	vec3 lmap  = texture2D(s_lmap, v_texcoord1).rgb;

	float w1 = (1.0 - mask) * t1.a;
	float w2 = mask * t2.a;
	float wsum = w1 + w2;

	// Straight (non-premultiplied) colour for BGFX_STATE_BLEND_ALPHA: divide
	// the weighted mix by the weight sum, never by the final alpha.
	vec3 colour = (t1.rgb * w1 + t2.rgb * w2) / max(wsum, 1.0 / 255.0);

	// The mask gates the whole layer on BLENDED shells: alpha must reach
	// EXACTLY zero where the mask fades out, or _alpha_zero's 3/255 leaves a
	// faint constant veil that cuts off in a hard line at the shell's
	// geometric rim. The base shell draws unblended, so its alpha is unused.
	float alpha = wsum * mask;

	gl_FragColor = vec4(colour * lmap * 2.0, alpha);
}
