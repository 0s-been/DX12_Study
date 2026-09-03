//***************************************************************************************
// CubeMapApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/Camera.h"
#include "FrameResource.h"
#include "ShadowMap.h"
#include "Ssao.h"

//osb
//Imgui 인클루드
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include "SkinnedData.h"
#include "FBXUtil/FbxLoader.h"


using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

struct SkinnedModelInstance
{
	SkinnedData* SkinnedInfo = nullptr;
	std::vector<DirectX::XMFLOAT4X4> FinalTransforms;
	std::string ClipName;
	float TimePos = 0.0f;

	void UpdateSkinnedAnimation(float dt)
	{
		TimePos += dt;

		if (TimePos > SkinnedInfo->GetClipEndTime(ClipName))
			TimePos = 0.0f;

		SkinnedInfo->GetFinalTransforms(ClipName, TimePos, FinalTransforms);
	}
};

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;
    RenderItem(const RenderItem& rhs) = delete;
 
    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;

	//osb
	//스키닝 렌더 아이템에만 해당
	UINT SkinnedCBIndex = -1;
	//스키닝되지 않는 아이템은 nullptr
	SkinnedModelInstance* SkinnedModelInst = nullptr;
};

enum class RenderLayer : int
{
	Opaque = 0,
	SkinnedOpaque,
	Sky,
	Debug, //osb 그림자맵 디버그 뷰용
	SsaoDebug, //ssao맵 디버그 뷰용
	Count
};

class CubeMapApp : public D3DApp
{
public:
    CubeMapApp(HINSTANCE hInstance);
    CubeMapApp(const CubeMapApp& rhs) = delete;
    CubeMapApp& operator=(const CubeMapApp& rhs) = delete;
    ~CubeMapApp();

    virtual bool Initialize()override;

