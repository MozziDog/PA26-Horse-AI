#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"

// 2D Blend Space 의 순수 수학 코어 — 엔진 런타임(pose/RM/tick)과 완전히 분리된 기하 계산만 담당.
//   입력: 임의의 2D 산점 샘플 좌표 목록.
//   출력: 임의 질의점 (Query) 에 대해 "어느 샘플들을 어떤 가중치로 섞을지" (합=1, 각 [0,1]).
//
// 알고리즘: Bowyer–Watson Delaunay 삼각분할 → 포함 삼각형의 barycentric 3-weight.
//   - hull(볼록껍질) 밖 질의: 최근접 edge 투영(2-weight) 또는 최근접 vertex(1-weight) 로 clamp.
//   - 퇴화(degenerate): 샘플 1개=passthrough(1-weight) · 2개/공선(collinear)=1D bracket-lerp.
//     → 별도 1D blend space 코드 없이 이 경로가 1D 사용을 흡수한다(설계 §2-2, §2-4).
//
// FAnimNode_BlendSpace::Initialize 에서 Build() 를 1회 호출해 삼각망을 캐시하고,
// 매 Update 마다 CalculateWeights() 로 활성 샘플/가중치를 얻는다.

// 하나의 활성 샘플 기여(질의 결과의 원소).
struct FBlendTriangulationWeight
{
	int32 SampleIndex = -1;   // 원본 샘플 배열에서의 인덱스.
	float Weight      = 0.0f; // [0,1]. 결과 배열 전체 합 = 1.
};

// 삼각형 하나 — 원본 샘플 인덱스 3개(반시계, CCW). 디버그 draw(에디터 삼각망 edge)에도 사용.
struct FBlendTriangle
{
	int32 V0 = -1;
	int32 V1 = -1;
	int32 V2 = -1;
};

// 샘플 좌표로부터 삼각망을 build 하고, 임의 질의점에 대한 블렌드 가중치를 계산한다.
// 상태는 Build 시점에 고정 — 이후 CalculateWeights 는 const/스레드-safe read only.
class FBlendSpaceTriangulation
{
public:
	// 샘플 2D 좌표로 삼각망을 구성. 재호출 시 이전 상태를 덮어쓴다.
	//   - 3개 이상 & 비공선 → Delaunay 삼각분할.
	//   - 그 외(0/1/2개, 공선) → 퇴화 모드로 표시(삼각형 없음).
	// 반환: build 성공(샘플 1개 이상) 여부.
	bool Build(const TArray<FVector2>& InSamples);

	// 질의점에 대한 활성 샘플/가중치. Out 은 항상 clear 후 채워지며 합=1(샘플 1개 이상일 때).
	void CalculateWeights(const FVector2& Query, TArray<FBlendTriangulationWeight>& OutWeights) const;

	// 에디터 삼각망 edge draw 용.
	const TArray<FBlendTriangle>& GetTriangles() const { return Triangles; }
	const TArray<FVector2>&       GetSamples()   const { return Samples;   }

	int32 GetSampleCount() const { return static_cast<int32>(Samples.size()); }
	bool  IsDegenerate()   const { return bDegenerate; }

private:
	// 퇴화(공선/2샘플) 경로용 1D 정렬 축을 Samples 로부터 구성.
	void BuildCollinearOrder();

	// 퇴화 경로: 샘플 0/1/2개 또는 공선. 1D bracket-lerp / passthrough.
	void CalculateWeightsDegenerate(const FVector2& Query, TArray<FBlendTriangulationWeight>& OutWeights) const;

	// hull 밖(또는 어떤 삼각형에도 안 들어가는) 질의 → 최근접 edge/vertex 투영.
	void CalculateWeightsFallback(const FVector2& Query, TArray<FBlendTriangulationWeight>& OutWeights) const;

	TArray<FVector2>       Samples;      // 원본 샘플 좌표(순서 = SampleIndex).
	TArray<FBlendTriangle> Triangles;    // Delaunay 결과(비공선 3+ 샘플일 때만).
	bool                   bDegenerate = false; // true 면 삼각형 없이 1D/passthrough 경로.

	// 공선 퇴화 시 1D 정렬 축(샘플들을 이 축에 투영해 bracket-lerp).
	FVector2 CollinearOrigin = FVector2(0.0f, 0.0f);
	FVector2 CollinearDir    = FVector2(1.0f, 0.0f);
	// 공선 정렬 후 (투영값, SampleIndex) 오름차순.
	TArray<TPair<float, int32>> CollinearOrder;
};
