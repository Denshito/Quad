#include "QuadFootStepComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Math/RotationMatrix.h"
#include "QuadIKMathLibrary.h"
#include "QuadFootInteractionComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogQuadFootStep, Log, All);

namespace
{
	// Offset from the foot bone to the center of the sole in the robot's reference mesh, in cm.
	const FVector SoleOffsets[] = {
		{5.24, 3.60, -23.08}, {4.16, -4.42, -22.41},
		{-5.33, -3.52, -22.77}, {-4.14, 4.70, -21.23}
	};
	constexpr float SoleSweepRadius = 6.0f;
	constexpr float MinHeadingSpeed = 5.0f;
	constexpr float VerticalSpeed = 210.0f;
	constexpr float BodyPreparationSpeed = 90.0f;
	constexpr double PathSampleSpacing = 2.0;
	const FName PelvisSocket(TEXT("Pelvis"));

	FQuat ContactFrame(const FVector& Heading, const FVector& Normal)
	{
		FVector Forward = FVector::VectorPlaneProject(Heading, Normal).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			FVector Other;
			Normal.FindBestAxisVectors(Forward, Other);
		}
		return FRotationMatrix::MakeFromXZ(Forward, Normal).ToQuat();
	}

	FVector SearchDirection(const FVector& Inward, const FVector& Normal, const int32 Index)
	{
		const int32 Angles[] = {0, 45, -45, 90, -90, 135, -135, 180};
		return Inward.RotateAngleAxis(Angles[Index], Normal);
	}
}

bool UQuadFootStepComponent::InitializeFootSteps(USkeletalMeshComponent* SkeletalMesh)
{
	if (Interaction && Interaction->bDispatching)
	{
		return false;
	}
	Interaction = GetOwner()->FindComponentByClass<UQuadFootInteractionComponent>();
	if (Interaction)
	{
		Interaction->ResetFeet(0);
	}
	Mesh = SkeletalMesh;
	Legs.Reset();
	ActiveLegID = FocusedLegID = LastRequestedID = INDEX_NONE;
	Phase = EQuadFootPhase::Planted;
	AimInput = FVector2D::ZeroVector;
	ConfirmRequested = CandidateValid = false;
	bFootAirborne = false;
	LiftRoute.Reset();
	BodyPreparationOffset = FVector::ZeroVector;
	LiftDiagnostic.Reset();
	if (!Mesh)
	{
		return false;
	}

	const FVector MeshUp = Mesh->GetUpVector();
	const FName FootTips[] = {TEXT("foot_tip_lu"), TEXT("foot_tip_ld"), TEXT("foot_tip_rd"), TEXT("foot_tip_ru")};
	const FTransform MeshTransform = Mesh->GetComponentTransform();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SoleOffsets); ++Index)
	{
		const FName Hip(*FString::Printf(TEXT("Hip%d"), Index));
		const FName Knee(*FString::Printf(TEXT("Knee%d"), Index));
		const FName Foot(*FString::Printf(TEXT("Foot%d"), Index));
		if (!Mesh->DoesSocketExist(Hip) || !Mesh->DoesSocketExist(Knee) || !Mesh->DoesSocketExist(Foot))
		{
			Legs.Reset();
			return false;
		}

		const FTransform FootWorld = Mesh->GetSocketTransform(Foot);
		const FVector HipLocation = Mesh->GetSocketLocation(Hip);
		const FVector KneeLocation = Mesh->GetSocketLocation(Knee);
		FLeg& Leg = Legs.AddDefaulted_GetRef();
		Leg.HipLocal = MeshTransform.InverseTransformPosition(HipLocation);
		Leg.RadialLocal = MeshTransform.InverseTransformVectorNoScale(FootWorld.GetLocation() - HipLocation);
		Leg.UpperLength = FVector::Dist(HipLocation, KneeLocation);
		Leg.LowerLength = FVector::Dist(KneeLocation, FootWorld.GetLocation());
		Leg.LocalSole = Mesh->GetSocketTransform(Foot, RTS_Component).InverseTransformVector(SoleOffsets[Index]);
		const FVector RestSole = FootWorld.TransformPosition(Leg.LocalSole);
		Leg.NominalContact = MeshTransform.InverseTransformPosition(RestSole);
		Leg.Planted = FootWorld;
		Leg.Contact = RestSole;
		Leg.Normal = MeshUp;
		Heading = Mesh->DoesSocketExist(FootTips[Index])
			? Mesh->GetSocketLocation(FootTips[Index]) - FootWorld.GetLocation()
			: MeshTransform.TransformVectorNoScale(Leg.RadialLocal);
		if (Heading.IsNearlyZero())
		{
			Heading = Mesh->GetForwardVector();
		}
		Leg.FootToContact = ContactFrame(Heading, MeshUp).Inverse() * FootWorld.GetRotation();

		FHitResult Hit;
		if (ProbeSurface(RestSole, MeshUp, Hit))
		{
			Leg.Contact = Hit.ImpactPoint;
			Leg.Normal = Hit.ImpactNormal;
			Leg.Planted = MakeContactPose(Leg, Leg.Contact, Leg.Normal);
		}
	}
	if (Interaction)
	{
		Interaction->ResetFeet(Legs.Num());
	}
	return true;
}