	//osb
	//imgui srv인덱스 getter
	UINT GetImGuiSrvIndex() const;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialBuffer(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	//osb
	//shadowmapapp의 함수 가져옮
	void UpdateShadowTransform(const GameTimer& gt);
	void UpdateShadowPassCB(const GameTimer& gt);
	void UpdateSkinnedCBs(const GameTimer& gt);

	//osb
	//ssaoApp에서 가져온 힘수들
	void BuildSsaoRootSignature();
	void UpdateSsaoCB(const GameTimer& gt);
	void DrawNormalsAndDepth();
	
	//osb
	//d3dapp의 가상함수 오버라이딩
	void CreateRtvAndDsvDescriptorHeaps() override;

	void LoadTextures();
    void BuildRootSignature();
	void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
	//osb
	//shadowmap용 지오메트리 함수 추가
	void BuildDebugQuadGeometry();
    void BuildSkullGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	//osb
	void DrawSceneToShadowMap();
	//
	void LoadSkinnedModel();

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();
	
	//osb
	//디스크립터 인덱스 버그를 겪은 뒤 절대 인덱스로 직접 계산하는 방식의 함수를 추가
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv(int index) const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuSrv(int index) const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsv(int index) const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtv(int index) const;


	//osb
	//ImGui init 함수
	void InitImGui();
	//ImgGui 세팅 함수
	void SetImGui(const GameTimer& gt);


private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
	UINT mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	//osb
	//ssao 루트시그니쳐 변수 추가
	ComPtr<ID3D12RootSignature> mSsaoRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	//osb
	//스키닝 인풋 레이어 추가
	std::vector<D3D12_INPUT_ELEMENT_DESC> mSkinnedInputLayout;
 
	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

    PassConstants mMainPassCB;

	Camera mCamera;

    POINT mLastMousePos;


	//sk box 설정 관련 변수들
	bool mSkyEnabled = true;
	std::array<UINT, 4> mSkyTexHeapIndices = { 0, 0, 0, 0 };
	UINT mCurrSkyIndex = 0; //0 = grass
	std::vector<std::string> mSkyTexNames = { "Grass", "Dessert", "Snow", "Sunset" };
	UINT mSkyTexHeapIndex = 0;

	//osb
	//imgui srv인덱싱 고정
	UINT mImguiSrvIndex = 7;

	//osb
	//shadowmap 관련 변수들
	std::unique_ptr<ShadowMap> mShadowMap; //그림자맵 렌더 타겟 객체
	DirectX::BoundingSphere mSceneBounds;  //씬 전체를 감싸는 가상의 구 -> 라티으 절두체 계산 기준
	float mLightNearZ = 0.0f; //빛 시점에서 카메라의 near, far 평면 거리
	float mLightFarZ = 0.0f;
	DirectX::XMFLOAT3 mLightPosW; //빛의 가상 위치
	DirectX::XMFLOAT4X4 mLightView = MathHelper::Identity4x4(); //빛 시점에서 view, proj행렬
	DirectX::XMFLOAT4X4 mLightProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4(); //월드 좌표 -> 그림자맵 텍스쳐 좌표 변환 행랼
	PassConstants mShadowPassCB; //그림자 패스 전용 PassConstants
	UINT mShadowMapHeapIndex = 0; //srv 힙에서 그림자맵 위치
	bool mShowShadowDebug = false;
	//빛 회전에 대한 변수
	float mLightRotationAngle = 0.0f;
	XMFLOAT3 mBaseLightDirections[3] = {
		XMFLOAT3(0.57735f, -0.57735f, 0.57735f),
		XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
		XMFLOAT3(0.0f, -0.707f, -0.707f)
	};
	XMFLOAT3 mRotatedLightDirections[3];
	bool mLightRotate = true;

	//osb
	//Ssao 관련 변수들
	std::unique_ptr<Ssao> mSsao;
	UINT mSsaoHeapIndexStart = 0;
	bool mSsaoEnabled = true;
	bool mShowSsaoDebug = false;

	//osb
	//스키닝 관련 변수들
	//fbx에서 읽어온 스키닝 데이터(본 계층, 본오프셋, 애니메이션 클립)
	SkinnedData mSkinnedInfo;

	//애니메이션 재생 상태를 들고 있는 인스턴스
	std::unique_ptr<SkinnedModelInstance> mSkinnedModelInst;

	//재생 중인 클립 이름
	std::string mSkinnedClipName;

	//로더가 알려준 클립 목록 (ImGui 콤보박스용)
	std::vector<std::string> mClipNames;

};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        CubeMapApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

CubeMapApp::CubeMapApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

CubeMapApp::~CubeMapApp()
{
	//osb
	//ImGui 해제
	if (md3dDevice != nullptr)
		FlushCommandQueue();

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

bool CubeMapApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;


    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mCamera.SetPosition(0.0f, 2.0f, -15.0f);
 
	//osb
	mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	mSceneBounds.Radius = sqrtf(10.0f * 10.0f + 15.0f * 15.0f);  // 씬 크기에 맞게 조정 가능
	//그림자맵 생성
	mShadowMap = std::make_unique<ShadowMap>(md3dDevice.Get(), 2048, 2048);
	//mSsao도 BuildDescriptorHeaps()보다 먼저 생성
	mSsao = std::make_unique<Ssao>(md3dDevice.Get(), mCommandList.Get(), mClientWidth, mClientHeight);


	LoadTextures();
    BuildRootSignature();
	//osb
	BuildSsaoRootSignature();
	BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
    BuildSkullGeometry();
	BuildDebugQuadGeometry();
	LoadSkinnedModel();
	BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

	//osb
	mSsao->SetPSOs(mPSOs["ssao"].Get(), mPSOs["ssaoBlur"].Get());

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

	//osb
	InitImGui();

    return true;
}
 
UINT CubeMapApp::GetImGuiSrvIndex() const
{
	return mImguiSrvIndex;
}

void CubeMapApp::OnResize()
{
    D3DApp::OnResize();

	mCamera.SetLens(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

	//osb
	if (mSsao != nullptr)
	{
		mSsao->OnResize(mClientWidth, mClientHeight);
		mSsao->RebuildDescriptors(mDepthStencilBuffer.Get());
	}
}

void CubeMapApp::Update(const GameTimer& gt)
{
	//osb
	SetImGui(gt);
    OnKeyboardInput(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	//회전 각도 증가는 mLightRotate가 true일 때만
	if (mLightRotate)
	{
		mLightRotationAngle += 0.1f * gt.DeltaTime();
	}

	//방향 재계산은 항상 수행하도록 함. 체크 해제 시엔 마지막 각도로 고정된 상태 유지
	XMMATRIX R = XMMatrixRotationY(mLightRotationAngle);
	for (int i = 0; i < 3; ++i)
	{
		XMVECTOR lightDir = XMLoadFloat3(&mBaseLightDirections[i]);
		lightDir = XMVector3TransformNormal(lightDir, R);
		XMStoreFloat3(&mRotatedLightDirections[i], lightDir);
	}

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateSkinnedCBs(gt);
	UpdateMaterialBuffer(gt);
	//osb
	//MainPassCB보다 먼저
	UpdateShadowTransform(gt);
	UpdateMainPassCB(gt);
	//MainPassCB보다 이후
	UpdateShadowPassCB(gt);
	//반드시 UpdateMainPassCB() 이후
	UpdateSsaoCB(gt);
}

void CubeMapApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	ThrowIfFailed(cmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(3, matBuffer->GetGPUVirtualAddress());

	//그림자맵 패스
	DrawSceneToShadowMap();

	//노멀/깊이 프리패스
	DrawNormalsAndDepth();

	//ssao 계산 - 루트 시그니처를 ssao 전용으로 전환
	mCommandList->SetGraphicsRootSignature(mSsaoRootSignature.Get());
	mSsao->ComputeSsao(mCommandList.Get(), mCurrFrameResource, 3);

	//메인 렌더링 - 루트 시그니처를 다시 메인으로 전환
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	//루트 시그니처가 바뀌었으니 바인딩들 다시 설정
	matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(3, matBuffer->GetGPUVirtualAddress());

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	//백버퍼만 클리어. 깊이버퍼는 DrawNormalsAndDepth()에서 이미 채웠으니 다시 클리어하면 안 됨
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	//스카이박스 (선택된 큐브맵)
	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	skyTexDescriptor.Offset(mSkyTexHeapIndices[mCurrSkyIndex], mCbvSrvDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(4, skyTexDescriptor);

	//그림자맵
	CD3DX12_GPU_DESCRIPTOR_HANDLE shadowMapDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	shadowMapDescriptor.Offset(mShadowMapHeapIndex, mCbvSrvDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(5, shadowMapDescriptor);

	//ssao맵 - mSsao가 직접 알려주는 핸들 사용 
	if (mSsaoEnabled)
	{
		mCommandList->SetGraphicsRootDescriptorTable(6, mSsao->AmbientMapSrv());
	}
	else
	{
		mCommandList->SetGraphicsRootDescriptorTable(6, GetGpuSrv(2));  // white 텍스처 = 가림 없음
	}

	//디퓨즈 텍스처 배열 
	mCommandList->SetGraphicsRootDescriptorTable(7, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	mCommandList->SetPipelineState(mPSOs["opaque"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs["skinnedOpaque"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::SkinnedOpaque]);

	mCommandList->SetPipelineState(mPSOs["sky"].Get());
	if (mSkyEnabled)
	{
		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Sky]);
	}

	if (mShowShadowDebug)
	{
		mCommandList->SetPipelineState(mPSOs["debug"].Get());
		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Debug]);
	}

	if (mShowSsaoDebug)
	{
		mCommandList->SetPipelineState(mPSOs["ssaoDebug"].Get());
		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::SsaoDebug]);
	}

	ImGui::Render();
	ID3D12DescriptorHeap* imguiHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(1, imguiHeaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurrFrameResource->Fence = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void CubeMapApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	//osb
	//마우스 입력 함수에 가드 추가
	if (ImGui::GetCurrentContext() 
	 && ImGui::GetIO().WantCaptureMouse) return;

    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void CubeMapApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	//osb
	//마우스 입력 함수에 가드 추가
	if (ImGui::GetCurrentContext()
		&& ImGui::GetIO().WantCaptureMouse) return;

    ReleaseCapture();
}

void CubeMapApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	//osb
	//마우스 입력 함수에 가드 추가
	if (ImGui::GetCurrentContext()
		&& ImGui::GetIO().WantCaptureMouse) return;

    if((btnState & MK_LBUTTON) != 0)
    {
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}
 
void CubeMapApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	if(GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(10.0f*dt);

	if(GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-10.0f*dt);

	if(GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-10.0f*dt);

	if(GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(10.0f*dt);

	mCamera.UpdateViewMatrix();
}
 
void CubeMapApp::AnimateMaterials(const GameTimer& gt)
{
	
}

void CubeMapApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for(auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if(e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.MaterialIndex = e->Mat->MatCBIndex;

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void CubeMapApp::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;

			currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void CubeMapApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));

	//osb
	//ssaoApp 소스 가져옮
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);
	XMMATRIX viewProjTex = XMMatrixMultiply(viewProj, T);
	XMStoreFloat4x4(&mMainPassCB.ViewProjTex, XMMatrixTranspose(viewProjTex));

	//osb
	//그림자 변환 행렬도 메인 패스 CB에 같이 채움
	XMMATRIX shadowTransform = XMLoadFloat4x4(&mShadowTransform);
	XMStoreFloat4x4(&mMainPassCB.ShadowTransform, XMMatrixTranspose(shadowTransform));

	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = mRotatedLightDirections[0];
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	mMainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void CubeMapApp::UpdateShadowTransform(const GameTimer& gt)
{
	//메인 방향광(Lights[0])을 그림자를 만드는 빛으로 사용
	XMVECTOR lightDir = XMLoadFloat3(&mMainPassCB.Lights[0].Direction);
	XMVECTOR lightPos = -2.0f * mSceneBounds.Radius * lightDir;
	XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);
	XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

	XMStoreFloat3(&mLightPosW, lightPos);

	//씬 바운딩 구를 빛의 시점 공간으로 옮겨서 직교투영 범위를 계산
	XMFLOAT3 sphereCenterLS;
	XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

	float l = sphereCenterLS.x - mSceneBounds.Radius;
	float b = sphereCenterLS.y - mSceneBounds.Radius;
	float n = sphereCenterLS.z - mSceneBounds.Radius;
	float r = sphereCenterLS.x + mSceneBounds.Radius;
	float t = sphereCenterLS.y + mSceneBounds.Radius;
	float f = sphereCenterLS.z + mSceneBounds.Radius;

	mLightNearZ = n;
	mLightFarZ = f;
	XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

	//NDC [-1,1] -> 텍스처 좌표 [0,1] 변환 행렬
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX S = lightView * lightProj * T;
	XMStoreFloat4x4(&mLightView, lightView);
	XMStoreFloat4x4(&mLightProj, lightProj);
	XMStoreFloat4x4(&mShadowTransform, S);
}

void CubeMapApp::UpdateShadowPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mLightView);
	XMMATRIX proj = XMLoadFloat4x4(&mLightProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	UINT w = mShadowMap->Width();
	UINT h = mShadowMap->Height();

	XMStoreFloat4x4(&mShadowPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mShadowPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mShadowPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mShadowPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mShadowPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mShadowPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mShadowPassCB.EyePosW = mLightPosW;
	mShadowPassCB.RenderTargetSize = XMFLOAT2((float)w, (float)h);
	mShadowPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / w, 1.0f / h);
	mShadowPassCB.NearZ = mLightNearZ;
	mShadowPassCB.FarZ = mLightFarZ;

	//그림자 패스는 1번 슬롯에 저장하고 메인 패스는 0번
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(1, mShadowPassCB);
}

