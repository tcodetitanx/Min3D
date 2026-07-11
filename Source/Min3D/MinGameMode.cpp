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
	});

	PlaneActors.SetNum(Min::NP);
	for (int32 P = 0; P < Min::NP; ++P)
	{
		AMinPlaneActor* A = GetWorld()->SpawnActor<AMinPlaneActor>();
		A->Init(P, MinWorld, Assets);
		PlaneActors[P] = A;
	}
	UpdatePlaneVisibility();
	SpawnBackdrops();

	Audio->EnsurePlaying(GetWorld());

	float V;
	if (FParse::Value(FCommandLine::Get(), TEXT("MinAutoShot="), V)) NextShotAt = V;
	if (FParse::Value(FCommandLine::Get(), TEXT("MinAutoQuit="), V)) AutoQuitAt = V;
	float Zoom;
	if (FParse::Value(FCommandLine::Get(), TEXT("MinZoom="), Zoom))
	{
		if (AMinPawn* Pawn = Cast<AMinPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
			Pawn->OrthoWidth = Zoom;
	}

	// restarts and smoke tests begin the run immediately, skipping the title
	const bool bSkipTitle = (GI && GI->bSkipTitle)
		|| FParse::Param(FCommandLine::Get(), TEXT("MinAutoBuild"))
		|| FParse::Param(FCommandLine::Get(), TEXT("MinSkipTitle"));
	if (bSkipTitle)
	{
		EMinGameRules Rules = GI ? GI->PendingRules : EMinGameRules::SinglePlane;
		if (FParse::Param(FCommandLine::Get(), TEXT("MinCross"))) Rules = EMinGameRules::CrossPlane;
		BeginRun(Rules);
	}

	UE_LOG(LogMin, Log, TEXT("Min3D V2 up: %dx%dx%d cells, %d planes"),
		Min::GX, Min::GD, Min::GZ, Min::NP);
}

void AMinGameMode::BeginRun(EMinGameRules Rules)
{
	if (!Sim || Sim->bRunStarted) return;
	float Fast;
	if (FParse::Value(FCommandLine::Get(), TEXT("MinFast="), Fast)) Sim->SimSpeed = Fast;
	Sim->StartRun(Rules);
	if (FParse::Param(FCommandLine::Get(), TEXT("MinAutoBuild"))) RunAutoBuild();
	UpdatePlaneVisibility();
}

void AMinGameMode::SpawnBackdrops()
{
	// soft dark quads behind the world so the void beyond reads as deep earth
	auto MakeBackdrop = [&](const FVector& Loc, const FRotator& Rot)
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
		Quad->SetWorldLocation(Loc);
		Quad->SetWorldRotation(Rot);
		Quad->SetWorldScale3D(FVector(1200.f, 1.f, 800.f));
	};
	// behind the front view (looking -Y) and behind the side view (looking +X)
	MakeBackdrop(FVector(Min::GX * Min::CellSize * 0.5f, -2000.f, -Min::GZ * Min::CellSize * 0.5f),
		FRotator::ZeroRotator);
	MakeBackdrop(FVector(Min::GX * Min::CellSize + 2000.f, Min::GD * Min::CellSize * 0.5f, -Min::GZ * Min::CellSize * 0.5f),
		FRotator(0.f, 90.f, 0.f));
}

void AMinGameMode::UpdatePlaneVisibility()
{
	AMinPlayerController* PC = Cast<AMinPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	AMinPawn* Pawn = PC ? Cast<AMinPawn>(PC->GetPawn()) : nullptr;
	const bool bMatrix = Pawn && Pawn->bMatrixView;
	const bool bSide = Pawn && Pawn->bSideView;
	const int32 Cur = Pawn ? Pawn->CurrentPlane() : 1;

	for (int32 P = 0; P < Min::NP; ++P)
	{
		if (!PlaneActors[P]) continue;
		if (bMatrix)
		{
			PlaneActors[P]->SetViewOpacity(P == Cur ? 0.22f : 0.05f, 0.9f);
			continue;
		}
		const bool bSameOrientation = Min::IsFront(P) != bSide;
		if (!bSameOrientation)
		{
			// perpendicular planes are edge-on in this view
			PlaneActors[P]->SetViewOpacity(0.f, 0.f);
			continue;
		}
		const int32 Dist = FMath::Abs(P - Cur);
		float Content = 0.f;
		if (Dist == 0) Content = 1.f;
		else if (bGhostPlanes && Dist == 1) Content = 0.20f;
		else if (bGhostPlanes && Dist == 2) Content = 0.08f;
		PlaneActors[P]->SetViewOpacity(Dist == 0 ? 1.f : 0.f, Content);
	}
}

