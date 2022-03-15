/** @file Week4-5-ShapePractice.cpp
 *  @brief Shape Practice.
 *
 *  Place all of the scene geometry in one big vertex and index buffer. 
 * Then use the DrawIndexedInstanced method to draw one object at a time ((as the
 * world matrix needs to be changed between objects)
 *
 *   Controls:
 *   Hold down '1' key to view scene in wireframe mode.
 *   Hold the left mouse button down and move the mouse to rotate.
 *   Hold the right mouse button down and move the mouse to zoom in and out.
 *
 *  @author Hooman Salamat
 */

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

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

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

class ShapesApp : public D3DApp
{
public:
	ShapesApp(HINSTANCE hInstance);
	ShapesApp(const ShapesApp& rhs) = delete;
	ShapesApp& operator=(const ShapesApp& rhs) = delete;
	~ShapesApp();

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
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void BuildDescriptorHeaps();
	void BuildConstantBufferViews();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

	PassConstants mMainPassCB;

	UINT mPassCbvOffset = 0;

	bool mIsWireframe = false;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * XM_PI;
	float mPhi = 0.2f * XM_PI;
	float mRadius = 35.0f;

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
		ShapesApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

ShapesApp::ShapesApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

ShapesApp::~ShapesApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool ShapesApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBufferViews();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void ShapesApp::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void ShapesApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	if (mIsWireframe)
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
	}
	else
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
	}

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

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
	auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

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

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;
}

void ShapesApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
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

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::BuildDescriptorHeaps()
{
	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource,
	// +1 for the perPass CBV for each frame resource.
	UINT numDescriptors = (objCount + 1) * gNumFrameResources;

	// Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.
	mPassCbvOffset = objCount * gNumFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&mCbvHeap)));
}

void ShapesApp::BuildConstantBufferViews()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
		for (UINT i = 0; i < objCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

			// Offset to the ith object constant buffer in the buffer.
			cbAddress += i * objCBByteSize;

			// Offset to the object cbv in the descriptor heap.
			int heapIndex = frameIndex * objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = objCBByteSize;

			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Last three descriptors are the pass CBVs for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();

		// Offset to the pass cbv in the descriptor heap.
		int heapIndex = mPassCbvOffset + frameIndex;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes = passCBByteSize;

		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

void ShapesApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	// Create root CBVs.
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
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
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void ShapesApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\VS.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\PS.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void ShapesApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData wholeWall = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(24.0f, 24.0f, 25, 25);
	GeometryGenerator::MeshData column = geoGen.CreateCylinder(0.5f, 0.5f, 1.0f, 20, 20);
	GeometryGenerator::MeshData columnTop = geoGen.CreateSphere(0.5f, 4, 2);
	GeometryGenerator::MeshData Base1 = geoGen.CreateCylinder(0.5f, 0.5f, 1.0f, 10, 2);
	GeometryGenerator::MeshData Base2 = geoGen.CreateCylinder(0.5f, 0.5f, 1.0f, 8, 2);
	GeometryGenerator::MeshData Base3 = geoGen.CreateCylinder(0.5f, 0.0f, 1.0f, 10, 1);
	GeometryGenerator::MeshData top = geoGen.CreateSphere(0.5f, 11, 10);


	
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
	for (size_t i = 0; i < wholeWall.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wholeWall.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Red);
	}

	//step6
	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4({0, 0.1f, 0, 1});
	}

	for (size_t i = 0; i < column.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = column.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Green);
	}

	for (size_t i = 0; i < columnTop.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = columnTop.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Yellow);
	}

	for (size_t i = 0; i < Base1.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = Base1.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Blue);
	}

	for (size_t i = 0; i < Base2.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = Base2.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DeepPink);
	}

	for (size_t i = 0; i < Base3.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = Base3.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Cyan);
	}

	for (size_t i = 0; i < top.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = top.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Red);
	}


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

void ShapesApp::BuildPSOs()
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
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
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


	//
	// PSO for opaque wireframe objects.
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));
}

void ShapesApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size()));
	}
}

