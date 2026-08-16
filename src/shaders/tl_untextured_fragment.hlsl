// Untextured fragment shader for the OpenRE GPU backend: outputs the
// interpolated vertex color. Used when no texture handle is bound/resolvable
// (the reference path renders with a white texture in that case; the vertex
// color carries the actual shading).

struct PSInput
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 texcoord : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target
{
    return input.color;
}