void UQuadFootStepComponent::RequestFootSelection(const int32 RequestedID)
{
	if (Interaction && Interaction->bDispatching)
	{
		return;
	}
	if (LastRequestedID == RequestedID)
	{
		return;
	}
	LastRequestedID = RequestedID;
	if (Phase != EQuadFootPhase::Planted || !Legs.IsValidIndex(RequestedID))
	{
		return;
	}

	FocusedLegID = RequestedID;
	AimInput = FVector2D::ZeroVector;
	if (Interaction && Interaction->GetGripMode(RequestedID) != EQuadGripMode::None)
	{
		ActiveLegID = INDEX_NONE;
		return;
	}
	if (Interaction && Interaction->Feet[RequestedID].bNeedsLanding)
	{
		BeginGripRecovery(RequestedID);
		return;
	}
	ActiveLegID = RequestedID;
	const FLeg& Leg = Legs[ActiveLegID];
	SwingPose = Leg.Planted;
	LiftNormal = Leg.Normal;
	Heading = (Leg.Planted.GetRotation() * Leg.FootToContact.Inverse()).GetForwardVector();
	AimInput = FVector2D::ZeroVector;
	ConfirmRequested = CandidateValid = false;
	LandingApproach = AimBlocked = false;
	bFootAirborne = bStepUpRoute = bTriedStepUpCorrection = false;
	BodyPreparationOffset = FVector::ZeroVector;
	LiftRoute.Reset();
	LiftDiagnostic.Reset();
	Phase = EQuadFootPhase::Raising;
}

void UQuadFootStepComponent::SetFootAimInput(const FVector2D Input)
{
	if (Interaction && Interaction->bDispatching)
	{
		return;
	}
	const bool bWasCentered = AimInput.IsNearlyZero();
	// Keep the vertical control convention of the original foot-aim Blueprint.
	AimInput = UQuadIKMathLibrary::ApplyRadialDeadZone(FVector2D(Input.X, -Input.Y));
	if (AimInput.IsNearlyZero())
	{
		bTriedStepUpCorrection = false;
	}
	if (bWasCentered && !AimInput.IsNearlyZero() && Phase == EQuadFootPhase::Planted && Legs.IsValidIndex(ActiveLegID))
	{
		LiftRoute.Reset();
		LiftDiagnostic.Reset();
		Phase = EQuadFootPhase::Raising;
	}
}

int32 UQuadFootStepComponent::GetActiveLegID() const
{
	if (ActiveLegID == INDEX_NONE && Interaction && Interaction->GetGripMode(FocusedLegID) != EQuadGripMode::None)
	{
		return FocusedLegID;
	}
	return ActiveLegID;
}

void UQuadFootStepComponent::ToggleFocusedGrip()
{
	if (!Interaction || Interaction->bDispatching || Phase != EQuadFootPhase::Planted || !Legs.IsValidIndex(FocusedLegID))
	{
		return;
	}
	if (Interaction->GetGripMode(FocusedLegID) == EQuadGripMode::None)
	{
		Interaction->BeginGrip(FocusedLegID, Legs[FocusedLegID].Planted, *Mesh);
		if (Interaction->GetGripMode(FocusedLegID) != EQuadGripMode::None)
		{
			ActiveLegID = INDEX_NONE;
		}
	}
	else
	{
		Interaction->ReleaseGrip(FocusedLegID, EQuadGripEndReason::Released);
		if (Interaction->Feet[FocusedLegID].bNeedsLanding)
		{
			BeginGripRecovery(FocusedLegID);
		}
	}
	AimInput = FVector2D::ZeroVector;
}

void UQuadFootStepComponent::BeginGripRecovery(const int32 LegID)
{
	const FLeg& Leg = Legs[LegID];
	ActiveLegID = LegID;
	SwingPose = Interaction->Feet[LegID].GripTarget;
	LiftNormal = Leg.Normal;
	Heading = (SwingPose.GetRotation() * Leg.FootToContact.Inverse()).GetForwardVector();
	AimStartSole = SwingPose.TransformPosition(Leg.LocalSole);
	AimStartSurfaceHeight = 0.0;
	BodyPreparationOffset = FVector::ZeroVector;
	ConfirmRequested = CandidateValid = AimBlocked = false;
	bFootAirborne = true;
	Phase = EQuadFootPhase::Aiming;
}