void CubeMapApp::UpdateSkinnedCBs(const GameTimer& gt)
{
	if (mSkinnedModelInst == nullptr)
		return;

	auto currSkinnedCB = mCurrFrameResource->SkinnedCB.get();

	mSkinnedModelInst->UpdateSkinnedAnimation(gt.DeltaTime());

	SkinnedConstants skinnedConstants;

	std::copy(
		std::begin(mSkinnedModelInst->FinalTransforms),
		std::end(mSkinnedModelInst->FinalTransforms),
		&skinnedConstants.BoneTransforms[0]);

	currSkinnedCB->CopyData(0, skinnedConstants);
}

void CubeMapApp::BuildSsaoRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstants(1, 1);
	slotRootParameter[2].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		0, D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		1, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
		2, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		0.0f, 0, D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		3, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> staticSamplers =
	{
		pointClamp, linearClamp, depthMapSam, linearWrap
	};

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mSsaoRootSignature.GetAddressOf())));
}

void CubeMapApp::UpdateSsaoCB(const GameTimer& gt)
{
	SsaoConstants ssaoCB;

	XMMATRIX P = mCamera.GetProj();

	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	ssaoCB.Proj = mMainPassCB.Proj;
	ssaoCB.InvProj = mMainPassCB.InvProj;
	XMStoreFloat4x4(&ssaoCB.ProjTex, XMMatrixTranspose(P * T));

	mSsao->GetOffsetVectors(ssaoCB.OffsetVectors);

	auto blurWeights = mSsao->CalcGaussWeights(2.5f);
	ssaoCB.BlurWeights[0] = XMFLOAT4(&blurWeights[0]);
	ssaoCB.BlurWeights[1] = XMFLOAT4(&blurWeights[4]);
	ssaoCB.BlurWeights[2] = XMFLOAT4(&blurWeights[8]);

	ssaoCB.InvRenderTargetSize = XMFLOAT2(1.0f / mSsao->SsaoMapWidth(), 1.0f / mSsao->SsaoMapHeight());

	ssaoCB.OcclusionRadius = 0.5f;
	ssaoCB.OcclusionFadeStart = 0.2f;
	ssaoCB.OcclusionFadeEnd = 1.0f;
	ssaoCB.SurfaceEpsilon = 0.05f;

	auto currSsaoCB = mCurrFrameResource->SsaoCB.get();
	currSsaoCB->CopyData(0, ssaoCB);
}

void CubeMapApp::DrawNormalsAndDepth()
{
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	auto normalMap = mSsao->NormalMap();
	auto normalMapRtv = mSsao->NormalMapRtv();

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

	float clearValue[] = { 0.0f, 0.0f, 1.0f, 0.0f };
	mCommandList->ClearRenderTargetView(normalMapRtv, clearValue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, &normalMapRtv, true, &DepthStencilView());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	mCommandList->SetPipelineState(mPSOs["drawNormals"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs["skinnedDrawNormals"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::SkinnedOpaque]);

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
}

void CubeMapApp::CreateRtvAndDsvDescriptorHeaps()
{
	//노멀맵1, 앰비언트맵2  rtv 3개 추가
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 3;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	//dsv힙은 그대로 2개 - ssao는 메인 깊이버퍼를 재사용해서 별도의 dsv 필요없음
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 2;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void CubeMapApp::LoadTextures()
{
	//osb
	//텍스쳐 이름 정의
    std::vector<std::string> texNames =
    {
        "bricksDiffuseMap",
        "tileDiffuseMap",
        "defaultDiffuseMap",
        "grassCubeMap",
		"desertCubeMap",
		"snowCubeMap",
		"sunsetCubeMap"
    };

	//osb
	//추가된 텍스쳐 경로 정의
    std::vector<std::wstring> texFilenames =
    {
        L"../../Textures/bricks2.dds",
        L"../../Textures/tile.dds",
        L"../../Textures/white1x1.dds",
        L"../../Textures/grasscube1024.dds",  
		L"../../Textures/desertcube1024.dds",  
		L"../../Textures/snowcube1024.dds",   
		L"../../Textures/sunsetcube1024.dds"   
    };

    for (int i = 0; i < (int)texNames.size(); ++i)
    {
        auto texMap = std::make_unique<Texture>();
        texMap->Name = texNames[i];
        texMap->Filename = texFilenames[i];
        ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
            mCommandList.Get(), texMap->Filename.c_str(),
            texMap->Resource, texMap->UploadHeap));

        mTextures[texMap->Name] = std::move(texMap);
    }
}

