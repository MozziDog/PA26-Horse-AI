#include "GameFramework/GameMode/GameModeBase.h"
#include "GameFramework/GameMode/GameStateBase.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/World.h"
#include "Object/Reflection/UClass.h"
#include "Core/Logging/Log.h"
#include "Core/ProjectSettings.h"
#include "Core/ScoreManager.h"
#include "Runtime/Engine.h"               // GEngine, GetGameViewportClient
#include "Viewport/GameViewportClient.h"  // 입력 모드(커서 표시) 결정

AGameModeBase::AGameModeBase()
{
	// 기본값 — 서브클래스 생성자가 더 구체 클래스로 덮어쓸 수 있다.
	GameStateClass = AGameStateBase::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
}

void AGameModeBase::BeginPlay()
{
	AActor::BeginPlay();

	// GameState spawn — World 경유로 등록되어 BeginPlay/Tick에 편입된다.
	if (UWorld* World = GetWorld())
	{
		UClass* StateClass = GameStateClass ? GameStateClass : AGameStateBase::StaticClass();
		AActor* Spawned = World->SpawnActorByClass(StateClass);
		GameState = Cast<AGameStateBase>(Spawned);
	}
}

void AGameModeBase::EndPlay()
{
	GameState = nullptr;
	PlayerController = nullptr;
	AActor::EndPlay();
}

void AGameModeBase::StartMatch()
{
	// 점수 카운터 리셋 — 새 매치 시작 시 발사/히트 0, 기록 플래그 해제.
	FScoreManager::Get().BeginRun();

	// PlayerController spawn — Editor 월드에선 GameMode 자체가 안 만들어지므로 안전.
	if (UWorld* World = GetWorld())
	{
		UClass* PCClass = PlayerControllerClass ? PlayerControllerClass : APlayerController::StaticClass();
		AActor* Spawned = World->SpawnActorByClass(PCClass);
		PlayerController = Cast<APlayerController>(Spawned);
	}

	AutoPossessFirstPawn();

	// 입력 모드 — 조종할 플레이어 폰이 있으면 GameOnly(마우스룩 + 커서 캡처),
	// 없으면(main.Scene 같은 메뉴 씬) UIOnly 로 커서를 보이고 풀어 UI 클릭만 받는다.
	// 매치 시작마다 평가하므로 씬 전환 시에도 자동으로 맞춰진다.
	if (GEngine)
	{
		if (UGameViewportClient* Viewport = GEngine->GetGameViewportClient())
		{
			const bool bHasPlayerPawn = PlayerController && PlayerController->GetPossessedPawn() != nullptr;
			Viewport->SetInputMode(bHasPlayerPawn ? EGameInputMode::GameOnly : EGameInputMode::UIOnly);
		}
	}
}

void AGameModeBase::EndMatch()
{
	if (PlayerController)
	{
		PlayerController->UnPossess();
	}
}

UClass* AGameModeBase::ResolveClassFromProjectSettings(UClass* InDefault)
{
	UClass* Result = InDefault;
	const FString& ConfiguredName = FProjectSettings::Get().Game.GameModeClassName;
	if (ConfiguredName.empty())
	{
		return Result;
	}

	UClass* Found = UClass::FindByName(ConfiguredName.c_str());
	if (Found && Found->IsA(AGameModeBase::StaticClass()))
	{
		return Found;
	}

	UE_LOG("[GameMode] GameModeClassName '%s' not found or not a AGameModeBase subclass — using default %s",
		ConfiguredName.c_str(), Result ? Result->GetName() : "(null)");
	return Result;
}

void AGameModeBase::AutoPossessFirstPawn()
{
	if (!PlayerController) return;

	UWorld* World = GetWorld();
	if (!World) return;

	for (AActor* Actor : World->GetActors())
	{
		if (!Actor) continue;
		APawn* Pawn = Cast<APawn>(Actor);
		if (!Pawn) continue;
		if (!Pawn->GetAutoPossessPlayer()) continue;

		PlayerController->Possess(Pawn);
		UE_LOG("[GameMode] Auto-possessed Pawn: %s", Pawn->GetName().c_str());
		return;
	}

	// 매칭 Pawn 없음 — PC만 살아있고 PossessedPawn은 nullptr.
}
