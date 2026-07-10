#include "MinPlayerController.h"
#include "Min3D.h"
#include "MinPawn.h"
#include "MinGameMode.h"
#include "MinGameInstance.h"
#include "MinSim.h"
#include "MinAudio.h"
#include "MinPlaneActor.h"
#include "MinHUD.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SWeakWidget.h"

AMinPlayerController::AMinPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	DefaultMouseCursor = EMouseCursor::Default;
}

AMinPawn* AMinPlayerController::GetMinPawn() const { return Cast<AMinPawn>(GetPawn()); }
AMinGameMode* AMinPlayerController::GetGM() const { return Cast<AMinGameMode>(UGameplayStatics::GetGameMode(this)); }
UMinSim* AMinPlayerController::GetSim() const { AMinGameMode* GM = GetGM(); return GM ? GM->Sim : nullptr; }
UMinAudio* AMinPlayerController::GetAudio() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMinAudio>() : nullptr;
}

void AMinPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (GEngine && GEngine->GameViewport)
	{
		HUD = SNew(SMinHUD, this);
		GEngine->GameViewport->AddViewportWidgetContent(
			SNew(SWeakWidget).PossiblyNullContent(HUD.ToSharedRef()), 10);

		const bool bSkipTitle = FParse::Param(FCommandLine::Get(), TEXT("MinAutoBuild"))
			|| FParse::Param(FCommandLine::Get(), TEXT("MinSkipTitle"));
		if (bSkipTitle) bInTitle = false;
		else
		{
			Title = SNew(SMinTitle, this);
			GEngine->GameViewport->AddViewportWidgetContent(
				SNew(SWeakWidget).PossiblyNullContent(Title.ToSharedRef()), 20);
		}
	}
}

void AMinPlayerController::StartNewGame()
{
	bInTitle = false;
	if (Title.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Title->AsShared());
		Title.Reset();
	}
	if (UMinAudio* A = GetAudio()) A->Play(EMinSfx::Chime);
}

void AMinPlayerController::ContinueGame()
{
	if (UMinSim* Sim = GetSim())
	{
		if (UMinSim::SaveExists()) Sim->LoadGame();
	}
	if (AMinGameMode* GM = GetGM()) GM->TutorialSkip();
	StartNewGame();
}

void AMinPlayerController::QuitGame()
{
	ConsoleCommand(TEXT("quit"));
}

void AMinPlayerController::SaveNow()
{
	if (UMinSim* Sim = GetSim())
	{
		Sim->SaveGame();
		ShowToast(TEXT("Game saved"));
		if (UMinAudio* A = GetAudio()) A->Play(EMinSfx::Coin);
	}
}

void AMinPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AMinPlayerController::OnZoomIn);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AMinPlayerController::OnZoomOut);
	InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AMinPlayerController::OnPlanePrev);
	InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AMinPlayerController::OnPlaneNext);
	InputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &AMinPlayerController::OnPlanePrev);
	InputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &AMinPlayerController::OnPlaneNext);
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AMinPlayerController::ToggleMatrix);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AMinPlayerController::OnLMBDown);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AMinPlayerController::OnLMBUp);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AMinPlayerController::OnRMBDown);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AMinPlayerController::OnEscape);
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AMinPlayerController::OnTogglePause);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AMinPlayerController::OnSpeed);
	InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AMinPlayerController::OnToggleGhosts);
	InputComponent->BindKey(EKeys::F5, IE_Pressed, this, &AMinPlayerController::OnSave);
	InputComponent->BindKey(EKeys::F9, IE_Pressed, this, &AMinPlayerController::OnLoad);

	auto BindTool = [&](FKey Key, EMinTool T)
	{
		FInputKeyBinding KB(FInputChord(Key), IE_Pressed);
		KB.KeyDelegate.GetDelegateForManualSet().BindLambda([this, T]() { SetTool(T); });
		InputComponent->KeyBindings.Add(KB);
	};
	BindTool(EKeys::One, EMinTool::Tunnel);
	BindTool(EKeys::Two, EMinTool::Pipe);
	BindTool(EKeys::Three, EMinTool::Miner);
	BindTool(EKeys::Four, EMinTool::Pump);
	BindTool(EKeys::Five, EMinTool::Smelter);
	BindTool(EKeys::Six, EMinTool::Depot);
	BindTool(EKeys::Seven, EMinTool::Generator);
	BindTool(EKeys::Eight, EMinTool::Lift);
	BindTool(EKeys::X, EMinTool::Remove);

	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

