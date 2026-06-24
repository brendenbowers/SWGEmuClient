#pragma once

#include "CoreMinimal.h"

/**
 * FResultBase — base result type without a value.
 * Used for operations that either succeed or fail with an error message.
 *
 * Example:
 *   FResultBase Result = DoSomething();
 *   if (Result.IsSuccess())
 *   {
 *       // Success
 *   }
 *   else
 *   {
 *       UE_LOG(LogTemp, Warning, TEXT("Error: %s"), *Result.GetError());
 *   }
 */
struct FResultBase
{
	FResultBase() = default;

	explicit FResultBase(bool bInSuccess, const FString& InError = FString())
		: bSuccess(bInSuccess), ErrorMessage(InError)
	{
	}

	// Factories
	static FResultBase Success()
	{
		return FResultBase(true);
	}

	static FResultBase Failure(const FString& InError)
	{
		return FResultBase(false, InError);
	}

	bool IsSuccess() const { return bSuccess; }
	bool IsFailure() const { return !bSuccess; }

	const FString& GetError() const { return ErrorMessage; }

protected:
	bool bSuccess = true;
	FString ErrorMessage;
};

/**
 * TResult<T> — generic result type that carries a value on success.
 *
 * Example:
 *   TResult<int32> Result = GetSomeNumber();
 *   if (Result.IsSuccess())
 *   {
 *       int32 Value = Result.GetValue();
 *   }
 *   else
 *   {
 *       UE_LOG(LogTemp, Warning, TEXT("Error: %s"), *Result.GetError());
 *   }
 */
template<typename ValueType>
struct TResult : public FResultBase
{
	using Super = FResultBase;

	TResult() = default;

	explicit TResult(bool bInSuccess, const ValueType& InValue = ValueType(), const FString& InError = FString())
		: Super(bInSuccess, InError), Value(InValue)
	{
	}

	// Factories
	static TResult Success(const ValueType& InValue)
	{
		return TResult(true, InValue);
	}

	static TResult Failure(const FString& InError)
	{
		return TResult(false, ValueType(), InError);
	}

	const ValueType& GetValue() const
	{
		check(IsSuccess());
		return Value;
	}

	ValueType& GetValue()
	{
		check(IsSuccess());
		return Value;
	}

	// Implicit cast to value when success (careful use)
	operator const ValueType&() const { return GetValue(); }

private:
	ValueType Value{};
};

/**
 * Specialization for void results (just success/failure, no value).
 * Equivalent to FResultBase but with explicit type semantics.
 */
template<>
struct TResult<void> : public FResultBase
{
	using Super = FResultBase;

	TResult() = default;

	explicit TResult(bool bInSuccess, const FString& InError = FString())
		: Super(bInSuccess, InError)
	{
	}

	static const TResult& Success()
	{
		static const TResult SuccessInstance(true);
		return SuccessInstance;
	}

	static TResult Failure(const FString& InError)
	{
		return TResult(false, InError);
	}
};