#include "MinAssets.h"
#include "Min3D.h"
#include "ImageUtils.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UMinAssets::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SpriteDir = FPaths::ProjectContentDir() / TEXT("SpriteSrc");
	SpriteMat = LoadObject<UMaterial>(nullptr, TEXT("/Game/Gen/M_MinSprite.M_MinSprite"));
	VertexMat = LoadObject<UMaterial>(nullptr, TEXT("/Game/Gen/M_MinVertex.M_MinVertex"));
	if (!SpriteMat || !VertexMat)
	{
		UE_LOG(LogMin, Error, TEXT("Parent materials missing - run Tools/bootstrap_content.py"));
	}
	LoadManifest();
}

void UMinAssets::LoadManifest()
{
	FString Json;
	const FString Path = SpriteDir / TEXT("sprites.json");
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogMin, Error, TEXT("Missing sprite manifest %s"), *Path);
		return;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		UE_LOG(LogMin, Error, TEXT("Bad sprite manifest"));
		return;
	}
	for (const auto& SheetPair : Root->Values)
	{
		const TSharedPtr<FJsonObject> Sheet = SheetPair.Value->AsObject();
		if (!Sheet.IsValid()) continue;
		const TArray<TSharedPtr<FJsonValue>>* Sprites;
		if (!Sheet->TryGetArrayField(TEXT("sprites"), Sprites)) continue;
		for (const TSharedPtr<FJsonValue>& V : *Sprites)
		{
			const TSharedPtr<FJsonObject> S = V->AsObject();
			if (!S.IsValid()) continue;
			SpriteSizes.Add(S->GetStringField(TEXT("id")),
				FVector2D(S->GetNumberField(TEXT("w")), S->GetNumberField(TEXT("h"))));
		}
	}
	UE_LOG(LogMin, Log, TEXT("Sprite manifest: %d sprites"), SpriteSizes.Num());
}

UTexture2D* UMinAssets::GetSprite(const FString& Id)
{
	if (TObjectPtr<UTexture2D>* Found = SpriteCache.Find(Id)) return *Found;
	const FString Path = SpriteDir / (Id + TEXT(".png"));
	UTexture2D* Tex = FImageUtils::ImportFileAsTexture2D(Path);
	if (!Tex)
	{
		UE_LOG(LogMin, Warning, TEXT("Sprite not found: %s"), *Path);
		return nullptr;
	}
	Tex->Filter = TF_Bilinear;
	SpriteCache.Add(Id, Tex);
	return Tex;
}

FVector2D UMinAssets::GetSpriteSize(const FString& Id) const
{
	if (const FVector2D* S = SpriteSizes.Find(Id)) return *S;
	return FVector2D(64, 64);
}

UStaticMesh* UMinAssets::GetQuad()
{
	if (QuadMesh) return QuadMesh;

	FMeshDescription Desc;
	FStaticMeshAttributes Attr(Desc);
	Attr.Register();
	FPolygonGroupID Group = Desc.CreatePolygonGroup();
	Attr.GetPolygonGroupMaterialSlotNames()[Group] = FName(TEXT("Mat"));

	// quad in XZ plane, facing -Y (toward the plane-view camera)
	const float H = 50.f;
	const FVector3f P[4] = { {-H, 0, -H}, {H, 0, -H}, {H, 0, H}, {-H, 0, H} };
	const FVector2f UV[4] = { {0, 1}, {1, 1}, {1, 0}, {0, 0} };
	const int32 TriIdx[2][3] = { {0, 2, 1}, {0, 3, 2} }; // CCW seen from -Y

	for (int32 T = 0; T < 2; ++T)
	{
		FVertexInstanceID VI[3];
		for (int32 I = 0; I < 3; ++I)
		{
			const int32 C = TriIdx[T][I];
			const FVertexID V = Desc.CreateVertex();
			Attr.GetVertexPositions()[V] = P[C];
			VI[I] = Desc.CreateVertexInstance(V);
			Attr.GetVertexInstanceNormals()[VI[I]] = FVector3f(0, -1, 0);
			Attr.GetVertexInstanceTangents()[VI[I]] = FVector3f(1, 0, 0);
			Attr.GetVertexInstanceBinormalSigns()[VI[I]] = 1.f;
			Attr.GetVertexInstanceUVs()[VI[I]] = UV[C];
		}
		Desc.CreatePolygon(Group, TArray<FVertexInstanceID>({ VI[0], VI[1], VI[2] }));
	}

	UStaticMesh* Mesh = NewObject<UStaticMesh>(this, TEXT("MinQuad"));
	FStaticMaterial SlotMat(UMaterial::GetDefaultMaterial(MD_Surface), FName(TEXT("Mat")));
#if WITH_EDITORONLY_DATA
	SlotMat.ImportedMaterialSlotName = FName(TEXT("Mat"));
#endif
	Mesh->GetStaticMaterials().Add(SlotMat);
	Mesh->bAllowCPUAccess = true;
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;
	Params.bAllowCpuAccess = true;
	Params.bCommitMeshDescription = false;
	Mesh->BuildFromMeshDescriptions(TArray<const FMeshDescription*>({ &Desc }), Params);
	QuadMesh = Mesh;
	return QuadMesh;
}

UMaterialInstanceDynamic* UMinAssets::MakeTexMID(UObject* Outer, UTexture2D* Tex,
	FLinearColor Tint, float Opacity)
{
	if (!SpriteMat) return nullptr;
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(SpriteMat, Outer);
	if (Tex) MID->SetTextureParameterValue(TEXT("Tex"), Tex);
	MID->SetVectorParameterValue(TEXT("Tint"), Tint);
	MID->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	return MID;
}

UMaterialInstanceDynamic* UMinAssets::MakeSpriteMID(UObject* Outer, const FString& SpriteId,
	FLinearColor Tint, float Opacity)
{
	return MakeTexMID(Outer, GetSprite(SpriteId), Tint, Opacity);
}

UStaticMeshComponent* UMinAssets::MakeSpriteComponent(AActor* Owner, const FString& SpriteId,
	float SizeUU, FLinearColor Tint, float Opacity, int32 SortPriority)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
	C->SetStaticMesh(GetQuad());
	C->SetMaterial(0, MakeSpriteMID(C, SpriteId, Tint, Opacity));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->SetTranslucentSortPriority(SortPriority);
	const FVector2D Src = GetSpriteSize(SpriteId);
	const float Aspect = Src.Y > 0 ? float(Src.X / Src.Y) : 1.f;
	C->SetWorldScale3D(FVector(SizeUU * Aspect / 100.f, 1.f, SizeUU / 100.f));
	C->RegisterComponent();
	return C;
}
