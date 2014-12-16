// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.
#include "UnrealEd.h"

/** Notification class for asynchronous shader compiling. */
class FNavigationBuildingNotificationImpl
	: public FTickableEditorObject
{

public:

	FNavigationBuildingNotificationImpl()
	{
		bPreviouslyDetectedBuild = false;
		LastEnableTime = 0;
	}

	/** Starts the notification. */
	void BuildStarted();

	/** Ends the notification. */
	void BuildFinished();

protected:
	/** FTickableEditorObject interface */
	virtual void Tick(float DeltaTime);
	virtual bool IsTickable() const
	{
		return true;
	}
	virtual TStatId GetStatId() const override;

private:
	void ClearCompleteNotification();
	FText GetNotificationText() const;

	bool bPreviouslyDetectedBuild;
	double TimeOfStartedBuild;
	double TimeOfStoppedBuild;

	/** Tracks the last time the notification was started, used to avoid spamming. */
	double LastEnableTime;

	TWeakPtr<SNotificationItem> NavigationBuiltCompleteNotification;

	/** The source code symbol query in progress message */
	TWeakPtr<SNotificationItem> NavigationBuildNotificationPtr;
};
