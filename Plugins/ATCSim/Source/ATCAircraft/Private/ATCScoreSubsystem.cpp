#include "ATCScoreSubsystem.h"
#include "ATCGameplayTags.h"
#include "ATCUnits.h"
#include "TaxiNetworkSubsystem.h"
#include "TrafficSubsystem.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

int32 FATCSessionScore::GetScore() const
{
	const float SlowClearances = FMath::Max(AverageWaitSeconds() - 30.f, 0.f) / 5.f;
	const float Score = 100.f - 10.f * GoArounds - 20.f * Conflicts - 5.f * Deadlocks - SlowClearances;
	return FMath::Clamp(FMath::RoundToInt(Score), 0, 100);
}

FString FATCSessionScore::ToLine() const
{
	return FString::Printf(TEXT("SCORE %d  LDG %d DEP %d GA %d  WAIT %.0fs"), GetScore(), Landings, Takeoffs, GoArounds, AverageWaitSeconds());
}

FString FATCSessionScore::ToReport() const
{
	const int32 Score = GetScore();
	const TCHAR* Grade = Score >= 90 ? TEXT("excellent") : Score >= 75 ? TEXT("good") : Score >= 55 ? TEXT("sufficient") : TEXT("insufficient");
	FString Text = FString::Printf(TEXT("Score: %d / 100 (%s) after %.0f minutes\n"), Score, Grade, Minutes);
	Text += FString::Printf(TEXT("Landings: %d\nTakeoffs: %d\nPushbacks: %d\n"), Landings, Takeoffs, Pushbacks);
	Text += FString::Printf(TEXT("Go-arounds: %d (-10 each)\n"), GoArounds);
	Text += FString::Printf(TEXT("Conflicts on the ground: %d (-20 each)\n"), Conflicts);
	Text += FString::Printf(TEXT("Deadlocks: %d (-5 each)\n"), Deadlocks);
	Text += FString::Printf(TEXT("Stop-bar waits: %d, on average %.0f s, longest %.0f s (-1 per 5 s over 30 s on average)"),
		StopBarWaits, AverageWaitSeconds(), LongestWaitSeconds);
	return Text;
}

bool UATCScoreSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UATCScoreSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UTrafficSubsystem* Traffic = InWorld.GetSubsystem<UTrafficSubsystem>())
	{
		Traffic->OnAircraftEvent.AddUniqueDynamic(this, &UATCScoreSubsystem::HandleAircraftEvent);
	}
	ResetScore();
	InWorld.GetTimerManager().SetTimer(RefreshTimer, FTimerDelegate::CreateUObject(this, &UATCScoreSubsystem::Refresh), 5.f, true);
}

void UATCScoreSubsystem::Deinitialize()
{
	// A session worth looking back on: the instructor finds the report in Saved/ATC
	if (Counts.Minutes >= 5.f && (Counts.Landings + Counts.Takeoffs + Counts.GoArounds) > 0)
	{
		SaveReport();
	}
	Super::Deinitialize();
}

double UATCScoreSubsystem::Now() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

FATCSessionScore UATCScoreSubsystem::GetScore() const
{
	FATCSessionScore Score = Counts;
	if (const UTaxiNetworkSubsystem* Network = GetWorld() ? GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>() : nullptr)
	{
		Score.Conflicts = Network->GetOverlapCount() - OverlapsAtStart;
		Score.Deadlocks = Network->GetDeadlockCount() - DeadlocksAtStart;
	}
	Score.Minutes = static_cast<float>((Now() - StartTime) / 60.0);
	return Score;
}

void UATCScoreSubsystem::ResetScore()
{
	Counts = FATCSessionScore();
	WaitingSince.Reset();
	StartTime = Now();
	OverlapsAtStart = DeadlocksAtStart = 0;
	if (const UTaxiNetworkSubsystem* Network = GetWorld() ? GetWorld()->GetSubsystem<UTaxiNetworkSubsystem>() : nullptr)
	{
		OverlapsAtStart = Network->GetOverlapCount();
		DeadlocksAtStart = Network->GetDeadlockCount();
	}
	LastLine.Reset();
	Refresh();
}

void UATCScoreSubsystem::Refresh()
{
	// The counts of the taxi network are read here, so Deinitialize still has them when the network is gone
	const FATCSessionScore Score = GetScore();
	Counts.Conflicts = Score.Conflicts;
	Counts.Deadlocks = Score.Deadlocks;
	Counts.Minutes = Score.Minutes;
	const FString Line = Score.ToLine();
	if (Line != LastLine)
	{
		LastLine = Line;
		OnScoreChanged.Broadcast();
	}
}

void UATCScoreSubsystem::HandleAircraftEvent(AActor* Aircraft, FName Callsign, FGameplayTag EventTag, const FString& Detail)
{
	if (EventTag == TAG_ATC_Event_Traffic_Landed)
	{
		++Counts.Landings;
	}
	else if (EventTag == TAG_ATC_Event_Traffic_Takeoff)
	{
		++Counts.Takeoffs;
	}
	else if (EventTag == TAG_ATC_Event_Pushback_Completed)
	{
		++Counts.Pushbacks;
	}
	else if (EventTag == TAG_ATC_Event_Traffic_Removed)
	{
		Counts.GoArounds += Detail == TEXT("went around") ? 1 : 0;
		WaitingSince.Remove(Callsign);
	}
	else if (EventTag == TAG_ATC_Event_Taxi_StopReached && Detail.Contains(TEXT("waiting for clearance")))
	{
		WaitingSince.Add(Callsign, Now());
	}
	// Moving again after a stop bar: the clearance came
	else if (EventTag == TAG_ATC_Event_Taxi_State && Detail == TEXT("Taxiing"))
	{
		double Since = 0.0;
		if (WaitingSince.RemoveAndCopyValue(Callsign, Since))
		{
			const float Wait = static_cast<float>(Now() - Since);
			++Counts.StopBarWaits;
			Counts.StopBarWaitSeconds += Wait;
			Counts.LongestWaitSeconds = FMath::Max(Counts.LongestWaitSeconds, Wait);
		}
	}
	else
	{
		return;
	}
	Refresh();
}

FString UATCScoreSubsystem::SaveReport() const
{
	const FString File = FPaths::ProjectSavedDir() / TEXT("ATC") / FString::Printf(TEXT("Score_%s.txt"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S")));
	const FString Report = Counts.ToReport();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
	FFileHelper::SaveStringToFile(Report, *File);
	UE_LOG(LogATC, Display, TEXT("Score saved to %s:\n%s"), *File, *Report);
	return File;
}
