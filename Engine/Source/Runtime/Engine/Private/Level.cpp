// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Level.cpp: Level-related functions
=============================================================================*/

#include "EnginePrivate.h"
#include "Engine/AssetUserData.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/WorldComposition.h"
#include "Net/UnrealNetwork.h"
#include "Model.h"
#include "StaticLighting.h"
#include "SoundDefinitions.h"
#include "PrecomputedLightVolume.h"
#include "TickTaskManagerInterface.h"
#include "BlueprintUtilities.h"
#include "DynamicMeshBuilder.h"
#include "Engine/LevelBounds.h"
#if WITH_EDITOR
#include "Editor/UnrealEd/Public/Kismet2/KismetEditorUtilities.h"
#include "Editor/UnrealEd/Public/Kismet2/BlueprintEditorUtils.h"
#endif
#include "Runtime/Engine/Classes/MovieScene/RuntimeMovieScenePlayerInterface.h"
#include "LevelUtils.h"
#include "TargetPlatform.h"
#include "ContentStreaming.h"
#include "Foliage/InstancedFoliageActor.h"
#include "Engine/NavigationObjectBase.h"
#include "Engine/ShadowMapTexture2D.h"
#include "Components/ModelComponent.h"
#include "Engine/LightMapTexture2D.h"
DEFINE_LOG_CATEGORY(LogLevel);

/*-----------------------------------------------------------------------------
ULevel implementation.
-----------------------------------------------------------------------------*/


/** Called when a level package has been dirtied. */
FSimpleMulticastDelegate ULevel::LevelDirtiedEvent;

int32 FPrecomputedVisibilityHandler::NextId = 0;

/** Updates visibility stats. */
void FPrecomputedVisibilityHandler::UpdateVisibilityStats(bool bAllocating) const
{
	if (bAllocating)
	{
		INC_DWORD_STAT_BY(STAT_PrecomputedVisibilityMemory, PrecomputedVisibilityCellBuckets.GetAllocatedSize());
		for (int32 BucketIndex = 0; BucketIndex < PrecomputedVisibilityCellBuckets.Num(); BucketIndex++)
		{
			INC_DWORD_STAT_BY(STAT_PrecomputedVisibilityMemory, PrecomputedVisibilityCellBuckets[BucketIndex].Cells.GetAllocatedSize());
			INC_DWORD_STAT_BY(STAT_PrecomputedVisibilityMemory, PrecomputedVisibilityCellBuckets[BucketIndex].CellDataChunks.GetAllocatedSize());
			for (int32 ChunkIndex = 0; ChunkIndex < PrecomputedVisibilityCellBuckets[BucketIndex].CellDataChunks.Num(); ChunkIndex++)
			{
				INC_DWORD_STAT_BY(STAT_PrecomputedVisibilityMemory, PrecomputedVisibilityCellBuckets[BucketIndex].CellDataChunks[ChunkIndex].Data.GetAllocatedSize());
			}
		}
	}
	else
	{
		DEC_DWORD_STAT_BY(STAT_PrecomputedVisibilityMemory, PrecomputedVisibilityCellBuckets.GetAllocatedSize());
		for (int32 BucketIndex = 0; BucketIndex < PrecomputedVisibilityCellBuckets.Num(); BucketIndex++)
		{
			DEC_DWORD_STAT_BY(STAT_PrecomputedVisibilityMemory, PrecomputedVisibilityCellBuckets[BucketIndex].Cells.GetAllocatedSize());
			DEC_DWORD_STAT_BY(STAT_PrecomputedVisibilityMemory, PrecomputedVisibilityCellBuckets[BucketIndex].CellDataChunks.GetAllocatedSize());
			for (int32 ChunkIndex = 0; ChunkIndex < PrecomputedVisibilityCellBuckets[BucketIndex].CellDataChunks.Num(); ChunkIndex++)
			{
				DEC_DWORD_STAT_BY(STAT_PrecomputedVisibilityMemory, PrecomputedVisibilityCellBuckets[BucketIndex].CellDataChunks[ChunkIndex].Data.GetAllocatedSize());
			}
		}
	}
}

/** Sets this visibility handler to be actively used by the rendering scene. */
void FPrecomputedVisibilityHandler::UpdateScene(FSceneInterface* Scene) const
{
	if (Scene && PrecomputedVisibilityCellBuckets.Num() > 0)
	{
		Scene->SetPrecomputedVisibility(this);
	}
}

/** Invalidates the level's precomputed visibility and frees any memory used by the handler. */
void FPrecomputedVisibilityHandler::Invalidate(FSceneInterface* Scene)
{
	Scene->SetPrecomputedVisibility(NULL);
	// Block until the renderer no longer references this FPrecomputedVisibilityHandler so we can delete its data
	FlushRenderingCommands();
	UpdateVisibilityStats(false);
	PrecomputedVisibilityCellBucketOriginXY = FVector2D(0,0);
	PrecomputedVisibilityCellSizeXY = 0;
	PrecomputedVisibilityCellSizeZ = 0;
	PrecomputedVisibilityCellBucketSizeXY = 0;
	PrecomputedVisibilityNumCellBuckets = 0;
	PrecomputedVisibilityCellBuckets.Empty();
	// Bump the Id so FSceneViewState will know to discard its cached visibility data
	Id = NextId;
	NextId++;
}

void FPrecomputedVisibilityHandler::ApplyWorldOffset(const FVector& InOffset)
{
	PrecomputedVisibilityCellBucketOriginXY-= FVector2D(InOffset.X, InOffset.Y);
	for (FPrecomputedVisibilityBucket& Bucket : PrecomputedVisibilityCellBuckets)
	{
		for (FPrecomputedVisibilityCell& Cell : Bucket.Cells)
		{
			Cell.Min+= InOffset;
		}
	}
}

FArchive& operator<<( FArchive& Ar, FPrecomputedVisibilityHandler& D )
{
	Ar << D.PrecomputedVisibilityCellBucketOriginXY;
	Ar << D.PrecomputedVisibilityCellSizeXY;
	Ar << D.PrecomputedVisibilityCellSizeZ;
	Ar << D.PrecomputedVisibilityCellBucketSizeXY;
	Ar << D.PrecomputedVisibilityNumCellBuckets;
	Ar << D.PrecomputedVisibilityCellBuckets;
	if (Ar.IsLoading())
	{
		D.UpdateVisibilityStats(true);
	}
	return Ar;
}


/** Sets this volume distance field to be actively used by the rendering scene. */
void FPrecomputedVolumeDistanceField::UpdateScene(FSceneInterface* Scene) const
{
	if (Scene && Data.Num() > 0)
	{
		Scene->SetPrecomputedVolumeDistanceField(this);
	}
}

/** Invalidates the level's volume distance field and frees any memory used by it. */
void FPrecomputedVolumeDistanceField::Invalidate(FSceneInterface* Scene)
{
	if (Scene && Data.Num() > 0)
	{
		Scene->SetPrecomputedVolumeDistanceField(NULL);
		// Block until the renderer no longer references this FPrecomputedVolumeDistanceField so we can delete its data
		FlushRenderingCommands();
		Data.Empty();
	}
}

FArchive& operator<<( FArchive& Ar, FPrecomputedVolumeDistanceField& D )
{
	Ar << D.VolumeMaxDistance;
	Ar << D.VolumeBox;
	Ar << D.VolumeSizeX;
	Ar << D.VolumeSizeY;
	Ar << D.VolumeSizeZ;
	Ar << D.Data;

	return Ar;
}

FLevelSimplificationDetails::FLevelSimplificationDetails()
 : DetailsPercentage(70.f)
 , LandscapeExportLOD(7)
 , bGenerateLandscapeNormalMap(true)
 , bGenerateLandscapeMetallicMap(false)
 , bGenerateLandscapeRoughnessMap(false)
 , bGenerateLandscapeSpecularMap(false)
 , bBakeFoliageToLandscape(false)
{
}

bool FLevelSimplificationDetails::operator == (const FLevelSimplificationDetails& Other) const
{
	return
		DetailsPercentage == Other.DetailsPercentage &&
		bCreatePackagePerAsset == Other.bCreatePackagePerAsset &&
		LandscapeExportLOD == Other.LandscapeExportLOD &&
		bGenerateLandscapeNormalMap == Other.bGenerateLandscapeNormalMap &&
		bGenerateLandscapeMetallicMap == Other.bGenerateLandscapeMetallicMap &&
		bGenerateLandscapeRoughnessMap == Other.bGenerateLandscapeRoughnessMap &&
		bGenerateLandscapeSpecularMap == Other.bGenerateLandscapeSpecularMap &&
		bBakeFoliageToLandscape == Other.bBakeFoliageToLandscape;
}

