#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "Support/WinInclude.h"
#include "Support/ComPointer.h"
#include "Support/Window.h"
#include "Support/Shader.h"

#include "Debug/DebugLayer.h"

#include "D3D/DXContext.h"
#include "D3D/Pipeline/RenderPipeline.h"
#include "D3D/Pipeline/MeshPipeline.h"
#include "D3D/Pipeline/ComputePipeline.h"


#include "Scene/Camera.h"
#include "Scene/Scene.h"
#include "Scene/PBMPMScene.h"

#include "ImGUI/ImGUIHelper.h"

static ImGUIDescriptorHeapAllocator imguiHeapAllocator;
static ID3D12DescriptorHeap* imguiSRVHeap = nullptr;

static int meshletRenderType = 2; // 0 = realistic, 1 = meshlets, 2 = toon shading
static unsigned int renderModeType = 0; // 0 = just mesh shading, 1 = both particles and mesh shading, 2 = just particles
static int toonShadingLevels = 3;
static int fixedPointExponent = 7;
static bool useGridVolume = true;
static bool renderGrid = false;
static bool renderSpawn = false;

const char* modes[] = { "流体表面 + 其他粒子", "流体表面 + 全部粒子", "仅显示全部粒子" };
const char* meshModes[] = { "写实", "网格块", "卡通" };

void loadChineseUIFont(ImGuiIO& io) {
    // 使用 Windows 自带字体，不在仓库中分发系统字体。ImGui 接收 UTF-8 文件路径。
    wchar_t windowsDirectory[MAX_PATH];
    const UINT directoryLength = GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    if (directoryLength == 0 || directoryLength >= MAX_PATH) {
        throw std::runtime_error("Cannot locate the Windows directory for the Chinese UI font.");
    }

    const auto fontsDirectory = std::filesystem::path(windowsDirectory) / L"Fonts";
    for (const wchar_t* fontName : { L"msyh.ttc", L"simhei.ttf", L"simsun.ttc" }) {
        const auto fontPath = fontsDirectory / fontName;
        if (!std::filesystem::exists(fontPath)) {
            continue;
        }
        const auto utf8Path = fontPath.u8string();
        const std::string filename(utf8Path.begin(), utf8Path.end());
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            filename.c_str(), 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
        if (font != nullptr) {
            io.FontDefault = font;
            return;
        }
    }
    throw std::runtime_error("No Chinese UI font found. Install Microsoft YaHei, SimHei or SimSun.");
}

ImGuiIO& initImGUI(DXContext& context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    loadChineseUIFont(io);
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(Window::get().getHWND());

    ImGui_ImplDX12_InitInfo imguiDXInfo;
    imguiDXInfo.CommandQueue = context.getCommandQueue();
    imguiDXInfo.Device = context.getDevice();
    imguiDXInfo.NumFramesInFlight = 2;
    imguiDXInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 64;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (context.getDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&imguiSRVHeap)) != S_OK) {
        std::cout << "could not create imgui descriptor heap\n";
        Window::get().shutdown();
    }
    imguiHeapAllocator.Create(context.getDevice(), imguiSRVHeap);

    imguiHeapAllocator.Heap = imguiSRVHeap;
    imguiDXInfo.SrvDescriptorHeap = imguiSRVHeap;
    imguiDXInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) { return imguiHeapAllocator.Alloc(out_cpu_handle, out_gpu_handle); };
    imguiDXInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) { return imguiHeapAllocator.Free(cpu_handle, gpu_handle); };

    ImGui_ImplDX12_Init(&imguiDXInfo);

    return io;
}

