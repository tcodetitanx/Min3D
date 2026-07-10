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
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

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

	Sim->EnsureBase();

	bool bLoadedSave = false;
	if ((GI && GI->bRequestLoadSave) || FParse::Param(FCommandLine::Get(), TEXT("MinLoad")))
	{
		if (UMinSim::SaveExists()) bLoadedSave = Sim->LoadGame();
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

	// soft backdrop behind all planes so the void beyond the world reads as
	// deep earth instead of black nothing
	{
		AActor* Backdrop = GetWorld()->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Backdrop);
		Backdrop->SetRootComponent(Root);
		Root->RegisterComponent();
		UStaticMeshComponent* Quad = NewObject<UStaticMeshComponent>(Backdrop);
		Quad->SetStaticMesh(Assets->GetQuad());
		Quad->SetMaterial(0, Assets->MakeTexMID(Quad, nullptr, FLinearColor(0.055f, 0.048f, 0.058f)));
		Quad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Quad->SetCastShadow(false);
		Quad->SetTranslucentSortPriority(-2);
		Quad->RegisterComponent();
		Quad->SetWorldLocation(FVector(Min::NX * Min::CellSize * 0.5f, -2.f * Min::PlaneGap,
			-Min::NY * Min::CellSize * 0.5f));
		Quad->SetWorldScale3D(FVector(1200.f, 1.f, 800.f));
	}

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
	const bool bAutoBuild = FParse::Param(FCommandLine::Get(), TEXT("MinAutoBuild"));
	if (bAutoBuild) RunAutoBuild();

	if (!bLoadedSave && !bAutoBuild && !FParse::Param(FCommandLine::Get(), TEXT("MinNoTutorial")))
		SetTutStep(0);

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
	// headless smoke test: mine iron and route it up to the surface Base
	const int32 P = Min::CurrentPlane;
	const FIntVector BC = Sim->BaseCell();
	Sim->Credits += 400; // headless test ignores early-game economy
	for (const FMinDeposit& D : MinWorld->Deposits)
	{
		if (D.Plane != P || D.Resource != EMinResource::Iron) continue;

		// BFS through diggable/open cells from the deposit to the base
		TMap<FIntPoint, FIntPoint> Came;
		TArray<FIntPoint> Queue;
		int32 Head = 0;
		Queue.Add(D.Center);
		Came.Add(D.Center, D.Center);
		const FIntPoint Goal(BC.X, BC.Y);
		bool bFound = false;
		while (Head < Queue.Num())
		{
			const FIntPoint C = Queue[Head++];
			if (C == Goal) { bFound = true; break; }
			const FIntPoint Dirs[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
			for (const FIntPoint& Dir : Dirs)
			{
				const FIntPoint N = C + Dir;
				if (!Min::InGrid(N.X, N.Y, P) || Came.Contains(N)) continue;
				const FMinCell& Cell = MinWorld->Cell(N.X, N.Y, P);
				if (Cell.Terrain == EMinTerrain::Sky || Cell.Terrain == EMinTerrain::Granite) continue;
				if (Cell.Terrain == EMinTerrain::Aquifer && !Cell.bExcavated) continue;
				if (Cell.StructureId >= 0 && N != Goal) continue;
				Came.Add(N, C);
				Queue.Add(N);
			}
		}
		if (!bFound) continue;
		TArray<FIntPoint> Path;
		for (FIntPoint C = Goal; ; C = Came[C])
		{
			Path.Insert(C, 0);
			if (C == D.Center) break;
		}
		if (!Sim->CheckStructure(EMinStructure::Miner, D.Center, P).bValid) continue;
		const FMinBuildCheck RC = Sim->CheckRoute(Path, P, EMinRoute::Tunnel);
		if (!RC.bValid) { UE_LOG(LogMin, Warning, TEXT("AutoBuild route: %s"), *RC.Reason); continue; }
		Sim->BuildStructure(EMinStructure::Miner, D.Center, P);
		Sim->BuildRoute(Path, P, EMinRoute::Tunnel);

		// smelter on a short spur anchored halfway along the route
		const FIntPoint Anchor = Path[Path.Num() / 2];
		const FIntPoint SpurDirs[4] = { {0, 1}, {0, -1}, {1, 0}, {-1, 0} };
		for (const FIntPoint& Dir : SpurDirs)
		{
			const FIntPoint SmelterSpot = Anchor + Dir * 2;
			TArray<FIntPoint> Spur = { Anchor, Anchor + Dir, SmelterSpot };
			if (Sim->CheckStructure(EMinStructure::Smelter, SmelterSpot, P).bValid
				&& Sim->CheckRoute(Spur, P, EMinRoute::Tunnel).bValid)
			{
				Sim->BuildStructure(EMinStructure::Smelter, SmelterSpot, P);
				Sim->BuildRoute(Spur, P, EMinRoute::Tunnel);
				break;
			}
		}
		UE_LOG(LogMin, Log, TEXT("AutoBuild: iron chain (%d,%d) -> base (%d,%d) plane %d"),
			D.Center.X, D.Center.Y, BC.X, BC.Y, P);
		if (AMinPawn* Pawn = Cast<AMinPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
		{
			Pawn->PanCenter = FVector2D((D.Center.X + BC.X) * 0.5f * Min::CellSize,
				-(D.Center.Y + BC.Y) * 0.5f * Min::CellSize);
			Pawn->OrthoWidth = 4200.f;
		}
		return;
	}
	UE_LOG(LogMin, Warning, TEXT("AutoBuild: no viable iron chain found"));
}

// ---------------------------------------------------------------------------
// Tutorial
// ---------------------------------------------------------------------------
void AMinGameMode::SetTutStep(int32 Step)
{
	TutStep = Step;
	bTutFinal = false;
	TutBaselineEdges = 0;
	if (Sim) for (const FMinEdge& E : Sim->Edges) if (E.bAlive) ++TutBaselineEdges;

	switch (Step)
	{
	case 0:
		TutTitle = TEXT("Welcome to MIN3D");
		TutText = TEXT("The big building on the surface is your BASE - every supply you mine must ultimately arrive there. Pan with WASD or middle-drag, zoom with the mouse wheel.");
		break;
	case 1:
		TutTitle = TEXT("Dig");
		TutText = TEXT("Press 1, then click and drag to dig a tunnel from the marked iron deposit up to the shaft beneath your Base. Release to build.");
		break;
	case 2:
		TutTitle = TEXT("Mine");
		TutText = TEXT("Press 3 and click the marked iron deposit to place a Miner on it.");
		break;
	case 3:
		TutTitle = TEXT("Deliver");
		TutText = TEXT("Carts now haul ore home on their own - as long as your tunnel actually reaches the Base. Dig more if it doesn't. Waiting for your first delivery...");
		break;
	case 4:
		TutTitle = TEXT("Planes");
		TutText = TEXT("This world is 10 parallel planes. Press Q/E to switch planes, G toggles neighbor ghosts, Tab opens the 3D matrix overview.");
		break;
	case 5:
		TutTitle = TEXT("Lifts");
		TutText = TEXT("Richer ore hides on other planes. Press 8, click a cell, switch planes with Q/E, then click again to connect them with a Lift. (Or press Next to skip.)");
		break;
	case 6:
		TutTitle = TEXT("Good digging!");
		TutText = TEXT("Smelters [5] refine ore into ingots, Generators [7] burn coal to power nearby machines, Pipes [2] carry water across aquifers. The contracts in the top right guide you from here.");
		bTutFinal = true;
		break;
	default:
		TutStep = -1;
		break;
	}
}

void AMinGameMode::TutorialNext()
{
	if (TutStep < 0) return;
	if (TutStep >= 6) { TutorialSkip(); return; }
	SetTutStep(TutStep + 1);
	if (Audio) Audio->Play(EMinSfx::Click);
}

void AMinGameMode::TutorialSkip()
{
	TutStep = -1;
	if (TutMarkerActor) TutMarkerActor->SetActorHiddenInGame(true);
}

void AMinGameMode::TickTutorial(float DeltaSeconds)
{
	if (TutStep < 0 || !Sim) return;
	AMinPawn* Pawn = Cast<AMinPawn>(UGameplayStatics::GetPlayerPawn(this, 0));

	// advance on the player's actions
	switch (TutStep)
	{
	case 1:
	{
		int32 Alive = 0;
		for (const FMinEdge& E : Sim->Edges) if (E.bAlive) ++Alive;
		if (Alive > TutBaselineEdges) TutorialNext();
		break;
	}
	case 2:
		for (const FMinStructureInst& S : Sim->Structures)
			if (S.bAlive && S.Type == EMinStructure::Miner) { TutorialNext(); break; }
		break;
	case 3:
		if (Sim->TotalDeliveries > 0) TutorialNext();
		break;
	case 4:
		if (Pawn && (Pawn->ViewPlane != Min::CurrentPlane || Pawn->bMatrixView)) TutorialNext();
		break;
	case 5:
		for (const FMinStructureInst& S : Sim->Structures)
			if (S.bAlive && S.Type == EMinStructure::Lift) { TutorialNext(); break; }
		break;
	default: break;
	}
	if (TutStep < 0) return;

	// pulsing ring over the current objective
	TutTarget = FIntVector(-1, -1, -1);
	if (TutStep == 1 || TutStep == 2)
	{
		const FIntVector BC = Sim->BaseCell();
		int32 BestDist = MAX_int32;
		for (const FMinDeposit& D : MinWorld->Deposits)
		{
			if (D.Plane != Min::CurrentPlane || D.Resource != EMinResource::Iron || D.Remaining <= 0) continue;
			const int32 Dist = FMath::Abs(D.Center.X - BC.X) + FMath::Abs(D.Center.Y - BC.Y);
			if (Dist < BestDist) { BestDist = Dist; TutTarget = FIntVector(D.Center.X, D.Center.Y, D.Plane); }
		}
	}
	else if (TutStep == 3) TutTarget = Sim->BaseCell();

	if (!TutMarkerActor && Assets)
	{
		TutMarkerActor = GetWorld()->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(TutMarkerActor);
		TutMarkerActor->SetRootComponent(Root);
		Root->RegisterComponent();
		TutMarker = Assets->MakeSpriteComponent(TutMarkerActor, TEXT("veh_09_06"), 300.f,
			FLinearColor(1.f, 0.85f, 0.4f), 0.9f, /*SortPriority=*/6);
	}
	if (TutMarkerActor && TutMarker)
	{
		const bool bShow = TutTarget.X >= 0 && Pawn && !Pawn->bMatrixView && Pawn->ViewPlane == TutTarget.Z;
		TutMarkerActor->SetActorHiddenInGame(!bShow);
		if (bShow)
		{
			TutPulse += DeltaSeconds * 3.f;
			const float S = 3.0f + FMath::Sin(TutPulse) * 0.35f;
			TutMarker->SetWorldLocation(Min::CellToWorld(TutTarget.X, TutTarget.Y, TutTarget.Z, Min::DepthFX));
			TutMarker->SetWorldScale3D(FVector(S, 1.f, S));
		}
	}
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

	TickTutorial(DeltaSeconds);

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
		FScreenshotRequest::RequestScreenshot(true /*show UI*/);
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
		FPlatformMisc::RequestExit(false);
	}
}
