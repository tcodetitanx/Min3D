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
	// plane-local point of a cell center
	FORCEINLINE FVector CellPt(int32 U, int32 V, float Depth = 0.f)
	{
		return FVector((U + 0.5f) * Min::CellSize, Depth, -((V + 0.5f) * Min::CellSize));
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

		// axis-aligned square centered at P
		void Box(const FVector& P, float Half, const FColor& Col)
		{
			Quad(P + FVector(-Half, 0, -Half), P + FVector(Half, 0, -Half),
				P + FVector(Half, 0, Half), P + FVector(-Half, 0, Half), Col);
		}

		// thick line segment in the local XZ plane
		void Strip(const FVector& P0, const FVector& P1, float HalfW, const FColor& Col)
		{
			FVector Dir = P1 - P0; Dir.Y = 0.f;
			if (!Dir.Normalize()) return;
			const FVector Side = FVector(-Dir.Z, 0.f, Dir.X) * HalfW;
			Quad(P0 - Side, P1 - Side, P1 + Side, P0 + Side, Col);
		}

		void Disc(const FVector& Center, float Radius, const FColor& ColIn, int32 Segs = 12)
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

		// partial ring, clockwise from 12 o'clock; Frac in 0..1
		void Arc(const FVector& Center, float R0, float R1, float Frac, const FColor& ColIn, int32 Segs = 24)
		{
			const FColor Col = Enc(ColIn);
			const int32 NSeg = FMath::Clamp(FMath::CeilToInt(Segs * Frac), 1, Segs);
			for (int32 S = 0; S < NSeg; ++S)
			{
				const float A0 = PI * 0.5f - 2.f * PI * Frac * S / NSeg;
				const float A1 = PI * 0.5f - 2.f * PI * Frac * (S + 1) / NSeg;
				const FVector D0(FMath::Cos(A0), 0.f, FMath::Sin(A0));
				const FVector D1(FMath::Cos(A1), 0.f, FMath::Sin(A1));
				Quad(Center + D0 * R0, Center + D1 * R0, Center + D1 * R1, Center + D0 * R1, ColIn);
			}
		}
	};

	struct FPal { FColor Base; FColor Deep; };
	// pastel terrain palette: Base near the surface, Deep at the bottom
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
		default:                     return { FColor(240, 232, 210), FColor(226, 214, 188) };
		}
	}

	FColor Darken(const FLinearColor& C, float F)
	{
		return (C * F).ToFColor(true);
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

	// the actor transform turns plane-local space into world space:
	// front planes sit at a fixed world Y, side planes rotate 90 degrees so
	// their local X axis runs front-to-back along world Y
	if (Min::IsFront(Plane))
	{
		SetActorLocation(FVector(0.f, Min::FrontPlaneY(Plane), 0.f));
		SetActorRotation(FRotator::ZeroRotator);
	}
	else
	{
		SetActorLocation(FVector(Min::SidePlaneX(Plane - Min::NumFront), 0.f, 0.f));
		SetActorRotation(FRotator(0.f, 90.f, 0.f));
	}

	const int32 CW = Min::PlaneW(Plane), CH = Min::PlaneH(Plane);
	const int32 W = CW * Min::TexPerCell;
	const int32 H = CH * Min::TexPerCell;
	TerrainPixels.SetNumUninitialized(W * H);
	BackPixels.SetNumUninitialized(W * H);

	auto MakeTex = [&]() -> UTexture2D*
	{
		UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		Tex->Filter = TF_Bilinear;
		Tex->SRGB = true;
		Tex->UpdateResource();
		return Tex;
	};
	TerrainTex = MakeTex();
	BackTex = MakeTex();

	auto MakeQuad = [&](UTexture2D* Tex, UMaterial* Mat, float Depth, int32 Sort) -> TPair<UStaticMeshComponent*, UMaterialInstanceDynamic*>
	{
		UStaticMeshComponent* Q = NewObject<UStaticMeshComponent>(this);
		Q->SetStaticMesh(Assets->GetQuad());
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Mat, Q);
		MID->SetTextureParameterValue(TEXT("Tex"), Tex);
		MID->SetVectorParameterValue(TEXT("Tint"), FLinearColor::White);
		MID->SetScalarParameterValue(TEXT("Opacity"), 1.f);
		Q->SetMaterial(0, MID);
		Q->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Q->SetCastShadow(false);
		Q->SetTranslucentSortPriority(Sort);
		Q->SetupAttachment(RootComponent);
		Q->RegisterComponent();
		Q->SetRelativeLocation(FVector(CW * Min::CellSize * 0.5f, Depth, -CH * Min::CellSize * 0.5f));
		Q->SetRelativeScale3D(FVector(CW * Min::CellSize / 100.f, 1.f, CH * Min::CellSize / 100.f));
		return { Q, MID };
	};
	auto Back = MakeQuad(BackTex, Assets->GetSpriteMaterial(), Min::DepthBack, -1);
	BackQuad = Back.Key; BackMID = Back.Value;
	auto Terr = MakeQuad(TerrainTex, Assets->GetTerrainMaterial(), Min::DepthTerrain, 0);
	TerrainQuad = Terr.Key; TerrainMID = Terr.Value;

	auto MakeProcMesh = [&](float Depth, int32 Sort) -> UProceduralMeshComponent*
	{
		UProceduralMeshComponent* M = NewObject<UProceduralMeshComponent>(this);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(false);
		M->SetTranslucentSortPriority(Sort);
		M->SetupAttachment(RootComponent);
		M->RegisterComponent();
		M->SetRelativeLocation(FVector(0.f, Depth, 0.f));
		return M;
	};
	RouteMesh = MakeProcMesh(Min::DepthRoute, 2);
	MarkerMesh = MakeProcMesh(Min::DepthMarker, 3);
	GhostRouteMesh = MakeProcMesh(Min::DepthFX, 6);

	RouteMID = UMaterialInstanceDynamic::Create(Assets->GetVertexMaterial(), this);
	RouteMesh->SetMaterial(0, RouteMID);
	MarkerMID = UMaterialInstanceDynamic::Create(Assets->GetVertexMaterial(), this);
	MarkerMesh->SetMaterial(0, MarkerMID);
	PreviewMID = UMaterialInstanceDynamic::Create(Assets->GetVertexMaterial(), this);
	GhostRouteMesh->SetMaterial(0, PreviewMID);

	RebuildTerrainTexture(FIntRect(0, 0, CW, CH));
}