void drawImGUIWindow(PBMPMConstants& pbmpmConstants, ImGuiIO& io,
    float* fluidIsovalue, float* fluidKernelScale, float* fluidKernelRadius, 
    float* elasticIsovalue, float* elasticKernelScale, float* elasticKernelRadius,
	float* sandIsovalue, float* sandKernelScale, float* sandKernelRadius,
	float* viscoIsovalue, float* viscoKernelScale, float* viscoKernelRadius,
    unsigned int* substepCount, int numParticles) {
    // ### 后使用稳定英文 ID，使后续文案调整不再改变窗口和控件身份。
    ImGui::SetNextWindowSize(ImVec2(480, 640), ImGuiCond_FirstUseEver);
    ImGui::Begin("场景选项###Scene Options");
    ImGui::PushItemWidth(-170.0f);

    ImGui::Text("平均帧耗时：%.3f 毫秒（%.1f 帧/秒）", 1000.0f / io.Framerate, io.Framerate);
    ImGui::Text("粒子数量：%d", numParticles);

    if (ImGui::CollapsingHeader("模拟参数###Simulation Parameters")) {
        ImGui::SliderFloat("重力强度###Gravity Strength", &pbmpmConstants.gravityStrength, 0.0f, 20.0f);
        ImGui::SliderFloat("液体松弛系数###Liquid Relaxation", &pbmpmConstants.liquidRelaxation, 0.0f, 10.0f);
        ImGui::SliderFloat("液体黏性###Liquid Viscosity", &pbmpmConstants.liquidViscosity, 0.0f, 1.0f);
        ImGui::SliderFloat("内摩擦角###Friction Angle", &pbmpmConstants.frictionAngle, 0.0f, 90.0f);

        ImGui::SliderFloat("弹性松弛系数###Elastic Relaxation", &pbmpmConstants.elasticRelaxation, 0.0f, 3.0f);
        ImGui::SliderFloat("弹性混合比例###Elastic Ratio", &pbmpmConstants.elasticityRatio, 0.0f, 4.0f);

        ImGui::SliderFloat("沙体松弛系数###Sand Relaxation", &pbmpmConstants.sandRelaxation, 0.0f, 3.0f);
        ImGui::SliderFloat("沙体混合比例###Sand Ratio", &pbmpmConstants.sandRatio, 0.0f, 2.0f);

        ImGui::SliderInt("每单元每轴粒子数###Particles Per Cell Axis", (int*)&pbmpmConstants.particlesPerCellAxis, 1, 8);
        ImGui::SliderInt("定点倍率指数###Fixed Point Multiplier", (int*)&fixedPointExponent, 4, 13);
        pbmpmConstants.fixedPointMultiplier = (unsigned int)pow(10, fixedPointExponent);

        ImGui::SliderFloat("边界摩擦###Border Friction", &pbmpmConstants.borderFriction, 0.0f, 1.0f);

        ImGui::SliderInt("迭代次数###Iteration Count", (int*)&pbmpmConstants.iterationCount, 1, 10);
        ImGui::SliderInt("子步数量###Substep Count", (int*)substepCount, 1, 20);

        ImGui::SliderFloat("鼠标作用半径###Mouse Radius", &pbmpmConstants.mouseRadius, 0.1f, 10.f);
        ImGui::SliderFloat("鼠标作用强度###Mouse Strength", &pbmpmConstants.mouseStrength, 0.f, 40.f);

        ImGui::Checkbox("液体使用网格体积###Use Grid Volume for Liquid", (bool*)&useGridVolume);
        pbmpmConstants.useGridVolumeForLiquid = useGridVolume;
    }

    if (ImGui::CollapsingHeader("表面重建参数###Mesh Shading Parameters")) {
        if (ImGui::CollapsingHeader("流体表面###Fluid Shading Parameters")) {
            ImGui::SliderFloat("流体等值面阈值###Fluid Isovalue", fluidIsovalue, 0.01f, 3.0f);
            ImGui::SliderFloat("流体核缩放###Fluid Kernel Scale", fluidKernelScale, 0.0f, 12.0f);
            ImGui::SliderFloat("流体核半径###Fluid Kernel Radius", fluidKernelRadius, 0.0f, 5.0f);
        }
        if (ImGui::CollapsingHeader("弹性体表面###Elastic Shading Parameters")) {
            ImGui::SliderFloat("弹性体等值面阈值###Elastic Isovalue", elasticIsovalue, 0.01f, 3.0f);
            ImGui::SliderFloat("弹性体核缩放###Elastic Kernel Scale", elasticKernelScale, 0.0f, 12.0f);
            ImGui::SliderFloat("弹性体核半径###Elastic Kernel Radius", elasticKernelRadius, 0.0f, 5.0f);
        }
        if (ImGui::CollapsingHeader("沙体表面###Sand Shading Parameters")) {
			ImGui::SliderFloat("沙体等值面阈值###Sand Isovalue", sandIsovalue, 0.01f, 3.0f);
			ImGui::SliderFloat("沙体核缩放###Sand Kernel Scale", sandKernelScale, 0.0f, 12.0f);
			ImGui::SliderFloat("沙体核半径###Sand Kernel Radius", sandKernelRadius, 0.0f, 5.0f);
		}
        if (ImGui::CollapsingHeader("黏塑性材料表面###Viscoelastic Shading Parameters")) {
            ImGui::SliderFloat("黏塑性等值面阈值###Visco Isovalue", viscoIsovalue, 0.01f, 3.0f);
            ImGui::SliderFloat("黏塑性核缩放###Visco Kernel Scale", viscoKernelScale, 0.0f, 12.0f);
            ImGui::SliderFloat("黏塑性核半径###Visco Kernel Radius", viscoKernelRadius, 0.0f, 5.0f);
        }
		//if (ImGui::CollapsingHeader("Snow Shading Parameters")) {
		//	ImGui::SliderFloat("Snow Isovalue", snowIsovalue, 0.01f, 3.0f);
		//	ImGui::SliderFloat("Snow Kernel Scale", snowKernelScale, 0.0f, 12.0f);
		//	ImGui::SliderFloat("Snow Kernel Radius", snowKernelRadius, 0.0f, 5.0f);
		//}
    }

    if (ImGui::CollapsingHeader("显示参数###Render Parameters")) {

        ImGui::Combo("显示方式###Select Render Mode", (int*)&renderModeType, modes, IM_ARRAYSIZE(modes));

        if (renderModeType != 2) {
            ImGui::Combo("表面风格###Select Mesh Mode", (int*)&meshletRenderType, meshModes, IM_ARRAYSIZE(meshModes));
        }

        if (meshletRenderType == 2) {
			ImGui::SliderInt("卡通色阶数量###Toon Shading Levels", (int*)&toonShadingLevels, 1, 10);
        }

        ImGui::Checkbox("显示网格###Render Grid", &renderGrid);

        ImGui::Checkbox("显示发射区域###Render Spawners", &renderSpawn);
    }

    ImGui::PopItemWidth();
    ImGui::End();
}

