// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"
#include "Slate.h"
#include "Kismet2NameValidators.h"
#include "AnimGraphDefinitions.h"
#include "KismetEditorUtilities.h"
#include "KismetDebugUtilities.h"
#include "BlueprintEditorUtils.h"
#include "StructureEditorUtils.h"

#define LOCTEXT_NAMESPACE "KismetNameValidators"

//////////////////////////////////////////////////
// FNameValidatorFactory

TSharedPtr<INameValidatorInterface> FNameValidatorFactory::MakeValidator(UEdGraphNode* Node)
{
	TSharedPtr<INameValidatorInterface> Validator;
	 
	// create a name validator for the node
	Validator = Node->MakeNameValidator();

	check(Validator.IsValid());
	return Validator;
}


FString INameValidatorInterface::GetErrorString(const FString& Name, EValidatorResult ErrorCode)
{
	FString ErrorText;

	switch (ErrorCode)
	{
		case EmptyName:
			ErrorText = LOCTEXT("EmptyName_Error", "Name cannot be empty.").ToString();
			break;

		case AlreadyInUse:
			ErrorText = FString::Printf( *LOCTEXT("AlreadyInUse_Error", "\"%s\" is already in use.").ToString(), *Name);
			break;

		case ExistingName:
			ErrorText = LOCTEXT("ExistingName_Error", "Name cannot be the same as the existing name.").ToString();
			break;
	}
	return ErrorText;
}


EValidatorResult INameValidatorInterface::FindValidString(FString& InOutName)
{
	FString DesiredName = InOutName;
	FString NewName = DesiredName;
	int32 NameIndex = 1;

	while (true)
	{
		EValidatorResult VResult = IsValid(NewName, true);
		if (VResult == EValidatorResult::Ok)
		{
			InOutName = NewName;
			return NewName == DesiredName? EValidatorResult::Ok : EValidatorResult::AlreadyInUse;
		}

		NewName = FString::Printf(TEXT("%s_%d"), *DesiredName, NameIndex++);
	}
}


 bool INameValidatorInterface::BlueprintObjectNameIsUnique(class UBlueprint* Blueprint, const FName& Name)
 {
	 UObject* Obj = FindObject<UObject>(Blueprint, *Name.ToString());
	 
	 return (Obj == NULL)
		 ? true
		 : false;
 }

//////////////////////////////////////////////////
// FKismetNameValidator

 namespace BlueprintNameConstants
 {
	 int32 NameMaxLength = 100;
 }

FKismetNameValidator::FKismetNameValidator(const class UBlueprint* Blueprint, FName InExistingName/* = NAME_None*/)
{
	ExistingName = InExistingName;
	BlueprintObject = Blueprint;
	FBlueprintEditorUtils::GetClassVariableList(BlueprintObject, Names, true);
	FBlueprintEditorUtils::GetAllGraphNames(BlueprintObject, Names);
	FBlueprintEditorUtils::GetSCSVariableNameList(Blueprint, Names);
	FStructureEditorUtils::GetAllStructureNames(Blueprint, Names);
}

EValidatorResult FKismetNameValidator::IsValid(const FString& Name, bool /*bOriginal*/)
{
	// Converting a string that is too large for an FName will cause an assert, so verify the length
	if(Name.Len() >= NAME_SIZE)
	{
		return EValidatorResult::TooLong;
	}

	// If not defined in name table, not current graph name
	return IsValid( FName(*Name) );
}

