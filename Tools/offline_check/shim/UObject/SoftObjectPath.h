#pragma once
#include "CoreMinimal.h"
struct FSoftObjectPath { FSoftObjectPath(const char*) {} FSoftObjectPath(const FString&) {} UObject* TryLoad() const { return nullptr; } };
