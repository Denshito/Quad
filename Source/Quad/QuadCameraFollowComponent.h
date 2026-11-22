#pragma once

#include "Components/ActorComponent.h"
#include "QuadCameraFollowComponent.generated.h"

class USkeletalMeshComponent;
class USpringArmComponent;

UCLASS(meta = (BlueprintSpawnableComponent))
class QUAD_API UQuadCameraFollowComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UQuadCameraFollowComponent();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void UpdateTarget() const;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<USpringArmComponent> SpringArm;
};