TMap<FName, UWorld*> ULevel::StreamedLevelsOwningWorld;

ULevel::ULevel( const FObjectInitializer& ObjectInitializer )
	:	UObject( ObjectInitializer )
	,	Actors(this)
	,	OwningWorld(NULL)
	,	TickTaskLevel(FTickTaskManagerInterface::Get().AllocateTickTaskLevel())
	,	PrecomputedLightVolume(new FPrecomputedLightVolume())
{
}

void ULevel::Initialize(const FURL& InURL)
{
	URL = InURL;
}

ULevel::~ULevel()
{
	FTickTaskManagerInterface::Get().FreeTickTaskLevel(TickTaskLevel);
	TickTaskLevel = NULL;
}


void ULevel::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	ULevel* This = CastChecked<ULevel>(InThis);

	// Let GC know that we're referencing some UTexture2D objects
	for( auto& It : This->TextureToInstancesMap )
	{
		UTexture2D* Texture2D = It.Key;
		Collector.AddReferencedObject( Texture2D, This );
	}

	// Let GC know that we're referencing some UTexture2D objects
	for( auto& It : This->DynamicTextureInstances )
	{
		UPrimitiveComponent* Primitive = It.Key.Get();
		const TArray<FDynamicTextureInstance>& TextureInstances = It.Value;

		for ( FDynamicTextureInstance& Instance : It.Value )
		{
			Collector.AddReferencedObject( Instance.Texture, This );
		}
	}

	// Let GC know that we're referencing some UTexture2D objects
	for( auto& It : This->ForceStreamTextures )
	{
		UTexture2D* Texture2D = It.Key;
		Collector.AddReferencedObject( Texture2D, This );
	}

	// Let GC know that we're referencing some AActor objects
	for (auto& Actor : This->Actors)
	{
		Collector.AddReferencedObject(Actor, This);
	}
	UObject* ActorsOwner = This->Actors.GetOwner();
	Collector.AddReferencedObject(ActorsOwner, This);

	Super::AddReferencedObjects( This, Collector );
}

// Compatibility classes
struct FOldGuidPair
{
public:
	FGuid	Guid;
	uint32	RefId;

	friend FArchive& operator<<( FArchive& Ar, FOldGuidPair& GP )
	{
		Ar << GP.Guid << GP.RefId;
		return Ar;
	}
};

struct FLegacyCoverIndexPair
{
	TLazyObjectPtr<class ACoverLink> CoverRef;
	uint8	SlotIdx;

	friend FArchive& operator<<( FArchive& Ar, struct FLegacyCoverIndexPair& IP )
	{
		Ar << IP.CoverRef << IP.SlotIdx;
		return Ar;
	}
};

void ULevel::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );

	Ar << Actors;
	Ar << URL;

	Ar << Model;

	Ar << ModelComponents;

	if(!Ar.IsFilterEditorOnly() || (Ar.UE4Ver() < VER_UE4_EDITORONLY_BLUEPRINTS) )
	{
#if WITH_EDITORONLY_DATA
		// Skip serializing the LSBP if this is a world duplication for PIE/SIE, as it is not needed, and it causes overhead in startup times
		if( (Ar.GetPortFlags() & PPF_DuplicateForPIE ) == 0 )
		{
			Ar << LevelScriptBlueprint;
		}
		else
#endif //WITH_EDITORONLY_DATA
		{
			UObject* DummyBP = NULL;
			Ar << DummyBP;
		}
	}

	if( !Ar.IsTransacting() )
	{
		Ar << LevelScriptActor;

		if( ( Ar.IsLoading() && !FPlatformProperties::SupportsTextureStreaming() ) ||
			( Ar.IsCooking() && !Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::TextureStreaming) ) )
		{
			// Strip for unsupported platforms
			TMap< UTexture2D*, TArray< FStreamableTextureInstance > >		Dummy0;
			TMap< UPrimitiveComponent*, TArray< FDynamicTextureInstance > >	Dummy1;
			Ar << Dummy0;
			Ar << Dummy1;
		}
		else
		{
			Ar << TextureToInstancesMap;
			Ar << DynamicTextureInstances;
		}

		bool bIsCooked = Ar.IsCooking();
		if (Ar.UE4Ver() >= VER_UE4_REBUILD_TEXTURE_STREAMING_DATA_ON_LOAD)
		{
			Ar << bIsCooked;
		}
		if (Ar.UE4Ver() < VER_UE4_REBUILD_TEXTURE_STREAMING_DATA_ON_LOAD)
		{
			bool bTextureStreamingBuiltDummy = bIsCooked;
			Ar << bTextureStreamingBuiltDummy;
		}
		if (Ar.IsLoading())
		{
			// Always rebuild texture streaming data after loading
			bTextureStreamingBuilt = false;
		}

		//@todo legacy, useless
		if (Ar.IsLoading())
		{
			uint32 Size;
			Ar << Size;
			Ar.Seek(Ar.Tell() + Size);
		}
		else if (Ar.IsSaving())
		{
			uint32 Len = 0;
			Ar << Len;
		}

		if(Ar.UE4Ver() < VER_UE4_REMOVE_LEVELBODYSETUP)
		{
			UBodySetup* DummySetup;
			Ar << DummySetup;
		}

		if( ( Ar.IsLoading() && !FPlatformProperties::SupportsTextureStreaming() ) ||
			( Ar.IsCooking() && !Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::TextureStreaming) ) )
		{
			TMap< UTexture2D*, bool > Dummy;
			Ar << Dummy;
		}
		else
		{
			Ar << ForceStreamTextures;
		}
	}

	// Mark archive and package as containing a map if we're serializing to disk.
	if( !HasAnyFlags( RF_ClassDefaultObject ) && Ar.IsPersistent() )
	{
		Ar.ThisContainsMap();
		GetOutermost()->ThisContainsMap();
	}

	// serialize the nav list
	Ar << NavListStart;
	Ar << NavListEnd;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FPrecomputedLightVolume DummyVolume;
		Ar << DummyVolume;
	}
	else
	{
		Ar << *PrecomputedLightVolume;
	}

	Ar << PrecomputedVisibilityHandler;
	Ar << PrecomputedVolumeDistanceField;

	if (Ar.UE4Ver() >= VER_UE4_WORLD_LEVEL_INFO &&
		Ar.UE4Ver() < VER_UE4_WORLD_LEVEL_INFO_UPDATED)
	{
		FWorldTileInfo Info;
		Ar << Info;
	}
}


void ULevel::SortActorList()
{
	if (Actors.Num() == 0)
	{
		// No need to sort an empty list
		return;
	}

	int32 StartIndex = 0;
	TArray<AActor*> NewActors;
	NewActors.Reserve(Actors.Num());

	// The WorldSettings has fixed actor index.
	check(Actors[StartIndex] == GetWorldSettings());
	NewActors.Add(Actors[StartIndex++]);

	// Static not net relevant actors.
	for (int32 ActorIndex = StartIndex; ActorIndex < Actors.Num(); ActorIndex++)
	{
		AActor* Actor = Actors[ActorIndex];
		if (Actor != NULL && !Actor->IsPendingKill() && Actor->GetRemoteRole() == ROLE_None)
		{
			NewActors.Add(Actor);
		}
	}
	iFirstNetRelevantActor = NewActors.Num();

	// Static net relevant actors.
	for (int32 ActorIndex = StartIndex; ActorIndex < Actors.Num(); ActorIndex++)
	{
		AActor* Actor = Actors[ActorIndex];		
		if (Actor != NULL && !Actor->IsPendingKill() && Actor->GetRemoteRole() > ROLE_None)
		{
			NewActors.Add(Actor);
		}
	}

	// Replace with sorted list.
	Actors.AssignButKeepOwner(NewActors);

	// Don't use sorted optimization outside of gameplay so we can safely shuffle around actors e.g. in the Editor
	// without there being a chance to break code using dynamic/ net relevant actor iterators.
	if (!OwningWorld->IsGameWorld())
	{
		iFirstNetRelevantActor = 0;
	}

	// Add all network actors to the owning world
	if ( OwningWorld != NULL )
	{
		for ( int32 i = iFirstNetRelevantActor; i < Actors.Num(); i++ )
		{
			if ( Actors[ i ] != NULL )
			{
				OwningWorld->AddNetworkActor( Actors[ i ] );
			}
		}
	}
}


void ULevel::ValidateLightGUIDs()
{
	for( TObjectIterator<ULightComponent> It; It; ++It )
	{
		ULightComponent*	LightComponent	= *It;
		bool				IsInLevel		= LightComponent->IsIn( this );

		if( IsInLevel )
		{
			LightComponent->ValidateLightGUIDs();
		}
	}
}


