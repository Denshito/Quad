#include "QuadSelectedLegComponent.h"

#include "Components/LineBatchComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Math/RotationMatrix.h"
#include "GameFramework/Actor.h"
#include "QuadFootStepComponent.h"

namespace
{
	struct FLegSockets
	{
		FName Hip, Knee, Foot;
	};

	const FLegSockets LegSockets[] = {
		{TEXT("Hip0"), TEXT("Knee0"), TEXT("Foot0")},
		{TEXT("Hip1"), TEXT("Knee1"), TEXT("Foot1")},
		{TEXT("Hip2"), TEXT("Knee2"), TEXT("Foot2")},
		{TEXT("Hip3"), TEXT("Knee3"), TEXT("Foot3")},
	};

	void AddSegmentBox(TArray<FBatchedLine>& Edges, const FVector& Start, const FVector& End, const FVector& Up)
	{
		const FVector Axis = End - Start;
		if (Axis.IsNearlyZero())
		{
			return;
		}

		const FMatrix Basis = FRotationMatrix::MakeFromXZ(Axis, Up);
		const FVector Center = (Start + End) * 0.5;
		const FVector HalfLength = Basis.GetUnitAxis(EAxis::X) * (Axis.Size() * 0.5 + 12.0);
		const FVector HalfWidth = Basis.GetUnitAxis(EAxis::Y) * 58.0;
		const FVector HalfHeight = Basis.GetUnitAxis(EAxis::Z) * 48.0;
		FVector Corners[8];
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Corners[Index] = Center + (Index & 1 ? HalfLength : -HalfLength)
				+ (Index & 2 ? HalfWidth : -HalfWidth) + (Index & 4 ? HalfHeight : -HalfHeight);
		}
		for (int32 Index = 0; Index < 8; ++Index)
		{
			for (const int32 Bit : {1, 2, 4})
			{
				if (!(Index & Bit))
				{
					Edges.Emplace(Corners[Index], Corners[Index | Bit], FLinearColor::Red, -1.0f, 2.0f, SDPG_World);
				}
			}
		}
	}
}

UQuadSelectedLegComponent::UQuadSelectedLegComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UQuadSelectedLegComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* Owner = GetOwner();
	Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
	FootStep = Owner->FindComponentByClass<UQuadFootStepComponent>();
	if (!Mesh || !FootStep)
	{
		SetComponentTickEnabled(false);
		return;
	}

	Lines = NewObject<ULineBatchComponent>(Owner, NAME_None, RF_Transient);
	Lines->RegisterComponent();
	Lines->SetComponentTickEnabled(false);
	AddTickPrerequisiteActor(Owner);
	AddTickPrerequisiteComponent(Mesh);
}

void UQuadSelectedLegComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Lines)
	{
		Lines->DestroyComponent();
		Lines = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void UQuadSelectedLegComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Lines || !Mesh)
	{
		return;
	}
	Lines->Flush();
	const int32 Index = FootStep->GetActiveLegID();
	if (Index < 0 || Index >= UE_ARRAY_COUNT(LegSockets))
	{
		return;
	}

	const FLegSockets& Sockets = LegSockets[Index];
	if (!Mesh->DoesSocketExist(Sockets.Hip) || !Mesh->DoesSocketExist(Sockets.Knee) || !Mesh->DoesSocketExist(Sockets.Foot))
	{
		return;
	}
	const FVector Knee = Mesh->GetSocketLocation(Sockets.Knee);
	const FVector Up = Mesh->GetUpVector();
	TArray<FBatchedLine> Edges;
	Edges.Reserve(24);
	AddSegmentBox(Edges, Mesh->GetSocketLocation(Sockets.Hip), Knee, Up);
	AddSegmentBox(Edges, Knee, Mesh->GetSocketLocation(Sockets.Foot), Up);
	Lines->DrawLines(Edges);
	Lines->UpdateBounds();
}
