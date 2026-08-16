// Textured fragment shader for the OpenRE GPU backend: samples the bound
// texture and multiplies by the interpolated vertex color (which carries the
// TL vertex diffuse color; alpha drives alpha blending when enabled).
//
// Resource layout per SDL_GPU conventions (SDL_gpu.h, SDL_CreateGPUShader):
// DXIL pixel shaders place sampled textures at (t[n], space2) with matching
// samplers at (s[n], space2); dxc's -fvk-use-dx-layout maps space 2 to
// descriptor set 2 for SPIR-V, which SDL's Vulkan backend expects.

Texture2D gTexture : register(t0, space2);
SamplerState gSampler : register(s0, space2);

struct PSInput
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 texcoord : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target
{
    return gTexture.Sample(gSampler, input.texcoord) * input.color;
}