void AMinPlaneActor::ShadePixel(float CU, float CV, FColor& OutSolid, FColor& OutBack) const
{
	const float Density = World->VisualDensity(Plane, CU, CV);
	const float DepthT = FMath::Clamp(CV / float(Min::PlaneH(Plane)), 0.f, 1.f);

	const int32 IU = FMath::Clamp(FMath::FloorToInt(CU), 0, Min::PlaneW(Plane) - 1);
	const int32 IV = FMath::Clamp(FMath::FloorToInt(CV), 0, Min::PlaneH(Plane) - 1);
	const FMinCell& Cell = World->PlaneCell(Plane, IU, IV);

	// ---- open-space color: sky above the surface, warm cave interior below
	FLinearColor Open;
	if (Cell.Terrain == EMinTerrain::Sky)
	{
		const float T = FMath::Clamp(CV / float(Min::SkyRows + 2), 0.f, 1.f);
		Open = FLinearColor::LerpUsingHSV(
			FLinearColor(0.78f, 0.90f, 0.88f), FLinearColor(0.94f, 0.91f, 0.82f), T);
		const float Cl = UMinWorld::Fbm(CU * 0.10f, CV * 0.28f + Plane * 3.1f, World->WorldSeed + 5, 3);
		if (Cl > 0.62f) Open = FLinearColor::LerpUsingHSV(Open, FLinearColor(0.99f, 0.99f, 0.97f),
			FMath::Min(1.f, (Cl - 0.62f) * 6.f));
	}
	else
	{
		Open = FLinearColor::LerpUsingHSV(
			FLinearColor(0.952f, 0.925f, 0.850f), FLinearColor(0.828f, 0.776f, 0.678f), DepthT * 0.9f);
		const float WallShadow = FMath::Clamp((Density - 0.20f) / 0.30f, 0.f, 1.f);
		Open *= (1.f - WallShadow * 0.13f);
	}
	OutBack = Open.ToFColor(true);
	OutBack.A = 255;

	// ---- solid terrain color (computed everywhere; masked by density alpha)
	const FPal P = Palette(Cell.Terrain);
	FLinearColor C = FLinearColor::LerpUsingHSV(FLinearColor(P.Base), FLinearColor(P.Deep), DepthT);

	// subtle color variation
	const float NV = UMinWorld::ValueNoise(CU * 0.55f, CV * 0.55f, World->WorldSeed + Plane * 3 + 9);
	C *= (0.97f + NV * 0.06f);

	// soft bevel highlight along open boundaries
	const float Edge = FMath::Clamp((0.68f - Density) / 0.18f, 0.f, 1.f);
	if (Edge > 0.f) C = FLinearColor::LerpUsingHSV(C, FLinearColor(1.f, 0.98f, 0.92f), Edge * 0.30f);

	// sparse speckles
	const float Spk = UMinWorld::Hash01(FMath::FloorToInt(CU * 4.f), FMath::FloorToInt(CV * 4.f),
		World->WorldSeed + Plane * 17 + 33);
	if (Spk > 0.985f) C *= 0.90f;
	else if (Spk < 0.008f) C = FLinearColor::LerpUsingHSV(C, FLinearColor::White, 0.25f);

	OutSolid = C.ToFColor(true);
	// alpha carries the density field; the terrain material cuts a crisp,
	// anti-aliased edge at 0.5 whatever the zoom
	OutSolid.A = (uint8)FMath::Clamp(FMath::RoundToInt(Density * 255.f), 0, 255);
}

