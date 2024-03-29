// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EnvironmentQuery/Items/EnvQueryItemType.h"
#include "DataProviders/AIDataProvider.h"
#include "EnvQueryTypes.generated.h"

class ARecastNavMesh;
class UNavigationQueryFilter;
class UEnvQueryTest;
class UEnvQueryGenerator;
class UEnvQueryItemType_VectorBase;
class UEnvQueryItemType_ActorBase;
class UEnvQueryContext;
struct FEnvQueryInstance;
struct FEnvQueryOptionInstance;
struct FEnvQueryItemDetails;

AIMODULE_API DECLARE_LOG_CATEGORY_EXTERN(LogEQS, Warning, All);

// If set, execution details will be processed by debugger
#define USE_EQS_DEBUGGER				(1 && !(UE_BUILD_SHIPPING || UE_BUILD_TEST))

DECLARE_STATS_GROUP(TEXT("Environment Query"), STATGROUP_AI_EQS, STATCAT_Advanced);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Tick"),STAT_AI_EQS_Tick,STATGROUP_AI_EQS, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Tick - EQS work"), STAT_AI_EQS_TickWork, STATGROUP_AI_EQS, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Tick - OnFinished delegates"), STAT_AI_EQS_TickNotifies, STATGROUP_AI_EQS, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Load Time"),STAT_AI_EQS_LoadTime,STATGROUP_AI_EQS, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Generator Time"),STAT_AI_EQS_GeneratorTime,STATGROUP_AI_EQS, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Test Time"),STAT_AI_EQS_TestTime,STATGROUP_AI_EQS, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Num Instances"),STAT_AI_EQS_NumInstances,STATGROUP_AI_EQS, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Num Items"),STAT_AI_EQS_NumItems,STATGROUP_AI_EQS, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Instance memory"),STAT_AI_EQS_InstanceMemory,STATGROUP_AI_EQS, AIMODULE_API);

UENUM()
namespace EEnvTestPurpose
{
	enum Type
	{
		Filter UMETA(DisplayName="Filter Only"),
		Score UMETA(DisplayName="Score Only"),
		FilterAndScore UMETA(DisplayName="Filter and Score")
	};
}

UENUM()
namespace EEnvTestFilterType
{
	enum Type
	{
		Minimum,	// For numeric tests
		Maximum,	// For numeric tests
		Range,		// For numeric tests
		Match		// For boolean tests
	};
}

UENUM()
namespace EEnvTestScoreEquation
{
	enum Type
	{
		Linear,
		Square,
		InverseLinear,	// For now...
		Constant
		// What other curve shapes should be supported?  At first I was thinking we'd have parametric (F*V^P + C), but
		// many versions of that curve would violate the [0, 1] output range which I think we should preserve.  So instead
		// I think we should define these by "curve shape".  I'm not sure if we need to allow full tweaks to the curves,
		// such as supporting other "Exponential" curves (positive even powers).  However, I think it's likely that we'll
		// want to support "smooth LERP" / S-shaped curve of the form 2x^3 - 3x^2, and possibly a "sideways" version of
		// the same S-curve.  We also might want to allow "Sine" curves, basically adjusted to match the range and then
		// simply offset by some amount to allow a peak or valley in the middle or on the ends.  (Four Sine options are
		// probably sufficient.)  I'm not sure if Sine is really needed though, so probably we should only add it if
		// there's a need identified.  One other curve shape we might want is "Square Root", which might optionally
		// support any positive fractional power (if we also supported any positive even number for an "Exponential"
		// type.
	};
}

UENUM()
namespace EEnvTestWeight
{
	enum Type
	{
		None,
		Square,
		Inverse,
		Unused			UMETA(Hidden),
		Constant,
		Skip			UMETA(DisplayName = "Do not weight"),
	};
}

