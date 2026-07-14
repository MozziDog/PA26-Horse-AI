#include "BlendSpaceTriangulation.h"

#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// 내부 기하 헬퍼
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	// 2D cross (B-A) x (C-A). 부호로 방향(CCW>0), 절대값/2 = 삼각형 넓이.
	inline float Cross2D(const FVector2& A, const FVector2& B, const FVector2& C)
	{
		return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
	}

	inline float DistSq(const FVector2& A, const FVector2& B)
	{
		const float dx = A.X - B.X;
		const float dy = A.Y - B.Y;
		return dx * dx + dy * dy;
	}

	// 점 P 를 선분 [A,B] 에 투영한 파라미터 t 를 [0,1] 로 clamp.
	inline float ClosestParamOnSegment(const FVector2& P, const FVector2& A, const FVector2& B)
	{
		const float dx = B.X - A.X;
		const float dy = B.Y - A.Y;
		const float lenSq = dx * dx + dy * dy;
		if (lenSq <= 1e-12f)
		{
			return 0.0f; // A==B 퇴화 선분.
		}
		float t = ((P.X - A.X) * dx + (P.Y - A.Y) * dy) / lenSq;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return t;
	}

	// 삼각형 (A,B,C) 내부에서 P 의 barycentric (w0,w1,w2). 반환 false = 퇴화 삼각형.
	bool Barycentric(const FVector2& P, const FVector2& A, const FVector2& B, const FVector2& C,
	                 float& W0, float& W1, float& W2)
	{
		const float denom = Cross2D(A, B, C);
		if (std::fabs(denom) <= 1e-9f)
		{
			return false; // 공선/퇴화.
		}
		const float inv = 1.0f / denom;
		W0 = Cross2D(P, B, C) * inv; // A 대응.
		W1 = Cross2D(A, P, C) * inv; // B 대응.
		W2 = Cross2D(A, B, P) * inv; // C 대응.
		return true;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Build — Bowyer–Watson Delaunay
// ─────────────────────────────────────────────────────────────────────────────
bool FBlendSpaceTriangulation::Build(const TArray<FVector2>& InSamples)
{
	Samples = InSamples;
	Triangles.clear();
	CollinearOrder.clear();
	bDegenerate = false;

	const int32 n = static_cast<int32>(Samples.size());
	if (n == 0)
	{
		bDegenerate = true;
		return false;
	}
	if (n <= 2)
	{
		bDegenerate = true;
		// 2개(또는 1개)는 1D 정렬 축 구성으로 처리.
		// (1개는 CalculateWeightsDegenerate 에서 passthrough.)
	}

	if (bDegenerate)
	{
		BuildCollinearOrder();
		return true;
	}

	// ── 좌표 정규화 (predicate 안정화) ──
	// 삼각분할 위상은 좌표 규모·축 단위와 무관해야 한다. 두 축을 같은 스케일(max span)로 나누면
	// 축 단위가 다를 때(x=deg ±180, y=±1 등) 한 축이 짓눌려 near-collinear 슬리버가 생기고,
	// 슬리버의 외접원 반지름은 밑변²/(8·높이) 규모로 커져 super-triangle 정점을 포함하게 된다.
	// 그러면 hull 쪽 삼각형이 super 정점과 묶여 마지막 단계에서 함께 제거된다(바깥 샘플 소실 버그).
	// → 축별(anisotropic) 정규화로 pts 를 [-0.5,0.5]² 에 펼쳐 predicate 를 수행한다.
	//   축별 스케일링은 affine 이라 hull/포함관계/barycentric 가중치가 보존되므로, Triangles
	//   인덱스를 원본 Samples 에 그대로 적용해도 CalculateWeights(원본 좌표)와 일관된다.
	float minX = Samples[0].X, minY = Samples[0].Y;
	float maxX = Samples[0].X, maxY = Samples[0].Y;
	for (int32 i = 1; i < n; ++i)
	{
		minX = std::min(minX, Samples[i].X);
		minY = std::min(minY, Samples[i].Y);
		maxX = std::max(maxX, Samples[i].X);
		maxY = std::max(maxY, Samples[i].Y);
	}
	const float ctrX  = (minX + maxX) * 0.5f;
	const float ctrY  = (minY + maxY) * 0.5f;
	const float spanX = maxX - minX;
	const float spanY = maxY - minY;
	// 변화 없는 축은 0 으로 접는다 → 아래 공선 판정이 1D 퇴화로 걸러낸다.
	const float invSpanX = (spanX > 1e-12f) ? (1.0f / spanX) : 0.0f;
	const float invSpanY = (spanY > 1e-12f) ? (1.0f / spanY) : 0.0f;

	TArray<FVector2> pts;
	pts.reserve(static_cast<size_t>(n) + 3);
	for (int32 i = 0; i < n; ++i)
	{
		pts.push_back(FVector2((Samples[i].X - ctrX) * invSpanX, (Samples[i].Y - ctrY) * invSpanY));
	}

	// 공선성 판정 — 정규화 공간에서 수행해 좌표 규모/축 단위와 무관하게 만든다.
	// 모든 점이 한 직선 위면 삼각분할 불가 → 1D 퇴화.
	{
		// 가장 멀리 떨어진 두 점을 기준선으로 삼아 수치 안정성 확보.
		int32 iA = 0, iB = 1;
		float best = -1.0f;
		for (int32 i = 0; i < n; ++i)
		{
			for (int32 j = i + 1; j < n; ++j)
			{
				const float d = DistSq(pts[i], pts[j]);
				if (d > best) { best = d; iA = i; iB = j; }
			}
		}
		bool bAllCollinear = true; // best ≤ 1e-12: 모든 점이 사실상 동일 위치.
		if (best > 1e-12f)
		{
			const FVector2& A = pts[iA];
			const FVector2& B = pts[iB];
			const float baseLen = std::sqrt(best);
			for (int32 k = 0; k < n; ++k)
			{
				const float perpDist = std::fabs(Cross2D(A, B, pts[k])) / baseLen;
				if (perpDist > 1e-4f) // 정규화 좌표(≤√2)라 사실상 상대 임계값.
				{
					bAllCollinear = false;
					break;
				}
			}
		}
		if (bAllCollinear)
		{
			bDegenerate = true;
			BuildCollinearOrder();
			return true;
		}
	}

	// ── Bowyer–Watson ──
	// super-triangle margin 은 공선 필터(1e-4)를 통과한 최악 슬리버의 외접원 반지름
	// (≈ baseLen/(8·1e-4) ≲ 1.8e3)보다 충분히 크게 — 외접원이 super 정점에 닿으면 안 된다.
	const float margin = 1.0e4f;
	pts.push_back(FVector2(-margin, -margin));
	pts.push_back(FVector2( margin, -margin));
	pts.push_back(FVector2( 0.0f,    margin));
	const int32 s0 = n, s1 = n + 1, s2 = n + 2;

	// 작업용 삼각형 목록(정점은 pts 인덱스). CCW 로 정규화해 저장.
	struct FWorkTri { int32 A, B, C; };
	auto MakeCCW = [&pts](int32 a, int32 b, int32 c) -> FWorkTri
	{
		const double orient = ((double)pts[b].X - pts[a].X) * ((double)pts[c].Y - pts[a].Y) - ((double)pts[b].Y - pts[a].Y) * ((double)pts[c].X - pts[a].X);
		if (orient < 0.0)
		{
			std::swap(b, c);
		}
		return FWorkTri{ a, b, c };
	};

	TArray<FWorkTri> tris;
	tris.push_back(MakeCCW(s0, s1, s2));

	// 외접원 포함 판정(CCW 삼각형 가정) — determinant incircle test.
	auto InCircumcircle = [&pts](const FWorkTri& T, const FVector2& P) -> bool
	{
		const FVector2& A = pts[T.A];
		const FVector2& B = pts[T.B];
		const FVector2& C = pts[T.C];
		const double ax = (double)A.X - (double)P.X, ay = (double)A.Y - (double)P.Y;
		const double bx = (double)B.X - (double)P.X, by = (double)B.Y - (double)P.Y;
		const double cx = (double)C.X - (double)P.X, cy = (double)C.Y - (double)P.Y;
		const double det =
			(ax * ax + ay * ay) * (bx * cy - cx * by) -
			(bx * bx + by * by) * (ax * cy - cx * ay) +
			(cx * cx + cy * cy) * (ax * by - bx * ay);
		return det > 0.0; // CCW 에서 >0 == 내부.
	};

	// 점을 하나씩 삽입.
	for (int32 ip = 0; ip < n; ++ip)
	{
		const FVector2& P = pts[ip];

		// 이 점을 외접원에 포함하는 "bad" 삼각형 수집.
		TArray<FWorkTri> bad;
		for (const FWorkTri& T : tris)
		{
			if (InCircumcircle(T, P))
			{
				bad.push_back(T);
			}
		}

		// bad 삼각형들의 경계(polygonal hole) edge 추출 — 정확히 1개 삼각형에만 속한 edge.
		struct FEdge { int32 A, B; };
		TArray<FEdge> boundary;
		auto SameEdge = [](int32 a0, int32 b0, int32 a1, int32 b1)
		{
			return (a0 == a1 && b0 == b1) || (a0 == b1 && b0 == a1);
		};
		auto AddEdge = [&](int32 a, int32 b)
		{
			for (size_t e = 0; e < boundary.size(); ++e)
			{
				if (SameEdge(boundary[e].A, boundary[e].B, a, b))
				{
					boundary.erase(boundary.begin() + e); // 공유 edge → 내부, 제거.
					return;
				}
			}
			boundary.push_back(FEdge{ a, b });
		};
		for (const FWorkTri& T : bad)
		{
			AddEdge(T.A, T.B);
			AddEdge(T.B, T.C);
			AddEdge(T.C, T.A);
		}

		// bad 삼각형 제거.
		for (const FWorkTri& B : bad)
		{
			for (size_t t = 0; t < tris.size(); ++t)
			{
				if (tris[t].A == B.A && tris[t].B == B.B && tris[t].C == B.C)
				{
					tris.erase(tris.begin() + t);
					break;
				}
			}
		}

		// hole 경계 edge 와 새 점으로 삼각형 재구성.
		for (const FEdge& E : boundary)
		{
			tris.push_back(MakeCCW(E.A, E.B, ip));
		}
	}

	// super-triangle 정점을 참조하는 삼각형 제거 후, 원본 인덱스로 확정.
	for (const FWorkTri& T : tris)
	{
		if (T.A == s0 || T.A == s1 || T.A == s2 ||
			T.B == s0 || T.B == s1 || T.B == s2 ||
			T.C == s0 || T.C == s1 || T.C == s2)
		{
			continue;
		}
		Triangles.push_back(FBlendTriangle{ T.A, T.B, T.C });
	}

	// 방어: 삼각형이 하나도 안 남으면(수치 이슈) 1D 퇴화로 강등.
	if (Triangles.empty())
	{
		bDegenerate = true;
		BuildCollinearOrder();
	}

	return true;
}

// 퇴화(공선/2샘플) 경로용 1D 정렬 축 구성. 샘플 1개면 축 불필요(passthrough).
void FBlendSpaceTriangulation::BuildCollinearOrder()
{
	CollinearOrder.clear();
	const int32 n = static_cast<int32>(Samples.size());
	if (n < 2)
	{
		return;
	}
	// 가장 멀리 떨어진 두 점을 축 방향으로.
	int32 iA = 0, iB = 1;
	float best = -1.0f;
	for (int32 i = 0; i < n; ++i)
	{
		for (int32 j = i + 1; j < n; ++j)
		{
			const float d = DistSq(Samples[i], Samples[j]);
			if (d > best) { best = d; iA = i; iB = j; }
		}
	}
	CollinearOrigin = Samples[iA];
	FVector2 dir = Samples[iB] - Samples[iA];
	const float len = dir.Length();
	CollinearDir = (len > 1e-6f) ? (dir / len) : FVector2(1.0f, 0.0f);

	CollinearOrder.reserve(n);
	for (int32 i = 0; i < n; ++i)
	{
		const FVector2 rel = Samples[i] - CollinearOrigin;
		const float proj = rel.Dot(CollinearDir);
		CollinearOrder.push_back(TPair<float, int32>(proj, i));
	}
	std::sort(CollinearOrder.begin(), CollinearOrder.end(),
		[](const TPair<float, int32>& L, const TPair<float, int32>& R)
		{
			return L.first < R.first;
		});
}

// ─────────────────────────────────────────────────────────────────────────────
// CalculateWeights
// ─────────────────────────────────────────────────────────────────────────────
void FBlendSpaceTriangulation::CalculateWeights(const FVector2& Query,
	TArray<FBlendTriangulationWeight>& OutWeights) const
{
	OutWeights.clear();

	const int32 n = static_cast<int32>(Samples.size());
	if (n == 0)
	{
		return;
	}
	if (n == 1)
	{
		OutWeights.push_back(FBlendTriangulationWeight{ 0, 1.0f });
		return;
	}

	if (bDegenerate)
	{
		CalculateWeightsDegenerate(Query, OutWeights);
		return;
	}

	// 포함 삼각형 탐색 → barycentric 3-weight.
	const float eps = -1e-4f; // 경계 수치 여유.
	for (const FBlendTriangle& T : Triangles)
	{
		float w0, w1, w2;
		if (!Barycentric(Query, Samples[T.V0], Samples[T.V1], Samples[T.V2], w0, w1, w2))
		{
			continue;
		}
		if (w0 >= eps && w1 >= eps && w2 >= eps)
		{
			// clamp & 정규화.
			w0 = std::max(0.0f, w0);
			w1 = std::max(0.0f, w1);
			w2 = std::max(0.0f, w2);
			const float sum = w0 + w1 + w2;
			if (sum > 1e-8f)
			{
				const float inv = 1.0f / sum;
				OutWeights.push_back(FBlendTriangulationWeight{ T.V0, w0 * inv });
				OutWeights.push_back(FBlendTriangulationWeight{ T.V1, w1 * inv });
				OutWeights.push_back(FBlendTriangulationWeight{ T.V2, w2 * inv });
				return;
			}
		}
	}

	// 어느 삼각형에도 안 들어감(hull 밖) → 최근접 edge/vertex 투영.
	CalculateWeightsFallback(Query, OutWeights);
}

void FBlendSpaceTriangulation::CalculateWeightsDegenerate(const FVector2& Query,
	TArray<FBlendTriangulationWeight>& OutWeights) const
{
	// n>=2 & (2개 또는 공선). CollinearOrder 를 따라 1D bracket-lerp.
	const int32 m = static_cast<int32>(CollinearOrder.size());
	if (m == 0)
	{
		return;
	}
	if (m == 1)
	{
		OutWeights.push_back(FBlendTriangulationWeight{ CollinearOrder[0].second, 1.0f });
		return;
	}

	const FVector2 rel = Query - CollinearOrigin;
	const float q = rel.Dot(CollinearDir);

	// 범위 밖 → 끝 샘플 clamp.
	if (q <= CollinearOrder[0].first)
	{
		OutWeights.push_back(FBlendTriangulationWeight{ CollinearOrder[0].second, 1.0f });
		return;
	}
	if (q >= CollinearOrder[m - 1].first)
	{
		OutWeights.push_back(FBlendTriangulationWeight{ CollinearOrder[m - 1].second, 1.0f });
		return;
	}

	// bracket 탐색.
	for (int32 i = 0; i < m - 1; ++i)
	{
		const float a = CollinearOrder[i].first;
		const float b = CollinearOrder[i + 1].first;
		if (q >= a && q <= b)
		{
			const float span = b - a;
			float t = (span > 1e-8f) ? (q - a) / span : 0.0f;
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			if (t <= 1e-6f)
			{
				OutWeights.push_back(FBlendTriangulationWeight{ CollinearOrder[i].second, 1.0f });
			}
			else if (t >= 1.0f - 1e-6f)
			{
				OutWeights.push_back(FBlendTriangulationWeight{ CollinearOrder[i + 1].second, 1.0f });
			}
			else
			{
				OutWeights.push_back(FBlendTriangulationWeight{ CollinearOrder[i].second,     1.0f - t });
				OutWeights.push_back(FBlendTriangulationWeight{ CollinearOrder[i + 1].second, t });
			}
			return;
		}
	}
}

void FBlendSpaceTriangulation::CalculateWeightsFallback(const FVector2& Query,
	TArray<FBlendTriangulationWeight>& OutWeights) const
{
	// hull 밖: 모든 삼각형 edge 중 Query 에 최근접인 edge 를 찾아 그 위에 투영(2-weight).
	// edge 가 없으면(이론상 없음) 최근접 vertex.
	float bestDistSq = 3.4e38f;
	int32 bestA = -1, bestB = -1;
	float bestT = 0.0f;

	auto ConsiderEdge = [&](int32 a, int32 b)
	{
		const float t = ClosestParamOnSegment(Query, Samples[a], Samples[b]);
		const FVector2 proj = Samples[a] + (Samples[b] - Samples[a]) * t;
		const float d = DistSq(Query, proj);
		if (d < bestDistSq)
		{
			bestDistSq = d;
			bestA = a; bestB = b; bestT = t;
		}
	};

	for (const FBlendTriangle& T : Triangles)
	{
		ConsiderEdge(T.V0, T.V1);
		ConsiderEdge(T.V1, T.V2);
		ConsiderEdge(T.V2, T.V0);
	}

	if (bestA < 0)
	{
		// 최근접 vertex fallback.
		int32 bestV = 0;
		float bv = 3.4e38f;
		for (int32 i = 0; i < static_cast<int32>(Samples.size()); ++i)
		{
			const float d = DistSq(Query, Samples[i]);
			if (d < bv) { bv = d; bestV = i; }
		}
		OutWeights.push_back(FBlendTriangulationWeight{ bestV, 1.0f });
		return;
	}

	if (bestT <= 1e-6f)
	{
		OutWeights.push_back(FBlendTriangulationWeight{ bestA, 1.0f });
	}
	else if (bestT >= 1.0f - 1e-6f)
	{
		OutWeights.push_back(FBlendTriangulationWeight{ bestB, 1.0f });
	}
	else
	{
		OutWeights.push_back(FBlendTriangulationWeight{ bestA, 1.0f - bestT });
		OutWeights.push_back(FBlendTriangulationWeight{ bestB, bestT });
	}
}
