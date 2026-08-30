$input v_color0, v_texcoord0

// One shader for every 2D draw. A solid quad is the same thing as a textured
// one with a 1x1 white texture bound, which keeps the whole HUD - panels,
// icons, glyphs - in a single batch and a single state.
//
// Text is drawn from an alpha-only glyph atlas expanded to RGBA at bake time,
// so it modulates exactly like an icon does.
//
// s_pattern is the menu's FONT TEXTURE: HUD::Print binds one as a second
// stage alongside the glyph atlas (46 shipped screens ask for
// "HUD/font_texturka_alpha"), and it is what gives menu text its engraved
// metal look rather than a flat colour - those rows are authored as
// RGBA(100,100,100), which unpatterned is literally grey.
//
// It is sampled in SCREEN space, not with the glyph's atlas UVs. The original
// can use its own atlas coordinates because its font texture was authored
// against its own atlas layout; ours is packed by stb_truetype and shares no
// layout with it, so atlas UVs would cut each glyph a random patch of the
// pattern. Screen space reproduces the look and is independent of packing.
#include <bgfx_shader.sh>

SAMPLER2D(s_diffuse, 0);
SAMPLER2D(s_pattern, 1);

// xy = 1/patternSize so the pattern tiles at native size; z is 1 when a
// pattern is actually bound.
uniform vec4 u_hudParams;

void main()
{
	vec4 texel = texture2D(s_diffuse, v_texcoord0);
	if (u_hudParams.z > 0.5)
	{
		// MODULATE2X: pattern times colour, DOUBLED. The authored numbers are
		// what say so - they sit near half scale, which is the signature of
		// that fixed-function op. A plain modulate leaves the rows a muddy
		// brown that vanishes into the art (textColor is RGBA(100,100,100)
		// against a gold pattern), and treating the colour as alpha-only
		// would throw away underMouseColor - which PainMenu defaults to
		// RGBA(166,3,3), a RED that has to survive as a hue. Doubling makes
		// all three land: gold for a normal row, bright red under the
		// pointer, washed-out for a disabled one.
		vec4 pattern = texture2D(s_pattern, gl_FragCoord.xy * u_hudParams.xy);
		gl_FragColor = vec4(pattern.rgb * v_color0.rgb * 2.0, texel.a * v_color0.a);
	}
	else
	{
		gl_FragColor = texel * v_color0;
	}
}
