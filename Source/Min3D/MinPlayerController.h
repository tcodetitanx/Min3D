#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MinTypes.h"
#include "MinPlayerController.generated.h"

class AMinPawn;
class AMinGameMode;
class UMinSim;
class UMinAudio;
class SMinHUD;
class SMinTitle;

UCLASS()
class AMinPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMinPlayerController();
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void BeginPlay() override;

	void SwitchPlane(int32 Delta);
	void JumpToPlane(int32 PlaneInView); // 0..2 within the current orientation
	void ToggleSideView();               // the perspective shift
	void ToggleMatrix();
	void SetTool(EMinTool InTool);

	// run lifecycle
	void StartNewRun(EMinGameRules Rules); // from the title screen
	void RestartRun();                     // after game over: fresh seed, same rules
	void BackToTitle();
	void QuitGame();

	bool GetMouseCell(FIntPoint& OutCell) const;

	EMinTool Tool = EMinTool::None;
	bool bInTitle = true;

	// live status line for the HUD (cost previews, hints)
	FString StatusLine;
	FString ToastLine;
	float ToastTimer = 0.f;

	AMinPawn* GetMinPawn() const;
	AMinGameMode* GetGM() const;
	UMinSim* GetSim() const;

private:
	UMinAudio* GetAudio() const;
	void OnZoomIn();
	void OnZoomOut();
	void OnPlanePrev() { SwitchPlane(-1); }
	void OnPlaneNext() { SwitchPlane(1); }
	void OnLMBDown();
	void OnLMBUp();
	void OnRMBDown();
	void OnTogglePause();
	void OnSpeed();
	void OnEscape();
	void UpdateDragPath();
	void UpdatePreview();
	void ShowToast(const FString& Msg);

	bool bDraggingRoute = false;
	bool bErasing = false;
	TArray<FIntPoint> DragPath;
	int32 DragPlane = 0;

	bool bDraggingView = false;
	FVector2D LastMouse = FVector2D::ZeroVector;

	TSharedPtr<SMinHUD> HUD;
	TSharedPtr<SMinTitle> Title;
};