UENUM()
namespace EEnvTestCost
{
	enum Type
	{
		Low,				// reading data, math operations (e.g. distance)
		Medium,				// processing data from multiple sources (e.g. fire tickets)
		High,				// really expensive calls (e.g. visibility traces, pathfinding)
	};
}

UENUM()
namespace EEnvQueryStatus
{
	enum Type
	{
		Processing,
		Success,
		Failed,
		Aborted,
		OwnerLost,
		MissingParam,
	};
}

UENUM()
namespace EEnvQueryRunMode
{
	enum Type
	{
		SingleResult,		// weight scoring first, try conditions from best result and stop after first item pass
		AllMatching,		// conditions first (limit set of items), weight scoring later
	};
}

UENUM()
namespace EEnvQueryParam
{
	enum Type
	{
		Float,
		Int,
		Bool,
	};
}

UENUM()
namespace EEnvQueryTrace
{
	enum Type
	{
		None,
		Navigation,
		Geometry,
	};
}

UENUM()
namespace EEnvTraceShape
{
	enum Type
	{
		Line,
		Box,
		Sphere,
		Capsule,
	};
}

UENUM()
namespace EEnvDirection
{
	enum Type
	{
		TwoPoints	UMETA(DisplayName="Two Points",ToolTip="Direction from location of one context to another."),
		Rotation	UMETA(ToolTip="Context's rotation will be used as a direction."),
	};
}

UENUM()
namespace EEnvQueryTestClamping
{
	enum Type
	{
		None,			
		SpecifiedValue,	// Clamp to value specified in test
		FilterThreshold	// Clamp to test's filter threshold
	};
}

// DEPRECATED, will be removed soon - use AI Data Providers instead 
USTRUCT()
struct AIMODULE_API FEnvFloatParam
{
	GENERATED_USTRUCT_BODY();

	/** default value */
	UPROPERTY(EditDefaultsOnly, Category=Param)
	float Value;

	/** name of parameter */
	UPROPERTY(EditDefaultsOnly, Category=Param)
	FName ParamName;

	bool IsNamedParam() const { return ParamName != NAME_None; }
	void Convert(UObject* Owner, FAIDataProviderFloatValue& ValueProvider);
};

// DEPRECATED, will be removed soon - use AI Data Providers instead 
USTRUCT()
struct AIMODULE_API FEnvIntParam
{
	GENERATED_USTRUCT_BODY();

	/** default value */
	UPROPERTY(EditDefaultsOnly, Category=Param)
	int32 Value;

	/** name of parameter */
	UPROPERTY(EditDefaultsOnly, Category=Param)
	FName ParamName;

	bool IsNamedParam() const { return ParamName != NAME_None; }
	void Convert(UObject* Owner, FAIDataProviderIntValue& ValueProvider);
};

// DEPRECATED, will be removed soon - use AI Data Providers instead 
USTRUCT()
struct AIMODULE_API FEnvBoolParam
{
	GENERATED_USTRUCT_BODY();

	/** default value */
	UPROPERTY(EditDefaultsOnly, Category=Param)
	bool Value;

	/** name of parameter */
	UPROPERTY(EditDefaultsOnly, Category=Param)
	FName ParamName;

	bool IsNamedParam() const { return ParamName != NAME_None; }
	void Convert(UObject* Owner, FAIDataProviderBoolValue& ValueProvider);
};

USTRUCT(BlueprintType)
struct AIMODULE_API FEnvNamedValue
{
	GENERATED_USTRUCT_BODY();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Param)
	FName ParamName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Param)
	TEnumAsByte<EEnvQueryParam::Type> ParamType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Param)
	float Value;
};

USTRUCT()
struct AIMODULE_API FEnvDirection
{
	GENERATED_USTRUCT_BODY()

	/** line A: start context */
	UPROPERTY(EditDefaultsOnly, Category=Direction)
	TSubclassOf<UEnvQueryContext> LineFrom;

