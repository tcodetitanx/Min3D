#include "MinHUD.h"
#include "Min3D.h"
#include "MinPlayerController.h"
#include "MinPawn.h"
#include "MinGameMode.h"
#include "MinSim.h"
#include "MinAssets.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/CoreStyle.h"

namespace
{
	const FLinearColor PanelBg(0.06f, 0.05f, 0.05f, 0.62f);
	const FLinearColor PanelBgSoft(0.06f, 0.05f, 0.05f, 0.42f);
	const FLinearColor Cream(0.96f, 0.93f, 0.86f);
	const FLinearColor Accent(0.55f, 0.83f, 0.78f);
	const FLinearColor AccentWarm(0.95f, 0.75f, 0.35f);
	const FLinearColor Danger(1.f, 0.45f, 0.35f);

	FSlateFontInfo Font(int32 Size, const FName Weight = "Bold")
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}

	int32 ReadHighScore(EMinGameRules Rules)
	{
		FString In;
		const FString Path = FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("min3d_scores.json");
		if (!FFileHelper::LoadFileToString(In, *Path)) return 0;
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(In), Root) || !Root.IsValid()) return 0;
		double V = 0;
		Root->TryGetNumberField(Rules == EMinGameRules::SinglePlane ? TEXT("single") : TEXT("cross"), V);
		return (int32)V;
	}

	FString Clock(float Seconds)
	{
		const int32 S = FMath::Max(0, FMath::FloorToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), S / 60, S % 60);
	}
}

TSharedPtr<FSlateBrush> SMinHUD::MakeIconBrush(const FString& SpriteId, float Size)
{
	TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	if (PC && PC->GetGameInstance())
	{
		if (UMinAssets* Assets = PC->GetGameInstance()->GetSubsystem<UMinAssets>())
		{
			if (UTexture2D* Tex = Assets->GetSprite(SpriteId))
			{
				Brush->SetResourceObject(Tex);
				const FVector2D Src = Assets->GetSpriteSize(SpriteId);
				const float Aspect = Src.Y > 0 ? Src.X / Src.Y : 1.f;
				Brush->ImageSize = FVector2D(Size * Aspect, Size);
			}
		}
	}
	Brushes.Add(Brush);
	return Brush;
}

void SMinHUD::Construct(const FArguments& InArgs, AMinPlayerController* InPC)
{
	PC = InPC;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Top).Padding(10, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[ BuildScorePanel() ]
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center).VAlign(VAlign_Top)
			[ BuildPlaneTabs() ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[ BuildRightPanel() ]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 12)
		[ BuildToolbar() ]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(12, 0, 0, 14)
		[ BuildStatusArea() ]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[ BuildGameOverOverlay() ]
	];
	SetVisibility(TAttribute<EVisibility>::CreateLambda([this]()
	{
		return PC && !PC->bInTitle ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
	}));
}

TSharedRef<SWidget> SMinHUD::BuildScorePanel()
{
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(PanelBg)
		.Padding(FMargin(14, 6))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(Font(24)).ColorAndOpacity(AccentWarm)
				.Text(TAttribute<FText>::CreateLambda([this]()
				{
					UMinSim* Sim = PC ? PC->GetSim() : nullptr;
					return FText::FromString(FString::Printf(TEXT("%d"), Sim ? Sim->Score : 0));
				}))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(10, 0, 0, 3)
			[
				SNew(STextBlock).Font(Font(11, "Regular")).ColorAndOpacity(FLinearColor(0.8f, 0.78f, 0.72f))
				.Text(TAttribute<FText>::CreateLambda([this]()
				{
					UMinSim* Sim = PC ? PC->GetSim() : nullptr;
					return FText::FromString(FString::Printf(TEXT("BEST %d"), Sim ? Sim->HighScore : 0));
				}))
			]
		];
}

