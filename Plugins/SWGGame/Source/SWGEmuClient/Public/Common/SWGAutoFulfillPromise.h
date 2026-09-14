#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"

/**
 * Non-owning local scope guard for a TUniquePtr<TPromise<T>>: declare one at
 * the top of a function/lambda that holds such a promise, and every plain
 * early return with nothing more specific to report needs nothing else — a
 * bare `return;` is enough, since this fires a default-constructed T on
 * destruction if Succeed() was never called.
 *
 * TPromise's own destructor otherwise asserts (check(State->IsComplete()))
 * if it's destroyed without SetValue() ever having been called
 *
 * A no-op (Promise == nullptr) is a valid, safe state — some requests
 * legitimately don't carry a promise at all.
 *
 * There is no separate field-level wrapper — this guard is the only
 * safety net, so every scope that can return early while holding a promise
 * needs its own instance (each nested lambda a promise gets moved through
 * declares its own, same as ProcessNextRequest's outer Async(Thread, ...)
 * and its nested AsyncTask(GameThread, ...) each do).
 *
 * Example:
 *   bool DoWork(FSWGPendingMeshRequest& Request)
 *   {
 *       TSWGScopedPromiseFulfiller<FSWGMeshGenerationResult> ResultGuard(Request.Promise);
 *
 *       if (!Step1()) return false;                        // default result, no extra line needed
 *       if (!Step2()) return false;                         // same
 *
 *       ResultGuard.Succeed(FSWGMeshGenerationResult(...)); // the one real outcome
 *       return true;
 *   }
 */
template<typename T>
class TSWGScopedPromiseFulfiller
{
public:
	explicit TSWGScopedPromiseFulfiller(TUniquePtr<TPromise<T>>& InPromise)
		: Promise(InPromise)
	{
	}

	TSWGScopedPromiseFulfiller(const TSWGScopedPromiseFulfiller&) = delete;
	TSWGScopedPromiseFulfiller& operator=(const TSWGScopedPromiseFulfiller&) = delete;
	TSWGScopedPromiseFulfiller(TSWGScopedPromiseFulfiller&&) = delete;
	TSWGScopedPromiseFulfiller& operator=(TSWGScopedPromiseFulfiller&&) = delete;

	~TSWGScopedPromiseFulfiller()
	{
		if (!bSucceeded && Promise.IsValid())
		{
			Promise->SetValue(T());
		}
	}

	void Succeed(T&& Value)
	{
		if (Promise.IsValid())
		{
			Promise->SetValue(MoveTemp(Value));
		}
		bSucceeded = true;
	}

	void Succeed(const T& Value)
	{
		if (Promise.IsValid())
		{
			Promise->SetValue(Value);
		}
		bSucceeded = true;
	}

	/**
	 * For handing off responsibility to another scope without firing the
	 * default here — e.g. the promise is about to be moved into a further
	 * nested continuation that declares its own guard over it. Unlike
	 * Succeed(), does not touch the promise at all.
	 */
	void Dismiss()
	{
		bSucceeded = true;
	}

private:
	TUniquePtr<TPromise<T>>& Promise;
	bool bSucceeded = false;
};
