#include "QuadCameraFollowComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/SpringArmComponent.h"

namespace
{
	const FName PelvisBone(TEXT("Pelvis"));
}

UQuadCameraFollowComponent::UQuadCameraFollowComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UQuadCameraFollowComponent::BeginPlay()
{
	Super::BeginPlay();

	Mesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
	SpringArm = GetOwner()->FindComponentByClass<USpringArmComponent>();
	UCameraComponent* Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
	if (!Mesh || !SpringArm || !Camera || Mesh->GetBoneIndex(PelvisBone) == INDEX_NONE ||
		Camera->GetAttachParent() != SpringArm)
	{
		SetComponentTickEnabled(false);
		return;
	}

	SpringArm->SocketOffset.Y = 0.0;
	SpringArm->SocketOffset.Z = 0.0;
	Camera->SetRelativeLocation(FVector(Camera->GetRelativeLocation().X, 0.0, 0.0));
	UpdateTarget();
	// Seed the spring arm's lag history at the pelvis before its first frame.
	SpringArm->ReregisterComponent();
	AddTickPrerequisiteComponent(Mesh);
	SpringArm->AddTickPrerequisiteComponent(this);
}

void UQuadCameraFollowComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateTarget();
}

void UQuadCameraFollowComponent::UpdateTarget() const
{
	SpringArm->TargetOffset = Mesh->GetSocketLocation(PelvisBone) - SpringArm->GetComponentLocation();
}
