#include "QuadFootExamples.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	void SetDebugLabel(ATextRenderActor* Label, const FString& Text, const FColor Color)
	{
		if (IsValid(Label))
		{
			UTextRenderComponent* Render = Label->GetTextRender();
			if (Render->Text.ToString() != Text)
			{
				Render->SetText(FText::FromString(Text));
				Render->SetTextRenderColor(Color);
			}
		}
	}
}

AQuadPressurePlate::AQuadPressurePlate()
{
	ContactSurface = CreateDefaultSubobject<UBoxComponent>(TEXT("ContactSurface"));
	SetRootComponent(ContactSurface);
	ContactSurface->SetBoxExtent(FVector(42, 42, 4));
	ContactSurface->SetCollisionProfileName(TEXT("BlockAll"));
	Plunger = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plunger"));
	Plunger->SetupAttachment(ContactSurface);
	Plunger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Plunger->SetRelativeScale3D(FVector(0.84, 0.84, 0.08));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Plunger->SetStaticMesh(Cube.Object);
}

void AQuadPressurePlate::BeginPlay()
{
	Super::BeginPlay();
	UpdateDebugLabel();
}

void AQuadPressurePlate::UpdateDebugLabel() const
{
	SetDebugLabel(DebugLabel, Pressed ? TEXT("PRESSURE PLATE\nTRIGGERED") : TEXT("PRESSURE PLATE\nREADY - RB TO LAND"),
		Pressed ? FColor::Green : FColor::White);
}

void AQuadPressurePlate::FootLanded_Implementation(const FQuadFootContact& Contact)
{
	if (IsValid(Contact.Quad) && Contact.LegID != INDEX_NONE)
	{
		Occupants.FindOrAdd(Contact.Quad.Get()).Add(Contact.LegID, Contact.ContactID);
		UpdatePressed();
	}
}

void AQuadPressurePlate::FootLeft_Implementation(const FQuadFootContact& Contact)
{
	if (TMap<int32, int32>* Steps = Occupants.Find(Contact.Quad.Get()))
	{
		const int32* ID = Steps->Find(Contact.LegID);
		if (ID && *ID == Contact.ContactID)
		{
			Steps->Remove(Contact.LegID);
			if (Steps->IsEmpty())
			{
				Occupants.Remove(Contact.Quad.Get());
			}
		}
		UpdatePressed();
	}
}

void AQuadPressurePlate::UpdatePressed()
{
	const bool bNext = !Occupants.IsEmpty();
	if (bNext != Pressed)
	{
		Pressed = bNext;
		UpdateDebugLabel();
		Plunger->SetRelativeLocation(FVector(0, 0, Pressed ? -2.0f : 0.0f));
		if (UMaterialInstanceDynamic* Material = Plunger->CreateDynamicMaterialInstance(0))
		{
			Material->SetVectorParameterValue(TEXT("Color"), Pressed ? FLinearColor::Green : FLinearColor::Blue);
		}
		OnPressedChanged.Broadcast(Pressed);
	}
}

AQuadFootMechanism::AQuadFootMechanism()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Body->SetStaticMesh(Cube.Object);
	Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Body->SetEnableGravity(false);
	Body->SetLinearDamping(6.0f);
	Body->SetAngularDamping(6.0f);
	Constraint = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("Constraint"));
	Constraint->SetupAttachment(Body);
	Constraint->SetAbsolute(true, true, true);
}

void AQuadFootMechanism::BeginPlay()
{
	Super::BeginPlay();
	MountedRotation = GetActorQuat();
	WorldAxis = MountedRotation.GetAxisX();
	MountedPosition = GetActorLocation();
	Constraint->SetWorldLocationAndRotation(GetActorTransform().TransformPositionNoScale(ConstraintPivotOffset), GetActorQuat());
	Constraint->SetDisableCollision(true);
	const bool bRotary = IsRotary();
	Constraint->ConstraintInstance.SetLinearLimits(
		bRotary ? ELinearConstraintMotion::LCM_Locked : ELinearConstraintMotion::LCM_Limited,
		ELinearConstraintMotion::LCM_Locked, ELinearConstraintMotion::LCM_Locked, TravelLimit);
	Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0);
	Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0);
	Constraint->SetAngularTwistLimit(bRotary ? EAngularConstraintMotion::ACM_Limited : EAngularConstraintMotion::ACM_Locked, TravelLimit);
	Body->SetSimulatePhysics(true);
	Constraint->SetConstrainedComponents(nullptr, NAME_None, Body, NAME_None);
	DisplayMaterial = Body->CreateDynamicMaterialInstance(0);
	UpdateDebugLabel();
}

