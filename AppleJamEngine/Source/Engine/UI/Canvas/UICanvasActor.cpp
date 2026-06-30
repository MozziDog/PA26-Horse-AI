#include "UI/Canvas/UICanvasActor.h"

#include "UI/Canvas/UICanvas.h"
#include "UI/Canvas/UITextElement.h"
#include "UI/Canvas/UICanvasManager.h"
#include "UI/UIAsset.h"
#include "UI/UIAssetManager.h"
#include "Serialization/SceneSaveManager.h"
#include "GameFramework/World.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "Component/Gameplay/BossPatternSelectorComponent.h"
#include "Component/Gameplay/BossPatternComponentBase.h"
#include "Component/Gameplay/PlayerSprayProjectileComponent.h"
#include "Render/Types/MinimalViewInfo.h"
#include "Runtime/Engine.h"
#include "Platform/WindowsWindow.h"
#include "Platform/Paths.h"
#include "Core/Logging/Log.h"
#include "Core/ScoreManager.h"

#include <cstdio>
#include <cstring>

namespace
{
	// [사이클 2] 체력 비율 → 색: 가득(1)=녹 · 절반(0.5)=노 · 고갈(0)=적. 2구간 lerp 로 중간 브라운 회피.
	// FVector4 의 검증된 4-인자 생성자만 사용(성분 접근자 가정 없음).
	FVector4 HealthRatioToColor(float Ratio)
	{
		if (Ratio >= 0.5f)
		{
			const float T = (Ratio - 0.5f) * 2.0f;   // 노(0.5) → 녹(1.0)
			return FVector4(0.95f + (0.15f - 0.95f) * T,
			                0.85f,
			                0.15f + (0.20f - 0.15f) * T, 1.0f);
		}
		const float T = Ratio * 2.0f;                // 적(0.0) → 노(0.5)
		return FVector4(0.90f + (0.95f - 0.90f) * T,
		                0.15f + (0.85f - 0.15f) * T,
		                0.15f, 1.0f);
	}

	// [사이클 5] 보스 컴포넌트의 디버그 상태(BossHealthRatio/BossPhase/ActivePatternName). 없으면 nullptr.
	const FBossPatternDebugState* GetBossState(APawn* Pawn)
	{
		if (!Pawn)
		{
			return nullptr;
		}
		if (UBossPatternSelectorComponent* Boss = Pawn->GetComponentByClass<UBossPatternSelectorComponent>())
		{
			return &Boss->GetBossPatternDebugState();
		}
		return nullptr;
	}

	// [사이클 7] 플레이어 스프레이 컴포넌트(UPlayerSprayProjectileComponent). 없으면 nullptr.
	// 보스값(GetBossState)과 동형 — GetComponentByClass 는 씬 로드분/런타임 추가분을 모두 찾는다.
	UPlayerSprayProjectileComponent* GetPlayerSpray(APawn* Pawn)
	{
		if (!Pawn)
		{
			return nullptr;
		}
		return Pawn->GetComponentByClass<UPlayerSprayProjectileComponent>();
	}

