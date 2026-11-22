#include "QuadFootInteractionComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "QuadFootStepComponent.h"

namespace
{
	UObject* FindFootReceiver(const FHitResult& Hit)
	{
		UActorComponent* Component = Hit.GetComponent();
		if (IsValid(Component) && Component->Implements<UQuadFootInteractable>())
		{
			return Component;
		}
		AActor* Actor = Hit.GetActor();
		return IsValid(Actor) && Actor->Implements<UQuadFootInteractable>() ? Actor : nullptr;
	}
}

void UQuadFootInteractionComponent::ToggleGripFor(AActor* Quad)
{
	if (IsValid(Quad))
	{
		if (UQuadFootStepComponent* Steps = Quad->FindComponentByClass<UQuadFootStepComponent>())
		{
			Steps->ToggleFocusedGrip();
		}
	}
}

EQuadGripMode UQuadFootInteractionComponent::GetGripMode(const int32 LegID) const
{
	return Feet.IsValidIndex(LegID) ? Feet[LegID].GripMode : EQuadGripMode::None;
}

bool UQuadFootInteractionComponent::GetFootContact(const int32 LegID, FQuadFootContact& Contact) const
{
	if (Feet.IsValidIndex(LegID) && Feet[LegID].bContact)
	{
		Contact = Feet[LegID].Contact;
		return true;
	}
	Contact = FQuadFootContact();
	return false;
}

void UQuadFootInteractionComponent::CommitContact(const int32 LegID, const FHitResult& Hit)
{
	LeaveContact(LegID);
	FFoot& Foot = Feet[LegID];
	Foot.Contact.Quad = GetOwner();
	Foot.Contact.LegID = LegID;
	Foot.Contact.ContactID = ++NextContactID;
	Foot.Contact.Hit = Hit;
	Foot.ContactReceiver = FindFootReceiver(Hit);
	Foot.bContact = true;
	Foot.bNeedsLanding = false;
	const FQuadFootContact Event = Foot.Contact;
	TGuardValue<bool> DispatchGuard(bDispatching, true);
	if (UObject* Receiver = Foot.ContactReceiver.Get())
	{
		IQuadFootInteractable::Execute_FootLanded(Receiver, Event);
	}
	OnFootLanded.Broadcast(Event);
}

void UQuadFootInteractionComponent::LeaveContact(const int32 LegID)
{
	FFoot& Foot = Feet[LegID];
	if (!Foot.bContact)
	{
		return;
	}
	const FQuadFootContact Event = Foot.Contact;
	const TWeakObjectPtr<UObject> Receiver = Foot.ContactReceiver;
	Foot.bContact = false;
	Foot.ContactReceiver.Reset();
	Foot.Contact = FQuadFootContact();
	TGuardValue<bool> DispatchGuard(bDispatching, true);
	if (UObject* Target = Receiver.Get())
	{
		IQuadFootInteractable::Execute_FootLeft(Target, Event);
	}
	OnFootLeft.Broadcast(Event);
}

void UQuadFootInteractionComponent::BeginGrip(const int32 LegID, const FTransform& FootPose, USkeletalMeshComponent& Mesh)
{
	FFoot& Foot = Feet[LegID];
	UObject* Receiver = Foot.ContactReceiver.Get();
	if (!Foot.bContact || !Receiver)
	{
		return;
	}
	for (const FFoot& Other : Feet)
	{
		if (Other.GripReceiver.Get() == Receiver)
		{
			return;
		}
	}

	TGuardValue<bool> DispatchGuard(bDispatching, true);
	const int32 ID = ++NextGripID;
	const FQuadGrip Result = IQuadFootInteractable::Execute_TryBeginGrip(Receiver, Foot.Contact, ID);
	if (Result.Mode == EQuadGripMode::None || !IsValid(Receiver))
	{
		return;
	}
	if ((Result.Mode == EQuadGripMode::Anchored && !IsValid(Result.Anchor))
		|| (Result.Mode == EQuadGripMode::Carrying && !IsValid(Result.CarryBody)))
	{
		IQuadFootInteractable::Execute_EndGrip(Receiver, Foot.Contact, ID, EQuadGripEndReason::TargetLost);
		return;
	}

	Foot.GripMode = Result.Mode;
	Foot.GripID = ID;
	Foot.GripContact = Foot.Contact;
	Foot.GripReceiver = Receiver;
	Foot.Anchor = Result.Anchor;
	Foot.CarryBody = Result.CarryBody;
	Foot.GripTarget = FootPose;
	if (USceneComponent* Anchor = Foot.Anchor.Get())
	{
		Foot.FootInAnchor = FootPose.GetRelativeTransform(Anchor->GetComponentTransform());
	}
	if (Result.Mode == EQuadGripMode::Carrying && !AttachCarry(LegID, Mesh))
	{
		ReleaseGrip(LegID, EQuadGripEndReason::TargetLost);
		return;
	}
	OnGripChanged.Broadcast(LegID, ID, Foot.GripMode);
}