void AMinPlayerController::SetTool(EMinTool InTool)
{
	if (bInTitle) return;
	Tool = Tool == InTool ? EMinTool::None : InTool;
	bLiftPending = false;
	bDraggingRoute = false;
	DragPath.Reset();
	if (UMinAudio* A = GetAudio()) A->Play(EMinSfx::Click);
}

void AMinPlayerController::ShowToast(const FString& Msg)
{
	ToastLine = Msg;
	ToastTimer = 4.f;
}

void AMinPlayerController::OnZoomIn()
{
	if (AMinPawn* P = GetMinPawn()) P->OrthoWidth = FMath::Clamp(P->OrthoWidth * 0.85f, 1200.f, 14000.f);
}

void AMinPlayerController::OnZoomOut()
{
	if (AMinPawn* P = GetMinPawn()) P->OrthoWidth = FMath::Clamp(P->OrthoWidth / 0.85f, 1200.f, 14000.f);
}

void AMinPlayerController::SwitchPlane(int32 Delta)
{
	AMinPawn* P = GetMinPawn();
	if (!P || bInTitle) return;
	JumpToPlane(P->ViewPlane + Delta);
}

void AMinPlayerController::JumpToPlane(int32 Plane)
{
	AMinPawn* P = GetMinPawn();
	if (!P || bInTitle) return;
	const int32 NewPlane = FMath::Clamp(Plane, 0, Min::NP - 1);
	if (NewPlane == P->ViewPlane) return;
	// route drags don't survive a plane switch
	if (bDraggingRoute && GetGM() && GetGM()->GetPlaneActor(DragPlane))
		GetGM()->GetPlaneActor(DragPlane)->ClearPreview();
	bDraggingRoute = false;
	DragPath.Reset();
	P->ViewPlane = NewPlane;
	if (AMinGameMode* GM = GetGM()) GM->UpdatePlaneVisibility();
	if (UMinAudio* A = GetAudio()) A->Play(EMinSfx::PlaneSwitch);
}

void AMinPlayerController::ToggleMatrix()
{
	AMinPawn* P = GetMinPawn();
	if (!P || bInTitle) return;
	P->bMatrixView = !P->bMatrixView;
	if (AMinGameMode* GM = GetGM()) GM->UpdatePlaneVisibility();
	if (UMinAudio* A = GetAudio()) A->Play(EMinSfx::Whoosh);
}

void AMinPlayerController::OnTogglePause()
{
	if (UMinSim* Sim = GetSim())
	{
		Sim->bPaused = !Sim->bPaused;
		ShowToast(Sim->bPaused ? TEXT("Paused") : TEXT("Running"));
	}
}

void AMinPlayerController::OnSpeed()
{
	if (UMinSim* Sim = GetSim())
	{
		Sim->SimSpeed = Sim->SimSpeed >= 3.f ? 1.f : Sim->SimSpeed * 1.75f;
		ShowToast(FString::Printf(TEXT("Speed x%.1f"), Sim->SimSpeed));
	}
}

void AMinPlayerController::OnToggleGhosts()
{
	if (AMinGameMode* GM = GetGM())
	{
		GM->bGhostPlanes = !GM->bGhostPlanes;
		GM->UpdatePlaneVisibility();
		ShowToast(GM->bGhostPlanes ? TEXT("Neighbor-plane ghosts on") : TEXT("Neighbor-plane ghosts off"));
	}
}

void AMinPlayerController::OnSave() { if (!bInTitle) SaveNow(); }

void AMinPlayerController::OnLoad()
{
	if (bInTitle) return;
	// a clean load needs a fresh world: reopen the level with the load flag
	if (UMinGameInstance* GI = Cast<UMinGameInstance>(GetGameInstance()))
	{
		if (!UMinSim::SaveExists()) { ShowToast(TEXT("No save found")); return; }
		GI->bRequestLoadSave = true;
		UGameplayStatics::OpenLevel(this, TEXT("Main"));
	}
}

void AMinPlayerController::OnEscape()
{
	if (bInTitle) { QuitGame(); return; }
	if (Tool != EMinTool::None || bLiftPending)
	{
		Tool = EMinTool::None;
		bLiftPending = false;
		bDraggingRoute = false;
		DragPath.Reset();
		if (AMinGameMode* GM = GetGM())
			for (int32 P = 0; P < Min::NP; ++P)
				if (AMinPlaneActor* A = GM->GetPlaneActor(P)) A->ClearPreview();
	}
	else QuitGame();
}

