#pragma once

#include "Components/ActorComponent.h"
#include "QuadSelectedLegComponent.generated.h"

class UQuadFootStepComponent;
class ULineBatchComponent;
class USkeletalMeshComponent;

UCLASS(meta = (BlueprintSpawnableComponent))
class QUAD_API UQuadSelectedLegComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UQuadSelectedLegComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> Lines;

	UPROPERTY(Transient)
	TObjectPtr<UQuadFootStepComponent> FootStep;
};
