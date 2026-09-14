nvOpticalFlowCommon.h and nvOpticalFlowD3D11.h are the interface headers of the
NVIDIA Optical Flow SDK 5.0.7, copied unchanged. Each carries its own permission
notice at the top ("This copyright notice applies to this header file only"),
which allows copying and redistribution. The rest of the SDK is not used.

The implementation lives in the display driver (nvofapi64.dll); it is loaded at
run time by Source/DLSS/DlssOpticalFlow.cpp.
