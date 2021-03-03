//***************************************************************************************
// ShapesApp.cpp 
//
// Assignment 1 by Andrew Boulanger 101 292 574 and Yaniv Milman 101 270 509
//
// Hold down '1' key to view scene in wireframe mode.
//***************************************************************************************

#include "d3dApp.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;
const float width = 50;
const float depth = 50;

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

    XMFLOAT4X4 TWorld = MathHelper::Identity4x4();

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
    void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

    void LoadTextures();
    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildConstantBufferViews();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void SetRenderItemInfo(RenderItem &Ritem, std::string itemType, XMMATRIX transform);
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
 
    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();
private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
  //  ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
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

    float mTheta = 1.5f*XM_PI;
    float mPhi = 0.2f*XM_PI;
    float mRadius = 65.0f;

    POINT mLastMousePos;

    UINT objCBIndex = 0;
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

ShapesApp::ShapesApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

ShapesApp::~ShapesApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool ShapesApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    LoadTextures();
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
    BuildMaterials();
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
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
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
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	UpdateObjectCBs(gt);
    UpdateMaterialCBs(gt);
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
        float dx = 0.05f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.05f*static_cast<float>(y - mLastMousePos.y);

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
    if(GetAsyncKeyState('1') & 0x8000)
        mIsWireframe = true;
    else
        mIsWireframe = false;
}
 
void ShapesApp::UpdateCamera(const GameTimer& gt)
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

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for(auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if(e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMMATRIX tWorld = XMLoadFloat4x4(&e->TWorld);
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TWorld, XMMatrixTranspose(MathHelper::InverseTranspose(world)));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));


			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void ShapesApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
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

    //lights
	//mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.25f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.0f, -0.0f, 1.0f };
	mMainPassCB.Lights[0].Strength = { 0.0f, 0.99, 0.0f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.0f, 0.0f, 0.0f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.0f, 0.0f, 0.0f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::LoadTextures()
{
	auto bricksTex = std::make_unique<Texture>();
	bricksTex->Name = "bricksTex";
	bricksTex->Filename = L"Textures/bricks.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), bricksTex->Filename.c_str(),
		bricksTex->Resource, bricksTex->UploadHeap));

	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Name = "stoneTex";
	stoneTex->Filename = L"Textures/stone.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), stoneTex->Filename.c_str(),
		stoneTex->Resource, stoneTex->UploadHeap));

	auto tileTex = std::make_unique<Texture>();
	tileTex->Name = "tileTex";
	tileTex->Filename = L"Textures/tile.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), tileTex->Filename.c_str(),
		tileTex->Resource, tileTex->UploadHeap));

	mTextures[bricksTex->Name] = std::move(bricksTex);
	mTextures[stoneTex->Name] = std::move(stoneTex);
	mTextures[tileTex->Name] = std::move(tileTex);
}

//If we have 3 frame resources and n render items, then we have three 3n object constant
//buffers and 3 pass constant buffers.Hence we need 3(n + 1) constant buffer views(CBVs).
//Thus we will need to modify our CBV heap to include the additional descriptors :

