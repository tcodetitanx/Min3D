#include "MinPlaneActor.h"
#include "Min3D.h"
#include "MinWorld.h"
#include "MinAssets.h"
#include "MinSim.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "RHI.h"

namespace
{
	// local-space point of a cell center on this plane's meshes
	FORCEINLINE FVector CellPt(const FIntVector& C)
	{
		return FVector(C.X * Min::CellSize + Min::CellSize * 0.5f, 0.f,
			-(C.Y * Min::CellSize + Min::CellSize * 0.5f));
	}

	struct FMeshAccum
	{
		TArray<FVector> V;
		TArray<int32> T;
		TArray<FVector> N;
		TArray<FVector2D> UV;
		TArray<FColor> C;

		// vertex color bytes are read raw (no sRGB decode) by the shader, so
		// encode desired sRGB display colors into linear byte values
		static FColor Enc(const FColor& SrgbCol)
		{
			return FLinearColor::FromSRGBColor(SrgbCol).ToFColor(false);
		}

		void Quad(const FVector& A, const FVector& B, const FVector& C2, const FVector& D, const FColor& ColIn)
		{
			const FColor Col = Enc(ColIn);
			const int32 I = V.Num();
			V.Append({ A, B, C2, D });
			N.Append({ FVector(0,-1,0), FVector(0,-1,0), FVector(0,-1,0), FVector(0,-1,0) });
			UV.Append({ FVector2D(0,0), FVector2D(1,0), FVector2D(1,1), FVector2D(0,1) });
			C.Append({ Col, Col, Col, Col });
			T.Append({ I, I + 2, I + 1, I, I + 3, I + 2 });
		}

		// thick line segment in the XZ plane
		void Strip(const FVector& P0, const FVector& P1, float HalfW, const FColor& Col)
		{
			FVector Dir = P1 - P0; Dir.Y = 0.f;
			if (!Dir.Normalize()) return;
			const FVector Side = FVector(-Dir.Z, 0.f, Dir.X) * HalfW;
			Quad(P0 - Side, P1 - Side, P1 + Side, P0 + Side, Col);
		}

		void Disc(const FVector& Center, float Radius, const FColor& ColIn, int32 Segs = 10)
		{
			const FColor Col = Enc(ColIn);
			const int32 CI = V.Num();
			V.Add(Center); N.Add(FVector(0, -1, 0)); UV.Add(FVector2D(0.5f, 0.5f)); C.Add(Col);
			for (int32 S = 0; S <= Segs; ++S)
			{
				const float A = 2.f * PI * S / Segs;
				V.Add(Center + FVector(FMath::Cos(A) * Radius, 0.f, FMath::Sin(A) * Radius));
				N.Add(FVector(0, -1, 0)); UV.Add(FVector2D(0.5f, 0.5f)); C.Add(Col);
				if (S > 0) T.Append({ CI, CI + S + 1, CI + S });
			}
		}

		// polyline with rounded joints: straight strips + discs at bends/ends
		void Polyline(const TArray<FVector>& Pts, float HalfW, const FColor& Col)
		{
			for (int32 I = 0; I + 1 < Pts.Num(); ++I) Strip(Pts[I], Pts[I + 1], HalfW, Col);
			for (int32 I = 0; I < Pts.Num(); ++I)
			{
				const bool bEnd = I == 0 || I == Pts.Num() - 1;
				bool bBend = false;
				if (!bEnd)
				{
					const FVector D0 = (Pts[I] - Pts[I - 1]).GetSafeNormal();
					const FVector D1 = (Pts[I + 1] - Pts[I]).GetSafeNormal();
					bBend = !D0.Equals(D1, 0.01f);
				}
				if (bEnd || bBend) Disc(Pts[I], HalfW, Col);
			}
		}
	};

	TArray<FVector> PathToPoints(const TArray<FIntVector>& Path)
	{
		// compress collinear runs so long straights are single strips
		TArray<FVector> Pts;
		for (int32 I = 0; I < Path.Num(); ++I)
		{
			if (I > 0 && I + 1 < Path.Num())
			{
				const FIntVector D0 = Path[I] - Path[I - 1];
				const FIntVector D1 = Path[I + 1] - Path[I];
				if (D0 == D1) continue;
			}
			Pts.Add(CellPt(Path[I]));
		}
		return Pts;
	}
}

