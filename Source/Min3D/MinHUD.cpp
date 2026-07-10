#include "MinHUD.h"
#include "Min3D.h"
#include "MinPlayerController.h"
#include "MinPawn.h"
#include "MinGameMode.h"
#include "MinSim.h"
#include "MinAssets.h"
#include "Engine/Texture2D.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Styling/CoreStyle.h"

namespace
{
	const FLinearColor PanelBg(0.06f, 0.05f, 0.05f, 0.62f);
	const FLinearColor PanelBgSoft(0.06f, 0.05f, 0.05f, 0.42f);
	const FLinearColor Cream(0.96f, 0.93f, 0.86f);
	const FLinearColor Accent(0.55f, 0.83f, 0.78f);
	const FLinearColor AccentWarm(0.95f, 0.75f, 0.35f);

	FSlateFontInfo Font(int32 Size, const FName Weight = "Bold")
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
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
			[ BuildResourceBar() ]
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center).VAlign(VAlign_Top)
			[ BuildPlaneTabs() ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[ BuildContractPanel() ]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 12)
		[ BuildToolbar() ]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(12, 0, 0, 14)
		[ BuildStatusArea() ]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(10, 64, 0, 0)
		[ BuildTutorialPanel() ]
	];
	SetVisibility(TAttribute<EVisibility>::CreateLambda([this]()
	{
		return PC && !PC->bInTitle ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
	}));
}

TSharedRef<SWidget> SMinHUD::BuildResourceBar()
{
	struct FEntry { EMinResource Res; const TCHAR* Icon; };
	static const FEntry Entries[] = {
		{ EMinResource::Iron, TEXT("ico_01_02") },
		{ EMinResource::Copper, TEXT("ico_01_04") },
		{ EMinResource::Gold, TEXT("ico_01_05") },
		{ EMinResource::Coal, TEXT("ico_01_00") },
		{ EMinResource::Water, TEXT("ico_00_01") },
		{ EMinResource::IronIngot, TEXT("ico_00_04") },
		{ EMinResource::CopperIngot, TEXT("ico_00_05") },
		{ EMinResource::GoldBar, TEXT("ico_00_03") },
	};

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	// credits first
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
	[
		SNew(SImage).Image(MakeIconBrush(TEXT("ico_06_07"), 22.f).Get())
	];
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 10, 0)
	[
		SNew(STextBlock)
		.Font(Font(16))
		.ColorAndOpacity(AccentWarm)
		.Text(TAttribute<FText>::CreateLambda([this]()
		{
			UMinSim* Sim = PC ? PC->GetSim() : nullptr;
			return FText::FromString(FString::Printf(TEXT("%d"), Sim ? Sim->Credits : 0));
		}))
	];

	for (const FEntry& E : Entries)
	{
		const EMinResource Res = E.Res;
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
		[
			SNew(SImage).Image(MakeIconBrush(E.Icon, 20.f).Get())
		];
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
		[
			SNew(STextBlock)
			.Font(Font(14))
			.ColorAndOpacity(Cream)
			.Text(TAttribute<FText>::CreateLambda([this, Res]()
			{
				UMinSim* Sim = PC ? PC->GetSim() : nullptr;
				const int32* N = Sim ? Sim->Stock.Find(Res) : nullptr;
				return FText::FromString(FString::Printf(TEXT("%d"), N ? *N : 0));
			}))
		];
	}

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(PanelBg)
		.Padding(FMargin(10, 6))
		[ Row ];
}

TSharedRef<SWidget> SMinHUD::BuildPlaneTabs()
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 P = 0; P < Min::NP; ++P)
	{
		const int32 Plane = P;
		Row->AddSlot().AutoWidth().Padding(2, 0)
		[
			SNew(SButton)
			.ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this, Plane]()
			{
				AMinPawn* Pawn = PC ? PC->GetMinPawn() : nullptr;
				const bool bCur = Pawn && Pawn->ViewPlane == Plane;
				return bCur ? FSlateColor(Accent) : FSlateColor(FLinearColor(0.25f, 0.24f, 0.22f, 0.9f));
			}))
			.OnClicked_Lambda([this, Plane]()
			{
				if (PC) PC->JumpToPlane(Plane);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font(Font(13))
				.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this, Plane]()
				{
					AMinPawn* Pawn = PC ? PC->GetMinPawn() : nullptr;
					return Pawn && Pawn->ViewPlane == Plane
						? FSlateColor(FLinearColor(0.05f, 0.08f, 0.08f)) : FSlateColor(Cream);
				}))
				.Text(FText::FromString(Min::PlaneName(Plane)))
			]
		];
	}
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(PanelBgSoft)
		.Padding(FMargin(6, 4))
		[ Row ];
}