	/** line A: finish context */
	UPROPERTY(EditDefaultsOnly, Category=Direction)
	TSubclassOf<UEnvQueryContext> LineTo;

	/** line A: direction context */
	UPROPERTY(EditDefaultsOnly, Category=Direction)
	TSubclassOf<UEnvQueryContext> Rotation;

	/** defines direction of second line used by test */
	UPROPERTY(EditDefaultsOnly, Category=Direction, meta=(DisplayName="Mode"))
	TEnumAsByte<EEnvDirection::Type> DirMode;

	FText ToText() const;
};

USTRUCT()
struct AIMODULE_API FEnvTraceData
{
	GENERATED_USTRUCT_BODY()

	enum EDescriptionMode
	{
		Brief,
		Detailed,
	};

	FEnvTraceData() : 
		ProjectDown(1024.0f), ProjectUp(1024.0f), ExtentX(10.0f), ExtentY(10.0f), ExtentZ(10.0f), bOnlyBlockingHits(true),
		bCanTraceOnNavMesh(true), bCanTraceOnGeometry(true), bCanDisableTrace(true), bCanProjectDown(false)
	{
	}

	/** navigation filter for tracing */
	UPROPERTY(EditDefaultsOnly, Category=Trace)
	TSubclassOf<UNavigationQueryFilter> NavigationFilter;

	/** search height: below point */
	UPROPERTY(EditDefaultsOnly, Category=Trace, meta=(UIMin=0, ClampMin=0))
	float ProjectDown;

	/** search height: above point */
	UPROPERTY(EditDefaultsOnly, Category=Trace, meta=(UIMin=0, ClampMin=0))
	float ProjectUp;

	/** shape parameter for trace */
	UPROPERTY(EditDefaultsOnly, Category=Trace, meta=(UIMin=0, ClampMin=0))
	float ExtentX;

	/** shape parameter for trace */
	UPROPERTY(EditDefaultsOnly, Category=Trace, meta=(UIMin=0, ClampMin=0))
	float ExtentY;

	/** shape parameter for trace */
	UPROPERTY(EditDefaultsOnly, Category=Trace, meta=(UIMin=0, ClampMin=0))
	float ExtentZ;

	/** this value will be added to resulting location's Z axis. Can be useful when 
	 *	projecting points to navigation since navmesh is just an approximation of level 
	 *	geometry and items may end up being under collide-able geometry which would 
	 *	for example falsify visibility tests.*/
	UPROPERTY(EditDefaultsOnly, Category=Trace)
	float PostProjectionVerticalOffset;

	/** geometry trace channel */
	UPROPERTY(EditDefaultsOnly, Category=Trace)
	TEnumAsByte<enum ETraceTypeQuery> TraceChannel;

	/** shape used for geometry tracing */
	UPROPERTY(EditDefaultsOnly, Category=Trace)
	TEnumAsByte<EEnvTraceShape::Type> TraceShape;

	/** shape used for geometry tracing */
	UPROPERTY(EditDefaultsOnly, Category=Trace)
	TEnumAsByte<EEnvQueryTrace::Type> TraceMode;

	/** if set, trace will run on complex collisions */
	UPROPERTY(EditDefaultsOnly, Category=Trace, AdvancedDisplay)
	uint32 bTraceComplex : 1;

	/** if set, trace will look only for blocking hits */
	UPROPERTY(EditDefaultsOnly, Category=Trace, AdvancedDisplay)
	uint32 bOnlyBlockingHits : 1;

	/** if set, editor will allow picking navmesh trace */
	UPROPERTY(EditDefaultsOnly, Category=Trace)
	uint32 bCanTraceOnNavMesh : 1;

	/** if set, editor will allow picking geometry trace */
	UPROPERTY(EditDefaultsOnly, Category=Trace)
	uint32 bCanTraceOnGeometry : 1;

	/** if set, editor will allow  */
	UPROPERTY(EditDefaultsOnly, Category=Trace)
	uint32 bCanDisableTrace : 1;

