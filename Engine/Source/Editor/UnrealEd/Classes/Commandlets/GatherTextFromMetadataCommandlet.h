// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once 
#include "Commandlets/Commandlet.h"
#include "GatherTextFromMetaDataCommandlet.generated.h"

/**
 *	UGatherTextFromMetaDataCommandlet: Localization commandlet that collects all text to be localized from generated metadata.
 */
UCLASS()
class UGatherTextFromMetaDataCommandlet : public UGatherTextCommandletBase
{
	GENERATED_UCLASS_BODY()

public:
	// Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	// End UCommandlet Interface

private:
	struct FGatherParameters
	{
		TArray<FString> InputKeys;
		TArray<FString> OutputNamespaces;
		TArray<FText> OutputKeys;
	};

private:
	void GatherTextFromUObjects(const TArray<FString>& IncludePaths, const TArray<FString>& ExcludePaths, const FGatherParameters& Arguments);
	void GatherTextFromUObject(UField* const Field, const FGatherParameters& Arguments);
};