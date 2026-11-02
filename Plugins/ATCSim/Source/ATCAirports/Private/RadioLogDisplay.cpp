#include "RadioLogDisplay.h"
#include "RadioSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ARadioLogDisplay::ARadioLogDisplay()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(RootComponent);
	Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Panel->SetCastShadow(false);
	if (Cube.Succeeded())
	{
		Panel->SetStaticMesh(Cube.Object);
	}

	Text = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Text"));
	Text->SetupAttachment(RootComponent);
	Text->SetHorizontalAlignment(EHTA_Left);
	Text->SetVerticalAlignment(EVRTA_TextBottom);
	Text->SetTextRenderColor(FColor(120, 255, 140));
	Text->SetText(FText::FromString(TEXT("RADIO  TWR 118.275  GND 121.800")));
}

void ARadioLogDisplay::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Layout();
}

void ARadioLogDisplay::Layout()
{
	const float Height = WidthCm * 0.62f;
	Panel->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(-2.f, 0.f, 0.f), FVector(0.03f, WidthCm / 100.f, Height / 100.f)));
	// Text faces +X; a viewer in front looks along -X, so left in the picture is +Y
	Text->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(1.f, WidthCm * 0.47f, -Height * 0.45f)));
	// Character width is about 0.55 of the height: LineLength characters fill 94 % of the width
	Text->SetWorldSize(WidthCm * 0.94f / (FMath::Max(LineLength, 10) * 0.55f));
}

void ARadioLogDisplay::BeginPlay()
{
	Super::BeginPlay();
	Layout();
	// Opaque text that scrolls leaves a ghost of the old lines in the temporal anti-aliasing; translucent text does not
	if (UMaterialInterface* TextMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultTextMaterialTranslucent.DefaultTextMaterialTranslucent"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		Text->SetTextMaterial(TextMaterial);
	}
	// A dark screen: the camera screen material with a black picture
	if (UMaterialInterface* Screen = LoadObject<UMaterialInterface>(nullptr, TEXT("/ATCSim/Materials/M_Monitor.M_Monitor"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* Dark = Panel->CreateDynamicMaterialInstance(0, Screen);
		Dark->SetTextureParameterValue(TEXT("Feed"), LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black"), nullptr, LOAD_NoWarn | LOAD_Quiet));
	}
	if (URadioSubsystem* Radio = GetWorld()->GetSubsystem<URadioSubsystem>())
	{
		Radio->OnRadioMessage.AddUniqueDynamic(this, &ARadioLogDisplay::HandleRadioMessage);
	}
}

void ARadioLogDisplay::HandleRadioMessage(const FString& Line)
{
	// Wrap at word boundaries; the newest lines stay
	FString Rest = Line;
	while (!Rest.IsEmpty())
	{
		int32 Cut = Rest.Len();
		if (Cut > LineLength)
		{
			Cut = LineLength;
			int32 Space = INDEX_NONE;
			if (Rest.Left(LineLength).FindLastChar(TEXT(' '), Space) && Space > 10)
			{
				Cut = Space;
			}
		}
		Lines.Add(Lines.Num() == 0 || Rest.Len() == Line.Len() ? Rest.Left(Cut) : TEXT("   ") + Rest.Left(Cut));
		Rest = Rest.Mid(Cut).TrimStart();
	}
	while (Lines.Num() > MaxLines)
	{
		Lines.RemoveAt(0);
	}
	Text->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
}
