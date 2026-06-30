#include "UIEditorWidget.h"

#include "Object/Object.h"
#include "Object/GarbageCollection.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/SceneSaveManager.h"
#include "UI/UIAsset.h"
#include "UI/UIAssetManager.h"
#include "UI/Canvas/UICanvas.h"
#include "UI/Canvas/UICanvasActor.h"
#include "UI/Canvas/UICanvasManager.h"
#include "UI/Canvas/UIElement.h"
#include "UI/Canvas/UIButton.h"
#include "UI/Canvas/UIImage.h"
#include "UI/Canvas/UITextElement.h"
#include "UI/Canvas/UILabel.h"
#include "UI/Canvas/UIRect.h"
#include "Editor/UI/Util/UICanvasMirror.h"
#include "Platform/Paths.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <initializer_list>

namespace
{
	// Font Weight / Text Align 의 "가능한 값"만 toggle 로 순환한다(자유 입력 금지). 모두 RmlUi 가
	// 인식하는 값 — weight: normal|bold, align: left|center|right(단일행 라벨이라 justify 제외).
	const char* const kFontWeightOptions[] = { "normal", "bold" };
	const char* const kTextAlignOptions[]  = { "left", "center", "right" };

	// 현재 값을 보여주는 버튼 — 클릭하면 Options 의 다음 값으로 순환. 바뀌면 OutNext 에 담고 true 반환.
	// 보이는 라벨은 "Label: value", ImGui ID 는 ###Label 로 고정(값이 바뀌어도 동일 위젯 유지).
	bool CycleButton(const char* Label, const char* const* Options, int Count,
	                 const FString& Current, FString& OutNext)
	{
		int Index = 0;
		for (int i = 0; i < Count; ++i)
		{
			if (strcmp(Current.c_str(), Options[i]) == 0) { Index = i; break; }
		}
		const FString Caption = FString(Label) + ": " + Options[Index] + "###" + Label;
		if (ImGui::Button(Caption.c_str()))
		{
			OutNext = FString(Options[(Index + 1) % Count]);
			return true;
		}
		return false;
	}

	// 디렉터리에서 주어진 확장자(소문자, 점 포함)에 해당하는 파일명만 수집(대소문자 무시). 드롭다운이
	// 열려 있을 때만 호출되므로(BeginCombo 가 열렸을 때만 true) 매 프레임 디스크 스캔은 아니다.
	TArray<FString> CollectFilesWithExt(const std::wstring& Dir, std::initializer_list<const wchar_t*> Exts)
	{
		TArray<FString> Files;
		std::error_code Ec;
		if (!std::filesystem::exists(Dir, Ec))
		{
			return Files;
		}
		for (const auto& Entry : std::filesystem::directory_iterator(Dir, Ec))
		{
			if (Ec || !Entry.is_regular_file())
			{
				continue;
			}
			std::wstring Ext = Entry.path().extension().wstring();
			for (wchar_t& Ch : Ext) { Ch = static_cast<wchar_t>(towlower(Ch)); }
			for (const wchar_t* Want : Exts)
			{
				if (Ext == Want)
				{
					Files.push_back(FPaths::ToUtf8(Entry.path().filename().wstring()));
					break;
				}
			}
		}
		return Files;
	}

	// Content/UI/Images 의 png/jpg/jpeg 파일명. 저장값은 호출부가 "Content/UI/Images/"+파일명으로 만든다.
	TArray<FString> CollectUIImageFiles()
	{
		return CollectFilesWithExt(FPaths::Combine(FPaths::AssetDir(), L"UI/Images"), { L".png", L".jpg", L".jpeg" });
	}

	// 캔버스 트리의 비어있지 않은 ElementName 들(액션 대상 후보 — Show/Hide/Toggle/SetImage Target).
	void CollectElementNames(UUIElement* Element, TArray<FString>& Out)
	{
		if (!Element)
		{
			return;
		}
		if (!Element->GetElementName().empty())
		{
			Out.push_back(Element->GetElementName());
		}
		for (USceneComponent* Child : Element->GetChildren())
		{
			if (UUIElement* ChildElement = Cast<UUIElement>(Child))
			{
				CollectElementNames(ChildElement, Out);
			}
		}
	}

