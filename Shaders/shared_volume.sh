// A volume's colour over the scene (Render/VolumeRenderer.h), shared by the single-
// and multisampled scene depth: the includer declares s_depth and defines
// VolumeSceneDepth(ivec2). The original's alpha: ramp(back) - ramp(front), a face
// hidden by the scene standing in at the scene's depth. Docs/Reference/FogVolumes.md
#ifndef PAINFUL_SHARED_VOLUME
#define PAINFUL_SHARED_VOLUME

uniform vec4 u_volume; // x: 1 / End, y: 1 a light volume (added), 0 fog (blended), z: 1 the eye is inside
uniform vec4 u_volumeColor; // rgb: the colour, dimmed while bloom is on
uniform vec4 u_volumeDepth; // x: proj[10], y: proj[14], z: 1 when depth runs -1..1
SAMPLER2D(s_volumeFaces, 1);

void main()
{
	ivec2 p = ivec2(gl_FragCoord.xy);
	vec4 faces = texelFetch(s_volumeFaces, p, 0);
	// The scene's distance ahead, from the right-handed projection: d = m14 / (ndc + m10).
	float d = VolumeSceneDepth(p);
	float ndcZ = u_volumeDepth.z > 0.5 ? d * 2.0 - 1.0 : d;
	float sceneDist = u_volumeDepth.y / min(ndcZ + u_volumeDepth.x, -1e-7);
	float scene = clamp(sceneDist * u_volume.x, 0.0, 1.0);
	// A missing front face counts from the eye only when the eye is inside (tested on
	// the CPU): a silhouette pixel's centre can take the back triangle and not the front
	// one, and an MSAA edge sample can have no face at its centre at all.
	float front = faces.a > 0.5 ? min(faces.r, scene) : (u_volume.z > 0.5 ? 0.0 : scene);
	float a = faces.b > 0.5 ? 0.0 : max(min(faces.g, scene) - front, 0.0);
	if (u_volume.y > 0.5) gl_FragColor = vec4(u_volumeColor.rgb * a, 1.0);
	else gl_FragColor = vec4(u_volumeColor.rgb, a);
}

#endif
