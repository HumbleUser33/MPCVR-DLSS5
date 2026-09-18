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
// Pass 21 of FSRCNNX_x2_16-0-4-1.glsl (sub-band residuals 2), translated to HLSL by Shaders/mpv/mpv_shaders.py
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
Texture2D<float4> FEATURE2_raw : register(t5);
SamplerState _FEATURE2_raw_sampler : register(s5);

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
    float4 res = float4(-0.013274687342345714569091796875f, 0.073112420737743377685546875f, 0.02966940402984619140625f, -0.0570529401302337646484375f);
    res += mul(MODEL1_raw.SampleLevel(_MODEL1_raw_sampler, (pl_pos + _57_pl_tex_shift[0].xy) + (_57_pl_tex_size[0].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(0.0329166017472743988037109375f, 0.048502184450626373291015625f, -0.4523101150989532470703125f, -0.0322404317557811737060546875f), float4(-0.0172702632844448089599609375f, -0.045602120459079742431640625f, -0.3700943291187286376953125f, 0.070628888905048370361328125f), float4(-0.0264692343771457672119140625f, 0.0057114795781672000885009765625f, 0.107256881892681121826171875f, -0.0367026627063751220703125f), float4(0.0059698573313653469085693359375f, 0.011143281124532222747802734375f, -0.15927334129810333251953125f, -0.014698109589517116546630859375f)));
    res += mul(MODEL2_raw.SampleLevel(_MODEL2_raw_sampler, (pl_pos + _57_pl_tex_shift[1].xy) + (_57_pl_tex_size[1].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(-0.0338360369205474853515625f, -0.14073725044727325439453125f, 0.84409892559051513671875f, 0.2665281593799591064453125f), float4(-0.07660220563411712646484375f, 0.17763777077198028564453125f, 0.682221889495849609375f, -0.23487444221973419189453125f), float4(-0.0140444673597812652587890625f, -0.0049600838683545589447021484375f, -0.11209256947040557861328125f, 0.048541106283664703369140625f), float4(0.0048136659897863864898681640625f, -0.008412645198404788970947265625f, 0.0507854782044887542724609375f, 0.005975441075861454010009765625f)));
    res += mul(MODEL3_raw.SampleLevel(_MODEL3_raw_sampler, (pl_pos + _57_pl_tex_shift[2].xy) + (_57_pl_tex_size[2].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(0.053494386374950408935546875f, -0.0054587344639003276824951171875f, -0.2227865755558013916015625f, 0.023686893284320831298828125f), float4(0.0262969471514225006103515625f, 0.123235918581485748291015625f, 0.12886552512645721435546875f, -0.113882072269916534423828125f), float4(-0.081213943660259246826171875f, -0.04555417597293853759765625f, 0.084914408624172210693359375f, 0.04631443321704864501953125f), float4(-0.1888351738452911376953125f, -0.485054314136505126953125f, -0.15640039741992950439453125f, 0.261883199214935302734375f)));
    res += mul(MODEL4_raw.SampleLevel(_MODEL4_raw_sampler, (pl_pos + _57_pl_tex_shift[3].xy) + (_57_pl_tex_size[3].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(-0.022189714014530181884765625f, 0.003184082917869091033935546875f, 0.14875264465808868408203125f, -0.0075739510357379913330078125f), float4(-0.0042014098726212978363037109375f, 0.01850690506398677825927734375f, -0.097230374813079833984375f, -0.0048297117464244365692138671875f), float4(-0.0396384298801422119140625f, -0.0136362798511981964111328125f, 0.15024353563785552978515625f, 0.00229691504500806331634521484375f), float4(0.098703809082508087158203125f, -0.001194477430544793605804443359375f, -0.3419188559055328369140625f, 0.0331442765891551971435546875f)));
    res += (FEATURE2_raw.SampleLevel(_FEATURE2_raw_sampler, (pl_pos + _57_pl_tex_shift[4].xy) + (_57_pl_tex_size[4].zw * 0.0f.xx), 0.0f) * 1.0f);
    res = max(res, 0.0f.xxxx) + (float4(1.10184824466705322265625f, 0.93374729156494140625f, 0.0558062084019184112548828125f, 1.00614261627197265625f) * min(res, 0.0f.xxxx));
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