bool AMinPlayerController::GetMouseCell(FIntPoint& OutCell) const
{
	AMinPawn* P = GetMinPawn();
	if (!P || P->bMatrixView) return false;
	float MX, MY;
	if (!GetMousePosition(MX, MY)) return false;

	// engine deprojection inverts the exact view-projection the frame rendered
	// with. (UE 5.8 applies OrthoWidth to the vertical axis here, so any
	// hand-rolled mapping must NOT assume OrthoWidth spans the screen width.)
	FVector Origin, Dir;
	if (UGameplayStatics::DeprojectScreenToWorld(this, FVector2D(MX, MY), Origin, Dir)
		&& !FMath::IsNearlyZero(Dir.Y))
	{
		const float PlaneY = P->ViewPlane * Min::PlaneGap;
		const float T = (PlaneY - Origin.Y) / Dir.Y;
		OutCell = Min::WorldToCell(Origin + Dir * T);
	}
	else
	{
		// fallback: OrthoWidth-as-height analytic mapping
		int32 VX, VY;
		GetViewportSize(VX, VY);
		if (VX <= 0 || VY <= 0) return false;
		const float UUPerPx = P->OrthoWidth / VY;
		const float WorldX = P->PanCenter.X + (MX - VX * 0.5f) * UUPerPx;
		const float WorldZ = P->PanCenter.Y - (MY - VY * 0.5f) * UUPerPx;
		OutCell = Min::WorldToCell(FVector(WorldX, 0.f, WorldZ));
	}
	return OutCell.X >= 0 && OutCell.X < Min::NX && OutCell.Y >= 0 && OutCell.Y < Min::NY;
}

void AMinPlayerController::OnLMBDown()
{
	if (bInTitle) return;
	AMinPawn* P = GetMinPawn();
	UMinSim* Sim = GetSim();
	FIntPoint Cell;
	if (!P || !Sim || !GetMouseCell(Cell)) return;
	UMinAudio* A = GetAudio();

	switch (Tool)
	{
	case EMinTool::Tunnel:
	case EMinTool::Pipe:
		bDraggingRoute = true;
		DragPlane = P->ViewPlane;
		DragPath = { Cell };
		break;

	case EMinTool::Miner: case EMinTool::Pump: case EMinTool::Smelter:
	case EMinTool::Depot: case EMinTool::Generator:
	{
		const EMinStructure SType =
			Tool == EMinTool::Miner ? EMinStructure::Miner :
			Tool == EMinTool::Pump ? EMinStructure::Pump :
			Tool == EMinTool::Smelter ? EMinStructure::Smelter :
			Tool == EMinTool::Depot ? EMinStructure::Depot : EMinStructure::Generator;
		const FMinBuildCheck Check = Sim->CheckStructure(SType, Cell, P->ViewPlane);
		if (Check.bValid && Sim->BuildStructure(SType, Cell, P->ViewPlane))
		{
			if (A) A->Play(EMinSfx::Build);
			ShowToast(FString::Printf(TEXT("%s built (-%d)"), UMinSim::StructureName(SType), Check.Cost));
		}
		else
		{
			if (A) A->Play(EMinSfx::Error);
			ShowToast(Check.Reason);
		}
		break;
	}

	case EMinTool::Lift:
		if (!bLiftPending)
		{
			bLiftPending = true;
			LiftBaseCell = Cell;
			LiftBasePlane = P->ViewPlane;
			ShowToast(FString::Printf(TEXT("Lift base set on %s - switch planes (Q/E) and click to connect"),
				*Min::PlaneName(LiftBasePlane)));
			if (A) A->Play(EMinSfx::Click);
		}
		else
		{
			const FMinBuildCheck Check = Sim->CheckLift(LiftBaseCell, LiftBasePlane, P->ViewPlane);
			if (Check.bValid && Sim->BuildLift(LiftBaseCell, LiftBasePlane, P->ViewPlane))
			{
				bLiftPending = false;
				if (A) A->Play(EMinSfx::Build);
				ShowToast(FString::Printf(TEXT("Lift connected %s to %s (-%d)"),
					*Min::PlaneName(LiftBasePlane), *Min::PlaneName(P->ViewPlane), Check.Cost));
			}
			else
			{
				if (A) A->Play(EMinSfx::Error);
				ShowToast(Check.Reason);
			}
		}
		break;

	case EMinTool::Remove:
		if (Sim->RemoveAt(Cell, P->ViewPlane))
		{
			if (A) A->Play(EMinSfx::Remove);
		}
		break;

	default: break;
	}
}

