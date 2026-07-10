#include "MinGameMode.h"
#include "Min3D.h"
#include "MinWorld.h"
#include "MinAssets.h"
#include "MinSim.h"
#include "MinAudio.h"
#include "MinPlaneActor.h"
#include "MinPawn.h"
#include "MinPlayerController.h"
#include "MinGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AMinGameMode::AMinGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AMinPawn::StaticClass();
	PlayerControllerClass = AMinPlayerController::StaticClass();
}

void AMinGameMode::BeginPlay()
{
	Super::BeginPlay();

	UMinGameInstance* GI = Cast<UMinGameInstance>(GetGameInstance());
	MinWorld = GetGameInstance()->GetSubsystem<UMinWorld>();
	Assets = GetGameInstance()->GetSubsystem<UMinAssets>();
	Audio = GetGameInstance()->GetSubsystem<UMinAudio>();

	int32 Seed = GI ? GI->WorldSeed : 824711;
	FParse::Value(FCommandLine::Get(), TEXT("MinSeed="), Seed);
	MinWorld->Generate(Seed);

	Sim = NewObject<UMinSim>(this);
	Sim->Init(MinWorld, Assets, this);
	Sim->Audio = Audio;
	Sim->OnEvent.AddLambda([this](const FString& Msg)
	{
		UE_LOG(LogMin, Log, TEXT("EVENT %s"), *Msg);
		if (AMinPlayerController* PC = Cast<AMinPlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
		{
			PC->ToastLine = Msg;
			PC->ToastTimer = 6.f;
		}
		if (Audio) Audio->Play(EMinSfx::Chime);
	});

	if ((GI && GI->bRequestLoadSave) || FParse::Param(FCommandLine::Get(), TEXT("MinLoad")))
	{
		if (UMinSim::SaveExists()) Sim->LoadGame();
		if (GI) GI->bRequestLoadSave = false;
	}

	PlaneActors.SetNum(Min::NP);
	for (int32 P = 0; P < Min::NP; ++P)
	{
		AMinPlaneActor* A = GetWorld()->SpawnActor<AMinPlaneActor>();
		A->Init(P, MinWorld, Assets);
		PlaneActors[P] = A;
	}
	UpdatePlaneVisibility();

	Audio->EnsurePlaying(GetWorld());

	float V;
	if (FParse::Value(FCommandLine::Get(), TEXT("MinAutoShot="), V)) { AutoShotAt = V; NextShotAt = V; }
	if (FParse::Value(FCommandLine::Get(), TEXT("MinAutoQuit="), V)) AutoQuitAt = V;
	float Zoom;
	if (FParse::Value(FCommandLine::Get(), TEXT("MinZoom="), Zoom))
	{
		if (AMinPawn* Pawn = Cast<AMinPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
			Pawn->OrthoWidth = Zoom;
	}
	float Fast;
	if (FParse::Value(FCommandLine::Get(), TEXT("MinFast="), Fast)) Sim->SimSpeed = Fast;
	if (FParse::Param(FCommandLine::Get(), TEXT("MinAutoBuild"))) RunAutoBuild();

	UE_LOG(LogMin, Log, TEXT("Min3D up: %dx%dx%d cells, %d deposits, %d credits"),
		Min::NX, Min::NY, Min::NP, MinWorld->Deposits.Num(), Sim->Credits);
}

void AMinGameMode::UpdatePlaneVisibility()
{
	AMinPlayerController* PC = Cast<AMinPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	AMinPawn* Pawn = PC ? Cast<AMinPawn>(PC->GetPawn()) : nullptr;
	const int32 Cur = Pawn ? Pawn->ViewPlane : Min::CurrentPlane;
	const bool bMatrix = Pawn && Pawn->bMatrixView;

	for (int32 P = 0; P < Min::NP; ++P)
	{
		if (!PlaneActors[P]) continue;
		if (bMatrix)
		{
			PlaneActors[P]->SetViewOpacity(P == Cur ? 0.22f : 0.035f, 0.9f, P == Cur);
		}
		else
		{
			const int32 Dist = FMath::Abs(P - Cur);
			float Content = 0.f;
			if (Dist == 0) Content = 1.f;
			else if (bGhostPlanes && Dist == 1) Content = 0.20f;
			else if (bGhostPlanes && Dist == 2) Content = 0.08f;
			PlaneActors[P]->SetViewOpacity(Dist == 0 ? 1.f : 0.f, Content, Dist == 0);
		}
	}
}

void AMinGameMode::RunAutoBuild()
{
	// headless smoke test: assemble a full iron chain on the starting plane
	const int32 P = Min::CurrentPlane;
	for (const FMinDeposit& D : MinWorld->Deposits)
	{
		if (D.Plane != P || D.Resource != EMinResource::Iron) continue;
		// try a spot to the side for the depot
		for (int32 DX : { -9, 9, -13, 13 })
		{
			const FIntPoint DepotCell(D.Center.X + DX, D.Center.Y);
			const FIntPoint MinerCell = D.Center;
			// L-shaped corridor
			TArray<FIntPoint> Path;
			const int32 Step = DX > 0 ? 1 : -1;
			for (int32 X = MinerCell.X; X != DepotCell.X + Step; X += Step)
				Path.Add(FIntPoint(X, MinerCell.Y));
			if (!Sim->CheckStructure(EMinStructure::Miner, MinerCell, P).bValid) continue;
			if (!Sim->BuildStructure(EMinStructure::Miner, MinerCell, P)) continue;
			if (!Sim->CheckStructure(EMinStructure::Depot, DepotCell, P).bValid) continue;
			Sim->BuildStructure(EMinStructure::Depot, DepotCell, P);
			const FMinBuildCheck RC = Sim->CheckRoute(Path, P, EMinRoute::Tunnel);
			if (!RC.bValid) { UE_LOG(LogMin, Warning, TEXT("AutoBuild route invalid: %s"), *RC.Reason); continue; }
			Sim->BuildRoute(Path, P, EMinRoute::Tunnel);
			// smelter on a spur halfway along the line + generator near the depot
			const FIntPoint SmelterCell(MinerCell.X + DX / 2, MinerCell.Y);
			const FIntPoint SmelterSpot(SmelterCell.X, SmelterCell.Y + 2);
			if (Sim->CheckStructure(EMinStructure::Smelter, SmelterSpot, P).bValid)
			{
				Sim->BuildStructure(EMinStructure::Smelter, SmelterSpot, P);
				TArray<FIntPoint> Spur = { FIntPoint(SmelterCell.X, SmelterCell.Y),
					FIntPoint(SmelterCell.X, SmelterCell.Y + 1), SmelterSpot };
				Sim->BuildRoute(Spur, P, EMinRoute::Tunnel);
			}
			const FIntPoint GenSpot(DepotCell.X, DepotCell.Y + 2);
			if (Sim->CheckStructure(EMinStructure::Generator, GenSpot, P).bValid)
				Sim->BuildStructure(EMinStructure::Generator, GenSpot, P);
			UE_LOG(LogMin, Log, TEXT("AutoBuild: chain at iron deposit %d (%d,%d) plane %d"),
				D.Id, D.Center.X, D.Center.Y, P);
			// center camera on the action
			if (AMinPawn* Pawn = Cast<AMinPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
			{
				Pawn->PanCenter = FVector2D(D.Center.X * Min::CellSize, -D.Center.Y * Min::CellSize);
				Pawn->OrthoWidth = 3200.f;
			}
			return;
		}
	}
	UE_LOG(LogMin, Warning, TEXT("AutoBuild: no viable iron chain found"));
}

void AMinGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RunTime += DeltaSeconds;

	if (Sim)
	{
		Sim->Tick(DeltaSeconds);
		if (Audio) Audio->TickMusic(DeltaSeconds);

		if (Sim->bTopologyDirty)
		{
			Sim->bTopologyDirty = false;
			for (AMinPlaneActor* A : PlaneActors) if (A) A->RebuildRoutes(Sim);
		}
		if (Sim->bStructuresDirty)
		{
			Sim->bStructuresDirty = false;
			for (AMinPlaneActor* A : PlaneActors)
				if (A) { A->SyncStructures(Sim); A->RefreshDepositMarkers(); }
		}
		// congestion recolor at 2 Hz
		static float RecolorTimer = 0.f;
		RecolorTimer += DeltaSeconds;
		if (RecolorTimer > 0.5f)
		{
			RecolorTimer = 0.f;
			for (AMinPlaneActor* A : PlaneActors) if (A) A->RebuildRoutes(Sim);
		}
		for (AMinPlaneActor* A : PlaneActors) if (A) A->UpdateCarts(Sim);
	}

	// process dirty terrain regions
	for (int32 P = 0; P < Min::NP; ++P)
	{
		if (!MinWorld->DirtyRects[P].IsEmpty() && PlaneActors.IsValidIndex(P) && PlaneActors[P])
		{
			PlaneActors[P]->RebuildTerrainTexture(MinWorld->DirtyRects[P]);
			MinWorld->DirtyRects[P] = FIntRect();
		}
	}

	if (RunTime >= HeartbeatAt)
	{
		HeartbeatAt = RunTime + 5.f;
		AMinPawn* Pawn = Cast<AMinPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
		FIntPoint MouseCell(-1, -1);
		if (AMinPlayerController* PC = Cast<AMinPlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
			PC->GetMouseCell(MouseCell);
		UE_LOG(LogMin, Log, TEXT("STATE t=%.0f plane=%d matrix=%d credits=%d carts=%d deliveries=%d structs=%d edges=%d mouse=(%d,%d)"),
			RunTime, Pawn ? Pawn->ViewPlane : -1, Pawn ? (int32)Pawn->bMatrixView : 0,
			Sim ? Sim->Credits : 0, Sim ? Sim->CartsInFlight() : 0,
			Sim ? Sim->TotalDeliveries : 0, Sim ? Sim->Structures.Num() : 0, Sim ? Sim->Edges.Num() : 0,
			MouseCell.X, MouseCell.Y);
	}

	if (NextShotAt > 0.f && RunTime >= NextShotAt && ShotsTaken < 4)
	{
		++ShotsTaken;
		NextShotAt = RunTime + 12.f;
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
			PC->ConsoleCommand(TEXT("Shot showui"));
	}

	// scripted view changes for headless verification
	static bool bDidAutoPlane = false, bDidAutoMatrix = false;
	float AutoV;
	if (!bDidAutoPlane && FParse::Value(FCommandLine::Get(), TEXT("MinAutoPlane="), AutoV) && RunTime >= AutoV)
	{
		bDidAutoPlane = true;
		if (AMinPlayerController* PC = Cast<AMinPlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
			PC->SwitchPlane(-1);
	}
	if (!bDidAutoMatrix && FParse::Value(FCommandLine::Get(), TEXT("MinAutoMatrix="), AutoV) && RunTime >= AutoV)
	{
		bDidAutoMatrix = true;
		if (AMinPlayerController* PC = Cast<AMinPlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
			PC->ToggleMatrix();
	}
	static bool bDidAutoSave = false;
	if (!bDidAutoSave && FParse::Value(FCommandLine::Get(), TEXT("MinAutoSave="), AutoV) && RunTime >= AutoV)
	{
		bDidAutoSave = true;
		if (Sim) Sim->SaveGame();
	}
	if (AutoQuitAt > 0.f && RunTime >= AutoQuitAt)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
			PC->ConsoleCommand(TEXT("quit"));
	}
}