void ULevel::PreSave()
{
	Super::PreSave();

#if WITH_EDITOR
	if( !IsTemplate() )
	{
		UPackage* Package = CastChecked<UPackage>(GetOutermost());

		ValidateLightGUIDs();

		// Clear out any crosslevel references
		for( int32 ActorIdx = 0; ActorIdx < Actors.Num(); ActorIdx++ )
		{
			AActor *Actor = Actors[ActorIdx];
			if( Actor != NULL )
			{
				Actor->ClearCrossLevelReferences();
			}
		}
	}
#endif // WITH_EDITOR
}


void ULevel::PostLoad()
{
	Super::PostLoad();

	// Ensure that the level is pointed to the owning world.  For streamed levels, this will be the world of the P map
	// they are streamed in to which we cached when the package loading was invoked
	OwningWorld = ULevel::StreamedLevelsOwningWorld.FindRef(GetOutermost()->GetFName());
	if (OwningWorld == NULL)
	{
		OwningWorld = CastChecked<UWorld>(GetOuter());
	}
	else
	{
		// This entry will not be used anymore, remove it
		ULevel::StreamedLevelsOwningWorld.Remove(GetOutermost()->GetFName());
	}

	UWorldComposition::OnLevelPostLoad(this);
		
#if WITH_EDITOR
	Actors.Remove(nullptr);
#endif

	// in the Editor, sort Actor list immediately (at runtime we wait for the level to be added to the world so that it can be delayed in the level streaming case)
	if (GIsEditor)
	{
		SortActorList();
	}

	// Remove UTexture2D references that are NULL (missing texture).
	ForceStreamTextures.Remove( NULL );

	// Validate navigable geometry
	if (Model == NULL || Model->NumUniqueVertices == 0)
	{
		StaticNavigableGeometry.Empty();
	}

#if WITH_EDITOR
	if (!(GetOutermost()->PackageFlags & PKG_PlayInEditor))
	{
		// Rename the LevelScriptBlueprint after the outer world.
		UWorld* OuterWorld = Cast<UWorld>(GetOuter());
		if (LevelScriptBlueprint && OuterWorld && LevelScriptBlueprint->GetFName() != OuterWorld->GetFName())
		{
			// Use LevelScriptBlueprint->GetOuter() instead of NULL to make sure the generated top level objects are moved appropriately
			LevelScriptBlueprint->Rename(*OuterWorld->GetName(), LevelScriptBlueprint->GetOuter(), REN_DoNotDirty | REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional | REN_SkipGeneratedClasses);
		}
	}
#endif
}

void ULevel::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	bWasDuplicatedForPIE = bDuplicateForPIE;
}

UWorld* ULevel::GetWorld() const
{
	return OwningWorld;
}

void ULevel::ClearLevelComponents()
{
	bAreComponentsCurrentlyRegistered = false;

	// Remove the model components from the scene.
	for (UModelComponent* ModelComponent : ModelComponents)
	{
		if (ModelComponent && ModelComponent->IsRegistered())
		{
			ModelComponent->UnregisterComponent();
		}
	}

	// Remove the actors' components from the scene and build a list of relevant worlds
	for( AActor* Actor : Actors )
	{
		if (Actor)
		{
			Actor->UnregisterAllComponents();
		}
	}

	if (IsPersistentLevel())
	{
		FSceneInterface* WorldScene = GetWorld()->Scene;
		if (WorldScene)
		{
			WorldScene->SetClearMotionBlurInfoGameThread();
		}
	}
}

void ULevel::BeginDestroy()
{
	if (!IStreamingManager::HasShutdown())
	{
		// At this time, referenced UTexture2Ds are still in memory.
		IStreamingManager::Get().RemoveLevel( this );
	}

	Super::BeginDestroy();

	if (OwningWorld && IsPersistentLevel() && OwningWorld->Scene)
	{
		OwningWorld->Scene->SetPrecomputedVisibility(NULL);
		OwningWorld->Scene->SetPrecomputedVolumeDistanceField(NULL);

		RemoveFromSceneFence.BeginFence();
	}
}

bool ULevel::IsReadyForFinishDestroy()
{
	const bool bReady = Super::IsReadyForFinishDestroy();
	return bReady && RemoveFromSceneFence.IsFenceComplete();
}

void ULevel::FinishDestroy()
{
	ReleaseRenderingResources();

	delete PrecomputedLightVolume;
	PrecomputedLightVolume = NULL;

	Super::FinishDestroy();
}

/**
* A TMap key type used to sort BSP nodes by locality and zone.
*/
struct FModelComponentKey
{
	uint32	X;
	uint32	Y;
	uint32	Z;
	uint32	MaskedPolyFlags;

	friend bool operator==(const FModelComponentKey& A,const FModelComponentKey& B)
	{
		return	A.X == B.X 
			&&	A.Y == B.Y 
			&&	A.Z == B.Z 
			&&	A.MaskedPolyFlags == B.MaskedPolyFlags;
	}

	friend uint32 GetTypeHash(const FModelComponentKey& Key)
	{
		return FCrc::MemCrc_DEPRECATED(&Key,sizeof(Key));
	}
};

void ULevel::UpdateLevelComponents(bool bRerunConstructionScripts)
{
	// Update all components in one swoop.
	IncrementalUpdateComponents( 0, bRerunConstructionScripts );
}

/**
*	Sorts actors such that parent actors will appear before children actors in the list
*	Stable sort
*/
static void SortActorsHierarchy(TTransArray<AActor*>& Actors)
{
	auto CalcAttachDepth = [](AActor* InActor) -> int32 {
		int32 Depth = MAX_int32;
		if (InActor)
		{
			Depth = 0;
			if (InActor->GetRootComponent())
			{
				for (const USceneComponent* Test = InActor->GetRootComponent()->AttachParent; Test != nullptr; Test = Test->AttachParent, Depth++);
			}
		}
		return Depth;
	};
	
	// Unfortunately TArray.StableSort assumes no null entries in the array
	// So it forces me to use internal unrestricted version
	StableSortInternal(Actors.GetData(), Actors.Num(), [&](AActor* L, AActor* R) {
			return CalcAttachDepth(L) < CalcAttachDepth(R);
	});
}

void ULevel::IncrementalUpdateComponents(int32 NumComponentsToUpdate, bool bRerunConstructionScripts)
{
	// A value of 0 means that we want to update all components.
	if (NumComponentsToUpdate != 0)
	{
		// Only the game can use incremental update functionality.
		checkf(OwningWorld->IsGameWorld(), TEXT("Cannot call IncrementalUpdateComponents with non 0 argument in the Editor/ commandlets."));
	}

	// Do BSP on the first pass.
	if (CurrentActorIndexForUpdateComponents == 0)
	{
		UpdateModelComponents();
		// Sort actors to ensure that parent actors will be registered before child actors
		SortActorsHierarchy(Actors);
	}

	// Find next valid actor to process components registration
	while (CurrentActorIndexForUpdateComponents < Actors.Num())
	{
		AActor* Actor = Actors[CurrentActorIndexForUpdateComponents];
		bool bAllComponentsRegistered = true;
		if (Actor)
		{
			bAllComponentsRegistered = Actor->IncrementalRegisterComponents(NumComponentsToUpdate);
		}

		if (bAllComponentsRegistered)
		{	
			// All components have been registered fro this actor, move to a next one
			CurrentActorIndexForUpdateComponents++;
		}

		// If we do an incremental registration return to outer loop after each processed actor 
		// so outer loop can decide whether we want to continue processing this frame
		if (NumComponentsToUpdate != 0)
		{
			break;
		}
	}

	// See whether we are done.
	if (CurrentActorIndexForUpdateComponents == Actors.Num())
	{
		CurrentActorIndexForUpdateComponents	= 0;
		bAreComponentsCurrentlyRegistered		= true;
		
		if (bRerunConstructionScripts && !IsTemplate() && !GIsUCCMakeStandaloneHeaderGenerator)
		{
			// Don't rerun construction scripts until after all actors' components have been registered.  This
			// is necessary because child attachment lists are populated during registration, and running construction
			// scripts requires that the attachments are correctly initialized.
			for (AActor* Actor : Actors)
			{
				if (Actor)
				{
					Actor->RerunConstructionScripts();
				}
			}
		}
	}
	// Only the game can use incremental update functionality.
	else
	{
		// The editor is never allowed to incrementally updated components.  Make sure to pass in a value of zero for NumActorsToUpdate.
		check(OwningWorld->IsGameWorld());
	}
}

#if WITH_EDITOR