void CubeMapApp::BuildRootSignature()
{
	//큐브맵(t0)
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

	//osb
	//그림자맵 루트 시그니쳐 초기화(t1)
	CD3DX12_DESCRIPTOR_RANGE texTableShadow;
	texTableShadow.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);

	//ssao맵 전용 테이블(t2)
	CD3DX12_DESCRIPTOR_RANGE texTableSsao;
	texTableSsao.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

	//diffuse맵 배열 (t3~)
	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 3, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[8];

	// Perfomance TIP: Order from most frequent to least frequent.
    slotRootParameter[0].InitAsConstantBufferView(0); //ObjectCB  b0
    slotRootParameter[1].InitAsConstantBufferView(1); //SkinnedCB b1
	slotRootParameter[2].InitAsConstantBufferView(2); //PassCB    b2
    slotRootParameter[3].InitAsShaderResourceView(0, 1);
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
	//osb
	//그림자맵 테이블 추가
	slotRootParameter[5].InitAsDescriptorTable(1, &texTableShadow, D3D12_SHADER_VISIBILITY_PIXEL);
	//ssao맵 테이블 추가
	slotRootParameter[6].InitAsDescriptorTable(1, &texTableSsao, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[7].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(8, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void CubeMapApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	//osb
	//bricks(0),tile(1),white(2) -> diffuse 배열용 5칸 확보(0~4)
	//그림자맵(5)
	//grass,desert,snow,sunset 원본(6~9)
	//ssao 5칸(10~14) ambientMap0,ambientMap1,normalMap,depthMap,randomVectorMap
	//ImGui(15)
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 16;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto bricksTex = mTextures["bricksDiffuseMap"]->Resource;
	auto tileTex = mTextures["tileDiffuseMap"]->Resource;
	auto whiteTex = mTextures["defaultDiffuseMap"]->Resource;

	//osb
	//절대 인덱스 함수 사용 방식으로 바꿈
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	srvDesc.Format = bricksTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, GetCpuSrv(0));

	srvDesc.Format = tileTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, GetCpuSrv(1));

	srvDesc.Format = whiteTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = whiteTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(whiteTex.Get(), &srvDesc, GetCpuSrv(2));

	//인덱스 5 - 그림자맵
	mShadowMapHeapIndex = 5;
	mShadowMap->BuildDescriptors(
		GetCpuSrv(mShadowMapHeapIndex),
		GetGpuSrv(mShadowMapHeapIndex),
		GetDsv(1));

	std::vector<std::string> skyTexKeys = { "grassCubeMap", "desertCubeMap", "snowCubeMap", "sunsetCubeMap" };

	for (int i = 0; i < skyTexKeys.size(); i++)
	{
		UINT heapIndex = 6 + i;
		auto skyTex = mTextures[skyTexKeys[i]]->Resource;

		D3D12_SHADER_RESOURCE_VIEW_DESC skySrvDesc = {};
		skySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		skySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		skySrvDesc.TextureCube.MostDetailedMip = 0;
		skySrvDesc.TextureCube.MipLevels = skyTex->GetDesc().MipLevels;
		skySrvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
		skySrvDesc.Format = skyTex->GetDesc().Format;
		md3dDevice->CreateShaderResourceView(skyTex.Get(), &skySrvDesc, GetCpuSrv(heapIndex));

		mSkyTexHeapIndices[i] = heapIndex;
	}

	//ssao 절대 인덱스 10부터 5칸, rtv는 SwapChainBufferCount부터 3칸
	mSsaoHeapIndexStart = 10;
	mSsao->BuildDescriptors(
		mDepthStencilBuffer.Get(),
		GetCpuSrv(mSsaoHeapIndexStart),
		GetGpuSrv(mSsaoHeapIndexStart),
		GetRtv(SwapChainBufferCount),
		mCbvSrvDescriptorSize,
		mRtvDescriptorSize);

	mImguiSrvIndex = 15;
}

void CubeMapApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	//osb
	//스키닝 셰이더용 매크로 
	// 배열 마지막을 null, null로 하는 이유는 d3d컴파일러가 이걸 종료 표시로 인식하기때문. 문자열의 \0처럼.
	// 빠뜨리면 엉뚱한 메모리를 읽음.
	const D3D_SHADER_MACRO skinnedDefines[] =
	{
		"SKINNED", "1",
		NULL, NULL
	};

	//osb
	//스키닝 관련 셰이더 컴파일 추가
	//PS가 없는 이유는 스키닝은 정점을 변환하는 단계에서만 일어나고 PS는 변환이 끝난 결과를 받아서 처리하기 때문.
	//같은 hlsl 파일을 매크로만 다르게 줘서 두 번 컴파일하여 재사용하는 방식. 원본은 하나라 한쪽에서 수정한 조명과 같은 처리는 다른 쪽에도 동일하게 적용됨.
	mShaders["skinnedVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", skinnedDefines, "VS", "vs_5_1");
	mShaders["skinnedShadowVS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", skinnedDefines, "VS", "vs_5_1");
	mShaders["skinnedDrawNormalsVS"] = d3dUtil::CompileShader(L"Shaders\\DrawNormals.hlsl", skinnedDefines, "VS", "vs_5_1");

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
	
	mShaders["skyVS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["skyPS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_1");

	//osb
	//그림자맵 렌더 패스용
	mShaders["shadowVS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["shadowOpaquePS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", nullptr, "PS", "ps_5_1");

	//그림자맵 디버그 뷰용
	mShaders["debugVS"] = d3dUtil::CompileShader(L"Shaders\\ShadowDebug.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["debugPS"] = d3dUtil::CompileShader(L"Shaders\\ShadowDebug.hlsl", nullptr, "PS", "ps_5_1");

	//ssao맵용
	mShaders["drawNormalsVS"] = d3dUtil::CompileShader(L"Shaders\\DrawNormals.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["drawNormalsPS"] = d3dUtil::CompileShader(L"Shaders\\DrawNormals.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["ssaoVS"] = d3dUtil::CompileShader(L"Shaders\\Ssao.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["ssaoPS"] = d3dUtil::CompileShader(L"Shaders\\Ssao.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["ssaoBlurVS"] = d3dUtil::CompileShader(L"Shaders\\SsaoBlur.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["ssaoBlurPS"] = d3dUtil::CompileShader(L"Shaders\\SsaoBlur.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["ssaoDebugVS"] = d3dUtil::CompileShader(L"Shaders\\SsaoDebug.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["ssaoDebugPS"] = d3dUtil::CompileShader(L"Shaders\\SsaoDebug.hlsl", nullptr, "PS", "ps_5_1");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

	//osb
	//스키닝 정점 레이아웃
	mSkinnedInputLayout =
	{
		{ "POSITION",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",      0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",    0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "WEIGHTS",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BONEINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,   0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void CubeMapApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for(size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for(size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
	}

	for(size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}

	for(size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size()  * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void CubeMapApp::BuildDebugQuadGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData quad = geoGen.CreateQuad(0.0f, 0.0f, 1.0f, 1.0f, 0.0f);

	std::vector<Vertex> vertices(quad.Vertices.size());
	for (size_t i = 0; i < quad.Vertices.size(); ++i)
	{
		vertices[i].Pos = quad.Vertices[i].Position;
		vertices[i].Normal = quad.Vertices[i].Normal;
		vertices[i].TexC = quad.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices = quad.GetIndices16();

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "debugQuadGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	geo->DrawArgs["quad"] = submesh;

	mGeometries["debugQuadGeo"] = std::move(geo);

	//ssao 디버그용 쿼드 추가
	GeometryGenerator::MeshData ssaoQuad = geoGen.CreateQuad(-1.0f, 0.0f, 1.0f, 1.0f, 0.0f);

	std::vector<Vertex> ssaoVertices(ssaoQuad.Vertices.size());
	for (size_t i = 0; i < ssaoQuad.Vertices.size(); ++i)
	{
		ssaoVertices[i].Pos = ssaoQuad.Vertices[i].Position;
		ssaoVertices[i].Normal = ssaoQuad.Vertices[i].Normal;
		ssaoVertices[i].TexC = ssaoQuad.Vertices[i].TexC;
	}
	std::vector<std::uint16_t> ssaoIndices = ssaoQuad.GetIndices16();

	const UINT ssaoVbByteSize = (UINT)ssaoVertices.size() * sizeof(Vertex);
	const UINT ssaoIbByteSize = (UINT)ssaoIndices.size() * sizeof(std::uint16_t);

	auto ssaoGeo = std::make_unique<MeshGeometry>();
	ssaoGeo->Name = "ssaoDebugQuadGeo";

	ThrowIfFailed(D3DCreateBlob(ssaoVbByteSize, &ssaoGeo->VertexBufferCPU));
	CopyMemory(ssaoGeo->VertexBufferCPU->GetBufferPointer(), ssaoVertices.data(), ssaoVbByteSize);
	ThrowIfFailed(D3DCreateBlob(ssaoIbByteSize, &ssaoGeo->IndexBufferCPU));
	CopyMemory(ssaoGeo->IndexBufferCPU->GetBufferPointer(), ssaoIndices.data(), ssaoIbByteSize);

	ssaoGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ssaoVertices.data(), ssaoVbByteSize, ssaoGeo->VertexBufferUploader);
	ssaoGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ssaoIndices.data(), ssaoIbByteSize, ssaoGeo->IndexBufferUploader);

	ssaoGeo->VertexByteStride = sizeof(Vertex);
	ssaoGeo->VertexBufferByteSize = ssaoVbByteSize;
	ssaoGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
	ssaoGeo->IndexBufferByteSize = ssaoIbByteSize;

	SubmeshGeometry ssaoSubmesh;
	ssaoSubmesh.IndexCount = (UINT)ssaoIndices.size();
	ssaoSubmesh.StartIndexLocation = 0;
	ssaoSubmesh.BaseVertexLocation = 0;
	ssaoGeo->DrawArgs["quad"] = ssaoSubmesh;

	mGeometries["ssaoDebugQuadGeo"] = std::move(ssaoGeo);
}

void CubeMapApp::BuildSkullGeometry()
{
    std::ifstream fin("Models/skull.txt");

    if (!fin)
    {
        MessageBox(0, L"Models/skull.txt not found.", 0, 0);
        return;
    }

    UINT vcount = 0;
    UINT tcount = 0;
    std::string ignore;

    fin >> ignore >> vcount;
    fin >> ignore >> tcount;
    fin >> ignore >> ignore >> ignore >> ignore;

    XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
    XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

    XMVECTOR vMin = XMLoadFloat3(&vMinf3);
    XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

    std::vector<Vertex> vertices(vcount);
    for (UINT i = 0; i < vcount; ++i)
    {
        fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
        fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;

        vertices[i].TexC = { 0.0f, 0.0f };

        XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

        vMin = XMVectorMin(vMin, P);
        vMax = XMVectorMax(vMax, P);
    }

    BoundingBox bounds;
    XMStoreFloat3(&bounds.Center, 0.5f*(vMin + vMax));
    XMStoreFloat3(&bounds.Extents, 0.5f*(vMax - vMin));

    fin >> ignore;
    fin >> ignore;
    fin >> ignore;

    std::vector<std::int32_t> indices(3 * tcount);
    for (UINT i = 0; i < tcount; ++i)
    {
        fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
    }

    fin.close();

    //
    // Pack the indices of all the meshes into one index buffer.
    //

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "skullGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;
    submesh.Bounds = bounds;

    geo->DrawArgs["skull"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void CubeMapApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;

	//
	// PSO for sky.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = opaquePsoDesc;
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyPsoDesc.pRootSignature = mRootSignature.Get();
	skyPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()), mShaders["skyVS"]->GetBufferSize() };
	skyPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()), mShaders["skyPS"]->GetBufferSize() };
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));

	//
	// PSO for shadow map pass.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc = opaquePsoDesc;
	smapPsoDesc.RasterizerState.DepthBias = 100000;
	smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
	smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
	smapPsoDesc.pRootSignature = mRootSignature.Get();
	smapPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["shadowVS"]->GetBufferPointer()), mShaders["shadowVS"]->GetBufferSize() };
	smapPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["shadowOpaquePS"]->GetBufferPointer()), mShaders["shadowOpaquePS"]->GetBufferSize() };
	smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	smapPsoDesc.NumRenderTargets = 0;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"])));

	//
	// PSO for shadow map debug view.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = opaquePsoDesc;
	debugPsoDesc.pRootSignature = mRootSignature.Get();
	debugPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["debugVS"]->GetBufferPointer()), mShaders["debugVS"]->GetBufferSize() };
	debugPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["debugPS"]->GetBufferPointer()), mShaders["debugPS"]->GetBufferSize() };
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSOs["debug"])));

	//
	// PSO for ssao map debug view.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoDebugPsoDesc = opaquePsoDesc;
	ssaoDebugPsoDesc.pRootSignature = mRootSignature.Get();
	ssaoDebugPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["ssaoDebugVS"]->GetBufferPointer()), mShaders["ssaoDebugVS"]->GetBufferSize() };
	ssaoDebugPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["ssaoDebugPS"]->GetBufferPointer()), mShaders["ssaoDebugPS"]->GetBufferSize() };
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&ssaoDebugPsoDesc, IID_PPV_ARGS(&mPSOs["ssaoDebug"])));

	//
	// PSO for drawing normals 
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC drawNormalsPsoDesc = opaquePsoDesc;
	drawNormalsPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["drawNormalsVS"]->GetBufferPointer()), mShaders["drawNormalsVS"]->GetBufferSize() };
	drawNormalsPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["drawNormalsPS"]->GetBufferPointer()), mShaders["drawNormalsPS"]->GetBufferSize() };
	drawNormalsPsoDesc.RTVFormats[0] = Ssao::NormalMapFormat;
	drawNormalsPsoDesc.SampleDesc.Count = 1;
	drawNormalsPsoDesc.SampleDesc.Quality = 0;
	drawNormalsPsoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&drawNormalsPsoDesc, IID_PPV_ARGS(&mPSOs["drawNormals"])));

	//
	// PSO for SSAO
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoPsoDesc = opaquePsoDesc;
	ssaoPsoDesc.InputLayout = { nullptr, 0 };
	ssaoPsoDesc.pRootSignature = mSsaoRootSignature.Get();
	ssaoPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["ssaoVS"]->GetBufferPointer()), mShaders["ssaoVS"]->GetBufferSize() };
	ssaoPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["ssaoPS"]->GetBufferPointer()), mShaders["ssaoPS"]->GetBufferSize() };
	ssaoPsoDesc.DepthStencilState.DepthEnable = false;
	ssaoPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ssaoPsoDesc.RTVFormats[0] = Ssao::AmbientMapFormat;
	ssaoPsoDesc.SampleDesc.Count = 1;
	ssaoPsoDesc.SampleDesc.Quality = 0;
	ssaoPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&ssaoPsoDesc, IID_PPV_ARGS(&mPSOs["ssao"])));

	//
	// PSO for SSAO blur
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoBlurPsoDesc = ssaoPsoDesc;
	ssaoBlurPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["ssaoBlurVS"]->GetBufferPointer()), mShaders["ssaoBlurVS"]->GetBufferSize() };
	ssaoBlurPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["ssaoBlurPS"]->GetBufferPointer()), mShaders["ssaoBlurPS"]->GetBufferSize() };
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&ssaoBlurPsoDesc, IID_PPV_ARGS(&mPSOs["ssaoBlur"])));

	//
    // 스키닝 그림자 패스 PSO
    //
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedSmapPsoDesc = smapPsoDesc;
	skinnedSmapPsoDesc.RasterizerState.FrontCounterClockwise = TRUE;
	skinnedSmapPsoDesc.InputLayout = { mSkinnedInputLayout.data(), (UINT)mSkinnedInputLayout.size() };
	skinnedSmapPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["skinnedShadowVS"]->GetBufferPointer()),
		mShaders["skinnedShadowVS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
		&skinnedSmapPsoDesc, IID_PPV_ARGS(&mPSOs["skinnedShadow_opaque"])));

	//
	// 스키닝 노멀/깊이 프리패스 PSO
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedDrawNormalsPsoDesc = drawNormalsPsoDesc;
	skinnedDrawNormalsPsoDesc.RasterizerState.FrontCounterClockwise = TRUE;
	skinnedDrawNormalsPsoDesc.InputLayout = { mSkinnedInputLayout.data(), (UINT)mSkinnedInputLayout.size() };
	skinnedDrawNormalsPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["skinnedDrawNormalsVS"]->GetBufferPointer()),
		mShaders["skinnedDrawNormalsVS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
		&skinnedDrawNormalsPsoDesc, IID_PPV_ARGS(&mPSOs["skinnedDrawNormals"])));

	//opaque의 깊이 설정을 EQUAL/ZERO로 변경
	opaquePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
	opaquePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	//
	// 스키닝 메인 패스 PSO
	// opaque가 EQUAL/ZERO로 바뀐 뒤에 복사해야 같은 깊이 설정을 가능.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedOpaquePsoDesc = opaquePsoDesc;
	skinnedOpaquePsoDesc.RasterizerState.FrontCounterClockwise = TRUE;
	skinnedOpaquePsoDesc.InputLayout = { mSkinnedInputLayout.data(), (UINT)mSkinnedInputLayout.size() };
	skinnedOpaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["skinnedVS"]->GetBufferPointer()),
		mShaders["skinnedVS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
		&skinnedOpaquePsoDesc, IID_PPV_ARGS(&mPSOs["skinnedOpaque"])));
}