void ComputeMouseRay(
    HWND hwnd,
    float ndcX,
    float ndcY,
    const XMMATRIX& projectionMatrix,
    const XMMATRIX& viewMatrix,
    XMFLOAT4& rayOrigin,
    XMFLOAT4& rayDirection
) {
    // Invert the projection and view matrices
    XMMATRIX invProj = XMMatrixInverse(nullptr, projectionMatrix);
    XMMATRIX invView = XMMatrixInverse(nullptr, viewMatrix);

    // Define the mouse's NDC position on the near and far planes
    XMVECTOR ndcNear = XMVectorSet(ndcX, ndcY, 0.0f, 1.0f); // Near plane
    XMVECTOR ndcFar = XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);   // Far plane

    // Unproject the NDC points to view space
    XMVECTOR viewNear = XMVector3TransformCoord(ndcNear, invProj);
    XMVECTOR viewFar = XMVector3TransformCoord(ndcFar, invProj);

    // Transform the points from view space to world space
    XMVECTOR worldNear = XMVector3TransformCoord(viewNear, invView);
    XMVECTOR worldFar = XMVector3TransformCoord(viewFar, invView);

    // Calculate the ray origin (camera position) and direction
    rayOrigin = XMFLOAT4(
        XMVectorGetX(worldNear),
        XMVectorGetY(worldNear),
        XMVectorGetZ(worldNear),
        1.0f
    );

    XMVECTOR rayDir = XMVector3Normalize(XMVectorSubtract(worldFar, worldNear));
    rayDirection = XMFLOAT4(
        XMVectorGetX(rayDir),
        XMVectorGetY(rayDir),
        XMVectorGetZ(rayDir),
        0.0f
    );
}