void ULevel::CreateModelComponents()
{
	// Update the model vertices and edges.
	Model->UpdateVertices();

	Model->InvalidSurfaces = 0;

	// Clear the model index buffers.
	Model->MaterialIndexBuffers.Empty();

	TMap< FModelComponentKey, TArray<uint16> > ModelComponentMap;

	// Sort the nodes by zone, grid cell and masked poly flags.
	for(int32 NodeIndex = 0;NodeIndex < Model->Nodes.Num();NodeIndex++)
	{
		FBspNode& Node = Model->Nodes[NodeIndex];
		FBspSurf& Surf = Model->Surfs[Node.iSurf];

		if(Node.NumVertices > 0)
		{
			for(int32 BackFace = 0;BackFace < ((Surf.PolyFlags & PF_TwoSided) ? 2 : 1);BackFace++)
			{
				// Calculate the bounding box of this node.
				FBox NodeBounds(0);
				for(int32 VertexIndex = 0;VertexIndex < Node.NumVertices;VertexIndex++)
				{
					NodeBounds += Model->Points[Model->Verts[Node.iVertPool + VertexIndex].pVertex];
				}

				// Create a sort key for this node using the grid cell containing the center of the node's bounding box.
#define MODEL_GRID_SIZE_XY	2048.0f
#define MODEL_GRID_SIZE_Z	4096.0f
				FModelComponentKey Key;
				check( OwningWorld );
				if (OwningWorld->GetWorldSettings()->bMinimizeBSPSections)
				{
					Key.X				= 0;
					Key.Y				= 0;
					Key.Z				= 0;
				}
				else
				{
					Key.X				= FMath::FloorToInt(NodeBounds.GetCenter().X / MODEL_GRID_SIZE_XY);
					Key.Y				= FMath::FloorToInt(NodeBounds.GetCenter().Y / MODEL_GRID_SIZE_XY);
					Key.Z				= FMath::FloorToInt(NodeBounds.GetCenter().Z / MODEL_GRID_SIZE_Z);
				}

				Key.MaskedPolyFlags = Surf.PolyFlags & PF_ModelComponentMask;

				// Find an existing node list for the grid cell.
				TArray<uint16>* ComponentNodes = ModelComponentMap.Find(Key);
				if(!ComponentNodes)
				{
					// This is the first node we found in this grid cell, create a new node list for the grid cell.
					ComponentNodes = &ModelComponentMap.Add(Key,TArray<uint16>());
				}

				// Add the node to the grid cell's node list.
				ComponentNodes->AddUnique(NodeIndex);
			}
		}
		else
		{
			// Put it in component 0 until a rebuild occurs.
			Node.ComponentIndex = 0;
		}
	}

	// Create a UModelComponent for each grid cell's node list.
	for(TMap< FModelComponentKey, TArray<uint16> >::TConstIterator It(ModelComponentMap);It;++It)
	{
		const FModelComponentKey&	Key		= It.Key();
		const TArray<uint16>&			Nodes	= It.Value();	

		for(int32 NodeIndex = 0;NodeIndex < Nodes.Num();NodeIndex++)
		{
			Model->Nodes[Nodes[NodeIndex]].ComponentIndex = ModelComponents.Num();							
			Model->Nodes[Nodes[NodeIndex]].ComponentNodeIndex = NodeIndex;
		}

		UModelComponent* ModelComponent = NewObject<UModelComponent>(this);
		ModelComponent->InitializeModelComponent(Model, ModelComponents.Num(), Key.MaskedPolyFlags, Nodes);
		ModelComponents.Add(ModelComponent);

		for(int32 NodeIndex = 0;NodeIndex < Nodes.Num();NodeIndex++)
		{
			Model->Nodes[Nodes[NodeIndex]].ComponentElementIndex = INDEX_NONE;

			const uint16								Node	 = Nodes[NodeIndex];
			const TIndirectArray<FModelElement>&	Elements = ModelComponent->GetElements();
			for( int32 ElementIndex=0; ElementIndex<Elements.Num(); ElementIndex++ )
			{
				if( Elements[ElementIndex].Nodes.Find( Node ) != INDEX_NONE )
				{
					Model->Nodes[Nodes[NodeIndex]].ComponentElementIndex = ElementIndex;
					break;
				}
			}
		}
	}

	// Clear old cached data in case we don't regenerate it below, e.g. after removing all BSP from a level.
	Model->NumIncompleteNodeGroups = 0;
	Model->CachedMappings.Empty();

	// Work only needed if we actually have BSP in the level.
	if( ModelComponents.Num() )
	{
		check( OwningWorld );
		// Build the static lighting vertices!
		/** The lights in the world which the system is building. */
		TArray<ULightComponentBase*> Lights;
		// Prepare lights for rebuild.
		for(TObjectIterator<ULightComponent> LightIt;LightIt;++LightIt)
		{
			ULightComponent* const Light = *LightIt;
			const bool bLightIsInWorld = Light->GetOwner() && OwningWorld->ContainsActor(Light->GetOwner()) && !Light->GetOwner()->IsPendingKill();
			if (bLightIsInWorld && (Light->HasStaticLighting() || Light->HasStaticShadowing()))
			{
				// Make sure the light GUIDs and volumes are up-to-date.
				Light->ValidateLightGUIDs();

				// Add the light to the system's list of lights in the world.
				Lights.Add(Light);
			}
		}

		// For BSP, we aren't Component-centric, so we can't use the GetStaticLightingInfo 
		// function effectively. Instead, we look across all nodes in the Level's model and
		// generate NodeGroups - which are groups of nodes that are coplanar, adjacent, and 
		// have the same lightmap resolution (henceforth known as being "conodes"). Each 
		// NodeGroup will get a mapping created for it

		// create all NodeGroups
		Model->GroupAllNodes(this, Lights);

		// now we need to make the mappings/meshes
		for (TMap<int32, FNodeGroup*>::TIterator It(Model->NodeGroups); It; ++It)
		{
			FNodeGroup* NodeGroup = It.Value();

			if (NodeGroup->Nodes.Num())
			{
				// get one of the surfaces/components from the NodeGroup
				// @todo UE4: Remove need for GetSurfaceLightMapResolution to take a surfaceindex, or a ModelComponent :)
				UModelComponent* SomeModelComponent = ModelComponents[Model->Nodes[NodeGroup->Nodes[0]].ComponentIndex];
				int32 SurfaceIndex = Model->Nodes[NodeGroup->Nodes[0]].iSurf;

				// fill out the NodeGroup/mapping, as UModelComponent::GetStaticLightingInfo did
				SomeModelComponent->GetSurfaceLightMapResolution(SurfaceIndex, true, NodeGroup->SizeX, NodeGroup->SizeY, NodeGroup->WorldToMap, &NodeGroup->Nodes);
				NodeGroup->MapToWorld = NodeGroup->WorldToMap.InverseFast();

				// Cache the surface's vertices and triangles.
				NodeGroup->BoundingBox.Init();

				for(int32 NodeIndex = 0;NodeIndex < NodeGroup->Nodes.Num();NodeIndex++)
				{
					const FBspNode& Node = Model->Nodes[NodeGroup->Nodes[NodeIndex]];
					const FBspSurf& NodeSurf = Model->Surfs[Node.iSurf];
					const FVector& TextureBase = Model->Points[NodeSurf.pBase];
					const FVector& TextureX = Model->Vectors[NodeSurf.vTextureU];
					const FVector& TextureY = Model->Vectors[NodeSurf.vTextureV];
					const int32 BaseVertexIndex = NodeGroup->Vertices.Num();
					// Compute the surface's tangent basis.
					FVector NodeTangentX = Model->Vectors[NodeSurf.vTextureU].GetSafeNormal();
					FVector NodeTangentY = Model->Vectors[NodeSurf.vTextureV].GetSafeNormal();
					FVector NodeTangentZ = Model->Vectors[NodeSurf.vNormal].GetSafeNormal();

					// Generate the node's vertices.
					for(uint32 VertexIndex = 0;VertexIndex < Node.NumVertices;VertexIndex++)
					{
						/*const*/ FVert& Vert = Model->Verts[Node.iVertPool + VertexIndex];
						const FVector& VertexWorldPosition = Model->Points[Vert.pVertex];

						FStaticLightingVertex* DestVertex = new(NodeGroup->Vertices) FStaticLightingVertex;
						DestVertex->WorldPosition = VertexWorldPosition;
						DestVertex->TextureCoordinates[0].X = ((VertexWorldPosition - TextureBase) | TextureX) / 128.0f;
						DestVertex->TextureCoordinates[0].Y = ((VertexWorldPosition - TextureBase) | TextureY) / 128.0f;
						DestVertex->TextureCoordinates[1].X = NodeGroup->WorldToMap.TransformPosition(VertexWorldPosition).X;
						DestVertex->TextureCoordinates[1].Y = NodeGroup->WorldToMap.TransformPosition(VertexWorldPosition).Y;
						DestVertex->WorldTangentX = NodeTangentX;
						DestVertex->WorldTangentY = NodeTangentY;
						DestVertex->WorldTangentZ = NodeTangentZ;

						// TEMP - Will be overridden when lighting is build!
						Vert.ShadowTexCoord = DestVertex->TextureCoordinates[1];

						// Include the vertex in the surface's bounding box.
						NodeGroup->BoundingBox += VertexWorldPosition;
					}

					// Generate the node's vertex indices.
					for(uint32 VertexIndex = 2;VertexIndex < Node.NumVertices;VertexIndex++)
					{
						NodeGroup->TriangleVertexIndices.Add(BaseVertexIndex + 0);
						NodeGroup->TriangleVertexIndices.Add(BaseVertexIndex + VertexIndex);
						NodeGroup->TriangleVertexIndices.Add(BaseVertexIndex + VertexIndex - 1);

						// track the source surface for each triangle
						NodeGroup->TriangleSurfaceMap.Add(Node.iSurf);
					}
				}
			}
		}
	}
	Model->UpdateVertices();

	for (int32 UpdateCompIdx = 0; UpdateCompIdx < ModelComponents.Num(); UpdateCompIdx++)
	{
		UModelComponent* ModelComp = ModelComponents[UpdateCompIdx];
		ModelComp->GenerateElements(true);
		ModelComp->InvalidateCollisionData();
	}
}
#endif

