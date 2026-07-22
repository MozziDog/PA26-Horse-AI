#pragma once

#include "Object/FName.h"

namespace HorseBBKeys
{
	// ── Perception → Locomotion ──────────────────────────────

	// 각 ray의 각도(actor forward 기준, deg 단위)
	inline constexpr float ObsFanAngles[5] = { -40.0f, -20.0f, 0.0f, 20.0f, 40.0f };
	inline constexpr int   ObsFanCount = 5;

	// 전방 부채꼴 각 ray의 clearance(장애물까지 거리, m). 미탐지 시에는 센서 Probe Range.
	inline const FName ObsClear[5] =
	{
		FName("Obs.Clear.L2"), FName("Obs.Clear.L1"), FName("Obs.Clear.C"),
		FName("Obs.Clear.R1"), FName("Obs.Clear.R2"),
	};

	// 전방 부채꼴 각 slot 진행지점에 밟고 지나갈 지면이 있는지 여부. false = 낭떠러지 또는 벽 안쪽
	inline const FName ObsGround[5] =
	{
		FName("Obs.Ground.L2"), FName("Obs.Ground.L1"), FName("Obs.Ground.C"),
		FName("Obs.Ground.R1"), FName("Obs.Ground.R2"),
	};

	// 점프 관련
	inline const FName ObsFwdDist  = FName("Obs.FwdDist");   // float, 정면(center) 장애물 거리(m). 미탐지 = Probe Range.
	inline const FName ObsJumpable = FName("Obs.Jumpable");  // bool, 정면 장애물 윗변이 점프 높이 이하 → 넘을 수 있음.

	// 도로 추종 관련
	inline const FName RoadDir     = FName("Road.Dir");      // FVector(world), 추종할 도로 방향. RoadSensor 산출.
	inline const FName RoadDist    = FName("Road.Dist");     // float, 도로 센서와 검출된 지점 간의 거리. 멀수록 도로 추종 약화.

	// 유저 입력
	inline const FName UserMoveDir = FName("User.MoveDir");  // FVector(world), 유저 입력 방향. 크기 = 강도[0~1]

	// ── BT → Locomotion ──────────────────────────────────────
	inline const FName DesiredGait = FName("DesiredGait");   // int (EHorseGait). BT 가 원하는 보법.
}
