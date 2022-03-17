//***************************************************************************************
// TexWavesApp.cpp 
//***************************************************************************************

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "Waves.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;
 
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
};

enum class RenderLayer : int
{
	Opaque = 0,
	Count
};

class TexWavesApp : public D3DApp
{
public:
    TexWavesApp(HINSTANCE hInstance);
    TexWavesApp(const TexWavesApp& rhs) = delete;
    TexWavesApp& operator=(const TexWavesApp& rhs) = delete;
    ~TexWavesApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt); 

	void LoadTextures();
    void BuildRootSignature();
	void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
	void BuildCastleGeometry();
    void BuildLandGeometry();
    void BuildWavesGeometry();
	void BuildBoxGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

    float GetHillsHeight(float x, float z)const;
    XMFLOAT3 GetHillsNormal(float x, float z)const;

private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
 
    RenderItem* mWavesRitem = nullptr;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> mWaves;

    PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f*XM_PI;
    float mPhi = XM_PIDIV2 - 0.1f;
    float mRadius = 50.0f;

    POINT mLastMousePos;
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
        TexWavesApp theApp(hInstance);
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

TexWavesApp::TexWavesApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

TexWavesApp::~TexWavesApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool TexWavesApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);
 
	LoadTextures();
    BuildRootSignature();
	BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
	BuildCastleGeometry();
    BuildLandGeometry();
    BuildWavesGeometry();
	BuildBoxGeometry();
	BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}
 
void TexWavesApp::OnResize()
{
    D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void TexWavesApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
	UpdateCamera(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
    UpdateWaves(gt);
}

void TexWavesApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TexWavesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void TexWavesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void TexWavesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.2f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.2f*static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}
 
void TexWavesApp::OnKeyboardInput(const GameTimer& gt)
{
}
 
void TexWavesApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius*sinf(mPhi)*cosf(mTheta);
	mEyePos.z = mRadius*sinf(mPhi)*sinf(mTheta);
	mEyePos.y = mRadius*cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void TexWavesApp::AnimateMaterials(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["water"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if(tu >= 1.0f)
		tu -= 1.0f;

	if(tv >= 1.0f)
		tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// Material has changed, so need to update cbuffer.
	waterMat->NumFramesDirty = gNumFrameResources;
}

void TexWavesApp::UpdateObjectCBs(const GameTimer& gt)
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

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void TexWavesApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void TexWavesApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

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
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 1, 1, 1, 1.0f };

	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.2f, 0.1f, 0.0f };
	
	mMainPassCB.Lights[1].Position = { -9.0f, 13.0f, -9.0f };
	mMainPassCB.Lights[1].Direction = { 0.0f, -5.0f, 0.0f };
	mMainPassCB.Lights[1].Strength = { 0.541f, 0.984f, 1.0f };
	mMainPassCB.Lights[1].SpotPower = 0.35;

	mMainPassCB.Lights[2].Position = { 9.0f, 13.0f, -9.0f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -5.0f, 0.0f };
	mMainPassCB.Lights[2].Strength = { 0.541f, 0.984f, 1.0f };
	mMainPassCB.Lights[2].SpotPower = 0.35;

	mMainPassCB.Lights[3].Position = { -9.0f, 13.0f, 9.0f };
	mMainPassCB.Lights[3].Direction = { 0.0f, -5.0f, 0.0f };
	mMainPassCB.Lights[3].Strength = { 0.541f, 0.984f, 1.0f };
	mMainPassCB.Lights[3].SpotPower = 0.35;

	mMainPassCB.Lights[4].Position = { 9.0f, 13.0f, 9.0f };
	mMainPassCB.Lights[4].Direction = { 0.0f, -5.0f, 0.0f };
	mMainPassCB.Lights[4].Strength = { 0.541f, 0.984f, 1.0f };
	mMainPassCB.Lights[4].SpotPower = 0.35;

	mMainPassCB.Lights[5].Position = { 0.0f, 18.0f, 0.0f };
	mMainPassCB.Lights[5].Direction = { 0.0f, -5.0f, 0.0f };
	mMainPassCB.Lights[5].Strength = { 1, 0, 0 };
	mMainPassCB.Lights[5].SpotPower = 0.95;

	//mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	//mMainPassCB.Lights[1].Strength = { 0.5f, 0.5f, 0.5f };
	//mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	//mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void TexWavesApp::UpdateWaves(const GameTimer& gt)
{
	// Every quarter second, generate a random wave.
	static float t_base = 0.0f;
	if((mTimer.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;

		int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
		int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

		float r = MathHelper::RandF(0.2f, 0.5f);

		mWaves->Disturb(i, j, r);
	}

	// Update the wave simulation.
	mWaves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = mCurrFrameResource->WavesVB.get();
	for(int i = 0; i < mWaves->VertexCount(); ++i)
	{
		Vertex v;

		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);
		
		// Derive tex-coords from position by 
		// mapping [-w/2,w/2] --> [0,1]
		v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
		v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void TexWavesApp::LoadTextures()
{
	auto grassTex = std::make_unique<Texture>();
	grassTex->Name = "grassTex";
	grassTex->Filename = L"../Textures/grass.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), grassTex->Filename.c_str(),
		grassTex->Resource, grassTex->UploadHeap));

	auto waterTex = std::make_unique<Texture>();
	waterTex->Name = "waterTex";
	waterTex->Filename = L"../Textures/water1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), waterTex->Filename.c_str(),
		waterTex->Resource, waterTex->UploadHeap));

	auto fenceTex = std::make_unique<Texture>();
	fenceTex->Name = "fenceTex";
	fenceTex->Filename = L"../Textures/WoodCrate01.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), fenceTex->Filename.c_str(),
		fenceTex->Resource, fenceTex->UploadHeap));

	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Name = "stoneTex";
	stoneTex->Filename = L"../Textures/stone2.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), stoneTex->Filename.c_str(),
		stoneTex->Resource, stoneTex->UploadHeap));


	mTextures[grassTex->Name] = std::move(grassTex);
	mTextures[waterTex->Name] = std::move(waterTex);
	mTextures[fenceTex->Name] = std::move(fenceTex);
	mTextures[stoneTex->Name] = std::move(stoneTex);
}

void TexWavesApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[1].InitAsConstantBufferView(0);
    slotRootParameter[2].InitAsConstantBufferView(1);
    slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
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

void TexWavesApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 4;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto grassTex = mTextures["grassTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto fenceTex = mTextures["fenceTex"]->Resource;
	auto stoneTex = mTextures["stoneTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = grassTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = waterTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = fenceTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(fenceTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = stoneTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);
}

void TexWavesApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_0");
	
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void TexWavesApp::BuildCastleGeometry()
{

	std::vector<GeometryGenerator::MeshData> shapesVector;

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData wholeWall = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(24.0f, 24.0f, 25, 25);
	GeometryGenerator::MeshData column = geoGen.CreateCylinder(0.5f, 0.5f, 1.0f, 20, 20);
	GeometryGenerator::MeshData columnTop = geoGen.CreateSphere(0.5f, 4, 2);
	GeometryGenerator::MeshData Base1 = geoGen.CreateCylinder(0.5f, 0.5f, 1.0f, 10, 2);
	GeometryGenerator::MeshData Base2 = geoGen.CreateCylinder(0.5f, 0.5f, 1.0f, 8, 2);
	GeometryGenerator::MeshData Base3 = geoGen.CreateCylinder(0.5f, 0.0f, 1.0f, 10, 1);
	GeometryGenerator::MeshData top = geoGen.CreateSphere(0.5f, 11, 10);

	shapesVector.push_back(wholeWall);
	shapesVector.push_back(grid);
	shapesVector.push_back(column);
	shapesVector.push_back(columnTop);
	shapesVector.push_back(Base1);
	shapesVector.push_back(Base2);
	shapesVector.push_back(Base3);
	shapesVector.push_back(top);

	//Step1
	/*GeometryGenerator::MeshData grid = geoGen.CreateGrid(24.0f, 24.0f, 25, 25);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 10, 2);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);*/


	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT wholeWallVertexOffset = 0;
	UINT gridVertexOffset = (UINT)wholeWall.Vertices.size();
	UINT columnVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT columnTopVertexOffset = columnVertexOffset + (UINT)column.Vertices.size();
	UINT Base1VertexOffset = columnTopVertexOffset + (UINT)columnTop.Vertices.size();
	UINT Base2VertexOffset = Base1VertexOffset + (UINT)Base1.Vertices.size();
	UINT Base3VertexOffset = Base2VertexOffset + (UINT)Base2.Vertices.size();
	UINT topVertexOffset = Base3VertexOffset + (UINT)Base3.Vertices.size();

	//Step2
	/*UINT gridVertexOffset = (UINT)wholeWall.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();*/

	// Cache the starting index for each object in the concatenated index buffer.
	UINT wholeWallIndexOffset = 0;
	UINT gridIndexOffset = (UINT)wholeWall.Indices32.size();
	UINT columnIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT columnTopIndexOffset = columnIndexOffset + (UINT)column.Indices32.size();
	UINT Base1IndexOffset = columnTopIndexOffset + (UINT)columnTop.Indices32.size();
	UINT Base2IndexOffset = Base1IndexOffset + (UINT)Base1.Indices32.size();
	UINT Base3IndexOffset = Base2IndexOffset + (UINT)Base2.Indices32.size();
	UINT topIndexOffset = Base3IndexOffset + (UINT)Base3.Indices32.size();


	//Step3
	/*UINT gridIndexOffset = (UINT)wholeWall.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();*/

	// Define the SubmeshGeometry that cover different 
	// regions of the vertex/index buffers.

	SubmeshGeometry wholeWallSubmesh;
	wholeWallSubmesh.IndexCount = (UINT)wholeWall.Indices32.size();
	wholeWallSubmesh.StartIndexLocation = wholeWallIndexOffset;
	wholeWallSubmesh.BaseVertexLocation = wholeWallVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry columnSubmesh;
	columnSubmesh.IndexCount = (UINT)column.Indices32.size();
	columnSubmesh.StartIndexLocation = columnIndexOffset;
	columnSubmesh.BaseVertexLocation = columnVertexOffset;

	SubmeshGeometry columnTopSubmesh;
	columnTopSubmesh.IndexCount = (UINT)columnTop.Indices32.size();
	columnTopSubmesh.StartIndexLocation = columnTopIndexOffset;
	columnTopSubmesh.BaseVertexLocation = columnTopVertexOffset;

	SubmeshGeometry Base1Submesh;
	Base1Submesh.IndexCount = (UINT)Base1.Indices32.size();
	Base1Submesh.StartIndexLocation = Base1IndexOffset;
	Base1Submesh.BaseVertexLocation = Base1VertexOffset;

	SubmeshGeometry Base2Submesh;
	Base2Submesh.IndexCount = (UINT)Base2.Indices32.size();
	Base2Submesh.StartIndexLocation = Base2IndexOffset;
	Base2Submesh.BaseVertexLocation = Base2VertexOffset;

	SubmeshGeometry Base3Submesh;
	Base3Submesh.IndexCount = (UINT)Base3.Indices32.size();
	Base3Submesh.StartIndexLocation = Base3IndexOffset;
	Base3Submesh.BaseVertexLocation = Base3VertexOffset;

	SubmeshGeometry topSubmesh;
	topSubmesh.IndexCount = (UINT)top.Indices32.size();
	topSubmesh.StartIndexLocation = topIndexOffset;
	topSubmesh.BaseVertexLocation = topVertexOffset;

	//step4
	/*SubmeshGeometry gridSubmesh;
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
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;*/

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		wholeWall.Vertices.size() +
		grid.Vertices.size() +
		column.Vertices.size() +
		columnTop.Vertices.size() +
		Base1.Vertices.size() +
		Base2.Vertices.size() +
		Base3.Vertices.size() +
		top.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < shapesVector.size(); i++)
	{
		for (size_t j = 0; j < shapesVector[i].Vertices.size(); j++, k++)
		{
			auto& p = shapesVector[i].Vertices[j].Position;
			vertices[k].Pos = p;
			vertices[k].Normal = shapesVector[i].Vertices[j].Normal;
			vertices[k].TexC = shapesVector[i].Vertices[j].TexC;
		}
	}

	//for (size_t i = 0; i < wholeWall.Vertices.size(); ++i, ++k)
	//{
	//	vertices[k].Pos = wholeWall.Vertices[i].Position;
	//	vertices[k].Color = XMFLOAT4(DirectX::Colors::Red);
	//}

	////step6
	//for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	//{
	//	vertices[k].Pos = grid.Vertices[i].Position;
	//	vertices[k].Color = XMFLOAT4({ 0, 0.1f, 0, 1 });
	//}

	//for (size_t i = 0; i < column.Vertices.size(); ++i, ++k)
	//{
	//	vertices[k].Pos = column.Vertices[i].Position;
	//	vertices[k].Color = XMFLOAT4(DirectX::Colors::Green);
	//}

	//for (size_t i = 0; i < columnTop.Vertices.size(); ++i, ++k)
	//{
	//	vertices[k].Pos = columnTop.Vertices[i].Position;
	//	vertices[k].Color = XMFLOAT4(DirectX::Colors::Yellow);
	//}

	//for (size_t i = 0; i < Base1.Vertices.size(); ++i, ++k)
	//{
	//	vertices[k].Pos = Base1.Vertices[i].Position;
	//	vertices[k].Color = XMFLOAT4(DirectX::Colors::Blue);
	//}

	//for (size_t i = 0; i < Base2.Vertices.size(); ++i, ++k)
	//{
	//	vertices[k].Pos = Base2.Vertices[i].Position;
	//	vertices[k].Color = XMFLOAT4(DirectX::Colors::DeepPink);
	//}

	//for (size_t i = 0; i < Base3.Vertices.size(); ++i, ++k)
	//{
	//	vertices[k].Pos = Base3.Vertices[i].Position;
	//	vertices[k].Color = XMFLOAT4(DirectX::Colors::Cyan);
	//}

	//for (size_t i = 0; i < top.Vertices.size(); ++i, ++k)
	//{
	//	vertices[k].Pos = top.Vertices[i].Position;
	//	vertices[k].Color = XMFLOAT4(DirectX::Colors::Red);
	//}


	//for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	//{
	//	vertices[k].Pos = cylinder.Vertices[i].Position;
	//	vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
	//}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(wholeWall.GetIndices16()), std::end(wholeWall.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(column.GetIndices16()), std::end(column.GetIndices16()));
	indices.insert(indices.end(), std::begin(columnTop.GetIndices16()), std::end(columnTop.GetIndices16()));
	indices.insert(indices.end(), std::begin(Base1.GetIndices16()), std::end(Base1.GetIndices16()));
	indices.insert(indices.end(), std::begin(Base2.GetIndices16()), std::end(Base2.GetIndices16()));
	indices.insert(indices.end(), std::begin(Base3.GetIndices16()), std::end(Base3.GetIndices16()));
	indices.insert(indices.end(), std::begin(top.GetIndices16()), std::end(top.GetIndices16()));


	//indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

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

	geo->DrawArgs["wholeWall"] = wholeWallSubmesh;
	geo->DrawArgs["ground"] = gridSubmesh;
	geo->DrawArgs["column"] = columnSubmesh;
	geo->DrawArgs["columnTop"] = columnTopSubmesh;
	geo->DrawArgs["Base1"] = Base1Submesh;
	geo->DrawArgs["Base2"] = Base2Submesh;
	geo->DrawArgs["Base3"] = Base3Submesh;
	geo->DrawArgs["top"] = topSubmesh;
	//step8
	/*geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;*/

	mGeometries[geo->Name] = std::move(geo);

}

