#include "NetLobbySubsystem.h"
#include "NetRegistrySettings.h"
#include "NetChatAgent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogNetLobby, Log, All);

#define LOBBY_FONT(Size) FCoreStyle::GetDefaultFontStyle("Regular", Size)

bool UNetLobbySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && !IsRunningCommandlet();
}

void UNetLobbySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UNetLobbySubsystem::OnPostLoadMap);
	ChatRecvHandle = ANetChatAgent::OnChatReceived.AddUObject(this, &UNetLobbySubsystem::AddChatLine);
	ChatOpenHandle = ANetChatAgent::OnChatOpenRequested.AddUObject(this, &UNetLobbySubsystem::OpenChat);
}

void UNetLobbySubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	ANetChatAgent::OnChatReceived.Remove(ChatRecvHandle);
	ANetChatAgent::OnChatOpenRequested.Remove(ChatOpenHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(UiTicker);
	FTSTicker::GetCoreTicker().RemoveTicker(AutoChatTicker);
	ClearUI();
	Super::Deinitialize();
}

// ---------------------------------------------------------------- HTTP

void UNetLobbySubsystem::Request(const FString& Verb, const FString& Path, const FString& Body, FResponseFn OnDone)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(UNetRegistrySettings::GetRegistryUrl() + Path);
	Req->SetVerb(Verb);
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	if (!Token.IsEmpty())
	{
		Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Token);
	}
	if (!Body.IsEmpty())
	{
		Req->SetContentAsString(Body);
	}
	TWeakObjectPtr<UNetLobbySubsystem> WeakThis(this);
	Req->OnProcessRequestComplete().BindLambda([WeakThis, OnDone](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		if (!WeakThis.IsValid())
		{
			return;
		}
		TSharedPtr<FJsonObject> Json;
		int32 Code = 0;
		if (bOk && Resp.IsValid())
		{
			Code = Resp->GetResponseCode();
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Resp->GetContentAsString()), Json);
		}
		OnDone(bOk && Resp.IsValid(), Code, Json);
	});
	Req->ProcessRequest();
}

void UNetLobbySubsystem::Login(const FString& User, const FString& Password)
{
	if (User.IsEmpty() || Password.IsEmpty())
	{
		StatusText = TEXT("Enter username and password.");
		return;
	}
	StatusText = TEXT("Logging in...");
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("username"), User);
	Json->SetStringField(TEXT("password"), Password);
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));

	Request(TEXT("POST"), TEXT("/api/login"), Body, [this, User](bool bOk, int32 Code, TSharedPtr<FJsonObject> Resp)
	{
		FString NewToken;
		if (bOk && Code == 200 && Resp.IsValid() && Resp->TryGetStringField(TEXT("token"), NewToken))
		{
			Token = NewToken;
			Username = User;
			ANetChatAgent::LocalUserName = User;
			UE_LOG(LogNetLobby, Log, TEXT("Login OK: %s"), *User);
			StatusText = TEXT("Logged in. Fetching servers...");
			RefreshServers();
		}
		else
		{
			UE_LOG(LogNetLobby, Warning, TEXT("Login failed (ok=%d code=%d)"), bOk, Code);
			StatusText = bOk ? TEXT("Login failed: invalid credentials.") : TEXT("Cannot reach registry web server.");
		}
	});
}

void UNetLobbySubsystem::RefreshServers()
{
	Request(TEXT("GET"), TEXT("/api/servers"), FString(), [this](bool bOk, int32 Code, TSharedPtr<FJsonObject> Resp)
	{
		if (!bOk || Code != 200 || !Resp.IsValid())
		{
			StatusText = Code == 401 ? TEXT("Session expired. Log in again.") : TEXT("Failed to fetch server list.");
			if (Code == 401)
			{
				Token.Empty();
			}
			return;
		}
		Servers.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Resp->TryGetArrayField(TEXT("servers"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (V->TryGetObject(O))
				{
					FRegisteredServer S;
					(*O)->TryGetStringField(TEXT("name"), S.Name);
					(*O)->TryGetStringField(TEXT("ip"), S.Ip);
					(*O)->TryGetStringField(TEXT("map"), S.Map);
					(*O)->TryGetNumberField(TEXT("port"), S.Port);
					(*O)->TryGetNumberField(TEXT("players"), S.Players);
					(*O)->TryGetNumberField(TEXT("maxPlayers"), S.MaxPlayers);
					Servers.Add(S);
					UE_LOG(LogNetLobby, Log, TEXT("Server: %s %s:%d"), *S.Name, *S.Ip, S.Port);
				}
			}
		}
		StatusText = FString::Printf(TEXT("%d server(s) found."), Servers.Num());
		RebuildServerList();
		if (FParse::Param(FCommandLine::Get(), TEXT("AutoJoin")))
		{
			if (Servers.Num() > 0)
			{
				JoinServer(Servers[0]);
			}
			else
			{
				// 서버가 아직 등록되지 않았을 수 있으므로 자동 접속 모드에서는 재시도
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float) { RefreshServers(); return false; }), 3.0f);
			}
		}
	});
}

