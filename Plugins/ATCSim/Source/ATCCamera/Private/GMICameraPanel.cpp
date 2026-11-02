#include "GMICameraPanel.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GMICamera.h"
#include "PTZControllerComponent.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UGMICameraPanel::RebuildWidget()
{
	// Built here instead of in a widget Blueprint, so the panel works without content
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
		Root->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.65f));
		Root->SetPadding(FMargin(6.f));
		Grid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("Grid"));
		Grid->SetSlotPadding(FMargin(4.f));
		Root->SetContent(Grid);
		WidgetTree->RootWidget = Root;
	}
	return Super::RebuildWidget();
}

void UGMICameraPanel::SetCameras(const TArray<AGMICamera*>& InCameras, const AGMICamera* Selected)
{
	TakeWidget();
	Grid->ClearChildren();
	Captions.Reset();
	Cameras.Reset();
	for (int32 Index = 0; Index < InCameras.Num(); ++Index)
	{
		AGMICamera* Camera = InCameras[Index];
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		UImage* Feed = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		FSlateBrush Brush;
		Brush.SetResourceObject(Camera->GetRenderTarget());
		Brush.ImageSize = FVector2D(320.f, 180.f);
		Feed->SetBrush(Brush);
		Box->AddChildToVerticalBox(Feed);
		UTextBlock* Caption = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Caption->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 10));
		Box->AddChildToVerticalBox(Caption);
		Grid->AddChildToUniformGrid(Box, Index / 2, Index % 2);
		Captions.Add(Caption);
		Cameras.Add(Camera);
	}
	Refresh(Selected);
	SetAnchorsInViewport(FAnchors(1.f, 0.f, 1.f, 0.f));
	SetAlignmentInViewport(FVector2D(1.f, 0.f));
	SetPositionInViewport(FVector2D(-12.f, 12.f), false);
}

void UGMICameraPanel::Refresh(const AGMICamera* Selected)
{
	SelectedCamera = Selected;
	for (int32 Index = 0; Index < Captions.Num(); ++Index)
	{
		const AGMICamera* Camera = Cameras[Index].Get();
		if (!Camera)
		{
			continue;
		}
		FString Text = FString::Printf(TEXT("%s  %.0f mm"), *Camera->CameraId.ToString(), Camera->PTZ->FocalLengthMm);
		const FGameplayTag Preset = Camera->PTZ->GetCurrentPreset();
		if (Preset.IsValid())
		{
			FString Name = Preset.ToString();
			Name.Split(TEXT("."), nullptr, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			Text += TEXT("  ") + Name;
		}
		Captions[Index]->SetText(FText::FromString(Text));
		Captions[Index]->SetColorAndOpacity(FSlateColor(Camera == Selected ? FLinearColor(1.f, 0.8f, 0.15f) : FLinearColor(0.9f, 0.9f, 0.9f)));
	}
}

void UGMICameraPanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	CaptionTimer -= InDeltaTime;
	if (CaptionTimer <= 0.f)
	{
		CaptionTimer = 0.5f;
		Refresh(SelectedCamera.Get());
	}
}
