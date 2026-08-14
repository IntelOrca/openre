// TL (transformed-and-lit) vertex shader for the OpenRE GPU backend.
//
// Input vertices are TL vertex values in screen space (top-left origin,
// pixels, z in [0,1]). SDL_GPU uses the modern graphics convention: NDC has
// its origin at the lower-left with +Y up, viewport coordinates are top-left
// origin with +Y down, and SDL auto-converts for backends that differ (e.g.
// Vulkan). We therefore flip Y here so the render target matches the game's
// top-left origin, exactly like the original viewport mapping.
//
// Resource layout follows SDL_GPU's shader conventions (SDL_gpu.h,
// SDL_CreateGPUShader): for DXIL the vertex uniform buffer lives in
// register space 1; when cross-compiling to SPIR-V with dxc's
// -fvk-use-dx-layout flag, space 1 maps to descriptor set 1, which is what
// SDL's Vulkan backend expects for vertex uniforms.
//
// Non system-value semantics must start at TEXCOORD0 and increment (SDL
// assumes all DXIL vertex inputs are TEXCOORDn and maps them to attribute
// locations in order).

cbuffer SceneConstants : register(b0, space1)
{
    float4 gViewport; // x, y (top-left origin, pixels), w, h (pixels)
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 texcoord : TEXCOORD1;
};

VSOutput main(
    float3 aPosition : TEXCOORD0, // sx, sy (pixels, top-left origin), sz (depth)
    float4 aColor : TEXCOORD1,    // packed RGBA as UBYTE4_NORM: byte order B,G,R,A
    float2 aTexcoord : TEXCOORD2) // tu, tv (top-left origin)
{
    VSOutput o;
    float2 ndc = float2(
        (aPosition.x - gViewport.x) / gViewport.z * 2.0 - 1.0,
        1.0 - (aPosition.y - gViewport.y) / gViewport.w * 2.0);
    o.position = float4(ndc, aPosition.z, 1.0);
    // aColor arrives in packed RGBA memory order (B,G,R,A); swizzle to RGBA.
    o.color = aColor.zyxw;
    o.texcoord = aTexcoord;
    return o;
}