void ShapesApp::BuildRenderItems()
{
	int cbindex = 0;
	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = cbindex++;
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["ground"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["ground"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["ground"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));


	auto backWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&backWall->World, XMMatrixScaling(18.0f, 8.0f, 0.5f) * XMMatrixTranslation(0.0f, 4.0f, 9.0f));
	backWall->ObjCBIndex = cbindex++;
	backWall->Geo = mGeometries["shapeGeo"].get();
	backWall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	backWall->IndexCount = backWall->Geo->DrawArgs["wholeWall"].IndexCount;
	backWall->StartIndexLocation = backWall->Geo->DrawArgs["wholeWall"].StartIndexLocation;
	backWall->BaseVertexLocation = backWall->Geo->DrawArgs["wholeWall"].BaseVertexLocation;
	mAllRitems.push_back(std::move(backWall));

	auto leftWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&leftWall->World, XMMatrixScaling(18.0f, 8.0f, 0.5f) * XMMatrixRotationAxis(XMVectorSet(0.0f, 1.0f, 0.0f,0.0f), XMConvertToRadians(90.0f)) * XMMatrixTranslation(-9.0f, 4.0f, 0.0f));
	leftWall->ObjCBIndex = cbindex++;
	leftWall->Geo = mGeometries["shapeGeo"].get();
	leftWall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftWall->IndexCount = leftWall->Geo->DrawArgs["wholeWall"].IndexCount;
	leftWall->StartIndexLocation = leftWall->Geo->DrawArgs["wholeWall"].StartIndexLocation;
	leftWall->BaseVertexLocation = leftWall->Geo->DrawArgs["wholeWall"].BaseVertexLocation;
	mAllRitems.push_back(std::move(leftWall));

	auto rightWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rightWall->World, XMMatrixScaling(18.0f, 8.0f, 0.5f) * XMMatrixRotationAxis(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), XMConvertToRadians(90.0f)) * XMMatrixTranslation(9.0f, 4.0f, 0.0f));
	rightWall->ObjCBIndex = cbindex++;
	rightWall->Geo = mGeometries["shapeGeo"].get();
	rightWall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightWall->IndexCount = rightWall->Geo->DrawArgs["wholeWall"].IndexCount;
	rightWall->StartIndexLocation = rightWall->Geo->DrawArgs["wholeWall"].StartIndexLocation;
	rightWall->BaseVertexLocation = rightWall->Geo->DrawArgs["wholeWall"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rightWall));

	auto frontWall1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontWall1->World, XMMatrixScaling(6.0f, 5.0f, 0.5f) * XMMatrixTranslation(-5.0f, 2.5f, -9.0f));
	frontWall1->ObjCBIndex = cbindex++;
	frontWall1->Geo = mGeometries["shapeGeo"].get();
	frontWall1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontWall1->IndexCount = frontWall1->Geo->DrawArgs["wholeWall"].IndexCount;
	frontWall1->StartIndexLocation = frontWall1->Geo->DrawArgs["wholeWall"].StartIndexLocation;
	frontWall1->BaseVertexLocation = frontWall1->Geo->DrawArgs["wholeWall"].BaseVertexLocation;
	mAllRitems.push_back(std::move(frontWall1));

	auto frontWall2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontWall2->World, XMMatrixScaling(6.0f, 5.0f, 0.5f) * XMMatrixTranslation(5.0f, 2.5f, -9.0f));
	frontWall2->ObjCBIndex = cbindex++;
	frontWall2->Geo = mGeometries["shapeGeo"].get();
	frontWall2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontWall2->IndexCount = frontWall2->Geo->DrawArgs["wholeWall"].IndexCount;
	frontWall2->StartIndexLocation = frontWall2->Geo->DrawArgs["wholeWall"].StartIndexLocation;
	frontWall2->BaseVertexLocation = frontWall2->Geo->DrawArgs["wholeWall"].BaseVertexLocation;
	mAllRitems.push_back(std::move(frontWall2));

	auto frontWall3 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontWall3->World, XMMatrixScaling(18.0f, 3.0f, 0.5f) * XMMatrixTranslation(0.0f, 6.5f, -9.0f));
	frontWall3->ObjCBIndex = cbindex++;
	frontWall3->Geo = mGeometries["shapeGeo"].get();
	frontWall3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontWall3->IndexCount = frontWall3->Geo->DrawArgs["wholeWall"].IndexCount;
	frontWall3->StartIndexLocation = frontWall3->Geo->DrawArgs["wholeWall"].StartIndexLocation;
	frontWall3->BaseVertexLocation = frontWall3->Geo->DrawArgs["wholeWall"].BaseVertexLocation;
	mAllRitems.push_back(std::move(frontWall3));

	auto columnFrontLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnFrontLeft->World, XMMatrixScaling(2.0f, 10.0f, 2.0f) * XMMatrixTranslation(-9.0f, 5.0f, -9.0f));
	columnFrontLeft->ObjCBIndex = cbindex++;
	columnFrontLeft->Geo = mGeometries["shapeGeo"].get();
	columnFrontLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnFrontLeft->IndexCount = columnFrontLeft->Geo->DrawArgs["column"].IndexCount;
	columnFrontLeft->StartIndexLocation = columnFrontLeft->Geo->DrawArgs["column"].StartIndexLocation;
	columnFrontLeft->BaseVertexLocation = columnFrontLeft->Geo->DrawArgs["column"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnFrontLeft));

	auto columnFrontRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnFrontRight->World, XMMatrixScaling(2.0f, 10.0f, 2.0f) * XMMatrixTranslation(9.0f, 5.0f, -9.0f));
	columnFrontRight->ObjCBIndex = cbindex++;
	columnFrontRight->Geo = mGeometries["shapeGeo"].get();
	columnFrontRight->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnFrontRight->IndexCount = columnFrontRight->Geo->DrawArgs["column"].IndexCount;
	columnFrontRight->StartIndexLocation = columnFrontRight->Geo->DrawArgs["column"].StartIndexLocation;
	columnFrontRight->BaseVertexLocation = columnFrontRight->Geo->DrawArgs["column"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnFrontRight));

	auto columnBackLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnBackLeft->World, XMMatrixScaling(2.0f, 10.0f, 2.0f) * XMMatrixTranslation(-9.0f, 5.0f, 9.0f));
	columnBackLeft->ObjCBIndex = cbindex++;
	columnBackLeft->Geo = mGeometries["shapeGeo"].get();
	columnBackLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnBackLeft->IndexCount = columnBackLeft->Geo->DrawArgs["column"].IndexCount;
	columnBackLeft->StartIndexLocation = columnBackLeft->Geo->DrawArgs["column"].StartIndexLocation;
	columnBackLeft->BaseVertexLocation = columnBackLeft->Geo->DrawArgs["column"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnBackLeft));


	auto columnBackRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnBackRight->World, XMMatrixScaling(2.0f, 10.0f, 2.0f)* XMMatrixTranslation(9.0f, 5.0f, 9.0f));
	columnBackRight->ObjCBIndex = cbindex++;
	columnBackRight->Geo = mGeometries["shapeGeo"].get();
	columnBackRight->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnBackRight->IndexCount = columnBackRight->Geo->DrawArgs["column"].IndexCount;
	columnBackRight->StartIndexLocation = columnBackRight->Geo->DrawArgs["column"].StartIndexLocation;
	columnBackRight->BaseVertexLocation = columnBackRight->Geo->DrawArgs["column"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnBackRight));

	auto columnTopFLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnTopFLeft->World, XMMatrixScaling(2.0f, 2.0f, 2.0f)* XMMatrixTranslation(-9.0f, 11.0f, -9.0f));
	columnTopFLeft->ObjCBIndex = cbindex++;
	columnTopFLeft->Geo = mGeometries["shapeGeo"].get();
	columnTopFLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnTopFLeft->IndexCount = columnTopFLeft->Geo->DrawArgs["columnTop"].IndexCount;
	columnTopFLeft->StartIndexLocation = columnTopFLeft->Geo->DrawArgs["columnTop"].StartIndexLocation;
	columnTopFLeft->BaseVertexLocation = columnTopFLeft->Geo->DrawArgs["columnTop"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnTopFLeft));

	auto columnTopFRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnTopFRight->World, XMMatrixScaling(2.0f, 2.0f, 2.0f)* XMMatrixTranslation(9.0f, 11.0f, -9.0f));
	columnTopFRight->ObjCBIndex = cbindex++;
	columnTopFRight->Geo = mGeometries["shapeGeo"].get();
	columnTopFRight->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnTopFRight->IndexCount = columnTopFRight->Geo->DrawArgs["columnTop"].IndexCount;
	columnTopFRight->StartIndexLocation = columnTopFRight->Geo->DrawArgs["columnTop"].StartIndexLocation;
	columnTopFRight->BaseVertexLocation = columnTopFRight->Geo->DrawArgs["columnTop"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnTopFRight));


	auto columnTopBLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnTopBLeft->World, XMMatrixScaling(2.0f, 2.0f, 2.0f)* XMMatrixTranslation(-9.0f, 11.0f, 9.0f));
	columnTopBLeft->ObjCBIndex = cbindex++;
	columnTopBLeft->Geo = mGeometries["shapeGeo"].get();
	columnTopBLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnTopBLeft->IndexCount = columnTopBLeft->Geo->DrawArgs["columnTop"].IndexCount;
	columnTopBLeft->StartIndexLocation = columnTopBLeft->Geo->DrawArgs["columnTop"].StartIndexLocation;
	columnTopBLeft->BaseVertexLocation = columnTopBLeft->Geo->DrawArgs["columnTop"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnTopBLeft));


	auto columnTopBRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&columnTopBRight->World, XMMatrixScaling(2.0f, 2.0f, 2.0f)* XMMatrixTranslation(9.0f, 11.0f, 9.0f));
	columnTopBRight->ObjCBIndex = cbindex++;
	columnTopBRight->Geo = mGeometries["shapeGeo"].get();
	columnTopBRight->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	columnTopBRight->IndexCount = columnTopBRight->Geo->DrawArgs["columnTop"].IndexCount;
	columnTopBRight->StartIndexLocation = columnTopBRight->Geo->DrawArgs["columnTop"].StartIndexLocation;
	columnTopBRight->BaseVertexLocation = columnTopBRight->Geo->DrawArgs["columnTop"].BaseVertexLocation;
	mAllRitems.push_back(std::move(columnTopBRight));

	auto Base1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Base1->World, XMMatrixScaling(14.0f, 6.0f, 14.0f)* XMMatrixTranslation(0.0f, 3.0f, 0.0f));
	Base1->ObjCBIndex = cbindex++;
	Base1->Geo = mGeometries["shapeGeo"].get();
	Base1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Base1->IndexCount = Base1->Geo->DrawArgs["Base1"].IndexCount;
	Base1->StartIndexLocation = Base1->Geo->DrawArgs["Base1"].StartIndexLocation;
	Base1->BaseVertexLocation = Base1->Geo->DrawArgs["Base1"].BaseVertexLocation;
	mAllRitems.push_back(std::move(Base1));

	auto Base2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Base2->World, XMMatrixScaling(10.0f, 4.0f, 10.0f)* XMMatrixTranslation(0.0f, 8.0f, 0.0f));
	Base2->ObjCBIndex = cbindex++;
	Base2->Geo = mGeometries["shapeGeo"].get();
	Base2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Base2->IndexCount = Base2->Geo->DrawArgs["Base2"].IndexCount;
	Base2->StartIndexLocation = Base2->Geo->DrawArgs["Base2"].StartIndexLocation;
	Base2->BaseVertexLocation = Base2->Geo->DrawArgs["Base2"].BaseVertexLocation;
	mAllRitems.push_back(std::move(Base2));

	auto Base3 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Base3->World, XMMatrixScaling(4.0f, 6.0f, 4.0f)* XMMatrixTranslation(0.0f, 13.0f, 0.0f));
	Base3->ObjCBIndex = cbindex++;
	Base3->Geo = mGeometries["shapeGeo"].get();
	Base3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Base3->IndexCount = Base3->Geo->DrawArgs["Base3"].IndexCount;
	Base3->StartIndexLocation = Base3->Geo->DrawArgs["Base3"].StartIndexLocation;
	Base3->BaseVertexLocation = Base3->Geo->DrawArgs["Base3"].BaseVertexLocation;
	mAllRitems.push_back(std::move(Base3));

	auto top = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&top->World, XMMatrixScaling(2.0f, 2.0f, 2.0f)* XMMatrixTranslation(0.0f, 18.0f, 0.0f));
	top->ObjCBIndex = cbindex++;
	top->Geo = mGeometries["shapeGeo"].get();
	top->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	top->IndexCount = top->Geo->DrawArgs["top"].IndexCount;
	top->StartIndexLocation = top->Geo->DrawArgs["top"].StartIndexLocation;
	top->BaseVertexLocation = top->Geo->DrawArgs["top"].BaseVertexLocation;
	mAllRitems.push_back(std::move(top));



	
	//Step9
	

	//UINT objCBIndex = 2;
	//for (int i = 0; i < 5; ++i)
	//{
	//	auto leftCylRitem = std::make_unique<RenderItem>();
	//	auto rightCylRitem = std::make_unique<RenderItem>();
	//	auto leftSphereRitem = std::make_unique<RenderItem>();
	//	auto rightSphereRitem = std::make_unique<RenderItem>();

	//	XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
	//	XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

	//	XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
	//	XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

	//	XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
	//	leftCylRitem->ObjCBIndex = objCBIndex++;
	//	leftCylRitem->Geo = mGeometries["shapeGeo"].get();
	//	leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//	leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	//	leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	//	leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

	//	XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
	//	rightCylRitem->ObjCBIndex = objCBIndex++;
	//	rightCylRitem->Geo = mGeometries["shapeGeo"].get();
	//	rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//	rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	//	rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	//	rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

	//	XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
	//	leftSphereRitem->ObjCBIndex = objCBIndex++;
	//	leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
	//	leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//	leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	//	leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	//	leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	//	XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
	//	rightSphereRitem->ObjCBIndex = objCBIndex++;
	//	rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
	//	rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//	rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	//	rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	//	rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	//	mAllRitems.push_back(std::move(leftCylRitem));
	//	mAllRitems.push_back(std::move(rightCylRitem));
	//	mAllRitems.push_back(std::move(leftSphereRitem));
	//	mAllRitems.push_back(std::move(rightSphereRitem));
	//}

	// All the render items are opaque.
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		// Offset to the CBV in the descriptor heap for this object and for this frame resource.
		UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size() + ri->ObjCBIndex;
		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
		cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);

		cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}