bool AQuadFootMechanism::IsRotary() const
{
	return Kind == EQuadMechanismKind::Hinge || Kind == EQuadMechanismKind::Dial;
}

void AQuadFootMechanism::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Detached)
	{
		const float NewTravel = IsRotary()
			? FMath::RadiansToDegrees((Body->GetComponentQuat() * MountedRotation.Inverse()).GetTwistAngle(WorldAxis))
			: FVector::DotProduct(Body->GetComponentLocation() - MountedPosition, WorldAxis);
		if (!FMath::IsNearlyEqual(NewTravel, Travel, 0.01f))
		{
			Travel = NewTravel;
			OnTravelChanged.Broadcast(Travel);
		}
	}
	const bool bNowActivated = Detached || (Kind != EQuadMechanismKind::Detachable && FMath::Abs(Travel) >= TravelLimit * 0.5f);
	if (bNowActivated != Activated)
	{
		Activated = bNowActivated;
		if (DisplayMaterial)
		{
			DisplayMaterial->SetVectorParameterValue(TEXT("Color"), Activated ? FLinearColor::Green : FLinearColor::White);
		}
		OnActivatedChanged.Broadcast(Activated);
	}
	UpdateDebugLabel();
}

void AQuadFootMechanism::UpdateDebugLabel() const
{
	if (!IsValid(DebugLabel))
	{
		return;
	}
	const bool bHeld = GrippingQuad.IsValid();
	if (Kind == EQuadMechanismKind::Detachable)
	{
		const FString Status = Detached ? (bHeld ? TEXT("CARRYING") : TEXT("DETACHED"))
			: bHeld ? FString::Printf(TEXT("SHAKE  %d/3"), Reversals) : TEXT("Y / TRIANGLE TO GRIP");
		SetDebugLabel(DebugLabel, TEXT("BREAKAWAY PICKUP\n") + Status,
			Detached ? FColor::Green : bHeld ? FColor::Yellow : FColor::White);
		return;
	}
	const TCHAR* Name = Kind == EQuadMechanismKind::Hinge ? TEXT("HINGE")
		: Kind == EQuadMechanismKind::Dial ? TEXT("DIAL") : TEXT("SLIDER");
	const TCHAR* State = Activated ? TEXT("ACTIVE") : bHeld ? TEXT("GRIPPED") : TEXT("READY");
	SetDebugLabel(DebugLabel, FString::Printf(TEXT("%s\n%s  %+d %s"), Name, State,
		FMath::RoundToInt(Travel), IsRotary() ? TEXT("deg") : TEXT("cm")),
		Activated ? FColor::Green : bHeld ? FColor::Yellow : FColor::White);
}

bool AQuadFootMechanism::OwnsGrip(const FQuadFootContact& Contact, const int32 ID) const
{
	return ID > 0 && ID == CurrentGripID
		&& GrippingQuad == TWeakObjectPtr<AActor>(Contact.Quad.Get()) && GrippingLeg == Contact.LegID;
}