void ULevel::UpdateModelComponents()
{
	// Create/update the level's BSP model components.
	if(!ModelComponents.Num())
	{
#if WITH_EDITOR
		CreateModelComponents();
#endif // WITH_EDITOR
	}
	else
	{
		for(int32 ComponentIndex = 0;ComponentIndex < ModelComponents.Num();ComponentIndex++)
		{
			if(ModelComponents[ComponentIndex] && ModelComponents[ComponentIndex]->IsRegistered())
			{
				ModelComponents[ComponentIndex]->UnregisterComponent();
			}
		}
	}

	if (ModelComponents.Num() > 0)
	{
		check( OwningWorld );
		// Update model components.
		for(int32 ComponentIndex = 0;ComponentIndex < ModelComponents.Num();ComponentIndex++)
		{
			if(ModelComponents[ComponentIndex])
			{
				ModelComponents[ComponentIndex]->RegisterComponentWithWorld(OwningWorld);
			}
		}
	}

	// Initialize the model's index buffers.
	for(TMap<UMaterialInterface*,TScopedPointer<FRawIndexBuffer16or32> >::TIterator IndexBufferIt(Model->MaterialIndexBuffers);
		IndexBufferIt;
		++IndexBufferIt)
	{
		BeginInitResource(IndexBufferIt.Value());
	}

	// Can now release the model's vertex buffer, will have been used for collision
	if(!IsRunningCommandlet())
	{
		Model->ReleaseVertices();
	}

	Model->bInvalidForStaticLighting = true;
}

#if WITH_EDITOR
void ULevel::PreEditUndo()
{
	Super::PreEditUndo();

	// Release the model's resources.
	Model->BeginReleaseResources();
	Model->ReleaseResourcesFence.Wait();

	// Detach existing model components.  These are left in the array, so they are saved for undoing the undo.
	for(int32 ComponentIndex = 0;ComponentIndex < ModelComponents.Num();ComponentIndex++)
	{
		if(ModelComponents[ComponentIndex])
		{
			ModelComponents[ComponentIndex]->UnregisterComponent();
		}
	}

	ReleaseRenderingResources();

	// Wait for the components to be detached.
	FlushRenderingCommands();

}


void ULevel::PostEditUndo()
{
	Super::PostEditUndo();

	Model->UpdateVertices();
	// Update model components that were detached earlier
	UpdateModelComponents();

	// If it's a streaming level and was not visible, don't init rendering resources
	if (OwningWorld)
	{
		bool bIsStreamingLevelVisible = false;
		if (OwningWorld->PersistentLevel == this)
		{
			bIsStreamingLevelVisible = FLevelUtils::IsLevelVisible(OwningWorld->PersistentLevel);
		}
		else
		{
			const int32 NumStreamingLevels = OwningWorld->StreamingLevels.Num();
			for (int i = 0; i < NumStreamingLevels; ++i)
			{
				const ULevelStreaming* StreamedLevel = OwningWorld->StreamingLevels[i];
				if (StreamedLevel && StreamedLevel->GetLoadedLevel() == this)
				{
					bIsStreamingLevelVisible = FLevelUtils::IsLevelVisible(StreamedLevel);
					break;
				}
			}
		}

		if (bIsStreamingLevelVisible)
		{
			InitializeRenderingResources();
		}
	}

	// Non-transactional actors may disappear from the actors list but still exist, so we need to re-add them
	// Likewise they won't get recreated if we undo to before they were deleted, so we'll have nulls in the actors list to remove
	//Actors.Remove(nullptr); // removed because TTransArray exploded (undo followed by redo ends up with a different ArrayNum to originally)
	TSet<AActor*> ActorsSet(Actors);
	TArray<UObject *> InnerObjects;
	GetObjectsWithOuter(this, InnerObjects, /*bIncludeNestedObjects*/ false, /*ExclusionFlags*/ RF_PendingKill);
	for (UObject* InnerObject : InnerObjects)
	{
		AActor* InnerActor = Cast<AActor>(InnerObject);
		if (InnerActor && !ActorsSet.Contains(InnerActor))
		{
			Actors.Add(InnerActor);
		}
	}

	if (LevelBoundsActor.IsValid())
	{
		LevelBoundsActor.Get()->OnLevelBoundsDirtied();
	}
}
#endif // WITH_EDITOR


void ULevel::InvalidateModelGeometry()
{
	// Save the level/model state for transactions.
	Model->Modify();
	Modify();

	// Begin releasing the model's resources.
	Model->BeginReleaseResources();

	// Remove existing model components.
	for(int32 ComponentIndex = 0;ComponentIndex < ModelComponents.Num();ComponentIndex++)
	{
		if(ModelComponents[ComponentIndex])
		{
			ModelComponents[ComponentIndex]->Modify();
			ModelComponents[ComponentIndex]->UnregisterComponent();
		}
	}
	ModelComponents.Empty();
}


void ULevel::InvalidateModelSurface()
{
	Model->InvalidSurfaces = true;
}

void ULevel::CommitModelSurfaces()
{
	if(Model->InvalidSurfaces)
	{
		// Unregister model components
		for(int32 ComponentIndex = 0;ComponentIndex < ModelComponents.Num();ComponentIndex++)
		{
			if(ModelComponents[ComponentIndex] && ModelComponents[ComponentIndex]->IsRegistered())
			{
				ModelComponents[ComponentIndex]->UnregisterComponent();
			}
		}

		// Begin releasing the model's resources.
		Model->BeginReleaseResources();

		// Wait for the model's resources to be released.
		FlushRenderingCommands();

		// Clear the model index buffers.
		Model->MaterialIndexBuffers.Empty();

		// Update the model vertices.
		Model->UpdateVertices();

		// Update the model components.
		for(int32 ComponentIndex = 0;ComponentIndex < ModelComponents.Num();ComponentIndex++)
		{
			if(ModelComponents[ComponentIndex])
			{
				ModelComponents[ComponentIndex]->CommitSurfaces();
			}
		}
		Model->InvalidSurfaces = false;

		// Register model components before init'ing index buffer so collision has access to index buffer data
		// This matches the order of operation in ULevel::UpdateModelComponents
		if (ModelComponents.Num() > 0)
		{
			check( OwningWorld );
			// Update model components.
			for(int32 ComponentIndex = 0;ComponentIndex < ModelComponents.Num();ComponentIndex++)
			{
				if(ModelComponents[ComponentIndex])
				{
					ModelComponents[ComponentIndex]->RegisterComponentWithWorld(OwningWorld);
				}
			}
		}

		// Initialize the model's index buffers.
		for(TMap<UMaterialInterface*,TScopedPointer<FRawIndexBuffer16or32> >::TIterator IndexBufferIt(Model->MaterialIndexBuffers);
			IndexBufferIt;
			++IndexBufferIt)
		{
			BeginInitResource(IndexBufferIt.Value());
		}
	}
}