FVector UQuadFootStepComponent::GetAimVelocity(const FVector& Normal, const float MoveSpeed) const
{
	if (AimInput.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	FRotator ViewRotation = Mesh->GetComponentRotation();
	if (Controller)
	{
		FVector ViewLocation;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	const FRotationMatrix View(ViewRotation);
	FVector U, V;
	if (UQuadIKMathLibrary::BuildSurfaceBasis(Normal, View.GetUnitAxis(EAxis::Y), View.GetUnitAxis(EAxis::Z),
		Mesh->GetRightVector(), U, V))
	{
		return (U * AimInput.X + V * AimInput.Y) * MoveSpeed;
	}
	return FVector::ZeroVector;
}

void UQuadFootStepComponent::AdvanceGrips(const float DeltaSeconds, const float MoveSpeed, const float MinBend, const float MaxBend)
{
	if (!Interaction || Interaction->bDispatching)
	{
		return;
	}
	const FTransform BodyPose = Mesh->GetComponentTransform();
	for (int32 Index = 0; Index < Legs.Num(); ++Index)
	{
		const FLeg& Leg = Legs[Index];
		auto& Foot = Interaction->Feet[Index];
		if (Foot.bContact && !Foot.Contact.Hit.GetComponent())
		{
			Interaction->LeaveContact(Index);
		}
		if (Foot.GripMode == EQuadGripMode::None)
		{
			continue;
		}

		UObject* Receiver = Foot.GripReceiver.Get();
		USceneComponent* Anchor = Foot.Anchor.Get();
		UPrimitiveComponent* CarryBody = Foot.CarryBody.Get();
		const bool bAnchored = Foot.GripMode == EQuadGripMode::Anchored;
		FTransform Next = bAnchored && Anchor ? Foot.FootInAnchor * Anchor->GetComponentTransform() : Foot.GripTarget;
		const FVector OldSole = Foot.GripTarget.TransformPosition(Leg.LocalSole);
		const AActor* GripOwner = bAnchored && Anchor ? Anchor->GetOwner() : CarryBody ? CarryBody->GetOwner() : nullptr;
		const bool bLost = !Receiver || (bAnchored ? !Anchor : !CarryBody);
		if (bLost || !IsReachable(Leg, BodyPose, Next.GetLocation(), MinBend, MaxBend)
			|| !CanMoveSole(OldSole, Next.TransformPosition(Leg.LocalSole), Leg.Normal, GripOwner))
		{
			Interaction->LeaveContact(Index);
			Interaction->ReleaseGrip(Index, bLost ? EQuadGripEndReason::TargetLost : EQuadGripEndReason::Unreachable);
			if (FocusedLegID == Index && Phase == EQuadFootPhase::Planted)
			{
				BeginGripRecovery(Index);
			}
			continue;
		}
		Foot.GripTarget = Next;

		const bool bDriving = FocusedLegID == Index && Phase == EQuadFootPhase::Planted;
		FVector Velocity = bDriving ? GetAimVelocity(Leg.Normal, MoveSpeed) : FVector::ZeroVector;
		if (bAnchored)
		{
			if (!Velocity.IsNearlyZero())
			{
				TGuardValue<bool> DispatchGuard(Interaction->bDispatching, true);
				const FVector Axis = IQuadFootInteractable::Execute_GetGripMotionAxis(Receiver, Foot.GripContact,
					Foot.GripID, Next.TransformPosition(Leg.LocalSole)).GetSafeNormal();
				if (!Axis.IsNearlyZero())
				{
					Velocity = Axis * FVector::DotProduct(Velocity, Axis);
				}
				const FVector Offset = Velocity * DeltaSeconds;
				const FVector Sole = Next.TransformPosition(Leg.LocalSole);
				if (!IsValid(Receiver) || !IsReachable(Leg, BodyPose, Next.GetLocation() + Offset, MinBend, MaxBend)
					|| !CanMoveSole(Sole, Sole + Offset, Leg.Normal, GripOwner))
				{
					Velocity = FVector::ZeroVector;
				}
			}
			Interaction->DriveGrip(Index, Velocity, DeltaSeconds, *Mesh);
		}
		else if (!Velocity.IsNearlyZero())
		{
			Next.AddToTranslation(Velocity * DeltaSeconds);
			const FTransform OldItem = Foot.CarryInFoot * Foot.GripTarget;
			const FTransform NewItem = Foot.CarryInFoot * Next;
			FComponentQueryParams Query(SCENE_QUERY_STAT(QuadCarry), GetOwner());
			Query.AddIgnoredActor(GripOwner);
			TArray<FHitResult> Hits;
			if (IsReachable(Leg, BodyPose, Next.GetLocation(), MinBend, MaxBend)
				&& CanMoveSole(OldSole, Next.TransformPosition(Leg.LocalSole), Leg.Normal, GripOwner)
				&& !GetWorld()->ComponentSweepMulti(Hits, CarryBody, OldItem.GetLocation(), NewItem.GetLocation(),
					NewItem.GetRotation(), Query))
			{
				Foot.GripTarget = Next;
			}
		}
		if (Foot.GripMode == EQuadGripMode::Carrying
			|| !Foot.GripTarget.TransformPosition(Leg.LocalSole).Equals(Leg.Contact, 0.1))
		{
			Interaction->LeaveContact(Index);
			if (Foot.GripMode == EQuadGripMode::None)
			{
				Foot.bNeedsLanding = true;
			}
		}
		if (Foot.bNeedsLanding && FocusedLegID == Index && Phase == EQuadFootPhase::Planted)
		{
			BeginGripRecovery(Index);
		}
	}
}

void UQuadFootStepComponent::ConfirmFootPlacement()
{
	if (Interaction && Interaction->bDispatching)
	{
		return;
	}
	if (Phase == EQuadFootPhase::Planted || Phase == EQuadFootPhase::PreparingLift
		|| (Phase == EQuadFootPhase::Raising && !bFootAirborne))
	{
		ActiveLegID = INDEX_NONE;
		Phase = EQuadFootPhase::Planted;
		LiftRoute.Reset();
		BodyPreparationOffset = FVector::ZeroVector;
	}
	ConfirmRequested = Phase == EQuadFootPhase::Aiming || Phase == EQuadFootPhase::Returning;
}

bool UQuadFootStepComponent::ProbeSurface(const FVector& Point, const FVector& Normal, FHitResult& Hit) const
{
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(QuadFootProbe), false, GetOwner());
	return GetWorld()->LineTraceSingleByChannel(Hit,
		Point + Normal * ProbeDistance, Point - Normal * ProbeDistance, ECC_Visibility, Query)
		&& !Hit.bStartPenetrating && FVector::DotProduct(Hit.ImpactNormal, Normal) >= 0.573576;
}

bool UQuadFootStepComponent::IsReachable(const FLeg& Leg, const FTransform& BodyPose, const FVector& Location,
	const float MinBend, const float MaxBend) const
{
	const FVector Up = BodyPose.GetRotation().GetUpVector();
	const FVector Radial = BodyPose.TransformVectorNoScale(Leg.RadialLocal);
	const FTransform HipFrame(ContactFrame(Radial, Up), BodyPose.TransformPosition(Leg.HipLocal));
	float KneeBend, HipYaw, HipPitch;
	return UQuadIKMathLibrary::ValidateTwoBoneReach(HipFrame, Location, Leg.UpperLength, Leg.LowerLength,
		MinBend, MaxBend, -75.0f, 75.0f, -80.0f, 80.0f, KneeBend, HipYaw, HipPitch);
}

bool UQuadFootStepComponent::CanMoveSole(const FVector& From, const FVector& To, const FVector& Normal,
	const AActor* IgnoredActor) const
{
	if (From.Equals(To, KINDA_SMALL_NUMBER))
	{
		return true;
	}
	const FVector SweepOffset = Normal * (SoleSweepRadius + 1.0f);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(QuadFootClearance), false, GetOwner());
	Query.AddIgnoredActor(IgnoredActor);
	return !GetWorld()->SweepTestByChannel(From + SweepOffset, To + SweepOffset,
		FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(SoleSweepRadius), Query);
}