FQuadGrip AQuadFootMechanism::TryBeginGrip_Implementation(const FQuadFootContact& Contact, const int32 GripID)
{
	FQuadGrip Result;
	if (GrippingQuad.IsValid() || Contact.Hit.GetComponent() != Body || !IsValid(Contact.Quad))
	{
		return Result;
	}
	GrippingQuad = Contact.Quad.Get();
	GrippingLeg = Contact.LegID;
	CurrentGripID = GripID;
	GripPointLocal = Body->GetComponentTransform().InverseTransformPosition(Contact.Hit.ImpactPoint);
	constexpr double CenterGripRadius = 10.0;
	bGripNearPivot = IsRotary() && FVector::VectorPlaneProject(
		Contact.Hit.ImpactPoint - Constraint->GetComponentLocation(), WorldAxis).SizeSquared() < FMath::Square(CenterGripRadius);
	PreviousExcursion = Reversals = 0;
	LastExcursionTime = GetWorld()->GetTimeSeconds();
	Result.Mode = Detached ? EQuadGripMode::Carrying : EQuadGripMode::Anchored;
	Result.Anchor = Body;
	Result.CarryBody = Kind == EQuadMechanismKind::Detachable ? Body.Get() : nullptr;
	return Result;
}

FVector AQuadFootMechanism::GetGripMotionAxis_Implementation(const FQuadFootContact& Contact, const int32 GripID,
	const FVector GrabPoint)
{
	if (!IsRotary())
	{
		return WorldAxis;
	}
	if (bGripNearPivot)
	{
		return Constraint->GetUpVector();
	}
	return FVector::CrossProduct(WorldAxis, GrabPoint - Constraint->GetComponentLocation()).GetSafeNormal();
}

EQuadGripMode AQuadFootMechanism::DriveGrip_Implementation(const FQuadFootContact& Contact, const int32 GripID,
	const FVector WorldVelocity, const float DeltaSeconds)
{
	if (!OwnsGrip(Contact, GripID) || !IsValid(Body))
	{
		return EQuadGripMode::None;
	}
	if (Detached)
	{
		return EQuadGripMode::Carrying;
	}
	const FVector GrabPoint = Body->GetComponentTransform().TransformPosition(
		GripPointLocal);
	const FVector Axis = GetGripMotionAxis_Implementation(Contact, GripID, GrabPoint);
	const float Command = FMath::Clamp(FVector::DotProduct(Axis, WorldVelocity) / 140.0, -1.0, 1.0);
	if (FMath::IsNearlyZero(Command))
	{
		return EQuadGripMode::Anchored;
	}
	if (IsRotary())
	{
		constexpr double ReferenceInertia = 20000.0;
		const double AxisInertia = FVector::DotProduct(Body->ScaleByMomentOfInertia(WorldAxis), WorldAxis);
		const double InertiaScale = FMath::Max(1.0, AxisInertia / ReferenceInertia);
		Body->AddTorqueInRadians(WorldAxis * (Command * MaxTorque * InertiaScale));
		return EQuadGripMode::Anchored;
	}
	Body->AddForce(WorldAxis * (Command * MaxForce));
	if (Kind != EQuadMechanismKind::Detachable)
	{
		return EQuadGripMode::Anchored;
	}

	constexpr double ReversalWindow = 2.0;
	constexpr float ExcursionDistance = 2.0f;
	const double Time = GetWorld()->GetTimeSeconds();
	if (Time - LastExcursionTime > ReversalWindow)
	{
		PreviousExcursion = Reversals = 0;
	}
	const int32 Side = FMath::Abs(Travel) >= ExcursionDistance ? FMath::Sign(Travel) : 0;
	if (Side != 0 && Side != PreviousExcursion)
	{
		Reversals += PreviousExcursion != 0;
		PreviousExcursion = Side;
		LastExcursionTime = Time;
	}
	if (Reversals >= 3)
	{
		Detached = true;
		Constraint->BreakConstraint();
		return EQuadGripMode::Carrying;
	}
	return EQuadGripMode::Anchored;
}

void AQuadFootMechanism::EndGrip_Implementation(const FQuadFootContact& Contact, const int32 GripID,
	const EQuadGripEndReason Reason)
{
	if (OwnsGrip(Contact, GripID))
	{
		GrippingQuad.Reset();
		GrippingLeg = INDEX_NONE;
		CurrentGripID = 0;
		if (Detached)
		{
			Body->SetEnableGravity(true);
			Body->SetSimulatePhysics(true);
		}
	}
}