void ULevel::BuildStreamingData(UWorld* World, ULevel* TargetLevel/*=NULL*/, UTexture2D* UpdateSpecificTextureOnly/*=NULL*/)
{
#if WITH_EDITORONLY_DATA
	double StartTime = FPlatformTime::Seconds();

	bool bUseDynamicStreaming = false;
	GConfig->GetBool(TEXT("TextureStreaming"), TEXT("UseDynamicStreaming"), bUseDynamicStreaming, GEngineIni);

	TArray<ULevel* > LevelsToCheck;
	if ( TargetLevel )
	{
		LevelsToCheck.Add(TargetLevel);
	}
	else if ( World )
	{
		for ( int32 LevelIndex=0; LevelIndex < World->GetNumLevels(); LevelIndex++ )
		{
			ULevel* Level = World->GetLevel(LevelIndex);
			LevelsToCheck.Add(Level);
		}
	}
	else
	{
		for (TObjectIterator<ULevel> It; It; ++It)
		{
			ULevel* Level = *It;
			LevelsToCheck.Add(Level);
		}
	}

	for ( int32 LevelIndex=0; LevelIndex < LevelsToCheck.Num(); LevelIndex++ )
	{
		ULevel* Level = LevelsToCheck[LevelIndex];
		Level->BuildStreamingData( UpdateSpecificTextureOnly );
	}

	UE_LOG(LogLevel, Verbose, TEXT("ULevel::BuildStreamingData took %.3f seconds."), FPlatformTime::Seconds() - StartTime);
#else
	UE_LOG(LogLevel, Fatal,TEXT("ULevel::BuildStreamingData should not be called on a console"));
#endif
}

void ULevel::BuildStreamingData(UTexture2D* UpdateSpecificTextureOnly/*=NULL*/)
{
	bool bUseDynamicStreaming = false;
	GConfig->GetBool(TEXT("TextureStreaming"), TEXT("UseDynamicStreaming"), bUseDynamicStreaming, GEngineIni);

	if ( UpdateSpecificTextureOnly == NULL )
	{
		// Reset the streaming manager, when building data for a whole Level
		IStreamingManager::Get().RemoveLevel( this );
		TextureToInstancesMap.Empty();
		DynamicTextureInstances.Empty();
		ForceStreamTextures.Empty();
		bTextureStreamingBuilt = false;
	}

	TArray<UObject *> ObjectsInOuter;
	GetObjectsWithOuter(this, ObjectsInOuter);

	for( int32 Index = 0; Index < ObjectsInOuter.Num(); Index++ )
	{
		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(ObjectsInOuter[Index]);
		if (Primitive)
		{
			const bool bIsClassDefaultObject = Primitive->IsTemplate(RF_ClassDefaultObject);
			if ( !bIsClassDefaultObject && Primitive->IsRegistered() )
			{
				const AActor* const Owner				= Primitive->GetOwner();
				const bool bIsFoliage					= Owner && Owner->IsA(AInstancedFoliageActor::StaticClass()) && Primitive->IsA(UInstancedStaticMeshComponent::StaticClass()); 
				const bool bIsStatic					= Owner == NULL 
															|| Primitive->Mobility == EComponentMobility::Static 
															|| Primitive->Mobility == EComponentMobility::Stationary
															|| bIsFoliage; // treat Foliage components as static, regardless of mobility settings

				TArray<FStreamingTexturePrimitiveInfo> PrimitiveStreamingTextures;

				// Ask the primitive to enumerate the streaming textures it uses.
				Primitive->GetStreamingTextureInfo(PrimitiveStreamingTextures);

				for(int32 TextureIndex = 0;TextureIndex < PrimitiveStreamingTextures.Num();TextureIndex++)
				{
					const FStreamingTexturePrimitiveInfo& PrimitiveStreamingTexture = PrimitiveStreamingTextures[TextureIndex];
					UTexture2D* Texture2D = Cast<UTexture2D>(PrimitiveStreamingTexture.Texture);
					bool bCanBeStreamedByDistance = !FMath::IsNearlyZero(PrimitiveStreamingTexture.TexelFactor) && !FMath::IsNearlyZero(PrimitiveStreamingTexture.Bounds.W);

					// Only handle 2D textures that match the target texture.
					const bool bIsTargetTexture = (!UpdateSpecificTextureOnly || UpdateSpecificTextureOnly == Texture2D);
					bool bShouldHandleTexture = (Texture2D && bIsTargetTexture);

					// Check if this is a lightmap/shadowmap that shouldn't be streamed.
					if ( bShouldHandleTexture )
					{
						UShadowMapTexture2D* ShadowMap2D	= Cast<UShadowMapTexture2D>(Texture2D);
						ULightMapTexture2D* Lightmap2D		= Cast<ULightMapTexture2D>(Texture2D);
						if ( (Lightmap2D && (Lightmap2D->LightmapFlags & LMF_Streamed) == 0) ||
							(ShadowMap2D && (ShadowMap2D->ShadowmapFlags & SMF_Streamed) == 0) )
						{
							bShouldHandleTexture			= false;
						}
					}

					// Check if this is a duplicate texture
					if (bShouldHandleTexture)
					{
						for (int32 HandledTextureIndex = 0; HandledTextureIndex < TextureIndex; HandledTextureIndex++)
						{
							const FStreamingTexturePrimitiveInfo& HandledStreamingTexture = PrimitiveStreamingTextures[HandledTextureIndex];
							if ( PrimitiveStreamingTexture.Texture == HandledStreamingTexture.Texture &&
								FMath::IsNearlyEqual(PrimitiveStreamingTexture.TexelFactor, HandledStreamingTexture.TexelFactor) &&
								PrimitiveStreamingTexture.Bounds.Equals( HandledStreamingTexture.Bounds ) )
							{
								// It's a duplicate, don't handle this one.
								bShouldHandleTexture = false;
								break;
							}
						}
					}

					if(bShouldHandleTexture)
					{
						// Is the primitive set to force its textures to be resident?
						if ( Primitive->bForceMipStreaming )
						{
							// Add them to the ForceStreamTextures set.
							ForceStreamTextures.Add(Texture2D,true);
						}
						// Is this texture used by a static object?
						else if ( bIsStatic && bCanBeStreamedByDistance )
						{
							// Texture instance information.
							FStreamableTextureInstance TextureInstance;
							TextureInstance.BoundingSphere	= PrimitiveStreamingTexture.Bounds;
							TextureInstance.TexelFactor		= PrimitiveStreamingTexture.TexelFactor;

							// See whether there already is an instance in the level.
							TArray<FStreamableTextureInstance>* TextureInstances = TextureToInstancesMap.Find( Texture2D );
							// We have existing instances.
							if( TextureInstances )
							{
								// Add to the array.
								TextureInstances->Add( TextureInstance );
							}
							// This is the first instance.
							else
							{
								// Create array with current instance as the only entry.
								TArray<FStreamableTextureInstance> NewTextureInstances;
								NewTextureInstances.Add( TextureInstance );
								// And set it.
								TextureToInstancesMap.Add( Texture2D, NewTextureInstances );
							}
						}
						// Is the texture used by a dynamic object that we can track at run-time.
						else if ( bUseDynamicStreaming && Owner && bCanBeStreamedByDistance )
						{
							// Texture instance information.
							FDynamicTextureInstance TextureInstance;
							TextureInstance.Texture = Texture2D;
							TextureInstance.BoundingSphere = PrimitiveStreamingTexture.Bounds;
							TextureInstance.TexelFactor	= PrimitiveStreamingTexture.TexelFactor;
							TextureInstance.OriginalRadius = PrimitiveStreamingTexture.Bounds.W;

							// See whether there already is an instance in the level.
							TArray<FDynamicTextureInstance>* TextureInstances = DynamicTextureInstances.Find( Primitive );
							// We have existing instances.
							if( TextureInstances )
							{
								// Add to the array.
								TextureInstances->Add( TextureInstance );
							}
							// This is the first instance.
							else
							{
								// Create array with current instance as the only entry.
								TArray<FDynamicTextureInstance> NewTextureInstances;
								NewTextureInstances.Add( TextureInstance );
								// And set it.
								DynamicTextureInstances.Add( Primitive, NewTextureInstances );
							}
						}
					}
				}
			}
		}
	}

	if ( UpdateSpecificTextureOnly == NULL )
	{
		// Normalize the texelfactor for lightmaps and shadowmaps
		NormalizeLightmapTexelFactor();

		bTextureStreamingBuilt = true;

		// Update the streaming manager.
		IStreamingManager::Get().AddPreparedLevel( this );
	}
}