void AMinPlaneActor::RebuildTerrainTexture(const FIntRect& CellRect)
{
	if (!TerrainTex || !World) return;
	const int32 W = Min::PlaneW(Plane) * Min::TexPerCell;
	const int32 H = Min::PlaneH(Plane) * Min::TexPerCell;
	const int32 PX0 = FMath::Clamp(CellRect.Min.X * Min::TexPerCell, 0, W);
	const int32 PY0 = FMath::Clamp(CellRect.Min.Y * Min::TexPerCell, 0, H);
	const int32 PX1 = FMath::Clamp(CellRect.Max.X * Min::TexPerCell, 0, W);
	const int32 PY1 = FMath::Clamp(CellRect.Max.Y * Min::TexPerCell, 0, H);
	if (PX1 <= PX0 || PY1 <= PY0) return;

	const float Inv = 1.f / Min::TexPerCell;
	ParallelFor(PY1 - PY0, [&](int32 Row)
	{
		const int32 PY = PY0 + Row;
		FColor* SolidLine = TerrainPixels.GetData() + PY * W;
		FColor* BackLine = BackPixels.GetData() + PY * W;
		for (int32 PX = PX0; PX < PX1; ++PX)
			ShadePixel((PX + 0.5f) * Inv, (PY + 0.5f) * Inv, SolidLine[PX], BackLine[PX]);
	});

	auto Upload = [&](UTexture2D* Tex, TArray<FColor>& Pixels)
	{
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(PX0, PY0, PX0, PY0, PX1 - PX0, PY1 - PY0);
		Tex->UpdateTextureRegions(0, 1, Region, W * 4, 4,
			reinterpret_cast<uint8*>(Pixels.GetData()),
			[](uint8*, const FUpdateTextureRegion2D* R) { delete R; });
	};
	Upload(TerrainTex, TerrainPixels);
	Upload(BackTex, BackPixels);
}