	// [사이클 4/5] 바인딩 값 → 바 비율 [0,1]. 체력=GetHealthRatio, 보스HP=컴포넌트, 페이즈=색매핑, 그 외=full.
	float ValueAsRatio(EHudBindingValue Value, APawn* Pawn)
	{
		switch (Value)
		{
		case EHudBindingValue::HealthCurrent:
		case EHudBindingValue::HealthOverMax:
		case EHudBindingValue::HealthPercent:
			return Pawn ? Pawn->GetHealthRatio() : 0.0f;
		case EHudBindingValue::BossHealthRatio:
			if (const FBossPatternDebugState* S = GetBossState(Pawn)) { return S->BossHealthRatio; }
			return Pawn ? Pawn->GetHealthRatio() : 0.0f;       // 컴포넌트 없으면 폰 체력 폴백
		case EHudBindingValue::BossPhase:
			// 페이즈 0→1.0(녹) · 1→0.5(노) · 2+→0.0(적) → BarColor 가 페이즈색이 된다.
			if (const FBossPatternDebugState* S = GetBossState(Pawn))
			{
				return S->BossPhase <= 0 ? 1.0f : (S->BossPhase == 1 ? 0.5f : 0.0f);
			}
			return 1.0f;
		case EHudBindingValue::PlayerUltimateRatio:
		case EHudBindingValue::PlayerUltimateGauge:
			// 궁극기 게이지 비율(현재/최대). 컴포넌트 없거나 최대=0 이면 0(빈 바).
			if (UPlayerSprayProjectileComponent* Spray = GetPlayerSpray(Pawn))
			{
				const float Max = Spray->GetUltimateGaugeMax();
				return Max > 0.0f ? (Spray->GetUltimateGauge() / Max) : 0.0f;
			}
			return 0.0f;
		case EHudBindingValue::BossPatternName:
		case EHudBindingValue::GameTime:
		case EHudBindingValue::PlayerProjectileCount:
			return 1.0f;       // 비-비율 값은 바에서 full
		case EHudBindingValue::Score:
			return FScoreManager::Get().GetCurrentScore() / 10000.0f;   // 점수/10000 → 바 비율(0~1)
		}
		return 0.0f;
	}

	// [사이클 4] 바인딩 값 → 표시 문자열(텍스트 채널). 소스 무효 시 "--". snprintf 로 FString(const char*) 구성.
	FString FormatBindingValue(EHudBindingValue Value, APawn* Pawn, UWorld* World)
	{
		char Buf[64] = {};
		switch (Value)
		{
		case EHudBindingValue::HealthCurrent:
			if (Pawn) { snprintf(Buf, sizeof(Buf), "%d", (int)(Pawn->GetCurrentHealth() + 0.5f)); return FString(Buf); }
			break;
		case EHudBindingValue::HealthOverMax:
			if (Pawn) { snprintf(Buf, sizeof(Buf), "%d / %d", (int)(Pawn->GetCurrentHealth() + 0.5f), (int)(Pawn->GetMaxHealth() + 0.5f)); return FString(Buf); }
			break;
		case EHudBindingValue::HealthPercent:
			if (Pawn) { snprintf(Buf, sizeof(Buf), "%d%%", (int)(Pawn->GetHealthRatio() * 100.0f + 0.5f)); return FString(Buf); }
			break;
		case EHudBindingValue::BossHealthRatio:
			if (const FBossPatternDebugState* S = GetBossState(Pawn)) { snprintf(Buf, sizeof(Buf), "%d%%", (int)(S->BossHealthRatio * 100.0f + 0.5f)); return FString(Buf); }
			break;
		case EHudBindingValue::BossPhase:
			if (const FBossPatternDebugState* S = GetBossState(Pawn)) { snprintf(Buf, sizeof(Buf), "Phase %d", S->BossPhase); return FString(Buf); }
			break;
		case EHudBindingValue::BossPatternName:
			if (const FBossPatternDebugState* S = GetBossState(Pawn)) { return S->ActivePatternName; }
			break;
		case EHudBindingValue::GameTime:
			if (World) { snprintf(Buf, sizeof(Buf), "%ds", (int)World->GetGameTimeSeconds()); return FString(Buf); }
			break;
		case EHudBindingValue::PlayerUltimateRatio:
			if (UPlayerSprayProjectileComponent* Spray = GetPlayerSpray(Pawn))
			{
				const float Max = Spray->GetUltimateGaugeMax();
				const float Ratio = Max > 0.0f ? (Spray->GetUltimateGauge() / Max) : 0.0f;
				snprintf(Buf, sizeof(Buf), "%d%%", (int)(Ratio * 100.0f + 0.5f));
				return FString(Buf);
			}
			break;
		case EHudBindingValue::PlayerUltimateGauge:
			if (UPlayerSprayProjectileComponent* Spray = GetPlayerSpray(Pawn))
			{
				snprintf(Buf, sizeof(Buf), "%d / %d", (int)(Spray->GetUltimateGauge() + 0.5f), (int)(Spray->GetUltimateGaugeMax() + 0.5f));
				return FString(Buf);
			}
			break;
		case EHudBindingValue::PlayerProjectileCount:
			if (UPlayerSprayProjectileComponent* Spray = GetPlayerSpray(Pawn))
			{
				snprintf(Buf, sizeof(Buf), "%d", Spray->GetProjectileCount());
				return FString(Buf);
			}
			break;
		case EHudBindingValue::Score:
			// 전역 점수 — 보스 히트/발사 카운트로 산출(소스 액터 불필요).
			snprintf(Buf, sizeof(Buf), "%d", FScoreManager::Get().GetCurrentScore());
			return FString(Buf);
		}
		return FString("--");
	}

