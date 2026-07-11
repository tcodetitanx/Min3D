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
	UMinGameInstance* GI = Cast<UMinGameInstance>(GetGameInstance());
	const bool bSkipTitle = (GI && GI->bSkipTitle)
		|| FParse::Param(FCommandLine::Get(), TEXT("MinAutoBuild"))
		|| FParse::Param(FCommandLine::Get(), TEXT("MinSkipTitle"));
	bInTitle = !bSkipTitle;

	if (GEngine && GEngine->GameViewport)
	{
		HUD = SNew(SMinHUD, this);
		GEngine->GameViewport->AddViewportWidgetContent(
			SNew(SWeakWidget).PossiblyNullContent(HUD.ToSharedRef()), 10);

		if (bInTitle)
		{
			Title = SNew(SMinTitle, this);
			GEngine->GameViewport->AddViewportWidgetContent(
				SNew(SWeakWidget).PossiblyNullContent(Title.ToSharedRef()), 20);
		}
	}
}

void AMinPlayerController::StartNewRun(EMinGameRules Rules)
{
	if (!bInTitle) return;
	bInTitle = false;
	if (Title.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Title->AsShared());
		Title.Reset();
	}
	if (AMinGameMode* GM = GetGM()) GM->BeginRun(Rules);
	if (UMinAudio* A = GetAudio()) A->Play(EMinSfx::Chime);
}

void AMinPlayerController::RestartRun()
{
	if (UMinGameInstance* GI = Cast<UMinGameInstance>(GetGameInstance()))
	{
		UMinSim* Sim = GetSim();
		GI->PendingRules = Sim ? Sim->Rules : EMinGameRules::SinglePlane;
		GI->WorldSeed = FMath::Rand();
		GI->bSkipTitle = true;
		UGameplayStatics::OpenLevel(this, TEXT("Main"));
	}
}

void AMinPlayerController::BackToTitle()
{
	if (UMinGameInstance* GI = Cast<UMinGameInstance>(GetGameInstance()))
	{
		GI->WorldSeed = FMath::Rand();
		GI->bSkipTitle = false;
		UGameplayStatics::OpenLevel(this, TEXT("Main"));
	}
}

void AMinPlayerController::QuitGame()
{
	ConsoleCommand(TEXT("quit"));
}

void AMinPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AMinPlayerController::OnZoomIn);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AMinPlayerController::OnZoomOut);
	InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AMinPlayerController::OnPlanePrev);
	InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AMinPlayerController::OnPlaneNext);
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AMinPlayerController::ToggleSideView);
	InputComponent->BindKey(EKeys::V, IE_Pressed, this, &AMinPlayerController::ToggleSideView);
	InputComponent->BindKey(EKeys::M, IE_Pressed, this, &AMinPlayerController::ToggleMatrix);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AMinPlayerController::OnLMBDown);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AMinPlayerController::OnLMBUp);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AMinPlayerController::OnRMBDown);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AMinPlayerController::OnEscape);
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AMinPlayerController::OnTogglePause);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AMinPlayerController::OnSpeed);

	auto BindTool = [&](FKey Key, EMinTool T)
	{
		FInputKeyBinding KB(FInputChord(Key), IE_Pressed);
		KB.KeyDelegate.GetDelegateForManualSet().BindLambda([this, T]() { SetTool(T); });
		InputComponent->KeyBindings.Add(KB);
	};
	BindTool(EKeys::One, EMinTool::Tunnel);
	BindTool(EKeys::T, EMinTool::Tunnel);
	BindTool(EKeys::Two, EMinTool::Erase);
	BindTool(EKeys::X, EMinTool::Erase);

	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

void AMinPlayerController::SetTool(EMinTool InTool)
{
	if (bInTitle) return;
	Tool = Tool == InTool ? EMinTool::None : InTool;
	bDraggingRoute = false;
	bErasing = false;
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
	if (AMinPawn* P = GetMinPawn()) P->OrthoWidth = FMath::Clamp(P->OrthoWidth * 0.85f, 1500.f, 12000.f);
}