EValidatorResult FKismetNameValidator::IsValid(const FName& Name, bool /* bOriginal */)
{
	EValidatorResult ValidatorResult = EValidatorResult::AlreadyInUse;

	if(Name == NAME_None)
	{
		ValidatorResult = EValidatorResult::EmptyName;
	}
	else if(Name == ExistingName)
	{
		ValidatorResult = EValidatorResult::Ok;
	}
	else if(Name.ToString().Len() > BlueprintNameConstants::NameMaxLength)
	{
		ValidatorResult = EValidatorResult::TooLong;
	}
	else if(Name != NAME_None || Name.ToString().Len() <= NAME_SIZE)
	{
		// If it is in the names list then it is already in use.
		if(!Names.Contains(Name))
		{
			UObject* ExistingObject = StaticFindObject(/*Class=*/ NULL, const_cast<UBlueprint*>(BlueprintObject), *Name.ToString(), true);
			ValidatorResult = (ExistingObject == NULL)? EValidatorResult::Ok : EValidatorResult::AlreadyInUse;
		}

		if(ValidatorResult == EValidatorResult::Ok)
		{
			TArray<UK2Node_LocalVariable*> EventNodes;
			FBlueprintEditorUtils::GetAllNodesOfClass(BlueprintObject, EventNodes);
			for (UK2Node_LocalVariable* LocalVariable : EventNodes)
			{
				if(LocalVariable->CustomVariableName == Name)
				{
					ValidatorResult = EValidatorResult::AlreadyInUse;
					break;
				}
			}
		}
	}
	
	return ValidatorResult;
}

//////////////////////////////////////////////////////////////////
// FStringSetNameValidator

EValidatorResult FStringSetNameValidator::IsValid(const FString& Name, bool bOriginal)
{
	if (Name.IsEmpty())
	{
		return EValidatorResult::EmptyName;
	}
	else if (Name == ExistingName)
	{
		return EValidatorResult::ExistingName;
	}
	else
	{
		return (Names.Contains(Name)) ? EValidatorResult::AlreadyInUse : EValidatorResult::Ok;
	}
}

EValidatorResult FStringSetNameValidator::IsValid(const FName& Name, bool bOriginal)
{
	return IsValid(Name.ToString(), bOriginal);
}

//////////////////////////////////////////////////////////////////
// FAnimStateTransitionNodeSharedRulesNameValidator
// this doesn't go to MakeValidator in factory, as it is validator for internal name in AnimStateTransitionNode
FAnimStateTransitionNodeSharedRulesNameValidator::FAnimStateTransitionNodeSharedRulesNameValidator(class UAnimStateTransitionNode* InStateTransitionNode)
	: FStringSetNameValidator(FString())
{
	TArray<UAnimStateTransitionNode*> Nodes;
	UAnimationStateMachineGraph* StateMachine = CastChecked<UAnimationStateMachineGraph>(InStateTransitionNode->GetOuter());

	StateMachine->GetNodesOfClass<UAnimStateTransitionNode>(Nodes);
	for (auto NodeIt = Nodes.CreateIterator(); NodeIt; ++NodeIt)
	{
		auto Node = *NodeIt;
		if(Node != InStateTransitionNode &&
		   Node->bSharedRules &&
		   Node->SharedRulesGuid != InStateTransitionNode->SharedRulesGuid) // store only those shared rules who have different guid
		{
			Names.Add(Node->SharedRulesName);
		}
	}
}

//////////////////////////////////////////////////////////////////
// FAnimStateTransitionNodeSharedCrossfadeNameValidator
// this doesn't go to MakeValidator in factory, as it is validator for internal name in AnimStateTransitionNode
FAnimStateTransitionNodeSharedCrossfadeNameValidator::FAnimStateTransitionNodeSharedCrossfadeNameValidator(class UAnimStateTransitionNode* InStateTransitionNode)
	: FStringSetNameValidator(FString())
{
	TArray<UAnimStateTransitionNode*> Nodes;
	UAnimationStateMachineGraph* StateMachine = CastChecked<UAnimationStateMachineGraph>(InStateTransitionNode->GetOuter());

	StateMachine->GetNodesOfClass<UAnimStateTransitionNode>(Nodes);
	for (auto NodeIt = Nodes.CreateIterator(); NodeIt; ++NodeIt)
	{
		auto Node = *NodeIt;
		if(Node != InStateTransitionNode &&
		   Node->bSharedCrossfade &&
		   Node->SharedCrossfadeGuid != InStateTransitionNode->SharedCrossfadeGuid) // store only those shared crossfade who have different guid
		{
			Names.Add(Node->SharedCrossfadeName);
		}
	}
}

////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE