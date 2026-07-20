#pragma once

#include "Core/Types/CoreTypes.h"
#include "Object/FName.h"

class FArchive;
class UAnimSequenceBase;

// AnimGraph 자산의 정적 데이터 모델 — 런타임 FAnimNode_* 트리와는 분리.
// 컴파일 단계에서 이 그래프를 위상정렬 → MakeNode<T> → SetRootNode 트리를 build.

enum class EAnimGraphPinKind : uint8
{
	Input,
	Output
};

// 단계 1 은 Pose 만 실질 사용. Float/Bool/Int/Name 은 후속 VariableGet 노드 대비 미리 정의.
enum class EAnimGraphPinType : uint8
{
	Pose,
	Float,
	Bool,
	Int,
	Name
};

// FAnimNode_* 와 1:1 매핑되는 enum.
enum class EAnimGraphNodeType : uint8
{
	OutputPose,           // FAnimNode_Root 와 매핑 — 그래프 종착점
	SequencePlayer,
	StateMachine,
	Slot,
	LayeredBlendPerBone,
	BlendListByEnum,
	VariableGet,          // UAnimInstance UPROPERTY 참조
	RefPose,              // FAnimNode_RefPose — mesh ref pose 출력. 보통 Slot/LayeredBlend 의 fallback 입력으로.
	BlendSpace,           // FAnimNode_BlendSpace — 2D(및 1D 퇴화) 산점 샘플 연속 블렌드. 노드 내장 샘플 리스트.
};


// AnimGraph-owned variable declaration. UE Anim Blueprint 의 "My Blueprint > Variables"에 해당한다.
// VariableGet / Transition Rule Graph 는 먼저 이 선언을 찾고, 없으면 OwnerClass UPROPERTY 로 fallback 한다.
struct FAnimGraphVariable
{
	FName             VariableName;
	EAnimGraphPinType Type         = EAnimGraphPinType::Float; // Float / Bool / Int 지원.
	float             DefaultValue = 0.0f;                     // Bool 은 0/1, Int 는 float 저장 후 int cast.
	FString           Category;

	friend FArchive& operator<<(FArchive& Ar, FAnimGraphVariable& Var);
};

// UAnimGraphInstance 의 per-instance runtime 변수 값. Asset DefaultValue 에서 초기화되고,
// 게임 코드 / Lua / preview UI 가 SetGraphVariable* 로 매 frame 갱신한다.
struct FAnimGraphRuntimeVariable
{
	FName             VariableName;
	EAnimGraphPinType Type  = EAnimGraphPinType::Float;
	float             Value = 0.0f;
};

struct FAnimGraphPin
{
	// 같은 자산 안에서 Node/Pin/Link 가 같은 id 공간을 공유 (imgui-node-editor 가
	// link 양 끝의 pin id 를 동일 namespace 로 식별하기 위함). 0 == invalid.
	uint32             PinId        = 0;
	uint32             OwningNodeId = 0;
	EAnimGraphPinKind  Kind         = EAnimGraphPinKind::Input;
	EAnimGraphPinType  Type         = EAnimGraphPinType::Pose;
	FName              DisplayName;

	friend FArchive& operator<<(FArchive& Ar, FAnimGraphPin& Pin);
};

struct FAnimGraphLink
{
	uint32 LinkId    = 0;
	uint32 FromPinId = 0; // Output 쪽 핀
	uint32 ToPinId   = 0; // Input 쪽 핀

	friend FArchive& operator<<(FArchive& Ar, FAnimGraphLink& Link);
};

// ── StateMachine 노드 보조 자료구조 ──
// 평면 그래프에 별도 노드로 표현하지 않고 StateMachine 노드 안에 nested 보유.
// 후속 단계에서 sub-graph view (UE 더블클릭 진입) 도입 시 동일 자료가 재사용됨.

enum class ETransitionOp : uint8
{
	Greater,       // var >  threshold
	GreaterEqual,  // var >= threshold
	Less,          // var <  threshold
	LessEqual,     // var <= threshold
	Equal,         // |var - threshold| < eps
	NotEqual
};

// State Machine transition rule preset. UE 의 Transition Rule Graph 전체를 그대로 복제하기 전 단계로,
// 실제 런타임 평가가 가능한 핵심 rule node 들을 데이터화한다.
// FloatCompare 를 0 으로 유지해 기존 저장 데이터(VariableName/Op/Threshold 기반)와 호환.
enum class ETransitionRuleKind : uint8
{
	FloatCompare,          // Property Access float/int/bool -> Var Op Threshold
	BoolProperty,          // Property Access bool-like -> expected true/false (Threshold >= 0.5 == true)
	TimeRemaining,         // Current state time remaining seconds <= Threshold
	TimeRemainingRatio,    // Current state time remaining ratio <= Threshold
	TimeElapsed,           // Current state elapsed seconds >= Threshold
	AutomaticSequenceEnd,  // Non-looping sequence reached the end
	AlwaysTrue,            // Explicit unconditional rule
	AlwaysFalse            // Explicit disabled rule
};

struct FAnimGraphState
{
	FName    StateName;
	FString  SequencePath; // 이 state 가 재생할 sequence (디스크 path). LoadAnimation 으로 해상.
	float    PlayRate    = 1.0f;
	bool     bLooping    = true;

	// Sub-state-machine — 그래프 안의 다른 StateMachine 노드를 가리킴. 0 == 없음 (일반 sequence state).
	// 컴파일러가 그 노드를 컴파일해 UAnimState::SubGraphOverride 에 박음 → state Enter 시 sub-tree
	// OnBecomeRelevant. UE 의 nested state machine 동등.
	// SubGraphNodeId 가 있으면 SequencePath 는 무시.
	uint32   SubGraphNodeId = 0;

	// StateMachine 내부 에디터에서의 이 state 노드 캔버스 위치. bEditorPosValid 가 false 면 첫 오픈
	// 시 grid 로 자동 배치되고 true 로 승격된다. 이후 드래그 위치가 자산에 저장돼 재오픈/다른
	// StateMachine 전환 후 복귀에도 레이아웃이 유지된다.
	float    EditorPosX      = 0.0f;
	float    EditorPosY      = 0.0f;
	bool     bEditorPosValid = false;

	friend FArchive& operator<<(FArchive& Ar, FAnimGraphState& State);
};

// 단일 leaf node 전환 규칙. 
// RuleKind가 property 기반이 아닐 경우 변수/비교연산/임계값 등은 무시함.
// FAnimGraphTransition::Rules 에 여러 개가 담겨 AND 조건으로 결합
// 필드 순서/타입은 구버전(단일 규칙) FAnimGraphTransition 과 동일하게 유지
struct FAnimGraphTransitionRule
{
	ETransitionRuleKind   RuleKind      = ETransitionRuleKind::FloatCompare;
	FName                 VariableName;  // FloatCompare/BoolProperty 만 사용 (OwnerClass UPROPERTY 또는 AnimGraph 변수)
	ETransitionOp         Op            = ETransitionOp::Greater; // FloatCompare 만 사용
	float                 Threshold     = 0.0f;                   // Bool 은 >=0.5 == true

	friend FArchive& operator<<(FArchive& Ar, FAnimGraphTransitionRule& R);
};

struct FAnimGraphTransition
{
	FName                 FromStateName; // FName::None == AnyState
	FName                 ToStateName;
	float                 BlendTime     = 0.2f;

	// AND 로 결합되는 규칙들. 모두 true 여야 전환한다. 비어 있으면 전환하지 않음(AlwaysFalse 동등).
	// float param 범위 이내(예: Speed>a AND Speed<b)를 여기서 표현한다. 범위 바깥(OR)은 후속
	// Unity 식 중복 transition / condition graph 로 처리하며, 그때 각 rule 이 AND 항으로 재사용된다.
	TArray<FAnimGraphTransitionRule> Rules;

	friend FArchive& operator<<(FArchive& Ar, FAnimGraphTransition& T);
};

// ── BlendSpace 노드 보조 자료구조 ──
// 독립 asset(UBlendSpace) 대신 노드에 산점 샘플을 내장(설계 §2-1). 각 샘플은 하나의 시퀀스와
// 그 2D 좌표(PosX/PosY). 축 의미는 노드가 고정하지 않음 — 범용 (AxisX,AxisY). 좌표 공간 범위는
// FAnimGraphNode 의 AxisMin/Max 필드가 정의(에디터 캔버스/정규화 참고용).
struct FBlendSample
{
	FString SequencePath;      // 디스크 path. 컴파일러가 LoadAnimation 으로 내부 SequencePlayer 에 해상.
	float   PosX     = 0.0f;   // AxisX 좌표.
	float   PosY     = 0.0f;   // AxisY 좌표.
	float   PlayRate = 1.0f;   // 샘플별 재생속도(보법 내 길이 정렬은 전제, 미세 조정용).

	friend FArchive& operator<<(FArchive& Ar, FBlendSample& Sample);
};

struct FAnimGraphNode
{
	uint32                 NodeId = 0;
	EAnimGraphNodeType     Type   = EAnimGraphNodeType::OutputPose;
	FName                  DisplayName;
	float                  PosX   = 0.0f;
	float                  PosY   = 0.0f;
	TArray<FAnimGraphPin>  Pins;

	// SequencePlayer 노드의 입력 시퀀스 — 컴파일러가 FAnimNode_SequencePlayer::Sequence 로 박음.
	// 다른 노드 타입에선 미사용. raw pointer + transient — 자산은 SequencePath 만 보유.
	UAnimSequenceBase*     SequenceRef = nullptr;

	// 직렬화 가능한 sequence 식별자. UAnimGraphInstance::NativeInitializeAnimation 가
	// FAnimationManager::LoadAnimation 으로 해상해 SequenceRef 에 박는다.
	// empty / "None" 이면 UAnimGraphInstance::DefaultSequencePath 가 fallback.
	FString                SequencePath;

	// SequencePlayer 옵션. PlayRate / bLooping — 노드 inspector 에서 편집.
	float                  PlayRate    = 1.0f;
	bool                   bLooping    = true;

	// Slot 노드의 montage slot name (비어있으면 컴파일러가 UAnimInstance::DefaultMontageSlot 으로 fallback).
	FName                  SlotName;

	// LayeredBlendPerBone 의 BlendPose 전체 contribution.
	float                  BlendWeight = 1.0f;

	// LayeredBlendPerBone 의 부분 mask root. 비어있으면 모든 본 true (full blend).
	// 컴파일러가 BuildBoneMaskFromRoot 로 이 본 + 자손 트리만 BlendPose 적용 — UpperBody 데모
	// 의 "Bip001 Spine" 같은 본 이름. 본 못 찾으면 mask 전부 false (= BlendPose 무효 → base 100%).
	FString                RootBoneName;

	// VariableGet 노드 — UAnimInstance 자식 클래스의 어떤 UPROPERTY 를 매 frame 읽을지.
	// inspector 에서 asset 의 OwnerClassName 기반 dropdown 으로 선택.
	// 컴파일러는 이 노드를 별도 런타임 노드로 만들지 않고, consumer 노드 (BlendListByEnum 등) 의
	// 람다로 inline — 그래프 시각화 ↔ 런타임 트리 디커플.
	FName                  VariableName;

	// StateMachine 노드 — states / transitions / initial state 를 nested 보유.
	// 평면 그래프에선 state 별 노드 표현 없음 (inspector form 에서 정의). 후속에서 sub-graph
	// 더블클릭 진입 시 동일 자료가 재사용됨.
	TArray<FAnimGraphState>      States;
	TArray<FAnimGraphTransition> Transitions;
	FName                        InitialStateName;

	// StateMachine 내부 에디터의 Entry / Any State pseudo 노드 위치(캔버스 좌표). 개별 State 노드
	// 위치는 각 FAnimGraphState.EditorPos* 에 저장한다. bStateMachineEditorPosValid 가 false 면 첫
	// 오픈 시 기본 위치로 초기화 후 true 로 승격 — 이후 드래그 위치가 자산에 저장돼 유지된다.
	float                        EntryPosX    = -360.0f;
	float                        EntryPosY    =  -55.0f;
	float                        AnyStatePosX = -360.0f;
	float                        AnyStatePosY =  115.0f;
	bool                         bStateMachineEditorPosValid = false;

	// BlendSpace 노드 — 내장 산점 샘플과 축 좌표 범위. 다른 노드 타입에선 미사용.
	// 컴파일러가 각 샘플을 내부 FAnimNode_SequencePlayer 로, 좌표를 삼각망 입력으로 주입.
	// X/Y Float 입력 핀(축값)은 그래프 링크(VariableGet → Float Variable)로 자립 해석(설계 §2-6).
	TArray<FBlendSample>         BlendSamples;
	float                        AxisMinX = -1.0f;
	float                        AxisMaxX =  1.0f;
	float                        AxisMinY = -1.0f;
	float                        AxisMaxY =  1.0f;

	friend FArchive& operator<<(FArchive& Ar, FAnimGraphNode& Node);
};