void CubeMapApp::BuildFrameResources()
{
	//osb
	//PassCB 슬롯 2개로 수정
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            2, (UINT)mAllRitems.size(), 1 ,(UINT)mMaterials.size()));
    }
}

void CubeMapApp::BuildMaterials()
{
    auto bricks0 = std::make_unique<Material>();
    bricks0->Name = "bricks0";
    bricks0->MatCBIndex = 0;
    bricks0->DiffuseSrvHeapIndex = 0;
    bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricks0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    bricks0->Roughness = 0.3f;

    auto tile0 = std::make_unique<Material>();
    tile0->Name = "tile0";
    tile0->MatCBIndex = 1;
    tile0->DiffuseSrvHeapIndex = 1;
    tile0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
    tile0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    tile0->Roughness = 0.1f;

    auto mirror0 = std::make_unique<Material>();
    mirror0->Name = "mirror0";
    mirror0->MatCBIndex = 2;
    mirror0->DiffuseSrvHeapIndex = 2;
    mirror0->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.1f, 1.0f);
    mirror0->FresnelR0 = XMFLOAT3(0.98f, 0.97f, 0.95f);
    mirror0->Roughness = 0.1f;

    auto skullMat = std::make_unique<Material>();
    skullMat->Name = "skullMat";
    skullMat->MatCBIndex = 3;
    skullMat->DiffuseSrvHeapIndex = 2;
    skullMat->DiffuseAlbedo = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
    skullMat->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    skullMat->Roughness = 0.2f;

    auto sky = std::make_unique<Material>();
    sky->Name = "sky";
    sky->MatCBIndex = 4;
    sky->DiffuseSrvHeapIndex = 3;
    sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    sky->Roughness = 1.0f;

    mMaterials["bricks0"] = std::move(bricks0);
    mMaterials["tile0"] = std::move(tile0);
    mMaterials["mirror0"] = std::move(mirror0);
    mMaterials["skullMat"] = std::move(skullMat);
    mMaterials["sky"] = std::move(sky);
}