	// [사이클 4] 소스 폰 해석. 빈 이름=로컬 플레이어(possessed pawn), 지정 시 그 이름(GetFName)의 액터.
	APawn* ResolveSourcePawn(UWorld* World, const FString& Name)
	{
		if (!World)
		{
			return nullptr;
		}
		if (Name.empty())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				return PC->GetPossessedPawn();
			}
			return nullptr;
		}
		for (AActor* A : World->GetActors())
		{
			if (IsValid(A) && A->GetFName().ToString() == Name)
			{
				return Cast<APawn>(A);
			}
		}
		return nullptr;
	}

	// 에셋 경로 비교용 정규화 키 — project-relative + 구분자(슬래시) 통일 + 소문자.
	// GetSourcePath()(매니저가 MakeProjectRelative 로 설정)와 액터 UIAssetPath(드롭/디테일에서 설정한
	// 경로)가 대소문자·구분자 표기에서 달라도 같은 .uasset 이면 매칭되도록 한다.
	FString NormalizeAssetKey(const FString& Path)
	{
		FString Key = FPaths::MakeProjectRelative(Path);
		for (size_t i = 0; i < Key.size(); ++i)
		{
			char& c = Key[i];
			if (c == '\\') { c = '/'; }
			else if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); }
		}
		return Key;
	}
}

void AUICanvasActor::InitCanvas()
{
	if (Canvas.Get())
	{
		return;
	}
	// 로드된 씬: 컴포넌트 트리 직렬화(DeserializeSceneComponentTree)가 이미 UUICanvas 를
	// RootComponent 로 복원했을 수 있다. 그 경우 새로 만들지 말고 재사용한다(중복 생성 방지).
	if (UUICanvas* Existing = Cast<UUICanvas>(GetRootComponent()))
	{
		Canvas = Existing;
		return;
	}
	UUICanvas* NewCanvas = AddComponent<UUICanvas>();
	SetRootComponent(NewCanvas);
	Canvas = NewCanvas;
}