void TexWavesApp::BuildLandGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);

    //
    // Extract the vertex elements we are interested and apply the height function to
    // each vertex.  In addition, color the vertices based on their height so we have
    // sandy looking beaches, grassy low hills, and snow mountain peaks.
    //

    std::vector<Vertex> vertices(grid.Vertices.size());
    for(size_t i = 0; i < grid.Vertices.size(); ++i)
    {
        auto& p = grid.Vertices[i].Position;
        vertices[i].Pos = p;
        vertices[i].Pos.y = GetHillsHeight(p.x, p.z);
        vertices[i].Normal = GetHillsNormal(p.x, p.z);
		vertices[i].TexC = grid.Vertices[i].TexC;
    }

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    std::vector<std::uint16_t> indices = grid.GetIndices16();
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

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

	geo->DrawArgs["grid"] = submesh;

	mGeometries["landGeo"] = std::move(geo);
}

void TexWavesApp::BuildWavesGeometry()
{
    std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
	assert(mWaves->VertexCount() < 0x0000ffff);

    // Iterate over each quad.
    int m = mWaves->RowCount();
    int n = mWaves->ColumnCount();
    int k = 0;
    for(int i = 0; i < m - 1; ++i)
    {
        for(int j = 0; j < n - 1; ++j)
        {
            indices[k] = i*n + j;
            indices[k + 1] = i*n + j + 1;
            indices[k + 2] = (i + 1)*n + j;

            indices[k + 3] = (i + 1)*n + j;
            indices[k + 4] = i*n + j + 1;
            indices[k + 5] = (i + 1)*n + j + 1;

            k += 6; // next quad
        }
    }

	UINT vbByteSize = mWaves->VertexCount()*sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size()*sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	// Set dynamically.
	geo->VertexBufferCPU = nullptr;
	geo->VertexBufferGPU = nullptr;

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

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

	geo->DrawArgs["grid"] = submesh;

	mGeometries["waterGeo"] = std::move(geo);
}

