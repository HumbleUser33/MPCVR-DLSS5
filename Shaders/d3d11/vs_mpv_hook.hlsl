// For the passes translated from mpv shaders (Shaders/mpv): a triangle over the
// target, with the normalized position of the pixel centres in TEXCOORD0 ahead of
// the position, the order their pixel shaders read their input in.

struct VS_OUTPUT
{
	float2 Tex : TEXCOORD0;
	float4 Pos : SV_POSITION;
};

VS_OUTPUT main(uint id : SV_VertexID)
{
	VS_OUTPUT output;
	output.Tex = float2((id << 1) & 2, id & 2);
	output.Pos = float4(output.Tex * float2(2, -2) + float2(-1, 1), 0, 1);
	return output;
}
