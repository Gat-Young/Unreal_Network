#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "ServerRegistrySubsystem.generated.h"

/** 데디케이티드 서버가 시작되면 레지스트리 웹서버에 자신의 주소를 등록하고 주기적으로 하트비트를 보낸다. */
UCLASS()
class UServerRegistrySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaTime);
	void Post(const FString& Path);

	FTSTicker::FDelegateHandle TickHandle;
};
