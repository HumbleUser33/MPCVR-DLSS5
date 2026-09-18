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
// Pass 2 of FSRCNNX_x2_16-0-4-1.glsl (feature map 3), translated to HLSL by Shaders/mpv/mpv_shaders.py
// (glslang, then SPIRV-Cross): do not edit.

cbuffer MpvGlobals : register(b0)
{
    float4 _40_pl_out_size : packoffset(c0);
    float4 _40_pl_input_target : packoffset(c1);
    float4 _40_pl_misc : packoffset(c2);
    float4 _40_pl_tex_size[8] : packoffset(c3);
    float4 _40_pl_tex_shift[8] : packoffset(c11);
    float4 _40_pl_params[8] : packoffset(c19);
};

Texture2D<float4> LUMA_raw : register(t1);
SamplerState _LUMA_raw_sampler : register(s1);

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
    float4 res = float4(-0.0074675981886684894561767578125f, -0.011272774077951908111572265625f, -0.007815017364919185638427734375f, 0.0054601472802460193634033203125f);
    res += (float4(0.0058205719105899333953857421875f, 0.010442492552101612091064453125f, 0.01022494770586490631103515625f, -0.0246734209358692169189453125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * (-2.0f).xx), 0.0f) * 1.0f).x);
    res += (float4(-0.0504507981240749359130859375f, -0.026587195694446563720703125f, -0.02115751802921295166015625f, 0.063991092145442962646484375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.08543704450130462646484375f, 0.0320916883647441864013671875f, -0.0444113314151763916015625f, 0.066708080470561981201171875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.071748532354831695556640625f, 0.10076034069061279296875f, 0.0549219138920307159423828125f, 0.0006571425474248826503753662109375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.009647555649280548095703125f, -0.01095334626734256744384765625f, -0.00342939072288572788238525390625f, -0.029865682125091552734375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.06123958528041839599609375f, 0.14387394487857818603515625f, -0.1342615187168121337890625f, 0.00051356083713471889495849609375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.190510928630828857421875f, -0.04875795543193817138671875f, 0.121500037610530853271484375f, -0.3903875648975372314453125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * (-1.0f).xx), 0.0f) * 1.0f).x);
    res += (float4(-0.110576651990413665771484375f, 0.1457407474517822265625f, 0.3190293610095977783203125f, 0.364491522312164306640625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.24068462848663330078125f, -0.100692324340343475341796875f, -0.20598156750202178955078125f, -0.0087168551981449127197265625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.02422956191003322601318359375f, 0.097342289984226226806640625f, 0.0189558453857898712158203125f, 0.04547567665576934814453125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.2777403295040130615234375f, -0.0261268876492977142333984375f, 0.055746294558048248291015625f, 0.11563880741596221923828125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.45936667919158935546875f, -0.574608743190765380859375f, 0.351687967777252197265625f, 0.544148743152618408203125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.1454727351665496826171875f, 0.4347074031829833984375f, -0.564951360225677490234375f, -0.2891949117183685302734375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 0.0f.xx), 0.0f) * 1.0f).x);
    res += (float4(-0.19713576138019561767578125f, -0.05287013947963714599609375f, 0.12341810762882232666015625f, -0.31387150287628173828125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.0748024880886077880859375f, -0.1438121795654296875f, -0.084964908659458160400390625f, -0.00266389339230954647064208984375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.13653361797332763671875f, 0.17828123271465301513671875f, 0.01451558060944080352783203125f, -0.0544562637805938720703125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.16096590459346771240234375f, 0.0399771369993686676025390625f, -0.24712042510509490966796875f, -0.3522113263607025146484375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.001560823176987469196319580078125f, -0.30231380462646484375f, -0.057903222739696502685546875f, 0.0277623422443866729736328125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.0035330341197550296783447265625f, 0.22252224385738372802734375f, 0.25198352336883544921875f, 0.23585736751556396484375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 1.0f.xx), 0.0f) * 1.0f).x);
    res += (float4(0.07970096170902252197265625f, -0.13690672814846038818359375f, -0.003201167099177837371826171875f, -0.107875756919384002685546875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.0340625829994678497314453125f, -0.0491722412407398223876953125f, 0.024866707623004913330078125f, 0.0046607558615505695343017578125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.11011739075183868408203125f, 0.02553499303758144378662109375f, -0.012416091747581958770751953125f, 0.09409673511981964111328125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.12850101292133331298828125f, 0.012611827813088893890380859375f, 0.108074463903903961181640625f, 0.0213258527219295501708984375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.03798313438892364501953125f, 0.1150698363780975341796875f, -0.0470580570399761199951171875f, -0.02745346911251544952392578125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.0675948560237884521484375f, -0.01508153043687343597412109375f, -0.0260353796184062957763671875f, 0.0053928769193589687347412109375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 2.0f.xx), 0.0f) * 1.0f).x);
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
