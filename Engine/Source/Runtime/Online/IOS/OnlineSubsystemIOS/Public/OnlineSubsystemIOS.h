// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OnlineSubsystem.h"
#include "OnlineSubsystemImpl.h"
#include "OnlineSubsystemIOSPackage.h"


/**
 *	OnlineSubsystemIOS - Implementation of the online subsystem for IOS services
 */
class ONLINESUBSYSTEMIOS_API FOnlineSubsystemIOS : 
	public FOnlineSubsystemImpl,
	public FTickerObjectBase
{

public:
	FOnlineSubsystemIOS();
	virtual ~FOnlineSubsystemIOS() {};

	// Begin IOnlineSubsystem Interface
	virtual IOnlineSessionPtr GetSessionInterface() const override;
	virtual IOnlineFriendsPtr GetFriendsInterface() const override;
	virtual IOnlineSharedCloudPtr GetSharedCloudInterface() const override;
	virtual IOnlineUserCloudPtr GetUserCloudInterface() const override;
	virtual IOnlineLeaderboardsPtr GetLeaderboardsInterface() const override;
	virtual IOnlineVoicePtr GetVoiceInterface() const  override;
	virtual IOnlineExternalUIPtr GetExternalUIInterface() const override;
	virtual IOnlineTimePtr GetTimeInterface() const override;
	virtual IOnlineIdentityPtr GetIdentityInterface() const override;
	virtual IOnlinePartyPtr GetPartyInterface() const override;
	virtual IOnlineTitleFilePtr GetTitleFileInterface() const override;
	virtual IOnlineEntitlementsPtr GetEntitlementsInterface() const override;
	virtual IOnlineStorePtr GetStoreInterface() const override;
	virtual IOnlineEventsPtr GetEventsInterface() const override;
	virtual IOnlineAchievementsPtr GetAchievementsInterface() const override;
	virtual IOnlineSharingPtr GetSharingInterface() const override;
	virtual IOnlineUserPtr GetUserInterface() const override;
	virtual IOnlineMessagePtr GetMessageInterface() const override;
	virtual IOnlinePresencePtr GetPresenceInterface() const override;
	virtual IOnlineChatPtr GetChatInterface() const override;
	virtual bool Init() override;
	virtual bool Shutdown() override;
	virtual FString GetAppId() const override;
	virtual bool Exec(class UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
	virtual bool Tick(float DeltaTime) override;
	// End IOnlineSubsystem Interface

PACKAGE_SCOPE:

	/**
	 * Is IOS available for use
	 * @return true if IOS is available, false otherwise
	 */
	bool IsEnabled();

	/**
	* Is IAP available for use
	* @return true if IAP should be available, false otherwise
	*/
	bool IsInAppPurchasingEnabled();

private:

	/** Online async task thread */
	class FRunnableThread* OnlineAsyncTaskThread;

	/** Interface to the session services */
	FOnlineSessionIOSPtr SessionInterface;

	/** Interface to the Identity information */
	FOnlineIdentityIOSPtr IdentityInterface;

	/** Interface to the friends services */
	FOnlineFriendsIOSPtr FriendsInterface;

	/** Interface to the profile information */
	FOnlineLeaderboardsIOSPtr LeaderboardsInterface;

	/** Interface to the online store */
	FOnlineStoreInterfaceIOSPtr StoreInterface;

	/** Interface to the online achievements */
	FOnlineAchievementsIOSPtr AchievementsInterface;

	/** Interface to the external UI services */
	FOnlineExternalUIIOSPtr ExternalUIInterface;
};

typedef TSharedPtr<FOnlineSubsystemIOS, ESPMode::ThreadSafe> FOnlineSubsystemIOSPtr;

