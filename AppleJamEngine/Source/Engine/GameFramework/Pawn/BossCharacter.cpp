#include "GameFramework/Pawn/BossCharacter.h"

#include "Animation/AnimationManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/Montage/AnimMontage.h"
#include "Component/Input/ActionComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/Logging/Log.h"
#include "Core/ScoreManager.h"
#include "GameFramework/World.h"
#include "UI/Canvas/UICanvasActor.h"
#include "UI/Canvas/UICanvas.h"
#include "UI/Canvas/UIElement.h"
#include "Runtime/Engine.h"
#include "Viewport/GameViewportClient.h"

ABossCharacter::ABossCharacter()
{
	bAutoInputWASD = false;
	bAutoInputMouseLook = false;
}

void ABossCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	
	// 결과 점수 UI 는 평소 숨김 — 첫 틱(모든 BeginPlay 이후, 캔버스 빌드 완료)에 찾아서 1회 숨긴다.
	// 아직 못 찾으면 다음 틱에 재시도.
	if (!bScoreUIInitialized)
	{
		if (SetScoreUIVisible(false))
		{
			bScoreUIInitialized = true;
		}
	}

	// 처형 감지(폴링) — 데미지 게이트상 체력 0 은 Beam(궁극기) 마무리로만 도달한다(일반 공격은 1에서 막힘).
	// 사망 이벤트가 없어 폴링하며, 체력 0 이 된 첫 프레임에 즉시 게임오버 대신 전용 처형 연출을 먼저 시작한다.
	if (!bExecutionStarted && GetCurrentHealth() <= 0.0f)
	{
		bExecutionStarted = true;
		ExecutionElapsed = 0.0f;
		PlayExecutionCinematic();
	}

	// 처형 연출 시간이 지나면 기존 게임오버 파이프라인을 1회 발동(OnBossDefeated 가 중복 방지 보장).
	if (bExecutionStarted && !bScoreRecorded)
	{
		ExecutionElapsed += DeltaTime;
		if (ExecutionElapsed >= ExecutionCinematicDuration)
		{
			bScoreRecorded = true;
			FScoreManager::Get().OnBossDefeated();
			SetScoreUIVisible(true);

			// 결과화면 입력 모드 — 마우스를 카메라(마우스룩)에서 분리하고 커서를 풀어 UI 클릭 가능하게.
			// UIOnly: 게임 입력 스냅샷을 만들지 않아 마우스룩/이동이 멈추고 커서 캡처도 풀리며,
			// UI 런타임 클릭(TickRuntimeInput)은 계속 처리된다(PIE/standalone 동일).
			if (GEngine)
			{
				if (UGameViewportClient* Viewport = GEngine->GetGameViewportClient())
				{
					Viewport->SetInputMode(EGameInputMode::UIOnly);
				}
			}
		}
	}
}

void ABossCharacter::PlayExecutionCinematic()
{
	// 전용 처형 연출 — 구체 수치/카메라/전용 사운드/특수 애님은 추후 확정.
	// 우선 검증된 ActionComponent 슬로모만 적용한다(사망 애님은 BP 가 체력 1에서 bDead 로 재생 중).
	if (UActionComponent* Action = GetComponentByClass<UActionComponent>())
	{
		Action->Slomo(ExecutionSlomoDuration, ExecutionSlomoTimeDilation);
	}
	UE_LOG("[BossExecution] finisher cinematic started — gameover in %.2fs", ExecutionCinematicDuration);
	// TODO(처형 연출): 카메라 연출/전용 사운드/특수 애님을 확정 후 추가.
}

namespace
{
	// 요소와 모든 하위 요소의 bVisible 를 일괄 설정. IsEffectivelyVisible 는 "자신 + 모든 조상"의
	// bVisible 를 보므로, 결과화면을 켜려면 루트뿐 아니라 하위 요소(ScoreBoard/ReturnTitle 등 —
	// 저작상 bVisible=false)의 bVisible 까지 직접 켜야 한다. (루트만 토글해 자식이 계속 숨겨지던 버그 수정.)
	void SetUISubtreeVisible(UUIElement* Element, bool bVisible)
	{
		if (!Element)
		{
			return;
		}
		Element->SetVisible(bVisible);
		for (USceneComponent* Child : Element->GetChildren())
		{
			if (UUIElement* UIChild = Cast<UUIElement>(Child))
			{
				SetUISubtreeVisible(UIChild, bVisible);
			}
		}
	}
}

// Score.uasset 캔버스 식별: "ScoreBoard" 요소를 가진 AUICanvasActor. 찾으면 캔버스 + 하위 요소
// 전체의 bVisible 를 토글하고 true 반환.
bool ABossCharacter::SetScoreUIVisible(bool bVisible)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	for (AActor* Actor : World->GetActors())
	{
		AUICanvasActor* CanvasActor = Cast<AUICanvasActor>(Actor);
		if (!CanvasActor)
		{
			continue;
		}
		UUICanvas* Canvas = CanvasActor->GetCanvas();
		if (!Canvas)
		{
			continue;
		}
		if (Canvas->FindByName(FString("ScoreBoard")))
		{
			SetUISubtreeVisible(Canvas, bVisible);
			return true;
		}
	}
	return false;
}