void AMinPlaneActor::RebuildRoutes(UMinSim* Sim)
{
	FMeshAccum Under, Over; // outer rims, then inner lanes
	const FColor Rim(94, 82, 70, 240);
	const FColor Lane(51, 43, 40, 255);
	const FColor Transfer(120, 200, 220, 255);

	const float RimW = Min::CellSize * 0.165f;
	const float LaneW = Min::CellSize * 0.105f;

	for (const FIntVector& C : Sim->RouteCells)
	{
		FIntPoint UV;
		if (!Min::CellToPlaneUV(C, Plane, UV)) continue;
		const FVector P = CellPt(UV.X, UV.Y);
		Under.Disc(P, RimW, Rim, 10);
		Over.Disc(P, LaneW, Lane, 10);

		// a route cell on a shared column is a cross-plane junction
		if (Min::IsTransferCell(C))
		{
			Under.Disc(P, RimW * 1.5f, Rim, 4);
			Over.Disc(P, RimW * 1.05f, Transfer, 4);
		}

		// links to neighbors on this plane (draw each pair once; always draw
		// stubs into buildings since they are not in RouteCells)
		const FIntVector Dirs[6] = {
			{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1} };
		for (const FIntVector& D : Dirs)
		{
			const FIntVector NC = C + D;
			FIntPoint NUV;
			if (!Min::CellToPlaneUV(NC, Plane, NUV)) continue;
			if (!Min::CellsLinked(C, NC) || !Sim->IsWalkable(NC)) continue;
			const bool bNeighborRoute = Sim->RouteCells.Contains(NC);
			if (bNeighborRoute &&
				!(NC.X > C.X || NC.Y > C.Y || NC.Z > C.Z)) continue; // draw once
			const FVector NP = CellPt(NUV.X, NUV.Y);
			Under.Strip(P, NP, RimW, Rim);
			Over.Strip(P, NP, LaneW, Lane);
			if (!bNeighborRoute) { Under.Disc(NP, RimW, Rim, 10); Over.Disc(NP, LaneW, Lane, 10); }
		}
	}

	// merge: rims first so lanes draw over them
	FMeshAccum All = MoveTemp(Under);
	{
		const int32 Base = All.V.Num();
		All.V.Append(Over.V); All.N.Append(Over.N); All.UV.Append(Over.UV); All.C.Append(Over.C);
		for (int32 I : Over.T) All.T.Add(Base + I);
	}
	for (int32 I = 0; I < All.V.Num(); ++I) All.V[I].Y = 0.f;
	RouteMesh->CreateMeshSection(0, All.V, All.T, All.N, All.UV, All.C, TArray<FProcMeshTangent>(), false);
}

