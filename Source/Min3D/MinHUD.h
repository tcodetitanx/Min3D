#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateBrush.h"
#include "MinTypes.h"

class AMinPlayerController;
class UTexture2D;

// In-game HUD: score, segment stock, plane tabs, tools, warnings, game over.
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

	TSharedRef<SWidget> BuildScorePanel();
	TSharedRef<SWidget> BuildPlaneTabs();
	TSharedRef<SWidget> BuildRightPanel();
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildStatusArea();
	TSharedRef<SWidget> BuildGameOverOverlay();
};

// Title screen overlay with mode selection.
class SMinTitle : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMinTitle) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, AMinPlayerController* InPC);

private:
	AMinPlayerController* PC = nullptr;
};
