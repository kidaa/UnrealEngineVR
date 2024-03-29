// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffectCalculation.h"
#include "GameplayEffectAggregator.h"
#include "GameplayEffect.h"
#include "GameplayEffectExecutionCalculation.generated.h"

struct FGameplayEffectSpec;
class UAbilitySystemComponent;

/** Struct representing parameters for a custom gameplay effect execution. Should not be held onto via reference, used just for the scope of the execution */
USTRUCT()
struct GAMEPLAYABILITIES_API FGameplayEffectCustomExecutionParameters
{
	GENERATED_USTRUCT_BODY()

public:

	// Constructors
	FGameplayEffectCustomExecutionParameters();
	FGameplayEffectCustomExecutionParameters(FGameplayEffectSpec& InOwningSpec, const TArray<FGameplayEffectExecutionScopedModifierInfo>& InScopedMods, UAbilitySystemComponent* InTargetAbilityComponent);

	/** Simple accessor to owning gameplay spec */
	const FGameplayEffectSpec& GetOwningSpec() const;

	/** Simple non-const accessor to owning gameplay spec */
	FGameplayEffectSpec& GetOwningSpec();

	/** Simple accessor to target ability system component */
	UAbilitySystemComponent* GetTargetAbilitySystemComponent() const;

	/** Simple accessor to source ability system component (could be null!) */
	UAbilitySystemComponent* GetSourcebilitySystemComponent() const;
	
	/**
	 * Attempts to calculate the magnitude of a captured attribute given the specified parameters. Can fail if the gameplay spec doesn't have
	 * a valid capture for the attribute.
	 * 
	 * @param InCaptureDef	Attribute definition to attempt to calculate the magnitude of
	 * @param InEvalParams	Parameters to evaluate the attribute under
	 * @param OutMagnitude	[OUT] Computed magnitude
	 * 
	 * @return True if the magnitude was successfully calculated, false if it was not
	 */
	bool AttemptCalculateCapturedAttributeMagnitude(const FGameplayEffectAttributeCaptureDefinition& InCaptureDef, const FAggregatorEvaluateParameters& InEvalParams, OUT float& OutMagnitude) const;
	
	/**
	 * Attempts to calculate the magnitude of a captured attribute given the specified parameters, including a starting base value. 
	 * Can fail if the gameplay spec doesn't have a valid capture for the attribute.
	 * 
	 * @param InCaptureDef	Attribute definition to attempt to calculate the magnitude of
	 * @param InEvalParams	Parameters to evaluate the attribute under
	 * @param InBaseValue	Base value to evaluate the attribute under
	 * @param OutMagnitude	[OUT] Computed magnitude
	 * 
	 * @return True if the magnitude was successfully calculated, false if it was not
	 */
	bool AttemptCalculateCapturedAttributeMagnitudeWithBase(const FGameplayEffectAttributeCaptureDefinition& InCaptureDef, const FAggregatorEvaluateParameters& InEvalParams, float InBaseValue, OUT float& OutMagnitude) const;

	/**
	 * Attempts to calculate the base value of a captured attribute given the specified parameters. Can fail if the gameplay spec doesn't have
	 * a valid capture for the attribute.
	 * 
	 * @param InCaptureDef	Attribute definition to attempt to calculate the base value of
	 * @param OutBaseValue	[OUT] Computed base value
	 * 
	 * @return True if the base value was successfully calculated, false if it was not
	 */
	bool AttemptCalculateCapturedAttributeBaseValue(const FGameplayEffectAttributeCaptureDefinition& InCaptureDef, OUT float& OutBaseValue) const;

	/**
	 * Attempts to calculate the bonus magnitude of a captured attribute given the specified parameters. Can fail if the gameplay spec doesn't have
	 * a valid capture for the attribute.
	 * 
	 * @param InCaptureDef		Attribute definition to attempt to calculate the bonus magnitude of
	 * @param InEvalParams		Parameters to evaluate the attribute under
	 * @param OutBonusMagnitude	[OUT] Computed bonus magnitude
	 * 
	 * @return True if the bonus magnitude was successfully calculated, false if it was not
	 */
	bool AttemptCalculateCapturedAttributeBonusMagnitude(const FGameplayEffectAttributeCaptureDefinition& InCaptureDef, const FAggregatorEvaluateParameters& InEvalParams, OUT float& OutBonusMagnitude) const;
	
	/**
	 * Attempts to populate the specified aggregator with a snapshot of a backing captured aggregator. Can fail if the gameplay spec doesn't have
	 * a valid capture for the attribute.
	 * 
	 * @param InCaptureDef				Attribute definition to attempt to snapshot
	 * @param OutSnapshottedAggregator	[OUT] Snapshotted aggregator, if possible
	 * 
	 * @return True if the aggregator was successfully snapshotted, false if it was not
	 */
	bool AttemptGetCapturedAttributeAggregatorSnapshot(const FGameplayEffectAttributeCaptureDefinition& InCaptureDef, OUT FAggregator& OutSnapshottedAggregator) const;

private:

	/** Mapping of capture definition to aggregator with scoped modifiers added in; Used to process scoped modifiers w/o modifying underlying aggregators in the capture */
	TMap<FGameplayEffectAttributeCaptureDefinition, FAggregator> ScopedModifierAggregators;

	/** Owning gameplay effect spec */
	FGameplayEffectSpec* OwningSpec;

	/** Target ability system component of the execution */
	TWeakObjectPtr<UAbilitySystemComponent> TargetAbilitySystemComponent;
};

UCLASS(BlueprintType, Blueprintable, Abstract)
class GAMEPLAYABILITIES_API UGameplayEffectExecutionCalculation : public UGameplayEffectCalculation
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITORONLY_DATA

protected:

	/** Any attribute in this list will not show up as a valid option for scoped modifiers; Used to allow attribute capture for internal calculation while preventing modification */
	UPROPERTY(EditDefaultsOnly, Category=Attributes)
	TArray<FGameplayEffectAttributeCaptureDefinition> InvalidScopedModifierAttributes;

public:
	/**
	 * Gets the collection of capture attribute definitions that the calculation class will accept as valid scoped modifiers
	 * 
	 * @param OutScopableModifiers	[OUT] Array to populate with definitions valid as scoped modifiers
	 */
	virtual void GetValidScopedModifierAttributeCaptureDefinitions(OUT TArray<FGameplayEffectAttributeCaptureDefinition>& OutScopableModifiers) const;

#endif // #if WITH_EDITORONLY_DATA

	/**
	 * Called whenever the owning gameplay effect is executed. Allowed to do essentially whatever is desired, including generating new
	 * modifiers to instantly execute as well.
	 * 
	 * @note: Native subclasses should override the auto-generated Execute_Implementation function and NOT this one.
	 * 
	 * @param ExecutionParams			Parameters for the custom execution calculation
	 * @param OutAdditionalModifiers	[OUT] Additional modifiers the custom execution has generated and would like executed upon the target
	 */
	UFUNCTION(BlueprintNativeEvent, Category="Calculation")
	void Execute(FGameplayEffectCustomExecutionParameters& ExecutionParams, TArray<FGameplayModifierEvaluatedData>& OutAdditionalModifiers) const;
};
