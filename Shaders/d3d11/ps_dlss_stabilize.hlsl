// Temporal stabilizer for DLSS 5 NR, applied after the network.
//
// The network's own history cannot be steadied through DLSSNR.ControlMask: any
// mask strips about 90 % of the effect (tools/dlssnr_probe --teffect). These
// passes steady the result instead. Two variants, measured against each other:
//   EFFECT  only the network's change to the picture, E = out - in, is filtered
//           over time and added to the current frame: the video underneath is
//           never delayed, only the enhancement is
//   OUTPUT  the output itself is filtered, TAA-style
// Either way the history is clamped to the neighbourhood of the current value,
// widened by a tolerance, so a wrong motion estimate cannot drag old content far.
//
// Motion comes from the shader detector (ps_dlss_motion.hlsl: its age map says
// where something moved, no vectors) or from NVIDIA Optical Flow (vectors with a
// confidence built from forward/backward consistency, matching cost and the input
// itself): with vectors the history follows the picture, so moving areas can be
// steadied too.
//
//   PASS 0  flow frame: luma of the working-size picture, box-filtered by an
//           integer factor, into the R8 texture NVIDIA Optical Flow reads
//   PASS 1  flow -> motion (MRT): target 0 motion in working-size pixels, current
//           to previous (RG16F, also usable as DLSSNR.MVec); target 1 confidence
//   PASS 2  stabilize (MRT): target 0 history for the next frame, target 1 the
//           picture to show
//
// Every fetch is a Load, or a bilinear built from Loads: the renderer's linear
// sampler only filters when magnifying, and integer textures cannot be sampled.

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

#ifndef PASS
#define PASS 2
#endif

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

#if PASS == 0

Texture2D picture : register(t0);

cbuffer PS_DLSS_FLOW_FRAME : register(b0)
{
	float factor;   // working pixels per flow pixel: 1, 2 or 4
	float3 reserved;
};

float4 main(PS_INPUT input) : SV_Target
{
	uint w, h;
	picture.GetDimensions(w, h);
	const int2 last = int2(w, h) - 1;
	const int f = clamp((int)factor, 1, 4);
	const int2 base = int2(input.Pos.xy) * f;
	float3 c = 0;
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			if (x < f && y < f) {
				c += picture.Load(int3(clamp(base + int2(x, y), int2(0, 0), last), 0)).rgb;
			}
		}
	}
	c /= (float)(f * f);
	return float4(dot(saturate(c), kLuma), 0, 0, 1);
}

#elif PASS == 1

Texture2D<int4>  flowForward  : register(t0);   // R16G16_SINT, 1/32 flow pixel, current -> previous
Texture2D<int4>  flowBackward : register(t1);   // previous -> current
Texture2D<uint4> costForward  : register(t2);   // R8_UINT, higher is worse

cbuffer PS_DLSS_FLOW_MOTION : register(b0)
{
	float2 flowScale;     // working pixels per flow pixel, x and y
	float  grid;          // flow pixels per vector
	float  consistency;   // forward/backward mismatch (flow pixels) where confidence starts to fall
	float2 flowSize;      // flow frame size, pixels
	float  costLow;       // cost where confidence starts to fall
	float  costHigh;      // and where it reaches 0
	float  useBackward;   // 0 when the engine gave no backward flow
	float  useCost;       // 0 when it gave no cost
	float2 reserved;
};

struct MOTION_OUTPUT
{
	float4 motion     : SV_Target0;
	float4 confidence : SV_Target1;
};

MOTION_OUTPUT main(PS_INPUT input)
{
	uint gw, gh;
	flowForward.GetDimensions(gw, gh);
	const int2 last = int2(gw, gh) - 1;

	// This pixel in the flow frame, and a bilinear of the four vector blocks
	// around it (block centres sit at (b + 0.5) * grid).
	const float2 q = input.Pos.xy / flowScale;
	const float2 bf = q / grid - 0.5;
	const int2 b0 = (int2)floor(bf);
	const float2 t = bf - (float2)b0;
	float2 f = 0;
	for (int j = 0; j < 2; j++) {
		for (int i = 0; i < 2; i++) {
			const int2 b = clamp(b0 + int2(i, j), int2(0, 0), last);
			const float weight = (i ? t.x : 1.0 - t.x) * (j ? t.y : 1.0 - t.y);
			f += weight * (float2)flowForward.Load(int3(b, 0)).xy;
		}
	}
	f /= 32.0;

	const float2 qPrevious = q + f;
	float confidence = 1.0;

	// Where this content was, the backward flow should point straight back.
	if (useBackward > 0.5) {
		const int2 bb = clamp((int2)floor(qPrevious / grid), int2(0, 0), last);
		const float2 back = (float2)flowBackward.Load(int3(bb, 0)).xy / 32.0;
		confidence *= 1.0 - smoothstep(consistency, 2.0 * consistency, length(f + back));
	}
	if (useCost > 0.5) {
		const int2 bc = clamp((int2)floor(q / grid), int2(0, 0), last);
		confidence *= 1.0 - smoothstep(costLow, costHigh, (float)costForward.Load(int3(bc, 0)).x);
	}
	if (any(qPrevious < 0.0) || any(qPrevious > flowSize)) {
		confidence = 0.0;
	}

	MOTION_OUTPUT o;
	o.motion = float4(f * flowScale, 0, 1);
	o.confidence = float4(confidence, 0, 0, 1);
	return o;
}