	/** if set, editor show height up/down properties for projection */
	UPROPERTY(EditDefaultsOnly, Category=Trace)
	uint32 bCanProjectDown : 1;

	FText ToText(EDescriptionMode DescMode) const;

	void SetGeometryOnly();
	void SetNavmeshOnly();
};

//////////////////////////////////////////////////////////////////////////
// Returned results

struct AIMODULE_API FEnvQueryItem
{
	/** total score of item */
	float Score;

	/** raw data offset */
	int32 DataOffset:31;

	/** has this item been discarded? */
	int32 bIsDiscarded:1;

	FORCEINLINE bool IsValid() const { return DataOffset >= 0 && !bIsDiscarded; }
	FORCEINLINE void Discard() { bIsDiscarded = true; }

	bool operator<(const FEnvQueryItem& Other) const
	{
		// sort by validity
		if (IsValid() != Other.IsValid())
		{
			// self not valid = less important
			return !IsValid();
		}

		// sort by score if not equal. As last resort sort by DataOffset to achieve stable sort.
		return Score != Other.Score ? Score < Other.Score : DataOffset < Other.DataOffset;
	}

	FEnvQueryItem() : Score(0.0f), DataOffset(-1), bIsDiscarded(false) {}
	FEnvQueryItem(int32 InOffset) : Score(0.0f), DataOffset(InOffset), bIsDiscarded(false) {}
};

template <> struct TIsZeroConstructType<FEnvQueryItem> { enum { Value = true }; };

struct AIMODULE_API FEnvQueryResult
{
	TArray<FEnvQueryItem> Items;

	/** type of generated items */
	TSubclassOf<UEnvQueryItemType> ItemType;

	/** raw data of items */
	TArray<uint8> RawData;

private:
	/** query status */
	TEnumAsByte<EEnvQueryStatus::Type> Status;

public:
	/** index of query option, that generated items */
	int32 OptionIndex;

	/** instance ID */
	int32 QueryID;

	/** instance owner */
	TWeakObjectPtr<UObject> Owner;

	FORCEINLINE float GetItemScore(int32 Index) const { return Items.IsValidIndex(Index) ? Items[Index].Score : 0.0f; }

	/** item accessors for basic types */
	AActor* GetItemAsActor(int32 Index) const;
	FVector GetItemAsLocation(int32 Index) const;

	FEnvQueryResult() : ItemType(NULL), Status(EEnvQueryStatus::Processing), OptionIndex(0) {}
	FEnvQueryResult(const EEnvQueryStatus::Type& InStatus) : ItemType(NULL), Status(InStatus), OptionIndex(0) {}

	FORCEINLINE bool IsFinished() const { return Status != EEnvQueryStatus::Processing; }
	FORCEINLINE bool IsAborted() const { return Status == EEnvQueryStatus::Aborted; }
	FORCEINLINE void MarkAsMissingParam() { Status = EEnvQueryStatus::MissingParam; }
	FORCEINLINE void MarkAsAborted() { Status = EEnvQueryStatus::Aborted; }
	FORCEINLINE void MarkAsFailed() { Status = EEnvQueryStatus::Failed; }
	FORCEINLINE void MarkAsFinishedWithoutIssues() { Status = EEnvQueryStatus::Success; }
	FORCEINLINE void MarkAsOwnerLost() { Status = EEnvQueryStatus::OwnerLost; }
};


//////////////////////////////////////////////////////////////////////////
// Runtime processing structures

DECLARE_DELEGATE_OneParam(FQueryFinishedSignature, TSharedPtr<FEnvQueryResult>);

struct AIMODULE_API FEnvQuerySpatialData
{
	FVector Location;
	FRotator Rotation;
};

