// --tdetect: the shader motion detector, scored against the perfect map.
//
// Included by harness.cpp after the other temporal suites, whose helpers it uses.
// Compiles Shaders/d3d11/ps_dlss_motion.hlsl -- the file the renderer will embed --
// runs its passes through a blit shaped like the renderer's TextureBlt11, feeds the
// mask to the network and measures what --toracle does.
//
// Measured so far (RTX 3050, default strengths):
//   - consecutive frames only: fine on an object at 3 px/frame and on a fade, but a
//     slow object (0.5 px/frame) smeared +53 % and a slow pan lagged +57 %
//   - also against four frames back: the slow object came within +7 %
//   - also against eight frames back: the slow object matches the perfect map; the
//     pan still lags on edges, because the noise floor learns the pan
//   - a slower floor rise changed nothing on a pan that starts at the first frame:
//     the floor never sees a static picture, so it starts inflated
//   - the floor measured on consecutive frames only: static backgrounds at 0.33x
//     and 0.30x, the slow object as good as the perfect map, slow pans within +3 % of it
// Real streams also repeat pictures -- variable frame rates, 25p stored as 50p, and
// the renderer feeding the last frame again after a settings change. A repeat
// differs by nothing at all, which the floor must not learn: S9 shows every picture
// twice. With the guard that ignores such frames the detector matches the perfect
// map; without it the floor collapses and the background flickers more than with
// no history at all (1.14x).
// Threshold 2.5 was never worse than 3 in any sequence, so the renderer uses 2.5
// (Source/DLSS/DlssMotionMask.cpp). --tbench on an RTX 3050: 0.31 ms of GPU time
// at 1080p, 1.08 ms at 2160p.

#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#include "DLSS/DlssMotionMask.h"

namespace temporal {

struct Vertex11 { float x, y, z, u, v; };   // the renderer's VERTEX layout

struct Target {
	CComPtr<ID3D11Texture2D> tex;
	CComPtr<ID3D11ShaderResourceView> srv;
	CComPtr<ID3D11RenderTargetView> rtv;
	UINT w = 0, h = 0, levels = 1;
};

static bool MakeTarget(ID3D11Device* dev, UINT w, UINT h, bool mips, Target& t,
                       DXGI_FORMAT format = DXGI_FORMAT_R16_FLOAT)
{
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w;
	d.Height = h;
	d.MipLevels = mips ? 0 : 1;
	d.ArraySize = 1;
	d.Format = format;
	d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_DEFAULT;
	d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	d.MiscFlags = mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;

	t = Target{};
	if (FAILED(dev->CreateTexture2D(&d, nullptr, &t.tex))) {
		return false;
	}
	t.tex->GetDesc(&d);
	t.w = w;
	t.h = h;
	t.levels = d.MipLevels;

	D3D11_RENDER_TARGET_VIEW_DESC rd = {};
	rd.Format = d.Format;
	rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	return SUCCEEDED(dev->CreateShaderResourceView(t.tex, nullptr, &t.srv))
		&& SUCCEEDED(dev->CreateRenderTargetView(t.tex, &rd, &t.rtv));
}

class CShaderDetector
{
public:
	struct Tuning {
		float strength      = 0.25f;
		float threshold     = 3.0f;
		float softness      = 1.0f;
		float decay         = 1.0f / 8;
		float minFloor      = 0.0015f;
		float maxFloor      = 0.02f;
		float floorRise     = 0.005f;  // share of a higher noise floor accepted per frame
		float fadeThreshold = 0.0015f;
		int   baselines     = 3;       // 1: previous frame; 2: and four back; 3: and eight back
		bool  repeatGuard   = true;    // a repeated picture leaves the noise floor alone
	};

	bool Init(ID3D11Device* dev, UINT w, UINT h, std::string& error);
	void Reset(ID3D11DeviceContext* ctx);
	void Run(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* pColour,
	         ID3D11RenderTargetView* pMask, UINT maskW, UINT maskH, const Tuning& tuning);

	float NoiseFloor() const { return m_noise; }
	float Protect() const { return m_protect; }
	int   Unchanged() const { return m_unchanged; }

	// Pipeline objects shaped like the renderer's, lent to CDlssMotionMask (--tport).
	ID3D11VertexShader* VertexShader() const { return m_vs; }
	ID3D11InputLayout*  InputLayout() const { return m_layout; }
	ID3D11SamplerState* SamplerPoint() const { return m_point; }
	ID3D11SamplerState* SamplerLinear() const { return m_linear; }

private:
	void Draw(ID3D11DeviceContext* ctx, ID3D11PixelShader* ps, ID3D11RenderTargetView* rtv, UINT w, UINT h,
	          ID3D11ShaderResourceView* srv0, ID3D11ShaderResourceView* srv1,
	          ID3D11ShaderResourceView* srv2 = nullptr, ID3D11ShaderResourceView* srv3 = nullptr);
	void ReadStats(ID3D11DeviceContext* ctx, const Tuning& tuning);

	static constexpr int   kHistory = 9;    // this frame and the eight before it
	static constexpr UINT  kDiffMip = 2;
	static constexpr int   kRelearnFrames = 12;
	static constexpr float kRelearnRise = 0.2f;

