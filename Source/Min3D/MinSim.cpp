#include "MinSim.h"
#include "Min3D.h"
#include "MinWorld.h"
#include "MinAssets.h"
#include "MinAudio.h"
#include "MinGameMode.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	int32 ResValue(EMinResource R)
	{
		switch (R)
		{
		case EMinResource::Stone: return 1;
		case EMinResource::Coal: return 2;
		case EMinResource::Iron: return 3;
		case EMinResource::Copper: return 4;
		case EMinResource::Gold: return 8;
		case EMinResource::Crystal: return 10;
		case EMinResource::Water: return 2;
		case EMinResource::IronIngot: return 9;
		case EMinResource::CopperIngot: return 12;
		case EMinResource::GoldBar: return 24;
		default: return 0;
		}
	}

	EMinResource ProcessedForm(EMinResource R)
	{
		switch (R)
		{
		case EMinResource::Iron: return EMinResource::IronIngot;
		case EMinResource::Copper: return EMinResource::CopperIngot;
		case EMinResource::Gold: return EMinResource::GoldBar;
		default: return EMinResource::None;
		}
	}

	constexpr int32 CartLoad = 4;
	constexpr float MinerRate = 0.55f;
	constexpr float PumpRate = 0.80f;
	constexpr float SmeltSeconds = 2.5f;
	constexpr float UnpoweredMult = 0.35f;
	constexpr int32 PowerRadius = 16; // manhattan cells, planes cost 8 each
}

int32 UMinSim::StructureCost(EMinStructure Type)
{
	switch (Type)
	{
	case EMinStructure::Miner: return 40;
	case EMinStructure::Pump: return 50;
	case EMinStructure::Smelter: return 80;
	case EMinStructure::Depot: return 30;
	case EMinStructure::Generator: return 60;
	case EMinStructure::Lift: return 0; // priced per plane in CheckLift
	default: return 0;
	}
}

const TCHAR* UMinSim::StructureName(EMinStructure Type)
{
	switch (Type)
	{
	case EMinStructure::Miner: return TEXT("Miner");
	case EMinStructure::Pump: return TEXT("Pump");
	case EMinStructure::Smelter: return TEXT("Smelter");
	case EMinStructure::Depot: return TEXT("Depot");
	case EMinStructure::Generator: return TEXT("Generator");
	case EMinStructure::Lift: return TEXT("Lift");
	default: return TEXT("?");
	}
}

const TCHAR* UMinSim::StructureSprite(EMinStructure Type)
{
	switch (Type)
	{
	case EMinStructure::Miner: return TEXT("net_05_02");
	case EMinStructure::Pump: return TEXT("net_08_04");
	case EMinStructure::Smelter: return TEXT("mac_00_00");
	case EMinStructure::Depot: return TEXT("net_04_03");
	case EMinStructure::Generator: return TEXT("mac_07_00");
	case EMinStructure::Lift: return TEXT("net_02_05");
	default: return TEXT("net_06_00");
	}
}

void UMinSim::Init(UMinWorld* InWorld, UMinAssets* InAssets, AMinGameMode* InGM)
{
	World = InWorld;
	Assets = InAssets;
	GM = InGM;
	SetupContracts();
}

void UMinSim::SetupContracts()
{
	Contracts.Reset();
	FMinContract C;
	C.Title = TEXT("First Ore");
	C.Resource = EMinResource::Iron; C.Needed = 20; C.RewardCredits = 140;
	C.Hint = TEXT("Dig a tunnel [1] from an iron deposit, place a Miner [3] on it and a Depot [6] in a cavern, then connect them.");
	Contracts.Add(C);
	C.Title = TEXT("Smelting Works");
	C.Resource = EMinResource::IronIngot; C.Needed = 12; C.RewardCredits = 220;
	C.Hint = TEXT("Build a Smelter [5]. Ore routed into it becomes ingots bound for your depot. Generators [7] burning coal speed everything up.");
	Contracts.Add(C);
	C.Title = TEXT("Copper Run");
	C.Resource = EMinResource::CopperIngot; C.Needed = 15; C.RewardCredits = 320;
	C.Hint = TEXT("Copper hides deeper and on other planes. Switch planes with Q/E and connect them with a Lift [8].");
	Contracts.Add(C);
	C.Title = TEXT("Waterworks");
	C.Resource = EMinResource::Water; C.Needed = 25; C.RewardCredits = 260;
	C.Hint = TEXT("Aquifers block tunnels - but Pipes [2] may cross them. Place a Pump [4] on a water spring.");
	Contracts.Add(C);
	C.Title = TEXT("Gold Standard");
	C.Resource = EMinResource::GoldBar; C.Needed = 8; C.RewardCredits = 500;
	C.Hint = TEXT("Gold veins wander the deep obsidian. Watch out for granite - it cannot be dug.");
	Contracts.Add(C);
	C.Title = TEXT("Network Mastery");
	C.Resource = EMinResource::None; C.Needed = 150; C.RewardCredits = 1000;
	C.Hint = TEXT("Deliver 150 of anything. Congested routes slow carts - parallel paths keep the network flowing.");
	Contracts.Add(C);
	ActiveContract = 0;
}

// ---------------------------------------------------------------------------
// Graph helpers
// ---------------------------------------------------------------------------
int32 UMinSim::EnsureNodeAt(const FIntVector& Cell, EMinRoute SplitType)
{
	if (const int32* Found = NodeAtCell.Find(Cell)) return *Found;
	if (const int32* EdgeId = EdgeAtCell.Find(Cell)) return SplitEdgeAt(*EdgeId, Cell);
	FMinNode N;
	N.Id = Nodes.Num();
	N.Cell = Cell;
	Nodes.Add(N);
	NodeAtCell.Add(Cell, N.Id);
	return N.Id;
}