namespace
{
	struct FPal { FColor Base; FColor Deep; };
	// pastel terrain palette: Base at surface, Deep at bottom of the world
	FPal Palette(EMinTerrain T)
	{
		switch (T)
		{
		case EMinTerrain::Soil:      return { FColor(232, 213, 176), FColor(214, 192, 152) };
		case EMinTerrain::Sandstone: return { FColor(230, 195, 154), FColor(212, 174, 134) };
		case EMinTerrain::Limestone: return { FColor(214, 203, 180), FColor(193, 180, 156) };
		case EMinTerrain::Clay:      return { FColor(217, 169, 126), FColor(196, 146, 106) };
		case EMinTerrain::Granite:   return { FColor(154, 160, 172), FColor(132, 138, 152) };
		case EMinTerrain::Obsidian:  return { FColor(106, 90, 112),  FColor(84, 70, 92) };
		case EMinTerrain::Aquifer:   return { FColor(126, 196, 221), FColor(104, 172, 204) };
		default:                     return { FColor(240, 232, 210), FColor(226, 214, 188) };
		}
	}
}

AMinPlaneActor::AMinPlaneActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMinPlaneActor::Init(int32 InPlane, UMinWorld* InWorld, UMinAssets* InAssets)
{
	Plane = InPlane;
	World = InWorld;
	Assets = InAssets;

	const int32 W = Min::NX * Min::TexPerCell;
	const int32 H = Min::NY * Min::TexPerCell;
	Pixels.SetNumUninitialized(W * H);

	TerrainTex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	TerrainTex->Filter = TF_Bilinear;
	TerrainTex->SRGB = true;
	TerrainTex->UpdateResource();

	TerrainQuad = NewObject<UStaticMeshComponent>(this);
	TerrainQuad->SetStaticMesh(Assets->GetQuad());
	TerrainMID = Assets->MakeTexMID(TerrainQuad, TerrainTex);
	TerrainQuad->SetMaterial(0, TerrainMID);
	TerrainQuad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TerrainQuad->SetCastShadow(false);
	TerrainQuad->SetTranslucentSortPriority(0);
	TerrainQuad->SetupAttachment(RootComponent);
	TerrainQuad->RegisterComponent();
	TerrainQuad->SetWorldLocation(FVector(Min::NX * Min::CellSize * 0.5f,
		Plane * Min::PlaneGap + Min::DepthTerrain, -Min::NY * Min::CellSize * 0.5f));
	TerrainQuad->SetWorldScale3D(FVector(Min::NX, 1.f, Min::NY));

	RouteMesh = NewObject<UProceduralMeshComponent>(this);
	RouteMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RouteMesh->SetCastShadow(false);
	RouteMesh->SetTranslucentSortPriority(2);
	RouteMesh->SetupAttachment(RootComponent);
	RouteMesh->RegisterComponent();
	RouteMesh->SetWorldLocation(FVector(0, Plane * Min::PlaneGap + Min::DepthRoute, 0));

	GhostRouteMesh = NewObject<UProceduralMeshComponent>(this);
	GhostRouteMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GhostRouteMesh->SetCastShadow(false);
	GhostRouteMesh->SetTranslucentSortPriority(2);
	GhostRouteMesh->SetupAttachment(RootComponent);
	GhostRouteMesh->RegisterComponent();
	GhostRouteMesh->SetWorldLocation(FVector(0, Plane * Min::PlaneGap + Min::DepthRoute, 0));

	RebuildTerrainTexture(FIntRect(0, 0, Min::NX, Min::NY));
	RefreshDepositMarkers();
}

