#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTemper, Log, All);

/** Loads an engine asset and keeps it alive for the session (safe to cache in function statics). */
template <class T>
T* TemperLoadPinned(const TCHAR* Path)
{
	T* Object = LoadObject<T>(nullptr, Path);
	if (Object)
	{
		Object->AddToRoot();
	}
	return Object;
}