TSharedRef<SWidget> SMinHUD::BuildToolbar()
{
	struct FToolDef { EMinTool Tool; const TCHAR* Key; const TCHAR* Icon; const TCHAR* Label; };
	static const FToolDef Tools[] = {
		{ EMinTool::Tunnel, TEXT("1"), TEXT("ico_03_00"), TEXT("Tunnel") },
		{ EMinTool::Pipe, TEXT("2"), TEXT("veh_05_08"), TEXT("Pipe") },
		{ EMinTool::Miner, TEXT("3"), TEXT("net_05_02"), TEXT("Miner") },
		{ EMinTool::Pump, TEXT("4"), TEXT("net_08_04"), TEXT("Pump") },
		{ EMinTool::Smelter, TEXT("5"), TEXT("mac_00_00"), TEXT("Smelter") },
		{ EMinTool::Depot, TEXT("6"), TEXT("net_04_03"), TEXT("Depot") },
		{ EMinTool::Generator, TEXT("7"), TEXT("mac_07_00"), TEXT("Generator") },
		{ EMinTool::Lift, TEXT("8"), TEXT("net_02_05"), TEXT("Lift") },
		{ EMinTool::Remove, TEXT("X"), TEXT("ico_07_03"), TEXT("Remove") },
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

TSharedRef<SWidget> SMinHUD::BuildContractPanel()
{
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(PanelBg)
		.Padding(FMargin(12, 8))
		[
			SNew(SBox).WidthOverride(300)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(Font(15)).ColorAndOpacity(AccentWarm)
					.Text(TAttribute<FText>::CreateLambda([this]()
					{
						UMinSim* Sim = PC ? PC->GetSim() : nullptr;
						if (!Sim || !Sim->Contracts.IsValidIndex(Sim->ActiveContract)) return FText::GetEmpty();
						const FMinContract& C = Sim->Contracts[Sim->ActiveContract];
						return FText::FromString(FString::Printf(TEXT("Contract: %s"), *C.Title));
					}))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SProgressBar)
					.FillColorAndOpacity(Accent)
					.Percent(TAttribute<TOptional<float>>::CreateLambda([this]() -> TOptional<float>
					{
						UMinSim* Sim = PC ? PC->GetSim() : nullptr;
						if (!Sim || !Sim->Contracts.IsValidIndex(Sim->ActiveContract)) return 0.f;
						const FMinContract& C = Sim->Contracts[Sim->ActiveContract];
						return C.Needed > 0 ? float(C.Delivered) / C.Needed : 0.f;
					}))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Cream)
					.Text(TAttribute<FText>::CreateLambda([this]()
					{
						UMinSim* Sim = PC ? PC->GetSim() : nullptr;
						if (!Sim || !Sim->Contracts.IsValidIndex(Sim->ActiveContract)) return FText::GetEmpty();
						const FMinContract& C = Sim->Contracts[Sim->ActiveContract];
						return FText::FromString(FString::Printf(TEXT("%s  %d / %d   reward %d"),
							C.Resource == EMinResource::None ? TEXT("Any cargo") : MinResInfo(C.Resource).Name,
							C.Delivered, C.Needed, C.RewardCredits));
					}))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
				[
					SNew(STextBlock).Font(Font(11, "Regular")).ColorAndOpacity(FLinearColor(0.85f, 0.83f, 0.78f))
					.AutoWrapText(true)
					.Text(TAttribute<FText>::CreateLambda([this]()
					{
						UMinSim* Sim = PC ? PC->GetSim() : nullptr;
						if (!Sim || !Sim->Contracts.IsValidIndex(Sim->ActiveContract)) return FText::GetEmpty();
						return FText::FromString(Sim->Contracts[Sim->ActiveContract].Hint);
					}))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SNew(STextBlock).Font(Font(11, "Regular")).ColorAndOpacity(FLinearColor(0.7f, 0.85f, 0.8f))
					.Text(TAttribute<FText>::CreateLambda([this]()
					{
						UMinSim* Sim = PC ? PC->GetSim() : nullptr;
						if (!Sim) return FText::GetEmpty();
						return FText::FromString(FString::Printf(TEXT("Carts %d   Congestion %.0f%%   Delivered %d"),
							Sim->CartsInFlight(), Sim->AvgCongestion() * 100.f, Sim->TotalDeliveries));
					}))
				]
			]
		];
}

TSharedRef<SWidget> SMinHUD::BuildStatusArea()
{
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
	[
		SNew(STextBlock).Font(Font(13)).ColorAndOpacity(FLinearColor(1.f, 0.55f, 0.4f))
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
			const FString PlaneStr = Pawn ? Min::PlaneName(Pawn->ViewPlane) : TEXT("?");
			return FText::FromString(FString::Printf(
				TEXT("Plane %s   Q/E planes   Tab matrix   WASD pan   wheel zoom   Space pause   F speed   G ghosts   F5 save   F9 load"),
				*PlaneStr));
		}))
	];
}