void ShapesApp::BuildDescriptorHeaps()
{
    //UINT objCount = (UINT)mOpaqueRitems.size();

    //UINT numDescriptors = (objCount + 1) * gNumFrameResources;

    //mPassCbvOffset = objCount * gNumFrameResources;

    //D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    //cbvHeapDesc.NumDescriptors = numDescriptors;
    //cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    //cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    //cbvHeapDesc.NodeMask = 0;
    //ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));

	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 3;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto bricksTex = mTextures["bricksTex"]->Resource;
	auto stoneTex = mTextures["stoneTex"]->Resource;
	auto tileTex = mTextures["tileTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = bricksTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = stoneTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = stoneTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = tileTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);
}

//assuming we have n renter items, we can populate the CBV heap with the following code where descriptors 0 to n-
//1 contain the object CBVs for the 0th frame resource, descriptors n to 2n−1 contains the
//object CBVs for 1st frame resource, descriptors 2n to 3n−1 contain the objects CBVs for
//the 2nd frame resource, and descriptors 3n, 3n + 1, and 3n + 2 contain the pass CBVs for the
//0th, 1st, and 2nd frame resource
void ShapesApp::BuildConstantBufferViews()
{
   // UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

   // UINT objCount = (UINT)mOpaqueRitems.size();

   // // Need a CBV descriptor for each object for each frame resource.
   // for(int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
   // {
   //     auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
   //     for(UINT i = 0; i < objCount; ++i)
   //     {
   //         D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

   //         // Offset to the ith object constant buffer in the buffer.
   //         cbAddress += i*objCBByteSize;

   //         // Offset to the object cbv in the descriptor heap.
   //         int heapIndex = frameIndex*objCount + i;

			////we can get a handle to the first descriptor in a heap with the ID3D12DescriptorHeap::GetCPUDescriptorHandleForHeapStart
   //         auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());

			////our heap has more than one descriptor,we need to know the size to increment in the heap to get to the next descriptor
			////This is hardware specific, so we have to query this information from the device, and it depends on
			////the heap type.Recall that our D3DApp class caches this information: 	mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   //         handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

   //         D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
   //         cbvDesc.BufferLocation = cbAddress;
   //         cbvDesc.SizeInBytes = objCBByteSize;

   //         md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
   //     }
   // }

   // UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

   // // Last three descriptors are the pass CBVs for each frame resource.
   // for(int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
   // {
   //     auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
   //     D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();

   //     // Offset to the pass cbv in the descriptor heap.
   //     int heapIndex = mPassCbvOffset + frameIndex;
   //     auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
   //     handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

   //     D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
   //     cbvDesc.BufferLocation = cbAddress;
   //     cbvDesc.SizeInBytes = passCBByteSize;
   //     
   //     md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
   // }
}

//A root signature defines what resources need to be bound to the pipeline before issuing a draw call and
//how those resources get mapped to shader input registers. there is a limit of 64 DWORDs that can be put in a root signature.
void ShapesApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,  // number of descriptors
		0); // register t0

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[1].InitAsConstantBufferView(0); // register b0
	slotRootParameter[2].InitAsConstantBufferView(1); // register b1
	slotRootParameter[3].InitAsConstantBufferView(2); // register b2

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
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_1");
	
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void ShapesApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(width, depth, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.5f, 2.0f, 20, 20);
	GeometryGenerator::MeshData cone = geoGen.CreateCone(0.5f, 1.0f, 20, 1);
    GeometryGenerator::MeshData triPrism = geoGen.CreateTriangularPrism(10, 1, 1);
    GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(1, 0.7f, 0.3, 1, 6, 1);
    GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1, 1, 1 );
    GeometryGenerator::MeshData torus = geoGen.CreateTorus(0.3f, 2.0f, 20, 20);
    GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0f, 1.0f, 2.0f);
    //GeometryGenerator::MeshData halfsphere = geoGen.CreateHalfSphere(0.5f, 20, 20);
    GeometryGenerator::MeshData torus2 = geoGen.CreateTorus(0.3f, 2.0f, 20, 20);
    GeometryGenerator::MeshData cylinder2 = geoGen.CreateCylinder(1.0f, 0.5f, 2.0f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
    UINT coneVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
    UINT triPrismVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
    UINT diamondVertexOffset = triPrismVertexOffset + (UINT)triPrism.Vertices.size();
    UINT pyramidVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();
    UINT torusVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
    UINT wedgeVertexOffset = torusVertexOffset + (UINT)torus.Vertices.size();
    //UINT halfsphereVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
    UINT torus2VertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
    UINT cylinder2VertexOffset = torus2VertexOffset + (UINT)torus2.Vertices.size();
    
	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
    UINT coneIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
    UINT triPrismIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
    UINT diamondIndexOffset = triPrismIndexOffset + (UINT)triPrism.Indices32.size();
    UINT pyramidIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();
    UINT torusIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
    UINT wedgeIndexOffset = torusIndexOffset + (UINT)torus.Indices32.size(); 
    UINT torus2IndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size(); 
    UINT cylinder2IndexOffset = torus2IndexOffset + (UINT)torus2.Indices32.size();

   // UINT halfsphereIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
    // Define the SubmeshGeometry that cover different 
    // regions of the vertex/index buffers.

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

    SubmeshGeometry wedgeSubmesh;
    wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
    wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
    wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

   /* SubmeshGeometry halfsphereSubmesh;
    halfsphereSubmesh.IndexCount = (UINT)halfsphere.Indices32.size();
    halfsphereSubmesh.StartIndexLocation = halfsphereIndexOffset;
    halfsphereSubmesh.BaseVertexLocation = halfsphereVertexOffset;*/

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

    SubmeshGeometry coneSubmesh;
    coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
    coneSubmesh.StartIndexLocation = coneIndexOffset;
    coneSubmesh.BaseVertexLocation = coneVertexOffset;

    SubmeshGeometry triPrismSubmesh;
    triPrismSubmesh.IndexCount = (UINT)triPrism.Indices32.size();
    triPrismSubmesh.StartIndexLocation = triPrismIndexOffset;
    triPrismSubmesh.BaseVertexLocation = triPrismVertexOffset;

    SubmeshGeometry diamondSubmesh;
    diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
    diamondSubmesh.StartIndexLocation = diamondIndexOffset;
    diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

    SubmeshGeometry pyramidSubmesh;
    pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
    pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
    pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;  

    SubmeshGeometry torusSubmesh;
    torusSubmesh.IndexCount = (UINT)torus.Indices32.size();
    torusSubmesh.StartIndexLocation = torusIndexOffset;
    torusSubmesh.BaseVertexLocation = torusVertexOffset;

    SubmeshGeometry torus2Submesh;
    torus2Submesh.IndexCount = (UINT)torus2.Indices32.size();
    torus2Submesh.StartIndexLocation = torus2IndexOffset;
    torus2Submesh.BaseVertexLocation = torus2VertexOffset;
    
    SubmeshGeometry cylinder2Submesh;
    cylinder2Submesh.IndexCount = (UINT)cylinder2.Indices32.size();
    cylinder2Submesh.StartIndexLocation = cylinder2IndexOffset;
    cylinder2Submesh.BaseVertexLocation = cylinder2VertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

    auto totalVertexCount =
        box.Vertices.size() +
        grid.Vertices.size() +
        sphere.Vertices.size() +
        cylinder.Vertices.size() +
        cone.Vertices.size() +
        triPrism.Vertices.size() +
        diamond.Vertices.size() +
        pyramid.Vertices.size() +
        torus.Vertices.size() +
        wedge.Vertices.size() +
        torus2.Vertices.size() +
        cylinder2.Vertices.size();


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

    for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Normal = cone.Vertices[i].Normal;
		vertices[k].TexC = cone.Vertices[i].TexC;
    }

    for (size_t i = 0; i < triPrism.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = triPrism.Vertices[i].Position;
		vertices[k].Normal = triPrism.Vertices[i].Normal;
		vertices[k].TexC = triPrism.Vertices[i].TexC;
    }

    for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Normal = diamond.Vertices[i].Normal;
		vertices[k].TexC = diamond.Vertices[i].TexC;
    }

    for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Normal = pyramid.Vertices[i].Normal;
		vertices[k].TexC = pyramid.Vertices[i].TexC;
    }

     for (size_t i = 0; i < torus.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = torus.Vertices[i].Position;
		vertices[k].Normal = torus.Vertices[i].Normal;
		vertices[k].TexC = torus.Vertices[i].TexC;
    }
     for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
     {
         vertices[k].Pos = wedge.Vertices[i].Position;
		 vertices[k].Normal = wedge.Vertices[i].Normal;
		 vertices[k].TexC = wedge.Vertices[i].TexC;
     }
     for (size_t i = 0; i < torus2.Vertices.size(); ++i, ++k)
     {
         vertices[k].Pos = torus2.Vertices[i].Position;
		 vertices[k].Normal = torus2.Vertices[i].Normal;
		 vertices[k].TexC = torus2.Vertices[i].TexC;
     }
     for (size_t i = 0; i < cylinder2.Vertices.size(); ++i, ++k)
     {
         vertices[k].Pos = cylinder2.Vertices[i].Position;
		 vertices[k].Normal = cylinder2.Vertices[i].Normal;
		 vertices[k].TexC = cylinder2.Vertices[i].TexC;
     }
     /*for (size_t i = 0; i < halfsphere.Vertices.size(); ++i, ++k)
     {
         vertices[k].Pos = halfsphere.Vertices[i].Position;
         vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
     }*/

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(triPrism.GetIndices16()), std::end(triPrism.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));
	indices.insert(indices.end(), std::begin(torus.GetIndices16()), std::end(torus.GetIndices16()));
    indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));
    indices.insert(indices.end(), std::begin(torus2.GetIndices16()), std::end(torus2.GetIndices16()));
    indices.insert(indices.end(), std::begin(cylinder2.GetIndices16()), std::end(cylinder2.GetIndices16()));
    //indices.insert(indices.end(), std::begin(halfsphere.GetIndices16()), std::end(halfsphere.GetIndices16()));

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
	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["prism"] = triPrismSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;
	geo->DrawArgs["torus"] = torusSubmesh;
    geo->DrawArgs["wedge"] = wedgeSubmesh;
    geo->DrawArgs["torus2"] = torus2Submesh;
    geo->DrawArgs["cylinder2"] = cylinder2Submesh;

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
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void ShapesApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	stone0->FresnelR0 = XMFLOAT3(0.95f, 0.95f, 0.95f);
	stone0->Roughness = 0.01f;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.3f;

	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["tile0"] = std::move(tile0);
}