	// 문자열 값을 옵션 목록에서 고르는 콤보(토글). 선택 시 Value 갱신 + true 반환. "(None)" 으로 비울 수 있음.
	// 저장값 == 표시 옵션일 때만 사용(이미지처럼 저장값≠표시값인 경우는 호출부에서 별도 처리).
	bool StringCombo(const char* Label, const TArray<FString>& Options, FString& Value)
	{
		bool bChanged = false;
		const FString Preview = Value.empty() ? FString("(None)") : Value;
		if (ImGui::BeginCombo(Label, Preview.c_str()))
		{
			if (ImGui::Selectable("(None)", Value.empty())) { Value.clear(); bChanged = true; }
			for (const FString& Opt : Options)
			{
				if (ImGui::Selectable(Opt.c_str(), Opt == Value)) { Value = Opt; bChanged = true; }
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}

	// 뷰포트 드로우는 FUICanvasMirror::DrawElement(공유 헤더) 로 이관 — 레벨 뷰포트와 동일 미러 사용.

	// 계층 트리 — 캔버스 GetChildren 재귀를 ImGui::TreeNode 로(진단 §B). 선택 하이라이트 + 클릭 선택(사이클 ④).
	void DrawHierarchyNode(UUIElement* Element, UUIElement*& Selected)
	{
		if (!Element)
		{
			return;
		}

		bool bHasChild = false;
		for (USceneComponent* Child : Element->GetChildren())
		{
			if (Cast<UUIElement>(Child)) { bHasChild = true; break; }
		}

		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen
			| ImGuiTreeNodeFlags_SpanAvailWidth;
		if (!bHasChild)
		{
			Flags |= ImGuiTreeNodeFlags_Leaf;
		}
		if (Element == Selected)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}

		// [show/off] 숨긴 요소는 계층에서 흐리게 + "(hidden)" 표기(토글은 Details 의 Visible 체크박스).
		const bool bElementVisible = Element->IsVisible();
		if (!bElementVisible)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
		}
		const bool bOpen = ImGui::TreeNodeEx((void*)Element, Flags, "%s%s",
			Element->GetClass()->GetName(), bElementVisible ? "" : "  (hidden)");
		if (!bElementVisible)
		{
			ImGui::PopStyleColor();
		}
		if (ImGui::IsItemClicked())
		{
			Selected = Element;   // 트리 클릭 → 선택(뷰포트/디테일과 공유).
		}
		if (bOpen)
		{
			for (USceneComponent* Child : Element->GetChildren())
			{
				if (UUIElement* ChildElement = Cast<UUIElement>(Child))
				{
					DrawHierarchyNode(ChildElement, Selected);
				}
			}
			ImGui::TreePop();
		}
	}
}

bool FUIEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UUIAsset>();
}

void FUIEditorWidget::Open(UObject* Object)
{
	if (!CanEdit(Object))
	{
		return;
	}

	DestroyLiveTree();   // 단일 인스턴스 재사용 — 다른 에셋으로 재오픈 시 이전 트리 정리.
	EditedObject = Object;
	bOpen        = true;
	ClearDirty();
	BuildLiveTree(static_cast<UUIAsset*>(Object));
}

void FUIEditorWidget::Close()
{
	DestroyLiveTree();
	FAssetEditorWidget::Close();
}

void FUIEditorWidget::AddReferencedObjects(FReferenceCollector& Collector)
{
	FAssetEditorWidget::AddReferencedObjects(Collector);   // EditedObject(UUIAsset) keepalive
	Collector.AddReferencedObject(OwnerActor);             // 소유자 액터 → OwnedComponents(캔버스 트리) keepalive
}

void FUIEditorWidget::BuildLiveTree(UUIAsset* Asset)
{
	if (!Asset)
	{
		return;
	}

	// JSON 블롭 → 라이브 트리. 소유자는 월드 없는 에디터 전용 액터.
	OwnerActor = UObjectManager::Get().CreateObject<AUICanvasActor>();
	USceneComponent* Root = FSceneSaveManager::DeserializeUITree(Asset->GetCanvasData(), OwnerActor);
	Canvas = Cast<UUICanvas>(Root);
	if (Canvas && OwnerActor)
	{
		OwnerActor->SetRootComponent(Canvas);   // AUICanvasActor 계약(RootComponent=Canvas) 유지.
	}
}

void FUIEditorWidget::DestroyLiveTree()
{
	Canvas   = nullptr;
	Selected = nullptr;
	if (OwnerActor)
	{
		UObjectManager::Get().DestroyObject(OwnerActor);   // 컴포넌트 트리까지 정리(2-phase GC).
		OwnerActor = nullptr;
	}
}

