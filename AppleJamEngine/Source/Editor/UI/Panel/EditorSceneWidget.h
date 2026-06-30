#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Core/Types/CoreTypes.h"

class FEditorSceneWidget : public FEditorWidget
{
public:
	virtual void Initialize(UEditorEngine* InEditorEngine) override;
	virtual void Render(float DeltaTime) override;

private:
	void RenderActorOutliner();
	void BeginRenameActor(class AActor* Actor);
	void RenderRenamePopup();
	bool TryRenameActor(class AActor* Actor, const FString& NewName);
	void HandleSceneManagerShortcuts();
	void CreatePrefabFromActor(class AActor* Actor);

	TArray<int32> ValidActorIndices;
	class AActor* RenameTargetActor = nullptr;
	char RenameBuffer[256] = {};
	bool bRenamePopupRequested = false;
	bool bFocusRenameInputNextFrame = false;
	bool bShowRenameDuplicateWarning = false;
};