void ULevel::NormalizeLightmapTexelFactor()
{
	for ( TMap<UTexture2D*,TArray<FStreamableTextureInstance> >::TIterator It(TextureToInstancesMap); It; ++It )
	{
		UTexture2D* Texture2D = It.Key();
		if ( Texture2D->LODGroup == TEXTUREGROUP_Lightmap || Texture2D->LODGroup == TEXTUREGROUP_Shadowmap)
		{
			TArray<FStreamableTextureInstance>& TextureInstances = It.Value();

			// Clamp texelfactors to 20-80% range.
			// This is to prevent very low-res or high-res charts to dominate otherwise decent streaming.
			struct FCompareTexelFactor
			{
				FORCEINLINE bool operator()( const FStreamableTextureInstance& A, const FStreamableTextureInstance& B ) const
				{
					return A.TexelFactor < B.TexelFactor;
				}
			};
			TextureInstances.Sort( FCompareTexelFactor() );

			float MinTexelFactor = TextureInstances[ TextureInstances.Num() * 0.2f ].TexelFactor;
			float MaxTexelFactor = TextureInstances[ TextureInstances.Num() * 0.8f ].TexelFactor;
			for ( int32 InstanceIndex=0; InstanceIndex < TextureInstances.Num(); ++InstanceIndex )
			{
				FStreamableTextureInstance& Instance = TextureInstances[InstanceIndex];
				Instance.TexelFactor = FMath::Clamp( Instance.TexelFactor, MinTexelFactor, MaxTexelFactor );
			}
		}
	}
}

TArray<FStreamableTextureInstance>* ULevel::GetStreamableTextureInstances(UTexture2D*& TargetTexture)
{
	typedef TArray<FStreamableTextureInstance>	STIA_Type;
	for (TMap<UTexture2D*,STIA_Type>::TIterator It(TextureToInstancesMap); It; ++It)
	{
		TArray<FStreamableTextureInstance>& TSIA = It.Value();
		TargetTexture = It.Key();
		return &TSIA;
	}		

	return NULL;
}

ABrush* ULevel::GetBrush() const
{
	return GetDefaultBrush();
}

ABrush* ULevel::GetDefaultBrush() const
{
	ABrush* DefaultBrush = nullptr;
	if (Actors.Num() >= 2)
	{
		// If the builder brush exists then it will be the 2nd actor in the actors array.
		DefaultBrush = Cast<ABrush>(Actors[1]);
		// If the second actor is not a brush then it certainly cannot be the builder brush.
		if (DefaultBrush != nullptr)
		{
			checkf(DefaultBrush->GetBrushComponent(), *GetPathName());
			checkf(DefaultBrush->Brush != nullptr, *GetPathName());
		}
	}
	return DefaultBrush;
}


AWorldSettings* ULevel::GetWorldSettings() const
{
	checkf( Actors.Num() >= 1, *GetPathName() );
	AWorldSettings* WorldSettings = Cast<AWorldSettings>( Actors[0] );
	checkf( WorldSettings != NULL, *GetPathName() );
	return WorldSettings;
}

ALevelScriptActor* ULevel::GetLevelScriptActor() const
{
	return LevelScriptActor;
}


void ULevel::InitializeNetworkActors()
{
	check( OwningWorld );
	bool			bIsServer				= OwningWorld->IsServer();

	// Kill non relevant client actors and set net roles correctly
	for( int32 ActorIndex=0; ActorIndex<Actors.Num(); ActorIndex++ )
	{
		AActor* Actor = Actors[ActorIndex];
		if( Actor )
		{
			// Kill off actors that aren't interesting to the client.
			if( !Actor->bActorInitialized && !Actor->bActorSeamlessTraveled )
			{
				// Add to startup list
				if (Actor->bNetLoadOnClient)
				{
					Actor->bNetStartup = true;
				}

				if (!bIsServer)
				{
					if (!Actor->bNetLoadOnClient)
					{
						Actor->Destroy();
					}
					else
					{
						// Exchange the roles if:
						//	-We are a client
						//  -This is bNetLoadOnClient=true
						//  -RemoteRole != ROLE_None
						Actor->ExchangeNetRoles(true);
					}
				}				
			}

			Actor->bActorSeamlessTraveled = false;
		}
	}
}

void ULevel::InitializeRenderingResources()
{
	// OwningWorld can be NULL when InitializeRenderingResources is called during undo, where a transient ULevel is created to allow undoing level move operations
	// At the point at which Pre/PostEditChange is called on that transient ULevel, it is not part of any world and therefore should not have its rendering resources initialized
	if (OwningWorld)
	{
		if( !PrecomputedLightVolume->IsAddedToScene() )
		{
			PrecomputedLightVolume->AddToScene(OwningWorld->Scene);

			if (OwningWorld->Scene)
			{
				OwningWorld->Scene->OnLevelAddedToWorld(GetOutermost()->GetFName());
			}
		}
	}
}

void ULevel::ReleaseRenderingResources()
{
	if (OwningWorld && PrecomputedLightVolume)
	{
		PrecomputedLightVolume->RemoveFromScene(OwningWorld->Scene);
	}
}

void ULevel::RouteActorInitialize()
{
	// Send PreInitializeComponents and collect volumes.
	for( int32 ActorIndex=0; ActorIndex<Actors.Num(); ActorIndex++ )
	{
		AActor* const Actor = Actors[ActorIndex];
		if( Actor )
		{
			if( !Actor->bActorInitialized )
			{
				Actor->PreInitializeComponents();
			}
		}
	}

	const bool bCallBeginPlay = OwningWorld->HasBegunPlay();
	TArray<AActor *> ActorsToBeginPlay;

	// Send InitializeComponents on components and PostInitializeComponents.
	for( int32 ActorIndex=0; ActorIndex<Actors.Num(); ActorIndex++ )
	{
		AActor* Actor = Actors[ActorIndex];
		if( Actor )
		{
			if( !Actor->bActorInitialized )
			{
				// Call Initialize on Components.
				Actor->InitializeComponents();

				Actor->PostInitializeComponents(); // should set Actor->bActorInitialized = true
				if (!Actor->bActorInitialized && !Actor->IsPendingKill())
				{
					UE_LOG(LogActor, Fatal, TEXT("%s failed to route PostInitializeComponents.  Please call Super::PostInitializeComponents() in your <className>::PostInitializeComponents() function. "), *Actor->GetFullName() );
				}

				if (bCallBeginPlay)
				{
					ActorsToBeginPlay.Add(Actor);
				}
			}

			// Components are all set up, init touching state.
			// Note: Not doing notifies here since loading or streaming in isn't actually conceptually beginning a touch.
			//	     Rather, it was always touching and the mechanics of loading is just an implementation detail.
			Actor->UpdateOverlaps(false);
		}
	}

	// Do this in a second pass to make sure they're all initialized before begin play starts
	for (int32 ActorIndex = 0; ActorIndex < ActorsToBeginPlay.Num(); ActorIndex++)
	{
		AActor* Actor = ActorsToBeginPlay[ActorIndex];
		Actor->BeginPlay();			
	}
}

bool ULevel::HasAnyActorsOfType(UClass *SearchType)
{
	// just search the actors array
	for (int32 Idx = 0; Idx < Actors.Num(); Idx++)
	{
		AActor *Actor = Actors[Idx];
		// if valid, not pending kill, and
		// of the correct type
		if (Actor != NULL &&
			!Actor->IsPendingKill() &&
			Actor->IsA(SearchType))
		{
			return true;
		}
	}
	return false;
}

#if WITH_EDITOR

TArray<UBlueprint*> ULevel::GetLevelBlueprints() const
{
	TArray<UBlueprint*> LevelBlueprints;
	TArray<UObject*> LevelChildren;
	GetObjectsWithOuter(this, LevelChildren, false, RF_PendingKill);

	for (UObject* LevelChild : LevelChildren)
	{
		UBlueprint* LevelChildBP = Cast<UBlueprint>(LevelChild);
		if (LevelChildBP)
		{
			LevelBlueprints.Add(LevelChildBP);
		}
	}

	return LevelBlueprints;
}

ULevelScriptBlueprint* ULevel::GetLevelScriptBlueprint(bool bDontCreate)
{
	const FString LevelScriptName = ULevelScriptBlueprint::CreateLevelScriptNameFromLevel(this);
	if( !LevelScriptBlueprint && !bDontCreate)
	{
		// If no blueprint is found, create one. 
		LevelScriptBlueprint = Cast<ULevelScriptBlueprint>(FKismetEditorUtilities::CreateBlueprint(GEngine->LevelScriptActorClass, this, FName(*LevelScriptName), BPTYPE_LevelScript, ULevelScriptBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));

		// LevelScript blueprints should not be standalone
		LevelScriptBlueprint->ClearFlags(RF_Standalone);
		ULevel::LevelDirtiedEvent.Broadcast();
	}

	// Ensure that friendly name is always up-to-date
	if (LevelScriptBlueprint)
	{
		LevelScriptBlueprint->FriendlyName = LevelScriptName;
	}

	return LevelScriptBlueprint;
}