void AMinPlayerController::OnLMBUp()
{
	if (!bDraggingRoute) return;
	bDraggingRoute = false;
	UMinSim* Sim = GetSim();
	AMinGameMode* GM = GetGM();
	UMinAudio* A = GetAudio();
	if (GM && GM->GetPlaneActor(DragPlane)) GM->GetPlaneActor(DragPlane)->ClearPreview();
	if (!Sim || DragPath.Num() < 2) { DragPath.Reset(); return; }

	const EMinRoute RType = Tool == EMinTool::Pipe ? EMinRoute::Pipe : EMinRoute::Tunnel;
	const FMinBuildCheck Check = Sim->CheckRoute(DragPath, DragPlane, RType);
	if (Check.bValid && Sim->BuildRoute(DragPath, DragPlane, RType))
	{
		if (A) A->Play(EMinSfx::Dig);
		ShowToast(FString::Printf(TEXT("%s built: %d cells (-%d)"),
			RType == EMinRoute::Pipe ? TEXT("Pipe") : TEXT("Tunnel"), DragPath.Num(), Check.Cost));
	}
	else
	{
		if (A) A->Play(EMinSfx::Error);
		ShowToast(Check.Reason);
	}
	DragPath.Reset();
}

void AMinPlayerController::OnRMBDown()
{
	if (bDraggingRoute)
	{
		bDraggingRoute = false;
		DragPath.Reset();
		if (AMinGameMode* GM = GetGM())
			if (GM->GetPlaneActor(DragPlane)) GM->GetPlaneActor(DragPlane)->ClearPreview();
		return;
	}
	if (bLiftPending) { bLiftPending = false; ShowToast(TEXT("Lift cancelled")); return; }
	if (Tool != EMinTool::None) { Tool = EMinTool::None; return; }
}

void AMinPlayerController::UpdateDragPath()
{
	FIntPoint Cell;
	if (!GetMouseCell(Cell) || DragPath.Num() == 0) return;
	FIntPoint Last = DragPath.Last();
	if (Cell == Last) return;
	// undo when retracing
	if (DragPath.Num() >= 2 && Cell == DragPath[DragPath.Num() - 2])
	{
		DragPath.Pop();
		return;
	}
	// walk a manhattan L from the last cell toward the cursor (x first, then y)
	int32 Guard = 256;
	while (Last != Cell && Guard-- > 0)
	{
		if (Last.X != Cell.X) Last.X += Last.X < Cell.X ? 1 : -1;
		else Last.Y += Last.Y < Cell.Y ? 1 : -1;
		if (DragPath.Num() >= 2 && Last == DragPath[DragPath.Num() - 2]) DragPath.Pop();
		else if (!DragPath.Contains(Last)) DragPath.Add(Last);
		else break; // self-intersection: stop extending
	}
}