int32 UMinSim::SplitEdgeAt(int32 EdgeId, const FIntVector& Cell)
{
	FMinEdge& E = Edges[EdgeId];
	const int32 Idx = E.Path.IndexOfByKey(Cell);
	check(Idx > 0 && Idx < E.Path.Num() - 1);

	// new junction node
	FMinNode N;
	N.Id = Nodes.Num();
	N.Cell = Cell;
	Nodes.Add(N);
	NodeAtCell.Add(Cell, N.Id);
	EdgeAtCell.Remove(Cell);

	// second half becomes a new edge
	FMinEdge E2;
	E2.Id = Edges.Num();
	E2.Type = E.Type;
	E2.A = N.Id;
	E2.B = E.B;
	E2.Capacity = E.Capacity;
	for (int32 I = Idx; I < E.Path.Num(); ++I) E2.Path.Add(E.Path[I]);
	E2.Length = E2.Path.Num() - 1;

	// first half keeps the id
	const int32 OldB = E.B;
	E.Path.SetNum(Idx + 1);
	E.Length = E.Path.Num() - 1;
	E.B = N.Id;

	// rewire node adjacency
	Nodes[OldB].Edges.Remove(EdgeId);
	Nodes[OldB].Edges.Add(E2.Id);
	Nodes[N.Id].Edges.Add(EdgeId);
	Nodes[N.Id].Edges.Add(E2.Id);

	// re-tag interior cells of the new half
	for (int32 I = 1; I < E2.Path.Num() - 1; ++I) EdgeAtCell.Add(E2.Path[I], E2.Id);
	Edges.Add(MoveTemp(E2));
	const int32 E2Id = Edges.Num() - 1;

	// carts whose plan references the split edge: remap current leg, then re-path
	for (FMinCart& Cart : Carts)
	{
		if (!Cart.bAlive) continue;
		bool bAffected = Cart.EdgePath.Contains(EdgeId);
		if (!bAffected) continue;
		// rebuild plan from the cart's next node
		const int32 CurEdge = Cart.EdgePath.IsValidIndex(Cart.Leg) ? Cart.EdgePath[Cart.Leg] : -1;
		if (CurEdge == EdgeId)
		{
			// figure out which half the cart is on
			const FMinEdge& H1 = Edges[EdgeId];
			const float SplitFrac = float(H1.Length) / FMath::Max(1.f, float(H1.Length + Edges[E2Id].Length));
			const bool bForward = Cart.NodePath[Cart.Leg] == H1.A || Cart.NodePath[Cart.Leg] == Edges[E2Id].B;
			float TAlong = Cart.T;
			if (Cart.NodePath[Cart.Leg + 1] == OldB || Cart.NodePath[Cart.Leg] == H1.A) {}
			// normalize to A->B direction of the original edge
			const bool bFromA = Cart.NodePath[Cart.Leg] == H1.A;
			if (!bFromA) TAlong = 1.f - TAlong;
			int32 NewEdge; float NewT; int32 FromNode; int32 ToNode;
			if (TAlong < SplitFrac)
			{
				NewEdge = EdgeId; NewT = TAlong / FMath::Max(0.01f, SplitFrac);
				FromNode = H1.A; ToNode = N.Id;
			}
			else
			{
				NewEdge = E2Id; NewT = (TAlong - SplitFrac) / FMath::Max(0.01f, 1.f - SplitFrac);
				FromNode = N.Id; ToNode = Edges[E2Id].B;
			}
			if (!bFromA) { NewT = 1.f - NewT; Swap(FromNode, ToNode); }
			// re-path from ToNode to destination
			TArray<int32> RestEdges, RestNodes;
			const FMinStructureInst* Dest = Structures.IsValidIndex(Cart.DestStructure) ? &Structures[Cart.DestStructure] : nullptr;
			if (Dest && Dest->bAlive && FindPath(ToNode, Dest->NodeId, Cart.bFluid, RestEdges, RestNodes))
			{
				Cart.EdgePath = { NewEdge };
				Cart.NodePath = { FromNode, ToNode };
				Cart.EdgePath.Append(RestEdges);
				for (int32 I = 1; I < RestNodes.Num(); ++I) Cart.NodePath.Add(RestNodes[I]);
				Cart.Leg = 0;
				Cart.T = NewT;
			}
			else Cart.bAlive = false; // stranded
		}
		else
		{
			// not on it yet - re-path from the next node boundary
			const int32 FromNode = Cart.NodePath.IsValidIndex(Cart.Leg + 1) ? Cart.NodePath[Cart.Leg + 1] : -1;
			const FMinStructureInst* Dest = Structures.IsValidIndex(Cart.DestStructure) ? &Structures[Cart.DestStructure] : nullptr;
			TArray<int32> RestEdges, RestNodes;
			if (FromNode >= 0 && Dest && Dest->bAlive && FindPath(FromNode, Dest->NodeId, Cart.bFluid, RestEdges, RestNodes))
			{
				const int32 KeepEdge = Cart.EdgePath[Cart.Leg];
				Cart.EdgePath = { KeepEdge };
				Cart.NodePath = { Cart.NodePath[Cart.Leg], FromNode };
				Cart.EdgePath.Append(RestEdges);
				for (int32 I = 1; I < RestNodes.Num(); ++I) Cart.NodePath.Add(RestNodes[I]);
				Cart.Leg = 0;
			}
			else Cart.bAlive = false;
		}
	}
	return N.Id;
}

void UMinSim::RemoveEdge(int32 EdgeId)
{
	if (!Edges.IsValidIndex(EdgeId) || !Edges[EdgeId].bAlive) return;
	FMinEdge& E = Edges[EdgeId];
	E.bAlive = false;
	for (int32 I = 1; I < E.Path.Num() - 1; ++I) EdgeAtCell.Remove(E.Path[I]);
	if (Nodes.IsValidIndex(E.A)) Nodes[E.A].Edges.Remove(EdgeId);
	if (Nodes.IsValidIndex(E.B)) Nodes[E.B].Edges.Remove(EdgeId);
	for (FMinCart& Cart : Carts)
		if (Cart.bAlive && Cart.EdgePath.Contains(EdgeId)) Cart.bAlive = false;
	// orphan endpoint nodes without structures vanish
	for (int32 NodeId : { E.A, E.B })
	{
		if (Nodes.IsValidIndex(NodeId) && Nodes[NodeId].Edges.Num() == 0 && Nodes[NodeId].StructureId < 0)
			NodeAtCell.Remove(Nodes[NodeId].Cell);
	}
	bTopologyDirty = true;
}