void FUIEditorWidget::SaveToAsset()
{
	UUIAsset* Asset = Cast<UUIAsset>(EditedObject);
	if (!Asset || !Canvas)
	{
		return;
	}

	// 편집된 라이브 트리를 다시 JSON 으로 직렬화(저장 방향은 ⓪에서 추가한 SerializeUITree 재사용)
	// → 에셋 블롭 갱신 → FAssetPackage string payload 로 .uasset 기록.
	Asset->SetCanvasData(FSceneSaveManager::SerializeUITree(Canvas));
	if (FUIAssetManager::Get().Save(Asset))
	{
		ClearDirty();
		// [에셋 변경 라이브 갱신] 이 .uasset 을 참조하는 편집 월드의 AUICanvasActor 들을 재빌드 →
		// 에디터 씬(뷰포트 미러)에 즉시 반영. 캐시는 방금 Save 가 최신 CanvasData 로 갱신해 둠.
		AUICanvasActor::RefreshActorsReferencingAsset(Asset->GetSourcePath());
	}
}

void FUIEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;
	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	UUIAsset* UIAsset = static_cast<UUIAsset*>(EditedObject);

	bool    bWindowOpen  = true;
	FString VisibleTitle = "UI Editor";
	if (!UIAsset->GetSourcePath().empty())
	{
		VisibleTitle += " - ";
		VisibleTitle += UIAsset->GetSourcePath();
	}

	ImGui::SetNextWindowSize(ImVec2(960.0f, 600.0f), ImGuiCond_Once);

	// ### 뒤 고정 ID 로 제목이 바뀌어도 같은 창을 재사용(단일 인스턴스).
	FString WindowTitle = VisibleTitle + "###UIEditor";
	if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen))
	{
		ImGui::End();
		if (!bWindowOpen)
		{
			Close();
		}
		return;
	}

	// 상단 툴바 — 저장(편집 트리 → .uasset 재직렬화) + dirty 표시(사이클 ⑥).
	if (ImGui::Button("Save"))
	{
		SaveToAsset();
	}
	ImGui::SameLine();
	ImGui::TextDisabled(IsDirty() ? "(modified)" : "(saved)");
	ImGui::Separator();

	// 4분할: [좌상 팔레트 / 좌하 계층트리] | [중앙 캔버스 뷰포트] | [우 디테일]. child 분할(진단 B).
	const float  LeftWidth  = 200.0f;
	const float  RightWidth = 260.0f;
	const float  Spacing    = ImGui::GetStyle().ItemSpacing.x;
	const ImVec2 Avail      = ImGui::GetContentRegionAvail();
	float        CenterWidth = Avail.x - LeftWidth - RightWidth - Spacing * 2.0f;
	if (CenterWidth < 80.0f) CenterWidth = 80.0f;

	// 좌측 컬럼 — 팔레트(상) + 계층트리(하) 상하 분할.
	ImGui::BeginChild("##UILeftColumn", ImVec2(LeftWidth, 0.0f), false);
	{
		const float PaletteHeight = ImGui::GetContentRegionAvail().y * 0.4f;
		ImGui::BeginChild("##UIPalette", ImVec2(0.0f, PaletteHeight), true);
		RenderPalettePanel();
		ImGui::EndChild();

		ImGui::BeginChild("##UIHierarchy", ImVec2(0.0f, 0.0f), true);
		RenderHierarchyPanel();
		ImGui::EndChild();
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// 중앙 — 캔버스 뷰포트.
	ImGui::BeginChild("##UIViewport", ImVec2(CenterWidth, 0.0f), true);
	RenderViewportPanel();
	ImGui::EndChild();

	ImGui::SameLine();

	// 우측 — 디테일(나머지 폭).
	ImGui::BeginChild("##UIDetails", ImVec2(0.0f, 0.0f), true);
	RenderDetailsPanel();
	ImGui::EndChild();

	ImGui::End();

	if (!bWindowOpen)
	{
		Close();
	}
}