void CubeMapApp::BuildRenderItems()
{
	UINT objCBIndex = 0;
	auto skyRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skyRitem->World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
	skyRitem->TexTransform = MathHelper::Identity4x4();
	skyRitem->ObjCBIndex = objCBIndex++;
	skyRitem->Mat = mMaterials["sky"].get();
	skyRitem->Geo = mGeometries["shapeGeo"].get();
	skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;
	skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Sky].push_back(skyRitem.get());
	mAllRitems.push_back(std::move(skyRitem));

	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 1.0f, 2.0f)*XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	boxRitem->ObjCBIndex = objCBIndex++;
	boxRitem->Mat = mMaterials["bricks0"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));

    auto skullRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(0.4f, 0.4f, 0.4f)*XMMatrixTranslation(0.0f, 1.0f, 0.0f));
    skullRitem->TexTransform = MathHelper::Identity4x4();
    skullRitem->ObjCBIndex = objCBIndex++;
    skullRitem->Mat = mMaterials["skullMat"].get();
    skullRitem->Geo = mGeometries["skullGeo"].get();
    skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
    skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
    skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;

    mRitemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());
    mAllRitems.push_back(std::move(skullRitem));

    auto gridRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&gridRitem->World, XMMatrixScaling(2.0f, 1.0f, 4.0f));
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 16.0f, 1.0f));
	gridRitem->ObjCBIndex = objCBIndex++;
	gridRitem->Mat = mMaterials["tile0"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));

	XMMATRIX brickTexTransform = XMMatrixScaling(1.5f, 2.0f, 1.0f);
	
	for(int i = 0; i < 8; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i*5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i*5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i*5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i*5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = mMaterials["bricks0"].get();
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = mMaterials["bricks0"].get();
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->TexTransform = MathHelper::Identity4x4();
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = mMaterials["mirror0"].get();
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = mMaterials["mirror0"].get();
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSphereRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSphereRitem.get());

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}

	//osb
	//그림자맵, ssao용 디버그 뷰 쿼드 추가
	auto debugQuadRitem = std::make_unique<RenderItem>();
	debugQuadRitem->World = MathHelper::Identity4x4();
	debugQuadRitem->TexTransform = MathHelper::Identity4x4();
	debugQuadRitem->ObjCBIndex = (UINT)mAllRitems.size();
	debugQuadRitem->Mat = mMaterials["bricks0"].get();  // 실제 렌더링엔 안 쓰이는 더미
	debugQuadRitem->Geo = mGeometries["debugQuadGeo"].get();
	debugQuadRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	debugQuadRitem->IndexCount = debugQuadRitem->Geo->DrawArgs["quad"].IndexCount;
	debugQuadRitem->StartIndexLocation = debugQuadRitem->Geo->DrawArgs["quad"].StartIndexLocation;
	debugQuadRitem->BaseVertexLocation = debugQuadRitem->Geo->DrawArgs["quad"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Debug].push_back(debugQuadRitem.get());
	mAllRitems.push_back(std::move(debugQuadRitem));

	auto ssaoDebugQuadRitem = std::make_unique<RenderItem>();
	ssaoDebugQuadRitem->World = MathHelper::Identity4x4();
	ssaoDebugQuadRitem->TexTransform = MathHelper::Identity4x4();
	ssaoDebugQuadRitem->ObjCBIndex = (UINT)mAllRitems.size();
	ssaoDebugQuadRitem->Mat = mMaterials["bricks0"].get(); // 실제 렌더링엔 안 쓰이는 더미
	ssaoDebugQuadRitem->Geo = mGeometries["ssaoDebugQuadGeo"].get();
	ssaoDebugQuadRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	ssaoDebugQuadRitem->IndexCount = ssaoDebugQuadRitem->Geo->DrawArgs["quad"].IndexCount;
	ssaoDebugQuadRitem->StartIndexLocation = ssaoDebugQuadRitem->Geo->DrawArgs["quad"].StartIndexLocation;
	ssaoDebugQuadRitem->BaseVertexLocation = ssaoDebugQuadRitem->Geo->DrawArgs["quad"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::SsaoDebug].push_back(ssaoDebugQuadRitem.get());
	mAllRitems.push_back(std::move(ssaoDebugQuadRitem));

	//스키닝 캐릭터
	if (mSkinnedModelInst != nullptr)
	{
		auto skinnedRitem = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&skinnedRitem->World,
			XMMatrixScaling(0.01f, -0.01f, 0.01f) *
			XMMatrixTranslation(.0f, 2.0f, -5.0f));

		skinnedRitem->TexTransform = MathHelper::Identity4x4();
		skinnedRitem->ObjCBIndex = (UINT)mAllRitems.size();
		skinnedRitem->Mat = mMaterials["skullMat"].get();
		skinnedRitem->Geo = mGeometries["skinnedGeo"].get();
		skinnedRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		skinnedRitem->IndexCount = skinnedRitem->Geo->DrawArgs["mesh"].IndexCount;
		skinnedRitem->StartIndexLocation = skinnedRitem->Geo->DrawArgs["mesh"].StartIndexLocation;
		skinnedRitem->BaseVertexLocation = skinnedRitem->Geo->DrawArgs["mesh"].BaseVertexLocation;

		// 이 둘이 있어야 DrawRenderItems()가 스키닝 아이템으로 인식함
		skinnedRitem->SkinnedCBIndex = 0;
		skinnedRitem->SkinnedModelInst = mSkinnedModelInst.get();

		mRitemLayer[(int)RenderLayer::SkinnedOpaque].push_back(skinnedRitem.get());
		mAllRitems.push_back(std::move(skinnedRitem));
	}

}

void CubeMapApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT skinnedCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(SkinnedConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto skinnedCB = mCurrFrameResource->SkinnedCB->Resource();

	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress =
			objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		//스키닝 아이템이면 본 행렬 버퍼를 바인딩. 아니면 널을 바인딩.
		if (ri->SkinnedModelInst != nullptr)
		{
			D3D12_GPU_VIRTUAL_ADDRESS skinnedCBAddress =
				skinnedCB->GetGPUVirtualAddress() + ri->SkinnedCBIndex * skinnedCBByteSize;
			cmdList->SetGraphicsRootConstantBufferView(1, skinnedCBAddress);
		}
		else
		{
			cmdList->SetGraphicsRootConstantBufferView(1, 0);
		}

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1,
			ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}
void CubeMapApp::DrawSceneToShadowMap()
{
	mCommandList->RSSetViewports(1, &mShadowMap->Viewport());
	mCommandList->RSSetScissorRects(1, &mShadowMap->ScissorRect());

	//그림자맵을 깊이 쓰기 가능 상태로 전환
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	mCommandList->ClearDepthStencilView(mShadowMap->Dsv(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	//컬러 렌더 타겟 없이 깊이 버퍼만 바인딩
	mCommandList->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv());

	//그림자 패스 전용 PassCB(1) 바인딩
	auto passCB = mCurrFrameResource->PassCB->Resource();
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1 * passCBByteSize;
	mCommandList->SetGraphicsRootConstantBufferView(2, passCBAddress);

	mCommandList->SetPipelineState(mPSOs["shadow_opaque"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs["skinnedShadow_opaque"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::SkinnedOpaque]);

	//다시 셰이더에서 읽을 수 있는 상태로 전환
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
}


void CubeMapApp::LoadSkinnedModel()
{
	FbxLoader loader;
	loader.SetVerbose(true);

	std::vector<SkinnedVertex> vertices;
	std::vector<std::uint32_t> indices;
	//
	if (!loader.LoadFBX("Models\\HipHopDancing.fbx", vertices, indices, mSkinnedInfo))
	{
		MessageBoxA(0, loader.GetErrorMsg().c_str(), "FBX 로드 실패", MB_OK);
		return;
	}

	//-----------------------------------------------------------------
	// 정점/인덱스 버퍼 생성
	//-----------------------------------------------------------------
	const UINT vbByteSize = (UINT)vertices.size() * sizeof(SkinnedVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skinnedGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(SkinnedVertex);
	geo->VertexBufferByteSize = vbByteSize;

	// 정점이 16만 개라 16비트로는 표현 불가. 반드시 32비트.
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	geo->DrawArgs["mesh"] = submesh;

	mGeometries[geo->Name] = std::move(geo);

	//-----------------------------------------------------------------
	// 애니메이션 인스턴스 생성
	//-----------------------------------------------------------------
	mClipNames = loader.GetClipNames();
	if (mClipNames.empty())
	{
		MessageBoxA(0, "애니메이션 클립이 없습니다.", "FBX 로드", MB_OK);
		return;
	}

	mSkinnedClipName = mClipNames[0];

	mSkinnedModelInst = std::make_unique<SkinnedModelInstance>();
	mSkinnedModelInst->SkinnedInfo = &mSkinnedInfo;
	mSkinnedModelInst->ClipName = mSkinnedClipName;
	mSkinnedModelInst->TimePos = 0.0f;
	mSkinnedModelInst->FinalTransforms.resize(mSkinnedInfo.BoneCount());
}
//osb
// 6 ->7로 변경
std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> CubeMapApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	//osb
	//그림자맵용 비교 샘플러 추가
	const CD3DX12_STATIC_SAMPLER_DESC shadow(
		6,                                               //shaderRegister (s6)
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,//filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		0.0f,
		16,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);

	return { 
		pointWrap, pointClamp,
		linearWrap, linearClamp, 
		anisotropicWrap, anisotropicClamp,
		shadow //그림자 추가
	};
}

CD3DX12_CPU_DESCRIPTOR_HANDLE CubeMapApp::GetCpuSrv(int index) const
{
	auto srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	srv.Offset(index, mCbvSrvDescriptorSize);
	return srv;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE CubeMapApp::GetGpuSrv(int index) const
{
	auto srv = CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	srv.Offset(index, mCbvSrvDescriptorSize);
	return srv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE CubeMapApp::GetDsv(int index) const
{
	auto dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
	dsv.Offset(index, mDsvDescriptorSize);
	return dsv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE CubeMapApp::GetRtv(int index) const
{
	auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	rtv.Offset(index, mRtvDescriptorSize);
	return rtv;
}

void CubeMapApp::InitImGui()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui_ImplWin32_Init(mhMainWnd);

	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = md3dDevice.Get();
	init_info.CommandQueue = mCommandQueue.Get();
	init_info.NumFramesInFlight = gNumFrameResources;
	init_info.RTVFormat = mBackBufferFormat;
	init_info.SrvDescriptorHeap = mSrvDescriptorHeap.Get();
	init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info,
		D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu,
		D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu)
		{
			auto* app = static_cast<CubeMapApp*>(D3DApp::GetApp());
			UINT size = static_cast<ID3D12Device*>(info->Device)
				->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			out_cpu->ptr = info->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr
				+ app->GetImGuiSrvIndex() * size;
			out_gpu->ptr = info->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr
				+ app->GetImGuiSrvIndex() * size;
		};
	init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*,
		D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};

	ImGui_ImplDX12_Init(&init_info);
}

void CubeMapApp::SetImGui(const GameTimer& gt)
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin("Settings");
	ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
	ImGui::Checkbox("Sky Box", &mSkyEnabled);
	if (mSkyEnabled)
	{
		//dx12는 c++기반이지만 imgui는 c기반이라서 string을 const char*로 변환해야함
		std::vector<const char*> skyNamesC;
		for (auto& name : mSkyTexNames)
			skyNamesC.push_back(name.c_str());

		ImGui::Combo("Sky Texture", (int*)&mCurrSkyIndex, skyNamesC.data(), (int)skyNamesC.size());
	}
	ImGui::Checkbox("Shadow Map View", &mShowShadowDebug);
	ImGui::Checkbox("Light Rotate", &mLightRotate);
	ImGui::Checkbox("SSAO", &mSsaoEnabled);
	ImGui::Checkbox("SSAO Map View", &mShowSsaoDebug);


	ImGui::End();

}