void AUICanvasActor::LoadFromAsset(const FString& InAssetPath)
{
	if (InAssetPath.empty())
	{
		return;
	}

	// 이 액터의 UI .uasset 참조를 기록한다(직렬화/재구성의 기준 — R4). 직접 호출돼도 에셋 참조 모델 유지.
	UIAssetPath = InAssetPath;

	// 파일 단위 로드(헤더 검증 + 트리 JSON 페이로드). 경로는 매니저가 project-relative 로 정규화.
	UUIAsset* Asset = FUIAssetManager::Get().Load(InAssetPath);
	if (!Asset)
	{
		UE_LOG("[UICanvasActor] LoadFromAsset: UI .uasset 로드 실패 — '%s'", InAssetPath.c_str());
		return;
	}

	// JSON 블롭 → 라이브 캔버스 트리. 컴포넌트는 이 액터(Owner=this)에 등록된다.
	// 선례: FUIEditorWidget::BuildLiveTree (CreateObject<AUICanvasActor> + DeserializeUITree + SetRootComponent).
	USceneComponent* Root = FSceneSaveManager::DeserializeUITree(Asset->GetCanvasData(), this);
	UUICanvas* NewCanvas = Cast<UUICanvas>(Root);
	if (!NewCanvas)
	{
		UE_LOG("[UICanvasActor] LoadFromAsset: 복원된 루트가 UUICanvas 가 아님 — '%s'", InAssetPath.c_str());
		return;
	}

	// [R2] InitCanvas() 의 "빈 UUICanvas 새로 생성" 분기를 일부러 우회한다. 복원된 캔버스를
	// 곧장 RootComponent 로 세팅함으로써, 빈 캔버스가 먼저 루트로 박힌 뒤 교체되어 두 개의
	// UUICanvas 가 생기는 이중 루트를 막는다. 이후 BeginPlay 에서 InitCanvas 가 다시 호출돼도
	// RootComponent 가 이미 UUICanvas 이므로 그걸 재사용한다(중복 생성 없음).
	SetRootComponent(NewCanvas);
	Canvas = NewCanvas;

	// [R1] 화면 렌더 등록(RegisterCanvas)은 여기서 하지 않고 BeginPlay 로 미룬다. 따라서 편집
	// 모드 월드(bHasBegunPlay=false → UWorld::AddActor 가 BeginPlay 미호출)에서는 드롭한 UI 가
	// 화면에 뜨지 않고, Play(PIE)/런타임에서만 보인다. 편집 모드 즉시 프리뷰는 의도적으로 후행
	// 사이클(진단 사이클 4)로 분리했다 — 버그가 아니다.
	// [R3] 만약 후행에서 편집 모드 프리뷰를 위해 이 자리(또는 스폰 시점)에서 RegisterCanvas 를
	// 하게 되면, 런타임 LayoutAll 의 bSyncExternal 경로를 타게 되어 RmlUi 텍스트가 게임 viewport
	// 로 새어나갈 수 있다. 그때는 에디터 전용 격리(bSyncExternal=false 또는 ImGui 미러)가 필요하다.
}

void AUICanvasActor::BeginPlay()
{
	Super::BeginPlay();

	// [R4] 에셋 참조 모델: UIAssetPath 가 있으면 .Scene 인라인 트리가 아니라 .uasset 에서 캔버스를
	// 재구성한다(저장 시 캔버스 서브트리를 직렬화하지 않으므로 — ShouldSerializeRootComponentTree —
	// 로드 후엔 트리가 없다). 단 이미 RootComponent 가 UUICanvas 면(편집 월드에서 빌드 후 PIE 복제로
	// 트리가 넘어온 경우) 재빌드하지 않고 재사용한다(이중 트리 방지 — R2 의 연장).
	if (!Cast<UUICanvas>(GetRootComponent()) && !UIAssetPath.IsNull())
	{
		LoadFromAsset(UIAssetPath.ToString());
	}

	InitCanvas();
	// [R1] UI 의 화면 렌더 합류는 이 등록 한 줄에 달려 있고, BeginPlay 는 PIE/런타임에서만
	// 돈다(에디터 월드는 BeginPlay 미진입). 즉 등록=PIE/런타임 한정 → 편집 모드에선 의도적으로
	// 안 보인다(메시는 프록시 기반이라 편집 모드에서도 보이는 것과 비대칭이지만 정상 동작이다).
	if (UUICanvas* C = Canvas.Get())
	{
		FUICanvasManager::Get().RegisterCanvas(C);
	}
}

bool AUICanvasActor::ShouldSerializeRootComponentTree() const
{
	// [R4] UIAssetPath 가 있으면 캔버스 트리는 .uasset 에서 재구성되므로 .Scene 에 인라인으로
	// 저장하지 않는다(인라인 트리 + 에셋 참조 동시 저장 → 중복/충돌 방지). 참조가 없으면 기존처럼 인라인.
	return UIAssetPath.IsNull();
}

