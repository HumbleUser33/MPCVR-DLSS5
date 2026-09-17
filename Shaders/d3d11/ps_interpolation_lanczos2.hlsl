#ifndef AXIS
    #define AXIS 0
#endif

#define PI acos(-1.)

Texture2D tex : register(t0);
SamplerState samp : register(s0);

cbuffer PS_CONSTANTS : register(b0)
{
    float2 wh;
    float2 dxdy;
    float2 scale;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    float pos = input.Tex[AXIS] * wh[AXIS] - 0.5;
    float t = frac(pos); // calculate the difference between the output pixel and the original surrounding two pixels
    pos = pos - t;
    // The weights below divide by t and by 1 - t. At an exact integer scale
    // factor, rounding lands t on 0 or on 1 and a whole row and column come out
    // as NaN -- 5999 pixels of a 4K frame at an exact x3, measured by
    // tools/dlssnr_probe --tupscale. A ten-thousandth of a pixel away from
    // either end is invisible and keeps every denominator finite.
    t = clamp(t, 1e-4, 1. - 1e-4);

#if (AXIS == 0)
    float4 Q1 = tex.Sample(samp, float2((pos + 0.5) * dxdy.x, input.Tex.y)); // nearest original pixel to the left
    if (t) {
        // original pixels
        float4 Q0 = tex.Sample(samp, float2((pos - 0.5) * dxdy.x, input.Tex.y));
        float4 Q2 = tex.Sample(samp, float2((pos + 1.5) * dxdy.x, input.Tex.y));
        float4 Q3 = tex.Sample(samp, float2((pos + 2.5) * dxdy.x, input.Tex.y));
#elif (AXIS == 1)
    float4 Q1 = tex.Sample(samp, float2(input.Tex.x, (pos + 0.5) * dxdy.y));
    if (t) {
        // original pixels
        float4 Q0 = tex.Sample(samp, float2(input.Tex.x, (pos - 0.5) * dxdy.y));
        float4 Q2 = tex.Sample(samp, float2(input.Tex.x, (pos + 1.5) * dxdy.y));
        float4 Q3 = tex.Sample(samp, float2(input.Tex.x, (pos + 2.5) * dxdy.y));
#else
    #error ERROR: incorrect AXIS.
#endif
        float4 wset = float3(0., 1., 2.).yxyz + float2(t, -t).xxyy;
        float4 w = sin(wset * PI) * sin(wset * PI * .5) / (wset * wset * PI * PI * .5);

        float wc = 1. - dot(1., w); // compensate truncated window factor by bilinear factoring on the two nearest samples
        w.y += wc * (1. - t);
        w.z += wc * t;

        return w.x * Q0 + w.y * Q1 + w.z * Q2 + w.w * Q3; // interpolation output
    }

    return Q1; // case t == 0. is required to return sample Q1, because of a possible division by 0.
}
