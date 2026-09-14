// --tflow: NVIDIA Optical Flow through CDlssOpticalFlow, on a real picture moved by
// known amounts.
//
// Included by harness.cpp after effect_suite.inl. Checks what the renderer will rely
// on before any of it is wired in: that the flow comes back in 1/32 pixel of the flow
// frame, pointing from the current frame towards the previous one (the DLSS motion
// vector direction), with backward flow its opposite; and what one flow costs at the
// sizes, grids and quality levels worth considering. --timage <path> picks the picture.

#include "DLSS/DlssOpticalFlow.h"

namespace temporal {

// A picture as 8-bit grey, centre-cropped to the target aspect ratio and scaled.
static bool LoadGray(IWICImagingFactory* factory, const wchar_t* path, int W, int H,
                     std::vector<float>& luma, std::string& error)
{
	CComPtr<IWICBitmapDecoder> decoder;
	CComPtr<IWICBitmapFrameDecode> frame;
	HRESULT hr = factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
	if (SUCCEEDED(hr)) {
		hr = decoder->GetFrame(0, &frame);
	}
	UINT sw = 0, sh = 0;
	if (SUCCEEDED(hr)) {
		hr = frame->GetSize(&sw, &sh);
	}
	if (FAILED(hr) || !sw || !sh) {
		error = std::format("cannot decode the picture (0x{:08X})", (unsigned)hr);
		return false;
	}
	UINT cw = sw, ch = sh;
	if ((double)sw * H > (double)sh * W) {
		cw = (UINT)((double)sh * W / H);
	} else {
		ch = (UINT)((double)sw * H / W);
	}
	const WICRect crop = { (INT)((sw - cw) / 2), (INT)((sh - ch) / 2), (INT)cw, (INT)ch };

	CComPtr<IWICBitmapClipper> clipper;
	CComPtr<IWICBitmapScaler> scaler;
	CComPtr<IWICFormatConverter> converter;
	std::vector<BYTE> bytes((size_t)W * H);
	hr = factory->CreateBitmapClipper(&clipper);
	if (SUCCEEDED(hr)) {
		hr = clipper->Initialize(frame, &crop);
	}
	if (SUCCEEDED(hr)) {
		hr = factory->CreateBitmapScaler(&scaler);
	}
	if (SUCCEEDED(hr)) {
		hr = scaler->Initialize(clipper, (UINT)W, (UINT)H, WICBitmapInterpolationModeFant);
	}
	if (SUCCEEDED(hr)) {
		hr = factory->CreateFormatConverter(&converter);
	}
	if (SUCCEEDED(hr)) {
		hr = converter->Initialize(scaler, GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
	}
	if (SUCCEEDED(hr)) {
		hr = converter->CopyPixels(nullptr, (UINT)W, (UINT)bytes.size(), bytes.data());
	}
	if (FAILED(hr)) {
		error = std::format("cannot crop and scale the picture (0x{:08X})", (unsigned)hr);
		return false;
	}
	luma.resize(bytes.size());
	for (size_t i = 0; i < bytes.size(); i++) {
		luma[i] = bytes[i] / 255.0f;
	}
	return true;
}

// W x H bytes of a larger grey picture: the window at (ox, oy), bilinear, so that a
// fractional move of the window is an exact fractional move of the content.
static void WindowBytes(const std::vector<float>& big, int BW, int BH, float ox, float oy, int W, int H,
                        std::vector<uint8_t>& out)
{
	out.resize((size_t)W * H);
	for (int y = 0; y < H; y++) {
		const float fy = std::clamp(oy + y, 0.0f, (float)BH - 1.001f);
		const int y0 = (int)fy;
		const float ty = fy - y0;
		for (int x = 0; x < W; x++) {
			const float fx = std::clamp(ox + x, 0.0f, (float)BW - 1.001f);
			const int x0 = (int)fx;
			const float tx = fx - x0;
			const float* r0 = &big[(size_t)y0 * BW + x0];
			const float* r1 = &big[(size_t)(y0 + 1) * BW + x0];
			const float v = (r0[0] * (1 - tx) + r0[1] * tx) * (1 - ty) + (r1[0] * (1 - tx) + r1[1] * tx) * ty;
			out[(size_t)y * W + x] = (uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255);
		}
	}
}

static bool ReadFlow(ID3D11DeviceContext* ctx, ID3D11Texture2D* stage, UINT gw, UINT gh, std::vector<int16_t>& out)
{
	D3D11_MAPPED_SUBRESOURCE mr = {};
	if (FAILED(ctx->Map(stage, 0, D3D11_MAP_READ, 0, &mr))) {
		return false;
	}
	out.resize(2 * (size_t)gw * gh);
	for (UINT y = 0; y < gh; y++) {
		memcpy(&out[2 * (size_t)y * gw], (const BYTE*)mr.pData + (size_t)mr.RowPitch * y, 2 * sizeof(int16_t) * gw);
	}
	ctx->Unmap(stage, 0);
	return true;
}

static CComPtr<ID3D11Texture2D> StagingLike(ID3D11Device* dev, ID3D11Texture2D* tex, UINT w = 0, UINT h = 0)
{
	D3D11_TEXTURE2D_DESC d = {};
	tex->GetDesc(&d);
	if (w && h) {
		d.Width = w;
		d.Height = h;
	}
	d.Usage = D3D11_USAGE_STAGING;
	d.BindFlags = 0;
	d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	d.MiscFlags = 0;
	CComPtr<ID3D11Texture2D> stage;
	dev->CreateTexture2D(&d, nullptr, &stage);
	return stage;
}

// Waits for everything queued on tex so far, through a one-texel readback.
static void WaitFor(ID3D11DeviceContext* ctx, ID3D11Texture2D* tex, ID3D11Texture2D* stage1x1)
{
	const D3D11_BOX box = { 0, 0, 0, 1, 1, 1 };
	ctx->CopySubresourceRegion(stage1x1, 0, 0, 0, 0, tex, 0, &box);
	D3D11_MAPPED_SUBRESOURCE mr = {};
	if (SUCCEEDED(ctx->Map(stage1x1, 0, D3D11_MAP_READ, 0, &mr))) {
		ctx->Unmap(stage1x1, 0);
	}
}

struct FlowAccuracy {
	double meanX = 0, meanY = 0;
	double medianX = 0, medianY = 0;
	double within = 0;           // % of vectors within 0.25 px of the truth
	double withinTextured = 0;   // the same over textured blocks only
	size_t count = 0, textured = 0;
};

// Vectors of the central 80 % of the frame against the known displacement. "Textured"
// blocks have a mean absolute gradient of at least 6/255 under them: flat areas give
// the engine nothing to match.
static FlowAccuracy Accuracy(const std::vector<int16_t>& flow, UINT gw, UINT gh, UINT grid,
                             float expectX, float expectY, const std::vector<uint8_t>& frame, int W, int H)
{
	FlowAccuracy a;
	std::vector<float> xs, ys;
	size_t inTolerance = 0, texturedIn = 0;
	for (UINT by = gh / 10; by < gh - gh / 10; by++) {
		for (UINT bx = gw / 10; bx < gw - gw / 10; bx++) {
			const float fx = flow[2 * ((size_t)by * gw + bx)] / 32.0f;
			const float fy = flow[2 * ((size_t)by * gw + bx) + 1] / 32.0f;
			xs.push_back(fx);
			ys.push_back(fy);
			a.meanX += fx;
			a.meanY += fy;
			const bool ok = std::hypot(fx - expectX, fy - expectY) <= 0.25f;
			inTolerance += ok;

			const int size = (int)std::max(grid, 4u);
			const int cx = (int)(bx * grid + grid / 2), cy = (int)(by * grid + grid / 2);
			double energy = 0;
			int n = 0;
			for (int y = std::max(cy - size / 2, 0); y < std::min(cy + size / 2, H - 1); y++) {
				for (int x = std::max(cx - size / 2, 0); x < std::min(cx + size / 2, W - 1); x++) {
					const int p = frame[(size_t)y * W + x];
					energy += std::abs(frame[(size_t)y * W + x + 1] - p) + std::abs(frame[(size_t)(y + 1) * W + x] - p);
					n++;
				}
			}
			if (n && energy / n >= 6.0) {
				a.textured++;
				texturedIn += ok;
			}
		}
	}
	a.count = xs.size();
	if (a.count) {
		a.meanX /= a.count;
		a.meanY /= a.count;
		std::nth_element(xs.begin(), xs.begin() + xs.size() / 2, xs.end());
		std::nth_element(ys.begin(), ys.begin() + ys.size() / 2, ys.end());
		a.medianX = xs[xs.size() / 2];
		a.medianY = ys[ys.size() / 2];
		a.within = 100.0 * inTolerance / a.count;
		a.withinTextured = a.textured ? 100.0 * texturedIn / a.textured : 0.0;
	}
	return a;
}

static int RunFlow(ID3D11Device* dev, ID3D11DeviceContext* ctx, const wchar_t* imagePath)
{
	Head("Temporal suite: NVIDIA Optical Flow");
	const HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	{
		CComPtr<IWICImagingFactory> factory;
		const bool wicOk = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
		Check(wicOk, "WIC factory");
		if (wicOk) {
			g_report = fopen("temporal_results_flow.txt", "w");

			struct Size { int W, H, BW, BH; };
			static const Size accuracySizes[] = {
				{ 1920, 1080, 2400, 1350 }, { 960, 540, 1200, 675 }, { 480, 270, 600, 338 },
			};
			static const struct { float x, y; } shifts[] = {
				{ 6.5f, -2.25f }, { -3.0f, 1.5f }, { 0.5f, 0.0f }, { -20.0f, 12.0f }, { 0.0f, 0.0f },
			};
			static const UINT grids[] = { 1, 4 };

			Out("\nNVIDIA Optical Flow on %S, windows of a larger copy moved by known amounts.\n", imagePath);
			Out("The content of the current frame is the previous frame's moved by (dx, dy), so the forward flow\n");
			Out("should read (-dx, -dy) -- where the content was -- and the backward flow (dx, dy). Vectors from the\n");
			Out("central 80 %% of the frame; within = share within 0.25 px of the truth, over all blocks and over\n");
			Out("textured blocks (flat areas give the engine nothing to match). Medium quality, both directions.\n\n");
			Out("  %-9s %4s %14s %20s %20s %7s %7s %20s\n",
			    "flow", "grid", "move (px)", "fwd mean", "fwd median", "within", "texture", "bwd mean");

			std::vector<float> big;
			std::vector<uint8_t> prev, cur;
			std::vector<int16_t> fwd, bwd;
			std::string error;

			for (const Size& sz : accuracySizes) {
				if (!LoadGray(factory, imagePath, sz.BW, sz.BH, big, error)) {
					Out("  %dx%d: %s\n", sz.W, sz.H, error.c_str());
					g_failures++;
					continue;
				}
				const float ox = (sz.BW - sz.W) / 2.0f, oy = (sz.BH - sz.H) / 2.0f;
				for (UINT grid : grids) {
					CDlssOpticalFlow of;
					CDlssOpticalFlow::Options options;
					options.gridSize = grid;
					if (!of.Init(dev, ctx, sz.W, sz.H, options)) {
						Out("  %dx%d grid %u: %S\n", sz.W, sz.H, grid, of.GetStatusLine().c_str());
						g_failures++;
						continue;
					}
					if (grid == grids[0] && sz.W == accuracySizes[0].W) {
						Out("  (driver Optical Flow API %u.%u; session %S)\n",
						    of.DriverApiVersion() >> 4, of.DriverApiVersion() & 0xF, of.GetStatusLine().c_str());
					}
					const UINT gw = of.OutputWidth(), gh = of.OutputHeight();
					CComPtr<ID3D11Texture2D> stageFwd = StagingLike(dev, of.ForwardFlowTexture());
					CComPtr<ID3D11Texture2D> stageBwd = of.Bidirectional() ? StagingLike(dev, of.BackwardFlowTexture()) : nullptr;

					for (const auto& s : shifts) {
						of.Reset();
						WindowBytes(big, sz.BW, sz.BH, ox, oy, sz.W, sz.H, prev);
						ctx->UpdateSubresource(of.FrameTexture(), 0, nullptr, prev.data(), sz.W, 0);
						of.Execute();   // the first frame has nothing to be compared with
						WindowBytes(big, sz.BW, sz.BH, ox - s.x, oy - s.y, sz.W, sz.H, cur);
						ctx->UpdateSubresource(of.FrameTexture(), 0, nullptr, cur.data(), sz.W, 0);
						if (!of.Execute()) {
							Out("  %dx%d grid %u: %S\n", sz.W, sz.H, of.GridSize(), of.GetStatusLine().c_str());
							g_failures++;
							continue;
						}
						ctx->CopyResource(stageFwd, of.ForwardFlowTexture());
						if (!ReadFlow(ctx, stageFwd, gw, gh, fwd)) {
							g_failures++;
							continue;
						}
						const FlowAccuracy f = Accuracy(fwd, gw, gh, of.GridSize(), -s.x, -s.y, cur, sz.W, sz.H);
						std::string bwdText = "-";
						if (stageBwd) {
							ctx->CopyResource(stageBwd, of.BackwardFlowTexture());
							if (ReadFlow(ctx, stageBwd, gw, gh, bwd)) {
								const FlowAccuracy b = Accuracy(bwd, gw, gh, of.GridSize(), s.x, s.y, prev, sz.W, sz.H);
								bwdText = std::format("({:+7.2f},{:+7.2f})", b.meanX, b.meanY);
							}
						}
						Out("  %4dx%-4d %4u (%+6.2f,%+6.2f) (%+8.3f,%+8.3f) (%+8.3f,%+8.3f) %6.1f%% %6.1f%% %20s\n",
						    sz.W, sz.H, of.GridSize(), s.x, s.y, f.meanX, f.meanY, f.medianX, f.medianY,
						    f.within, f.withinTextured, bwdText.c_str());
					}
				}
			}

			// Cost of one flow, upload excluded, readback of one texel included so the
			// engine's work is really finished.
			struct TimeCase { const Size* size; UINT grid; NV_OF_PERF_LEVEL perf; bool bidirectional; };
			static const Size timeSizes[] = {
				{ 3840, 2160, 4000, 2250 }, { 1920, 1080, 2400, 1350 }, { 960, 540, 1200, 675 }, { 480, 270, 600, 338 },
			};
			static const NV_OF_PERF_LEVEL perfs[] = { NV_OF_PERF_LEVEL_FAST, NV_OF_PERF_LEVEL_MEDIUM, NV_OF_PERF_LEVEL_SLOW };
			const int kWarm = 6, kFrames = 16;

			Out("\nTime per flow, %d frames after %d warm-up, alternating a 3 px move left and right. Upload excluded;\n", kFrames, kWarm);
			Out("the call and a one-texel readback of the forward flow included, so the engine's work is finished.\n\n");
			Out("  %-9s %4s %-6s %5s %10s %10s\n", "flow", "grid", "perf", "bidir", "mean ms", "p95 ms");

			LARGE_INTEGER qpf = {};
			QueryPerformanceFrequency(&qpf);
			for (const Size& sz : timeSizes) {
				if (!LoadGray(factory, imagePath, sz.BW, sz.BH, big, error)) {
					Out("  %dx%d: %s\n", sz.W, sz.H, error.c_str());
					g_failures++;
					continue;
				}
				const float ox = (sz.BW - sz.W) / 2.0f, oy = (sz.BH - sz.H) / 2.0f;
				std::vector<uint8_t> left, right;
				WindowBytes(big, sz.BW, sz.BH, ox - 3, oy, sz.W, sz.H, left);
				WindowBytes(big, sz.BW, sz.BH, ox + 3, oy, sz.W, sz.H, right);

				std::vector<TimeCase> cases;
				for (UINT grid : grids) {
					for (NV_OF_PERF_LEVEL perf : perfs) {
						cases.push_back({ &sz, grid, perf, true });
					}
				}
				cases.push_back({ &sz, 4, NV_OF_PERF_LEVEL_MEDIUM, false });

				for (const TimeCase& tc : cases) {
					CDlssOpticalFlow of;
					CDlssOpticalFlow::Options options;
					options.gridSize = tc.grid;
					options.perfLevel = tc.perf;
					options.bidirectional = tc.bidirectional;
					if (!of.Init(dev, ctx, sz.W, sz.H, options)) {
						Out("  %dx%d grid %u: %S\n", sz.W, sz.H, tc.grid, of.GetStatusLine().c_str());
						g_failures++;
						continue;
					}
					CComPtr<ID3D11Texture2D> texel = StagingLike(dev, of.ForwardFlowTexture(), 1, 1);
					CComPtr<ID3D11Texture2D> frameTexel = StagingLike(dev, of.FrameTexture(), 1, 1);
					std::vector<double> ms;
					for (int f = 0; f < kWarm + kFrames; f++) {
						const std::vector<uint8_t>& bytes = (f % 2) ? right : left;
						ctx->UpdateSubresource(of.FrameTexture(), 0, nullptr, bytes.data(), sz.W, 0);
						WaitFor(ctx, of.FrameTexture(), frameTexel);
						LARGE_INTEGER t0 = {}, t1 = {};
						QueryPerformanceCounter(&t0);
						const bool ok = of.Execute();
						if (ok) {
							WaitFor(ctx, of.ForwardFlowTexture(), texel);
						}
						QueryPerformanceCounter(&t1);
						if (ok && f >= kWarm) {
							ms.push_back(double(t1.QuadPart - t0.QuadPart) * 1000.0 / qpf.QuadPart);
						}
					}
					if (ms.empty()) {
						Out("  %4dx%-4d %4u: no flow (%S)\n", sz.W, sz.H, of.GridSize(), of.GetStatusLine().c_str());
						g_failures++;
						continue;
					}
					double sum = 0;
					for (double v : ms) {
						sum += v;
					}
					std::sort(ms.begin(), ms.end());
					const double p95 = ms[std::min(ms.size() - 1, ms.size() * 95 / 100)];
					Out("  %4dx%-4d %4u %-6s %5s %10.2f %10.2f\n", sz.W, sz.H, of.GridSize(),
					    tc.perf == NV_OF_PERF_LEVEL_FAST ? "fast" : tc.perf == NV_OF_PERF_LEVEL_SLOW ? "slow" : "medium",
					    of.Bidirectional() ? "yes" : "no", sum / ms.size(), p95);
				}
			}

			fclose(g_report);
			g_report = nullptr;
			printf("\n  written to temporal_results_flow.txt\n");
		}
	}
	if (SUCCEEDED(coInit)) {
		CoUninitialize();
	}
	return g_failures ? 1 : 0;
}

} // namespace temporal