void TexWavesApp::BuildBoxGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(8.0f, 8.0f, 8.0f, 3);

	std::vector<Vertex> vertices(box.Vertices.size());
	for (size_t i = 0; i < box.Vertices.size(); ++i)
	{
		auto& p = box.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = box.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "boxGeo";

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

	geo->DrawArgs["box"] = submesh;

	mGeometries["boxGeo"] = std::move(geo);
}

void TexWavesApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
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
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));
}

void TexWavesApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount()));
    }
}

void TexWavesApp::BuildMaterials()
{
	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 0;
	grass->DiffuseSrvHeapIndex = 0;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;

	// This is not a good water material definition, but we do not have all the rendering
	// tools we need (transparency, environment reflection), so we fake it for now.
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 1;
	water->DiffuseSrvHeapIndex = 1;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	water->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	water->Roughness = 0.0f;

	auto wirefence = std::make_unique<Material>();
	wirefence->Name = "wirefence";
	wirefence->MatCBIndex = 2;
	wirefence->DiffuseSrvHeapIndex = 2;
	wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	wirefence->Roughness = 0.25f;

	auto stone = std::make_unique<Material>();
	stone->Name = "stone";
	stone->MatCBIndex = 3;
	stone->DiffuseSrvHeapIndex = 3;
	stone->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	stone->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	stone->Roughness = 0.25f;

	mMaterials["grass"] = std::move(grass);
	mMaterials["water"] = std::move(water);
	mMaterials["wirefence"] = std::move(wirefence);
	mMaterials["stone"] = std::move(stone);
}