void UMinSim::RemoveStructure(int32 StructId)
{
	if (!Structures.IsValidIndex(StructId) || !Structures[StructId].bAlive) return;
	FMinStructureInst& S = Structures[StructId];
	S.bAlive = false;
	auto ClearCellAndNode = [&](const FIntVector& Cell, int32 NodeId)
	{
		FMinCell& C = World->Cell(Cell.X, Cell.Y, Cell.Z);
		C.StructureId = -1;
		if (Nodes.IsValidIndex(NodeId))
		{
			Nodes[NodeId].StructureId = -1;
			if (Nodes[NodeId].Edges.Num() == 0) NodeAtCell.Remove(Nodes[NodeId].Cell);
		}
	};
	ClearCellAndNode(S.Cell, S.NodeId);
	if (S.Type == EMinStructure::Lift && S.TargetPlane >= 0)
	{
		ClearCellAndNode(FIntVector(S.Cell.X, S.Cell.Y, S.TargetPlane), S.NodeIdB);
		// the shaft edge dies too
		if (Nodes.IsValidIndex(S.NodeId))
			for (int32 EId : TArray<int32>(Nodes[S.NodeId].Edges))
				if (Edges[EId].Type == EMinRoute::LiftShaft) RemoveEdge(EId);
		// clear intermediate shaft markers
		const int32 P0 = FMath::Min(S.Cell.Z, S.TargetPlane), P1 = FMath::Max(S.Cell.Z, S.TargetPlane);
		for (int32 P = P0 + 1; P < P1; ++P) EdgeAtCell.Remove(FIntVector(S.Cell.X, S.Cell.Y, P));
	}
	Credits += StructureCost(S.Type) / 2;
	bStructuresDirty = true;
	bTopologyDirty = true;
}

// ---------------------------------------------------------------------------
// Build checks
// ---------------------------------------------------------------------------
FMinBuildCheck UMinSim::CheckRoute(const TArray<FIntPoint>& Cells, int32 Plane, EMinRoute Type) const
{
	FMinBuildCheck R;
	if (Cells.Num() < 2) { R.Reason = TEXT("Drag to draw a route"); return R; }
	int32 Cost = 0;
	int32 SharedRun = 0;
	for (int32 I = 0; I < Cells.Num(); ++I)
	{
		const FIntPoint& C = Cells[I];
		if (!Min::InGrid(C.X, C.Y, Plane)) { R.Reason = TEXT("Out of bounds"); return R; }
		if (I > 0)
		{
			const FIntPoint D = C - Cells[I - 1];
			if (FMath::Abs(D.X) + FMath::Abs(D.Y) != 1) { R.Reason = TEXT("Route must be continuous"); return R; }
		}
		const FMinCell& Cell = World->Cell(C.X, C.Y, Plane);
		if (Cell.Terrain == EMinTerrain::Sky) { R.Reason = TEXT("Cannot build in the sky"); return R; }
		if (Cell.Terrain == EMinTerrain::Granite && !Cell.bExcavated) { R.Reason = TEXT("Granite is unbreakable"); return R; }
		if (Cell.Terrain == EMinTerrain::Aquifer && !Cell.bExcavated && Type == EMinRoute::Tunnel)
		{
			R.Reason = TEXT("Tunnels flood in aquifers - use a Pipe");
			return R;
		}
		const bool bInterior = I > 0 && I < Cells.Num() - 1;
		const FIntVector C3(C.X, C.Y, Plane);
		if (bInterior && Cell.StructureId >= 0) { R.Reason = TEXT("Blocked by a structure"); return R; }
		const bool bShared = EdgeAtCell.Contains(C3) || NodeAtCell.Contains(C3);
		SharedRun = bShared ? SharedRun + 1 : 0;
		if (SharedRun > 1) { R.Reason = TEXT("Cannot run along an existing route"); return R; }

		const bool bNeedsDig = World->IsSolid(C.X, C.Y, Plane) &&
			!(Type == EMinRoute::Pipe && Cell.Terrain == EMinTerrain::Aquifer);
		Cost += (Type == EMinRoute::Pipe ? 3 : 2) + (bNeedsDig ? 2 : 0);
	}
	R.bValid = true;
	R.Cost = Cost;
	if (Cost > Credits) { R.bValid = false; R.Reason = FString::Printf(TEXT("Needs %d credits"), Cost); }
	return R;
}

bool UMinSim::BuildRoute(const TArray<FIntPoint>& Cells, int32 Plane, EMinRoute Type)
{
	const FMinBuildCheck Check = CheckRoute(Cells, Plane, Type);
	if (!Check.bValid) return false;
	Credits -= Check.Cost;

	// excavate the corridor (pipes leave aquifer water in place)
	for (const FIntPoint& C : Cells)
	{
		const FMinCell& Cell = World->Cell(C.X, C.Y, Plane);
		if (Type == EMinRoute::Pipe && Cell.Terrain == EMinTerrain::Aquifer) continue;
		if (World->IsSolid(C.X, C.Y, Plane)) World->Excavate(C.X, C.Y, Plane);
	}

	// split the drawn polyline at every cell that is (or becomes) a junction
	TArray<int32> Waypoints; // indices into Cells that are nodes
	Waypoints.Add(0);
	for (int32 I = 1; I < Cells.Num() - 1; ++I)
	{
		const FIntVector C3(Cells[I].X, Cells[I].Y, Plane);
		if (NodeAtCell.Contains(C3) || EdgeAtCell.Contains(C3)) Waypoints.Add(I);
	}
	Waypoints.Add(Cells.Num() - 1);

	for (int32 W = 0; W + 1 < Waypoints.Num(); ++W)
	{
		const int32 I0 = Waypoints[W], I1 = Waypoints[W + 1];
		if (I1 - I0 < 1) continue;
		const int32 NodeA = EnsureNodeAt(FIntVector(Cells[I0].X, Cells[I0].Y, Plane), Type);
		const int32 NodeB = EnsureNodeAt(FIntVector(Cells[I1].X, Cells[I1].Y, Plane), Type);
		if (NodeA == NodeB) continue;
		FMinEdge E;
		E.Id = Edges.Num();
		E.Type = Type;
		E.A = NodeA; E.B = NodeB;
		for (int32 I = I0; I <= I1; ++I) E.Path.Add(FIntVector(Cells[I].X, Cells[I].Y, Plane));
		E.Length = E.Path.Num() - 1;
		E.Capacity = Type == EMinRoute::Pipe ? 5.f : 3.f;
		Nodes[NodeA].Edges.Add(E.Id);
		Nodes[NodeB].Edges.Add(E.Id);
		for (int32 I = 1; I < E.Path.Num() - 1; ++I) EdgeAtCell.Add(E.Path[I], E.Id);
		Edges.Add(MoveTemp(E));
	}
	bTopologyDirty = true;
	return true;
}