void FUIEditorWidget::RenderPalettePanel()
{
	ImGui::TextUnformatted("Palette");
	ImGui::Separator();
	if (!Canvas)
	{
		ImGui::TextDisabled("No canvas");
		return;
	}

	const float W = ImGui::GetContentRegionAvail().x;
	if (ImGui::Button("Canvas", ImVec2(W, 0.0f))) { SpawnElement(UUICanvas::StaticClass()); }
	if (ImGui::Button("Button", ImVec2(W, 0.0f))) { SpawnElement(UUIButton::StaticClass()); }
	if (ImGui::Button("Image",  ImVec2(W, 0.0f))) { SpawnElement(UUIImage::StaticClass()); }
	// "배경 없는 텍스트" 프리셋 — UUILabel(bVisibleRect=false + 기본 Text="Text"). 스폰 직후 자동 선택되어
	// 디테일에서 바로 편집 가능. 뷰포트 클릭 선택은 불가(배경 없음→히트테스트 제외)이라 계층 트리로 선택(R5).
	if (ImGui::Button("Text",   ImVec2(W, 0.0f))) { SpawnElement(UUILabel::StaticClass()); }

	ImGui::Spacing();
	ImGui::TextDisabled("Adds under selection,\nelse under Canvas.");
}

void FUIEditorWidget::SpawnElement(UClass* ElementClass)
{
	if (!ElementClass || !OwnerActor || !Canvas)
	{
		return;
	}

	// 진단 §D: AddComponentToActor 모델 — CreateObject + RegisterComponent + AttachToComponent.
	UObject*    Obj     = FObjectFactory::Get().Create(ElementClass->GetName(), OwnerActor);
	UUIElement* NewElem = Cast<UUIElement>(Obj);
	if (!NewElem)
	{
		if (Obj) UObjectManager::Get().DestroyObject(Obj);
		return;
	}
	OwnerActor->RegisterComponent(NewElem);

	// 부모 = 선택 노드(없으면 캔버스 루트). 자식 수 기반 cascade 로 겹침 방지.
	UUIElement* Parent     = Selected ? Selected : Canvas;
	int32       ChildCount = 0;
	for (USceneComponent* Child : Parent->GetChildren())
	{
		if (Cast<UUIElement>(Child)) ++ChildCount;
	}
	NewElem->SetPosition(FVector2(40.0f + ChildCount * 30.0f, 40.0f + ChildCount * 30.0f));
	NewElem->AttachToComponent(Parent);

	Selected = NewElem;   // 새로 만든 요소를 선택 상태로(디테일/다음 생성 부모).
	MarkDirty();
}

void FUIEditorWidget::DeleteSelected()
{
	if (!Selected || !OwnerActor)
	{
		return;
	}

	// 루트 캔버스는 삭제 불가 — 트리 루트이자 AUICanvasActor RootComponent 계약. 비우면 편집 불능.
	if (Selected == Canvas)
	{
		return;
	}

	// 댕글링 방지 — 디테일/뷰포트/계층이 더 이상 참조하지 않도록 선택을 먼저 해제한 뒤 파괴.
	UUIElement* ToDelete = Selected;
	Selected = nullptr;

	// 서브트리 일괄 정리: 부모 detach + OwnedComponents(키프얼라이브) 해제 + RouteComponentDestroyed
	// (자식 재귀) + DestroyObject(2-phase GC). SpawnElement 의 RegisterComponent/Attach 와 대칭.
	OwnerActor->RemoveComponent(ToDelete);

	// 다음 SaveToAsset 의 SerializeUITree 가 살아있는 트리만 기록 → 삭제가 .uasset 에 영속화.
	MarkDirty();
}

void FUIEditorWidget::RenderHierarchyPanel()
{
	ImGui::TextUnformatted("Hierarchy");
	ImGui::Separator();
	if (!Canvas)
	{
		ImGui::TextDisabled("No canvas");
		return;
	}
	DrawHierarchyNode(Canvas, Selected);
}

