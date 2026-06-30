#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Singleton.h"
#include "Platform/Paths.h"

/*
	FProjectSettings — 프로젝트 전역 설정 (per-viewport가 아닌 전체 공유).
	Settings/ProjectSettings.ini에 독립 직렬화됩니다.
*/

class FProjectSettings : public TSingleton<FProjectSettings>
{
	friend class TSingleton<FProjectSettings>;

	// --- Shadow ---
	struct FShadowOption
	{
		bool bEnabled = true;
		uint32 CSMResolution       = 2048;	// Directional Light CSM cascade 해상도
		uint32 SpotAtlasResolution = 4096;	// Spot Light Atlas page 해상도
		uint32 PointAtlasResolution = 4096;	// Point Light Atlas page 해상도
		uint32 MaxSpotAtlasPages   = 4;		// Spot Light Atlas 최대 page 수
		uint32 MaxPointAtlasPages  = 4;		// Point Light Atlas 최대 page 수
	};

	// --- Physics ---
	struct FPhysicsOption
	{

        // Fixed-step simulation. 렌더 FrameDt를 그대로 PhysX에 넣지 않고 이 값으로만 진행한다.
        // FixedTimeStep is the target physics publish/catch-up step.
        // MaxSimulationSubstepDeltaTime caps the actual PxScene::simulate(dt) slice used inside that step.
        // Keep this at 1/60 or lower when the exposed physics Hz is reduced for stability.
        float FixedTimeStep                 = 1.0f / 60.0f;
        float MaxSimulationSubstepDeltaTime = 1.0f / 60.0f;
        float MaxFrameDeltaTime             = 0.1f;
        int32 MaxSubsteps                   = 4;

        // PhysX scene/worker 설정. 0 이하면 안전한 기본값을 고른다.
        int32 WorkerThreadCount          = 2;
        bool  bEnableCCD                 = true;
        bool  bEnablePCM                 = true;
        bool  bEnableActiveActors        = true;
        bool  bRequireSceneReadWriteLock = true;
        bool  bAsyncPhysics              = false;

        // Debug/이벤트 publish 제어.
        bool bDispatchCollisionEvents = true;
        bool bDispatchTriggerEvents   = true;
        bool bBuildDebugSnapshot      = true;
	};

	// --- Game ---
	struct FGameOption
	{
		FString StartLevelName;     // Scene 파일 이름 (확장자 제외)
		FString GameModeClassName;  // ""면 GameEngine이 코드로 지정한 디폴트 사용.
		                            // 잘못된 이름이거나 AGameModeBase 파생이 아니면 디폴트 fallback.
	};

	// --- UI ---
	struct FUIOption
	{
		// 신규 계층형 UI 의 레퍼런스 해상도(16:9 고정). GlobalScale = ClientHeight / RefResY.
		float RefResX = 1920.0f;
		float RefResY = 1080.0f;
	};

	// --- Post Process ---
	struct FPostProcessOption
	{
		bool bBloom = false;
		float BloomThreshold = 1.0f;
		float BloomSoftKnee = 0.5f;
		float BloomIntensity = 0.6f;
		float BloomBlurRadius = 1.0f;

		bool bGammaCorrection = true;
		float Exposure = 1.0f;
		float Gamma = 2.4f;
	};

public:
	FShadowOption Shadow;
	FPhysicsOption Physics;
	FGameOption Game;
	FUIOption UI;
	FPostProcessOption PostProcess;

	// --- 직렬화 ---
	void SaveToFile(const FString& Path) const;
	void LoadFromFile(const FString& Path);

	static FString GetDefaultPath() { return FPaths::ToUtf8(FPaths::ProjectSettingsFilePath()); }
};