FMinBuildCheck UMinSim::CheckStructure(EMinStructure Type, const FIntPoint& Cell, int32 Plane) const
{
	FMinBuildCheck R;
	R.Cost = StructureCost(Type);
	if (!Min::InGrid(Cell.X, Cell.Y, Plane)) { R.Reason = TEXT("Out of bounds"); return R; }
	const FMinCell& C = World->Cell(Cell.X, Cell.Y, Plane);
	if (C.StructureId >= 0) { R.Reason = TEXT("Occupied"); return R; }
	const FIntVector C3(Cell.X, Cell.Y, Plane);
	if (EdgeAtCell.Contains(C3)) { R.Reason = TEXT("A route passes through here"); return R; }
	if (C.Terrain == EMinTerrain::Sky) { R.Reason = TEXT("Underground only"); return R; }

	switch (Type)
	{
	case EMinStructure::Miner:
		if (C.DepositId < 0 || C.Resource == EMinResource::Water || C.Resource == EMinResource::None)
		{
			R.Reason = TEXT("Place on an ore deposit"); return R;
		}
		if (World->Deposits[C.DepositId].Remaining <= 0) { R.Reason = TEXT("Deposit exhausted"); return R; }
		break;
	case EMinStructure::Pump:
		if (C.DepositId < 0 || C.Resource != EMinResource::Water) { R.Reason = TEXT("Place on a water spring"); return R; }
		break;
	case EMinStructure::Lift:
		R.Reason = TEXT("Use CheckLift");
		return R;
	default:
		if (World->IsSolid(Cell.X, Cell.Y, Plane))
		{
			if (!World->IsDiggable(Cell.X, Cell.Y, Plane)) { R.Reason = TEXT("Granite is unbreakable"); return R; }
			R.Cost += 4; // includes carving the chamber
		}
		break;
	}
	if (R.Cost > Credits) { R.Reason = FString::Printf(TEXT("Needs %d credits"), R.Cost); return R; }
	R.bValid = true;
	return R;
}

bool UMinSim::BuildStructure(EMinStructure Type, const FIntPoint& Cell, int32 Plane)
{
	const FMinBuildCheck Check = CheckStructure(Type, Cell, Plane);
	if (!Check.bValid) return false;
	Credits -= Check.Cost;

	if (World->IsSolid(Cell.X, Cell.Y, Plane)) World->Excavate(Cell.X, Cell.Y, Plane);

	FMinStructureInst S;
	S.Id = Structures.Num();
	S.Type = Type;
	S.Cell = FIntVector(Cell.X, Cell.Y, Plane);
	S.NodeId = EnsureNodeAt(S.Cell, EMinRoute::Tunnel);
	Nodes[S.NodeId].StructureId = S.Id;
	if (Type == EMinStructure::Generator) S.FuelSeconds = 180.f;
	FMinCell& C = World->Cell(Cell.X, Cell.Y, Plane);
	C.StructureId = S.Id;
	if (Type == EMinStructure::Miner || Type == EMinStructure::Pump) S.DepositId = C.DepositId;
	Structures.Add(MoveTemp(S));
	bStructuresDirty = true;
	bTopologyDirty = true;
	return true;
}

FMinBuildCheck UMinSim::CheckLift(const FIntPoint& Cell, int32 PlaneA, int32 PlaneB) const
{
	FMinBuildCheck R;
	if (PlaneA == PlaneB) { R.Reason = TEXT("Pick a different plane (Q/E) before confirming"); return R; }
	R.Cost = 30 * FMath::Abs(PlaneA - PlaneB);
	for (int32 P : { PlaneA, PlaneB })
	{
		if (!Min::InGrid(Cell.X, Cell.Y, P)) { R.Reason = TEXT("Out of bounds"); return R; }
		const FMinCell& C = World->Cell(Cell.X, Cell.Y, P);
		if (C.Terrain == EMinTerrain::Sky) { R.Reason = TEXT("Underground only"); return R; }
		if (C.StructureId >= 0) { R.Reason = TEXT("Occupied"); return R; }
		if (World->IsSolid(Cell.X, Cell.Y, P) && !World->IsDiggable(Cell.X, Cell.Y, P))
		{
			R.Reason = TEXT("Granite is unbreakable"); return R;
		}
	}
	const int32 P0 = FMath::Min(PlaneA, PlaneB), P1 = FMath::Max(PlaneA, PlaneB);
	for (int32 P = P0 + 1; P < P1; ++P)
	{
		const FIntVector C3(Cell.X, Cell.Y, P);
		if (NodeAtCell.Contains(C3) || EdgeAtCell.Contains(C3) || World->Cell(Cell.X, Cell.Y, P).StructureId >= 0)
		{
			R.Reason = FString::Printf(TEXT("Shaft blocked on plane %s"), *Min::PlaneName(P)); return R;
		}
		if (World->Cell(Cell.X, Cell.Y, P).Terrain == EMinTerrain::Granite)
		{
			R.Reason = FString::Printf(TEXT("Granite blocks the shaft on %s"), *Min::PlaneName(P)); return R;
		}
	}
	if (R.Cost > Credits) { R.Reason = FString::Printf(TEXT("Needs %d credits"), R.Cost); return R; }
	R.bValid = true;
	return R;
}

