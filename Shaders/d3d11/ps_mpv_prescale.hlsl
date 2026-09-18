// The passes around the mpv prescalers (Shaders/mpv), which enlarge one plane:
// the renderer hands them that plane and puts the colour back itself.
//
//   PASS 1  luma of the picture, for FSRCNNX and RAVU-zoom
//   PASS 2  colour: the picture enlarged by Catmull-Rom (t0) with its luma
//           replaced by the prescaler's (t1), both at the same size. The
//           difference goes to R, G and B alike, which moves luma and leaves
//           B - Y and R - Y as they were, whatever weights the luma has.
//   PASS 3  one channel of a chroma plane (the constant picks it) into red, for
//           RAVU-zoom on Cb and on Cr
//
// All three draw through the renderer's quad and read with its point sampler,
// output and input sizes matching texel for texel.

#ifndef PASS
#define PASS 1
#endif

Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
SamplerState samp : register(s0);

cbuffer PS_MPV_PRESCALE : register(b0)
{
	float4 channel; // pass 3: 1 for the channel to take, 0 for the others
};

// BT.709 weights, those of the video the networks were trained on.
static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
#if PASS == 1
	float3 rgb = tex0.Sample(samp, input.Tex).rgb;
	return float4(dot(rgb, kLuma), 0, 0, 1);
#elif PASS == 2
	float4 color = tex0.Sample(samp, input.Tex);
	float y = tex1.Sample(samp, input.Tex).r;
	color.rgb += y - dot(color.rgb, kLuma);
	return color;
#else
	return float4(dot(tex0.Sample(samp, input.Tex), channel), 0, 0, 1);
#endif
}