//makes building render items simpler, reduces repeated chunks of code
//the itemType is the key used to access the submesh
void ShapesApp::SetRenderItemInfo(RenderItem &Ritem, std::string itemType, XMMATRIX transform)
{
    Ritem.ObjCBIndex = objCBIndex++;
    XMStoreFloat4x4(&Ritem.World, transform);
    Ritem.Mat = mMaterials["stone0"].get();
    Ritem.Geo = mGeometries["shapeGeo"].get();
    Ritem.PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    Ritem.IndexCount = Ritem.Geo->DrawArgs[itemType].IndexCount;
    Ritem.StartIndexLocation = Ritem.Geo->DrawArgs[itemType].StartIndexLocation;
    Ritem.BaseVertexLocation = Ritem.Geo->DrawArgs[itemType].BaseVertexLocation;
    XMMATRIX inverseTransform = XMMatrixTranspose(transform); //MathHelper::InverseTranspose(transform);
    XMStoreFloat4x4(&Ritem.TWorld, inverseTransform);
}

void ShapesApp::BuildRenderItems()
{
    float thetaSquareStep = XM_2PI /4;  //90 degrees
    float w2, d2;
    w2 = d2 = width * 0.5;
    float radius = sqrt(w2 * w2 + d2 * d2);

    auto gridRitem = std::make_unique<RenderItem>();
    XMMATRIX gridWorld = XMMatrixIdentity();
	
    SetRenderItemInfo(*gridRitem, "grid",gridWorld);
	mAllRitems.push_back(std::move(gridRitem));

    //tower objects
    for (int i = 0; i < 4; ++i)
    {
        float theta = i * thetaSquareStep + thetaSquareStep * 0.5;
        float sRadius = radius * sinf(theta);
        float cRadius = radius * cosf(theta);

        auto towerRitem = std::make_unique<RenderItem>();
        auto poleRitem = std::make_unique<RenderItem>();
        auto sphereRitem = std::make_unique<RenderItem>();
        auto flagRitem = std::make_unique<RenderItem>();
       

        XMMATRIX towerWorld = XMMatrixScaling(6.0f, 7.0f, 6.0f) * XMMatrixTranslation(cRadius, 7.0f, sRadius); //keep in mind the base cylinder has a height of 2
        XMMATRIX poleWorld = XMMatrixScaling(0.2f, 2.0f, 0.2f) * XMMatrixTranslation(cRadius, 21.0f, sRadius);
        XMMATRIX sphereWorld = XMMatrixScaling(0.3f, 0.3f, 0.3f) * XMMatrixTranslation(cRadius, 23.1f, sRadius);
        XMMATRIX flagWorld = XMMatrixScaling(1.5f, 1.0f, 0.1f) * XMMatrixTranslation(cRadius - 1, 22.5f, sRadius);

        SetRenderItemInfo(*towerRitem, "cylinder", towerWorld);

        SetRenderItemInfo(*poleRitem, "cylinder", poleWorld);


        SetRenderItemInfo(*flagRitem, "box", flagWorld);

        SetRenderItemInfo(*sphereRitem, "sphere", sphereWorld);
   

        mAllRitems.push_back(std::move(towerRitem));
        mAllRitems.push_back(std::move(poleRitem));
        mAllRitems.push_back(std::move(sphereRitem));
        mAllRitems.push_back(std::move(flagRitem));

        //adding the rooves seperately from the other tower components, since the last tower wont have one
        if (i < 3)
        {
            auto roofRitem = std::make_unique<RenderItem>();
            XMMATRIX roofWorld = XMMatrixScaling(8.0f, 6.0f, 8.0f) * XMMatrixTranslation(cRadius, 17.0f, sRadius);
            SetRenderItemInfo(*roofRitem, "cone", roofWorld);
            mAllRitems.push_back(std::move(roofRitem));
        }
    }
    
    //adding pale and torus on their own

    auto paleRitem = std::make_unique<RenderItem>();
    XMMATRIX paleWorld = XMMatrixScaling(3.5f, 3.0f, 3.5f) * XMMatrixTranslation(w2, 17.5f, -d2);
    SetRenderItemInfo(*paleRitem, "cylinder2", paleWorld);
    mAllRitems.push_back(std::move(paleRitem));

    auto torusRitem = std::make_unique<RenderItem>();
    XMMATRIX torusWorld = XMMatrixScaling(1.8f, 2.0f, 1.8f) * XMMatrixTranslation(w2, 14.5f, -d2);
    SetRenderItemInfo(*torusRitem, "torus2", torusWorld);
    mAllRitems.push_back(std::move(torusRitem));



    //walls 
    for (int i = 0; i < 4; i++)
    {
        float theta = i * thetaSquareStep;
        float sRadius = w2 * sinf(theta);
        float cRadius = w2 * cosf(theta);

        //the three identical castle walls
        if (i < 3)
        {
            auto boxRitem = std::make_unique<RenderItem>();
            XMMATRIX world = XMMatrixScaling(1.0f, 10.0f, width) * XMMatrixRotationY(theta) * XMMatrixTranslation(cRadius, 5.0f, sRadius);
           
            SetRenderItemInfo(*boxRitem, "box", world);
            mAllRitems.push_back(std::move(boxRitem));
        }
        //the prism along the top of the walls
        auto prismRitem = std::make_unique<RenderItem>();
        XMMATRIX PrismWorld = XMMatrixScaling(1.0f, 4.0f, width - 3) * XMMatrixRotationY(theta) * XMMatrixTranslation(cRadius, 10.5f, sRadius);
       
        SetRenderItemInfo(*prismRitem, "prism", PrismWorld);
        mAllRitems.push_back(std::move(prismRitem));

        int mogulsNum = 50;
        for (int j =0; j < mogulsNum; j+= 2) //changed for the loop to increment by 2 to make nesting a little clearer to read, also less division
        {
          
            auto boxRitem = std::make_unique<RenderItem>();
            XMMATRIX MogulWorld;
            if (theta == XM_PI || theta == 0) 
            {
                MogulWorld = XMMatrixScaling(2.0f, 1.0f, 1.0) * XMMatrixRotationY(theta) * XMMatrixTranslation(cRadius , 12.8f, (sRadius-25) + j);
            }
            else 
            {
                 MogulWorld = XMMatrixScaling(2.0f, 1.0f, 1.0)* XMMatrixRotationY(theta)* XMMatrixTranslation((cRadius-25) + j, 12.8f, sRadius );
            }
            SetRenderItemInfo(*boxRitem, "box", MogulWorld);
            mAllRitems.push_back(std::move(boxRitem));
        }
            
    
    }

    //smaller, front walls
    for (int i = 0; i < 2; i++)
    {
        auto boxRitem = std::make_unique<RenderItem>();
        XMMATRIX WallWorld = XMMatrixScaling(19.0f, 10.0f, 1.0f) * XMMatrixTranslation(-12.5f + i * 25.0f, 5.0f, -25.0f);
        SetRenderItemInfo(*boxRitem, "box", WallWorld);
        mAllRitems.push_back(std::move(boxRitem));
    }

    auto pyramidRitem = std::make_unique<RenderItem>();
    XMMATRIX PyrWorld = XMMatrixScaling(21.0f, 6.0f, 21.0f) * XMMatrixTranslation(0.0f, 7.5f, 13.0f);
    SetRenderItemInfo(*pyramidRitem, "pyramid", PyrWorld);
    mAllRitems.push_back(std::move(pyramidRitem));

    auto diamondRitem = std::make_unique<RenderItem>();
    XMMATRIX DiamondWorld = XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 13.0f, 13.0f);
    SetRenderItemInfo(*diamondRitem, "diamond", DiamondWorld);
    mAllRitems.push_back(std::move(diamondRitem));

    auto RingRitem = std::make_unique<RenderItem>();
    XMMATRIX RingWorld = XMMatrixScaling(0.5f, 0.5f, 0.5f) * XMMatrixRotationX(1.571) * XMMatrixTranslation(0.0f, 11.75f, 13.0f);
    SetRenderItemInfo(*RingRitem, "torus", RingWorld);
    mAllRitems.push_back(std::move(RingRitem));
    
    for (int i = 0; i < 2; i++)
    {
        auto wedgeRitem = std::make_unique<RenderItem>();
        XMMATRIX wedgeWorld = (XMMatrixRotationY(-thetaSquareStep) * XMMatrixScaling(3.0f, 3.0f, 18.0f) * XMMatrixTranslation(0.0f, 4.5f + i * -3, -3.5f + i * -31));
        SetRenderItemInfo(*wedgeRitem, "wedge", wedgeWorld);
        mAllRitems.push_back(std::move(wedgeRitem));
    }

    auto pathRitem = std::make_unique<RenderItem>();
    XMMATRIX pathWorld = XMMatrixScaling(6.0f, 3.0f, 13.0f)* XMMatrixTranslation( 0.0f, 1.5f, -19.0f);
    SetRenderItemInfo(*pathRitem, "box", pathWorld);

    mAllRitems.push_back(std::move(pathRitem));

	// All the render items are opaque.
	for(auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}


//The DrawRenderItems method is invoked in the main Draw call:
void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
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

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
        D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }

}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> ShapesApp::GetStaticSamplers()
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