#else

Texture2D current    : register(t0);   // the network's input this frame
Texture2D network    : register(t1);   // its output
Texture2D history    : register(t2);   // last frame's target 0
Texture2D motion     : register(t3);   // flow: PASS 1 target 0; detector: its age map (R)
Texture2D previous   : register(t4);   // flow: last frame's network input
Texture2D confidence : register(t5);   // flow: PASS 1 target 1

cbuffer PS_DLSS_STABILIZE : register(b0)
{
	float  minCurrent;     // weight of the current frame where the history is trusted
	float  tolerance;      // how far the history may stray from the current neighbourhood
	float  useFlow;        // 1: vectors in motion/confidence; 0: detector age in motion.r
	float  reprojection;   // flow: input luma error where confidence starts to fall; 0 = off
	float  reset;          // 1: no history this frame
	float  outputMode;     // 0: EFFECT, 1: OUTPUT
	float  ageSize;        // detector age map texels per working pixel (0.25)
	float  reserved;
};

struct STABILIZE_OUTPUT
{
	float4 history : SV_Target0;
	float4 color   : SV_Target1;
};

float3 Fetch(Texture2D tex, int2 p, int2 last)
{
	return tex.Load(int3(clamp(p, int2(0, 0), last), 0)).rgb;
}

// Bilinear at a position in pixels (centres at +0.5), from Loads.
float3 Bilinear(Texture2D tex, float2 pos, int2 last)
{
	const float2 f = pos - 0.5;
	const int2 i0 = (int2)floor(f);
	const float2 t = f - (float2)i0;
	const float3 top    = lerp(Fetch(tex, i0, last), Fetch(tex, i0 + int2(1, 0), last), t.x);
	const float3 bottom = lerp(Fetch(tex, i0 + int2(0, 1), last), Fetch(tex, i0 + int2(1, 1), last), t.x);
	return lerp(top, bottom, t.y);
}

float BilinearR(Texture2D tex, float2 pos, int2 last)
{
	const float2 f = pos - 0.5;
	const int2 i0 = (int2)floor(f);
	const float2 t = f - (float2)i0;
	const float top    = lerp(tex.Load(int3(clamp(i0, int2(0, 0), last), 0)).r, tex.Load(int3(clamp(i0 + int2(1, 0), int2(0, 0), last), 0)).r, t.x);
	const float bottom = lerp(tex.Load(int3(clamp(i0 + int2(0, 1), int2(0, 0), last), 0)).r, tex.Load(int3(clamp(i0 + int2(1, 1), int2(0, 0), last), 0)).r, t.x);
	return lerp(top, bottom, t.y);
}

float3 Value(int2 p, int2 last)
{
	const float3 result = Fetch(network, p, last);
	return (outputMode > 0.5) ? result : result - Fetch(current, p, last);
}

STABILIZE_OUTPUT main(PS_INPUT input)
{
	uint w, h;
	current.GetDimensions(w, h);
	const int2 last = int2(w, h) - 1;
	const int2 p = int2(input.Pos.xy);

	// The quantity being steadied, and its 3x3 neighbourhood.
	const float3 value = Value(p, last);
	float3 low = value;
	float3 high = value;
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			const float3 v = Value(p + int2(x, y), last);
			low = min(low, v);
			high = max(high, v);
		}
	}

	float trust = 0.0;
	float2 previousPos = input.Pos.xy;
	if (useFlow > 0.5) {
		previousPos += motion.Load(int3(p, 0)).rg;
		trust = confidence.Load(int3(p, 0)).r;
		if (reprojection > 0.0) {
			const float error = abs(dot(Fetch(current, p, last) - Bilinear(previous, previousPos, last), kLuma));
			trust *= 1.0 - smoothstep(reprojection, 2.0 * reprojection, error);
		}
		if (any(previousPos < 0.0) || any(previousPos > float2(w, h))) {
			trust = 0.0;
		}
	} else {
		// Detector age: 0 where nothing moved, 1 where something just did.
		uint aw, ah;
		motion.GetDimensions(aw, ah);
		const float age = BilinearR(motion, input.Pos.xy * ageSize, int2(aw, ah) - 1);
		trust = 1.0 - saturate(age * 2.0);
	}
	trust *= 1.0 - reset;

	const float3 old = clamp(Bilinear(history, previousPos, last), low - tolerance, high + tolerance);
	const float3 steady = lerp(old, value, lerp(1.0, minCurrent, trust));

	STABILIZE_OUTPUT o;
	o.history = float4(steady, 1);
	o.color = float4((outputMode > 0.5) ? steady : Fetch(current, p, last) + steady, 1);
	return o;
}

#endif
