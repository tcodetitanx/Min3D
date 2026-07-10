#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateBrush.h"
#include "MinTypes.h"

class AMinPlayerController;
class UTexture2D;

// In-game HUD: resource bar, plane tabs, tool palette, contract panel, toasts.
class SMinHUD : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMinHUD) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, AMinPlayerController* InPC);

private:
	AMinPlayerController* PC = nullptr;
	TArray<TSharedPtr<FSlateBrush>> Brushes; // keep icon brushes alive
	TSharedPtr<FSlateBrush> MakeIconBrush(const FString& SpriteId, float Size);

	TSharedRef<SWidget> BuildResourceBar();
	TSharedRef<SWidget> BuildPlaneTabs();
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildContractPanel();
	TSharedRef<SWidget> BuildStatusArea();
	TSharedRef<SWidget> BuildTutorialPanel();
};

// Title screen overlay.
class SMinTitle : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMinTitle) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, AMinPlayerController* InPC);

private:
	AMinPlayerController* PC = nullptr;
};