FTransform UQuadFootStepComponent::MakeContactPose(const FLeg& Leg, const FVector& Contact, const FVector& Normal) const
{
	FTransform Pose(ContactFrame(Heading, Normal) * Leg.FootToContact, Contact, Leg.Planted.GetScale3D());
	Pose.AddToTranslation(-Pose.TransformVector(Leg.LocalSole));
	return Pose;
}

bool UQuadFootStepComponent::CanTraverseSole(const FLeg& Leg, const FTransform& BodyPose, const FVector& From,
	const FVector& To, const float MinBend, const float MaxBend, FString* Reason) const
{
	const FVector SoleOffset = SwingPose.TransformVector(Leg.LocalSole);
	const int32 Samples = FMath::Max(1, FMath::CeilToInt(FVector::Dist(From, To) / PathSampleSpacing));
	if (!IsReachable(Leg, BodyPose, To - SoleOffset, MinBend, MaxBend))
	{
		if (Reason)
		{
			*Reason = TEXT("Joint reach at target");
		}
		return false;
	}
	FVector Previous = From;
	for (int32 Sample = 1; Sample <= Samples; ++Sample)
	{
		const FVector Point = FMath::Lerp(From, To, static_cast<double>(Sample) / Samples);
		if (!IsReachable(Leg, BodyPose, Point - SoleOffset, MinBend, MaxBend))
		{
			if (Reason)
			{
				*Reason = TEXT("Joint reach along path");
			}
			return false;
		}
		if (!CanMoveSole(Previous, Point, LiftNormal))
		{
			if (Reason)
			{
				*Reason = TEXT("Foot sweep blocked");
			}
			return false;
		}
		Previous = Point;
	}
	return true;
}

bool UQuadFootStepComponent::FindLiftRoute(const FLeg& Leg, const FTransform& BodyPose, const float Clearance,
	const float MinBend, const float MaxBend, const FVector* StepUpGoal)
{
	const FVector Start = SwingPose.TransformPosition(Leg.LocalSole);
	if (!IsReachable(Leg, BodyPose, SwingPose.GetLocation(), MinBend, MaxBend))
	{
		if (LiftDiagnostic.IsEmpty())
		{
			LiftDiagnostic = TEXT("Joint reach at start");
		}
		return false;
	}
	const FVector PlanarStart = Leg.Contact + FVector::VectorPlaneProject(Start - Leg.Contact, LiftNormal);
	const FVector Hip = BodyPose.TransformPosition(Leg.HipLocal);
	const FVector Inward = ContactFrame(Hip - Start, LiftNormal).GetForwardVector();
	const auto CanTravel = [&](const FVector& From, const FVector& To, FString* Reason = nullptr)
	{
		return CanTraverseSole(Leg, BodyPose, From, To, MinBend, MaxBend, Reason);
	};
	FHitResult Surface;
	FString DirectFailure;
	if (StepUpGoal || ProbeSurface(PlanarStart, LiftNormal, Surface))
	{
		const FVector Direct = StepUpGoal ? *StepUpGoal : Surface.ImpactPoint + LiftNormal * Clearance;
		if (CanTravel(Start, Direct, &DirectFailure))
		{
			LiftRoute = {Start, Direct};
			return true;
		}
	}
	else
	{
		DirectFailure = TEXT("No supporting surface under start");
	}
	if (LiftDiagnostic.IsEmpty())
	{
		LiftDiagnostic = DirectFailure;
	}

	for (int32 Distance = 5; Distance <= 60; Distance += 5)
	{
		for (int32 Direction = 0; Direction < 8; ++Direction)
		{
			const FVector Offset = SearchDirection(Inward, LiftNormal, Direction) * Distance;
			if (!ProbeSurface(PlanarStart + Offset, LiftNormal, Surface))
			{
				continue;
			}
			FVector Goal = Surface.ImpactPoint + LiftNormal * Clearance;
			if (StepUpGoal)
			{
				Goal += LiftNormal * FMath::Max(0.0, FVector::DotProduct(*StepUpGoal - Goal, LiftNormal));
				if (CanTravel(Start, Goal) && CanTravel(Goal, *StepUpGoal))
				{
					LiftRoute = {Start, Goal, *StepUpGoal};
					return true;
				}
			}
			else if (FVector::DotProduct(Goal - Start, LiftNormal) >= 1.0)
			{
				if (CanTravel(Start, Goal))
				{
					LiftRoute = {Start, Goal};
					return true;
				}

				// Ease the foot sideways at low height before asking for full clearance.
				const FVector Low = Start + Offset * 0.5 + LiftNormal;
				if (IsReachable(Leg, BodyPose, Goal - SwingPose.TransformVector(Leg.LocalSole), MinBend, MaxBend)
					&& CanTravel(Start, Low) && CanTravel(Low, Goal))
				{
					LiftRoute = {Start, Low, Goal};
					return true;
				}
			}
		}
	}
	return false;
}