void AMinPlayerController::OnZoomOut()
{
	if (AMinPawn* P = GetMinPawn()) P->OrthoWidth = FMath::Clamp(P->OrthoWidth / 0.85f, 1500.f, 12000.f);
}

void AMinPlayerController::SwitchPlane(int32 Delta)
{
	AMinPawn* P = GetMinPawn();
	if (!P || bInTitle) return;
	JumpToPlane((P->bSideView ? P->ViewSide : P->ViewFront) + Delta);
}

void AMinPlayerController::JumpToPlane(int32 PlaneInView)
{
	AMinPawn* P = GetMinPawn();
	if (!P || bInTitle) return;
	const int32 NewPlane = FMath::Clamp(PlaneInView, 0, 2);
	int32& Cur = P->bSideView ? P->ViewSide : P->ViewFront;
	if (NewPlane == Cur) return;
	// route drags don't survive a plane switch
	if (bDraggingRoute && GetGM() && GetGM()->GetPlaneActor(DragPlane))
		GetGM()->GetPlaneActor(DragPlane)->ClearPreview();
	bDraggingRoute = false;
	DragPath.Reset();
	Cur = NewPlane;
	if (AMinGameMode* GM = GetGM()) GM->UpdatePlaneVisibility();
	if (UMinAudio* A = GetAudio()) A->Play(EMinSfx::PlaneSwitch);
}

void AMinPlayerController::ToggleSideView()
{
	AMinPawn* P = GetMinPawn();
	if (!P || bInTitle) return;
	if (bDraggingRoute && GetGM() && GetGM()->GetPlaneActor(DragPlane))
		GetGM()->GetPlaneActor(DragPlane)->ClearPreview();
	bDraggingRoute = false;
	bErasing = false;
	DragPath.Reset();
	P->bSideView = !P->bSideView;
	P->bMatrixView = false;
	P->ClampPan();
	if (AMinGameMode* GM = GetGM()) GM->UpdatePlaneVisibility();
	if (UMinAudio* A = GetAudio()) A->Play(EMinSfx::Whoosh);
	ShowToast(P->bSideView
		? TEXT("Side view - tunnels here run front-to-back across the planes")
		: TEXT("Front view"));
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
		if (bInTitle || Sim->bGameOver) return;
		Sim->bPaused = !Sim->bPaused;
		ShowToast(Sim->bPaused ? TEXT("Paused") : TEXT("Running"));
	}
}

void AMinPlayerController::OnSpeed()
{
	if (UMinSim* Sim = GetSim())
	{
		if (bInTitle || Sim->bGameOver) return;
		Sim->SimSpeed = Sim->SimSpeed >= 3.f ? 1.f : Sim->SimSpeed * 1.75f;
		ShowToast(FString::Printf(TEXT("Speed x%.1f"), Sim->SimSpeed));
	}
}

void AMinPlayerController::OnEscape()
{
	if (bInTitle) { QuitGame(); return; }
	if (Tool != EMinTool::None)
	{
		Tool = EMinTool::None;
		bDraggingRoute = false;
		bErasing = false;
		DragPath.Reset();
		if (AMinGameMode* GM = GetGM())
			for (int32 P = 0; P < Min::NP; ++P)
				if (AMinPlaneActor* A = GM->GetPlaneActor(P)) A->ClearPreview();
	}
	else BackToTitle();
}

bool AMinPlayerController::GetMouseCell(FIntPoint& OutCell) const
{
	AMinPawn* P = GetMinPawn();
	if (!P || P->bMatrixView) return false;
	float MX, MY;
	if (!GetMousePosition(MX, MY)) return false;

	FVector Origin, Dir;
	if (!UGameplayStatics::DeprojectScreenToWorld(this, FVector2D(MX, MY), Origin, Dir))
		return false;

	const int32 Plane = P->CurrentPlane();
	float U, V;
	if (!P->bSideView)
	{
		if (FMath::IsNearlyZero(Dir.Y)) return false;
		const float PlaneY = Min::FrontPlaneY(Plane);
		const float T = (PlaneY - Origin.Y) / Dir.Y;
		const FVector W = Origin + Dir * T;
		U = W.X; V = -W.Z;
	}
	else
	{
		if (FMath::IsNearlyZero(Dir.X)) return false;
		const float PlaneX = Min::SidePlaneX(Plane - Min::NumFront);
		const float T = (PlaneX - Origin.X) / Dir.X;
		const FVector W = Origin + Dir * T;
		U = W.Y; V = -W.Z;
	}
	OutCell = FIntPoint(FMath::FloorToInt(U / Min::CellSize), FMath::FloorToInt(V / Min::CellSize));
	return OutCell.X >= 0 && OutCell.X < Min::PlaneW(Plane)
		&& OutCell.Y >= 0 && OutCell.Y < Min::PlaneH(Plane);
}