bool UMinSim::BuildLift(const FIntPoint& Cell, int32 PlaneA, int32 PlaneB)
{
	const FMinBuildCheck Check = CheckLift(Cell, PlaneA, PlaneB);
	if (!Check.bValid) return false;
	Credits -= Check.Cost;

	for (int32 P : { PlaneA, PlaneB })
		if (World->IsSolid(Cell.X, Cell.Y, P)) World->Excavate(Cell.X, Cell.Y, P);

	FMinStructureInst S;
	S.Id = Structures.Num();
	S.Type = EMinStructure::Lift;
	S.Cell = FIntVector(Cell.X, Cell.Y, PlaneA);
	S.TargetPlane = PlaneB;
	S.NodeId = EnsureNodeAt(S.Cell, EMinRoute::Tunnel);
	S.NodeIdB = EnsureNodeAt(FIntVector(Cell.X, Cell.Y, PlaneB), EMinRoute::Tunnel);
	Nodes[S.NodeId].StructureId = S.Id;
	Nodes[S.NodeIdB].StructureId = S.Id;
	World->Cell(Cell.X, Cell.Y, PlaneA).StructureId = S.Id;
	World->Cell(Cell.X, Cell.Y, PlaneB).StructureId = S.Id;

	FMinEdge E;
	E.Id = Edges.Num();
	E.Type = EMinRoute::LiftShaft;
	E.A = S.NodeId; E.B = S.NodeIdB;
	E.Path = { S.Cell, FIntVector(Cell.X, Cell.Y, PlaneB) };
	E.Length = 5.f * FMath::Abs(PlaneA - PlaneB);
	E.Capacity = 6.f;
	Nodes[E.A].Edges.Add(E.Id);
	Nodes[E.B].Edges.Add(E.Id);
	Edges.Add(MoveTemp(E));

	const int32 P0 = FMath::Min(PlaneA, PlaneB), P1 = FMath::Max(PlaneA, PlaneB);
	for (int32 P = P0 + 1; P < P1; ++P)
		EdgeAtCell.Add(FIntVector(Cell.X, Cell.Y, P), Edges.Num() - 1);

	Structures.Add(MoveTemp(S));
	bStructuresDirty = true;
	bTopologyDirty = true;
	return true;
}