bool UQuadFootStepComponent::CanPrepareBody(const FTransform& From, const FTransform& To,
	const float MinBend, const float MaxBend) const
{
	const FVector PelvisLocal = Mesh->GetSocketTransform(PelvisSocket, RTS_Component).GetLocation();
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(QuadBodyPreparation), false, GetOwner());
	// The torso sweep complements the per-leg constraints while all four feet are planted.
	constexpr float TorsoSweepRadius = 35.0f;
	if (GetWorld()->SweepTestByChannel(From.TransformPosition(PelvisLocal), To.TransformPosition(PelvisLocal),
		FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(TorsoSweepRadius), Query))
	{
		return false;
	}

	const int32 Samples = FMath::Max(1, FMath::CeilToInt(FVector::Dist(From.GetLocation(), To.GetLocation()) / PathSampleSpacing));
	FTransform SamplePose = From;
	for (int32 Sample = 1; Sample <= Samples; ++Sample)
	{
		SamplePose.SetLocation(FMath::Lerp(From.GetLocation(), To.GetLocation(), static_cast<double>(Sample) / Samples));
		for (int32 Index = 0; Index < Legs.Num(); ++Index)
		{
			FTransform Target;
			FVector Contact, Normal;
			float Weight;
			bool bControlling;
			GetLegPose(Index, Target, Contact, Normal, Weight, bControlling);
			if (!IsReachable(Legs[Index], SamplePose, Target.GetLocation(), MinBend, MaxBend))
			{
				return false;
			}
		}
	}
	return true;
}

bool UQuadFootStepComponent::PlanInitialLift(const FLeg& Leg, const float Clearance, const float MinBend, const float MaxBend)
{
	const FTransform BodyPose = Mesh->GetComponentTransform();
	RouteIndex = 1;
	bStepUpRoute = false;
	if (FindLiftRoute(Leg, BodyPose, Clearance, MinBend, MaxBend))
	{
		return true;
	}

	const FVector Inward = ContactFrame(BodyPose.TransformPosition(Leg.HipLocal) - Leg.Contact, LiftNormal).GetForwardVector();
	for (int32 Distance = 5; Distance <= 15; Distance += 5)
	{
		for (int32 Direction = 0; Direction < 8; ++Direction)
		{
			const FVector Offset = SearchDirection(-Inward, LiftNormal, Direction) * Distance;
			FTransform Proposed = BodyPose;
			Proposed.AddToTranslation(Offset);
			if (CanPrepareBody(BodyPose, Proposed, MinBend, MaxBend)
				&& FindLiftRoute(Leg, Proposed, Clearance, MinBend, MaxBend))
			{
				PreparedBodyPose = BodyPose;
				BodyPreparationTarget = Proposed.GetLocation();
				BodyPreparationOffset = Offset;
				Phase = EQuadFootPhase::PreparingLift;
				return true;
			}
		}
	}
	LiftDiagnostic += TEXT("; no safe route at full clearance");
	return false;
}

bool UQuadFootStepComponent::UpdateLandingCandidate(const FLeg& Leg, const float MinBend, const float MaxBend)
{
	const FVector Sole = SwingPose.TransformPosition(Leg.LocalSole);
	const FVector Plane = Leg.Contact + FVector::VectorPlaneProject(Sole - Leg.Contact, LiftNormal);
	if (!ProbeSurface(Plane, LiftNormal, LandingSurface))
	{
		return false;
	}
	const FVector& Contact = LandingSurface.ImpactPoint;
	const FVector& Normal = LandingSurface.ImpactNormal;
	LandingPose = MakeContactPose(Leg, Contact, Normal);
	const FVector Approach = Contact + Normal * FMath::Max(0.0, FVector::DotProduct(Sole - Contact, Normal));
	return IsReachable(Leg, Mesh->GetComponentTransform(), LandingPose.GetLocation(), MinBend, MaxBend)
		&& CanMoveSole(Sole, Approach, Normal) && CanMoveSole(Approach, Contact, Normal);
}

void UQuadFootStepComponent::BeginLowering()
{
	if (const UPrimitiveComponent* Component = LandingSurface.GetComponent())
	{
		const FTransform Anchor = Component->GetComponentTransform();
		LandingLocalPoint = Anchor.InverseTransformPosition(LandingSurface.ImpactPoint);
		LandingLocalNormal = Anchor.InverseTransformVectorNoScale(LandingSurface.ImpactNormal);
	}
	LandingApproach = true;
	Phase = EQuadFootPhase::Lowering;
}

bool UQuadFootStepComponent::RefreshLandingSurface(const FLeg& Leg)
{
	UPrimitiveComponent* Component = LandingSurface.GetComponent();
	if (!IsValid(Component) || !Component->IsQueryCollisionEnabled()
		|| Component->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block)
	{
		return false;
	}
	const FTransform Anchor = Component->GetComponentTransform();
	const FVector Point = Anchor.TransformPosition(LandingLocalPoint);
	const FVector Normal = Anchor.TransformVectorNoScale(LandingLocalNormal).GetSafeNormal();
	FHitResult Hit;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(QuadFootLanding), false, GetOwner());
	if (!Component->LineTraceComponent(Hit, Point + Normal * 3.0, Point - Normal * 3.0, Query)
		|| FVector::DotProduct(Hit.ImpactNormal, LiftNormal) < 0.573576)
	{
		return false;
	}
	LandingSurface = Hit;
	LandingPose = MakeContactPose(Leg, Hit.ImpactPoint, Hit.ImpactNormal);
	return true;
}

void UQuadFootStepComponent::FinishLift(const float Clearance)
{
	const FLeg& Leg = Legs[ActiveLegID];
	const FVector Sole = SwingPose.TransformPosition(Leg.LocalSole);
	FHitResult Surface;
	const FVector Plane = Leg.Contact + FVector::VectorPlaneProject(Sole - Leg.Contact, LiftNormal);
	const double FinalClearance = ProbeSurface(Plane, LiftNormal, Surface)
		? FVector::DotProduct(Sole - Surface.ImpactPoint, LiftNormal) : 0.0;
	if (FinalClearance < Clearance - 0.1)
	{
		LiftDiagnostic += TEXT("; surface changed during lift");
		Phase = EQuadFootPhase::Returning;
		RouteIndex = LiftRoute.Num() - 2;
		return;
	}
	if (!bStepUpRoute)
	{
		AimStartSole = Sole;
		AimStartSurfaceHeight = FVector::DotProduct(Sole - Leg.Contact, LiftNormal) - Clearance;
	}
	if (!LiftDiagnostic.IsEmpty())
	{
		LiftDiagnostic += TEXT("; ");
	}
	LiftDiagnostic += FString::Printf(TEXT("clearance %.1f / %.1f cm"), FinalClearance, Clearance);
	UE_LOG(LogQuadFootStep, Verbose, TEXT("%s leg %d: %s"), *GetOwner()->GetName(), ActiveLegID, *LiftDiagnostic);
	LiftRoute.Reset();
	AimBlocked = false;
	Phase = EQuadFootPhase::Aiming;
}