TSharedRef<SWidget> SMinHUD::BuildPlaneTabs()
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	// orientation toggle: the perspective shift between front and side grids
	Row->AddSlot().AutoWidth().Padding(2, 0, 10, 0)
	[
		SNew(SButton)
		.ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this]()
		{
			AMinPawn* Pawn = PC ? PC->GetMinPawn() : nullptr;
			return Pawn && Pawn->bSideView ? FSlateColor(AccentWarm) : FSlateColor(FLinearColor(0.25f, 0.24f, 0.22f, 0.9f));
		}))
		.OnClicked_Lambda([this]() { if (PC) PC->ToggleSideView(); return FReply::Handled(); })
		[
			SNew(STextBlock).Font(Font(13))
			.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this]()
			{
				AMinPawn* Pawn = PC ? PC->GetMinPawn() : nullptr;
				return Pawn && Pawn->bSideView ? FSlateColor(FLinearColor(0.1f, 0.08f, 0.05f)) : FSlateColor(Cream);
			}))
			.Text(TAttribute<FText>::CreateLambda([this]()
			{
				AMinPawn* Pawn = PC ? PC->GetMinPawn() : nullptr;
				return FText::FromString(Pawn && Pawn->bSideView ? TEXT("SIDE [Tab]") : TEXT("FRONT [Tab]"));
			}))
		]
	];

	for (int32 P = 0; P < 3; ++P)
	{
		const int32 Idx = P;
		Row->AddSlot().AutoWidth().Padding(2, 0)
		[
			SNew(SButton)
			.ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this, Idx]()
			{
				AMinPawn* Pawn = PC ? PC->GetMinPawn() : nullptr;
				const bool bCur = Pawn && (Pawn->bSideView ? Pawn->ViewSide : Pawn->ViewFront) == Idx;
				return bCur ? FSlateColor(Accent) : FSlateColor(FLinearColor(0.25f, 0.24f, 0.22f, 0.9f));
			}))
			.OnClicked_Lambda([this, Idx]()
			{
				if (PC) PC->JumpToPlane(Idx);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font(Font(13))
				.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this, Idx]()
				{
					AMinPawn* Pawn = PC ? PC->GetMinPawn() : nullptr;
					const bool bCur = Pawn && (Pawn->bSideView ? Pawn->ViewSide : Pawn->ViewFront) == Idx;
					return bCur ? FSlateColor(FLinearColor(0.05f, 0.08f, 0.08f)) : FSlateColor(Cream);
				}))
				.Text(TAttribute<FText>::CreateLambda([this, Idx]()
				{
					AMinPawn* Pawn = PC ? PC->GetMinPawn() : nullptr;
					const bool bSide = Pawn && Pawn->bSideView;
					return FText::FromString(Min::PlaneName(bSide ? Min::NumFront + Idx : Idx));
				}))
			]
		];
	}

	Row->AddSlot().AutoWidth().Padding(10, 0, 2, 0)
	[
		SNew(SButton)
		.ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this]()
		{
			AMinPawn* Pawn = PC ? PC->GetMinPawn() : nullptr;
			return Pawn && Pawn->bMatrixView ? FSlateColor(Accent) : FSlateColor(FLinearColor(0.25f, 0.24f, 0.22f, 0.9f));
		}))
		.OnClicked_Lambda([this]() { if (PC) PC->ToggleMatrix(); return FReply::Handled(); })
		[
			SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Cream)
			.Text(FText::FromString(TEXT("3D [M]")))
		]
	];

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(PanelBgSoft)
		.Padding(FMargin(6, 4))
		[ Row ];
}

TSharedRef<SWidget> SMinHUD::BuildRightPanel()
{
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(PanelBg)
		.Padding(FMargin(12, 6))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(SImage).Image(MakeIconBrush(TEXT("ico_03_01"), 22.f).Get())
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(Font(16)).ColorAndOpacity(Cream)
				.Text(TAttribute<FText>::CreateLambda([this]()
				{
					UMinSim* Sim = PC ? PC->GetSim() : nullptr;
					return FText::FromString(FString::Printf(TEXT("%d"), Sim ? Sim->SegmentsLeft : 0));
				}))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14, 0, 0, 0)
			[
				SNew(STextBlock).Font(Font(14)).ColorAndOpacity(FLinearColor(0.8f, 0.78f, 0.72f))
				.Text(TAttribute<FText>::CreateLambda([this]()
				{
					UMinSim* Sim = PC ? PC->GetSim() : nullptr;
					FString S = Sim ? Clock(Sim->PlayTime) : TEXT("0:00");
					if (Sim && Sim->SimSpeed > 1.f) S += FString::Printf(TEXT("  x%.0f"), Sim->SimSpeed);
					if (Sim && Sim->bPaused) S += TEXT("  ||");
					return FText::FromString(S);
				}))
			]
		];
}