void AUICanvasActor::EnsureCanvasForEditor()
{
	// 이미 루트가 UUICanvas 면 재사용(인라인 트리가 역직렬화로 복원된 경우 등).
	if (UUICanvas* Root = Cast<UUICanvas>(GetRootComponent()))
	{
		Canvas = Root;
		return;
	}
	// 에셋 참조 모델: .uasset 에서 트리 복원(LoadFromAsset 이 SetRootComponent 까지 수행).
	if (!UIAssetPath.IsNull())
	{
		LoadFromAsset(UIAssetPath.ToString());
	}
	// 로드 실패/경로 없음 → 빈 캔버스라도 보장(루트가 있어야 에디터에서 선택·디테일이 뜬다).
	if (!Cast<UUICanvas>(GetRootComponent()))
	{
		InitCanvas();
	}
}

void AUICanvasActor::RebuildCanvasFromAsset()
{
	// 기존 루트 캔버스를 정리(이중 캔버스 방지 — R2)한 뒤 에셋에서 다시 빌드한다. 편집 월드 전용이라
	// 캔버스가 매니저에 등록돼 있지 않으므로(Tier2 미러는 LayoutCanvas 직접 호출) 등록 처리는 불필요.
	if (UUICanvas* Old = Canvas.Get())
	{
		RemoveComponent(Old);
		Canvas = nullptr;
	}
	EnsureCanvasForEditor();
}

void AUICanvasActor::RefreshActorsReferencingAsset(const FString& AssetPath)
{
	if (AssetPath.empty() || !GEngine)
	{
		return;
	}

	const FString Target = NormalizeAssetKey(AssetPath);

	// 활성 월드(GEngine->GetWorld()) 한 곳만 보지 않고 모든 "편집 월드" 컨텍스트를 순회한다. UI 에셋을
	// 저장하는 시점에 다른 에셋 에디터(머티리얼/메시 등 EditorPreview)가 열려 활성 월드가 레벨 월드가
	// 아니면, 활성 월드만 봤을 때 레벨의 AUICanvasActor 를 놓쳐 디테일 패널/뷰포트 미러가 갱신되지 않는다.
	// PIE/Game/EditorPreview 는 제외 — 등록 캔버스(BeginPlay 관리)·프리뷰를 건드리지 않도록 타입 게이트.
	for (const FWorldContext& Ctx : GEngine->GetWorldList())
	{
		UWorld* World = Ctx.World;
		if (!World || World->GetWorldType() != EWorldType::Editor)
		{
			continue;
		}
		for (AActor* A : World->GetActors())
		{
			if (AUICanvasActor* UICanvasActor = Cast<AUICanvasActor>(A))
			{
				if (NormalizeAssetKey(UICanvasActor->GetUIAssetPath()) == Target)
				{
					UICanvasActor->RebuildCanvasFromAsset();
				}
			}
		}
	}
}

void AUICanvasActor::PostDuplicate()
{
	Super::PostDuplicate();
	// [에디터] 씬 로드/복제 직후 호출(SceneSaveManager 가 컴포넌트·속성 복원 후 호출). 에디터 월드는
	// BeginPlay 미진입이라 여기서 루트 캔버스를 보장해야 선택/디테일/뷰포트 미러가 가능하다.
	// 런타임(PIE)에선 이후 BeginPlay 의 InitCanvas 가 이 캔버스를 재사용하고 RegisterCanvas 한다(무중복).
	EnsureCanvasForEditor();
}

void AUICanvasActor::PostEditProperty(const char* PropertyName)
{
	Super::PostEditProperty(PropertyName);
	// [에디터] 디테일에서 UI Asset 경로(멤버명 "UIAssetPath")를 바꾸면 캔버스를 새 에셋으로 재구성.
	// 기존 루트 캔버스는 먼저 제거해 이중 캔버스(R2)를 막는다.
	if (PropertyName && strcmp(PropertyName, "UIAssetPath") == 0)
	{
		if (UUICanvas* Old = Canvas.Get())
		{
			RemoveComponent(Old);
			Canvas = nullptr;
		}
		EnsureCanvasForEditor();
	}
}