void UQuadFootStepComponent::AdvanceFootStep(const float DeltaSeconds, const float LiftClearance,
	const float MoveSpeed, const float MinKneeBend, const float MaxKneeBend)
{
	if (!Mesh || Legs.IsEmpty() || (Interaction && Interaction->bDispatching))
	{
		return;
	}
	if (Phase == EQuadFootPhase::PreparingLift
		&& !Mesh->GetComponentLocation().Equals(PreparedBodyPose.GetLocation(), 0.1))
	{
		LiftDiagnostic += TEXT("; body could not apply preparation");
		Phase = EQuadFootPhase::Planted;
		LiftRoute.Reset();
		BodyPreparationOffset = FVector::ZeroVector;
		return;
	}
	const float Clearance = FMath::Max(1.0f, LiftClearance);
	const float Speed = FMath::Max(0.0f, MoveSpeed);
	AdvanceGrips(FMath::Clamp(DeltaSeconds, 0.0f, 0.05f), Speed, MinKneeBend, MaxKneeBend);
	const float MaxStep = FMath::Min(1.0f / 60.0f, 12.0f / FMath::Max(Speed, 1.0f));
	for (float Remaining = FMath::Min(DeltaSeconds, 0.2f);
		Phase != EQuadFootPhase::Planted && Legs.IsValidIndex(ActiveLegID) && Remaining > KINDA_SMALL_NUMBER; Remaining -= MaxStep)
	{
		AdvanceSubstep(FMath::Min(Remaining, MaxStep), Clearance, Speed, MinKneeBend, MaxKneeBend);
	}
}