FColor AMinPlaneActor::ShadePixel(float CX, float CY) const
{
	const float Density = World->VisualDensity(CX, CY, Plane);
	const float DepthT = FMath::Clamp(CY / float(Min::NY), 0.f, 1.f);

	const int32 IX = FMath::Clamp(FMath::FloorToInt(CX), 0, Min::NX - 1);
	const int32 IY = FMath::Clamp(FMath::FloorToInt(CY), 0, Min::NY - 1);
	const FMinCell& Cell = World->Cell(IX, IY, Plane);

	const float Solidness = FMath::SmoothStep(0.44f, 0.56f, Density);

	// open-space color: sky above the surface, warm cave interior below
	FLinearColor Open;
	if (Cell.Terrain == EMinTerrain::Sky)
	{
		const float T = FMath::Clamp(CY / float(Min::SkyRows + 2), 0.f, 1.f);
		Open = FLinearColor::LerpUsingHSV(
			FLinearColor(0.78f, 0.90f, 0.88f), FLinearColor(0.94f, 0.91f, 0.82f), T);
		const float Cl = UMinWorld::Fbm(CX * 0.10f, CY * 0.28f + Plane * 3.1f, World->WorldSeed + 5, 3);
		if (Cl > 0.62f) Open = FLinearColor::LerpUsingHSV(Open, FLinearColor(0.99f, 0.99f, 0.97f),
			FMath::Min(1.f, (Cl - 0.62f) * 6.f));
	}
	else
	{
		Open = FLinearColor::LerpUsingHSV(
			FLinearColor(0.952f, 0.925f, 0.850f), FLinearColor(0.828f, 0.776f, 0.678f), DepthT * 0.9f);
		const float WallShadow = FMath::Clamp((Density - 0.26f) / 0.24f, 0.f, 1.f);
		Open *= (1.f - WallShadow * 0.13f);
	}
	if (Solidness <= 0.f) return Open.ToFColor(true);

	// solid terrain
	const FPal P = Palette(Cell.Terrain);
	FLinearColor C = FLinearColor::LerpUsingHSV(FLinearColor(P.Base), FLinearColor(P.Deep), DepthT);

	// subtle color variation
	const float N = UMinWorld::ValueNoise(CX * 0.35f, CY * 0.35f, World->WorldSeed + Plane * 3 + 9);
	C *= (0.97f + N * 0.06f);

	// soft bevel highlight along open boundaries
	const float Edge = FMath::Clamp((0.68f - Density) / 0.18f, 0.f, 1.f);
	if (Edge > 0.f) C = FLinearColor::LerpUsingHSV(C, FLinearColor(1.f, 0.98f, 0.92f), Edge * 0.35f);

	// sparse speckles
	const float Spk = UMinWorld::Hash01(FMath::FloorToInt(CX * 3.f), FMath::FloorToInt(CY * 3.f),
		World->WorldSeed + Plane * 17 + 33);
	if (Spk > 0.985f) C *= 0.90f;
	else if (Spk < 0.008f) C = FLinearColor::LerpUsingHSV(C, FLinearColor::White, 0.25f);

	// resource tint blobs so deposits read in the terrain itself
	if (Cell.Resource != EMinResource::None && !Cell.bExcavated)
	{
		const FLinearColor RC = MinResInfo(Cell.Resource).Color;
		const float Blob = UMinWorld::ValueNoise(CX * 1.3f, CY * 1.3f, World->WorldSeed + 77);
		C = FLinearColor::LerpUsingHSV(C, RC, 0.18f + 0.20f * Blob);
	}

	// aquifer shimmer
	if (Cell.Terrain == EMinTerrain::Aquifer)
	{
		const float Sh = UMinWorld::ValueNoise(CX * 0.8f, CY * 0.8f + 11.f, World->WorldSeed + 3);
		C = FLinearColor::LerpUsingHSV(C, FLinearColor(0.6f, 0.85f, 0.95f), Sh * 0.3f);
	}
	if (Solidness < 1.f) C = FMath::Lerp(Open, C, Solidness);
	return C.ToFColor(true);
}

void AMinPlaneActor::RebuildTerrainTexture(const FIntRect& CellRect)
{
	if (!TerrainTex || !World) return;
	const int32 W = Min::NX * Min::TexPerCell;
	const int32 H = Min::NY * Min::TexPerCell;
	const int32 PX0 = FMath::Clamp(CellRect.Min.X * Min::TexPerCell, 0, W);
	const int32 PY0 = FMath::Clamp(CellRect.Min.Y * Min::TexPerCell, 0, H);
	const int32 PX1 = FMath::Clamp(CellRect.Max.X * Min::TexPerCell, 0, W);
	const int32 PY1 = FMath::Clamp(CellRect.Max.Y * Min::TexPerCell, 0, H);
	if (PX1 <= PX0 || PY1 <= PY0) return;

	const float Inv = 1.f / Min::TexPerCell;
	ParallelFor(PY1 - PY0, [&](int32 Row)
	{
		const int32 PY = PY0 + Row;
		FColor* Line = Pixels.GetData() + PY * W;
		for (int32 PX = PX0; PX < PX1; ++PX)
		{
			Line[PX] = ShadePixel((PX + 0.5f) * Inv, (PY + 0.5f) * Inv);
			Line[PX].A = 255;
		}
	});

	// upload the dirty region
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(PX0, PY0, PX0, PY0, PX1 - PX0, PY1 - PY0);
	TerrainTex->UpdateTextureRegions(0, 1, Region, W * 4, 4,
		reinterpret_cast<uint8*>(Pixels.GetData()),
		[](uint8*, const FUpdateTextureRegion2D* R) { delete R; });
}

