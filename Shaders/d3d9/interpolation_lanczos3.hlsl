// compensated Lanczos3

#ifndef AXIS
    #define AXIS 0
#endif

#define PI acos(-1.)

sampler s0 : register(s0);
float2 dxdy : register(c0);

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float t = frac(tex[AXIS]);
#if (AXIS == 0)
    float2 pos = tex - float2(t, 0.);
#elif (AXIS == 1)
    float2 pos = tex-float2(0., t);
#else
    #error ERROR: incorrect AXIS.
#endif

    // The weights below divide by t and by 1 - t. At an exact integer scale
    // factor, rounding lands t on 0 or on 1 and a whole row and column come out
    // as NaN -- 5999 pixels of a 4K frame at an exact x3, measured by
    // tools/dlssnr_probe --tupscale. A ten-thousandth of a pixel away from
    // either end is invisible and keeps every denominator finite.
    t = clamp(t, 1e-4, 1. - 1e-4);

    float4 Q2 = tex2D(s0, (pos+.5)*dxdy); // nearest original pixel to the left
    if(!t) return Q2; // case t == 0. is required to return sample Q2, because of a possible division by 0.
    else {
        // original pixels
#if (AXIS == 0)
        float4 Q0 = tex2D(s0, (pos+float2(-1.5, .5))*dxdy);
        float4 Q1 = tex2D(s0, (pos+float2(-.5, .5))*dxdy);
        float4 Q3 = tex2D(s0, (pos+float2(1.5, .5))*dxdy);
        float4 Q4 = tex2D(s0, (pos+float2(2.5, .5))*dxdy);
        float4 Q5 = tex2D(s0, (pos+float2(3.5, .5))*dxdy);
#elif (AXIS == 1)
        float4 Q0 = tex2D(s0, (pos+float2(.5, -1.5))*dxdy);
        float4 Q1 = tex2D(s0, (pos+float2(.5, -.5))*dxdy);
        float4 Q3 = tex2D(s0, (pos+float2(.5, 1.5))*dxdy);
        float4 Q4 = tex2D(s0, (pos+float2(.5, 2.5))*dxdy);
        float4 Q5 = tex2D(s0, (pos+float2(.5, 3.5))*dxdy);
#endif
        float3 wset0 = float3(2., 1., 0.)*PI+t*PI;
        float3 wset1 = float3(1., 2., 3.)*PI-t*PI;
        float3 wset0s = wset0*.5;
        float3 wset1s = wset1*.5;
        float3 w0 = sin(wset0)*sin(wset0s)/(wset0*wset0s);
        float3 w1 = sin(wset1)*sin(wset1s)/(wset1*wset1s);

        float wc = 1.-dot(1., w0+w1); // compensate truncated window factor by linear factoring on the two nearest samples
        w0.z += wc*(1.-t);
        w1.x += wc*t;

        return w0.x*Q0+w0.y*Q1+w0.z*Q2+w1.x*Q3+w1.y*Q4+w1.z*Q5; // interpolation output
    }
}