void UNetLobbySubsystem::JoinServer(const FRegisteredServer& Server)
{
	APlayerController* PC = GetPC();
	if (!PC)
	{
		return;
	}
	const FString Url = FString::Printf(TEXT("%s:%d?Name=%s"), *Server.Ip, Server.Port, *Username);
	UE_LOG(LogNetLobby, Log, TEXT("Joining %s"), *Url);
	StatusText = TEXT("Connecting to ") + Server.Ip + TEXT("...");
	PC->ClientTravel(Url, TRAVEL_Absolute);
}

void UNetLobbySubsystem::SendChat(const FString& Message)
{
	if (ANetChatAgent* Agent = ANetChatAgent::GetLocalAgent(GetGameInstance()->GetWorld()))
	{
		Agent->ServerSendChat(Message);
	}
}

// ---------------------------------------------------------------- UI

APlayerController* UNetLobbySubsystem::GetPC() const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return World ? World->GetFirstPlayerController() : nullptr;
}

void UNetLobbySubsystem::OnPostLoadMap(UWorld* World)
{
	if (!World || !World->IsGameWorld() || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	ClearUI();
	FTSTicker::GetCoreTicker().RemoveTicker(UiTicker);
	TWeakObjectPtr<UWorld> WeakWorld(World);
	UiTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, WeakWorld](float)
	{
		if (WeakWorld.IsValid())
		{
			BuildUI(WeakWorld.Get());
		}
		return false;
	}), 1.0f);
}

void UNetLobbySubsystem::BuildUI(UWorld* World)
{
	if (World->GetNetMode() == NM_Client)
	{
		BuildChatUI();
		FString AutoChat;
		if (FParse::Value(FCommandLine::Get(), TEXT("AutoChat="), AutoChat) && !bChatSent)
		{
			FTSTicker::GetCoreTicker().RemoveTicker(AutoChatTicker);
			AutoChatTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UNetLobbySubsystem::TickAutoChat), 1.0f);
		}
		return;
	}

	FString AutoUser, AutoPass;
	FParse::Value(FCommandLine::Get(), TEXT("AutoUser="), AutoUser);
	FParse::Value(FCommandLine::Get(), TEXT("AutoPass="), AutoPass);

	BuildLobbyUI();
	if (!AutoUser.IsEmpty() && !AutoPass.IsEmpty() && Token.IsEmpty())
	{
		Login(AutoUser, AutoPass);
	}
	else if (!Token.IsEmpty())
	{
		RefreshServers();
	}
}

bool UNetLobbySubsystem::TickAutoChat(float)
{
	FString AutoChat;
	FParse::Value(FCommandLine::Get(), TEXT("AutoChat="), AutoChat);
	if (ANetChatAgent::GetLocalAgent(GetGameInstance()->GetWorld()))
	{
		SendChat(AutoChat);
		bChatSent = true;
		return false;
	}
	return true;
}

void UNetLobbySubsystem::ClearUI()
{
	if (RootWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(RootWidget.ToSharedRef());
	}
	if (bLobbyShown)
	{
		if (APlayerController* PC = GetPC())
		{
			PC->SetShowMouseCursor(false);
			PC->SetInputMode(FInputModeGameOnly());
		}
	}
	RootWidget.Reset();
	ServerListBox.Reset();
	ChatScroll.Reset();
	ChatInput.Reset();
	UserBox.Reset();
	PassBox.Reset();
	bChatOpen = false;
	bLobbyShown = false;
}