/** Detailed information about item, used by tests */
struct AIMODULE_API FEnvQueryItemDetails
{
	/** Results assigned by option's tests, before any modifications */
	TArray<float> TestResults;

#if USE_EQS_DEBUGGER
	/** Results assigned by option's tests, after applying modifiers, normalization and weight */
	TArray<float> TestWeightedScores;

	int32 FailedTestIndex;
	int32 ItemIndex;
	FString FailedDescription;
#endif // USE_EQS_DEBUGGER

	FEnvQueryItemDetails() {}
	FEnvQueryItemDetails(int32 NumTests, int32 InItemIndex)
	{
		TestResults.AddZeroed(NumTests);
#if USE_EQS_DEBUGGER
		TestWeightedScores.AddZeroed(NumTests);
		ItemIndex = InItemIndex;
		FailedTestIndex = INDEX_NONE;
#endif
	}

	FORCEINLINE uint32 GetAllocatedSize() const
	{
		return sizeof(*this) +
#if USE_EQS_DEBUGGER
			TestWeightedScores.GetAllocatedSize() +
#endif
			TestResults.GetAllocatedSize();
	}
};

struct AIMODULE_API FEnvQueryContextData
{
	/** type of context values */
	TSubclassOf<UEnvQueryItemType> ValueType;

	/** number of stored values */
	int32 NumValues;

	/** data of stored values */
	TArray<uint8> RawData;

	FEnvQueryContextData()
		: NumValues(0)
	{}

	FORCEINLINE uint32 GetAllocatedSize() const { return sizeof(*this) + RawData.GetAllocatedSize(); }
};

struct AIMODULE_API FEnvQueryOptionInstance
{
	/** generator object, raw pointer can be used safely because it will be always referenced by EnvQueryManager */
	UEnvQueryGenerator* Generator;

	/** test objects, raw pointer can be used safely because it will be always referenced by EnvQueryManager */
	TArray<UEnvQueryTest*> Tests;

	/** type of generated items */
	TSubclassOf<UEnvQueryItemType> ItemType;

	/** if set, items will be shuffled after tests */
	bool bShuffleItems;

	FORCEINLINE uint32 GetAllocatedSize() const { return sizeof(*this) + Tests.GetAllocatedSize(); }
};

#if NO_LOGGING
#define EQSHEADERLOG(...)
#else
#define EQSHEADERLOG(msg) Log(msg)
#endif // NO_LOGGING

struct FEQSQueryDebugData
{
	TArray<FEnvQueryItem> DebugItems;
	TArray<FEnvQueryItemDetails> DebugItemDetails;
	TArray<uint8> RawData;
	TArray<FString> PerformedTestNames;
	// indicates the query was run in a single-item mode and that it has been found
	uint32 bSingleItemResult : 1;

	void Store(const FEnvQueryInstance* QueryInstance);
	void Reset()
	{
		DebugItems.Reset();
		DebugItemDetails.Reset();
		RawData.Reset();
		PerformedTestNames.Reset();
		bSingleItemResult = false;
	}
};

struct AIMODULE_API FEnvQueryInstance : public FEnvQueryResult
{
	typedef float FNamedParamValueType;

	/** short name of query template */
	FString QueryName;

	/** world owning this query instance */
	UWorld* World;

	/** observer's delegate */
	FQueryFinishedSignature FinishDelegate;

	/** execution params */
	TMap<FName, FNamedParamValueType> NamedParams;

	/** contexts in use */
	TMap<UClass*, FEnvQueryContextData> ContextCache;

	/** list of options */
	TArray<FEnvQueryOptionInstance> Options;

	/** currently processed test (-1 = generator) */
	int32 CurrentTest;

	/** non-zero if test run last step has been stopped mid-process. This indicates
	 *	index of the first item that needs processing when resumed */
	int32 CurrentTestStartingItem;

	/** list of item details */
	TArray<FEnvQueryItemDetails> ItemDetails;

	/** number of valid items on list */
	int32 NumValidItems;

	/** size of current value */
	uint16 ValueSize;

