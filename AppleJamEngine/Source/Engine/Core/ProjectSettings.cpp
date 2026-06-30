#include "Core/ProjectSettings.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <cmath>

namespace PSKey
{
	constexpr const char* Shadow = "Shadow";
	constexpr const char* bShadows = "bShadows";
	constexpr const char* CSMResolution = "CSMResolution";
	constexpr const char* SpotAtlasResolution = "SpotAtlasResolution";
	constexpr const char* PointAtlasResolution = "PointAtlasResolution";
	constexpr const char* MaxSpotAtlasPages = "MaxSpotAtlasPages";
	constexpr const char* MaxPointAtlasPages = "MaxPointAtlasPages";

	constexpr const char* PhysicsSection             = "Physics";
    constexpr const char* FixedTimeStep              = "FixedTimeStep";
    constexpr const char* MaxSimulationSubstepDeltaTime = "MaxSimulationSubstepDeltaTime";
    constexpr const char* MaxFrameDeltaTime          = "MaxFrameDeltaTime";
    constexpr const char* MaxSubsteps                = "MaxSubsteps";
    constexpr const char* WorkerThreadCount          = "WorkerThreadCount";
    constexpr const char* bEnableCCD                 = "bEnableCCD";
    constexpr const char* bEnablePCM                 = "bEnablePCM";
    constexpr const char* bEnableActiveActors        = "bEnableActiveActors";
    constexpr const char* bRequireSceneReadWriteLock = "bRequireSceneReadWriteLock";
    constexpr const char* bAsyncPhysics              = "bAsyncPhysics";
    constexpr const char* bDispatchCollisionEvents   = "bDispatchCollisionEvents";
    constexpr const char* bDispatchTriggerEvents     = "bDispatchTriggerEvents";
    constexpr const char* bBuildDebugSnapshot        = "bBuildDebugSnapshot";

	constexpr const char* GameSection = "Game";
	constexpr const char* StartLevelName = "StartLevelName";
	constexpr const char* GameModeClassName = "GameModeClassName";

	constexpr const char* UISection = "UI";
	constexpr const char* RefResX = "RefResX";
	constexpr const char* RefResY = "RefResY";

	constexpr const char* PostProcessSection = "PostProcess";
	constexpr const char* bBloom = "bBloom";
	constexpr const char* BloomThreshold = "BloomThreshold";
	constexpr const char* BloomSoftKnee = "BloomSoftKnee";
	constexpr const char* BloomIntensity = "BloomIntensity";
	constexpr const char* BloomBlurRadius = "BloomBlurRadius";
	constexpr const char* bGammaCorrection = "bGammaCorrection";
	constexpr const char* Exposure = "Exposure";
	constexpr const char* Gamma = "Gamma";
}

void FProjectSettings::SaveToFile(const FString& Path) const
{
	using namespace json;

	JSON Root = Object();

	JSON ShadowObj = Object();
	ShadowObj[PSKey::bShadows] = Shadow.bEnabled;
	ShadowObj[PSKey::CSMResolution] = static_cast<int>(Shadow.CSMResolution);
	ShadowObj[PSKey::SpotAtlasResolution] = static_cast<int>(Shadow.SpotAtlasResolution);
	ShadowObj[PSKey::PointAtlasResolution] = static_cast<int>(Shadow.PointAtlasResolution);
	ShadowObj[PSKey::MaxSpotAtlasPages] = static_cast<int>(Shadow.MaxSpotAtlasPages);
	ShadowObj[PSKey::MaxPointAtlasPages] = static_cast<int>(Shadow.MaxPointAtlasPages);
	Root[PSKey::Shadow] = ShadowObj;

	JSON PhysObj                               = Object();
    PhysObj[PSKey::FixedTimeStep]              = Physics.FixedTimeStep;
    PhysObj[PSKey::MaxSimulationSubstepDeltaTime] = Physics.MaxSimulationSubstepDeltaTime;
    PhysObj[PSKey::MaxFrameDeltaTime]          = Physics.MaxFrameDeltaTime;
    PhysObj[PSKey::MaxSubsteps]                = Physics.MaxSubsteps;
    PhysObj[PSKey::WorkerThreadCount]          = Physics.WorkerThreadCount;
    PhysObj[PSKey::bEnableCCD]                 = Physics.bEnableCCD;
    PhysObj[PSKey::bEnablePCM]                 = Physics.bEnablePCM;
    PhysObj[PSKey::bEnableActiveActors]        = Physics.bEnableActiveActors;
    PhysObj[PSKey::bRequireSceneReadWriteLock] = Physics.bRequireSceneReadWriteLock;
    PhysObj[PSKey::bAsyncPhysics]              = Physics.bAsyncPhysics;
    PhysObj[PSKey::bDispatchCollisionEvents]   = Physics.bDispatchCollisionEvents;
    PhysObj[PSKey::bDispatchTriggerEvents]     = Physics.bDispatchTriggerEvents;
    PhysObj[PSKey::bBuildDebugSnapshot]        = Physics.bBuildDebugSnapshot;
	Root[PSKey::PhysicsSection]                = PhysObj;

	JSON GameObj = Object();
	GameObj[PSKey::StartLevelName] = Game.StartLevelName;
	GameObj[PSKey::GameModeClassName] = Game.GameModeClassName;
	Root[PSKey::GameSection] = GameObj;

	JSON UIObj = Object();
	UIObj[PSKey::RefResX] = UI.RefResX;
	UIObj[PSKey::RefResY] = UI.RefResY;
	Root[PSKey::UISection] = UIObj;

	JSON PostProcessObj = Object();
	PostProcessObj[PSKey::bBloom] = PostProcess.bBloom;
	PostProcessObj[PSKey::BloomThreshold] = PostProcess.BloomThreshold;
	PostProcessObj[PSKey::BloomSoftKnee] = PostProcess.BloomSoftKnee;
	PostProcessObj[PSKey::BloomIntensity] = PostProcess.BloomIntensity;
	PostProcessObj[PSKey::BloomBlurRadius] = PostProcess.BloomBlurRadius;
	PostProcessObj[PSKey::bGammaCorrection] = PostProcess.bGammaCorrection;
	PostProcessObj[PSKey::Exposure] = PostProcess.Exposure;
	PostProcessObj[PSKey::Gamma] = PostProcess.Gamma;
	Root[PSKey::PostProcessSection] = PostProcessObj;

	std::filesystem::path FilePath(FPaths::ToWide(Path));
	if (FilePath.has_parent_path())
		std::filesystem::create_directories(FilePath.parent_path());

	std::ofstream File(FilePath);
	if (File.is_open())
		File << Root;
}

