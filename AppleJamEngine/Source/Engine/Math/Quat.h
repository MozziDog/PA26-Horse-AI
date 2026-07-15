#pragma once
#include <cmath>
#include "Vector.h"
#include "MathUtils.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Reflection/UStruct.h"

#include "Source/Engine/Math/Quat.generated.h"

struct FRotator;
struct FMatrix;

USTRUCT()
struct FQuat
{
	GENERATED_BODY()

	UPROPERTY(Save)
	float X;

	UPROPERTY(Save)
	float Y;

	UPROPERTY(Save)
	float Z;

	UPROPERTY(Save)
	float W;

	FQuat() : X(0.0f), Y(0.0f), Z(0.0f), W(1.0f) {}
	FQuat(float InX, float InY, float InZ, float InW) : X(InX), Y(InY), Z(InZ), W(InW) {}

	// 축-각 생성 (Axis는 정규화, AngleRad는 라디안)
	static FQuat FromAxisAngle(const FVector& Axis, float AngleRad)
	{
		float Half = AngleRad * 0.5f;
		float S = sinf(Half);
		return FQuat(Axis.X * S, Axis.Y * S, Axis.Z * S, cosf(Half));
	}

	// 쿼터니언 곱 (회전 합성)
	FQuat operator*(const FQuat& Q) const
	{
		return FQuat(
			W * Q.X + X * Q.W + Y * Q.Z - Z * Q.Y,
			W * Q.Y - X * Q.Z + Y * Q.W + Z * Q.X,
			W * Q.Z + X * Q.Y - Y * Q.X + Z * Q.W,
			W * Q.W - X * Q.X - Y * Q.Y - Z * Q.Z
		);
	}

	FQuat& operator*=(const FQuat& Q)
	{
		*this = *this * Q;
		return *this;
	}

	float SizeSquared() const { return X * X + Y * Y + Z * Z + W * W; }
	float Size() const { return sqrtf(SizeSquared()); }

	void Normalize()
	{
		float S = Size();
		if (S > EPSILON)
		{
			float Inv = 1.0f / S;
			X *= Inv; Y *= Inv; Z *= Inv; W *= Inv;
		}
	}

	FQuat GetNormalized() const
	{
		FQuat Result = *this;
		Result.Normalize();
		return Result;
	}

	FQuat Inverse() const
	{
		// 단위 쿼터니언의 역 = 켤레
		return FQuat(-X, -Y, -Z, W);
	}

	// 벡터 회전: q * v * q^-1
	FVector RotateVector(const FVector& V) const
	{
		FQuat VQ(V.X, V.Y, V.Z, 0.0f);
		FQuat Result = *this * VQ * Inverse();
		return FVector(Result.X, Result.Y, Result.Z);
	}

	FVector GetForwardVector() const { return RotateVector(FVector(1.0f, 0.0f, 0.0f)); }
	FVector GetRightVector()   const { return RotateVector(FVector(0.0f, 1.0f, 0.0f)); }
	FVector GetUpVector()      const { return RotateVector(FVector(0.0f, 0.0f, 1.0f)); }

	// Swing-Twist 분해: *this == OutTwist * OutSwing (곱은 오른쪽부터 적용 — swing 먼저, twist 나중).
	// OutTwist 는 TwistAxis(단위 벡터) 축 회전 성분, OutSwing 은 그 축과 수직인 나머지 성분.
	// *this 는 정규화되어 있어야 한다. 회전각이 TwistAxis 와 수직인 축의 ~180°에 가까우면
	// twist 가 수치적으로 정의되지 않으므로 (Identity, *this) 로 fallback.
	void ToSwingTwist(const FVector& TwistAxis, FQuat& OutSwing, FQuat& OutTwist) const
	{
		const float Dot = X * TwistAxis.X + Y * TwistAxis.Y + Z * TwistAxis.Z;
		const FQuat Projected(TwistAxis.X * Dot, TwistAxis.Y * Dot, TwistAxis.Z * Dot, W);
		const float LenSq = Projected.SizeSquared();
		if (LenSq < 1.e-8f)
		{
			OutTwist = Identity;
			OutSwing = *this;
			return;
		}
		const float InvLen = 1.0f / sqrtf(LenSq);
		OutTwist = FQuat(Projected.X * InvLen, Projected.Y * InvLen, Projected.Z * InvLen, Projected.W * InvLen);
		OutSwing = OutTwist.Inverse() * (*this);
	}

	// TwistAxis 축 회전 성분만 반환하는 편의 함수.
	FQuat GetTwist(const FVector& TwistAxis) const
	{
		FQuat Swing, Twist;
		ToSwingTwist(TwistAxis, Swing, Twist);
		return Twist;
	}

	// Spherical Linear Interpolation
	static FQuat Slerp(const FQuat& A, const FQuat& B, float Alpha)
	{
		float CosAngle = A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;

		// 최단 경로 보장
		FQuat B2 = B;
		if (CosAngle < 0.0f)
		{
			B2 = FQuat(-B.X, -B.Y, -B.Z, -B.W);
			CosAngle = -CosAngle;
		}

		float S0, S1;
		if (CosAngle > 0.9999f)
		{
			// 거의 같은 방향 — 선형 보간 후 정규화
			S0 = 1.0f - Alpha;
			S1 = Alpha;
		}
		else
		{
			float Angle = acosf(CosAngle);
			float InvSin = 1.0f / sinf(Angle);
			S0 = sinf((1.0f - Alpha) * Angle) * InvSin;
			S1 = sinf(Alpha * Angle) * InvSin;
		}

		return FQuat(
			S0 * A.X + S1 * B2.X,
			S0 * A.Y + S1 * B2.Y,
			S0 * A.Z + S1 * B2.Z,
			S0 * A.W + S1 * B2.W
		).GetNormalized();
	}

	bool Equals(const FQuat& Other, float Tolerance = EPSILON) const
	{
		return fabsf(X - Other.X) < Tolerance
			&& fabsf(Y - Other.Y) < Tolerance
			&& fabsf(Z - Other.Z) < Tolerance
			&& fabsf(W - Other.W) < Tolerance;
	}

	static const FQuat Identity;

	// 변환 — 선언만, 구현은 Quat.cpp (순환 의존 방지)
	FRotator ToRotator() const;
	FMatrix ToMatrix() const;
	static FQuat FromRotator(const FRotator& Rot);
	static FQuat FromMatrix(const FMatrix& Mat);
};