void AMinPlayerController::UpdatePreview()
{
	AMinGameMode* GM = GetGM();
	AMinPawn* P = GetMinPawn();
	UMinSim* Sim = GetSim();
	if (!GM || !P || !Sim) return;
	AMinPlaneActor* Plane = GM->GetPlaneActor(P->ViewPlane);
	if (!Plane) return;
	StatusLine.Reset();

	if (bDraggingRoute && DragPath.Num() > 0)
	{
		const EMinRoute RType = Tool == EMinTool::Pipe ? EMinRoute::Pipe : EMinRoute::Tunnel;
		const FMinBuildCheck Check = Sim->CheckRoute(DragPath, DragPlane, RType);
		Plane->ShowPreview(DragPath, Check.bValid, RType);
		StatusLine = Check.bValid
			? FString::Printf(TEXT("%d cells - cost %d"), DragPath.Num(), Check.Cost)
			: Check.Reason;
		return;
	}

	FIntPoint Cell;
	if (Tool != EMinTool::None && GetMouseCell(Cell))
	{
		TArray<FIntPoint> One = { Cell };
		switch (Tool)
		{
		case EMinTool::Tunnel: case EMinTool::Pipe:
		{
			Plane->ShowPreview(One, true, Tool == EMinTool::Pipe ? EMinRoute::Pipe : EMinRoute::Tunnel);
			StatusLine = TEXT("Click and drag to dig");
			break;
		}
		case EMinTool::Lift:
		{
			const FMinBuildCheck Check = bLiftPending
				? Sim->CheckLift(LiftBaseCell, LiftBasePlane, P->ViewPlane)
				: Sim->CheckLift(Cell, P->ViewPlane, P->ViewPlane == 0 ? 1 : P->ViewPlane - 1);
			TArray<FIntPoint> Spot = { bLiftPending ? LiftBaseCell : Cell };
			Plane->ShowPreview(Spot, bLiftPending ? Check.bValid : true, EMinRoute::Tunnel);
			StatusLine = bLiftPending
				? (Check.bValid ? FString::Printf(TEXT("Connect %s to %s - cost %d"),
					*Min::PlaneName(LiftBasePlane), *Min::PlaneName(P->ViewPlane), Check.Cost) : Check.Reason)
				: TEXT("Click to set the lift base");
			break;
		}
		case EMinTool::Remove:
			Plane->ShowPreview(One, false, EMinRoute::Tunnel);
			StatusLine = TEXT("Click a structure or route to remove it");
			break;
		default:
		{
			const EMinStructure SType =
				Tool == EMinTool::Miner ? EMinStructure::Miner :
				Tool == EMinTool::Pump ? EMinStructure::Pump :
				Tool == EMinTool::Smelter ? EMinStructure::Smelter :
				Tool == EMinTool::Depot ? EMinStructure::Depot : EMinStructure::Generator;
			const FMinBuildCheck Check = Sim->CheckStructure(SType, Cell, P->ViewPlane);
			Plane->ShowPreview(One, Check.bValid, EMinRoute::Tunnel);
			StatusLine = Check.bValid
				? FString::Printf(TEXT("%s - cost %d"), UMinSim::StructureName(SType), Check.Cost)
				: Check.Reason;
			break;
		}
		}
	}
	else Plane->ClearPreview();
}

void AMinPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	AMinPawn* P = GetMinPawn();
	if (!P) return;
	if (ToastTimer > 0.f) ToastTimer -= DeltaTime;

	// keyboard pan
	FVector2D Pan(0, 0);
	if (!bInTitle)
	{
		if (IsInputKeyDown(EKeys::A) || IsInputKeyDown(EKeys::Left)) Pan.X -= 1.f;
		if (IsInputKeyDown(EKeys::D) || IsInputKeyDown(EKeys::Right)) Pan.X += 1.f;
		if (IsInputKeyDown(EKeys::W) || IsInputKeyDown(EKeys::Up)) Pan.Y += 1.f;
		if (IsInputKeyDown(EKeys::S) || IsInputKeyDown(EKeys::Down)) Pan.Y -= 1.f;
	}

	if (P->bMatrixView)
	{
		P->MatrixYaw += Pan.X * DeltaTime * 60.f;
		P->MatrixPitch = FMath::Clamp(P->MatrixPitch + Pan.Y * DeltaTime * 40.f, -70.f, -5.f);
	}
	else if (!Pan.IsNearlyZero())
	{
		const float Speed = P->OrthoWidth * 0.6f * DeltaTime;
		P->PanCenter += Pan * Speed;
		P->PanCenter.X = FMath::Clamp(P->PanCenter.X, 0.f, Min::NX * Min::CellSize);
		P->PanCenter.Y = FMath::Clamp(P->PanCenter.Y, -Min::NY * Min::CellSize, 0.f);
	}

	// middle-mouse drag pan (world sticks to the cursor)
	if (IsInputKeyDown(EKeys::MiddleMouseButton) && !P->bMatrixView)
	{
		float MX, MY;
		if (GetMousePosition(MX, MY))
		{
			if (bDraggingView)
			{
				int32 VX, VY;
				GetViewportSize(VX, VY);
				const float UUPerPx = P->OrthoWidth / FMath::Max(1, VY);
				P->PanCenter.X -= (MX - LastMouse.X) * UUPerPx;
				P->PanCenter.Y += (MY - LastMouse.Y) * UUPerPx;
			}
			LastMouse = FVector2D(MX, MY);
			bDraggingView = true;
		}
	}
	else bDraggingView = false;

	if (!bInTitle)
	{
		if (bDraggingRoute) UpdateDragPath();
		UpdatePreview();
	}

	P->UpdateCamera(DeltaTime);
}