void AMinPlaneActor::RebuildMarkers(UMinSim* Sim)
{
	FMeshAccum M;

	// depot: cream square with a dark rim
	{
		FIntPoint UV;
		if (Min::CellToPlaneUV(Sim->DepotCell, Plane, UV))
		{
			const FVector P = CellPt(UV.X, UV.Y);
			M.Box(P, Min::CellSize * 0.62f, FColor(70, 62, 55, 255));
			M.Box(P, Min::CellSize * 0.54f, FColor(240, 230, 205, 255));
			if (!DepotSprite)
			{
				DepotSprite = Assets->MakeSpriteComponent(this, TEXT("net_04_03"),
					Min::CellSize * 0.9f, FLinearColor::White, 1.f, /*SortPriority=*/4);
				DepotSprite->SetWorldLocation(Min::CellOnPlane(Sim->DepotCell, Plane, Min::DepthMarker + 4.f));
				if (!Min::IsFront(Plane)) DepotSprite->SetWorldRotation(FRotator(0.f, 90.f, 0.f));
			}
		}
	}

	// pools: small colored discs
	for (const FMinPool& Pool : Sim->Pools)
	{
		if (!Pool.bAlive) continue;
		FIntPoint UV;
		if (!Min::CellToPlaneUV(Pool.Cell, Plane, UV)) continue;
		const FVector P = CellPt(UV.X, UV.Y);
		const FLinearColor RC = MinResInfo(Pool.Res).Color;
		M.Disc(P, Min::CellSize * 0.32f, Darken(RC, 0.45f), 14);
		M.Disc(P, Min::CellSize * 0.25f, RC.ToFColor(true), 14);
		// surplus load waiting for a depot run
		if (Pool.bSurplusReady)
			M.Disc(P + FVector(0, 0, Min::CellSize * 0.42f), Min::CellSize * 0.09f,
				FColor(240, 230, 205, 255), 8);
	}

	// plants: bigger colored squares with pins and an overflow ring
	for (const FMinPlant& Plant : Sim->Plants)
	{
		if (!Plant.bAlive) continue;
		FIntPoint UV;
		if (!Min::CellToPlaneUV(Plant.Cell, Plane, UV)) continue;
		const FVector P = CellPt(UV.X, UV.Y);
		const FLinearColor RC = MinResInfo(Plant.Res).Color;
		M.Box(P, Min::CellSize * 0.60f, Darken(RC, 0.42f));
		M.Box(P, Min::CellSize * 0.51f, RC.ToFColor(true));
		M.Box(P, Min::CellSize * 0.18f, Darken(RC, 0.55f));

		// demand pins march across the top
		const float PinStep = Min::CellSize * 0.17f;
		const float PinY = Min::CellSize * 0.78f;
		for (int32 I = 0; I < Plant.Pins; ++I)
		{
			const int32 Row = I / 6, Col = I % 6;
			const FVector PinP = P + FVector((Col - 2.5f) * PinStep, 0.f, PinY + Row * PinStep);
			M.Box(PinP, Min::CellSize * 0.055f,
				Plant.Pins >= UMinSim::PinCap ? FColor(235, 64, 52, 255) : FColor(245, 238, 220, 255));
		}

		// fail countdown ring
		if (Plant.bOverflow || Plant.OverflowT < UMinSim::OverflowMax - 0.5f)
		{
			const float Frac = FMath::Clamp(Plant.OverflowT / UMinSim::OverflowMax, 0.f, 1.f);
			M.Arc(P, Min::CellSize * 0.72f, Min::CellSize * 0.82f, 1.f, FColor(60, 52, 48, 160), 28);
			M.Arc(P, Min::CellSize * 0.72f, Min::CellSize * 0.82f, Frac,
				Plant.bOverflow ? FColor(235, 64, 52, 255) : FColor(240, 230, 205, 220), 28);
		}
	}

	for (int32 I = 0; I < M.V.Num(); ++I) M.V[I].Y = 0.f;
	MarkerMesh->CreateMeshSection(0, M.V, M.T, M.N, M.UV, M.C, TArray<FProcMeshTangent>(), false);
}

void AMinPlaneActor::UpdateCarts(UMinSim* Sim)
{
	int32 Used = 0;
	for (const FMinCart& Cart : Sim->Carts)
	{
		if (!Cart.bAlive || Cart.Plane != Plane) continue;
		if (Used >= CartPool.Num())
		{
			// a plain tintable quad: flat colored cart
			UStaticMeshComponent* Q = NewObject<UStaticMeshComponent>(this);
			Q->SetStaticMesh(Assets->GetQuad());
			Q->SetMaterial(0, Assets->MakeTexMID(Q, nullptr, FLinearColor::White));
			Q->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Q->SetCastShadow(false);
			Q->SetTranslucentSortPriority(5);
			Q->RegisterComponent();
			Q->SetWorldScale3D(FVector(Min::CellSize * 0.0042f, 1.f, Min::CellSize * 0.0042f));
			if (!Min::IsFront(Plane)) Q->SetWorldRotation(FRotator(0.f, 90.f, 0.f));
			CartPool.Add(Q);
		}
		UStaticMeshComponent* Q = CartPool[Used];
		FVector Loc = Cart.WorldPos;
		if (Min::IsFront(Plane)) Loc.Y = Min::FrontPlaneY(Plane) + Min::DepthCart;
		else Loc.X = Min::SidePlaneX(Plane - Min::NumFront) - Min::DepthCart;
		Q->SetWorldLocation(Loc);
		Q->SetVisibility(ContentOpacityCur > 0.01f);
		if (UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(Q->GetMaterial(0)))
		{
			FLinearColor Col = MinResInfo(Cart.Res).Color;
			if (Cart.bReturning) Col *= 0.55f;
			Mat->SetVectorParameterValue(TEXT("Tint"), Col);
			Mat->SetScalarParameterValue(TEXT("Opacity"), ContentOpacityCur);
		}
		++Used;
	}
	for (int32 I = Used; I < CartPool.Num(); ++I)
		if (CartPool[I]) CartPool[I]->SetVisibility(false);
}