void UQuadFootStepComponent::AdvanceSubstep(const float DeltaSeconds, const float Clearance,
	const float MoveSpeed, const float MinKneeBend, const float MaxKneeBend)
{
	FLeg& Leg = Legs[ActiveLegID];
	const FVector CurrentSole = SwingPose.TransformPosition(Leg.LocalSole);
	const FTransform BodyPose = Mesh->GetComponentTransform();
	auto Advance = [this, &Leg, &BodyPose, MinKneeBend, MaxKneeBend](const FTransform& Next, const FVector& Normal)
	{
		if (!IsReachable(Leg, BodyPose, Next.GetLocation(), MinKneeBend, MaxKneeBend)
			|| !CanMoveSole(SwingPose.TransformPosition(Leg.LocalSole), Next.TransformPosition(Leg.LocalSole), Normal))
		{
			return false;
		}
		SwingPose = Next;
		return true;
	};

	if (Phase == EQuadFootPhase::PreparingLift)
	{
		if (BodyPose.GetLocation().Equals(BodyPreparationTarget, 0.01))
		{
			Phase = EQuadFootPhase::Raising;
			return;
		}
		if (PreparedBodyPose.GetLocation().Equals(BodyPreparationTarget, 0.01))
		{
			return;
		}

		FTransform Next = PreparedBodyPose;
		Next.SetLocation(FMath::VInterpConstantTo(PreparedBodyPose.GetLocation(), BodyPreparationTarget, DeltaSeconds,
			BodyPreparationSpeed));
		if (!CanPrepareBody(PreparedBodyPose, Next, MinKneeBend, MaxKneeBend))
		{
			LiftDiagnostic += TEXT("; body preparation interrupted");
			Phase = EQuadFootPhase::Planted;
			LiftRoute.Reset();
			BodyPreparationOffset = FVector::ZeroVector;
			return;
		}
		PreparedBodyPose = Next;
		return;
	}

	if (Phase == EQuadFootPhase::Raising || Phase == EQuadFootPhase::Returning)
	{
		if (LiftRoute.IsEmpty())
		{
			if (!PlanInitialLift(Leg, Clearance, MinKneeBend, MaxKneeBend))
			{
				UE_LOG(LogQuadFootStep, Verbose, TEXT("%s leg %d: %s"), *GetOwner()->GetName(), ActiveLegID, *LiftDiagnostic);
				Phase = EQuadFootPhase::Planted;
			}
			if (Phase != EQuadFootPhase::Raising)
			{
				return;
			}
		}
		if (Phase == EQuadFootPhase::Raising && bStepUpRoute && AimInput.IsNearlyZero())
		{
			LiftRoute.Reset();
			AimBlocked = false;
			Phase = EQuadFootPhase::Aiming;
			return;
		}

		const FVector Goal = LiftRoute[RouteIndex];
		const FVector Next = FMath::VInterpConstantTo(CurrentSole, Goal, DeltaSeconds, VerticalSpeed);
		if (!CanTraverseSole(Leg, BodyPose, CurrentSole, Next, MinKneeBend, MaxKneeBend))
		{
			if (Phase == EQuadFootPhase::Raising)
			{
				LiftDiagnostic += TEXT("; lift route interrupted");
				if (!bFootAirborne)
				{
					Phase = EQuadFootPhase::Planted;
					LiftRoute.Reset();
					BodyPreparationOffset = FVector::ZeroVector;
					return;
				}
				Phase = EQuadFootPhase::Returning;
				--RouteIndex;
			}
			else if (bFootAirborne)
			{
				CandidateValid = UpdateLandingCandidate(Leg, MinKneeBend, MaxKneeBend);
				if (ConfirmRequested && CandidateValid)
				{
					LiftRoute.Reset();
					BeginLowering();
				}
				ConfirmRequested = false;
			}
			return;
		}

		SwingPose.AddToTranslation(Next - CurrentSole);
		if (!bFootAirborne && Interaction && !Next.Equals(LiftRoute[0], 0.01))
		{
			Interaction->LeaveContact(ActiveLegID);
		}
		bFootAirborne |= !Next.Equals(LiftRoute[0], 0.01);
		if (!Next.Equals(Goal, 0.001))
		{
			return;
		}
		if (Phase == EQuadFootPhase::Raising && ++RouteIndex == LiftRoute.Num())
		{
			FinishLift(Clearance);
		}
		else if (Phase == EQuadFootPhase::Returning && --RouteIndex < 0)
		{
			LiftRoute.Reset();
			ConfirmRequested = CandidateValid = false;
			Phase = bStepUpRoute ? EQuadFootPhase::Aiming : EQuadFootPhase::Planted;
			bFootAirborne = bStepUpRoute;
			if (!bFootAirborne)
			{
				SwingPose = Leg.Planted;
				BodyPreparationOffset = FVector::ZeroVector;
				if (Interaction && Leg.LastConfirmedHit.GetComponent() && !Interaction->Feet[ActiveLegID].bContact)
				{
					Interaction->CommitContact(ActiveLegID, Leg.LastConfirmedHit);
				}
			}
		}
		return;
	}

	if (Phase == EQuadFootPhase::Aiming)
	{
		const FVector OnStartPlane = Leg.Contact + FVector::VectorPlaneProject(CurrentSole - Leg.Contact, LiftNormal);
		const FVector Velocity = GetAimVelocity(LiftNormal, MoveSpeed);
		if (!Velocity.IsNearlyZero())
		{
			AimBlocked = true;
			const FVector Delta = Velocity * DeltaSeconds;
			const FVector NextPlane = OnStartPlane + Delta;
			FHitResult NextSurface;
			if (ProbeSurface(NextPlane, LiftNormal, NextSurface))
			{
				FHitResult Ahead;
				double SurfaceHeight = FVector::DotProduct(NextSurface.ImpactPoint - Leg.Contact, LiftNormal);
				FHitResult HighestSurface = NextSurface;
				if (ProbeSurface(NextPlane + Delta.GetSafeNormal() * (SoleSweepRadius * 2.0f), LiftNormal, Ahead))
				{
					const double AheadHeight = FVector::DotProduct(Ahead.ImpactPoint - Leg.Contact, LiftNormal);
					if (AheadHeight > SurfaceHeight)
					{
						SurfaceHeight = AheadHeight;
						HighestSurface = Ahead;
					}
				}
				const float Height = FMath::Max(AimStartSurfaceHeight, SurfaceHeight) + Clearance;
				const float CurrentHeight = FVector::DotProduct(CurrentSole - Leg.Contact, LiftNormal);
				const bool bNeedClearance = Height > CurrentHeight + 0.1;
				const FVector NextSole = bNeedClearance
					? CurrentSole + LiftNormal * FMath::Min(Height - CurrentHeight, VerticalSpeed * DeltaSeconds)
					: CurrentSole + Delta;
				FTransform Next = SwingPose;
				Next.AddToTranslation(NextSole - CurrentSole);
				const bool Moved = IsReachable(Leg, BodyPose, MakeContactPose(Leg, NextSurface.ImpactPoint, NextSurface.ImpactNormal).GetLocation(),
					MinKneeBend, MaxKneeBend) && Advance(Next, LiftNormal);
				AimBlocked = !Moved || bNeedClearance;
				if (!Moved && bNeedClearance && !bTriedStepUpCorrection)
				{
					bTriedStepUpCorrection = true;
					const FVector Goal = HighestSurface.ImpactPoint
						+ LiftNormal * (Clearance + FMath::Max(0.0, AimStartSurfaceHeight - SurfaceHeight));
					if (IsReachable(Leg, BodyPose,
						MakeContactPose(Leg, HighestSurface.ImpactPoint, HighestSurface.ImpactNormal).GetLocation(), MinKneeBend, MaxKneeBend)
						&& FindLiftRoute(Leg, BodyPose, Clearance, MinKneeBend, MaxKneeBend, &Goal))
					{
						bStepUpRoute = true;
						RouteIndex = 1;
						CandidateValid = ConfirmRequested = false;
						Phase = EQuadFootPhase::Raising;
						return;
					}
				}
				if (Moved && !bNeedClearance && Velocity.Length() >= MinHeadingSpeed)
				{
					Heading = Delta.GetSafeNormal();
					FTransform Turned = SwingPose;
					Turned.SetRotation(FQuat::Slerp(SwingPose.GetRotation(),
						ContactFrame(Heading, LiftNormal) * Leg.FootToContact, 1.0 - FMath::Exp(-12.0f * DeltaSeconds)).GetNormalized());
					Turned.SetLocation(NextSole - Turned.TransformVector(Leg.LocalSole));
					Advance(Turned, LiftNormal);
				}
			}
		}

		const bool bAtAimOrigin = AimInput.IsNearlyZero()
			&& FVector::VectorPlaneProject(CurrentSole - AimStartSole, LiftNormal).IsNearlyZero(0.1);
		CandidateValid = (!AimBlocked || bAtAimOrigin) && UpdateLandingCandidate(Leg, MinKneeBend, MaxKneeBend);
		if (ConfirmRequested && CandidateValid)
		{
			BeginLowering();
		}
		ConfirmRequested = false;
		return;
	}

	if (!RefreshLandingSurface(Leg))
	{
		CandidateValid = false;
		Phase = EQuadFootPhase::Aiming;
		return;
	}
	const FVector Normal = LandingSurface.ImpactNormal;
	const FVector LandingContact = LandingSurface.ImpactPoint;
	const double Height = FMath::Max(0.0, FVector::DotProduct(CurrentSole - LandingContact, Normal));
	const FVector GoalSole = LandingApproach ? LandingContact + Normal * Height : LandingContact;
	const FVector NextSole = FMath::VInterpConstantTo(CurrentSole, GoalSole, DeltaSeconds, VerticalSpeed);
	FTransform Next = SwingPose;
	Next.SetRotation(FQuat::Slerp(SwingPose.GetRotation(), LandingPose.GetRotation(),
		NextSole.Equals(GoalSole, 0.1) && !LandingApproach ? 1.0 : 1.0 - FMath::Exp(-12.0f * DeltaSeconds)).GetNormalized());
	Next.SetLocation(NextSole - Next.TransformVector(Leg.LocalSole));
	if (!Advance(Next, Normal))
	{
		CandidateValid = false;
		AimBlocked = true;
		Phase = EQuadFootPhase::Aiming;
		return;
	}
	if (NextSole.Equals(GoalSole, 0.1))
	{
		if (LandingApproach)
		{
			LandingApproach = false;
			return;
		}
		Leg.Planted = LandingPose;
		Leg.Contact = LandingContact;
		Leg.Normal = Normal;
		Leg.LastConfirmedHit = LandingSurface;
		const int32 LandedLeg = ActiveLegID;
		Phase = EQuadFootPhase::Planted;
		ActiveLegID = INDEX_NONE;
		bFootAirborne = false;
		BodyPreparationOffset = FVector::ZeroVector;
		CandidateValid = false;
		if (Interaction)
		{
			Interaction->CommitContact(LandedLeg, Leg.LastConfirmedHit);
		}
	}
}

