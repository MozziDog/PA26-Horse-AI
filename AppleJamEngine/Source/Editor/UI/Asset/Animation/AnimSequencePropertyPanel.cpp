#include "AnimSequencePropertyPanel.h"

#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Sequence/BoneAnimationTrack.h"

#include <imgui.h>

namespace
{
	bool RenderRootMotionSection(UAnimSequence* Seq)
	{
		bool bChanged = false;

		ImGui::TextUnformatted("Root Motion");
		ImGui::Separator();

		// Force Root Lock — horizontal 만 잠금 (in-place 걷기), vertical Z 는 anim 유지.
		bool bLock = Seq->GetForceRootLock();
		if (ImGui::Checkbox("Force Root Lock", &bLock))
		{
			Seq->SetForceRootLock(bLock);
			if (bLock) Seq->SetEnableRootMotion(false);   // mutex
			bChanged = true;
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Root motion 본의 horizontal (X/Y) translation 을 잠가\nin-place 재생. Z (vertical bobbing) 는 유지.");
		}

		// Enable Root Motion — translation/rotation 을 actor 의 world transform 에 반영.
		bool bRootMotion = Seq->GetEnableRootMotion();
		if (ImGui::Checkbox("Enable Root Motion", &bRootMotion))
		{
			Seq->SetEnableRootMotion(bRootMotion);   // bForceRootLock 자동 해제 (setter 안)
			bChanged = true;
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Root motion 본의 translation/rotation 을 본 pose 에서 제거하고\nowning actor 의 transform 에 반영 (캐릭터가 anim 으로 world 이동).");
		}

		// Root motion 성분 분해 — 무엇이 이동(추출+잠금)이고 무엇이 제자리 동작(pose 유지)인지는
		// 클립의 속성 (per-asset).
		if (bRootMotion)
		{
			bool bExtractZ = Seq->GetExtractRootMotionZ();
			if (ImGui::Checkbox("Extract Root Motion Z", &bExtractZ))
			{
				Seq->SetExtractRootMotionZ(bExtractZ);
				bChanged = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("ON: Z 도 이동으로 추출 (점프 상승 등) — pose 에서 첫 키로 잠금.\nOFF: Z 는 제자리 bob (보행/구보) — pose 에 애니메이션 값 유지, delta Z = 0.");
			}

			ImGui::Dummy(ImVec2(0.0f, 4.0f));
			ImGui::TextUnformatted("Root Rotation Lock");
			ImGui::SetNextItemWidth(-1.0f);
			int RotLockIdx = static_cast<int>(Seq->GetRootRotationLock());
			if (ImGui::Combo("##rootRotationLock", &RotLockIdx,
				GRootMotionRotationLockNames, static_cast<int>(GRootMotionRotationLockCount)))
			{
				Seq->SetRootRotationLock(static_cast<ERootMotionRotationLock>(RotLockIdx));
				bChanged = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Full: rotation 전체 추출+잠금 (몽타주 등 기본).\nYawOnly: yaw 만 추출 — swing(pitch/roll)은 pose 유지 (보행류).\nNone: rotation 추출 안 함 (translation-only).");
			}
		}

		// Root motion 본 선택 콤보 — 둘 중 하나라도 켜져 있을 때만 노출.
		const bool bShowBoneCombo = bLock || bRootMotion;
		if (bShowBoneCombo)
		{
			ImGui::Dummy(ImVec2(0.0f, 4.0f));
			ImGui::TextUnformatted("Root Motion Bone");
			ImGui::SetNextItemWidth(-1.0f);

			const FString& Current = Seq->GetRootMotionBoneName();
			const char* CurrentLabel = Current.empty() ? "(none)" : Current.c_str();
			if (ImGui::BeginCombo("##rootMotionBone", CurrentLabel))
			{
				if (ImGui::Selectable("(none)", Current.empty()))
				{
					Seq->SetRootMotionBoneName(FString());
					bChanged = true;
				}
				for (const FBoneAnimationTrack& Track : Seq->GetBoneTracks())
				{
					if (Track.BoneName.empty()) continue;
					const bool bSelected = (Track.BoneName == Current);
					if (ImGui::Selectable(Track.BoneName.c_str(), bSelected))
					{
						Seq->SetRootMotionBoneName(Track.BoneName);
						bChanged = true;
					}
					if (bSelected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		// Root Yaw Offset (Unity에서 따온 기능)
		// Animation sequence의 몸통 기준 방향 재정의
		ImGui::Dummy(ImVec2(0.0f, 4.0f));
		ImGui::TextUnformatted("Root Yaw Offset (deg)");
		ImGui::SetNextItemWidth(-1.0f);
		float YawOffsetDeg = Seq->GetRootYawOffsetDegrees();
		if (ImGui::DragFloat("##rootYawOffset", &YawOffsetDeg, 0.5f, -180.0f, 180.0f, "%.1f"))
		{
			Seq->SetRootYawOffsetDegrees(YawOffsetDeg);
			bChanged = true;
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Re-align animation sequence's 'direction'.\nApply to both character pose & root motion move direction.");
		}

		return bChanged;
	}
}

bool FAnimSequencePropertyPanel::Render(UAnimSequence* Seq)
{
	if (!Seq)
	{
		ImGui::TextDisabled("No animation selected.");
		return false;
	}

	return RenderRootMotionSection(Seq);
	// 향후 추가 section 은 여기에 ImGui::Dummy + 호출 추가.
}
