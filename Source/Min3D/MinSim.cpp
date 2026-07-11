#include "MinSim.h"
#include "Min3D.h"
#include "MinWorld.h"
#include "MinAssets.h"
#include "MinAudio.h"
#include "MinGameMode.h"
#include "Algo/Reverse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr float CartSpeed = 2.2f;        // cells per second
	constexpr float PlantGrace = 12.f;       // seconds before a new plant demands
	constexpr float SurplusEvery = 50.f;     // pool bonus load cadence
	constexpr float SegmentDropEvery = 45.f;
	constexpr int32 SegmentDropAmount = 12;
	constexpr int32 MaxPools = 26;
	constexpr int32 MaxPlants = 10;

	// activation time (seconds) of each color; index = color slot
	constexpr float ColorAt[MinMaxColors] = { 0.f, 150.f, 330.f, 540.f, 780.f };
	// SinglePlane mode: home plane per color slot (mix of front and side planes)
	constexpr int32 ColorHome[MinMaxColors] = { 1, 4, 2, 3, 0 };

	FString ScorePath()
	{
		return FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("min3d_scores.json");
	}
	const TCHAR* RulesKey(EMinGameRules R)
	{
		return R == EMinGameRules::SinglePlane ? TEXT("single") : TEXT("cross");
	}

	// primary display plane of a structure cell
	int32 PlaneOf(const FIntVector& C)
	{
		const int32 F = Min::FrontIndexOfY(C.Y);
		return F >= 0 ? F : Min::NumFront + Min::SideIndexOfX(C.X);
	}
}

void UMinSim::Init(UMinWorld* InWorld, UMinAssets* InAssets, AMinGameMode* InGM)
{
	World = InWorld;
	Assets = InAssets;
	GM = InGM;
}

float UMinSim::Difficulty01() const
{
	return FMath::Clamp(PlayTime / 600.f, 0.f, 1.f);
}

int32 UMinSim::CartsInFlight() const
{
	int32 N = 0;
	for (const FMinCart& C : Carts) if (C.bAlive) ++N;
	return N;
}

// ---------------------------------------------------------------------------
// Run setup
// ---------------------------------------------------------------------------
void UMinSim::StartRun(EMinGameRules InRules)
{
	if (bRunStarted) return;
	Rules = InRules;
	Rng.Initialize(World->WorldSeed * 3 + 1);
	LoadHighScore();

	PlaceDepot();
	ColorsActive = 1;
	ActivateColor(0);

	PoolSpawnT = 0.f;
	PlantSpawnT = 0.f;
	SegmentDropT = 0.f;
	bRunStarted = true;
	UE_LOG(LogMin, Log, TEXT("Run started: rules=%s seed=%d"),
		Rules == EMinGameRules::SinglePlane ? TEXT("SinglePlane") : TEXT("CrossPlane"), World->WorldSeed);
}

void UMinSim::PlaceDepot()
{
	// the depot sits on the middle front plane, connected to the surface by a
	// starter shaft of free tunnel
	const int32 P = 1;
	int32 U = Min::GX / 2 - 1;
	if (Min::SideIndexOfX(U) >= 0) --U; // keep transfer columns free
	int32 SurfV = World->SurfaceV(P, U);

	// nudge sideways if granite blocks the shaft
	for (int32 Try = 0; Try < 10; ++Try)
	{
		bool bOk = true;
		for (int32 V = SurfV; V <= SurfV + 3; ++V)
			if (!Min::InBox(Min::PlaneToCell(P, U, V)) ||
				World->PlaneCell(P, U, V).Terrain == EMinTerrain::Granite) { bOk = false; break; }
		if (bOk) break;
		U += (Try % 2 ? 1 : -1) * (Try / 2 + 1);
		if (Min::SideIndexOfX(U) >= 0) ++U;
		SurfV = World->SurfaceV(P, U);
	}

	const int32 DepotV = SurfV + 3;
	for (int32 V = SurfV; V < DepotV; ++V)
	{
		const FIntVector C = Min::PlaneToCell(P, U, V);
		World->Excavate(C);
		RouteCells.Add(C);
	}
	DepotCell = Min::PlaneToCell(P, U, DepotV);
	World->Excavate(DepotCell);
	World->Cell(DepotCell).bDepot = true;
	// small chamber
	for (int32 DU : { -1, 1 })
	{
		const FIntVector C = Min::PlaneToCell(P, U + DU, DepotV);
		if (Min::InBox(C)) World->Excavate(C);
	}
	bTopologyDirty = true;
	bStructuresDirty = true;
	UE_LOG(LogMin, Log, TEXT("Depot at (%d,%d,%d)"), DepotCell.X, DepotCell.Y, DepotCell.Z);
}