bool UMinSim::RemoveAt(const FIntPoint& Cell, int32 Plane)
{
	const FIntVector C3(Cell.X, Cell.Y, Plane);
	const FMinCell& C = World->Cell(Cell.X, Cell.Y, Plane);
	if (C.StructureId >= 0) { RemoveStructure(C.StructureId); return true; }
	if (const int32* EdgeId = EdgeAtCell.Find(C3))
	{
		Credits += FMath::RoundToInt(Edges[*EdgeId].Length * 0.6f);
		RemoveEdge(*EdgeId);
		return true;
	}
	if (const int32* NodeId = NodeAtCell.Find(C3))
	{
		// removing a junction removes all its edges
		for (int32 EId : TArray<int32>(Nodes[*NodeId].Edges)) RemoveEdge(EId);
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Pathfinding (A* over nodes)
// ---------------------------------------------------------------------------
float UMinSim::EdgeSpeedMult(const FMinEdge& E) const
{
	const float Load = E.Occupancy / FMath::Max(1.f, E.Capacity);
	if (Load <= 0.7f) return 1.f;
	return FMath::Lerp(1.f, 0.25f, FMath::Clamp((Load - 0.7f) / 0.7f, 0.f, 1.f));
}

bool UMinSim::FindPath(int32 StartNode, int32 EndNode, bool bFluid,
	TArray<int32>& OutEdges, TArray<int32>& OutNodes, float* OutCost) const
{
	if (StartNode == EndNode) { OutNodes = { StartNode }; OutEdges.Reset(); if (OutCost) *OutCost = 0; return true; }
	if (!Nodes.IsValidIndex(StartNode) || !Nodes.IsValidIndex(EndNode)) return false;

	auto Heuristic = [&](int32 N)
	{
		const FIntVector D = Nodes[N].Cell - Nodes[EndNode].Cell;
		return FMath::Abs(D.X) + FMath::Abs(D.Y) + FMath::Abs(D.Z) * 5.f;
	};

	TMap<int32, float> G;
	TMap<int32, int32> CameNode, CameEdge;
	TArray<TPair<float, int32>> Open; // (f, node) - small graph, linear pop is fine
	G.Add(StartNode, 0.f);
	Open.Add({ Heuristic(StartNode), StartNode });

	while (Open.Num())
	{
		int32 Best = 0;
		for (int32 I = 1; I < Open.Num(); ++I) if (Open[I].Key < Open[Best].Key) Best = I;
		const int32 Cur = Open[Best].Value;
		Open.RemoveAtSwap(Best);
		if (Cur == EndNode) break;

		for (int32 EId : Nodes[Cur].Edges)
		{
			const FMinEdge& E = Edges[EId];
			if (!E.bAlive) continue;
			const bool bAllowed = E.Type == EMinRoute::LiftShaft ||
				(bFluid ? E.Type == EMinRoute::Pipe : E.Type == EMinRoute::Tunnel);
			if (!bAllowed) continue;
			const int32 Next = E.A == Cur ? E.B : E.A;
			const float Congestion = (E.Occupancy / FMath::Max(1.f, E.Capacity)) * 4.f;
			const float NewG = G[Cur] + E.Length / EdgeSpeedMult(E) + 2.f + Congestion;
			if (const float* OldG = G.Find(Next); OldG && *OldG <= NewG) continue;
			G.Add(Next, NewG);
			CameNode.Add(Next, Cur);
			CameEdge.Add(Next, EId);
			Open.Add({ NewG + Heuristic(Next), Next });
		}
	}
	if (!CameNode.Contains(EndNode)) return false;

	OutNodes.Reset(); OutEdges.Reset();
	int32 Walk = EndNode;
	while (Walk != StartNode)
	{
		OutNodes.Insert(Walk, 0);
		OutEdges.Insert(CameEdge[Walk], 0);
		Walk = CameNode[Walk];
	}
	OutNodes.Insert(StartNode, 0);
	if (OutCost) *OutCost = G[EndNode];
	return true;
}

bool UMinSim::StructureAcceptsCargo(const FMinStructureInst& S, EMinResource Cargo) const
{
	if (!S.bAlive) return false;
	switch (S.Type)
	{
	case EMinStructure::Depot: return true;
	case EMinStructure::Smelter:
		return ProcessedForm(Cargo) != EMinResource::None
			&& (S.InputRes == EMinResource::None || S.InputRes == Cargo)
			&& S.InputBuffer < 10;
	case EMinStructure::Generator:
		return Cargo == EMinResource::Coal && S.FuelSeconds < 240.f;
	default: return false;
	}
}

int32 UMinSim::PickDestination(const FMinStructureInst& Source, EMinResource Cargo,
	TArray<int32>& OutEdges, TArray<int32>& OutNodes)
{
	int32 BestId = -1;
	float BestCost = FLT_MAX;
	TArray<int32> TmpE, TmpN;
	const bool bFluid = MinResInfo(Cargo).bFluid;
	for (const FMinStructureInst& Dest : Structures)
	{
		if (Dest.Id == Source.Id || !StructureAcceptsCargo(Dest, Cargo)) continue;
		// smelters shouldn't hoard what depots want more: order by path cost only
		float Cost;
		if (FindPath(Source.NodeId, Dest.NodeId, bFluid, TmpE, TmpN, &Cost))
		{
			// prefer processors for raw ore so the chain builds itself
			if (Dest.Type == EMinStructure::Smelter) Cost *= 0.5f;
			if (Dest.Type == EMinStructure::Generator) Cost *= 0.6f;
			if (Cost < BestCost)
			{
				BestCost = Cost;
				BestId = Dest.Id;
				OutEdges = TmpE;
				OutNodes = TmpN;
			}
		}
	}
	return BestId;
}

void UMinSim::DispatchFrom(FMinStructureInst& S, EMinResource Cargo, int32 Amount)
{
	TArray<int32> PathEdges, PathNodes;
	const int32 DestId = PickDestination(S, Cargo, PathEdges, PathNodes);
	if (DestId < 0) return;

	FMinCart Cart;
	Cart.Id = NextCartId++;
	Cart.Cargo = Cargo;
	Cart.Amount = Amount;
	Cart.EdgePath = PathEdges;
	Cart.NodePath = PathNodes;
	Cart.DestStructure = DestId;
	Cart.bFluid = MinResInfo(Cargo).bFluid;
	Cart.Plane = S.Cell.Z;
	Cart.WorldPos = Min::CellToWorld(S.Cell.X, S.Cell.Y, S.Cell.Z, Min::DepthCart);
	Carts.Add(MoveTemp(Cart));
	S.Buffer -= Amount;
}

void UMinSim::DeliverCargo(FMinCart& Cart)
{
	Cart.bAlive = false;
	if (!Structures.IsValidIndex(Cart.DestStructure)) return;
	FMinStructureInst& Dest = Structures[Cart.DestStructure];
	if (!Dest.bAlive) return;

	switch (Dest.Type)
	{
	case EMinStructure::Depot:
	{
		Stock.FindOrAdd(Cart.Cargo) += Cart.Amount;
		Credits += ResValue(Cart.Cargo) * Cart.Amount;
		TotalDeliveries += Cart.Amount;
		if (Contracts.IsValidIndex(ActiveContract))
		{
			FMinContract& C = Contracts[ActiveContract];
			if (!C.bComplete && (C.Resource == EMinResource::None || C.Resource == Cart.Cargo))
				C.Delivered += Cart.Amount;
		}
		if (Audio) Audio->Play(EMinSfx::Delivery, 0.5f);
		break;
	}
	case EMinStructure::Smelter:
		Dest.InputRes = Cart.Cargo;
		Dest.InputBuffer += Cart.Amount;
		break;
	case EMinStructure::Generator:
		Dest.FuelSeconds += 40.f * Cart.Amount;
		break;
	default: break;
	}
}

// ---------------------------------------------------------------------------
// Ticking
// ---------------------------------------------------------------------------
void UMinSim::UpdatePower()
{
	for (FMinStructureInst& S : Structures)
	{
		if (!S.bAlive) continue;
		S.bPowered = false;
		for (const FMinStructureInst& Gen : Structures)
		{
			if (!Gen.bAlive || Gen.Type != EMinStructure::Generator || Gen.FuelSeconds <= 0.f) continue;
			const FIntVector D = S.Cell - Gen.Cell;
			if (FMath::Abs(D.X) + FMath::Abs(D.Y) + FMath::Abs(D.Z) * 8 <= PowerRadius)
			{
				S.bPowered = true;
				break;
			}
		}
	}
}

void UMinSim::TickStructures(float Dt)
{
	for (FMinStructureInst& S : Structures)
	{
		if (!S.bAlive) continue;
		const float Mult = S.bPowered ? 1.f : UnpoweredMult;
		S.RetryTimer -= Dt;

		switch (S.Type)
		{
		case EMinStructure::Miner:
		case EMinStructure::Pump:
		{
			if (S.DepositId < 0 || !World->Deposits.IsValidIndex(S.DepositId)) break;
			FMinDeposit& Dep = World->Deposits[S.DepositId];
			if (Dep.Remaining > 0 && S.Buffer < 10)
			{
				S.Progress += Dt * (S.Type == EMinStructure::Pump ? PumpRate : MinerRate) * Mult;
				if (S.Progress >= 1.f)
				{
					S.Progress -= 1.f;
					--Dep.Remaining;
					++S.Buffer;
					S.BufferRes = Dep.Resource;
					if (Dep.Remaining == 0)
					{
						OnEvent.Broadcast(FString::Printf(TEXT("%s deposit exhausted on %s"),
							MinResInfo(Dep.Resource).Name, *Min::PlaneName(S.Cell.Z)));
						bStructuresDirty = true;
					}
				}
			}
			if (S.Buffer >= CartLoad && S.RetryTimer <= 0.f)
			{
				S.RetryTimer = 1.0f;
				DispatchFrom(S, S.BufferRes, CartLoad);
			}
			break;
		}
		case EMinStructure::Smelter:
		{
			if (S.InputBuffer > 0)
			{
				S.Progress += Dt * Mult / SmeltSeconds;
				if (S.Progress >= 1.f)
				{
					S.Progress -= 1.f;
					--S.InputBuffer;
					++S.Buffer;
					S.BufferRes = ProcessedForm(S.InputRes);
					if (S.InputBuffer == 0 && S.Buffer == 0) S.InputRes = EMinResource::None;
				}
			}
			if (S.Buffer >= 2 && S.RetryTimer <= 0.f)
			{
				S.RetryTimer = 1.0f;
				DispatchFrom(S, S.BufferRes, FMath::Min(S.Buffer, CartLoad));
			}
			break;
		}
		case EMinStructure::Generator:
			S.FuelSeconds = FMath::Max(0.f, S.FuelSeconds - Dt);
			break;
		default: break;
		}
	}
}

void UMinSim::TickCarts(float Dt)
{
	// recount occupancy
	for (FMinEdge& E : Edges) E.Occupancy = 0;
	for (const FMinCart& Cart : Carts)
		if (Cart.bAlive && Cart.EdgePath.IsValidIndex(Cart.Leg))
			++Edges[Cart.EdgePath[Cart.Leg]].Occupancy;
	for (FMinEdge& E : Edges)
		E.CongestionSmoothed = FMath::FInterpTo(E.CongestionSmoothed,
			E.Occupancy / FMath::Max(1.f, E.Capacity), Dt, 2.f);

	for (FMinCart& Cart : Carts)
	{
		if (!Cart.bAlive) continue;
		if (!Cart.EdgePath.IsValidIndex(Cart.Leg)) { DeliverCargo(Cart); continue; }
		FMinEdge& E = Edges[Cart.EdgePath[Cart.Leg]];
		if (!E.bAlive) { Cart.bAlive = false; continue; }

		float Speed = E.Type == EMinRoute::Pipe ? 4.f : (E.Type == EMinRoute::LiftShaft ? 2.5f : 3.f);
		Speed *= EdgeSpeedMult(E);
		Cart.T += Dt * Speed / FMath::Max(1.f, E.Length);
		if (Cart.T >= 1.f)
		{
			Cart.T = 0.f;
			++Cart.Leg;
			if (Cart.Leg >= Cart.EdgePath.Num()) { DeliverCargo(Cart); continue; }
		}

		// world position along the edge path
		const bool bForward = Cart.NodePath[Cart.Leg] == E.A;
		const float TAlong = bForward ? Cart.T : 1.f - Cart.T;
		if (E.Type == EMinRoute::LiftShaft)
		{
			const FIntVector& A = E.Path[0];
			const FIntVector& B = E.Path.Last();
			const FVector WA = Min::CellToWorld(A.X, A.Y, A.Z, Min::DepthCart);
			const FVector WB = Min::CellToWorld(B.X, B.Y, B.Z, Min::DepthCart);
			Cart.WorldPos = FMath::Lerp(WA, WB, TAlong);
			Cart.Plane = FMath::RoundToInt(FMath::Lerp((float)A.Z, (float)B.Z, TAlong));
		}
		else
		{
			const float FIdx = TAlong * (E.Path.Num() - 1);
			const int32 I0 = FMath::Clamp(FMath::FloorToInt(FIdx), 0, E.Path.Num() - 2);
			const float Frac = FIdx - I0;
			const FIntVector& A = E.Path[I0];
			const FIntVector& B = E.Path[I0 + 1];
			Cart.WorldPos = FMath::Lerp(
				Min::CellToWorld(A.X, A.Y, A.Z, Min::DepthCart),
				Min::CellToWorld(B.X, B.Y, B.Z, Min::DepthCart), Frac);
			Cart.Plane = A.Z;
		}
	}

	// compact dead carts occasionally
	if (Carts.Num() > 64)
		Carts.RemoveAll([](const FMinCart& C) { return !C.bAlive; });
}

void UMinSim::TickContracts()
{
	if (!Contracts.IsValidIndex(ActiveContract)) return;
	FMinContract& C = Contracts[ActiveContract];
	if (!C.bComplete && C.Delivered >= C.Needed)
	{
		C.bComplete = true;
		Credits += C.RewardCredits;
		OnEvent.Broadcast(FString::Printf(TEXT("Contract complete: %s  (+%d credits)"), *C.Title, C.RewardCredits));
		if (ActiveContract + 1 < Contracts.Num()) ++ActiveContract;
	}
}

void UMinSim::Tick(float Dt)
{
	if (bPaused) return;
	Dt *= SimSpeed;
	PlayTime += Dt;
	PowerTimer -= Dt;
	if (PowerTimer <= 0.f) { PowerTimer = 0.5f; UpdatePower(); }
	TickStructures(Dt);
	TickCarts(Dt);
	TickContracts();

	// congestion warning
	CurrentWarning.Reset();
	for (const FMinEdge& E : Edges)
	{
		if (E.bAlive && E.CongestionSmoothed > 1.f)
		{
			CurrentWarning = FString::Printf(TEXT("Route congested on %s"), *Min::PlaneName(E.Path[0].Z));
			break;
		}
	}
}

int32 UMinSim::CartsInFlight() const
{
	int32 N = 0;
	for (const FMinCart& C : Carts) if (C.bAlive) ++N;
	return N;
}

float UMinSim::AvgCongestion() const
{
	float Sum = 0.f; int32 N = 0;
	for (const FMinEdge& E : Edges) if (E.bAlive) { Sum += E.CongestionSmoothed; ++N; }
	return N ? Sum / N : 0.f;
}

// ---------------------------------------------------------------------------
// Save / load (JSON in Saved/SaveGames)
// ---------------------------------------------------------------------------
static FString SavePath(const FString& Slot)
{
	return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (Slot + TEXT(".json"));
}

bool UMinSim::SaveExists(const FString& Slot)
{
	return FPaths::FileExists(SavePath(Slot));
}

bool UMinSim::SaveGame(const FString& Slot) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("seed"), World->WorldSeed);
	Root->SetNumberField(TEXT("credits"), Credits);
	Root->SetNumberField(TEXT("playTime"), PlayTime);
	Root->SetNumberField(TEXT("activeContract"), ActiveContract);
	Root->SetNumberField(TEXT("totalDeliveries"), TotalDeliveries);

	TArray<TSharedPtr<FJsonValue>> JStock;
	for (const auto& Pair : Stock)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("res"), (int32)Pair.Key);
		O->SetNumberField(TEXT("n"), Pair.Value);
		JStock.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("stock"), JStock);

	TArray<TSharedPtr<FJsonValue>> JContracts;
	for (const FMinContract& C : Contracts)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("delivered"), C.Delivered);
		O->SetBoolField(TEXT("complete"), C.bComplete);
		JContracts.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("contracts"), JContracts);

	TArray<TSharedPtr<FJsonValue>> JExc;
	for (const FIntVector& C : World->ExcavatedList)
	{
		TArray<TSharedPtr<FJsonValue>> V = {
			MakeShared<FJsonValueNumber>(C.X), MakeShared<FJsonValueNumber>(C.Y), MakeShared<FJsonValueNumber>(C.Z) };
		JExc.Add(MakeShared<FJsonValueArray>(V));
	}
	Root->SetArrayField(TEXT("excavated"), JExc);

	TArray<TSharedPtr<FJsonValue>> JDeps;
	for (const FMinDeposit& D : World->Deposits)
		JDeps.Add(MakeShared<FJsonValueNumber>(D.Remaining));
	Root->SetArrayField(TEXT("deposits"), JDeps);

	TArray<TSharedPtr<FJsonValue>> JStructs;
	for (const FMinStructureInst& S : Structures)
	{
		if (!S.bAlive) continue;
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("type"), (int32)S.Type);
		O->SetNumberField(TEXT("x"), S.Cell.X);
		O->SetNumberField(TEXT("y"), S.Cell.Y);
		O->SetNumberField(TEXT("p"), S.Cell.Z);
		O->SetNumberField(TEXT("tp"), S.TargetPlane);
		O->SetNumberField(TEXT("fuel"), S.FuelSeconds);
		JStructs.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("structures"), JStructs);

	TArray<TSharedPtr<FJsonValue>> JEdges;
	for (const FMinEdge& E : Edges)
	{
		if (!E.bAlive || E.Type == EMinRoute::LiftShaft) continue;
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("type"), (int32)E.Type);
		O->SetNumberField(TEXT("p"), E.Path[0].Z);
		TArray<TSharedPtr<FJsonValue>> Pts;
		for (const FIntVector& C : E.Path)
		{
			TArray<TSharedPtr<FJsonValue>> V = {
				MakeShared<FJsonValueNumber>(C.X), MakeShared<FJsonValueNumber>(C.Y) };
			Pts.Add(MakeShared<FJsonValueArray>(V));
		}
		O->SetArrayField(TEXT("path"), Pts);
		JEdges.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("edges"), JEdges);

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	const bool bOk = FFileHelper::SaveStringToFile(Out, *SavePath(Slot));
	UE_LOG(LogMin, Log, TEXT("SaveGame %s: %s"), *Slot, bOk ? TEXT("ok") : TEXT("FAILED"));
	return bOk;
}

