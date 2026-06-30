#include "Core/ScoreManager.h"

#include "Platform/Paths.h"
#include "Core/Logging/Log.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

void FScoreManager::BeginRun()
{
	ShotsFired = 0;
	BossHits = 0;
	bRecorded = false;
	UE_LOG("[Score] BeginRun — counters reset");
}

void FScoreManager::AddShotFired()
{
	++ShotsFired;
}

void FScoreManager::AddBossHit()
{
	++BossHits;
}

int32 FScoreManager::ComputeScore(int32 BossHits, int32 ShotsFired)
{
	if (ShotsFired <= 0)
	{
		return 0;
	}
	const double Ratio = static_cast<double>(BossHits) / static_cast<double>(ShotsFired);
	int32 Score = static_cast<int32>(std::llround(Ratio * 10000.0));
	return (std::max)(0, (std::min)(10000, Score));
}

void FScoreManager::OnBossDefeated()
{
	if (bRecorded)
	{
		return;  // 이미 이번 런 점수를 기록함 — 중복 방지
	}
	bRecorded = true;

	const int32 Score = ComputeScore(BossHits, ShotsFired);
	UE_LOG("[Score] Boss defeated — hits=%d shots=%d score=%d", BossHits, ShotsFired, Score);
	WriteRecord(Score);
}

void FScoreManager::WriteRecord(int32 Score) const
{
	using namespace json;

	// 경로: Saves/Scores.json (부모 폴더 보장).
	const std::wstring Dir = FPaths::SaveDir();
	FPaths::CreateDir(Dir);
	const std::filesystem::path FilePath = std::filesystem::path(Dir) / L"Scores.json";

	// 기존 배열 로드 — 파일이 없거나 비었거나 배열이 아니면 새 배열로 시작.
	JSON Root;
	{
		std::ifstream In(FilePath);
		if (In.is_open())
		{
			const std::string Content((std::istreambuf_iterator<char>(In)),
				std::istreambuf_iterator<char>());
			if (!Content.empty())
			{
				Root = JSON::Load(Content);
			}
		}
	}
	if (Root.JSONType() != JSON::Class::Array)
	{
		Root = Array();
	}

	// 레코드 1건 append.
	JSON Rec = Object();
	Rec["score"] = Score;
	Rec["bossHits"] = BossHits;
	Rec["shotsFired"] = ShotsFired;
	Rec["timestamp"] = static_cast<long>(std::time(nullptr));
	Root.append(Rec);

	// 재기록 (전체 배열 덮어쓰기).
	std::ofstream Out(FilePath, std::ios::trunc);
	if (Out.is_open())
	{
		Out << Root;
		UE_LOG("[Score] Record written to %s (total records=%d)",
			FPaths::ToUtf8(FilePath.wstring()).c_str(), Root.size());
	}
	else
	{
		UE_LOG("[Score] FAILED to open Scores.json for write");
	}
}

FString FScoreManager::BuildScoreboardText(int32 MaxEntries) const
{
	using namespace json;

	const std::wstring Dir = FPaths::SaveDir();
	const std::filesystem::path FilePath = std::filesystem::path(Dir) / L"Scores.json";

	std::ifstream In(FilePath);
	if (!In.is_open())
	{
		return FString("No scores yet");
	}
	const std::string Content((std::istreambuf_iterator<char>(In)),
		std::istreambuf_iterator<char>());
	if (Content.empty())
	{
		return FString("No scores yet");
	}

	JSON Root = JSON::Load(Content);
	if (Root.JSONType() != JSON::Class::Array)
	{
		return FString("No scores yet");
	}

	std::vector<long> Scores;
	for (auto& Rec : Root.ArrayRange())
	{
		if (Rec.hasKey("score"))
		{
			Scores.push_back(Rec["score"].ToInt());
		}
	}
	if (Scores.empty())
	{
		return FString("No scores yet");
	}

	std::sort(Scores.begin(), Scores.end(), std::greater<long>());

	const int32 Count = (std::min)(MaxEntries, static_cast<int32>(Scores.size()));
	std::string Out;
	char Buf[64] = {};
	for (int32 i = 0; i < Count; ++i)
	{
		snprintf(Buf, sizeof(Buf), "%d.  %ld", i + 1, Scores[static_cast<size_t>(i)]);
		Out += Buf;
		if (i < Count - 1)
		{
			Out += "\n";
		}
	}
	return FString(Out);
}