void FUIEditorWidget::RenderViewportPanel()
{
	ImDrawList*  DL     = ImGui::GetWindowDrawList();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 Avail  = ImGui::GetContentRegionAvail();
	const ImVec2 RegionMax(Origin.x + Avail.x, Origin.y + Avail.y);

	DL->AddRectFilled(Origin, RegionMax, IM_COL32(28, 28, 32, 255));

	if (!Canvas)
	{
		ImGui::TextDisabled("No canvas");
		return;
	}

	// 입력 캡처 표면 — 뷰포트 위 좌클릭/드래그를 InvisibleButton 이 흡수해 ImGui 창 이동으로
	// 전파되지 않게 한다(사이클 ⑦). 영역 점유도 겸하므로 끝의 별도 Dummy 불필요.
	if (Avail.x > 0.0f && Avail.y > 0.0f)
	{
		ImGui::InvisibleButton("##UIViewportSurface", Avail, ImGuiButtonFlags_MouseButtonLeft);
	}
	const bool     bHovered = ImGui::IsItemHovered();
	const bool     bActive  = ImGui::IsItemActive();
	const ImGuiIO& IO       = ImGui::GetIO();

	// 휠 줌(표면 호버 시). 기본 스케일 = 뷰포트 높이를 레퍼런스 1080 에 맞춤(진단 §C).
	if (bHovered && IO.MouseWheel != 0.0f)
	{
		ViewportZoom *= (1.0f + IO.MouseWheel * 0.1f);
		if (ViewportZoom < 0.1f) ViewportZoom = 0.1f;
		if (ViewportZoom > 5.0f) ViewportZoom = 5.0f;
	}

	const float RefW  = 1920.0f;
	const float RefH  = 1080.0f;
	const float Scale = ((Avail.y > 0.0f) ? (Avail.y / RefH) : 1.0f) * ViewportZoom;

	// 단일 캔버스 레이아웃(전역 레지스트리 미사용 seam) → 각 요소 ScreenRect 갱신.
	FUICanvasManager::Get().LayoutCanvas(Canvas, Scale);

	// 프레스 순간 → 히트테스트로 선택(빈 곳은 해제). 좌표는 캔버스 원점 기준 역변환(진단 §C/E, 사이클 ④).
	if (ImGui::IsItemActivated())
	{
		const ImVec2 M = ImGui::GetMousePos();
		Selected = FUICanvasManager::Get().HitTestCanvas(Canvas, FVector2(M.x - Origin.x, M.y - Origin.y));
	}

	// 드래그 → 선택 요소 이동. 화면 델타를 Scale 로 나눠 레퍼런스 공간 델타로(사이클 7 TickEditor 로직 재사용).
	if (bActive && Selected && (IO.MouseDelta.x != 0.0f || IO.MouseDelta.y != 0.0f))
	{
		const float S = (Scale > 0.0f) ? Scale : 1.0f;
		Selected->SetPosition(Selected->GetPosition() + FVector2(IO.MouseDelta.x / S, IO.MouseDelta.y / S));
		MarkDirty();
	}

	// 레퍼런스 해상도(1920x1080) 경계 + 그리드.
	const ImVec2 RefMax(Origin.x + RefW * Scale, Origin.y + RefH * Scale);
	const float  Step = 120.0f * Scale;
	if (Step >= 4.0f)
	{
		for (float x = Origin.x; x <= RefMax.x && x <= RegionMax.x; x += Step)
			DL->AddLine(ImVec2(x, Origin.y), ImVec2(x, (RefMax.y < RegionMax.y ? RefMax.y : RegionMax.y)), IM_COL32(55, 55, 62, 255));
		for (float y = Origin.y; y <= RefMax.y && y <= RegionMax.y; y += Step)
			DL->AddLine(ImVec2(Origin.x, y), ImVec2((RefMax.x < RegionMax.x ? RefMax.x : RegionMax.x), y), IM_COL32(55, 55, 62, 255));
	}
	DL->AddRect(Origin, RefMax, IM_COL32(120, 120, 135, 255));

	// 요소 드로우(뷰포트 영역 클리핑) + 선택 강조.
	DL->PushClipRect(Origin, RegionMax, true);
	FUICanvasMirror::DrawElement(Canvas, DL, Origin, Scale);
	if (Selected)
	{
		const FUIRect& SR = Selected->GetScreenRect();
		const ImVec2   SMin(Origin.x + SR.Pos.X, Origin.y + SR.Pos.Y);
		const ImVec2   SMax(SMin.x + SR.Size.X, SMin.y + SR.Size.Y);
		// 선택 강조 테두리도 요소의 둥글기를 따라가게 한다(요소와 동일한 레퍼런스 px*Scale 환산).
		const float SRounding = Selected->GetEffectiveCornerRadius() * Scale;
		DL->AddRect(SMin, SMax, IM_COL32(255, 180, 40, 255), SRounding, 0, 2.0f);
	}
	DL->PopClipRect();

	DL->AddText(ImVec2(Origin.x + 6.0f, Origin.y + 6.0f), IM_COL32(170, 170, 180, 255), "Canvas Viewport (drag: move, wheel: zoom)");
}