	CComPtr<ID3D11VertexShader>    m_vs;
	CComPtr<ID3D11InputLayout>     m_layout;
	CComPtr<ID3D11PixelShader>     m_psLuma;
	CComPtr<ID3D11PixelShader>     m_psDiff[3];   // one, two or three baselines
	CComPtr<ID3D11PixelShader>     m_psAge;
	CComPtr<ID3D11PixelShader>     m_psMask;
	CComPtr<ID3D11SamplerState>    m_point;
	CComPtr<ID3D11SamplerState>    m_linear;
	CComPtr<ID3D11RasterizerState> m_raster;
	CComPtr<ID3D11Buffer>          m_vb;
	CComPtr<ID3D11Buffer>          m_cb;
	Target m_luma[kHistory];
	Target m_age[2];
	Target m_diff;                          // red: motion, green: consecutive frames
	CComPtr<ID3D11Texture2D> m_stageDiff;   // frame differences, mip 2
	CComPtr<ID3D11Texture2D> m_stageMean;   // mean luma, last mip
	int   m_head = 0;                       // m_luma slot written this frame
	int   m_ageCur = 0;
	int   m_frames = 0;                     // frames run since the last reset
	int   m_relearn = 0;                    // frames left of fast noise-floor learning
	int   m_unchanged = 0;                  // frames taken for a repeated picture
	bool  m_haveStats = false;
	float m_noise = 0;
	float m_protect = 1;
	float m_prevMean = -1;
};

bool CShaderDetector::Init(ID3D11Device* dev, UINT w, UINT h, std::string& error)
{
	const wchar_t* psFile = L"..\\..\\Shaders\\d3d11\\ps_dlss_motion.hlsl";
	const wchar_t* vsFile = L"..\\..\\Shaders\\d3d11\\vs_simple.hlsl";

	CComPtr<ID3DBlob> code, errors;
	HRESULT hr = D3DCompileFromFile(vsFile, nullptr, nullptr, "main", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
	if (FAILED(hr)) {
		error = errors ? std::string((const char*)errors->GetBufferPointer(), errors->GetBufferSize()) : "vs_simple.hlsl not found";
		return false;
	}
	if (FAILED(dev->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &m_vs))) {
		error = "CreateVertexShader failed";
		return false;
	}
	const D3D11_INPUT_ELEMENT_DESC layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};
	if (FAILED(dev->CreateInputLayout(layout, (UINT)std::size(layout), code->GetBufferPointer(), code->GetBufferSize(), &m_layout))) {
		error = "CreateInputLayout failed";
		return false;
	}

	struct Variant { const char* pass; const char* baselines; ID3D11PixelShader** out; };
	const Variant variants[] = {
		{ "1", "1", &m_psLuma },
		{ "2", "1", &m_psDiff[0] }, { "2", "2", &m_psDiff[1] }, { "2", "3", &m_psDiff[2] },
		{ "3", "1", &m_psAge },
		{ "4", "1", &m_psMask },
	};
	for (const Variant& v : variants) {
		const D3D_SHADER_MACRO defines[] = { { "PASS", v.pass }, { "BASELINES", v.baselines }, { nullptr, nullptr } };
		code.Release();
		errors.Release();
		hr = D3DCompileFromFile(psFile, defines, nullptr, "main", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
		if (FAILED(hr)) {
			error = std::string("PASS ") + v.pass + ": "
				+ (errors ? std::string((const char*)errors->GetBufferPointer(), errors->GetBufferSize()) : "ps_dlss_motion.hlsl not found");
			return false;
		}
		if (FAILED(dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, v.out))) {
			error = "CreatePixelShader failed";
			return false;
		}
	}

	// Samplers exactly as CDX11VideoProcessor::SetDevice creates them.
	D3D11_SAMPLER_DESC sd = {};
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sd.MaxLOD = D3D11_FLOAT32_MAX;
	dev->CreateSamplerState(&sd, &m_point);
	sd.Filter = D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
	dev->CreateSamplerState(&sd, &m_linear);

	D3D11_RASTERIZER_DESC rd = {};
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_NONE;
	rd.DepthClipEnable = TRUE;
	dev->CreateRasterizerState(&rd, &m_raster);