void AMinPlayerController::OnLMBDown()
{
	if (bInTitle) return;
	AMinPawn* P = GetMinPawn();
	UMinSim* Sim = GetSim();
	FIntPoint Cell;
	if (!P || !Sim || Sim->bGameOver || !GetMouseCell(Cell)) return;
	UMinAudio* A = GetAudio();

	switch (Tool)
	{
	case EMinTool::Tunnel:
		bDraggingRoute = true;
		DragPlane = P->CurrentPlane();
		DragPath = { Cell };
		break;

	case EMinTool::Erase:
		bErasing = true;
		if (Sim->EraseAt(Cell, P->CurrentPlane()))
		{
			if (A) A->Play(EMinSfx::Remove);
		}
		break;

	default: break;
	}
}

void AMinPlayerController::OnLMBUp()
{
	bErasing = false;
	if (!bDraggingRoute) return;
	bDraggingRoute = false;
	UMinSim* Sim = GetSim();
	AMinGameMode* GM = GetGM();
	UMinAudio* A = GetAudio();
	if (GM && GM->GetPlaneActor(DragPlane)) GM->GetPlaneActor(DragPlane)->ClearPreview();
	if (!Sim || DragPath.Num() < 2) { DragPath.Reset(); return; }

	const FMinBuildCheck Check = Sim->CheckRoute(DragPath, DragPlane);
	if (Check.bValid && Sim->BuildRoute(DragPath, DragPlane))
	{
		if (A) A->Play(EMinSfx::Dig);
		ShowToast(FString::Printf(TEXT("Tunnel dug: %d segments (%d left)"),
			Check.Cost, Sim->SegmentsLeft));
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
	int32 Guard = 128;
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
	AMinPlaneActor* Plane = GM->GetPlaneActor(P->CurrentPlane());
	if (!Plane) return;
	StatusLine.Reset();

	if (Sim->bGameOver) { Plane->ClearPreview(); return; }

	if (bDraggingRoute && DragPath.Num() > 0)
	{
		const FMinBuildCheck Check = Sim->CheckRoute(DragPath, DragPlane);
		Plane->ShowPreview(DragPath, Check.bValid, false);
		StatusLine = Check.bValid
			? FString::Printf(TEXT("%d segments (%d left)"), Check.Cost, Sim->SegmentsLeft)
			: Check.Reason;
		return;
	}

	FIntPoint Cell;
	if (Tool != EMinTool::None && GetMouseCell(Cell))
	{
		TArray<FIntPoint> One = { Cell };
		if (Tool == EMinTool::Tunnel)
		{
			Plane->ShowPreview(One, true, false);
			StatusLine = TEXT("Click and drag to dig");
		}
		else
		{
			Plane->ShowPreview(One, false, true);
			StatusLine = TEXT("Click or drag over tunnel sections to remove them (+1 segment each)");
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
		P->ClampPan();
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
				P->ClampPan();
			}
			LastMouse = FVector2D(MX, MY);
			bDraggingView = true;
		}
	}
	else bDraggingView = false;

	if (!bInTitle)
	{
		if (bDraggingRoute) UpdateDragPath();
		if (bErasing)
		{
			FIntPoint Cell;
			UMinSim* Sim = GetSim();
			if (Sim && !Sim->bGameOver && GetMouseCell(Cell)
				&& Sim->EraseAt(Cell, P->CurrentPlane()))
			{
				if (UMinAudio* A = GetAudio()) A->Play(EMinSfx::Remove, 0.7f);
			}
		}
		UpdatePreview();
	}

	P->UpdateCamera(DeltaTime);
}