bool UQuadFootInteractionComponent::AttachCarry(const int32 LegID, USkeletalMeshComponent& Mesh)
{
	FFoot& Foot = Feet[LegID];
	UPrimitiveComponent* Body = Foot.CarryBody.Get();
	const FName Socket(*FString::Printf(TEXT("Foot%d"), LegID));
	if (!Body || !Mesh.DoesSocketExist(Socket))
	{
		return false;
	}
	Foot.CarryInFoot = Body->GetComponentTransform().GetRelativeTransform(Foot.GripTarget);
	Body->SetSimulatePhysics(false);
	return Body->AttachToComponent(&Mesh, FAttachmentTransformRules::KeepWorldTransform, Socket);
}

void UQuadFootInteractionComponent::DriveGrip(const int32 LegID, const FVector& Velocity, const float DeltaSeconds,
	USkeletalMeshComponent& Mesh)
{
	FFoot& Foot = Feet[LegID];
	if (UObject* Receiver = Foot.GripReceiver.Get(); Receiver && Foot.GripMode == EQuadGripMode::Anchored)
	{
		TGuardValue<bool> DispatchGuard(bDispatching, true);
		const EQuadGripMode Requested = IQuadFootInteractable::Execute_DriveGrip(Receiver, Foot.GripContact, Foot.GripID, Velocity, DeltaSeconds);
		if (Requested == EQuadGripMode::Carrying)
		{
			Foot.GripMode = Requested;
			if (AttachCarry(LegID, Mesh))
			{
				OnGripChanged.Broadcast(LegID, Foot.GripID, Requested);
			}
			else
			{
				ReleaseGrip(LegID, EQuadGripEndReason::TargetLost);
			}
		}
		else if (Requested == EQuadGripMode::None)
		{
			ReleaseGrip(LegID, EQuadGripEndReason::TargetLost);
		}
	}
}

void UQuadFootInteractionComponent::ReleaseGrip(const int32 LegID, const EQuadGripEndReason Reason)
{
	FFoot& Foot = Feet[LegID];
	if (Foot.GripMode == EQuadGripMode::None)
	{
		return;
	}
	if (UPrimitiveComponent* Body = Foot.CarryBody.Get(); Body && Foot.GripMode == EQuadGripMode::Carrying)
	{
		Body->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
	const FQuadFootContact Event = Foot.GripContact;
	const int32 ID = Foot.GripID;
	const TWeakObjectPtr<UObject> Receiver = Foot.GripReceiver;
	Foot.GripMode = EQuadGripMode::None;
	Foot.GripID = 0;
	Foot.GripReceiver.Reset();
	Foot.Anchor.Reset();
	Foot.CarryBody.Reset();
	Foot.GripContact = FQuadFootContact();
	Foot.bNeedsLanding = !Foot.bContact;
	TGuardValue<bool> DispatchGuard(bDispatching, true);
	if (UObject* Target = Receiver.Get())
	{
		IQuadFootInteractable::Execute_EndGrip(Target, Event, ID, Reason);
	}
	OnGripChanged.Broadcast(LegID, ID, EQuadGripMode::None);
}

void UQuadFootInteractionComponent::ResetFeet(const int32 Count)
{
	for (int32 Index = 0; Index < Feet.Num(); ++Index)
	{
		ReleaseGrip(Index, EQuadGripEndReason::Reset);
		LeaveContact(Index);
	}
	Feet.Reset(Count);
	Feet.SetNum(Count);
}

void UQuadFootInteractionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ResetFeet(0);
	Super::EndPlay(Reason);
}
