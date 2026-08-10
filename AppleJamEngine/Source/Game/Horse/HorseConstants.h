#pragma once

#include "Object/FName.h"

namespace HorseBBKeys
{
	// ── Perception → Locomotion ──────────────────────────────

	// 각 ray의 각도(actor forward 기준, deg 단위)
	inline constexpr float ObsSlotAngles[5] = { -40.0f, -20.0f, 0.0f, 20.0f, 40.0f };
	inline constexpr int   ObsSlotCount = 5;

	// ExtraSlot은 경로 추종 시의 NavDir가 후방인지를 판정하고 후방인 경우 '유턴' 방향을 고르는 용도
	// 장애물/절벽 등 danger sensor와는 상호작용하지 않음.
	inline constexpr int ExtraSlotCount = 2;
	inline constexpr int SteeringSlotCount = ObsSlotCount + ExtraSlotCount;
	inline constexpr int ExtraSlotLeftIndex = 0;
	inline constexpr int ExtraSlotRightIndex = SteeringSlotCount - 1;
	inline constexpr float SteeringSlotAngles[SteeringSlotCount] =
	{
		-110.0f,	// 좌측 후방 슬롯
		-40.0f, -20.0f, 0.0f, 20.0f, 40.0f,		// 전방 슬롯 (= ObsSlotAngles)
		110.0f,		// 우측 후방 슬롯
	};

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
	inline const FName ObsFwdDist  = FName("Obs.FwdDist");   // float, 정면(center) 장애물 거리(m). 미탐지 = Jump Probe Range.
	inline const FName ObsJumpable = FName("Obs.Jumpable");  // bool, 정면 장애물 윗변이 점프 높이 이하 → 넘을 수 있음.

	// 도로 추종 관련
	inline const FName RoadDir     = FName("Road.Dir");      // FVector(world), 추종할 도로 방향. RoadSensor 산출.
	inline const FName RoadDist    = FName("Road.Dist");     // float, 도로 센서와 검출된 지점 간의 거리. 멀수록 도로 추종 약화.

	// 활성 Guidance producer가 locomotion에 전달하는 '가고 싶은 방향' + 가중치
	inline const FName GuidanceDirection = FName("Guidance.Direction"); // FVector(world), 수평 정규화 방향
	inline const FName GuidanceWeight    = FName("Guidance.Weight");    // float, 0 이하면 guidance 없음

	// 상위 계층(BT task / rider state)에서 하위 계층 locomotion 기능을 제어하기 위한 기능 단위 '정책' 플래그
	// NOTE: 로보틱스 방식의 접근법대로라면 상위 계층에서 하위 계층의 일을 모르는 게 맞지만
	//       Strafe 모드 등 수동과 자동이 혼합된 프로젝트 특성을 고려해서 하위 계층을 플래그로 컨트롤할 수 있게 제공
	inline const FName ControlEnableRoadAssist       = FName("Control.EnableRoadAssist");
	inline const FName ControlIgnoreContextAvoidance = FName("Control.IgnoreContextAvoidance");
	inline const FName ControlEnableAutoJump         = FName("Control.EnableAutoJump");
	inline const FName ControlEnableStrafe           = FName("Control.EnableStrafe");

	// 호출 요청/상태
	inline const FName CallRequested          = FName("Call.Requested");
	inline const FName CallStatus             = FName("Call.Status");

	// ── BT → Locomotion ──────────────────────────────────────
	inline const FName DesiredGait = FName("DesiredGait");   // int (EHorseGait). BT 가 원하는 보법.
}