TSharedRef<SWidget> SMinHUD::BuildTutorialPanel()
{
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.05f, 0.07f, 0.07f, 0.80f))
		.Padding(FMargin(14, 10))
		.Visibility_Lambda([this]()
		{
			AMinGameMode* GM = PC ? PC->GetGM() : nullptr;
			return GM && GM->TutStep >= 0 ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SBox).WidthOverride(340)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(Font(16)).ColorAndOpacity(Accent)
					.Text_Lambda([this]()
					{
						AMinGameMode* GM = PC ? PC->GetGM() : nullptr;
						return GM ? FText::FromString(GM->TutTitle) : FText::GetEmpty();
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
				[
					SNew(STextBlock).Font(Font(12, "Regular")).ColorAndOpacity(Cream)
					.AutoWrapText(true)
					.Text_Lambda([this]()
					{
						AMinGameMode* GM = PC ? PC->GetGM() : nullptr;
						return GM ? FText::FromString(GM->TutText) : FText::GetEmpty();
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
					[
						SNew(SButton)
						.ButtonColorAndOpacity(Accent)
						.OnClicked_Lambda([this]()
						{
							if (AMinGameMode* GM = PC ? PC->GetGM() : nullptr) GM->TutorialNext();
							return FReply::Handled();
						})
						[
							SNew(STextBlock).Font(Font(12)).ColorAndOpacity(FLinearColor(0.05f, 0.08f, 0.08f))
							.Text_Lambda([this]()
							{
								AMinGameMode* GM = PC ? PC->GetGM() : nullptr;
								return FText::FromString(GM && GM->bTutFinal ? TEXT("Finish") : TEXT("Next"));
							})
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.ButtonColorAndOpacity(FLinearColor(0.3f, 0.29f, 0.27f))
						.OnClicked_Lambda([this]()
						{
							if (AMinGameMode* GM = PC ? PC->GetGM() : nullptr) GM->TutorialSkip();
							return FReply::Handled();
						})
						[
							SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Cream)
							.Text(FText::FromString(TEXT("Skip tutorial")))
						]
					]
				]
			]
		];
}

// ---------------------------------------------------------------------------
void SMinTitle::Construct(const FArguments& InArgs, AMinPlayerController* InPC)
{
	PC = InPC;

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
				.Text(FText::FromString(TEXT("MINE  ·  MIND  ·  3D")))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 30)
			[
				SNew(STextBlock).Font(Font(12, "Regular")).ColorAndOpacity(FLinearColor(0.8f, 0.78f, 0.72f))
				.Text(FText::FromString(TEXT("a minimalist routing puzzle inside a living underground cube")))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4)
			[
				SNew(SBox).WidthOverride(240).HeightOverride(44)
				[
					SNew(SButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ButtonColorAndOpacity(Accent)
					.OnClicked_Lambda([this]() { if (PC) PC->StartNewGame(); return FReply::Handled(); })
					[
						SNew(STextBlock).Font(Font(16)).ColorAndOpacity(FLinearColor(0.05f, 0.08f, 0.08f))
						.Text(FText::FromString(TEXT("NEW NETWORK")))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4)
			[
				SNew(SBox).WidthOverride(240).HeightOverride(44)
				[
					SNew(SButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ButtonColorAndOpacity(FLinearColor(0.3f, 0.29f, 0.27f))
					.IsEnabled_Lambda([]() { return UMinSim::SaveExists(); })
					.OnClicked_Lambda([this]() { if (PC) PC->ContinueGame(); return FReply::Handled(); })
					[
						SNew(STextBlock).Font(Font(16)).ColorAndOpacity(Cream)
						.Text(FText::FromString(TEXT("CONTINUE")))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4)
			[
				SNew(SBox).WidthOverride(240).HeightOverride(44)
				[
					SNew(SButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ButtonColorAndOpacity(FLinearColor(0.3f, 0.29f, 0.27f))
					.OnClicked_Lambda([this]() { if (PC) PC->QuitGame(); return FReply::Handled(); })
					[
						SNew(STextBlock).Font(Font(16)).ColorAndOpacity(Cream)
						.Text(FText::FromString(TEXT("QUIT")))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 34, 0, 0)
			[
				SNew(STextBlock).Font(Font(10, "Regular")).ColorAndOpacity(FLinearColor(0.6f, 0.58f, 0.54f))
				.Text(FText::FromString(TEXT("terrain, audio and logistics are generated procedurally · BullAxiom 2026")))
			]
		]
	];
}
