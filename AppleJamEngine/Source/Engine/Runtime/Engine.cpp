#include "Engine/Runtime/Engine.h"

#include "Platform/Paths.h"
#include "Core/Logging/Log.h"
#include "Core/Logging/Notification.h"
#include "Engine/Platform/DirectoryWatcher.h"
#include "Profiling/Stats/Stats.h"
#include "Profiling/Stats/ParticleStats.h"
#include "Profiling/Stats/ClothCollisionStats.h"
#include "Profiling/Stats/BulletHellStats.h"
#include "Component/Gameplay/BossPatternDebug.h"
#include "AI/BT/BehaviorTreeDebug.h"
#include "Profiling/StartupProfiler.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Platform/WindowsWindow.h"
#include "Resource/ResourceManager.h"
#include "Render/Pipeline/DefaultRenderPipeline.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Mesh/MeshManager.h"
#include "Texture/Texture2D.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "Viewport/GameViewportClient.h"
#include "Core/TickFunction.h"
#include "Lua/LuaScriptManager.h"
#include "UI/UIManager.h"
#include "UI/Canvas/UICanvasManager.h"
#include "Core/ProjectSettings.h"
#include "Audio/AudioManager.h"
#include "Object/GarbageCollection.h"
#include "LuaBlueprint/LuaBlueprintManager.h"
#include "FloatCurve/FloatCurveManager.h"
#include "CameraShake/CameraShakeManager.h"
#include "Particle/ParticleSystemManager.h"
#include "UI/UIAssetManager.h"
#include "Animation/Graph/AnimGraphManager.h"
#include "Animation/Skeleton/SkeletonManager.h"
#include "Animation/AnimationManager.h"
#include "Materials/MaterialManager.h"
#include "Physics/PhysicsAssetManager.h"

UEngine* GEngine = nullptr;

namespace
{
	ELevelTick ToLevelTickType(EWorldType WorldType)
	{
		switch (WorldType)
		{
		case EWorldType::Editor:
		case EWorldType::EditorPreview:
			return ELevelTick::LEVELTICK_ViewportsOnly;
		case EWorldType::PIE:
		case EWorldType::Game:
			return ELevelTick::LEVELTICK_All;
		default:
			return ELevelTick::LEVELTICK_TimeOnly;
		}
	}
}

void UEngine::AddReferencedObjects(FReferenceCollector& Collector)
{
	UObject::AddReferencedObjects(Collector);
	for (FWorldContext& Context : WorldList)
	{
		Collector.AddReferencedObject(Context.World, "WorldContext.World");
	}
	Collector.AddReferencedObject(GameViewportClient, "GameViewportClient");
}


void UEngine::Init(FWindowsWindow* InWindow)
{
	AddToRoot();
	Window = InWindow;

	// 싱글턴 초기화 순서 보장
	FNamePool::Get();
	FObjectFactory::Get();

	InputSystem::Get().SetOwnerWindow(Window->GetHWND());

	{
		SCOPE_STARTUP_STAT("Renderer::Create");
		Renderer.Create(Window->GetHWND());
	}

	ID3D11Device* Device = Renderer.GetFD3DDevice().GetDevice();

	{
		SCOPE_STARTUP_STAT("MeshBufferManager::Init");
		FMeshBufferManager::Get().Initialize(Device);
	}

	{
		SCOPE_STARTUP_STAT("ResourceManager::LoadFromFile");
		FResourceManager::Get().LoadFromFile(FPaths::ToUtf8(FPaths::ResourceFilePath()), Device);
	}

	{
		SCOPE_STARTUP_STAT("RenderPipeline::Create");
		SetRenderPipeline(std::make_unique<FDefaultRenderPipeline>(this, Renderer));
	}

	UUIManager::Get().Initialize(Device);

	FLogManager::Get().Initialize();
	FDirectoryWatcher::Get().Initialize();
	FLuaScriptManager::Initialize();
	FAudioManager::Get().Initialize();
}