	const Vertex11 quad[4] = {
		{ -1, -1, 0, 0, 1 }, { -1, 1, 0, 0, 0 }, { 1, -1, 0, 1, 1 }, { 1, 1, 0, 1, 0 },
	};
	D3D11_BUFFER_DESC bd = { sizeof(quad), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
	D3D11_SUBRESOURCE_DATA init = { quad, 0, 0 };
	dev->CreateBuffer(&bd, &init, &m_vb);
	D3D11_BUFFER_DESC cbd = { 32, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
	dev->CreateBuffer(&cbd, nullptr, &m_cb);
	if (!m_point || !m_linear || !m_raster || !m_vb || !m_cb) {
		error = "pipeline state objects";
		return false;
	}

	const UINT w4 = w / 4, h4 = h / 4;
	bool targetsOk = MakeTarget(dev, w4, h4, false, m_age[0]) && MakeTarget(dev, w4, h4, false, m_age[1])
		&& MakeTarget(dev, w4, h4, true, m_diff, DXGI_FORMAT_R16G16_FLOAT);
	for (int i = 0; i < kHistory && targetsOk; i++) {
		targetsOk = MakeTarget(dev, w4, h4, true, m_luma[i]);
	}
	if (!targetsOk) {
		error = "quarter-resolution targets";
		return false;
	}

	D3D11_TEXTURE2D_DESC st = {};
	st.Width = std::max(w4 >> kDiffMip, 1u);
	st.Height = std::max(h4 >> kDiffMip, 1u);
	st.MipLevels = 1;
	st.ArraySize = 1;
	st.Format = DXGI_FORMAT_R16G16_FLOAT;
	st.SampleDesc.Count = 1;
	st.Usage = D3D11_USAGE_STAGING;
	st.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	if (FAILED(dev->CreateTexture2D(&st, nullptr, &m_stageDiff))) {
		error = "difference staging texture";
		return false;
	}
	st.Width = 1;
	st.Height = 1;
	st.Format = DXGI_FORMAT_R16_FLOAT;
	if (FAILED(dev->CreateTexture2D(&st, nullptr, &m_stageMean))) {
		error = "mean staging texture";
		return false;
	}
	return true;
}

void CShaderDetector::Reset(ID3D11DeviceContext* ctx)
{
	// Everything counts as moving until the detector has seen a few frames.
	const FLOAT one[4] = { 1, 1, 1, 1 };
	ctx->ClearRenderTargetView(m_age[0].rtv, one);
	ctx->ClearRenderTargetView(m_age[1].rtv, one);
	m_head = 0;
	m_ageCur = 0;
	m_frames = 0;
	m_relearn = 0;
	m_unchanged = 0;
	m_haveStats = false;
	m_noise = 0;
	m_protect = 1;
	m_prevMean = -1;
}

void CShaderDetector::Draw(ID3D11DeviceContext* ctx, ID3D11PixelShader* ps, ID3D11RenderTargetView* rtv, UINT w, UINT h,
                           ID3D11ShaderResourceView* srv0, ID3D11ShaderResourceView* srv1,
                           ID3D11ShaderResourceView* srv2, ID3D11ShaderResourceView* srv3)
{
	const D3D11_VIEWPORT vp = { 0, 0, (FLOAT)w, (FLOAT)h, 0, 1 };
	const UINT stride = sizeof(Vertex11), offset = 0;
	ID3D11Buffer* vb = m_vb;
	ID3D11Buffer* cb = m_cb;
	ID3D11SamplerState* samplers[2] = { m_point, m_linear };
	ID3D11ShaderResourceView* srvs[4] = { srv0, srv1, srv2, srv3 };

	ctx->IASetInputLayout(m_layout);
	ctx->OMSetRenderTargets(1, &rtv, nullptr);
	ctx->RSSetViewports(1, &vp);
	ctx->RSSetState(m_raster);
	ctx->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
	ctx->VSSetShader(m_vs, nullptr, 0);
	ctx->PSSetShader(ps, nullptr, 0);
	ctx->PSSetShaderResources(0, 4, srvs);
	ctx->PSSetSamplers(0, 2, samplers);
	ctx->PSSetConstantBuffers(0, 1, &cb);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
	ctx->Draw(4, 0);

	ID3D11ShaderResourceView* none[4] = {};
	ctx->PSSetShaderResources(0, 4, none);
	ID3D11RenderTargetView* noTarget = nullptr;
	ctx->OMSetRenderTargets(1, &noTarget, nullptr);
}

// Statistics of the previous frame: the renderer will read them without waiting,
// so they are one frame late here too.
void CShaderDetector::ReadStats(ID3D11DeviceContext* ctx, const Tuning& tuning)
{
	if (!m_haveStats) {
		m_protect = 1;
		return;
	}

	D3D11_MAPPED_SUBRESOURCE mr = {};
	std::vector<float> values;
	if (SUCCEEDED(ctx->Map(m_stageDiff, 0, D3D11_MAP_READ, 0, &mr))) {
		D3D11_TEXTURE2D_DESC d = {};
		m_stageDiff->GetDesc(&d);
		std::vector<float> row(2 * (size_t)d.Width);
		values.reserve((size_t)d.Width * d.Height);
		for (UINT y = 0; y < d.Height; y++) {
			DirectX::PackedVector::XMConvertHalfToFloatStream(row.data(), sizeof(float),
				(const HALF*)((const BYTE*)mr.pData + (size_t)mr.RowPitch * y), sizeof(HALF), row.size());
			for (UINT x = 0; x < d.Width; x++) {
				values.push_back(row[2 * (size_t)x + 1]);   // green: consecutive frames only
			}
		}
		ctx->Unmap(m_stageDiff, 0);
	}

	float mean = m_prevMean;
	if (SUCCEEDED(ctx->Map(m_stageMean, 0, D3D11_MAP_READ, 0, &mr))) {
		mean = DirectX::PackedVector::XMConvertHalfToFloat(*(const HALF*)mr.pData);
		ctx->Unmap(m_stageMean, 0);
	}

	const float change = (m_prevMean >= 0) ? std::abs(mean - m_prevMean) : 1.0f;
	m_protect = std::clamp((change - tuning.fadeThreshold) / tuning.fadeThreshold, 0.0f, 1.0f);
	m_prevMean = mean;

	// A change of the whole picture -- a fade, a cut -- can bring a different noise
	// level with it, so the floor may follow quickly for a moment.
	if (m_protect > 0.5f) {
		m_relearn = kRelearnFrames;
	}

	// The very first difference compares the picture with copies of itself and
	// would read as no noise at all.
	if (m_frames <= 2) {
		m_protect = 1;
	} else if (!values.empty()) {
		// A repeated picture differs by nothing, even where things move. Nearly all
		// of it changing less than the lowest floor means a repeat; learning that
		// as the noise level would flag the grain of every following frame as
		// motion.
		const size_t k95 = values.size() * 19 / 20;
		std::nth_element(values.begin(), values.begin() + k95, values.end());
		const bool unchanged = values[k95] < tuning.minFloor;
		if (unchanged) {
			m_unchanged++;
		}

		if (!unchanged || !tuning.repeatGuard) {
			// A low percentile is the typical difference where nothing moves, as
			// long as a fifth of the picture is still.
			const size_t k = values.size() / 5;
			std::nth_element(values.begin(), values.begin() + k, values.end());
			const float current = std::clamp(values[k], tuning.minFloor, tuning.maxFloor);
			// Down at once, up slowly: sustained motion must not teach the detector
			// that motion is noise.
			const float rise = (m_relearn > 0) ? kRelearnRise : tuning.floorRise;
			m_noise = (m_noise <= 0 || current < m_noise) ? current : m_noise + (current - m_noise) * rise;
		}
	}
	if (m_relearn > 0) {
		m_relearn--;
	}
}

void CShaderDetector::Run(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* pColour,
                          ID3D11RenderTargetView* pMask, UINT maskW, UINT maskH, const Tuning& tuning)
{
	Target& lumaCur  = m_luma[m_head];
	Target& lumaPrev = m_luma[(m_head + kHistory - 1) % kHistory];
	Target& luma4    = m_luma[(m_head + kHistory - 4) % kHistory];
	Target& luma8    = m_luma[(m_head + kHistory - 8) % kHistory];
	Target& ageCur   = m_age[m_ageCur];
	Target& agePrev  = m_age[1 - m_ageCur];

	Draw(ctx, m_psLuma, lumaCur.rtv, lumaCur.w, lumaCur.h, pColour, nullptr);
	if (m_frames == 0) {
		for (int i = 0; i < kHistory; i++) {
			if (i != m_head) {
				ctx->CopyResource(m_luma[i].tex, lumaCur.tex);
			}
		}
	}
	m_frames++;

	const int variant = std::clamp(tuning.baselines, 1, 3) - 1;
	Draw(ctx, m_psDiff[variant], m_diff.rtv, m_diff.w, m_diff.h, lumaCur.srv, lumaPrev.srv, luma4.srv, luma8.srv);

	ReadStats(ctx, tuning);
	ctx->GenerateMips(m_diff.srv);
	ctx->GenerateMips(lumaCur.srv);
	ctx->CopySubresourceRegion(m_stageDiff, 0, 0, 0, 0, m_diff.tex, kDiffMip, nullptr);
	ctx->CopySubresourceRegion(m_stageMean, 0, 0, 0, 0, lumaCur.tex, lumaCur.levels - 1, nullptr);
	m_haveStats = true;

	D3D11_MAPPED_SUBRESOURCE mr = {};
	if (SUCCEEDED(ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mr))) {
		const float constants[8] = {
			m_noise > 0 ? m_noise : tuning.maxFloor, tuning.threshold, tuning.softness, tuning.decay,
			m_protect, tuning.strength, 0, 0
		};
		memcpy(mr.pData, constants, sizeof(constants));
		ctx->Unmap(m_cb, 0);
	}

	Draw(ctx, m_psAge, ageCur.rtv, ageCur.w, ageCur.h, m_diff.srv, agePrev.srv);
	Draw(ctx, m_psMask, pMask, maskW, maskH, ageCur.srv, nullptr);

	m_head = (m_head + 1) % kHistory;
	m_ageCur = 1 - m_ageCur;
}

// A static noisy picture whose brightness falls by a fixed fraction per frame.
static void RenderFadeFrame(const Image& bg, int t, float gain, std::vector<float>& rgba)
{
	const int W = bg.W, H = bg.H;
	const int bw = (W + 7) / 8;
	Rng r(0x2468ACEu + (uint32_t)t * 7919u);
	std::vector<float> blockDc((size_t)bw * ((H + 7) / 8));
	for (float& d : blockDc) {
		d = (r.Uniform() * 2 - 1) * (1.5f / 255);
	}
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			float* o = &rgba[4 * ((size_t)y * W + x)];
			const float* p = bg.At(x, y);
			const float n = blockDc[(size_t)(y / 8) * bw + x / 8] + r.Gauss() * (1.5f / 255);
			for (int c = 0; c < 3; c++) {
				o[c] = std::round(std::clamp(p[c] * gain + n, 0.0f, 1.0f) * 127.5f) / 127.5f;
			}
			o[3] = 1.0f;
		}
	}
}