	/** used to breaking from item iterator loops */
	uint8 bFoundSingleResult : 1;

private:
	/** set when testing final condition of an option */
	uint8 bPassOnSingleResult : 1;

public:
#if USE_EQS_DEBUGGER
	/** set to true to store additional debug info */
	uint8 bStoreDebugInfo : 1;
#endif // USE_EQS_DEBUGGER

	/** run mode */
	TEnumAsByte<EEnvQueryRunMode::Type> Mode;

	/** item type's CDO for location tests */
	UEnvQueryItemType_VectorBase* ItemTypeVectorCDO;

	/** item type's CDO for actor tests */
	UEnvQueryItemType_ActorBase* ItemTypeActorCDO;

	/** if > 0 then it's how much time query has for performing current step */
	double TimeLimit;

	FEnvQueryInstance() : World(NULL), CurrentTest(-1), NumValidItems(0), bFoundSingleResult(false), bPassOnSingleResult(false)
#if USE_EQS_DEBUGGER
		, bStoreDebugInfo(bDebuggingInfoEnabled) 
#endif // USE_EQS_DEBUGGER
	{ IncStats(); }
	FEnvQueryInstance(const FEnvQueryInstance& Other) { *this = Other; IncStats(); }
	~FEnvQueryInstance() { DecStats(); }

	/** execute single step of query */
	void ExecuteOneStep(double TimeLimit);

	/** update context cache */
	bool PrepareContext(UClass* Context, FEnvQueryContextData& ContextData);

	/** helpers for reading spatial data from context */
	bool PrepareContext(UClass* Context, TArray<FEnvQuerySpatialData>& Data);
	bool PrepareContext(UClass* Context, TArray<FVector>& Data);
	bool PrepareContext(UClass* Context, TArray<FRotator>& Data);
	/** helpers for reading actor data from context */
	bool PrepareContext(UClass* Context, TArray<AActor*>& Data);
	
	bool IsInSingleItemFinalSearch() const { return !!bPassOnSingleResult; }
	/** check if current test can batch its calculations */
	bool CanBatchTest() const { return !IsInSingleItemFinalSearch(); }

	/** raw data operations */
	void ReserveItemData(int32 NumAdditionalItems);

	template<typename TypeItem, typename TypeValue>
	void AddItemData(TypeValue ItemValue)
	{
		DEC_MEMORY_STAT_BY(STAT_AI_EQS_InstanceMemory, RawData.GetAllocatedSize() + Items.GetAllocatedSize());

		const int32 DataOffset = RawData.AddUninitialized(ValueSize);
		TypeItem::SetValue(RawData.GetData() + DataOffset, ItemValue);
		Items.Add(FEnvQueryItem(DataOffset));

		INC_MEMORY_STAT_BY(STAT_AI_EQS_InstanceMemory, RawData.GetAllocatedSize() + Items.GetAllocatedSize());
	}

protected:

	/** prepare item data after generator has finished */
	void FinalizeGeneration();

	/** update costs and flags after test has finished */
	void FinalizeTest();
	
	/** final pass on items of finished query */
	void FinalizeQuery();

	/** normalize total score in range 0..1 */
	void NormalizeScores();

	/** sort all scores, from highest to lowest */
	void SortScores();

	/** pick one of items with highest score */
	void PickBestItem();

	/** discard all items but one */
	void PickSingleItem(int32 ItemIndex);

public:

	/** removes all runtime data that can be used for debugging (not a part of actual query result) */
	void StripRedundantData();

#if STATS
	FORCEINLINE void IncStats()
	{
		INC_MEMORY_STAT_BY(STAT_AI_EQS_InstanceMemory, GetAllocatedSize());
		INC_DWORD_STAT_BY(STAT_AI_EQS_NumItems, Items.Num());
	}