void UEngine::Shutdown()
{
	while (!WorldList.empty())
	{
		DestroyWorldContext(WorldList.back().ContextHandle);
	}

	// UI 가 Lua callback (FWidgetClickEventListener::Callback 의 sol::protected_function 등)
	// 을 보유하므로, 위젯 destroy 시점에 lua_State 가 살아있어야 deref 가 안전.
	// 따라서 UIManager → LuaScriptManager 순서.
	UUIManager::Get().Shutdown();
	FLuaScriptManager::Shutdown();
	FAudioManager::Get().Shutdown();
	FDirectoryWatcher::Get().Shutdown();
	FLogManager::Get().Shutdown();

	// Any render pass/UI/viewport can leave resources bound on the immediate context.
	// Detach before tearing down GPU resource owners so COM refcounts and debug-layer state
	// do not keep stale bindings alive until FD3DDevice::Release().
	Renderer.GetFD3DDevice().ReleaseImmediateContextBindings(false);
	RenderPipeline.reset();
	Renderer.GetFD3DDevice().ReleaseImmediateContextBindings(false);
	FResourceManager::Get().ReleaseGPUResources();
	UTexture2D::ReleaseAllGPU();
	FMeshManager::ReleaseAllGPU();
	FMaterialManager::Get().Release();

	// PhysicsAssetManager is also an FGCObject root. If this cache is left alive until
	// process teardown, loaded PhysicsAsset data stays referenced and CRT reports it
	// as a leak, especially after opening/simulating in the Physics Asset editor.
	FPhysicsAssetManager::Get().ClearCache();

	FAnimationManager::Get().ClearCache();
	FSkeletonManager::Get().ClearCache();
	FAnimGraphManager::Get().ClearCache();
	FLuaBlueprintManager::Get().ClearCache();
	FParticleSystemManager::Get().ClearCache();
	FCameraShakeManager::Get().ClearCache();
	FFloatCurveManager::Get().ClearCache();
	FUIAssetManager::Get().ClearCache();

	FMeshBufferManager::Get().Release();
	Renderer.Release();
	RemoveFromRoot();
}

void UEngine::BeginPlay()
{
	FWorldContext* Context = GetWorldContextFromHandle(ActiveWorldHandle);
	if (Context && Context->World)
	{
		if (Context->WorldType == EWorldType::Game || Context->WorldType == EWorldType::PIE)
		{
			Context->World->BeginPlay();
		}
	}
}

void UEngine::TickFrameStart(float DeltaTime)
{
	FDirectoryWatcher::Get().ProcessChanges();
	FNotificationManager::Get().Tick(DeltaTime);
	InputSystem::Get().Tick();
}

void UEngine::TickFrameBody(float DeltaTime)
{
	FAudioManager::Get().Tick();
	WorldTick(DeltaTime);

	// 런타임 UI 드래그 에디터 — 토글키/드래그로 Element.position 을 갱신한다(진단 E2).
	// 레이아웃 전에 호출해 같은 프레임에 갱신 결과가 반영되도록 한다.
	FUICanvasManager::Get().TickEditor();

	// 신규 계층형 UI 레이아웃 패스 — 게임플레이/월드 틱 정산 후, 렌더 제출 전에 1회
	// top-down 전체 재계산(진단 C1). 레이아웃은 렌더패스가 아니라 게임스레드에서 한다.
	FUICanvasManager::Get().LayoutAll();

	Render(DeltaTime);
}

void UEngine::ProcessActiveWorldPlayerInput(const FInputSystemSnapshot& Snapshot, float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || World->IsPaused())
	{
		return;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return;
	}

	PlayerController->ProcessPlayerInput(Snapshot, DeltaTime);
}

void UEngine::Tick(float DeltaTime)
{
	TickFrameStart(DeltaTime);
	TickFrameBody(DeltaTime);
}

void UEngine::Render(float DeltaTime)
{
	if (RenderPipeline)
	{
		SCOPE_STAT_CAT("UEngine::Render", "2_Render");
		RenderPipeline->Execute(DeltaTime, Renderer);
	}
}

void UEngine::SetRenderPipeline(std::unique_ptr<IRenderPipeline> InPipeline)
{
	RenderPipeline = std::move(InPipeline);
}