void ULevel::OnLevelScriptBlueprintChanged(ULevelScriptBlueprint* InBlueprint)
{
	if( !InBlueprint->bIsRegeneratingOnLoad )
	{
		// Make sure this is OUR level scripting blueprint
		check(InBlueprint == LevelScriptBlueprint);
		UClass* SpawnClass = (LevelScriptBlueprint->GeneratedClass) ? LevelScriptBlueprint->GeneratedClass : LevelScriptBlueprint->SkeletonGeneratedClass;

		// Get rid of the old LevelScriptActor
		if( LevelScriptActor )
		{
			LevelScriptActor->Destroy();
			LevelScriptActor = NULL;
		}

		check( OwningWorld );
		// Create the new one
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.OverrideLevel = this;
		LevelScriptActor = OwningWorld->SpawnActor<ALevelScriptActor>( SpawnClass, SpawnInfo );
		LevelScriptActor->ClearFlags(RF_Transactional);
		check(LevelScriptActor->GetOuter() == this);

		if( LevelScriptActor )
		{
			// Finally, fixup all the bound events to point to their new LSA
			FBlueprintEditorUtils::FixLevelScriptActorBindings(LevelScriptActor, InBlueprint);
		}		
	}
}	

#endif	//WITH_EDITOR


#if WITH_EDITOR
void ULevel::AddMovieSceneBindings( class UMovieSceneBindings* MovieSceneBindings )
{
	// @todo sequencer major: We need to clean up stale bindings objects that no PlayMovieScene node references anymore (LevelScriptBlueprint compile time?)
	if( ensure( MovieSceneBindings != NULL ) )
	{
		Modify();

		// @todo sequencer UObjects: Dangerous cast here to work around MovieSceneCore not being a dependency module of engine
		UObject* ObjectToAdd = (UObject*)MovieSceneBindings;
		MovieSceneBindingsArray.AddUnique( ObjectToAdd );
	}
}

void ULevel::ClearMovieSceneBindings()
{
	Modify();

	MovieSceneBindingsArray.Reset();
}
#endif

void ULevel::AddActiveRuntimeMovieScenePlayer( UObject* RuntimeMovieScenePlayer )
{
	ActiveRuntimeMovieScenePlayers.Add( RuntimeMovieScenePlayer );
}


void ULevel::TickRuntimeMovieScenePlayers( const float DeltaSeconds )
{
	for( int32 CurPlayerIndex = 0; CurPlayerIndex < ActiveRuntimeMovieScenePlayers.Num(); ++CurPlayerIndex )
	{
		// Should never have any active RuntimeMovieScenePlayers on an editor world!
		check( OwningWorld->IsGameWorld() );

		IRuntimeMovieScenePlayerInterface* RuntimeMovieScenePlayer =
			Cast< IRuntimeMovieScenePlayerInterface >( ActiveRuntimeMovieScenePlayers[ CurPlayerIndex ] );
		check( RuntimeMovieScenePlayer != NULL );

		// @todo sequencer runtime: Support expiring instances of RuntimeMovieScenePlayers that have finished playing
		// @todo sequencer runtime: Support destroying spawned actors for a completed moviescene
		RuntimeMovieScenePlayer->Tick( DeltaSeconds );
	}
}


bool ULevel::IsPersistentLevel() const
{
	bool bIsPersistent = false;
	if( OwningWorld )
	{
		bIsPersistent = (this == OwningWorld->PersistentLevel);
	}
	return bIsPersistent;
}

bool ULevel::IsCurrentLevel() const
{
	bool bIsCurrent = false;
	if( OwningWorld )
	{
		bIsCurrent = (this == OwningWorld->GetCurrentLevel());
	}
	return bIsCurrent;
}

void ULevel::ApplyWorldOffset(const FVector& InWorldOffset, bool bWorldShift)
{
	if (bTextureStreamingBuilt)
	{
		// Update texture streaming data to account for the move
		for (TMap< UTexture2D*, TArray<FStreamableTextureInstance> >::TIterator It(TextureToInstancesMap); It; ++It)
		{
			TArray<FStreamableTextureInstance>& TextureInfo = It.Value();
			for (int32 i = 0; i < TextureInfo.Num(); i++)
			{
				TextureInfo[i].BoundingSphere.Center+= InWorldOffset;
			}
		}
		
		// Re-add level data to a manager
		IStreamingManager::Get().AddPreparedLevel( this );
	}

	// Move precomputed light samples
	if (PrecomputedLightVolume)
	{
		if (!PrecomputedLightVolume->IsAddedToScene())
		{
			PrecomputedLightVolume->ApplyWorldOffset(InWorldOffset);
		}
		// At world origin rebasing all registered volumes will be moved during FScene shifting
		// Otherwise we need to send a command to move just this volume
		else if (!bWorldShift) 
		{
			ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
 				ApplyWorldOffset_PLV,
 				FPrecomputedLightVolume*, InPrecomputedLightVolume, PrecomputedLightVolume,
 				FVector, InWorldOffset, InWorldOffset,
 			{
				InPrecomputedLightVolume->ApplyWorldOffset(InWorldOffset);
 			});
		}
	}

	// Iterate over all actors in the level and move them
	for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ActorIndex++)
	{
		AActor* Actor = Actors[ActorIndex];
		if (Actor)
		{
			FVector Offset = (bWorldShift && Actor->bIgnoresOriginShifting) ? FVector::ZeroVector : InWorldOffset;
						
			if (!Actor->IsA(ANavigationData::StaticClass())) // Navigation data will be moved in NavigationSystem
			{
				Actor->ApplyWorldOffset(Offset, bWorldShift);
			}
		}
	}
	
	// Move model geometry
	for (int32 CompIdx = 0; CompIdx < ModelComponents.Num(); ++CompIdx)
	{
		ModelComponents[CompIdx]->ApplyWorldOffset(InWorldOffset, bWorldShift);
	}
}

void ULevel::RegisterActorForAutoReceiveInput(AActor* Actor, const int32 PlayerIndex)
{
	PendingAutoReceiveInputActors.Add(FPendingAutoReceiveInputActor(Actor, PlayerIndex));
};

void ULevel::PushPendingAutoReceiveInput(APlayerController* InPlayerController)
{
	check( InPlayerController );
	int32 PlayerIndex = -1;
	int32 Index = 0;
	for( FConstPlayerControllerIterator Iterator = InPlayerController->GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator )
	{
		APlayerController* PlayerController = *Iterator;
		if (InPlayerController == PlayerController)
		{
			PlayerIndex = Index;
			break;
		}
		Index++;
	}

	if (PlayerIndex >= 0)
	{
		TArray<AActor*> ActorsToAdd;
		for (int32 PendingIndex = PendingAutoReceiveInputActors.Num() - 1; PendingIndex >= 0; --PendingIndex)
		{
			FPendingAutoReceiveInputActor& PendingActor = PendingAutoReceiveInputActors[PendingIndex];
			if (PendingActor.PlayerIndex == PlayerIndex)
			{
				if (PendingActor.Actor.IsValid())
				{
					ActorsToAdd.Add(PendingActor.Actor.Get());
				}
				PendingAutoReceiveInputActors.RemoveAtSwap(PendingIndex);
			}
		}
		for (int32 ToAddIndex = ActorsToAdd.Num() - 1; ToAddIndex >= 0; --ToAddIndex)
		{
			APawn* PawnToPossess = Cast<APawn>(ActorsToAdd[ToAddIndex]);
			if (PawnToPossess)
			{
				InPlayerController->Possess(PawnToPossess);
			}
			else
			{
				ActorsToAdd[ToAddIndex]->EnableInput(InPlayerController);
			}
		}
	}
}

void ULevel::AddAssetUserData(UAssetUserData* InUserData)
{
	if(InUserData != NULL)
	{
		UAssetUserData* ExistingData = GetAssetUserDataOfClass(InUserData->GetClass());
		if(ExistingData != NULL)
		{
			AssetUserData.Remove(ExistingData);
		}
		AssetUserData.Add(InUserData);
	}
}

UAssetUserData* ULevel::GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for(int32 DataIdx=0; DataIdx<AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if(Datum != NULL && Datum->IsA(InUserDataClass))
		{
			return Datum;
		}
	}
	return NULL;
}

void ULevel::RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for(int32 DataIdx=0; DataIdx<AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if(Datum != NULL && Datum->IsA(InUserDataClass))
		{
			AssetUserData.RemoveAt(DataIdx);
			return;
		}
	}
}