void AMinPlaneActor::ShowPreview(const TArray<FIntPoint>& Cells, bool bValid, bool bErase)
{
	FMeshAccum M;
	const FColor Col = bErase ? FColor(230, 70, 60, 150)
		: (bValid ? FColor(90, 210, 120, 150) : FColor(230, 70, 60, 150));
	const FColor Edge = bErase ? FColor(255, 120, 105, 230)
		: (bValid ? FColor(150, 245, 175, 230) : FColor(255, 120, 105, 230));
	TArray<FVector> Pts;
	for (const FIntPoint& C : Cells) Pts.Add(CellPt(C.X, C.Y));

	// grid-aligned cell highlight: soft fill plus a brighter outline
	auto CellHighlight = [&](const FVector& Center)
	{
		const float H = Min::CellSize * 0.5f - 4.f;
		const float B = 10.f; // outline thickness
		M.Quad(Center + FVector(-H, 0, -H), Center + FVector(H, 0, -H),
			Center + FVector(H, 0, H), Center + FVector(-H, 0, H), Col);
		M.Quad(Center + FVector(-H, 0, H - B), Center + FVector(H, 0, H - B),
			Center + FVector(H, 0, H), Center + FVector(-H, 0, H), Edge);
		M.Quad(Center + FVector(-H, 0, -H), Center + FVector(H, 0, -H),
			Center + FVector(H, 0, -H + B), Center + FVector(-H, 0, -H + B), Edge);
		M.Quad(Center + FVector(-H, 0, -H), Center + FVector(-H + B, 0, -H),
			Center + FVector(-H + B, 0, H), Center + FVector(-H, 0, H), Edge);
		M.Quad(Center + FVector(H - B, 0, -H), Center + FVector(H, 0, -H),
			Center + FVector(H, 0, H), Center + FVector(H - B, 0, H), Edge);
	};

	if (Pts.Num() == 1) CellHighlight(Pts[0]);
	else if (Pts.Num() > 1)
	{
		for (int32 I = 0; I + 1 < Pts.Num(); ++I)
			M.Strip(Pts[I], Pts[I + 1], Min::CellSize * 0.11f, Col);
		for (const FVector& P : Pts) M.Disc(P, Min::CellSize * 0.11f, Col, 8);
		CellHighlight(Pts.Last());
	}
	GhostRouteMesh->CreateMeshSection(0, M.V, M.T, M.N, M.UV, M.C, TArray<FProcMeshTangent>(), false);
	GhostRouteMesh->SetVisibility(true);
}

void AMinPlaneActor::ClearPreview()
{
	GhostRouteMesh->ClearMeshSection(0);
}

void AMinPlaneActor::SetViewOpacity(float TerrainOpacity, float ContentOpacity)
{
	ContentOpacityCur = ContentOpacity;
	if (TerrainMID) TerrainMID->SetScalarParameterValue(TEXT("Opacity"), TerrainOpacity);
	if (BackMID) BackMID->SetScalarParameterValue(TEXT("Opacity"), TerrainOpacity);
	TerrainQuad->SetVisibility(TerrainOpacity > 0.01f);
	BackQuad->SetVisibility(TerrainOpacity > 0.01f);

	const bool bContent = ContentOpacity > 0.01f;
	if (RouteMID) RouteMID->SetScalarParameterValue(TEXT("Opacity"), ContentOpacity);
	if (MarkerMID) MarkerMID->SetScalarParameterValue(TEXT("Opacity"), ContentOpacity);
	RouteMesh->SetVisibility(bContent);
	MarkerMesh->SetVisibility(bContent);
	if (DepotSprite)
	{
		DepotSprite->SetVisibility(bContent);
		if (UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(DepotSprite->GetMaterial(0)))
			Mat->SetScalarParameterValue(TEXT("Opacity"), ContentOpacity);
	}
}