struct DetectSequence {
	const char* name;
	int   kind;      // 0 object over a static background, 1 fade, 2 pan, 3 pan after a static lead-in
	float vx, vy;    // object or pan motion, pixels per picture
	int   repeat;    // each picture is shown this many times
};

enum DetectMode { DetectNone, DetectOracle, DetectShader, DetectRenderer };   // DetectRenderer: Source/DLSS/DlssMotionMask.cpp

struct DetectConfig {
	const char* name;
	bool  noHistory;
	int   mode;         // DetectMode
	float strength;
	int   baselines;    // detector: 1, 2 or 3 frame differences
	float threshold;    // detector: motion starts at this many noise floors
	float floorRise;    // detector: share of a higher noise floor accepted per frame
	float decay;        // detector: protection lost per frame
	bool  repeatGuard;  // detector: a repeated picture leaves the noise floor alone
};

// port (--tport): instead of comparing tunings, check that the renderer's own class,
// CDlssMotionMask with its shaders embedded as the filter embeds them, gives what the
// harness detector gives at the same settings.
static int RunDetect(ID3D11Device* dev, ID3D11DeviceContext* ctx, CDlssNR& dlss, int frames, bool strong, bool port)
{
	const int W = 1920, H = 1080;
	const int w2 = W / 2, h2 = H / 2;
	const int gw = w2 / 4, gh = h2 / 4;
	const int warm = 16;          // past the detector's own start-up
	const int trailFrames = 8;
	const int leadIn = 32;        // static frames before the pan of S8
	const int OW = 480, OH = 320;
	const float OX = 200, OY = 300;

	static const DetectSequence seqs[] = {
		{ "S4 object moving (3, 1) px/frame over a noisy static background",   0, 3.0f, 1.0f, 1 },
		{ "S5 object moving (0.5, 0) px/frame over a noisy static background", 0, 0.5f, 0.0f, 1 },
		{ "S6 noisy static picture fading out 0.4 % per frame",                1, 0.0f, 0.0f, 1 },
		{ "S7 noisy picture panning (0.3, 0) px/frame from the first frame",   2, 0.3f, 0.0f, 1 },
		{ "S8 the same pan after 32 static frames",                            3, 0.3f, 0.0f, 1 },
		{ "S9 S5 with every picture shown twice, as repeated frames",          0, 0.5f, 0.0f, 2 },
	};
	static const DetectConfig cfgs[] = {
		{ "no history",            true,  DetectNone,   0.0f,  0, 0.0f, 0.0f,   0.0f,     false },
		{ "perfect map, bg 0.25",  false, DetectOracle, 0.25f, 0, 0.0f, 0.0f,   0.0f,     false },
		{ "0.25, thr 3",           false, DetectShader, 0.25f, 3, 3.0f, 0.005f, 1.0f / 8, true  },
		{ "0.25, thr 2.5",         false, DetectShader, 0.25f, 3, 2.5f, 0.005f, 1.0f / 8, true  },
		{ "0.35, thr 3",           false, DetectShader, 0.35f, 3, 3.0f, 0.005f, 1.0f / 8, true  },
		{ "0.25, thr 3, no guard", false, DetectShader, 0.25f, 3, 3.0f, 0.005f, 1.0f / 8, false },
	};
	static const DetectConfig portCfgs[] = {
		{ "no history",            true,  DetectNone,     0.0f,  0, 0.0f, 0.0f,   0.0f,     false },
		{ "perfect map, bg 0.25",  false, DetectOracle,   0.25f, 0, 0.0f, 0.0f,   0.0f,     false },
		{ "harness, 0.25 thr 2.5", false, DetectShader,   0.25f, 3, 2.5f, 0.005f, 1.0f / 8, true  },
		{ "renderer class, 0.25",  false, DetectRenderer, 0.25f, 0, 0.0f, 0.0f,   0.0f,     false },
	};
	const DetectConfig* const cfgList = port ? portCfgs : cfgs;
	const size_t cfgCount = port ? std::size(portCfgs) : std::size(cfgs);

	Head("Temporal suite: shader motion detector");
	Tex2D_t texIn, texOut, texMask;
	Check(MakeSharedPair(dev, texIn, texOut, W, H), "colour pair 1920x1080 RGBA16F");
	Check(SUCCEEDED(texMask.CheckCreate(dev, DXGI_FORMAT_R8_UNORM, W, H, Tex2D_DefaultShaderRTargetUAVShared)), "ControlMask R8");
	if (!texIn.pTexture || !texOut.pTexture || !texMask.pTexture || !texIn.pShaderResource) {
		return 1;
	}
	CComPtr<ID3D11RenderTargetView> maskTarget;
	Check(SUCCEEDED(dev->CreateRenderTargetView(texMask.pTexture, nullptr, &maskTarget)), "ControlMask render target");

	CShaderDetector detector;
	std::string error;
	const bool detectorOk = detector.Init(dev, W, H, error);
	Check(detectorOk, "detector shaders compiled, targets created");
	if (!detectorOk) {
		printf("  %s\n", error.c_str());
		return 1;
	}
	CDlssMotionMask rendererMask;
	if (port) {
		const bool rendererOk = SUCCEEDED(rendererMask.Create(dev, W, H));
		Check(rendererOk, "renderer detector created from its embedded shaders");
		if (!rendererOk) {
			return 1;
		}
	}

	D3D11_TEXTURE2D_DESC sd = texOut.desc;
	sd.Usage = D3D11_USAGE_STAGING;
	sd.BindFlags = 0;
	sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	sd.MiscFlags = 0;
	CComPtr<ID3D11Texture2D> stage;
	Check(SUCCEEDED(dev->CreateTexture2D(&sd, nullptr, &stage)), "colour readback staging");
	D3D11_TEXTURE2D_DESC md = texMask.desc;
	md.Usage = D3D11_USAGE_STAGING;
	md.BindFlags = 0;
	md.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	md.MiscFlags = 0;
	CComPtr<ID3D11Texture2D> stageMask;
	Check(SUCCEEDED(dev->CreateTexture2D(&md, nullptr, &stageMask)), "mask readback staging");
	if (!stage || !stageMask || !maskTarget) {
		return 1;
	}

	CDlssNR::Params params;
	params.iStyle = 0;
	params.iPreset = 0;
	params.fIntensity = 1.50f;
	params.fLocalTone = strong ? 1.0f : 0.30f;
	params.fLocalStructure = strong ? 1.0f : 0.50f;
	params.fSkinStructure = 0.90f;
	params.bUseAutoMask = true;

	printf("  building the textures...\n");
	const Image bgImage = MakeBase(W, H, 20260913u);
	const Image objImage = MakeBase(W, H, 777u);

	g_report = fopen(port ? "temporal_results_port.txt" : "temporal_results_detect.txt", "w");
	if (port) {
		Out("\nThe renderer's detector (CDlssMotionMask) against the harness detector at the same settings: the\n");
		Out("two rows should read the same. The renderer class draws under the default rasterizer state, as in the filter.\n");
	}
	Out("\nDLSS 5 NR, ControlMask from the shader motion detector -- %d frames per run, first %d skipped, %s strengths\n",
	    frames, warm, strong ? "strong (tone 1.00, structure 1.00)" : "default (tone 0.30, structure 0.50)");
	Out("bg LF = frame-to-frame change of blurred luma on the background (S4, S5, S9) or the whole picture (S6 to\n");
	Out("S8, warped by the pan); dev = mean |Y - Y(no history)| at 1/8 resolution -- smear on the object and its\n");
	Out("trail, lag on a fade or a pan; edge dev = the same over the whole picture, weighted by local contrast\n");
	Out("(where lag shows). mask = mean ControlMask on the background and on the object. Detector runs compare\n");
	Out("against the previous frame, four back and eight back; the noise floor is measured on the previous frame.\n");
	Out("rep = frames the detector took for a repeated picture, which leave the floor alone unless 'no guard'.\n");
	Out("S8 is scored from its first moving frame; S9 shows every picture twice.\n");

	std::vector<float> rgba(4 * (size_t)W * H);
	std::vector<HALF> half(4 * (size_t)W * H);
	std::vector<uint8_t> maskBytes((size_t)W * H);
	std::vector<float> yIn, yOut, yInPrev, yOutPrev, bIn, bOut, bInPrev, bOutPrev;
	std::vector<float> lowres((size_t)gw * gh);

	for (const DetectSequence& s : seqs) {
		// S8 is scored on its pan only, from the first frame that moves: its static
		// lead-in would dilute the lag with stabilisation both runs share.
		const int start = (s.kind == 3) ? leadIn + 1 : warm;
		const int repeat = std::max(s.repeat, 1);
		std::vector<float> reference;
		double inBgLF = 0;
		int inCount = 0;

		Out("\n%s\n", s.name);
		Out("  %-22s %8s %8s %9s %9s %9s %9s %7s %7s %7s %5s\n",
		    "config", "bg LF", "ratio", "obj dev", "trail dev", "bg dev", "edge dev", "mask bg", "mask ob", "floor", "rep");

		for (size_t ci = 0; ci < cfgCount; ci++) {
			const DetectConfig& c = cfgList[ci];
			// Without repeats the guard never acts, and an unguarded run would only
			// measure the guarded one again.
			if (c.mode == DetectShader && !c.repeatGuard && repeat == 1) {
				continue;
			}
			const bool isReference = (ci == 0);
			dlss.ReleaseFeature();

			CDlssNR::Guides g;
			if (c.mode == DetectRenderer) {
				g.pControlMask = rendererMask.GetMask();
			} else if (c.mode != DetectNone) {
				g.pControlMask = texMask.pTexture;
			}
			const bool guidesOk = dlss.SetGuides(g);
			const bool featureOk = guidesOk && dlss.CreateFeature(texIn.pTexture, texOut.pTexture, W, H, params);
			if (!guidesOk || !featureOk) {
				Out("  %-22s could not set up: %S\n", c.name, dlss.GetStatusLine().c_str());
				g_failures++;
				continue;
			}
			dlss.RequestReset();
			params.bNoHistory = c.noHistory;
			if (c.mode == DetectShader) {
				detector.Reset(ctx);
			} else if (c.mode == DetectRenderer) {
				rendererMask.Reset();
			}
			CShaderDetector::Tuning tuning;
			tuning.strength = c.strength;
			tuning.baselines = c.baselines;
			tuning.threshold = c.threshold;
			tuning.floorRise = c.floorRise;
			tuning.decay = c.decay;
			tuning.repeatGuard = c.repeatGuard;

			double bgLF = 0, objDev = 0, trailDev = 0, bgDev = 0, edgeDev = 0, maskBg = 0, maskObj = 0, floorSum = 0;
			int count = 0, failed = 0;

			for (int t = 0; t < frames; t++) {
				const int tc = t / repeat;   // the picture shown at frame t
				const bool panning = (s.kind == 2) || (s.kind == 3 && t > leadIn);

				// Geometry: only S4, S5 and S9 have an object; elsewhere the whole picture is "background".
				Rect objFull = { 0, 0, 0, 0 }, sweepFull = { 0, 0, 0, 0 };
				if (s.kind == 0) {
					const float ox = OX + s.vx * tc, oy = OY + s.vy * tc;
					const int tp = std::max(tc - trailFrames, 0);
					const float oxp = OX + s.vx * tp, oyp = OY + s.vy * tp;
					objFull = { (int)std::floor(ox), (int)std::floor(oy), (int)std::ceil(ox) + OW, (int)std::ceil(oy) + OH };
					const Rect prevFull = { (int)std::floor(oxp), (int)std::floor(oyp), (int)std::ceil(oxp) + OW, (int)std::ceil(oyp) + OH };
					sweepFull = Union(objFull, prevFull);
					RenderOracleFrame(bgImage, objImage, ox, oy, OW, OH, tc, rgba);
				} else if (s.kind == 1) {
					RenderFadeFrame(bgImage, t, std::max(1.0f - 0.004f * t, 0.0f), rgba);
				} else {
					// RenderFrame shifts by speed * t and seeds the noise from t; for the
					// lead-in, scale the speed so the shift counts only the moving frames.
					const int moving = (s.kind == 3) ? std::max(t - leadIn, 0) : t;
					const float scale = t > 0 ? (float)moving / t : 0.0f;
					const Sequence pan = { "", s.vx * scale, s.vy * scale, true };
					RenderFrame(bgImage, pan, t, rgba);
				}

				DirectX::PackedVector::XMConvertFloatToHalfStream(half.data(), sizeof(HALF), rgba.data(), sizeof(float), rgba.size());
				ctx->UpdateSubresource(texIn.pTexture, 0, nullptr, half.data(), 4 * sizeof(HALF) * W, 0);

				if (c.mode == DetectOracle) {
					const bool moves = (s.kind == 1) || panning;
					std::fill(maskBytes.begin(), maskBytes.end(), (uint8_t)(moves ? 255 : std::lround(c.strength * 255)));
					if (s.kind == 0) {
						const Rect keep = Expand(sweepFull, 16, W, H);
						for (int y = keep.y0; y < keep.y1; y++) {
							memset(&maskBytes[(size_t)y * W + keep.x0], 255, (size_t)(keep.x1 - keep.x0));
						}
					}
					ctx->UpdateSubresource(texMask.pTexture, 0, nullptr, maskBytes.data(), W, 0);
				} else if (c.mode == DetectShader) {
					detector.Run(ctx, texIn.pShaderResource, maskTarget, W, H, tuning);
				} else if (c.mode == DetectRenderer) {
					// The filter never sets a rasterizer state, and the harness
					// detector leaves CULL_NONE bound: back to the default.
					ctx->RSSetState(nullptr);
					rendererMask.Process(ctx, texIn.pShaderResource, c.strength,
						detector.InputLayout(), detector.VertexShader(), detector.SamplerPoint(), detector.SamplerLinear());
				}

				if (!dlss.Evaluate(params)) {
					if (++failed <= 2) {
						Out("  %-22s frame %d: Evaluate failed -- %S\n", c.name, t, dlss.GetStatusLine().c_str());
					}
					continue;
				}
				ctx->CopyResource(stage, texOut.pTexture);
				if (!ReadLumaHalf(ctx, stage, W, H, yOut)) {
					failed++;
					continue;
				}
				LumaHalfFromFloat(rgba, W, H, yIn);
				Blur(yOut, w2, h2, 4, bOut);

				for (int y = 0; y < gh; y++) {
					for (int x = 0; x < gw; x++) {
						float acc = 0;
						for (int yy = 0; yy < 4; yy++) {
							for (int xx = 0; xx < 4; xx++) {
								acc += yOut[(size_t)(y * 4 + yy) * w2 + x * 4 + xx];
							}
						}
						lowres[(size_t)y * gw + x] = acc / 16;
					}
				}

				const Rect guardHalf = (s.kind == 0) ? Expand(Scale(sweepFull, 2), 16, w2, h2) : Rect{ 0, 0, 0, 0 };
				const float dxHalf = panning ? s.vx / 2 : 0.0f;
				const float dyHalf = panning ? s.vy / 2 : 0.0f;

				if (t >= start) {
					bgLF += (s.kind >= 2)
						? WarpDiff(bOut, bOutPrev, w2, h2, dxHalf, dyHalf, 24, 0, w2)
						: OutsideDiff(bOut, bOutPrev, w2, h2, guardHalf, 24);

					if (c.mode != DetectNone) {
						ctx->CopyResource(stageMask, c.mode == DetectRenderer ? rendererMask.GetMask() : texMask.pTexture.p);
						D3D11_MAPPED_SUBRESOURCE mr = {};
						if (SUCCEEDED(ctx->Map(stageMask, 0, D3D11_MAP_READ, 0, &mr))) {
							const Rect guardFull = (s.kind == 0) ? Expand(sweepFull, 32, W, H) : Rect{ 0, 0, 0, 0 };
							double sb = 0, so = 0;
							long long nb = 0, no = 0;
							for (int y = 0; y < H; y += 4) {
								const uint8_t* row = (const uint8_t*)mr.pData + (size_t)mr.RowPitch * y;
								for (int x = 0; x < W; x += 4) {
									if (s.kind == 0 && Inside(objFull, x, y)) { so += row[x]; no++; }
									else if (!Inside(guardFull, x, y)) { sb += row[x]; nb++; }
								}
							}
							ctx->Unmap(stageMask, 0);
							maskBg += nb ? sb / nb / 255.0 : 0;
							maskObj += no ? so / no / 255.0 : 0;
						}
					}
					if (c.mode == DetectShader) {
						floorSum += detector.NoiseFloor();
					} else if (c.mode == DetectRenderer) {
						floorSum += rendererMask.NoiseFloor();
					}

					if (isReference) {
						Blur(yIn, w2, h2, 4, bIn);
						inBgLF += (s.kind >= 2)
							? WarpDiff(bIn, bInPrev, w2, h2, dxHalf, dyHalf, 24, 0, w2)
							: OutsideDiff(bIn, bInPrev, w2, h2, guardHalf, 24);
						inCount++;
						reference.insert(reference.end(), lowres.begin(), lowres.end());
					} else if (reference.size() >= (size_t)(count + 1) * gw * gh) {
						const Rect objG = Scale(objFull, 8);
						const Rect sweepG = Scale(sweepFull, 8);
						const Rect guardG = (s.kind == 0) ? Expand(sweepG, 2, gw, gh) : Rect{ 0, 0, 0, 0 };
						const float* ref = &reference[(size_t)count * gw * gh];
						double dO = 0, dT = 0, dB = 0, eNum = 0, eDen = 0;
						long long nO = 0, nT = 0, nB = 0;
						for (int y = 0; y < gh; y++) {
							for (int x = 0; x < gw; x++) {
								const size_t i = (size_t)y * gw + x;
								const double d = std::abs(lowres[i] - ref[i]);
								if (s.kind == 0 && Inside(objG, x, y)) { dO += d; nO++; }
								else if (s.kind == 0 && Inside(sweepG, x, y)) { dT += d; nT++; }
								else if (!Inside(guardG, x, y)) { dB += d; nB++; }
								if (x > 0 && y > 0 && x < gw - 1 && y < gh - 1) {
									const double contrast = std::abs(ref[i + 1] - ref[i - 1]) + std::abs(ref[i + gw] - ref[i - gw]);
									eNum += contrast * d;
									eDen += contrast;
								}
							}
						}
						objDev += nO ? dO / nO : 0;
						trailDev += nT ? dT / nT : 0;
						bgDev += nB ? dB / nB : 0;
						edgeDev += eDen > 0 ? eNum / eDen : 0;
					}
					count++;
				} else if (isReference) {
					Blur(yIn, w2, h2, 4, bIn);
				}

				yOutPrev.swap(yOut);
				bOutPrev.swap(bOut);
				if (isReference) {
					yInPrev.swap(yIn);
					bInPrev.swap(bIn);
				}
			}

			if (!count) {
				Out("  %-22s no usable frames (%d failed)\n", c.name, failed);
				g_failures++;
				continue;
			}
			const double inL = inCount ? inBgLF / inCount : 0;
			const double l = bgLF / count;
			Out("  %-22s %8.5f %7.2fx %9.5f %9.5f %9.5f %9.5f",
			    c.name, l, inL > 0 ? l / inL : 0.0,
			    isReference ? 0.0 : objDev / count, isReference ? 0.0 : trailDev / count,
			    isReference ? 0.0 : bgDev / count, isReference ? 0.0 : edgeDev / count);
			if (c.mode != DetectNone) {
				Out(" %7.2f %7.2f", maskBg / count, s.kind == 0 ? maskObj / count : 0.0);
			} else {
				Out(" %7s %7s", "-", "-");
			}
			if (c.mode == DetectShader) {
				Out(" %7.4f %5d", floorSum / count, detector.Unchanged());
			} else if (c.mode == DetectRenderer) {
				Out(" %7.4f %5s", floorSum / count, "-");
			}
			if (failed) {
				Out("   (%d failed)", failed);
			}
			Out("\n");
		}
		Out("  %-22s %8.5f\n", "(input)", inCount ? inBgLF / inCount : 0.0);
	}

	dlss.SetGuides(CDlssNR::Guides{});
	if (g_report) {
		fclose(g_report);
		g_report = nullptr;
		printf("\n  written to %s\n", port ? "temporal_results_port.txt" : "temporal_results_detect.txt");
	}
	return g_failures ? 1 : 0;
}

