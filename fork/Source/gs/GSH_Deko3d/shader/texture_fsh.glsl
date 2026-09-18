#version 460

layout (location = 0) in vec4 inColor;
layout (location = 1) in vec3 inUVQ;

// Fase 2: binding 0 = slot de imagem 0 (ver GSH_Deko3d_Texture.cpp).
// O sampler (repeat/clamp) vem do handle ligado por draw run.
layout (binding = 0) uniform sampler2D tex;

layout (location = 0) out vec4 outColor;

void main()
{
    // ST usa perspectiva (s/q, t/q); UV ja vem normalizado com q=1.
    float q = max(inUVQ.z, 1e-6);
    vec2 uv = inUVQ.xy / q;
    // TFX modulate: texel x cor do vertice. (DECAL fica p/ depois.)
    outColor = texture(tex, uv) * inColor;
}