	FORCEINLINE void DecStats()
	{
		DEC_MEMORY_STAT_BY(STAT_AI_EQS_InstanceMemory, GetAllocatedSize()); 
		DEC_DWORD_STAT_BY(STAT_AI_EQS_NumItems, Items.Num());
	}

	uint32 GetAllocatedSize() const;
	uint32 GetContextAllocatedSize() const;
#else
	FORCEINLINE uint32 GetAllocatedSize() const { return 0; }
	FORCEINLINE uint32 GetContextAllocatedSize() const { return 0; }
	FORCEINLINE void IncStats() {}
	FORCEINLINE void DecStats() {}
#endif // STATS

#if !NO_LOGGING
	void Log(const FString Msg) const;
#endif // #if !NO_LOGGING
	
#if USE_EQS_DEBUGGER
#	define  UE_EQS_DBGMSG(Format, ...) \
					Instance->ItemDetails[CurrentItem].FailedDescription = FString::Printf(Format, ##__VA_ARGS__)

#	define UE_EQS_LOG(CategoryName, Verbosity, Format, ...) \
					UE_LOG(CategoryName, Verbosity, Format, ##__VA_ARGS__); \
					UE_EQS_DBGMSG(Format, ##__VA_ARGS__); 
#else
#	define UE_EQS_DBGMSG(Format, ...)
#	define UE_EQS_LOG(CategoryName, Verbosity, Format, ...) UE_LOG(CategoryName, Verbosity, Format, ##__VA_ARGS__); 
#endif

#if CPP || UE_BUILD_DOCS
	struct AIMODULE_API ItemIterator
	{
		ItemIterator(const UEnvQueryTest* QueryTest, FEnvQueryInstance& QueryInstance, int32 StartingItemIndex = INDEX_NONE);

		~ItemIterator()
		{
			Instance->CurrentTestStartingItem = CurrentItem;
		}

		void SetScore(EEnvTestPurpose::Type TestPurpose, EEnvTestFilterType::Type FilterType, float Score, float Min, float Max)
		{
			bool bPassedTest = true;

			if (TestPurpose != EEnvTestPurpose::Score)	// May need to filter results!
			{
				switch (FilterType)
				{
					case EEnvTestFilterType::Maximum:
						if (Score > Max)
						{
							UE_EQS_DBGMSG(TEXT("Value %f is above maximum value set to %f"), Score, Max);
							bPassedTest = false;
						}
						break;

					case EEnvTestFilterType::Minimum:
						if (Score < Min)
						{
							UE_EQS_DBGMSG(TEXT("Value %f is below minimum value set to %f"), Score, Min);
							bPassedTest = false;
						}
						break;

					case EEnvTestFilterType::Range:
						if ((Score < Min) || (Score > Max))
						{
							UE_EQS_DBGMSG(TEXT("Value %f is out of range set to (%f, %f)"), Score, Min, Max);
							bPassedTest = false;
						}
						break;

					case EEnvTestFilterType::Match:
						UE_EQS_LOG(LogEQS, Error, TEXT("Filtering Type set to 'Match' for floating point test.  Will consider test as failed in all cases."));
						bPassedTest = false;
						break;

					default:
						UE_EQS_LOG(LogEQS, Error, TEXT("Filtering Type set to invalid value for floating point test.  Will consider test as failed in all cases."));
						bPassedTest = false;
						break;
				}
			}

			if (bPassedTest)
			{
				// If we passed the test, either we really did, or we're only scoring, so we can't truly "fail".	
				ItemScore += Score;
				NumPartialScores++;
			}
			else
			{
				// We are ONLY filtering, and we failed
				bPassed = false;
			}
		}

		void SetScore(EEnvTestPurpose::Type TestPurpose, EEnvTestFilterType::Type FilterType, bool bScore, bool bExpected)
		{
			bool bPassedTest = true;
			switch (FilterType)
			{
				case EEnvTestFilterType::Match:
					bPassedTest = (bScore == bExpected);
#if USE_EQS_DEBUGGER
					if (!bPassedTest)
					{
						UE_EQS_DBGMSG(TEXT("Boolean score don't mach (expected %s and got %s)"), bExpected ? TEXT("TRUE") : TEXT("FALSE"), bScore ? TEXT("TRUE") : TEXT("FALSE"));
					}
#endif
					break;

				case EEnvTestFilterType::Maximum:
					UE_EQS_LOG(LogEQS, Error, TEXT("Filtering Type set to 'Maximum' for boolean test.  Will consider test as failed in all cases."));
					bPassedTest = false;
					break;

				case EEnvTestFilterType::Minimum:
					UE_EQS_LOG(LogEQS, Error, TEXT("Filtering Type set to 'Minimum' for boolean test.  Will consider test as failed in all cases."));
					bPassedTest = false;
					break;

				case EEnvTestFilterType::Range:
					UE_EQS_LOG(LogEQS, Error, TEXT("Filtering Type set to 'Range' for boolean test.  Will consider test as failed in all cases."));
					bPassedTest = false;
					break;

				default:
					UE_EQS_LOG(LogEQS, Error, TEXT("Filtering Type set to invalid value for boolean test.  Will consider test as failed in all cases."));
					bPassedTest = false;
					break;
			}

			if (!bPassedTest)
			{
				if (TestPurpose == EEnvTestPurpose::Score)
				{
					bSkipped = true;
					NumPartialScores++;
				}
				else // We are filtering!
				{
					bPassed = false;
				}
			}
			else
			{
				ItemScore += 1.0f;
				NumPartialScores++;
			}
		}

		uint8* GetItemData()
		{
			return Instance->RawData.GetData() + Instance->Items[CurrentItem].DataOffset;
		}

		void DiscardItem()
		{
			bPassed = false;
		}

		void SkipItem()
		{
			bSkipped++;
		}

		operator bool() const
		{
			return CurrentItem < Instance->Items.Num() && !Instance->bFoundSingleResult && (Deadline < 0 || FPlatformTime::Seconds() < Deadline);
		}

		int32 operator*() const
		{
			return CurrentItem; 
		}

		void operator++()
		{
			StoreTestResult();
			if (!Instance->bFoundSingleResult)
			{
				InitItemScore();
				FindNextValidIndex();
			}
		}

	protected:

		FEnvQueryInstance* Instance;
		int32 CurrentItem;
		int32 NumPartialScores;
		double Deadline;
		float ItemScore;
		uint32 bPassed : 1;
		uint32 bSkipped : 1;

		void InitItemScore()
		{
			NumPartialScores = 0;
			ItemScore = 0.0f;
			bPassed = true;
			bSkipped = false;
		}

		void HandleFailedTestResult();
		void StoreTestResult();

		FORCEINLINE void FindNextValidIndex()
		{
			for (CurrentItem++; CurrentItem < Instance->Items.Num() && !Instance->Items[CurrentItem].IsValid(); CurrentItem++)
				;
		}
	};
#endif

#undef UE_EQS_LOG
#undef UE_EQS_DBGMSG

#if USE_EQS_DEBUGGER
	FEQSQueryDebugData DebugData;
	static bool bDebuggingInfoEnabled;
#endif // USE_EQS_DEBUGGER

	FBox GetBoundingBox() const;
};

namespace FEQSHelpers
{
#if WITH_RECAST
	AIMODULE_API const ARecastNavMesh* FindNavMeshForQuery(FEnvQueryInstance& QueryInstance);
#endif // WITH_RECAST
}

UCLASS(Abstract)
class AIMODULE_API UEnvQueryTypes : public UObject
{
	GENERATED_BODY()

public:
	/** special test value assigned to items skipped by condition check */
	static float SkippedItemValue;

	static FText GetShortTypeName(const UObject* Ob);
	static FText DescribeContext(TSubclassOf<UEnvQueryContext> ContextClass);
};