void UQuadFootStepComponent::GetLegPose(const int32 LegID, FTransform& Target, FVector& Contact,
	FVector& Normal, float& Weight, bool& IsControlling) const
{
	IsControlling = Legs.IsValidIndex(LegID) && LegID == GetActiveLegID();
	Weight = 0.0f;
	Target = FTransform::Identity;
	Contact = FVector::ZeroVector;
	Normal = FVector::UpVector;
	if (Legs.IsValidIndex(LegID))
	{
		const FLeg& Leg = Legs[LegID];
		const bool bSwinging = LegID == ActiveLegID && bFootAirborne;
		const auto* Foot = Interaction ? &Interaction->Feet[LegID] : nullptr;
		const bool bGripTarget = Foot && (Foot->GripMode != EQuadGripMode::None || Foot->bNeedsLanding);
		Target = bSwinging ? SwingPose : bGripTarget ? Foot->GripTarget : Leg.Planted;
		Contact = Leg.Contact;
		Normal = Leg.Normal;
		Weight = bSwinging || bGripTarget ? 0.0f : 1.0f;
	}
}

FTransform UQuadFootStepComponent::ResolveFootBodyPose(const FTransform& SupportPose) const
{
	if (!Mesh || Legs.IsEmpty())
	{
		return SupportPose;
	}

	const FTransform Current = Mesh->GetComponentTransform();
	if (Phase == EQuadFootPhase::PreparingLift)
	{
		return PreparedBodyPose;
	}
	if (Phase == EQuadFootPhase::Raising || Phase == EQuadFootPhase::Returning)
	{
		return Current;
	}
	FTransform Desired = SupportPose;
	Desired.SetScale3D(Current.GetScale3D());
	const bool bSwinging = bFootAirborne && Legs.IsValidIndex(ActiveLegID);
	if (bSwinging)
	{
		const FLeg& SwingLeg = Legs[ActiveLegID];
		const FVector Sole = SwingPose.TransformPosition(SwingLeg.LocalSole);
		// Follow the swing foot's planar movement without using its lift as a support height.
		Desired.AddToTranslation(BodyPreparationOffset
			+ FVector::VectorPlaneProject(Sole - AimStartSole, LiftNormal) / Legs.Num());
	}

	float Alpha = 1.0f - FMath::Exp(-10.0f * GetWorld()->GetDeltaSeconds());
	for (int32 Attempt = 0; Attempt < 6; ++Attempt, Alpha *= 0.5f)
	{
		FTransform Next;
		Next.Blend(Current, Desired, Alpha);

		// Keep the planted targets fixed and limit the body motion to the available leg reach.
		bool bWithinReach = true;
		for (int32 Index = 0; Index < Legs.Num(); ++Index)
		{
			const FLeg& Leg = Legs[Index];
			FTransform FootTarget;
			FVector Contact, Normal;
			float Weight;
			bool bControlling;
			GetLegPose(Index, FootTarget, Contact, Normal, Weight, bControlling);
			const FVector Foot = FootTarget.GetLocation();
			const double CurrentDistance = FVector::Dist(Current.TransformPosition(Leg.HipLocal), Foot);
			const double NextDistance = FVector::Dist(Next.TransformPosition(Leg.HipLocal), Foot);
			const double MinReach = FMath::Min<double>(FMath::Abs(Leg.UpperLength - Leg.LowerLength), CurrentDistance);
			const double MaxReach = FMath::Max<double>(Leg.UpperLength + Leg.LowerLength, CurrentDistance);
			if (NextDistance < MinReach - KINDA_SMALL_NUMBER || NextDistance > MaxReach + KINDA_SMALL_NUMBER)
			{
				bWithinReach = false;
				break;
			}
		}
		if (bWithinReach)
		{
			return Next;
		}
	}
	return Current;
}

TArray<FVector> UQuadFootStepComponent::GetNominalContacts() const
{
	TArray<FVector> Contacts;
	for (const FLeg& Leg : Legs)
	{
		Contacts.Add(Leg.NominalContact);
	}
	return Contacts;
}