void UNetLobbySubsystem::BuildLobbyUI()
{
	if (!GEngine || !GEngine->GameViewport)
	{
		return;
	}
	bLobbyShown = true;
	auto LoggedOut = [this]() { return Token.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; };
	auto LoggedIn = [this]() { return Token.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; };

	RootWidget = SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SBox).WidthOverride(520)
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.04f, 0.9f)).Padding(20)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Unreal Network Lobby"))).Font(LOBBY_FONT(24))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SVerticalBox).Visibility_Lambda(LoggedOut)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)[ SAssignNew(UserBox, SEditableTextBox).HintText(FText::FromString(TEXT("Username"))) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)[ SAssignNew(PassBox, SEditableTextBox).IsPassword(true).HintText(FText::FromString(TEXT("Password (new name = sign up)"))) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
					[
						SNew(SButton).Text(FText::FromString(TEXT("Login"))).HAlign(HAlign_Center)
						.OnClicked_Lambda([this]()
						{
							Login(UserBox->GetText().ToString(), PassBox->GetText().ToString());
							return FReply::Handled();
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SVerticalBox).Visibility_Lambda(LoggedIn)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
					[
						SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("Servers  (logged in as %s)"), *Username)); }).Font(LOBBY_FONT(14))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)[ SAssignNew(ServerListBox, SVerticalBox) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
					[
						SNew(SButton).Text(FText::FromString(TEXT("Refresh"))).HAlign(HAlign_Center)
						.OnClicked_Lambda([this]() { RefreshServers(); return FReply::Handled(); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
				[
					SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(StatusText); }).ColorAndOpacity(FLinearColor(1.f, 0.85f, 0.4f))
				]
			]
		]
	];
	GEngine->GameViewport->AddViewportWidgetContent(RootWidget.ToSharedRef(), 100);

	if (APlayerController* PC = GetPC())
	{
		PC->SetShowMouseCursor(true);
		FInputModeUIOnly Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	RebuildServerList();
}

void UNetLobbySubsystem::RebuildServerList()
{
	if (!ServerListBox.IsValid())
	{
		return;
	}
	ServerListBox->ClearChildren();
	if (Servers.Num() == 0)
	{
		ServerListBox->AddSlot().AutoHeight()[ SNew(STextBlock).Text(FText::FromString(TEXT("(no registered servers)"))) ];
	}
	for (const FRegisteredServer& S : Servers)
	{
		const FRegisteredServer Copy = S;
		ServerListBox->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SButton).HAlign(HAlign_Left)
			.OnClicked_Lambda([this, Copy]() { JoinServer(Copy); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s   %s:%d   [%s]   %d/%d  - Join"), *S.Name, *S.Ip, S.Port, *S.Map, S.Players, S.MaxPlayers)))
			]
		];
	}
}

void UNetLobbySubsystem::BuildChatUI()
{
	if (!GEngine || !GEngine->GameViewport)
	{
		return;
	}
	RootWidget = SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(20)
	[
		SNew(SBox).WidthOverride(560).HeightOverride(260)
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.45f)).Padding(8)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().FillHeight(1.f)[ SAssignNew(ChatScroll, SScrollBox) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SAssignNew(ChatInput, SEditableTextBox)
					.HintText(FText::FromString(TEXT("Type a message, Enter to send")))
					.Visibility_Lambda([this]() { return bChatOpen ? EVisibility::Visible : EVisibility::Collapsed; })
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Type)
					{
						if (Type == ETextCommit::OnEnter && !Text.IsEmpty())
						{
							SendChat(Text.ToString());
						}
						ChatInput->SetText(FText::GetEmpty());
						CloseChat();
					})
				]
			]
		]
	];
	GEngine->GameViewport->AddViewportWidgetContent(RootWidget.ToSharedRef(), 100);
	AddChatLine(TEXT("System"), TEXT("Connected. Press Enter to chat."));
}

void UNetLobbySubsystem::AddChatLine(const FString& Sender, const FString& Message)
{
	if (!ChatScroll.IsValid())
	{
		return;
	}
	ChatScroll->AddSlot()
	[
		SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(FString::Printf(TEXT("%s: %s"), *Sender, *Message)))
	];
	ChatScroll->ScrollToEnd();
}

void UNetLobbySubsystem::OpenChat()
{
	if (!ChatInput.IsValid() || bChatOpen)
	{
		return;
	}
	bChatOpen = true;
	if (APlayerController* PC = GetPC())
	{
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(ChatInput);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	FSlateApplication::Get().SetKeyboardFocus(ChatInput);
}

void UNetLobbySubsystem::CloseChat()
{
	bChatOpen = false;
	if (APlayerController* PC = GetPC())
	{
		PC->SetInputMode(FInputModeGameOnly());
	}
}