void FProjectSettings::LoadFromFile(const FString& Path)
{
	using namespace json;

	std::ifstream File(std::filesystem::path(FPaths::ToWide(Path)));
	if (!File.is_open())
		return;

	FString Content((std::istreambuf_iterator<char>(File)),
		std::istreambuf_iterator<char>());

	JSON Root = JSON::Load(Content);

	if (Root.hasKey(PSKey::PhysicsSection))
	{
		JSON P = Root[PSKey::PhysicsSection];
        if (P.hasKey(PSKey::FixedTimeStep))
        {
            float v               = static_cast<float>(P[PSKey::FixedTimeStep].ToFloat());
            Physics.FixedTimeStep = (std::max)(1.0f / 240.0f, (std::min)(v, 1.0f / 15.0f));
        }
        if (P.hasKey(PSKey::MaxSimulationSubstepDeltaTime))
        {
            float v = static_cast<float>(P[PSKey::MaxSimulationSubstepDeltaTime].ToFloat());
            Physics.MaxSimulationSubstepDeltaTime = (std::max)(1.0f / 240.0f, (std::min)(v, Physics.FixedTimeStep));
        }
        else
        {
            Physics.MaxSimulationSubstepDeltaTime = (std::min)(Physics.MaxSimulationSubstepDeltaTime, Physics.FixedTimeStep);
        }
        if (P.hasKey(PSKey::MaxFrameDeltaTime))
        {
            float v                   = static_cast<float>(P[PSKey::MaxFrameDeltaTime].ToFloat());
            Physics.MaxFrameDeltaTime = (std::max)(Physics.FixedTimeStep, (std::min)(v, 1.0f));
        }
        if (P.hasKey(PSKey::MaxSubsteps))
        {
            int v               = P[PSKey::MaxSubsteps].ToInt();
            Physics.MaxSubsteps = (std::max)(1, (std::min)(v, 32));
        }
        if (P.hasKey(PSKey::WorkerThreadCount))
        {
            int v                     = P[PSKey::WorkerThreadCount].ToInt();
            Physics.WorkerThreadCount = (std::max)(0, (std::min)(v, 32));
        }
        if (P.hasKey(PSKey::bEnableCCD)) Physics.bEnableCCD = P[PSKey::bEnableCCD].ToBool();
        if (P.hasKey(PSKey::bEnablePCM)) Physics.bEnablePCM = P[PSKey::bEnablePCM].ToBool();
        if (P.hasKey(PSKey::bEnableActiveActors)) Physics.bEnableActiveActors = P[PSKey::bEnableActiveActors].ToBool();
        if (P.hasKey(PSKey::bRequireSceneReadWriteLock)) Physics.bRequireSceneReadWriteLock = P[PSKey::bRequireSceneReadWriteLock].ToBool();
        if (P.hasKey(PSKey::bAsyncPhysics)) Physics.bAsyncPhysics = P[PSKey::bAsyncPhysics].ToBool();
        if (P.hasKey(PSKey::bDispatchCollisionEvents)) Physics.bDispatchCollisionEvents = P[PSKey::bDispatchCollisionEvents].ToBool();
        if (P.hasKey(PSKey::bDispatchTriggerEvents)) Physics.bDispatchTriggerEvents = P[PSKey::bDispatchTriggerEvents].ToBool();
        if (P.hasKey(PSKey::bBuildDebugSnapshot)) Physics.bBuildDebugSnapshot = P[PSKey::bBuildDebugSnapshot].ToBool();

        const int RequiredSubsteps = (Physics.MaxSimulationSubstepDeltaTime > 0.0f)
            ? static_cast<int>(std::ceil(Physics.FixedTimeStep / Physics.MaxSimulationSubstepDeltaTime - 1.e-6f))
            : 1;
        Physics.MaxSubsteps = (std::max)(Physics.MaxSubsteps, (std::max)(1, RequiredSubsteps));
	}

	if (Root.hasKey(PSKey::GameSection))
	{
		JSON G = Root[PSKey::GameSection];
		if (G.hasKey(PSKey::StartLevelName))
			Game.StartLevelName = G[PSKey::StartLevelName].ToString();
		if (G.hasKey(PSKey::GameModeClassName))
			Game.GameModeClassName = G[PSKey::GameModeClassName].ToString();
	}

	if (Root.hasKey(PSKey::UISection))
	{
		JSON U = Root[PSKey::UISection];
		if (U.hasKey(PSKey::RefResX))
		{
			float v = static_cast<float>(U[PSKey::RefResX].ToFloat());
			UI.RefResX = (v > 1.0f) ? v : 1920.0f;
		}
		if (U.hasKey(PSKey::RefResY))
		{
			float v = static_cast<float>(U[PSKey::RefResY].ToFloat());
			UI.RefResY = (v > 1.0f) ? v : 1080.0f;   // 0 분모 방지
		}
	}

	if (Root.hasKey(PSKey::PostProcessSection))
	{
		JSON P = Root[PSKey::PostProcessSection];
		if (P.hasKey(PSKey::bBloom))
			PostProcess.bBloom = P[PSKey::bBloom].ToBool();
		if (P.hasKey(PSKey::BloomThreshold))
			PostProcess.BloomThreshold = static_cast<float>(P[PSKey::BloomThreshold].ToFloat());
		if (P.hasKey(PSKey::BloomSoftKnee))
			PostProcess.BloomSoftKnee = static_cast<float>(P[PSKey::BloomSoftKnee].ToFloat());
		if (P.hasKey(PSKey::BloomIntensity))
			PostProcess.BloomIntensity = static_cast<float>(P[PSKey::BloomIntensity].ToFloat());
		if (P.hasKey(PSKey::BloomBlurRadius))
			PostProcess.BloomBlurRadius = static_cast<float>(P[PSKey::BloomBlurRadius].ToFloat());
		if (P.hasKey(PSKey::bGammaCorrection))
			PostProcess.bGammaCorrection = P[PSKey::bGammaCorrection].ToBool();
		if (P.hasKey(PSKey::Exposure))
			PostProcess.Exposure = static_cast<float>(P[PSKey::Exposure].ToFloat());
		if (P.hasKey(PSKey::Gamma))
			PostProcess.Gamma = static_cast<float>(P[PSKey::Gamma].ToFloat());
	}

	if (Root.hasKey(PSKey::Shadow))
	{
		JSON S = Root[PSKey::Shadow];
		if (S.hasKey(PSKey::bShadows))
			Shadow.bEnabled = S[PSKey::bShadows].ToBool();
		if (S.hasKey(PSKey::CSMResolution))
		{
			int v = S[PSKey::CSMResolution].ToInt();
			Shadow.CSMResolution = static_cast<uint32>((std::max)(64, (std::min)(v, 8192)));
		}
		if (S.hasKey(PSKey::SpotAtlasResolution))
		{
			int v = S[PSKey::SpotAtlasResolution].ToInt();
			Shadow.SpotAtlasResolution = static_cast<uint32>((std::max)(64, (std::min)(v, 8192)));
		}
		if (S.hasKey(PSKey::PointAtlasResolution))
		{
			int v = S[PSKey::PointAtlasResolution].ToInt();
			Shadow.PointAtlasResolution = static_cast<uint32>((std::max)(64, (std::min)(v, 8192)));
		}
		if (S.hasKey(PSKey::MaxSpotAtlasPages))
		{
			int v = S[PSKey::MaxSpotAtlasPages].ToInt();
			Shadow.MaxSpotAtlasPages = static_cast<uint32>(v > 1 ? v : 1);
		}
		if (S.hasKey(PSKey::MaxPointAtlasPages))
		{
			int v = S[PSKey::MaxPointAtlasPages].ToInt();
			Shadow.MaxPointAtlasPages = static_cast<uint32>(v > 1 ? v : 1);
		}
	}
}