bool UMinSim::LoadGame(const FString& Slot)
{
	FString In;
	if (!FFileHelper::LoadFileToString(In, *SavePath(Slot))) return false;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(In), Root) || !Root.IsValid()) return false;

	// world must already be generated with the same seed by the game mode
	Credits = Root->GetIntegerField(TEXT("credits"));
	PlayTime = Root->GetNumberField(TEXT("playTime"));
	ActiveContract = Root->GetIntegerField(TEXT("activeContract"));
	TotalDeliveries = Root->GetIntegerField(TEXT("totalDeliveries"));

	Stock.Reset();
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("stock")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		Stock.Add((EMinResource)(int32)O->GetNumberField(TEXT("res")), O->GetIntegerField(TEXT("n")));
	}
	const TArray<TSharedPtr<FJsonValue>>& JContracts = Root->GetArrayField(TEXT("contracts"));
	for (int32 I = 0; I < JContracts.Num() && I < Contracts.Num(); ++I)
	{
		const TSharedPtr<FJsonObject> O = JContracts[I]->AsObject();
		Contracts[I].Delivered = O->GetIntegerField(TEXT("delivered"));
		Contracts[I].bComplete = O->GetBoolField(TEXT("complete"));
	}

	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("excavated")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		World->Excavate((int32)A[0]->AsNumber(), (int32)A[1]->AsNumber(), (int32)A[2]->AsNumber());
	}
	const TArray<TSharedPtr<FJsonValue>>& JDeps = Root->GetArrayField(TEXT("deposits"));
	for (int32 I = 0; I < JDeps.Num() && I < World->Deposits.Num(); ++I)
		World->Deposits[I].Remaining = (int32)JDeps[I]->AsNumber();

	const int32 SavedCredits = Credits;
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("edges")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		TArray<FIntPoint> Cells;
		for (const TSharedPtr<FJsonValue>& PV : O->GetArrayField(TEXT("path")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = PV->AsArray();
			Cells.Add(FIntPoint((int32)A[0]->AsNumber(), (int32)A[1]->AsNumber()));
		}
		Credits = 1000000; // rebuild without cost constraints
		BuildRoute(Cells, (int32)O->GetNumberField(TEXT("p")), (EMinRoute)(int32)O->GetNumberField(TEXT("type")));
	}
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("structures")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		const EMinStructure Type = (EMinStructure)(int32)O->GetNumberField(TEXT("type"));
		const FIntPoint Cell((int32)O->GetNumberField(TEXT("x")), (int32)O->GetNumberField(TEXT("y")));
		const int32 P = (int32)O->GetNumberField(TEXT("p"));
		Credits = 1000000;
		if (Type == EMinStructure::Lift)
			BuildLift(Cell, P, (int32)O->GetNumberField(TEXT("tp")));
		else
			BuildStructure(Type, Cell, P);
		if (Structures.Num() && Structures.Last().Type == EMinStructure::Generator)
			Structures.Last().FuelSeconds = (float)O->GetNumberField(TEXT("fuel"));
	}
	Credits = SavedCredits;
	bTopologyDirty = true;
	bStructuresDirty = true;
	UE_LOG(LogMin, Log, TEXT("LoadGame %s: %d structures, %d edges"), *Slot, Structures.Num(), Edges.Num());
	return true;
}