int32 UMinSim::PlaneForSpawn(int32 ColorIdx, bool bPlant, int32 AvoidPlane)
{
	if (Rules == EMinGameRules::SinglePlane) return ColorHome[ColorIdx];
	int32 P = Rng.RandRange(0, Min::NP - 1);
	if (P == AvoidPlane) P = (P + 1 + Rng.RandRange(0, Min::NP - 2)) % Min::NP;
	return P;
}

bool UMinSim::PickSpawnCell(int32 Plane, int32 MinDist, FIntVector& Out, const FIntVector* Near)
{
	FIntPoint NearUV(-100, -100);
	if (Near) Min::CellToPlaneUV(*Near, Plane, NearUV);

	for (int32 Try = 0; Try < 80; ++Try)
	{
		int32 U, V;
		if (Near && NearUV.X >= 0 && Try < 50)
		{
			U = FMath::Clamp(NearUV.X + Rng.RandRange(-7, 7), 1, Min::PlaneW(Plane) - 2);
			V = FMath::Clamp(NearUV.Y + Rng.RandRange(-5, 5), Min::SkyRows + 2, Min::PlaneH(Plane) - 2);
		}
		else
		{
			U = Rng.RandRange(1, Min::PlaneW(Plane) - 2);
			V = Rng.RandRange(Min::SkyRows + 2, Min::PlaneH(Plane) - 2);
		}
		const FIntVector C = Min::PlaneToCell(Plane, U, V);
		if (Min::IsTransferCell(C)) continue; // keep transfer columns free
		const FMinCell& K = World->Cell(C);
		if (K.Terrain == EMinTerrain::Sky || K.Terrain == EMinTerrain::Granite) continue;
		if (K.PoolId >= 0 || K.PlantId >= 0 || K.bDepot) continue;
		if (RouteCells.Contains(C)) continue;

		bool bTooClose = false;
		auto CheckDist = [&](const FIntVector& Other)
		{
			FIntPoint OUV;
			if (Min::CellToPlaneUV(Other, Plane, OUV) &&
				FMath::Abs(OUV.X - U) + FMath::Abs(OUV.Y - V) < MinDist)
				bTooClose = true;
		};
		for (const FMinPool& Pool : Pools) if (Pool.bAlive) CheckDist(Pool.Cell);
		for (const FMinPlant& Plant : Plants) if (Plant.bAlive) CheckDist(Plant.Cell);
		if (DepotCell.X >= 0) CheckDist(DepotCell);
		if (bTooClose) continue;

		Out = C;
		return true;
	}
	return false;
}

bool UMinSim::SpawnPool(EMinResource Res, int32 Plane, const FIntVector* Near)
{
	FIntVector C;
	if (!PickSpawnCell(Plane, 3, C, Near)) return false;
	FMinPool Pool;
	Pool.Id = Pools.Num();
	Pool.Res = Res;
	Pool.Cell = C;
	Pool.CartsFree = MaxPoolCarts;
	World->Cell(C).PoolId = (int16)Pool.Id;
	World->Excavate(C);
	World->MarkDirtyCell(C);
	Pools.Add(Pool);
	bStructuresDirty = true;
	OnEvent.Broadcast(FString::Printf(TEXT("%s pool found on %s"),
		MinResInfo(Res).Name, *Min::PlaneName(Plane)));
	return true;
}

