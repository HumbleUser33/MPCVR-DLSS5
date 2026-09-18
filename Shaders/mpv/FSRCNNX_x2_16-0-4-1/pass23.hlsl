// Copyright (C) 2017-2021 igv
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 3.0 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// Pass 23 of FSRCNNX_x2_16-0-4-1.glsl (sub-band residuals 4), translated to HLSL by Shaders/mpv/mpv_shaders.py
// (glslang, then SPIRV-Cross): do not edit.

cbuffer MpvGlobals : register(b0)
{
    float4 _57_pl_out_size : packoffset(c0);
    float4 _57_pl_input_target : packoffset(c1);
    float4 _57_pl_misc : packoffset(c2);
    float4 _57_pl_tex_size[8] : packoffset(c3);
    float4 _57_pl_tex_shift[8] : packoffset(c11);
    float4 _57_pl_params[8] : packoffset(c19);
};

Texture2D<float4> MODEL1_raw : register(t1);
SamplerState _MODEL1_raw_sampler : register(s1);
Texture2D<float4> MODEL2_raw : register(t2);
SamplerState _MODEL2_raw_sampler : register(s2);
Texture2D<float4> MODEL3_raw : register(t3);
SamplerState _MODEL3_raw_sampler : register(s3);
Texture2D<float4> MODEL4_raw : register(t4);
SamplerState _MODEL4_raw_sampler : register(s4);
Texture2D<float4> FEATURE4_raw : register(t5);
SamplerState _FEATURE4_raw_sampler : register(s5);

static float2 pl_pos;
static float4 pl_out;

struct SPIRV_Cross_Input
{
    float2 pl_pos : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 pl_out : SV_Target0;
};

float4 hook()
{
    float4 res = float4(-0.0254043601453304290771484375f, -0.0337074063718318939208984375f, 0.0073674134910106658935546875f, -0.0777111053466796875f);
    res += mul(MODEL1_raw.SampleLevel(_MODEL1_raw_sampler, (pl_pos + _57_pl_tex_shift[0].xy) + (_57_pl_tex_size[0].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(-0.0152108408510684967041015625f, -0.0290673710405826568603515625f, 0.015214902348816394805908203125f, 0.000118296244181692600250244140625f), float4(-0.0191980488598346710205078125f, 0.0593055188655853271484375f, -0.1341477930545806884765625f, -0.061183042824268341064453125f), float4(-0.0543372966349124908447265625f, 0.02988349460065364837646484375f, 0.1418028175830841064453125f, -0.20279355347156524658203125f), float4(0.0062750629149377346038818359375f, 0.010627991519868373870849609375f, 0.0231198258697986602783203125f, -0.0041293469257652759552001953125f)));
    res += mul(MODEL2_raw.SampleLevel(_MODEL2_raw_sampler, (pl_pos + _57_pl_tex_shift[1].xy) + (_57_pl_tex_size[1].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(0.11406457424163818359375f, -0.16938664019107818603515625f, -0.51530921459197998046875f, -0.1875651776790618896484375f), float4(-0.0672051906585693359375f, 0.62629520893096923828125f, 0.50454962253570556640625f, -0.082727976143360137939453125f), float4(0.016116805374622344970703125f, -0.16741590201854705810546875f, -0.17411220073699951171875f, 0.06558521091938018798828125f), float4(-0.0021867123432457447052001953125f, -0.1953728497028350830078125f, -0.0054047326557338237762451171875f, -0.0233427695930004119873046875f)));
    res += mul(MODEL3_raw.SampleLevel(_MODEL3_raw_sampler, (pl_pos + _57_pl_tex_shift[2].xy) + (_57_pl_tex_size[2].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(0.02934307046234607696533203125f, -0.02397561259567737579345703125f, -0.089479021728038787841796875f, -0.360088884830474853515625f), float4(0.0042837061919271945953369140625f, 0.0316841900348663330078125f, 0.14849065244197845458984375f, 0.0149612911045551300048828125f), float4(0.0249301753938198089599609375f, -0.00222035706974565982818603515625f, -0.0415629185736179351806640625f, 0.03028834052383899688720703125f), float4(0.0327147059142589569091796875f, -0.13601706922054290771484375f, -0.16075931489467620849609375f, -0.607601344585418701171875f)));
    res += mul(MODEL4_raw.SampleLevel(_MODEL4_raw_sampler, (pl_pos + _57_pl_tex_shift[3].xy) + (_57_pl_tex_size[3].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(-0.0638960301876068115234375f, 0.08971838653087615966796875f, 0.062156021595001220703125f, -0.5748775005340576171875f), float4(0.0110273323953151702880859375f, -0.008254588581621646881103515625f, -0.01696989871561527252197265625f, 0.0448103360831737518310546875f), float4(-0.0238428302109241485595703125f, -0.00901554711163043975830078125f, 0.02057706750929355621337890625f, -0.0579754747450351715087890625f), float4(0.0513568818569183349609375f, 0.0123459398746490478515625f, -0.104472868144512176513671875f, 0.13799403607845306396484375f)));
    res += (FEATURE4_raw.SampleLevel(_FEATURE4_raw_sampler, (pl_pos + _57_pl_tex_shift[4].xy) + (_57_pl_tex_size[4].zw * 0.0f.xx), 0.0f) * 1.0f);
    res = max(res, 0.0f.xxxx) + (float4(0.918745517730712890625f, 0.3813512325286865234375f, 1.00339305400848388671875f, 0.11045087873935699462890625f) * min(res, 0.0f.xxxx));
    return res;
}

void frag_main()
{
    pl_out = hook();
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    pl_pos = stage_input.pl_pos;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.pl_out = pl_out;
    return stage_output;
}