void AMinGameMode::RunAutoBuild()
{
	// headless smoke test: tunnel from the first pool to the first plant
	if (!Sim || Sim->Pools.Num() == 0 || Sim->Plants.Num() == 0) return;
	Sim->SegmentsLeft += 300;
	const FMinPool& Pool = Sim->Pools[0];
	const FMinPlant& Plant = Sim->Plants[0];

	int32 PoolPlanes[2], PlantPlanes[2];
	if (Min::PlanesOfCell(Pool.Cell, PoolPlanes) == 0) return;
	if (Min::PlanesOfCell(Plant.Cell, PlantPlanes) == 0) return;
	const int32 P = PoolPlanes[0];
	if (PlantPlanes[0] != P) { UE_LOG(LogMin, Warning, TEXT("AutoBuild: pool/plant on different planes")); return; }

	FIntPoint A, B;
	Min::CellToPlaneUV(Pool.Cell, P, A);
	Min::CellToPlaneUV(Plant.Cell, P, B);

	// BFS through diggable plane cells
	TMap<FIntPoint, FIntPoint> Came;
	TArray<FIntPoint> Queue;
	int32 Head = 0;
	Queue.Add(A);
	Came.Add(A, A);
	bool bFound = false;
	while (Head < Queue.Num())
	{
		const FIntPoint C = Queue[Head++];
		if (C == B) { bFound = true; break; }
		const FIntPoint Dirs[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
		for (const FIntPoint& Dir : Dirs)
		{
			const FIntPoint N = C + Dir;
			if (N.X < 0 || N.X >= Min::PlaneW(P) || N.Y < 0 || N.Y >= Min::PlaneH(P)) continue;
			if (Came.Contains(N)) continue;
			const FMinCell& Cell = MinWorld->PlaneCell(P, N.X, N.Y);
			if (Cell.Terrain == EMinTerrain::Sky || Cell.Terrain == EMinTerrain::Granite) continue;
			if ((Cell.PoolId >= 0 || Cell.PlantId >= 0 || Cell.bDepot) && N != B) continue;
			Came.Add(N, C);
			Queue.Add(N);
		}
	}
	if (!bFound) { UE_LOG(LogMin, Warning, TEXT("AutoBuild: no path")); return; }

	TArray<FIntPoint> Path;
	for (FIntPoint C = B; ; C = Came[C])
	{
		Path.Insert(C, 0);
		if (C == A) break;
	}
	// buildings themselves cannot carry rails - drop the endpoints
	if (Path.Num() >= 2) { Path.RemoveAt(0); Path.Pop(); }
	if (Path.Num() < 2) { UE_LOG(LogMin, Warning, TEXT("AutoBuild: path too short")); return; }
	const FMinBuildCheck RC = Sim->CheckRoute(Path, P);
	if (!RC.bValid) { UE_LOG(LogMin, Warning, TEXT("AutoBuild route: %s"), *RC.Reason); return; }
	Sim->BuildRoute(Path, P);
	UE_LOG(LogMin, Log, TEXT("AutoBuild: connected pool (%d,%d) -> plant (%d,%d) on %s"),
		A.X, A.Y, B.X, B.Y, *Min::PlaneName(P));

	if (AMinPawn* Pawn = Cast<AMinPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		Pawn->PanCenter = FVector2D((A.X + B.X) * 0.5f * Min::CellSize,
			-(A.Y + B.Y) * 0.5f * Min::CellSize);
		Pawn->OrthoWidth = 6000.f;
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
		// markers animate (pins, overflow rings), so refresh on a short timer too
		MarkerRefreshT += DeltaSeconds;
		if (Sim->bStructuresDirty || MarkerRefreshT > 0.25f)
		{
			Sim->bStructuresDirty = false;
			MarkerRefreshT = 0.f;
			for (AMinPlaneActor* A : PlaneActors) if (A) A->RebuildMarkers(Sim);
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
		int32 TotalPins = 0;
		if (Sim) for (const FMinPlant& Plant : Sim->Plants) if (Plant.bAlive) TotalPins += Plant.Pins;
		UE_LOG(LogMin, Log, TEXT("STATE t=%.0f plane=%d side=%d matrix=%d score=%d segs=%d pools=%d plants=%d pins=%d carts=%d over=%d"),
			RunTime, Pawn ? Pawn->CurrentPlane() : -1, Pawn ? (int32)Pawn->bSideView : 0,
			Pawn ? (int32)Pawn->bMatrixView : 0,
			Sim ? Sim->Score : 0, Sim ? Sim->SegmentsLeft : 0,
			Sim ? Sim->Pools.Num() : 0, Sim ? Sim->Plants.Num() : 0, TotalPins,
			Sim ? Sim->CartsInFlight() : 0, Sim ? (int32)Sim->bGameOver : 0);
	}

	if (NextShotAt > 0.f && RunTime >= NextShotAt && ShotsTaken < 4)
	{
		++ShotsTaken;
		NextShotAt = RunTime + 12.f;
		FScreenshotRequest::RequestScreenshot(true /*show UI*/);
	}

	// scripted view changes for headless verification
	static bool bDidAutoSide = false, bDidAutoMatrix = false;
	float AutoV;
	if (!bDidAutoSide && FParse::Value(FCommandLine::Get(), TEXT("MinAutoSide="), AutoV) && RunTime >= AutoV)
	{
		bDidAutoSide = true;
		if (AMinPlayerController* PC = Cast<AMinPlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
			PC->ToggleSideView();
	}
	if (!bDidAutoMatrix && FParse::Value(FCommandLine::Get(), TEXT("MinAutoMatrix="), AutoV) && RunTime >= AutoV)
	{
		bDidAutoMatrix = true;
		if (AMinPlayerController* PC = Cast<AMinPlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
			PC->ToggleMatrix();
	}
	if (AutoQuitAt > 0.f && RunTime >= AutoQuitAt)
	{
		FPlatformMisc::RequestExit(false);
	}
}
