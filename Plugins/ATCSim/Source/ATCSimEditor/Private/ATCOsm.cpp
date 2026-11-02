#include "ATCOsm.h"
#include "ATCUnits.h"
#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ATCOsm
{
	FString DataFolder()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ATCSim"));
		return Plugin.IsValid() ? Plugin->GetBaseDir() / TEXT("Data") / TEXT("OSM") : FString();
	}

	FVector2D FElement::Center() const
	{
		if (Points.Num() == 0)
		{
			return FVector2D::ZeroVector;
		}
		// A closed way repeats its first point; leave that out of the average
		const int32 Count = IsClosed() ? Points.Num() - 1 : Points.Num();
		FVector2D Sum = FVector2D::ZeroVector;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Sum += Points[Index];
		}
		return Sum / Count;
	}

	bool FData::Load(const FString& InICAO)
	{
		ICAO = InICAO;
		Elements.Reset();
		const FString File = DataFolder() / InICAO + TEXT(".json");
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			UE_LOG(LogATC, Error, TEXT("OSM: %s not found"), *File);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			UE_LOG(LogATC, Error, TEXT("OSM: %s is not valid JSON"), *File);
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("elements")))
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			FElement Element;
			Element.Id = static_cast<int64>(Object->GetNumberField(TEXT("id")));
			Element.bWay = Object->GetStringField(TEXT("type")) == TEXT("way");
			if (const TSharedPtr<FJsonObject>* Tags; Object->TryGetObjectField(TEXT("tags"), Tags))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Tags)->Values)
				{
					Element.Tags.Add(Pair.Key, Pair.Value->AsString());
				}
			}
			if (const TArray<TSharedPtr<FJsonValue>>* Geometry; Object->TryGetArrayField(TEXT("geometry"), Geometry))
			{
				for (const TSharedPtr<FJsonValue>& Point : *Geometry)
				{
					Element.Points.Add(FVector2D(Point->AsObject()->GetNumberField(TEXT("lat")), Point->AsObject()->GetNumberField(TEXT("lon"))));
				}
			}
			else if (Object->HasField(TEXT("lat")))
			{
				Element.Points.Add(FVector2D(Object->GetNumberField(TEXT("lat")), Object->GetNumberField(TEXT("lon"))));
			}
			else if (const TSharedPtr<FJsonObject>* Center; Object->TryGetObjectField(TEXT("center"), Center))
			{
				Element.Points.Add(FVector2D((*Center)->GetNumberField(TEXT("lat")), (*Center)->GetNumberField(TEXT("lon"))));
			}
			if (Element.Points.Num() > 0)
			{
				Elements.Add(MoveTemp(Element));
			}
		}
		UE_LOG(LogATC, Display, TEXT("OSM %s: %d elements"), *InICAO, Elements.Num());
		return true;
	}

	TArray<const FElement*> FData::WithTag(const TCHAR* Key, const TCHAR* Value) const
	{
		TArray<const FElement*> Result;
		for (const FElement& Element : Elements)
		{
			if (Element.Tag(Key) == Value)
			{
				Result.Add(&Element);
			}
		}
		return Result;
	}

	TArray<FRunway> Runways(const FData& Data)
	{
		// Pieces as polylines; join pieces whose ends touch and that run in the same direction
		TArray<TArray<FVector2D>> Pieces;
		TArray<FString> Refs;
		TArray<double> Widths;
		for (const FElement* Element : Data.WithTag(TEXT("aeroway"), TEXT("runway")))
		{
			if (!Element->bWay || Element->IsClosed() || Element->Points.Num() < 2)
			{
				continue;   // runway areas drawn as polygons are skipped, the centerline ways are used
			}
			Pieces.Add(Element->Points);
			Refs.Add(Element->Tag(TEXT("ref")));
			Widths.Add(Element->Tag(TEXT("width")).IsEmpty() ? 45.0 : FCString::Atod(*Element->Tag(TEXT("width"))));
		}

		auto Direction = [](const FVector2D& From, const FVector2D& To)
		{
			// Rough metric direction, good enough to compare headings
			return FVector2D((To.X - From.X), (To.Y - From.Y) * FMath::Cos(FMath::DegreesToRadians(From.X))).GetSafeNormal();
		};

		bool bJoined = true;
		while (bJoined)
		{
			bJoined = false;
			for (int32 I = 0; I < Pieces.Num() && !bJoined; ++I)
			{
				for (int32 J = 0; J < Pieces.Num() && !bJoined; ++J)
				{
					if (I == J)
					{
						continue;
					}
					TArray<FVector2D>& First = Pieces[I];
					TArray<FVector2D> Second = Pieces[J];
					if (First.Last().Equals(Second.Last(), 1e-8))
					{
						Algo::Reverse(Second);
					}
					else if (First[0].Equals(Second[0], 1e-8))
					{
						Algo::Reverse(First);
					}
					else if (First[0].Equals(Second.Last(), 1e-8))
					{
						Algo::Reverse(First);
						Algo::Reverse(Second);
					}
					if (!First.Last().Equals(Second[0], 1e-8))
					{
						continue;
					}
					const double Alignment = Direction(First[0], First.Last()) | Direction(Second[0], Second.Last());
					if (Alignment < 0.99)
					{
						continue;   // crossing runways share a point but are different runways
					}
					First.Append(TArrayView<const FVector2D>(Second).RightChop(1));
					if (Refs[I].IsEmpty())
					{
						Refs[I] = Refs[J];
					}
					Widths[I] = FMath::Max(Widths[I], Widths[J]);
					Pieces.RemoveAt(J);
					Refs.RemoveAt(J);
					Widths.RemoveAt(J);
					bJoined = true;
				}
			}
		}

		TArray<FRunway> Result;
		for (int32 Index = 0; Index < Pieces.Num(); ++Index)
		{
			FRunway Runway;
			Runway.Ref = Refs[Index];
			Runway.WidthM = Widths[Index];
			Runway.Line = Pieces[Index];
			Runway.A = Pieces[Index][0];
			Runway.B = Pieces[Index].Last();
			Result.Add(Runway);
		}
		return Result;
	}
}