void FUIEditorWidget::RenderDetailsPanel()
{
	ImGui::TextUnformatted("Details");
	ImGui::Separator();
	if (!Selected)
	{
		ImGui::TextDisabled("No selection");
		return;
	}

	ImGui::TextDisabled("%s", Selected->GetClass()->GetName());
	ImGui::SameLine();

	// 선택 요소(+서브트리) 삭제 — SpawnElement 의 역연산. 루트 캔버스는 비울 수 없으므로 비활성.
	const bool bIsRoot = (Selected == Canvas);
	if (bIsRoot)
	{
		ImGui::BeginDisabled();
	}
	ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(150, 45, 45, 255));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(185, 55, 55, 255));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(120, 35, 35, 255));
	const bool bDeleteClicked = ImGui::Button("Delete");
	ImGui::PopStyleColor(3);
	if (bIsRoot)
	{
		ImGui::EndDisabled();
	}

	// Delete/Backspace 단축키 — 디테일 패널 포커스 시(루트 제외). 버튼과 동일 경로.
	// 단, 텍스트 입력 필드(ElementName/Text 등) 편집 중에는 발동 금지 — WantTextInput 이 참이면
	// Backspace/Delete 는 글자 수정용이므로, 요소 삭제로 가로채면 안 된다. 삭제는 Delete 버튼으로.
	const bool bDeleteKey = !bIsRoot && ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput
		&& (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace));
	if (bDeleteClicked || bDeleteKey)
	{
		DeleteSelected();
		return;   // Selected 가 무효화됨 — 이번 프레임 나머지 디테일 바인딩 건너뜀.
	}

	ImGui::Spacing();

	// 요소 식별자 — 런타임 데이터 바인딩(체력바 등)이 캔버스 루트에서 이 이름으로 대상을 찾는다
	// (UUIElement::FindByName). 빈 이름은 바인딩 대상에서 제외. Save 되어 .uasset 에 영속.
	{
		char NameBuf[128];
		snprintf(NameBuf, sizeof(NameBuf), "%s", Selected->GetElementName().c_str());
		if (ImGui::InputText("Element Name", NameBuf, sizeof(NameBuf)))
		{
			Selected->SetElementName(FString(NameBuf));
			MarkDirty();
		}
	}

	ImGui::Spacing();

	// [show/off] 요소 표시 토글 — 끄면 이 요소와 하위 트리가 뷰포트/런타임에서 숨겨진다(.uasset Save).
	{
		bool bElemVisible = Selected->IsVisible();
		if (ImGui::Checkbox("Visible", &bElemVisible))
		{
			Selected->SetVisible(bElemVisible);
			MarkDirty();
		}
	}

	// 공통 RectTransform/Color 직접 바인딩 + (텍스트 요소면) 텍스트 5필드. 편집 즉시 멤버 반영 →
	// 다음 프레임 LayoutCanvas 가 ScreenRect 갱신 → 뷰포트 실시간 반영(텍스트는 아래 ImGui 미러).
	FUIRectTransform& RT = Selected->GetRectTransform();

	float Size[2] = { RT.Size.X, RT.Size.Y };
	if (ImGui::DragFloat2("Size (W/H)", Size, 1.0f))
	{
		RT.Size = FVector2(Size[0], Size[1]);
		MarkDirty();
	}

	float Pos[2] = { RT.Position.X, RT.Position.Y };
	if (ImGui::DragFloat2("Offset (X/Y)", Pos, 1.0f))
	{
		RT.Position = FVector2(Pos[0], Pos[1]);
		MarkDirty();
	}

	float Pivot[2] = { RT.Pivot.X, RT.Pivot.Y };
	if (ImGui::DragFloat2("Pivot", Pivot, 0.01f))
	{
		RT.Pivot = FVector2(Pivot[0], Pivot[1]);
		MarkDirty();
	}

	FVector4 C      = Selected->GetColor();
	float    Col[4] = { C.R, C.G, C.B, C.A };
	if (ImGui::ColorEdit4("Color", Col))
	{
		Selected->SetColor(FVector4(Col[0], Col[1], Col[2], Col[3]));
		MarkDirty();
	}

	// 외곽 모양(Shape) — Rectangle(직각, CornerRadius 사용) ↔ Circle(짧은 변 절반 반지름의 완전 둥근꼴).
	// 변경 즉시 멤버 반영 → 다음 프레임 미러/런타임이 GetEffectiveCornerRadius 로 동일 모양 렌더(.uasset Save).
	{
		const char* ShapeNames[] = { "Rectangle", "Circle" };
		int ShapeIdx = static_cast<int>(Selected->GetShape());
		if (ImGui::Combo("Shape", &ShapeIdx, ShapeNames, IM_ARRAYSIZE(ShapeNames)))
		{
			Selected->SetShape(static_cast<EUIElementShape>(ShapeIdx));
			MarkDirty();
		}
	}

	// 모서리 둥글기(레퍼런스 px). 0=직각. 드로우 패스가 변의 절반까지 클램프하므로 상한은 넉넉히 둔다.
	// Circle 모양에선 반지름이 자동(짧은 변 절반)이라 이 값이 무시되므로 입력을 비활성화해 혼동을 막는다.
	const bool bCircleShape = (Selected->GetShape() == EUIElementShape::Circle);
	if (bCircleShape) { ImGui::BeginDisabled(); }
	float Radius = Selected->GetCornerRadius();
	if (ImGui::DragFloat("Corner Radius", &Radius, 0.5f, 0.0f, 512.0f, "%.1f"))
	{
		Selected->SetCornerRadius(Radius);
		MarkDirty();
	}
	if (bCircleShape) { ImGui::EndDisabled(); }

	// 이미지 요소 전용 — Content/UI/Images/ 안의 png/jpg/jpeg 파일을 확장자로 인식해 드롭다운으로 띄운다.
	// 선택 시 "Content/UI/Images/<파일명>"(프로젝트 상대) 저장 → 스탠드얼론 빌드에서 그대로 해석.
	// 런타임/PIE 는 ResolveTextureSRV → SimpleUIPass 가 텍스처×Color 로 렌더, 에디터는 뷰포트 미러로 표시.
	// UUIImage 는 UUITextElement 의 하위라 아래 텍스트 5필드(오버레이 텍스트)도 함께 노출된다.
	if (UUIImage* ImgElem = Cast<UUIImage>(Selected))
	{
		ImGui::Spacing();
		ImGui::TextDisabled("Image");
		ImGui::Separator();

		// 콤보 프리뷰 = 현재 경로의 파일명만(없으면 "(None)"). 목록 항목과 표기 일치.
		const FString CurPath = ImgElem->GetTexturePath();
		FString Preview = "(None)";
		if (!CurPath.empty())
		{
			Preview = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(CurPath)).filename().wstring());
		}

		if (ImGui::BeginCombo("Texture", Preview.c_str()))
		{
			// 선택 해제(단색 쿼드로 복귀).
			if (ImGui::Selectable("(None)", CurPath.empty()))
			{
				ImgElem->SetTexturePath(FString());
				MarkDirty();
			}
			// 디렉터리에 실제로 존재하는 png/jpg/jpeg 만 나열(드롭다운 열릴 때만 스캔).
			for (const FString& File : CollectUIImageFiles())
			{
				const FString Rel  = FString("Content/UI/Images/") + File;
				const bool    bSel = (Rel == CurPath);
				if (ImGui::Selectable(File.c_str(), bSel))
				{
					ImgElem->SetTexturePath(Rel);
					MarkDirty();
				}
				if (bSel)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}

	// 텍스트 5필드 — 중간 클래스 UUITextElement(Button/Image/Label)에만 노출. Canvas/Group(순수
	// UUIElement)은 캐스트 실패 → 표시 안 함. Color(BackgroundColor)와 별개의 Text Color.
	if (UUITextElement* TextElem = Cast<UUITextElement>(Selected))
	{
		ImGui::Spacing();
		ImGui::TextDisabled("Text");
		ImGui::Separator();

		char TextBuf[256];
		snprintf(TextBuf, sizeof(TextBuf), "%s", TextElem->GetText().c_str());
		if (ImGui::InputText("Text", TextBuf, sizeof(TextBuf)))
		{
			TextElem->SetText(FString(TextBuf));
			MarkDirty();
		}

		float FontSize = TextElem->GetFontSize();
		if (ImGui::DragFloat("Font Size", &FontSize, 0.5f, 1.0f, 512.0f, "%.0f"))
		{
			TextElem->SetFontSize(FontSize);
			MarkDirty();
		}

		// Font Weight / Text Align — 자유 입력 대신 유효 값만 순환하는 toggle 버튼(클릭 시 다음 값).
		FString NextWeight;
		if (CycleButton("Font Weight", kFontWeightOptions, 2, TextElem->GetFontWeight(), NextWeight))
		{
			TextElem->SetFontWeight(NextWeight);
			MarkDirty();
		}

		FString NextAlign;
		if (CycleButton("Text Align", kTextAlignOptions, 3, TextElem->GetTextAlign(), NextAlign))
		{
			TextElem->SetTextAlign(NextAlign);
			MarkDirty();
		}

		FVector4 TC      = TextElem->GetTextColor();
		float    TCol[4] = { TC.R, TC.G, TC.B, TC.A };
		if (ImGui::ColorEdit4("Text Color", TCol))
		{
			TextElem->SetTextColor(FVector4(TCol[0], TCol[1], TCol[2], TCol[3]));
			MarkDirty();
		}
	}

	// [버튼 액션] UUIButton 전용 — 클릭 시 실행할 액션 배열(다중). 각 행 = 액션 종류 + 대상/파라미터.
	// 런타임(PIE/게임)에서 FUICanvasManager 의 클릭 디스패처가 순서대로 실행한다(.uasset 에 영속).
	if (UUIButton* BtnElem = Cast<UUIButton>(Selected))
	{
		ImGui::Spacing();
		ImGui::TextDisabled("On Click Actions");
		ImGui::Separator();

		static const char* const kActionNames[] = {
			"None", "ChangeScene", "ShowElement", "HideElement", "ToggleElement", "SetImage", "CallLua"
		};
		TArray<FUIButtonAction>& Actions = BtnElem->GetOnClickActionsMutable();
		int RemoveIdx = -1;
		for (int i = 0; i < static_cast<int>(Actions.size()); ++i)
		{
			ImGui::PushID(i);
			FUIButtonAction& A = Actions[i];

			int Cur = static_cast<int>(A.Action);
			ImGui::SetNextItemWidth(150.0f);
			if (ImGui::Combo("##Action", &Cur, kActionNames, IM_ARRAYSIZE(kActionNames)))
			{
				A.Action = static_cast<EUIButtonAction>(Cur);
				MarkDirty();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove")) { RemoveIdx = i; }

			// 액션별 파라미터 — 모두 토글(콤보)로 선택. 파일 기반 액션은 관련 확장자만 노출.
			switch (A.Action)
			{
			case EUIButtonAction::ChangeScene:
				// Content/Scene 의 .Scene 파일만.
				if (StringCombo("Scene", CollectFilesWithExt(FPaths::SceneDir(), { L".scene" }), A.Target)) { MarkDirty(); }
				break;
			case EUIButtonAction::ShowElement:
			case EUIButtonAction::HideElement:
			case EUIButtonAction::ToggleElement:
			{
				// 캔버스의 명명된 요소 후보.
				TArray<FString> Names;
				CollectElementNames(Canvas, Names);
				if (StringCombo("Target Element", Names, A.Target)) { MarkDirty(); }
				break;
			}
			case EUIButtonAction::SetImage:
			{
				TArray<FString> Names;
				CollectElementNames(Canvas, Names);
				if (StringCombo("Target Element", Names, A.Target)) { MarkDirty(); }
				// 이미지 — Content/UI/Images 의 png/jpg/jpeg. 저장값은 프로젝트 상대 경로(표시값=파일명).
				const FString ImgPreview = A.Param.empty()
					? FString("(None)")
					: FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(A.Param)).filename().wstring());
				if (ImGui::BeginCombo("Image", ImgPreview.c_str()))
				{
					if (ImGui::Selectable("(None)", A.Param.empty())) { A.Param.clear(); MarkDirty(); }
					for (const FString& File : CollectUIImageFiles())
					{
						const FString Rel = FString("Content/UI/Images/") + File;
						if (ImGui::Selectable(File.c_str(), Rel == A.Param)) { A.Param = Rel; MarkDirty(); }
					}
					ImGui::EndCombo();
				}
				break;
			}
			case EUIButtonAction::CallLua:
				// Content/Script 의 .lua 파일만.
				if (StringCombo("Lua Script", CollectFilesWithExt(FPaths::ScriptDir(), { L".lua" }), A.Target)) { MarkDirty(); }
				break;
			case EUIButtonAction::None:
			default:
				break;
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (RemoveIdx >= 0) { Actions.erase(Actions.begin() + RemoveIdx); MarkDirty(); }
		if (ImGui::Button("+ Add Action")) { Actions.emplace_back(); MarkDirty(); }
	}
}
