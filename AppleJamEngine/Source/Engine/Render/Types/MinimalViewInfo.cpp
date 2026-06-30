#include "Render/Types/MinimalViewInfo.h"

#include <cmath>

FMatrix FMinimalViewInfo::CalculateViewMatrix() const
{
	const FVector Forward = Rotation.GetForwardVector();
	const FVector Right   = Rotation.GetRightVector();
	const FVector Up      = Rotation.GetUpVector();
	return FMatrix::MakeViewMatrix(Right, Up, Forward, Location);
}

FMatrix FMinimalViewInfo::CalculateProjectionMatrix() const
{
	if (bIsOrtho)
	{
		const float HalfW = OrthoWidth * 0.5f;
		const float HalfH = HalfW / AspectRatio;
		return FMatrix::OrthoLH(HalfW * 2.0f, HalfH * 2.0f, NearClip, FarClip);
	}
	return FMatrix::PerspectiveFovLH(FOV, AspectRatio, NearClip, FarClip);
}

FMatrix FMinimalViewInfo::CalculateViewProjectionMatrix() const
{
	return CalculateViewMatrix() * CalculateProjectionMatrix();
}

FRay FMinimalViewInfo::DeprojectScreenToWorld(float MouseX, float MouseY, float ScreenWidth, float ScreenHeight) const
{
	const float NdcX = (2.0f * MouseX) / ScreenWidth - 1.0f;
	const float NdcY = 1.0f - (2.0f * MouseY) / ScreenHeight;

	// Reversed-Z 컨벤션: near plane = 1, far plane = 0 (codebase 전체 일관)
	const FVector NdcNear(NdcX, NdcY, 1.0f);
	const FVector NdcFar (NdcX, NdcY, 0.0f);

	const FMatrix InvVP = CalculateViewProjectionMatrix().GetInverse();

	const FVector WorldNear = InvVP.TransformPositionWithW(NdcNear);
	const FVector WorldFar  = InvVP.TransformPositionWithW(NdcFar);

	FRay Ray;
	Ray.Origin = WorldNear;

	const FVector Dir = WorldFar - WorldNear;
	const float Length = std::sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y + Dir.Z * Dir.Z);
	Ray.Direction = (Length > 1e-4f) ? Dir / Length : FVector(1.0f, 0.0f, 0.0f);

	return Ray;
}

bool FMinimalViewInfo::ProjectWorldToScreen(const FVector& WorldPos, float ScreenWidth, float ScreenHeight, FVector2& OutScreen) const
{
	const FMatrix VP = CalculateViewProjectionMatrix();

	// 행벡터 곱(WorldPos 의 w=1 암묵). operator*(FVector,FMatrix) 와 동일 레이아웃이되, W 성분을
	// 유지해 카메라 뒤 점을 reject 한다(TransformPositionWithW 는 W 부호를 숨겨 사용 불가).
	const float ClipX = WorldPos.X * VP.M[0][0] + WorldPos.Y * VP.M[1][0] + WorldPos.Z * VP.M[2][0] + VP.M[3][0];
	const float ClipY = WorldPos.X * VP.M[0][1] + WorldPos.Y * VP.M[1][1] + WorldPos.Z * VP.M[2][1] + VP.M[3][1];
	const float ClipW = WorldPos.X * VP.M[0][3] + WorldPos.Y * VP.M[1][3] + WorldPos.Z * VP.M[2][3] + VP.M[3][3];

	// LH 투영에서 ClipW = 뷰공간 전방 거리. 눈앞/뒤( <= 0 )면 화면에 없음.
	if (ClipW <= 1e-6f)
	{
		return false;
	}

	const float InvW = 1.0f / ClipW;
	const float NdcX = ClipX * InvW;   // [-1,1]
	const float NdcY = ClipY * InvW;   // [-1,1], NDC 는 Y-up

	// DeprojectScreenToWorld 의 픽셀↔NDC( NdcX=2x/W-1, NdcY=1-2y/H )의 역(좌상단 원점/Y-down).
	OutScreen.X = (NdcX + 1.0f) * 0.5f * ScreenWidth;
	OutScreen.Y = (1.0f - NdcY) * 0.5f * ScreenHeight;
	return true;
}