TSharedRef<SWidget> SMinHUD::BuildToolbar()
{
	struct FToolDef { EMinTool Tool; const TCHAR* Key; const TCHAR* Icon; const TCHAR* Label; };
	static const FToolDef Tools[] = {
		{ EMinTool::Tunnel, TEXT("1"), TEXT("ico_03_01"), TEXT("Tunnel") },
		{ EMinTool::Erase, TEXT("X"), TEXT("ico_07_03"), TEXT("Erase") },
	};

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (const FToolDef& T : Tools)
	{
		const EMinTool Tool = T.Tool;
		Row->AddSlot().AutoWidth().Padding(3, 0)
		[
			SNew(SButton)
			.ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this, Tool]()
			{
				const bool bActive = PC && PC->Tool == Tool;
				return bActive ? FSlateColor(Accent) : FSlateColor(FLinearColor(0.22f, 0.21f, 0.20f, 0.92f));
			}))
			.OnClicked_Lambda([this, Tool]()
			{
				if (PC) PC->SetTool(Tool);
				return FReply::Handled();
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(2, 2, 2, 0)
				[
					SNew(SBox).WidthOverride(40).HeightOverride(40)
					[
						SNew(SImage).Image(MakeIconBrush(T.Icon, 38.f).Get())
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Cream)
					.Text(FText::FromString(FString::Printf(TEXT("%s %s"), T.Key, T.Label)))
				]
			]
		];
	}

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(PanelBg)
		.Padding(FMargin(8, 6))
		[ Row ];
}

TSharedRef<SWidget> SMinHUD::BuildStatusArea()
{
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
	[
		SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Danger)
		.Text(TAttribute<FText>::CreateLambda([this]()
		{
			UMinSim* Sim = PC ? PC->GetSim() : nullptr;
			return FText::FromString(Sim ? Sim->CurrentWarning : FString());
		}))
	]
	+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
	[
		SNew(STextBlock).Font(Font(13)).ColorAndOpacity(AccentWarm)
		.Text(TAttribute<FText>::CreateLambda([this]()
		{
			return PC && PC->ToastTimer > 0.f ? FText::FromString(PC->ToastLine) : FText::GetEmpty();
		}))
	]
	+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
	[
		SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Cream)
		.Text(TAttribute<FText>::CreateLambda([this]()
		{
			return PC ? FText::FromString(PC->StatusLine) : FText::GetEmpty();
		}))
	]
	+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 3, 0, 0)
	[
		SNew(STextBlock).Font(Font(10, "Regular")).ColorAndOpacity(FLinearColor(0.75f, 0.73f, 0.68f, 0.85f))
		.Text(TAttribute<FText>::CreateLambda([this]()
		{
			AMinPawn* Pawn = PC ? PC->GetMinPawn() : nullptr;
			const FString PlaneStr = Pawn ? Min::PlaneName(Pawn->CurrentPlane()) : TEXT("?");
			return FText::FromString(FString::Printf(
				TEXT("Plane %s   Tab side view   Q/E planes   M 3D view   WASD pan   wheel zoom   Space pause   F speed"),
				*PlaneStr));
		}))
	];
}

