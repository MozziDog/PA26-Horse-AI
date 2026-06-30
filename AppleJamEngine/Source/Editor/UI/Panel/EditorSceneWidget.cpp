#include "Editor/UI/Panel/EditorSceneWidget.h"

#include "Editor/EditorEngine.h"
#include "Editor/Viewport/Level/LevelEditorViewportClient.h"
#include "Engine/Input/InputSystem.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/FName.h"
#include "Serialization/SceneSaveManager.h"

#include "ImGui/imgui.h"
#include "Profiling/Stats/Stats.h"

#include <cstring>

void FEditorSceneWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
}

void FEditorSceneWidget::Render(float DeltaTime)
{
	if (!EditorEngine)
	{
		return;
	}

	(void)DeltaTime;
	ImGui::SetNextWindowSize(ImVec2(400.0f, 350.0f), ImGuiCond_Once);

	ImGui::Begin("Outliner");

	// 씬 파일 작업은 상단 메뉴로 옮기고, Scene Manager는 액터 목록만 유지한다.
	RenderActorOutliner();
	RenderRenamePopup();
	HandleSceneManagerShortcuts();

	ImGui::End();
}

void FEditorSceneWidget::HandleSceneManagerShortcuts()
{
	if (ImGui::GetIO().WantTextInput)
	{
		return;
	}

	if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		return;
	}

	FSelectionManager& Selection = EditorEngine->GetSelectionManager();
	InputSystem& Input = InputSystem::Get();

	if (Input.GetKeyDown(VK_DELETE))
	{
		Selection.DeleteSelectedActors();
		return;
	}

	if (Input.GetKeyDown('F'))
	{
		if (FLevelEditorViewportClient* ActiveViewport = EditorEngine->GetActiveViewport())
		{
			ActiveViewport->FocusOnPrimarySelection();
		}
	}
}

void FEditorSceneWidget::RenderActorOutliner()
{
	SCOPE_STAT_CAT("SceneWidget::ActorOutliner", "5_UI");

	UWorld* World = EditorEngine->GetWorld();
	if (!World) return;

	TArray<AActor*> Actors = World->GetActors();

	// null이 아닌 유효 Actor 인덱스만 수집 (Clipper는 연속 인덱스 필요)
	ValidActorIndices.clear();
	ValidActorIndices.reserve(Actors.size());
	for (int32 i = 0; i < static_cast<int32>(Actors.size()); ++i)
	{
		if (Actors[i]) ValidActorIndices.push_back(i);
	}

	ImGui::Text("Actors (%d)", static_cast<int32>(ValidActorIndices.size()));
	ImGui::Separator();

	FSelectionManager& Selection = EditorEngine->GetSelectionManager();

	ImGui::BeginChild("ActorList", ImVec2(0, 0), ImGuiChildFlags_Borders);

	AActor* PendingRemoveActor = nullptr;

	ImGuiListClipper Clipper;
	Clipper.Begin(static_cast<int>(ValidActorIndices.size()));
	while (Clipper.Step())
	{
		for (int Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; ++Row)
		{
			AActor* Actor = Actors[ValidActorIndices[Row]];

			const FString StoredName = Actor->GetFName().ToString();
			const char* DisplayName = StoredName.empty()
				? Actor->GetClass()->GetName()
				: StoredName.c_str();

			bool bIsSelected = Selection.IsSelected(Actor);
			ImGui::PushID(Actor);
			if (ImGui::Selectable(DisplayName, bIsSelected))
			{
				if (ImGui::GetIO().KeyShift)
				{
					Selection.SelectRange(Actor, Actors);
				}
				else if (ImGui::GetIO().KeyCtrl)
				{
					Selection.ToggleSelect(Actor);
				}
				else
				{
					Selection.Select(Actor);
				}
			}
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Rename"))
				{
					BeginRenameActor(Actor);
				}
				if (ImGui::MenuItem("Create Prefab"))
				{
					CreatePrefabFromActor(Actor);
				}
				if (ImGui::MenuItem("Remove"))
				{
					PendingRemoveActor = Actor;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
	}

	ImGui::EndChild();

	if (PendingRemoveActor)
	{
		// 편집 모드의 AUICanvasActor 는 RootComponent(UUICanvas)가 BeginPlay 에서만 생겨 에디터에선
		// 없다 → Select() 가 거부(ClearSelection)하므로 기존 Select→DeleteSelected 경로로는 안 지워졌다.
		// 선택 의존 없이 직접 파괴해 mesh 없는(뷰포트 picking 불가) UI 액터도 Outliner 에서 제거 가능.
		Selection.DeleteActor(PendingRemoveActor);
	}
}

void FEditorSceneWidget::CreatePrefabFromActor(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}

	// 액터 + 컴포넌트 트리를 직렬화해 Content/prefab/<액터이름>.uasset 으로 저장한다.
	// 컴포넌트 간 참조는 로컬 ObjectId 로 보존된다(FSceneSaveManager 가 처리).
	// 경로/디렉토리 생성은 SaveActorAsPrefab → MakePrefabPackagePath 가 담당.
	FSceneSaveManager::SaveActorAsPrefab(Actor);
}

void FEditorSceneWidget::BeginRenameActor(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}

	RenameTargetActor = Actor;
	bShowRenameDuplicateWarning = false;

	const FString CurrentName = Actor->GetFName().ToString();
	strncpy_s(RenameBuffer, sizeof(RenameBuffer), CurrentName.c_str(), _TRUNCATE);

	bRenamePopupRequested = true;
	bFocusRenameInputNextFrame = true;
}

