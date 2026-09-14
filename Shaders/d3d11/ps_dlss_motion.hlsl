// Motion map for the DLSS 5 NR history mask.
//
// DLSSNR.ControlMask weighs, per pixel, how much of its previous output the
// network keeps: low on static content removes most of the shimmer, but the
// history it keeps is not moved by motion vectors, so wherever the picture
// moves the mask has to stay at 1 or it smears. These passes decide where that
// is, at a quarter of the working resolution.
//
//   PASS 1  colour -> luma, 4x4 box, at quarter resolution
//   PASS 2  frame differences, 5x5 means: red is the largest of |L(t) - L(t-1)|,
//           |L(t) - L(t-4)| and |L(t) - L(t-8)| (BASELINES), green is the first
//           one alone
//   PASS 3  protection "age": dilated motion, or the previous age minus decay
//   PASS 4  ControlMask at full resolution: the stabiliser strength where
//           nothing moved recently, 1 where something did
//
// Passes 1 to 3 fetch texels with Load, so they depend on no sampler: the
// renderer's linear sampler only filters when magnifying. Pass 4 magnifies.
// The noise floor -- measured on the green channel -- and the whole-frame change
// (fades, cuts) come from a small readback on the CPU and are passed in as
// constants.

Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);   // pass 2: luma four frames back
Texture2D tex3 : register(t3);   // pass 2: luma eight frames back
SamplerState samp : register(s0);
SamplerState sampL : register(s1);

cbuffer PS_DLSS_MOTION : register(b0)
{
	float noiseFloor;      // frame difference of static content, luma units
	float threshold;       // motion starts at this many noise floors
	float softness;        // and is complete this many noise floors further
	float decay;           // protection lost per new frame
	float globalProtect;   // 0..1, a change of the whole frame
	float strength;        // mask value where nothing moves
	float2 reserved;
};

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

#ifndef PASS
#define PASS 1
#endif

#ifndef BASELINES
#define BASELINES 3   // 1: previous frame; 2: and four back; 3: and eight back
#endif

#ifndef DILATE
#define DILATE 4      // quarter-resolution texels, 16 source pixels
#endif

#if PASS == 1

// Luma of the 4x4 source block under this quarter-resolution texel.
float4 main(PS_INPUT input) : SV_Target
{
	static const float3 k = float3(0.2126, 0.7152, 0.0722);
	uint w, h;
	tex0.GetDimensions(w, h);
	const int2 last = int2(w, h) - 1;
	const int2 base = int2(input.Pos.xy) * 4;
	float3 c = 0;
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			c += tex0.Load(int3(clamp(base + int2(x, y), int2(0, 0), last), 0)).rgb;
		}
	}
	return float4(dot(c / 16.0, k), 0, 0, 1);
}

#elif PASS == 2

// tex0 current luma, tex1 the previous frame, tex2 four and tex3 eight frames
// back. A 5x5 mean rather than a single texel: compression noise differs from
// frame to frame everywhere, and single-texel peaks would survive the dilation
// below as motion all over the picture.
//
// Red, what motion is judged on, takes the longer baselines as well: a pan of
// 0.3 px per frame barely changes consecutive frames, but moves 1.2 px over four
// and 2.4 px over eight, while the noise between any two frames stays the same.
// Green, what the noise floor is measured on, is consecutive frames only: that
// same pan inflates the long differences everywhere, and a floor measured on
// them learns the pan as noise.
float4 main(PS_INPUT input) : SV_Target
{
	uint w, h;
	tex0.GetDimensions(w, h);
	const int2 last = int2(w, h) - 1;
	const int2 p = int2(input.Pos.xy);
	float d1 = 0;
	float d4 = 0;
	float d8 = 0;
	for (int y = -2; y <= 2; y++) {
		for (int x = -2; x <= 2; x++) {
			const int3 q = int3(clamp(p + int2(x, y), int2(0, 0), last), 0);
			const float l = tex0.Load(q).r;
			d1 += abs(l - tex1.Load(q).r);
#if BASELINES >= 2
			d4 += abs(l - tex2.Load(q).r);
#endif
#if BASELINES >= 3
			d8 += abs(l - tex3.Load(q).r);
#endif
		}
	}
	return float4(max(d1, max(d4, d8)) / 25.0, d1 / 25.0, 0, 1);
}

#elif PASS == 3

// tex0 frame differences (red), tex1 previous age. Dilated so the edges of a
// moving object are covered; the age keeps a recently moved area protected for
// 1/decay frames, which is the trail the history would otherwise smear over.
float4 main(PS_INPUT input) : SV_Target
{
	uint w, h;
	tex0.GetDimensions(w, h);
	const int2 last = int2(w, h) - 1;
	const int2 p = int2(input.Pos.xy);
	float d = 0;
	for (int y = -DILATE; y <= DILATE; y++) {
		for (int x = -DILATE; x <= DILATE; x++) {
			d = max(d, tex0.Load(int3(clamp(p + int2(x, y), int2(0, 0), last), 0)).r);
		}
	}
	const float motion = saturate((d / max(noiseFloor, 1e-5) - threshold) / max(softness, 1e-5));
	const float previous = tex1.Load(int3(p, 0)).r - decay;
	return float4(max(max(motion, globalProtect), previous), 0, 0, 1);
}

#else

// tex0 age at quarter resolution, filtered up to the mask size.
float4 main(PS_INPUT input) : SV_Target
{
	const float protect = saturate(tex0.Sample(sampL, input.Tex).r * 2.0);
	return float4(lerp(strength, 1.0, protect), 0, 0, 1);
}

#endif
