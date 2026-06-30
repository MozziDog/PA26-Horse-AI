#pragma once

#include "GameFramework/Pawn/Character.h"
#include "Source/Engine/GameFramework/Pawn/BossCharacter.generated.h"

UCLASS()
class ABossCharacter : public ACharacter
{
public:
	GENERATED_BODY()
	ABossCharacter();
	~ABossCharacter() override = default;

protected:
	void Tick(float DeltaTime) override;

private:
	// Score.uasset 결과 캔버스("ScoreBoard" 요소 보유)를 찾아 표시/숨김. 찾아서 적용했으면 true 반환.
	bool SetScoreUIVisible(bool bVisible);
	// 전용 처형 연출 시작(슬로모 등). 구체 연출(카메라/사운드/특수 애님)은 추후 확정.
	void PlayExecutionCinematic();

	// 보스 체력이 0 이 된 프레임에 점수를 1회만 확정/기록하기 위한 런타임 가드.
	bool bScoreRecorded = false;
	// 결과 점수 UI 를 시작 시 1회 숨기기 위한 가드(캔버스 빌드 후 성공 시 set).
	bool bScoreUIInitialized = false;

	// 처형 연출 진행 상태 — 체력 0(=Beam 마무리)이 된 뒤 연출을 먼저 재생하고, 연출 시간이
	// 지난 다음 기존 게임오버 파이프라인(점수/결과 UI/입력 차단)을 1회 발동한다.
	bool  bExecutionStarted = false;
	float ExecutionElapsed = 0.0f;
	// 연출 길이/슬로모 파라미터(게임 시간 기준) — 추후 UPROPERTY 로 승격해 에디터 노출 가능.
	float ExecutionCinematicDuration = 1.5f;
	float ExecutionSlomoDuration = 1.5f;
	float ExecutionSlomoTimeDilation = 0.3f;
};