void FEditorSceneWidget::RenderRenamePopup()
{
	if (bRenamePopupRequested)
	{
		ImGui::OpenPopup("Rename Actor");
		bRenamePopupRequested = false;
	}

	if (ImGui::BeginPopupModal("Rename Actor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (!IsValid(RenameTargetActor))
		{
			RenameTargetActor = nullptr;
			RenameBuffer[0] = '\0';
			bShowRenameDuplicateWarning = false;
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		ImGui::TextUnformatted("Actor Name");
		ImGui::SetNextItemWidth(320.0f);

		if (bFocusRenameInputNextFrame)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusRenameInputNextFrame = false;
		}

		const bool bSubmit = ImGui::InputText("##RenameInput", RenameBuffer, sizeof(RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

		if (bShowRenameDuplicateWarning)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Already used name.");
		}

		const bool bApply = bSubmit || ImGui::Button("OK");
		ImGui::SameLine();
		const bool bCancel = ImGui::Button("Cancel");

		if (bApply)
		{
			if (TryRenameActor(RenameTargetActor, FString(RenameBuffer)))
			{
				RenameTargetActor = nullptr;
				RenameBuffer[0] = '\0';
				bShowRenameDuplicateWarning = false;
				ImGui::CloseCurrentPopup();
			}
		}
		else if (bCancel)
		{
			RenameTargetActor = nullptr;
			RenameBuffer[0] = '\0';
			bShowRenameDuplicateWarning = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

bool FEditorSceneWidget::TryRenameActor(AActor* Actor, const FString& NewName)
{
	if (!IsValid(Actor))
	{
		return false;
	}

	const FString CurrentName = Actor->GetFName().ToString();
	if (NewName == CurrentName)
	{
		return true;
	}

	bShowRenameDuplicateWarning = false;

	UWorld* World = EditorEngine ? EditorEngine->GetWorld() : nullptr;
	if (World)
	{
		for (AActor* OtherActor : World->GetActors())
		{
			if (!IsValid(OtherActor) || OtherActor == Actor)
			{
				continue;
			}
			if (OtherActor->GetFName().ToString() == NewName)
			{
				bShowRenameDuplicateWarning = true;
				return false;
			}
		}
	}

	Actor->SetFName(FName(NewName));
	return true;
}