bool UMinSim::SpawnPlant(EMinResource Res, int32 Plane)
{
	FIntVector C;
	if (!PickSpawnCell(Plane, 5, C)) return false;
	FMinPlant Plant;
	Plant.Id = Plants.Num();
	Plant.Res = Res;
	Plant.Cell = C;
	Plant.OverflowT = OverflowMax;
	World->Cell(C).PlantId = (int16)Plant.Id;
	// carve a chamber so the plant reads as a built structure
	World->Excavate(C);
	FIntPoint UV;
	Min::CellToPlaneUV(C, Plane, UV);
	const FIntPoint Dirs[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
	for (const FIntPoint& D : Dirs)
	{
		const FIntVector N = Min::PlaneToCell(Plane, UV.X + D.X, UV.Y + D.Y);
		if (Min::InBox(N) && !Min::IsTransferCell(N)) World->Excavate(N);
	}
	Plants.Add(Plant);
	bStructuresDirty = true;
	OnEvent.Broadcast(FString::Printf(TEXT("%s plant built on %s - keep it supplied!"),
		MinResInfo(Res).Name, *Min::PlaneName(Plane)));
	return true;
}

void UMinSim::ActivateColor(int32 ColorIdx)
{
	const EMinResource Res = (EMinResource)(ColorIdx + 1);
	const int32 PlantPlane = PlaneForSpawn(ColorIdx, true);
	SpawnPlant(Res, PlantPlane);

	// pools: in CrossPlane mode force them onto another plane so routing has
	// to cross through the perpendicular grid
	const int32 PoolPlane = PlaneForSpawn(ColorIdx, false,
		Rules == EMinGameRules::CrossPlane ? PlantPlane : -1);
	if (ColorIdx == 0)
	{
		// the very first pools hug the depot so the opening is easy
		SpawnPool(Res, 1, &DepotCell);
		SpawnPool(Res, PoolPlane == 1 ? 1 : PoolPlane, &DepotCell);
	}
	else
	{
		SpawnPool(Res, PoolPlane);
		SpawnPool(Res, PoolPlane);
	}
	if (ColorIdx > 0)
		OnEvent.Broadcast(FString::Printf(TEXT("New ore discovered: %s"), MinResInfo(Res).Name));
}

void UMinSim::TickSpawner(float Dt)
{
	// new colors on a fixed timeline
	while (ColorsActive < MinMaxColors && PlayTime >= ColorAt[ColorsActive])
	{
		ActivateColor(ColorsActive);
		++ColorsActive;
	}

	const float D = Difficulty01();

	PoolSpawnT += Dt;
	const float PoolEvery = FMath::Lerp(20.f, 8.f, D);
	if (PoolSpawnT >= PoolEvery)
	{
		PoolSpawnT = 0.f;
		int32 AlivePools = 0;
		for (const FMinPool& Pool : Pools) if (Pool.bAlive) ++AlivePools;
		if (AlivePools < MaxPools)
		{
			// favor the active color with the fewest pools
			int32 Best = 0, BestCount = MAX_int32;
			for (int32 CI = 0; CI < ColorsActive; ++CI)
			{
				int32 N = 0;
				for (const FMinPool& Pool : Pools)
					if (Pool.bAlive && Pool.Res == (EMinResource)(CI + 1)) ++N;
				if (N < BestCount) { BestCount = N; Best = CI; }
			}
			SpawnPool((EMinResource)(Best + 1), PlaneForSpawn(Best, false));
		}
	}

	PlantSpawnT += Dt;
	const float PlantEvery = FMath::Lerp(55.f, 26.f, D);
	if (PlantSpawnT >= PlantEvery)
	{
		PlantSpawnT = 0.f;
		int32 AlivePlants = 0;
		for (const FMinPlant& Plant : Plants) if (Plant.bAlive) ++AlivePlants;
		if (AlivePlants < MaxPlants)
		{
			int32 Best = 0, BestCount = MAX_int32;
			for (int32 CI = 0; CI < ColorsActive; ++CI)
			{
				int32 N = 0;
				for (const FMinPlant& Plant : Plants)
					if (Plant.bAlive && Plant.Res == (EMinResource)(CI + 1)) ++N;
				if (N < BestCount) { BestCount = N; Best = CI; }
			}
			SpawnPlant((EMinResource)(Best + 1), PlaneForSpawn(Best, true));
		}
	}

	SegmentDropT += Dt;
	if (SegmentDropT >= SegmentDropEvery)
	{
		SegmentDropT = 0.f;
		SegmentsLeft += SegmentDropAmount;
		OnEvent.Broadcast(FString::Printf(TEXT("Supply drop: +%d tunnel segments"), SegmentDropAmount));
		if (Audio) Audio->Play(EMinSfx::Coin, 0.6f);
	}
}

// ---------------------------------------------------------------------------
// Build / erase
// ---------------------------------------------------------------------------
bool UMinSim::IsWalkable(const FIntVector& C) const
{
	if (RouteCells.Contains(C)) return true;
	if (!Min::InBox(C)) return false;
	const FMinCell& K = World->Cell(C);
	return K.PoolId >= 0 || K.PlantId >= 0 || K.bDepot;
}

FMinBuildCheck UMinSim::CheckRoute(const TArray<FIntPoint>& UVCells, int32 Plane) const
{
	FMinBuildCheck R;
	if (UVCells.Num() < 2) { R.Reason = TEXT("Drag to draw a tunnel"); return R; }
	int32 Cost = 0;
	for (int32 I = 0; I < UVCells.Num(); ++I)
	{
		const FIntPoint& UV = UVCells[I];
		if (UV.X < 0 || UV.X >= Min::PlaneW(Plane) || UV.Y < 0 || UV.Y >= Min::PlaneH(Plane))
		{
			R.Reason = TEXT("Out of bounds"); return R;
		}
		if (I > 0)
		{
			const FIntPoint D = UV - UVCells[I - 1];
			if (FMath::Abs(D.X) + FMath::Abs(D.Y) != 1) { R.Reason = TEXT("Tunnel must be continuous"); return R; }
		}
		const FIntVector C = Min::PlaneToCell(Plane, UV.X, UV.Y);
		const FMinCell& K = World->Cell(C);
		if (K.Terrain == EMinTerrain::Sky) { R.Reason = TEXT("Cannot build in the sky"); return R; }
		if (K.Terrain == EMinTerrain::Granite) { R.Reason = TEXT("Granite is unbreakable"); return R; }
		if (K.PoolId >= 0 || K.PlantId >= 0 || K.bDepot)
		{
			R.Reason = TEXT("Tunnel up to a building, not through it"); return R;
		}
		if (!RouteCells.Contains(C)) ++Cost; // extending over existing tunnel is free
	}
	R.Cost = Cost;
	if (Cost > SegmentsLeft)
	{
		R.Reason = FString::Printf(TEXT("Needs %d segments (%d left)"), Cost, SegmentsLeft);
		return R;
	}
	R.bValid = true;
	return R;
}

bool UMinSim::BuildRoute(const TArray<FIntPoint>& UVCells, int32 Plane)
{
	const FMinBuildCheck Check = CheckRoute(UVCells, Plane);
	if (!Check.bValid) return false;
	SegmentsLeft -= Check.Cost;
	for (const FIntPoint& UV : UVCells)
	{
		const FIntVector C = Min::PlaneToCell(Plane, UV.X, UV.Y);
		World->Excavate(C);
		RouteCells.Add(C);
	}
	bTopologyDirty = true;
	return true;
}

bool UMinSim::EraseAt(const FIntPoint& UV, int32 Plane)
{
	if (UV.X < 0 || UV.X >= Min::PlaneW(Plane) || UV.Y < 0 || UV.Y >= Min::PlaneH(Plane)) return false;
	const FIntVector C = Min::PlaneToCell(Plane, UV.X, UV.Y);
	if (!RouteCells.Contains(C)) return false;
	RouteCells.Remove(C);
	SegmentsLeft += 1; // the cavity stays; the rails are refunded
	bTopologyDirty = true;
	return true;
}

// ---------------------------------------------------------------------------
// Pathfinding: BFS over the walkable cell network
// ---------------------------------------------------------------------------
bool UMinSim::FindNearest(const FIntVector& From, TFunctionRef<bool(const FIntVector&)> IsGoal,
	TArray<FIntVector>& OutPath) const
{
	if (!IsWalkable(From)) return false;
	TMap<FIntVector, FIntVector> Came;
	TArray<FIntVector> Queue;
	int32 Head = 0;
	Queue.Add(From);
	Came.Add(From, From);

	const FIntVector Dirs[6] = {
		{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1} };

	while (Head < Queue.Num())
	{
		const FIntVector C = Queue[Head++];
		if (IsGoal(C) && C != From)
		{
			OutPath.Reset();
			for (FIntVector Walk = C; ; Walk = Came[Walk])
			{
				OutPath.Insert(Walk, 0);
				if (Walk == From) break;
			}
			return true;
		}
		// buildings are endpoints, not corridors - do not path through them
		if (C != From)
		{
			const FMinCell& K = World->Cell(C);
			if (K.PoolId >= 0 || K.PlantId >= 0 || K.bDepot) continue;
		}
		for (const FIntVector& D : Dirs)
		{
			const FIntVector N = C + D;
			if (Came.Contains(N)) continue;
			if (!Min::CellsLinked(C, N)) continue;
			if (!IsWalkable(N)) continue;
			Came.Add(N, C);
			Queue.Add(N);
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Dispatch and carts
// ---------------------------------------------------------------------------
void UMinSim::Dispatch()
{
	// plants pull carts from the nearest connected same-color pool
	for (FMinPlant& Plant : Plants)
	{
		if (!Plant.bAlive || Plant.Pins - Plant.Inbound <= 0) continue;
		TArray<FIntVector> Path;
		const bool bFound = FindNearest(Plant.Cell, [&](const FIntVector& C)
		{
			const FMinCell& K = World->Cell(C);
			return K.PoolId >= 0 && Pools[K.PoolId].bAlive
				&& Pools[K.PoolId].Res == Plant.Res && Pools[K.PoolId].CartsFree > 0;
		}, Path);
		if (!bFound) continue;

		Algo::Reverse(Path); // pool -> plant
		FMinPool& Pool = Pools[World->Cell(Path[0]).PoolId];
		--Pool.CartsFree;
		++Plant.Inbound;

		FMinCart Cart;
		Cart.Id = NextCartId++;
		Cart.Res = Pool.Res;
		Cart.Path = MoveTemp(Path);
		Cart.PoolId = Pool.Id;
		Cart.PlantId = Plant.Id;
		Cart.WorldPos = Min::CellCenter(Cart.Path[0]);
		int32 PlanesOf[2];
		if (Min::PlanesOfCell(Cart.Path[0], PlanesOf) > 0) Cart.Plane = PlanesOf[0];
		Carts.Add(MoveTemp(Cart));
	}

	// surplus loads head for the depot and pay out bonus segments
	for (FMinPool& Pool : Pools)
	{
		if (!Pool.bAlive || !Pool.bSurplusReady || Pool.CartsFree <= 0) continue;
		TArray<FIntVector> Path;
		if (!FindNearest(Pool.Cell, [&](const FIntVector& C)
			{ return World->Cell(C).bDepot; }, Path)) continue;

		Pool.bSurplusReady = false;
		--Pool.CartsFree;
		FMinCart Cart;
		Cart.Id = NextCartId++;
		Cart.Res = Pool.Res;
		Cart.Path = MoveTemp(Path);
		Cart.bToDepot = true;
		Cart.PoolId = Pool.Id;
		Cart.WorldPos = Min::CellCenter(Cart.Path[0]);
		int32 PlanesOf[2];
		if (Min::PlanesOfCell(Cart.Path[0], PlanesOf) > 0) Cart.Plane = PlanesOf[0];
		Carts.Add(MoveTemp(Cart));
	}
}

void UMinSim::KillCart(FMinCart& Cart)
{
	if (!Cart.bAlive) return;
	Cart.bAlive = false;
	if (Pools.IsValidIndex(Cart.PoolId) && Pools[Cart.PoolId].bAlive)
		Pools[Cart.PoolId].CartsFree = FMath::Min(MaxPoolCarts, Pools[Cart.PoolId].CartsFree + 1);
	if (!Cart.bReturning && !Cart.bToDepot && Plants.IsValidIndex(Cart.PlantId))
		Plants[Cart.PlantId].Inbound = FMath::Max(0, Plants[Cart.PlantId].Inbound - 1);
}

void UMinSim::TickCarts(float Dt)
{
	for (FMinCart& Cart : Carts)
	{
		if (!Cart.bAlive) continue;
		if (Cart.Path.Num() < 2) { KillCart(Cart); continue; }

		Cart.T += Dt * CartSpeed;
		while (Cart.T >= 1.f && Cart.bAlive)
		{
			Cart.T -= 1.f;
			++Cart.Leg;
			if (Cart.Leg >= Cart.Path.Num() - 1)
			{
				// arrived
				if (!Cart.bReturning)
				{
					if (Cart.bToDepot)
					{
						Score += 1;
						SegmentsLeft += 2;
						if (Audio) Audio->Play(EMinSfx::Coin, 0.5f);
					}
					else if (Plants.IsValidIndex(Cart.PlantId) && Plants[Cart.PlantId].bAlive)
					{
						FMinPlant& Plant = Plants[Cart.PlantId];
						Plant.Pins = FMath::Max(0, Plant.Pins - 1);
						Plant.Inbound = FMath::Max(0, Plant.Inbound - 1);
						Score += 1;
						bStructuresDirty = true;
						if (Audio) Audio->Play(EMinSfx::Delivery, 0.45f);
					}
					// turn around and head home
					Cart.bReturning = true;
					Algo::Reverse(Cart.Path);
					Cart.Leg = 0;
					Cart.T = 0.f;
					break;
				}
				// home again
				Cart.bAlive = false;
				if (Pools.IsValidIndex(Cart.PoolId) && Pools[Cart.PoolId].bAlive)
					Pools[Cart.PoolId].CartsFree =
						FMath::Min(MaxPoolCarts, Pools[Cart.PoolId].CartsFree + 1);
				break;
			}
		}
		if (!Cart.bAlive) continue;

		// validate the segment we are on; the player may have erased it
		const FIntVector A = Cart.Path[Cart.Leg];
		const FIntVector B = Cart.Path[Cart.Leg + 1];
		if (!IsWalkable(A) || !IsWalkable(B) || !Min::CellsLinked(A, B))
		{
			// try to re-route from the segment start
			const FIntVector Goal = Cart.Path.Last();
			TArray<FIntVector> NewPath;
			if (IsWalkable(A) && FindNearest(A, [&](const FIntVector& C) { return C == Goal; }, NewPath))
			{
				Cart.Path = MoveTemp(NewPath);
				Cart.Leg = 0;
				Cart.T = 0.f;
			}
			else
			{
				KillCart(Cart);
				continue;
			}
		}

		const FIntVector& SA = Cart.Path[Cart.Leg];
		const FIntVector& SB = Cart.Path[Cart.Leg + 1];
		Cart.WorldPos = FMath::Lerp(Min::CellCenter(SA), Min::CellCenter(SB), Cart.T);

		// pick the rendering plane from the direction of travel; vertical runs
		// keep the previous plane while it still contains the cells
		if (SB.X != SA.X) Cart.Plane = Min::FrontIndexOfY(SA.Y);
		else if (SB.Y != SA.Y) Cart.Plane = Min::NumFront + Min::SideIndexOfX(SA.X);
		else
		{
			FIntPoint Tmp;
			if (!Min::CellToPlaneUV(SA, Cart.Plane, Tmp) || !Min::CellToPlaneUV(SB, Cart.Plane, Tmp))
			{
				int32 PlanesOf[2];
				if (Min::PlanesOfCell(SA, PlanesOf) > 0) Cart.Plane = PlanesOf[0];
			}
		}
	}

	if (Carts.Num() > 64)
		Carts.RemoveAll([](const FMinCart& C) { return !C.bAlive; });
}

// ---------------------------------------------------------------------------
// Demand, overflow, fail
// ---------------------------------------------------------------------------
void UMinSim::TickPlants(float Dt)
{
	CurrentWarning.Reset();
	for (FMinPlant& Plant : Plants)
	{
		if (!Plant.bAlive) continue;
		Plant.Age += Dt;
		if (Plant.Age < PlantGrace) continue;

		// demand pins arrive faster as the run goes on; jitter per plant
		const float Jitter = 0.85f + UMinWorld::Hash01(Plant.Id * 7, 3, World->WorldSeed) * 0.3f;
		const float Every = FMath::Lerp(13.f, 5.f, Difficulty01()) * Jitter;
		Plant.DemandT += Dt;
		if (Plant.DemandT >= Every)
		{
			Plant.DemandT = 0.f;
			if (Plant.Pins < 12) { ++Plant.Pins; bStructuresDirty = true; }
		}

		const bool bWasOverflow = Plant.bOverflow;
		Plant.bOverflow = Plant.Pins >= PinCap;
		if (Plant.bOverflow)
		{
			Plant.OverflowT -= Dt;
			if (!bWasOverflow)
			{
				OnEvent.Broadcast(FString::Printf(TEXT("%s plant on %s is overloaded!"),
					MinResInfo(Plant.Res).Name, *Min::PlaneName(PlaneOf(Plant.Cell))));
				if (Audio) Audio->Play(EMinSfx::Warn);
			}
			if (Plant.OverflowT <= 0.f)
			{
				TriggerGameOver(Plant);
				return;
			}
		}
		else
		{
			Plant.OverflowT = FMath::Min(OverflowMax, Plant.OverflowT + Dt * 0.5f);
		}

		if (Plant.bOverflow && CurrentWarning.IsEmpty())
		{
			CurrentWarning = FString::Printf(TEXT("%s plant on %s fails in %.0fs!"),
				MinResInfo(Plant.Res).Name, *Min::PlaneName(PlaneOf(Plant.Cell)), Plant.OverflowT);
		}
	}
}

void UMinSim::TickPools(float Dt)
{
	for (FMinPool& Pool : Pools)
	{
		if (!Pool.bAlive || Pool.bSurplusReady) continue;
		Pool.SurplusT += Dt;
		if (Pool.SurplusT >= SurplusEvery)
		{
			Pool.SurplusT = 0.f;
			Pool.bSurplusReady = true;
		}
	}
}

void UMinSim::TriggerGameOver(const FMinPlant& Plant)
{
	bGameOver = true;
	GameOverReason = FString::Printf(TEXT("The %s plant on %s went unsupplied."),
		MinResInfo(Plant.Res).Name, *Min::PlaneName(PlaneOf(Plant.Cell)));
	bNewRecord = Score > HighScore;
	if (bNewRecord) HighScore = Score;
	SaveHighScore();
	CurrentWarning.Reset();
	OnEvent.Broadcast(TEXT("The mine has failed its quota."));
	if (Audio) Audio->Play(EMinSfx::Error);
	UE_LOG(LogMin, Log, TEXT("GAME OVER score=%d high=%d t=%.0f"), Score, HighScore, PlayTime);
}

// ---------------------------------------------------------------------------
void UMinSim::Tick(float Dt)
{
	if (bPaused || !bRunStarted || bGameOver) return;
	Dt *= SimSpeed;
	PlayTime += Dt;

	TickSpawner(Dt);
	TickPlants(Dt);
	if (bGameOver) return;
	TickPools(Dt);

	DispatchT -= Dt;
	if (DispatchT <= 0.f)
	{
		DispatchT = 0.35f;
		Dispatch();
	}
	TickCarts(Dt);
}

// ---------------------------------------------------------------------------
// High scores
// ---------------------------------------------------------------------------
void UMinSim::LoadHighScore()
{
	HighScore = 0;
	FString In;
	if (!FFileHelper::LoadFileToString(In, *ScorePath())) return;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(In), Root) || !Root.IsValid()) return;
	double V = 0;
	if (Root->TryGetNumberField(RulesKey(Rules), V)) HighScore = (int32)V;
}

void UMinSim::SaveHighScore()
{
	TSharedPtr<FJsonObject> Root;
	FString In;
	if (FFileHelper::LoadFileToString(In, *ScorePath()))
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(In), Root);
	if (!Root.IsValid()) Root = MakeShared<FJsonObject>();
	Root->SetNumberField(RulesKey(Rules), HighScore);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	FFileHelper::SaveStringToFile(Out, *ScorePath());
}