// --tbench: what the detector costs at 1080p and 2160p, without the network. The
// passes do the same work whatever the picture, so a few noisy pictures cycle.
static int RunDetectBench(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
	Head("Temporal suite: shader motion detector cost");

	CComPtr<ID3D11Query> disjoint, tsStart, tsEnd;
	D3D11_QUERY_DESC qd = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
	dev->CreateQuery(&qd, &disjoint);
	qd.Query = D3D11_QUERY_TIMESTAMP;
	dev->CreateQuery(&qd, &tsStart);
	dev->CreateQuery(&qd, &tsEnd);
	Check(disjoint && tsStart && tsEnd, "timestamp queries");
	if (!disjoint || !tsStart || !tsEnd) {
		return 1;
	}

	const int kWarm = 8, kFrames = 64, kDistinct = 4;
	g_report = fopen("temporal_results_bench.txt", "w");
	Out("\nShader motion detector cost -- GPU time from timestamp queries around CShaderDetector::Run, and the\n");
	Out("CPU time of that call (statistics readback and percentiles included). Each frame waits for the GPU to\n");
	Out("finish the previous one, so no stall is counted. %d warm-up frames, then %d measured.\n\n", kWarm, kFrames);
	Out("  %-10s %11s %11s %11s %11s\n", "size", "GPU mean", "GPU p95", "CPU mean", "CPU p95");

	struct Size { UINT w, h; const char* name; };
	static const Size sizes[] = { { 1920, 1080, "1920x1080" }, { 3840, 2160, "3840x2160" } };

	LARGE_INTEGER qpf = {};
	QueryPerformanceFrequency(&qpf);

	for (const Size& sz : sizes) {
		CShaderDetector detector;
		std::string error;
		Tex2D_t texMask;
		CComPtr<ID3D11RenderTargetView> maskTarget;
		bool ok = SUCCEEDED(texMask.CheckCreate(dev, DXGI_FORMAT_R8_UNORM, sz.w, sz.h, Tex2D_DefaultShaderRTarget))
			&& SUCCEEDED(dev->CreateRenderTargetView(texMask.pTexture, nullptr, &maskTarget))
			&& detector.Init(dev, sz.w, sz.h, error);

		printf("  building %s pictures...\n", sz.name);
		Tex2D_t pictures[kDistinct];
		if (ok) {
			const Image bg = MakeBase((int)sz.w, (int)sz.h, 20260913u);
			const Image obj = MakeBase((int)sz.w, (int)sz.h, 777u);
			std::vector<float> rgba(4 * (size_t)sz.w * sz.h);
			std::vector<HALF> half(rgba.size());
			for (int i = 0; i < kDistinct && ok; i++) {
				RenderOracleFrame(bg, obj, 200.0f + 3.0f * i, 300.0f + 1.0f * i, (int)sz.w / 4, (int)sz.h / 3, i, rgba);
				DirectX::PackedVector::XMConvertFloatToHalfStream(half.data(), sizeof(HALF), rgba.data(), sizeof(float), rgba.size());
				ok = SUCCEEDED(pictures[i].CheckCreate(dev, DXGI_FORMAT_R16G16B16A16_FLOAT, sz.w, sz.h, Tex2D_DefaultShaderRTarget))
					&& pictures[i].pShaderResource;
				if (ok) {
					ctx->UpdateSubresource(pictures[i].pTexture, 0, nullptr, half.data(), 4 * sizeof(HALF) * sz.w, 0);
				}
			}
		}
		Check(ok, sz.name);
		if (!ok) {
			if (!error.empty()) {
				printf("  %s\n", error.c_str());
			}
			continue;
		}

		std::vector<double> gpu, cpu;
		const CShaderDetector::Tuning tuning;
		detector.Reset(ctx);
		for (int f = 0; f < kWarm + kFrames; f++) {
			ctx->Begin(disjoint);
			ctx->End(tsStart);
			LARGE_INTEGER c0 = {}, c1 = {};
			QueryPerformanceCounter(&c0);
			detector.Run(ctx, pictures[f % kDistinct].pShaderResource, maskTarget, sz.w, sz.h, tuning);
			QueryPerformanceCounter(&c1);
			ctx->End(tsEnd);
			ctx->End(disjoint);

			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
			UINT64 t0 = 0, t1 = 0;
			while (ctx->GetData(disjoint, &dj, sizeof(dj), 0) == S_FALSE) {
				Sleep(0);
			}
			while (ctx->GetData(tsStart, &t0, sizeof(t0), 0) == S_FALSE) {
				Sleep(0);
			}
			while (ctx->GetData(tsEnd, &t1, sizeof(t1), 0) == S_FALSE) {
				Sleep(0);
			}
			if (f >= kWarm && !dj.Disjoint && dj.Frequency) {
				gpu.push_back(double(t1 - t0) * 1000.0 / dj.Frequency);
				cpu.push_back(double(c1.QuadPart - c0.QuadPart) * 1000.0 / qpf.QuadPart);
			}
		}

		auto summarise = [](std::vector<double>& v, double& mean, double& p95) {
			double sum = 0;
			for (double x : v) {
				sum += x;
			}
			mean = v.empty() ? 0 : sum / v.size();
			std::sort(v.begin(), v.end());
			p95 = v.empty() ? 0 : v[std::min(v.size() - 1, v.size() * 95 / 100)];
		};
		double gm = 0, gp = 0, cm = 0, cp = 0;
		summarise(gpu, gm, gp);
		summarise(cpu, cm, cp);
		Out("  %-10s %8.3f ms %8.3f ms %8.3f ms %8.3f ms   (%zu frames)\n", sz.name, gm, gp, cm, cp, gpu.size());
	}

	if (g_report) {
		fclose(g_report);
		g_report = nullptr;
		printf("\n  written to temporal_results_bench.txt\n");
	}
	return g_failures ? 1 : 0;
}

} // namespace temporal
