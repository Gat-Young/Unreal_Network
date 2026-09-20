#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "NetLobbySubsystem.generated.h"

class SWidget;
class SVerticalBox;
class SScrollBox;
class SEditableTextBox;
class FJsonObject;
class APlayerController;

struct FRegisteredServer
{
	FString Name;
	FString Ip;
	int32 Port = 7777;
	FString Map;
	int32 Players = 0;
	int32 MaxPlayers = 0;
};

/**
 * 클라이언트 측: 로그인 -> 레지스트리에서 서버 목록 수신 -> 접속, 접속 후에는 채팅 UI를 표시한다.
 * 테스트 자동화용 명령줄: -AutoUser= -AutoPass= -AutoJoin -AutoChat=메시지
 */
UCLASS()
class UNetLobbySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void Login(const FString& User, const FString& Password);
	void RefreshServers();
	void JoinServer(const FRegisteredServer& Server);
	void SendChat(const FString& Message);

private:
	using FResponseFn = TFunction<void(bool bOk, int32 Code, TSharedPtr<FJsonObject> Json)>;
	void Request(const FString& Verb, const FString& Path, const FString& Body, FResponseFn OnDone);

	void OnPostLoadMap(UWorld* World);
	void BuildUI(UWorld* World);
	void ClearUI();
	void BuildLobbyUI();
	void BuildChatUI();
	void RebuildServerList();
	void AddChatLine(const FString& Sender, const FString& Message);
	void OpenChat();
	void CloseChat();
	APlayerController* GetPC() const;
	bool TickAutoChat(float DeltaTime);

	FString Token;
	FString Username;
	FString StatusText;
	TArray<FRegisteredServer> Servers;

	TSharedPtr<SWidget> RootWidget;
	TSharedPtr<SVerticalBox> ServerListBox;
	TSharedPtr<SScrollBox> ChatScroll;
	TSharedPtr<SEditableTextBox> ChatInput;
	TSharedPtr<SEditableTextBox> UserBox;
	TSharedPtr<SEditableTextBox> PassBox;

	bool bChatOpen = false;
	bool bLobbyShown = false;
	bool bChatSent = false;
	FDelegateHandle PostLoadHandle, ChatRecvHandle, ChatOpenHandle;
	FTSTicker::FDelegateHandle UiTicker, AutoChatTicker;
};