void AMinPlaneActor::RefreshDepositMarkers()
{
	// deposit marker sprite ids by resource
	auto MarkerFor = [](EMinResource R) -> const TCHAR*
	{
		switch (R)
		{
		case EMinResource::Coal:    return TEXT("res_00_04");
		case EMinResource::Iron:    return TEXT("res_00_00");
		case EMinResource::Copper:  return TEXT("res_00_01");
		case EMinResource::Gold:    return TEXT("res_00_02");
		case EMinResource::Crystal: return TEXT("res_00_07");
		case EMinResource::Water:   return TEXT("res_00_06");
		default:                    return TEXT("res_00_08");
		}
	};

	for (const FMinDeposit& D : World->Deposits)
	{
		if (D.Plane != Plane || D.Remaining <= 0) continue;
		if (DepositSprites.Contains(D.Id)) continue;
		UStaticMeshComponent* C = Assets->MakeSpriteComponent(this, MarkerFor(D.Resource), 220.f,
			FLinearColor::White, 1.f, /*SortPriority=*/1);
		C->SetWorldLocation(Min::CellToWorld(D.Center.X, D.Center.Y, Plane, Min::DepthDeposit));
		DepositSprites.Add(D.Id, C);
		UE_LOG(LogMin, Log, TEXT("Deposit marker %d %s plane=%d at %s"), D.Id,
			*FString(MinResInfo(D.Resource).Name), Plane, *C->GetComponentLocation().ToString());
	}
	// remove depleted
	for (auto It = DepositSprites.CreateIterator(); It; ++It)
	{
		const FMinDeposit* D = World->Deposits.IsValidIndex(It.Key()) ? &World->Deposits[It.Key()] : nullptr;
		if (!D || D->Remaining <= 0)
		{
			if (It.Value()) It.Value()->DestroyComponent();
			It.RemoveCurrent();
		}
	}
}

void AMinPlaneActor::RebuildRoutes(UMinSim* Sim)
{
	if (!RouteMID)
	{
		RouteMID = UMaterialInstanceDynamic::Create(Assets->GetVertexMaterial(), this);
		RouteMesh->SetMaterial(0, RouteMID);
	}

	FMeshAccum Under, Over; // outer rims then inner lanes
	for (const FMinEdge& E : Sim->Edges)
	{
		if (!E.bAlive) continue;
		const int32 PA = E.Path[0].Z, PB = E.Path.Last().Z;

		if (E.Type == EMinRoute::LiftShaft)
		{
			// shaft marker ring wherever the shaft crosses this plane
			const int32 P0 = FMath::Min(PA, PB), P1 = FMath::Max(PA, PB);
			if (Plane >= P0 && Plane <= P1)
			{
				const FVector C = CellPt(FIntVector(E.Path[0].X, E.Path[0].Y, 0));
				Under.Disc(C, 34.f, FColor(96, 84, 74, 235), 14);
				Over.Disc(C, 24.f, FColor(150, 200, 235, 235), 14);
			}
			continue;
		}
		if (PA != Plane) continue;

		const TArray<FVector> Pts = PathToPoints(E.Path);
		const bool bPipe = E.Type == EMinRoute::Pipe;
		// colors are authored as display-intent sRGB; FMeshAccum::Enc linearizes
		const FColor Rim = bPipe ? FColor(88, 102, 118, 240) : FColor(94, 82, 70, 240);
		FLinearColor LaneDisp = bPipe ? FLinearColor(0.30f, 0.55f, 0.72f) : FLinearColor(0.20f, 0.17f, 0.155f);
		// congestion glows the lane toward warm red
		LaneDisp = FMath::Lerp(LaneDisp, FLinearColor(0.88f, 0.28f, 0.16f),
			FMath::Clamp(E.CongestionSmoothed - 0.4f, 0.f, 1.f));
		Under.Polyline(Pts, bPipe ? 20.f : 26.f, Rim);
		Over.Polyline(Pts, bPipe ? 11.f : 16.f, LaneDisp.ToFColor(false));
	}

	// junction dots
	for (const FMinNode& N : Sim->Nodes)
	{
		if (N.Cell.Z != Plane || N.Edges.Num() < 2) continue;
		int32 AliveEdges = 0;
		for (int32 EId : N.Edges) if (Sim->Edges[EId].bAlive) ++AliveEdges;
		if (AliveEdges < 2) continue;
		const FVector C = CellPt(FIntVector(N.Cell.X, N.Cell.Y, 0));
		Under.Disc(C, 32.f, FColor(94, 82, 70, 240), 12);
		Over.Disc(C, 20.f, FColor(238, 228, 205, 245), 12);
	}

	// merge: rims first so lanes draw over them
	FMeshAccum All = MoveTemp(Under);
	{
		const int32 Base = All.V.Num();
		All.V.Append(Over.V); All.N.Append(Over.N); All.UV.Append(Over.UV); All.C.Append(Over.C);
		for (int32 I : Over.T) All.T.Add(Base + I);
	}
	// lift the lane slightly toward the camera to avoid z-fighting
	for (int32 I = 0; I < All.V.Num(); ++I) All.V[I].Y = 0.f;

	RouteMesh->CreateMeshSection(0, All.V, All.T, All.N, All.UV, All.C, TArray<FProcMeshTangent>(), false);
}

void AMinPlaneActor::SyncStructures(UMinSim* Sim)
{
	auto SpriteFor = [](const FMinStructureInst& S) { return UMinSim::StructureSprite(S.Type); };
	auto SizeFor = [](EMinStructure T)
	{
		switch (T)
		{
		case EMinStructure::Base: return 420.f;
		case EMinStructure::Smelter: return 230.f;
		case EMinStructure::Depot: return 210.f;
		case EMinStructure::Generator: return 190.f;
		case EMinStructure::Lift: return 190.f;
		case EMinStructure::Pump: return 170.f;
		default: return 185.f;
		}
	};

	for (const FMinStructureInst& S : Sim->Structures)
	{
		const bool bHere = S.bAlive && (S.Cell.Z == Plane ||
			(S.Type == EMinStructure::Lift && S.TargetPlane == Plane));
		const int32 Key = S.Id;
		TObjectPtr<UStaticMeshComponent>* Existing = StructureSprites.Find(Key);
		if (bHere && !Existing)
		{
			UStaticMeshComponent* C = Assets->MakeSpriteComponent(this, SpriteFor(S), SizeFor(S.Type),
				FLinearColor::White, 1.f, /*SortPriority=*/3);
			FVector Loc = Min::CellToWorld(S.Cell.X, S.Cell.Y, Plane, Min::DepthStructure);
			// the base building sits on top of the surface rather than inside it
			if (S.Type == EMinStructure::Base) Loc.Z += Min::CellSize * 1.4f;
			C->SetWorldLocation(Loc);
			StructureSprites.Add(Key, C);
		}
		else if (!bHere && Existing)
		{
			if (*Existing) (*Existing)->DestroyComponent();
			StructureSprites.Remove(Key);
		}
		else if (bHere && Existing && *Existing)
		{
			// unpowered structures dim slightly
			if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>((*Existing)->GetMaterial(0)))
				M->SetVectorParameterValue(TEXT("Tint"),
					S.bPowered ? FLinearColor::White : FLinearColor(0.72f, 0.70f, 0.68f));
		}
	}
}

void AMinPlaneActor::UpdateCarts(UMinSim* Sim)
{
	auto CartSprite = [](EMinResource R) -> const TCHAR*
	{
		switch (R)
		{
		case EMinResource::Coal: return TEXT("veh_01_00");
		case EMinResource::Iron: return TEXT("veh_01_02");
		case EMinResource::Copper: return TEXT("veh_01_05");
		case EMinResource::Gold: return TEXT("veh_01_01");
		case EMinResource::Crystal: return TEXT("veh_01_03");
		case EMinResource::Water: return TEXT("veh_01_06");
		case EMinResource::IronIngot: return TEXT("veh_02_01");
		case EMinResource::CopperIngot: return TEXT("veh_02_02");
		case EMinResource::GoldBar: return TEXT("veh_02_00");
		default: return TEXT("veh_00_00");
		}
	};

	int32 Used = 0;
	for (const FMinCart& Cart : Sim->Carts)
	{
		if (!Cart.bAlive || Cart.Plane != Plane) continue;
		if (Used >= CartPool.Num())
		{
			UStaticMeshComponent* C = Assets->MakeSpriteComponent(this, CartSprite(Cart.Cargo), 95.f,
				FLinearColor::White, 1.f, /*SortPriority=*/4);
			CartPool.Add(C);
			CartPoolSprite.Add(CartSprite(Cart.Cargo));
		}
		UStaticMeshComponent* C = CartPool[Used];
		const FString WantSprite = CartSprite(Cart.Cargo);
		if (CartPoolSprite[Used] != WantSprite)
		{
			CartPoolSprite[Used] = WantSprite;
			if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)))
				M->SetTextureParameterValue(TEXT("Tex"), Assets->GetSprite(WantSprite));
		}
		C->SetWorldLocation(FVector(Cart.WorldPos.X, Plane * Min::PlaneGap + Min::DepthCart, Cart.WorldPos.Z));
		C->SetVisibility(ContentOpacityCur > 0.01f);
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)))
			M->SetScalarParameterValue(TEXT("Opacity"), ContentOpacityCur);
		++Used;
	}
	for (int32 I = Used; I < CartPool.Num(); ++I)
		if (CartPool[I]) CartPool[I]->SetVisibility(false);
}

void AMinPlaneActor::ShowPreview(const TArray<FIntPoint>& Cells, bool bValid, EMinRoute Type)
{
	if (!PreviewMID)
	{
		PreviewMID = UMaterialInstanceDynamic::Create(Assets->GetVertexMaterial(), this);
		GhostRouteMesh->SetMaterial(0, PreviewMID);
		GhostRouteMesh->SetTranslucentSortPriority(6);
	}
	FMeshAccum M;
	const FColor Col = bValid ? FColor(90, 210, 120, 150) : FColor(230, 70, 60, 150);
	TArray<FVector> Pts;
	for (const FIntPoint& C : Cells) Pts.Add(CellPt(FIntVector(C.X, C.Y, 0)));
	if (Pts.Num() == 1) M.Disc(Pts[0], 30.f, Col);
	else M.Polyline(Pts, Type == EMinRoute::Pipe ? 18.f : 24.f, Col);
	GhostRouteMesh->CreateMeshSection(0, M.V, M.T, M.N, M.UV, M.C, TArray<FProcMeshTangent>(), false);
	GhostRouteMesh->SetVisibility(true);
}

void AMinPlaneActor::ClearPreview()
{
	GhostRouteMesh->ClearMeshSection(0);
}

void AMinPlaneActor::SetViewOpacity(float TerrainOpacity, float ContentOpacity, bool bShowDeposits)
{
	ContentOpacityCur = ContentOpacity;
	if (TerrainMID) TerrainMID->SetScalarParameterValue(TEXT("Opacity"), TerrainOpacity);
	TerrainQuad->SetVisibility(TerrainOpacity > 0.01f);
	const bool bContent = ContentOpacity > 0.01f;
	for (auto& Pair : DepositSprites)
	{
		if (!Pair.Value) continue;
		Pair.Value->SetVisibility(bShowDeposits);
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Pair.Value->GetMaterial(0)))
			M->SetScalarParameterValue(TEXT("Opacity"), ContentOpacity);
	}
	for (auto& Pair : StructureSprites)
	{
		if (!Pair.Value) continue;
		Pair.Value->SetVisibility(bContent);
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Pair.Value->GetMaterial(0)))
			M->SetScalarParameterValue(TEXT("Opacity"), ContentOpacity);
	}
	if (RouteMID) RouteMID->SetScalarParameterValue(TEXT("Opacity"), ContentOpacity);
	RouteMesh->SetVisibility(bContent);
}