void UEngine::OnWindowResized(uint32 Width, uint32 Height)
{
	if (Width == 0 || Height == 0)
	{
		return;
	}

	Renderer.GetFD3DDevice().OnResizeViewport(Width, Height);
	Renderer.ResetRenderStateCache();

	// 신규 계층형 UI 의 GlobalScale 갱신 — 레퍼런스 해상도(높이) 대비 균일 스케일(진단 D2/D4).
	// 리사이즈마다 1회만 계산해 FUICanvasManager 에 저장하고, 레이아웃 패스가 픽셀 결과에 곱한다.
	const float RefResY = FProjectSettings::Get().UI.RefResY;
	if (RefResY > 0.0f)
	{
		FUICanvasManager::Get().SetGlobalScale(static_cast<float>(Height) / RefResY);
	}
}

void UEngine::WorldTick(float DeltaTime)
{
	SCOPE_STAT_CAT("UEngine::WorldTick", "1_WorldTick");

	// 파티클 stat은 GT tick 중(PSC TickComponent / emitter Spawn·Update) 누적되므로,
	// 월드 tick 루프 진입 전 여기서 프레임 카운터를 리셋한다. (Peak은 유지)
	PARTICLE_STATS_RESET();
	CLOTH_COLLISION_STATS_RESET();
	BULLETHELL_STATS_RESET();
	BOSSPATTERN_DEBUG_RESET();
	BT_DEBUG_RESET();

	// PIE 활성 시 Editor 월드는 sleep (UE 동작과 동일).
	// culling/octree/visibility 갱신을 건너뛰어 50k+ 환경에서 비용 2배를 방지.
	bool bHasPIEWorld = false;
	for (const FWorldContext& Ctx : WorldList)
	{
		if (Ctx.WorldType == EWorldType::PIE && Ctx.World)
		{
			bHasPIEWorld = true;
			break;
		}
	}

	// 월드 타입별 Tick 라우팅:
	// - Editor: bTickInEditor 액터만 TickManager 대상
	// - PIE/Game: BeginPlay 이후 bNeedsTick 액터만 TickManager 대상
	// - 기타:   시간 갱신만 유지
	for (FWorldContext& Ctx : WorldList)
	{
		UWorld* World = Ctx.World;
		if (!World) continue;

		// PIE 활성 시 Editor 월드는 완전히 skip
		if (bHasPIEWorld && Ctx.WorldType == EWorldType::Editor)
		{
			continue;
		}

		const ELevelTick TickType = ToLevelTickType(Ctx.WorldType);

		// 월드 단위 업데이트 (FlushPrimitive / VisibleProxies / DebugDraw /s TickManager)
		World->Tick(DeltaTime, TickType);
	}
}

UWorld* UEngine::GetWorld() const
{
	const FWorldContext* Context = GetWorldContextFromHandle(ActiveWorldHandle);
	return Context ? Context->World : nullptr;
}

FWorldContext& UEngine::CreateWorldContext(EWorldType Type, const FName& Handle, const FString& Name)
{
	FWorldContext Context;
	Context.WorldType = Type;
	Context.ContextHandle = Handle;
	Context.ContextName = Name.empty() ? Handle.ToString() : Name;
	Context.World = UObjectManager::Get().CreateObject<UWorld>();
	WorldList.push_back(Context);
	return WorldList.back();
}

void UEngine::DestroyWorldContext(const FName& Handle)
{
	for (auto it = WorldList.begin(); it != WorldList.end(); ++it)
	{
		if (it->ContextHandle == Handle)
		{
			it->World->EndPlay();
			UObjectManager::Get().DestroyObject(it->World);
			WorldList.erase(it);
			return;
		}
	}
}

FWorldContext* UEngine::GetWorldContextFromHandle(const FName& Handle)
{
	for (FWorldContext& Ctx : WorldList)
	{
		if (Ctx.ContextHandle == Handle)
		{
			return &Ctx;
		}
	}
	return nullptr;
}

const FWorldContext* UEngine::GetWorldContextFromHandle(const FName& Handle) const
{
	for (const FWorldContext& Ctx : WorldList)
	{
		if (Ctx.ContextHandle == Handle)
		{
			return &Ctx;
		}
	}
	return nullptr;
}

FWorldContext* UEngine::GetWorldContextFromWorld(const UWorld* World)
{
	for (FWorldContext& Ctx : WorldList)
	{
		if (Ctx.World == World)
		{
			return &Ctx;
		}
	}
	return nullptr;
}

void UEngine::SetActiveWorld(const FName& Handle)
{
	ActiveWorldHandle = Handle;
}