void TexWavesApp::BuildRenderItems()
{
	int cbindex = 0;

    auto wavesRitem = std::make_unique<RenderItem>();
    wavesRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	wavesRitem->ObjCBIndex = cbindex++;
	wavesRitem->Mat = mMaterials["water"].get();
	wavesRitem->Geo = mGeometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	//// we use mVavesRitem in updatewaves() to set the dynamic VB of the wave renderitem to the current frame VB.
    mWavesRitem = wavesRitem.get();

	mAllRitems.push_back(std::move(wavesRitem));
	
	//mRitemLayer[(int)RenderLayer::Opaque].push_back(wavesRitem.get());

   /* auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	gridRitem->ObjCBIndex = cbindex++;
	gridRitem->Mat = mMaterials["grass"].get();
	gridRitem->Geo = mGeometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mAllRitems.push_back(std::move(gridRitem));*/
	
	//mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());

	/*auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixTranslation(3.0f, 2.0f, -9.0f));
	boxRitem->ObjCBIndex = cbindex++;
	boxRitem->Mat = mMaterials["wirefence"].get();
	boxRitem->Geo = mGeometries["boxGeo"].get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

	mAllRitems.push_back(std::move(boxRitem));*/
	//mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());

	
	auto gridRitem2 = std::make_unique<RenderItem>();
	gridRitem2->World = MathHelper::Identity4x4();
	gridRitem2->ObjCBIndex = cbindex++;
	gridRitem2->Mat = mMaterials["wirefence"].get();
	gridRitem2->Geo = mGeometries["shapeGeo"].get();
	gridRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem2->IndexCount = gridRitem2->Geo->DrawArgs["ground"].IndexCount;
	gridRitem2->StartIndexLocation = gridRitem2->Geo->DrawArgs["ground"].StartIndexLocation;
	gridRitem2->BaseVertexLocation = gridRitem2->Geo->DrawArgs["ground"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem2));


	auto backWall = std::make_unique<RenderItem>();
	//backWall->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&backWall->World, XMMatrixScaling(18.0f, 8.0f, 0.5f) * XMMatrixTranslation(0.0f, 4.0f, 9.0f));
	XMStoreFloat4x4(&backWall->TexTransform, XMMatrixScaling(4.0f, 1.6f, 1.0f));
	backWall->ObjCBIndex = cbindex++;
	backWall->Mat = mMaterials["stone"].get();
	backWall->Geo = mGeometries["shapeGeo"].get();
	backWall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	backWall->IndexCount = backWall->Geo->DrawArgs["wholeWall"].IndexCount;
	backWall->StartIndexLocation = backWall->Geo->DrawArgs["wholeWall"].StartIndexLocation;
	backWall->BaseVertexLocation = backWall->Geo->DrawArgs["wholeWall"].BaseVertexLocation;
	mAllRitems.push_back(std::move(backWall));

	auto leftWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&leftWall->World, XMMatrixScaling(18.0f, 8.0f, 0.5f) * XMMatrixRotationAxis(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), XMConvertToRadians(90.0f)) * XMMatrixTranslation(-9.0f, 4.0f, 0.0f));
	XMStoreFloat4x4(&leftWall->TexTransform, XMMatrixScaling(4.0f, 1.6f, 1.0f));
	leftWall->ObjCBIndex = cbindex++;
	leftWall->Mat = mMaterials["stone"].get();
	leftWall->Geo = mGeometries["shapeGeo"].get();
	leftWall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftWall->IndexCount = leftWall->Geo->DrawArgs["wholeWall"].IndexCount;
	leftWall->StartIndexLocation = leftWall->Geo->DrawArgs["wholeWall"].StartIndexLocation;
	leftWall->BaseVertexLocation = leftWall->Geo->DrawArgs["wholeWall"].BaseVertexLocation;
	mAllRitems.push_back(std::move(leftWall));

	auto rightWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rightWall->World, XMMatrixScaling(18.0f, 8.0f, 0.5f) * XMMatrixRotationAxis(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), XMConvertToRadians(90.0f)) * XMMatrixTranslation(9.0f, 4.0f, 0.0f));
	XMStoreFloat4x4(&rightWall->TexTransform, XMMatrixScaling(4.0f, 1.6f, 1.0f));
	rightWall->ObjCBIndex = cbindex++;
	rightWall->Mat = mMaterials["stone"].get();
	rightWall->Geo = mGeometries["shapeGeo"].get();
	rightWall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightWall->IndexCount = rightWall->Geo->DrawArgs["wholeWall"].IndexCount;
	rightWall->StartIndexLocation = rightWall->Geo->DrawArgs["wholeWall"].StartIndexLocation;
	rightWall->BaseVertexLocation = rightWall->Geo->DrawArgs["wholeWall"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rightWall));

	auto frontWall1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontWall1->World, XMMatrixScaling(6.0f, 5.0f, 0.5f) * XMMatrixTranslation(-5.0f, 2.5f, -9.0f));
	XMStoreFloat4x4(&frontWall1->TexTransform, XMMatrixScaling(1.33f, 1.11f, 1.0f));
	frontWall1->ObjCBIndex = cbindex++;
	frontWall1->Mat = mMaterials["stone"].get();
	frontWall1->Geo = mGeometries["shapeGeo"].get();
	frontWall1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontWall1->IndexCount = frontWall1->Geo->DrawArgs["wholeWall"].IndexCount;
	frontWall1->StartIndexLocation = frontWall1->Geo->DrawArgs["wholeWall"].StartIndexLocation;
	frontWall1->BaseVertexLocation = frontWall1->Geo->DrawArgs["wholeWall"].BaseVertexLocation;
	mAllRitems.push_back(std::move(frontWall1));

	auto frontWall2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontWall2->World, XMMatrixScaling(6.0f, 5.0f, 0.5f) * XMMatrixTranslation(5.0f, 2.5f, -9.0f));
	XMStoreFloat4x4(&frontWall2->TexTransform, XMMatrixScaling(1.33f, 1.11f, 1.0f));
	frontWall2->ObjCBIndex = cbindex++;
	frontWall2->Mat = mMaterials["stone"].get();
	frontWall2->Geo = mGeometries["shapeGeo"].get();
	frontWall2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontWall2->IndexCount = frontWall2->Geo->DrawArgs["wholeWall"].IndexCount;
	frontWall2->StartIndexLocation = frontWall2->Geo->DrawArgs["wholeWall"].StartIndexLocation;
	frontWall2->BaseVertexLocation = frontWall2->Geo->DrawArgs["wholeWall"].BaseVertexLocation;
	mAllRitems.push_back(std::move(frontWall2));

	auto frontWall3 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontWall3->World, XMMatrixScaling(18.0f, 3.0f, 0.5f) * XMMatrixTranslation(0.0f, 6.5f, -9.0f));
	XMStoreFloat4x4(&frontWall3->TexTransform, XMMatrixScaling(4.0f, 0.66f, 1.0f));
	frontWall3->ObjCBIndex = cbindex++;
	frontWall3->Mat = mMaterials["stone"].get();
	frontWall3->Geo = mGeometries["shapeGeo"].get();
	frontWall3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontWall3->IndexCount = frontWall3->Geo->DrawArgs["wholeWall"].IndexCount;
	frontWall3->StartIndexLocation = frontWall3->Geo->DrawArgs["wholeWall"].StartIndexLocation;
	frontWall3->BaseVertexLocation = frontWall3->Geo->DrawArgs["wholeWall"].BaseVertexLocation;
	mAllRitems.push_back(std::move(frontWall3));

	auto columnFrontLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnFrontLeft->World, XMMatrixScaling(2.0f, 10.0f, 2.0f) * XMMatrixTranslation(-9.0f, 5.0f, -9.0f));
	columnFrontLeft->ObjCBIndex = cbindex++;
	columnFrontLeft->Mat = mMaterials["wirefence"].get();
	columnFrontLeft->Geo = mGeometries["shapeGeo"].get();
	columnFrontLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnFrontLeft->IndexCount = columnFrontLeft->Geo->DrawArgs["column"].IndexCount;
	columnFrontLeft->StartIndexLocation = columnFrontLeft->Geo->DrawArgs["column"].StartIndexLocation;
	columnFrontLeft->BaseVertexLocation = columnFrontLeft->Geo->DrawArgs["column"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnFrontLeft));

	auto columnFrontRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnFrontRight->World, XMMatrixScaling(2.0f, 10.0f, 2.0f) * XMMatrixTranslation(9.0f, 5.0f, -9.0f));
	columnFrontRight->ObjCBIndex = cbindex++;
	columnFrontRight->Mat = mMaterials["wirefence"].get();
	columnFrontRight->Geo = mGeometries["shapeGeo"].get();
	columnFrontRight->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnFrontRight->IndexCount = columnFrontRight->Geo->DrawArgs["column"].IndexCount;
	columnFrontRight->StartIndexLocation = columnFrontRight->Geo->DrawArgs["column"].StartIndexLocation;
	columnFrontRight->BaseVertexLocation = columnFrontRight->Geo->DrawArgs["column"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnFrontRight));

	auto columnBackLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnBackLeft->World, XMMatrixScaling(2.0f, 10.0f, 2.0f) * XMMatrixTranslation(-9.0f, 5.0f, 9.0f));
	columnBackLeft->ObjCBIndex = cbindex++;
	columnBackLeft->Mat = mMaterials["wirefence"].get();
	columnBackLeft->Geo = mGeometries["shapeGeo"].get();
	columnBackLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnBackLeft->IndexCount = columnBackLeft->Geo->DrawArgs["column"].IndexCount;
	columnBackLeft->StartIndexLocation = columnBackLeft->Geo->DrawArgs["column"].StartIndexLocation;
	columnBackLeft->BaseVertexLocation = columnBackLeft->Geo->DrawArgs["column"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnBackLeft));


	auto columnBackRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnBackRight->World, XMMatrixScaling(2.0f, 10.0f, 2.0f) * XMMatrixTranslation(9.0f, 5.0f, 9.0f));
	columnBackRight->ObjCBIndex = cbindex++;
	columnBackRight->Mat = mMaterials["wirefence"].get();
	columnBackRight->Geo = mGeometries["shapeGeo"].get();
	columnBackRight->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnBackRight->IndexCount = columnBackRight->Geo->DrawArgs["column"].IndexCount;
	columnBackRight->StartIndexLocation = columnBackRight->Geo->DrawArgs["column"].StartIndexLocation;
	columnBackRight->BaseVertexLocation = columnBackRight->Geo->DrawArgs["column"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnBackRight));

	auto columnTopFLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnTopFLeft->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(-9.0f, 13.0f, -9.0f));
	columnTopFLeft->ObjCBIndex = cbindex++;
	columnTopFLeft->Mat = mMaterials["wirefence"].get();
	columnTopFLeft->Geo = mGeometries["shapeGeo"].get();
	columnTopFLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnTopFLeft->IndexCount = columnTopFLeft->Geo->DrawArgs["columnTop"].IndexCount;
	columnTopFLeft->StartIndexLocation = columnTopFLeft->Geo->DrawArgs["columnTop"].StartIndexLocation;
	columnTopFLeft->BaseVertexLocation = columnTopFLeft->Geo->DrawArgs["columnTop"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnTopFLeft));

	auto columnTopFRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnTopFRight->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(9.0f, 13.0f, -9.0f));
	columnTopFRight->ObjCBIndex = cbindex++;
	columnTopFRight->Mat = mMaterials["wirefence"].get();
	columnTopFRight->Geo = mGeometries["shapeGeo"].get();
	columnTopFRight->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnTopFRight->IndexCount = columnTopFRight->Geo->DrawArgs["columnTop"].IndexCount;
	columnTopFRight->StartIndexLocation = columnTopFRight->Geo->DrawArgs["columnTop"].StartIndexLocation;
	columnTopFRight->BaseVertexLocation = columnTopFRight->Geo->DrawArgs["columnTop"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnTopFRight));


	auto columnTopBLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnTopBLeft->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(-9.0f, 13.0f, 9.0f));
	columnTopBLeft->ObjCBIndex = cbindex++;
	columnTopBLeft->Mat = mMaterials["wirefence"].get();
	columnTopBLeft->Geo = mGeometries["shapeGeo"].get();
	columnTopBLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnTopBLeft->IndexCount = columnTopBLeft->Geo->DrawArgs["columnTop"].IndexCount;
	columnTopBLeft->StartIndexLocation = columnTopBLeft->Geo->DrawArgs["columnTop"].StartIndexLocation;
	columnTopBLeft->BaseVertexLocation = columnTopBLeft->Geo->DrawArgs["columnTop"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnTopBLeft));


	auto columnTopBRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnTopBRight->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(9.0f, 13.0f, 9.0f));
	columnTopBRight->ObjCBIndex = cbindex++;
	columnTopBRight->Mat = mMaterials["wirefence"].get();
	columnTopBRight->Geo = mGeometries["shapeGeo"].get();
	columnTopBRight->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnTopBRight->IndexCount = columnTopBRight->Geo->DrawArgs["columnTop"].IndexCount;
	columnTopBRight->StartIndexLocation = columnTopBRight->Geo->DrawArgs["columnTop"].StartIndexLocation;
	columnTopBRight->BaseVertexLocation = columnTopBRight->Geo->DrawArgs["columnTop"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnTopBRight));

	auto Base1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Base1->World, XMMatrixScaling(14.0f, 6.0f, 14.0f) * XMMatrixTranslation(0.0f, 3.0f, 0.0f));
	Base1->ObjCBIndex = cbindex++;
	Base1->Mat = mMaterials["wirefence"].get();
	Base1->Geo = mGeometries["shapeGeo"].get();
	Base1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Base1->IndexCount = Base1->Geo->DrawArgs["Base1"].IndexCount;
	Base1->StartIndexLocation = Base1->Geo->DrawArgs["Base1"].StartIndexLocation;
	Base1->BaseVertexLocation = Base1->Geo->DrawArgs["Base1"].BaseVertexLocation;
	mAllRitems.push_back(std::move(Base1));

	auto Base2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Base2->World, XMMatrixScaling(10.0f, 4.0f, 10.0f) * XMMatrixTranslation(0.0f, 8.0f, 0.0f));
	Base2->ObjCBIndex = cbindex++;
	Base2->Mat = mMaterials["wirefence"].get();
	Base2->Geo = mGeometries["shapeGeo"].get();
	Base2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Base2->IndexCount = Base2->Geo->DrawArgs["Base2"].IndexCount;
	Base2->StartIndexLocation = Base2->Geo->DrawArgs["Base2"].StartIndexLocation;
	Base2->BaseVertexLocation = Base2->Geo->DrawArgs["Base2"].BaseVertexLocation;
	mAllRitems.push_back(std::move(Base2));

	auto Base3 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Base3->World, XMMatrixScaling(4.0f, 6.0f, 4.0f) * XMMatrixTranslation(0.0f, 13.0f, 0.0f));
	Base3->ObjCBIndex = cbindex++;
	Base3->Mat = mMaterials["wirefence"].get();
	Base3->Geo = mGeometries["shapeGeo"].get();
	Base3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Base3->IndexCount = Base3->Geo->DrawArgs["Base3"].IndexCount;
	Base3->StartIndexLocation = Base3->Geo->DrawArgs["Base3"].StartIndexLocation;
	Base3->BaseVertexLocation = Base3->Geo->DrawArgs["Base3"].BaseVertexLocation;
	mAllRitems.push_back(std::move(Base3));

	auto top = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&top->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 18.0f, 0.0f));
	top->ObjCBIndex = cbindex++;
	top->Mat = mMaterials["wirefence"].get();
	top->Geo = mGeometries["shapeGeo"].get();
	top->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	top->IndexCount = top->Geo->DrawArgs["top"].IndexCount;
	top->StartIndexLocation = top->Geo->DrawArgs["top"].StartIndexLocation;
	top->BaseVertexLocation = top->Geo->DrawArgs["top"].BaseVertexLocation;
	mAllRitems.push_back(std::move(top));



	//mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	// All the render items are opaque.
	for (auto& e : mAllRitems)
		mRitemLayer[(int)RenderLayer::Opaque].push_back(e.get());
	
}

void TexWavesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> TexWavesApp::GetStaticSamplers()
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

	return { 
		pointWrap, pointClamp,
		linearWrap, linearClamp, 
		anisotropicWrap, anisotropicClamp };
}

float TexWavesApp::GetHillsHeight(float x, float z)const
{
    return 0.3f*(z*sinf(0.1f*x) + x*cosf(0.1f*z));
}

XMFLOAT3 TexWavesApp::GetHillsNormal(float x, float z)const
{
    // n = (-df/dx, 1, -df/dz)
    XMFLOAT3 n(
        -0.03f*z*cosf(0.1f*x) - 0.3f*cosf(0.1f*z),
        1.0f,
        -0.3f*sinf(0.1f*x) + 0.03f*x*sinf(0.1f*z));

    XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, unitNormal);

    return n;
}