void AUICanvasActor::EndPlay()
{
	if (UUICanvas* C = Canvas.Get())
	{
		FUICanvasManager::Get().UnregisterCanvas(C);
	}
	Super::EndPlay();
}

void AUICanvasActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateDataBindings();
}

void AUICanvasActor::UpdateDataBindings()
{
	// 캔버스 미구성(편집/미등록)이면 비활성. 매 프레임 호출되므로 빠른 탈출 우선.
	UUICanvas* C = Canvas.Get();
	if (!C)
	{
		return;
	}

	UWorld* W = GetWorld();
	if (!W)
	{
		return;
	}

	// [사이클 4] 각 바인딩을 독립 적용: 소스 폰 해석(캐시) → 대상 요소 조회 → 채널별 setter.
	for (FHudBinding& B : Bindings)
	{
		if (B.TargetElement.empty())
		{
			continue;
		}

		// 소스 폰 해석 — 캐시 우선, 무효일 때만 재해석(매 프레임 선형 스캔 회피).
		APawn* Source = B.SourceCache.Get();
		if (!IsValid(Source))
		{
			Source = ResolveSourcePawn(W, B.SourceActor);
			B.SourceCache = Source;
		}

		UUIElement* El = C->FindByName(B.TargetElement);
		if (!El)
		{
			continue;
		}

		switch (B.Channel)
		{
		case EHudBindingChannel::BarWidth:
		{
			// 최초 적용 시 저작 폭을 100% 기준으로 캡처(음수 sentinel 일 때만).
			if (B.BarFullWidth < 0.0f)
			{
				B.BarFullWidth = El->GetSize().X;
			}
			const float    Ratio = ValueAsRatio(B.Value, Source);
			const FVector2 Cur   = El->GetSize();
			El->SetSize(FVector2(B.BarFullWidth * Ratio, Cur.Y));
			break;
		}
		case EHudBindingChannel::BarColor:
		{
			// draw 가 BackgroundColor 를 매 프레임 live read 하므로 relayout 불필요(D2).
			El->SetColor(HealthRatioToColor(ValueAsRatio(B.Value, Source)));
			break;
		}
		case EHudBindingChannel::Text:
		{
			// UUITextElement 계열만 텍스트 표시. OnLayoutUpdated 가 RmlUi 에 재푸시(재마운트 아님).
			if (UUITextElement* TextEl = Cast<UUITextElement>(El))
			{
				TextEl->SetText(FormatBindingValue(B.Value, Source, W));
			}
			break;
		}
		case EHudBindingChannel::WorldAnchor:
		{
			// [사이클 6] 소스 액터 월드 위치(+높이)를 화면에 투영해 Position 설정(옵션: 머리 위 추적).
			// Position 은 design px(=실제px / GlobalScale). 요소는 Anchor=(0,0)·Pivot=(0.5,1.0) 저작 권장.
			if (!Source)
			{
				break;
			}
			FMinimalViewInfo POV;
			if (!W->GetActivePOV(POV))
			{
				break;   // 아직 카메라 없음(초기 프레임)
			}
			FWindowsWindow* Win = GEngine ? GEngine->GetWindow() : nullptr;
			if (!Win)
			{
				break;
			}
			const float Scale = FUICanvasManager::Get().GetGlobalScale();
			if (Scale <= 0.0f)
			{
				break;
			}
			const FVector Head = Source->GetActorLocation() + FVector(0.0f, 0.0f, B.WorldAnchorHeight);
			FVector2 P;
			if (POV.ProjectWorldToScreen(Head, Win->GetWidth(), Win->GetHeight(), P))
			{
				El->SetPosition(FVector2(P.X / Scale, P.Y / Scale));
			}
			else
			{
				El->SetPosition(FVector2(-10000.0f, -10000.0f));   // 카메라 뒤 → 화면 밖으로
			}
			break;
		}
		}
	}
}
