#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Singleton.h"

// ============================================================
// FScoreManager — 보스전 점수 집계/기록 (프로세스 전역, per-run 리셋).
//
// 점수 = (보스 유효타격 / 발사체 수) * 10000  → 0~10000 명중률.
//   - BeginRun()      : 매치 시작 시 카운터 리셋        (AGameModeBase::StartMatch)
//   - AddShotFired()  : 발사체가 실제 발사될 때마다     (Spray SpawnProjectile / Bow Launch)
//   - AddBossHit()    : 보스에 유효 데미지가 들어갈 때  (Arrow/Spray ApplyDamageToHitTarget, IsBossActor 가드)
//   - OnBossDefeated(): 보스 처치 시 점수 확정 + Saves/Scores.json 에 1건 append (bRecorded 로 1회만)
//
// 보스 히트/발사 카운트를 모두 보유하므로 점수는 이 클래스만으로 산출된다(UI 도 소스 액터 불필요).
// ============================================================
class FScoreManager : public TSingleton<FScoreManager>
{
	friend class TSingleton<FScoreManager>;

public:
	// --- 집계 ---
	void BeginRun();
	void AddShotFired();
	void AddBossHit();

	// 보스 처치 순간 호출 — 점수 확정 후 JSON 기록. bRecorded 가드로 한 런당 1회만 기록.
	void OnBossDefeated();

	// --- 조회 (UI 등) ---
	int32 GetShotsFired() const { return ShotsFired; }
	int32 GetBossHits() const { return BossHits; }
	int32 GetCurrentScore() const { return ComputeScore(BossHits, ShotsFired); }

	// 점수 공식 — 발사체 0이면 0(0 나누기 방지), 상한 10000 보장.
	static int32 ComputeScore(int32 BossHits, int32 ShotsFired);

	// Saves/Scores.json 을 읽어 점수 내림차순 상위 MaxEntries 개를 멀티라인 문자열로 만든다(점수판 UI용).
	// 파일이 없거나 비면 "No scores yet". (한 줄당 "순위.  점수")
	FString BuildScoreboardText(int32 MaxEntries = 10) const;

private:
	FScoreManager() = default;
	~FScoreManager() = default;

	// Saves/Scores.json 에 한 줄 append (기존 배열 로드 → 레코드 추가 → 재기록).
	void WriteRecord(int32 Score) const;

	int32 ShotsFired = 0;
	int32 BossHits = 0;
	bool bRecorded = false;  // 한 런에서 OnBossDefeated 중복 호출 시 이중 기록 방지
};
