// The varying declarations every shader here shares - bgfx's "varying def".
//
// A .sc shader names its interpolants in $input / $output and nothing else;
// shaderc resolves each name against THIS file to get its type and semantic.
// So a shader can only pass v_wpos because the line for it exists below.
//
// One file rather than one per shader, for two reasons:
//
//   - a vertex and a fragment shader only link up if both resolve a name to
//     the same semantic, and sharing the declaration is what guarantees it.
//     Note that ADDING a varying to a vertex shader is not free either: bgfx
//     matches the whole signature, and fs_sky is built against vs_world, so a
//     varying added to vs_world must be added to fs_sky's $input too;
//   - the a_* entries pin the VERTEX ATTRIBUTE semantics, which have to agree
//     with the bgfx::VertexLayout built in C++ (Render/MeshVertex.h). The
//     world mesh's second UV set reaches the lightmap sampler because
//     a_texcoord1 is TEXCOORD1 in both places.
//
// bgfx would find this on its own if it were named varying.def.sc; it is named
// for what it holds instead, so Shaders/CMakeLists.txt passes --varyingdef.
// Blank lines and // comments are stripped by shaderc's parser.

vec3 a_position : POSITION;
vec3 a_normal : NORMAL;
vec4 a_color0 : COLOR0;
vec2 a_texcoord0 : TEXCOORD0;
vec2 a_texcoord1 : TEXCOORD1;

vec2 v_texcoord0 : TEXCOORD0;
vec2 v_texcoord1 : TEXCOORD1;
vec3 v_normal : NORMAL;
vec4 v_color0 : COLOR0;
float v_viewdist : TEXCOORD2;
vec3 v_wpos : TEXCOORD3;