TSharedRef<SWidget> SMinHUD::BuildGameOverOverlay()
{
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.03f, 0.03f, 0.04f, 0.72f))
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		.Visibility_Lambda([this]()
		{
			UMinSim* Sim = PC ? PC->GetSim() : nullptr;
			return Sim && Sim->bGameOver ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).Font(Font(42)).ColorAndOpacity(Danger)
				.Text(FText::FromString(TEXT("THE MINE HAS CLOSED")))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6)
			[
				SNew(STextBlock).Font(Font(14, "Regular")).ColorAndOpacity(Cream)
				.Text_Lambda([this]()
				{
					UMinSim* Sim = PC ? PC->GetSim() : nullptr;
					return FText::FromString(Sim ? Sim->GameOverReason : FString());
				})
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 18, 0, 2)
			[
				SNew(STextBlock).Font(Font(64)).ColorAndOpacity(AccentWarm)
				.Text_Lambda([this]()
				{
					UMinSim* Sim = PC ? PC->GetSim() : nullptr;
					return FText::FromString(FString::Printf(TEXT("%d"), Sim ? Sim->Score : 0));
				})
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 24)
			[
				SNew(STextBlock).Font(Font(14)).ColorAndOpacity(FLinearColor(0.8f, 0.78f, 0.72f))
				.Text_Lambda([this]()
				{
					UMinSim* Sim = PC ? PC->GetSim() : nullptr;
					if (!Sim) return FText::GetEmpty();
					return FText::FromString(Sim->bNewRecord
						? TEXT("NEW RECORD!")
						: FString::Printf(TEXT("best %d"), Sim->HighScore));
				})
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4)
			[
				SNew(SBox).WidthOverride(260).HeightOverride(44)
				[
					SNew(SButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ButtonColorAndOpacity(Accent)
					.OnClicked_Lambda([this]() { if (PC) PC->RestartRun(); return FReply::Handled(); })
					[
						SNew(STextBlock).Font(Font(16)).ColorAndOpacity(FLinearColor(0.05f, 0.08f, 0.08f))
						.Text(FText::FromString(TEXT("DIG AGAIN")))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4)
			[
				SNew(SBox).WidthOverride(260).HeightOverride(44)
				[
					SNew(SButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ButtonColorAndOpacity(FLinearColor(0.3f, 0.29f, 0.27f))
					.OnClicked_Lambda([this]() { if (PC) PC->BackToTitle(); return FReply::Handled(); })
					[
						SNew(STextBlock).Font(Font(16)).ColorAndOpacity(Cream)
						.Text(FText::FromString(TEXT("TITLE")))
					]
				]
			]
		];
}

// ---------------------------------------------------------------------------
void SMinTitle::Construct(const FArguments& InArgs, AMinPlayerController* InPC)
{
	PC = InPC;

	auto ModeButton = [this](const TCHAR* Label, const TCHAR* Blurb, EMinGameRules Rules) -> TSharedRef<SWidget>
	{
		return SNew(SBox).WidthOverride(320).HeightOverride(64).Padding(0, 4)
		[
			SNew(SButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
			.ButtonColorAndOpacity(Rules == EMinGameRules::SinglePlane ? Accent : AccentWarm)
			.OnClicked_Lambda([this, Rules]() { if (PC) PC->StartNewRun(Rules); return FReply::Handled(); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(Font(17)).ColorAndOpacity(FLinearColor(0.05f, 0.08f, 0.08f))
					.Text(FText::FromString(FString::Printf(TEXT("%s   ·   best %d"),
						Label, ReadHighScore(Rules))))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(Font(10, "Regular")).ColorAndOpacity(FLinearColor(0.12f, 0.14f, 0.14f))
					.Text(FText::FromString(Blurb))
				]
			]
		];
	};

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.04f, 0.05f, 0.06f, 0.78f))
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock).Font(Font(96)).ColorAndOpacity(Cream)
					.Text(FText::FromString(TEXT("MIN")))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock).Font(Font(96)).ColorAndOpacity(Accent)
					.Text(FText::FromString(TEXT("3")))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock).Font(Font(96)).ColorAndOpacity(Cream)
					.Text(FText::FromString(TEXT("D")))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 4)
			[
				SNew(STextBlock).Font(Font(18)).ColorAndOpacity(AccentWarm)
				.Text(FText::FromString(TEXT("UNDERGROUND LOGISTICS")))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 26)
			[
				SNew(STextBlock).Font(Font(12, "Regular")).ColorAndOpacity(FLinearColor(0.8f, 0.78f, 0.72f))
				.Text(FText::FromString(TEXT("tunnel pools to plants, keep every quota, survive as long as you can")))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[ ModeButton(TEXT("SINGLE-PLANE"), TEXT("each ore lives on one plane"), EMinGameRules::SinglePlane) ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[ ModeButton(TEXT("CROSS-PLANE"), TEXT("ores split across planes - use the side view"), EMinGameRules::CrossPlane) ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4)
			[
				SNew(SBox).WidthOverride(320).HeightOverride(40)
				[
					SNew(SButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ButtonColorAndOpacity(FLinearColor(0.3f, 0.29f, 0.27f))
					.OnClicked_Lambda([this]() { if (PC) PC->QuitGame(); return FReply::Handled(); })
					[
						SNew(STextBlock).Font(Font(15)).ColorAndOpacity(Cream)
						.Text(FText::FromString(TEXT("QUIT")))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 30, 0, 0)
			[
				SNew(STextBlock).Font(Font(10, "Regular")).ColorAndOpacity(FLinearColor(0.6f, 0.58f, 0.54f))
				.Text(FText::FromString(TEXT("terrain, audio and logistics are generated procedurally · BullAxiom 2026")))
			]
		]
	];
}
