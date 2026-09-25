// Minimal stand-ins for the Unreal types used by Heatline MX's pure gameplay code, so it can be
// compiled and exercised offline with clang. NOT Unreal: just enough semantics for the logic.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using int32 = int32_t;
using uint32 = uint32_t;
using uint8 = uint8_t;
using int64 = int64_t;
using TCHAR = char;
#define TEXT(x) x
#define FORCEINLINE inline
#define INDEX_NONE (-1)
#define KINDA_SMALL_NUMBER (1.e-4f)
#ifndef PI
#define PI (3.1415926535897932f)
#endif
#define UENUM(...)
#define UCLASS(...)
#define USTRUCT(...)
#define UPROPERTY(...)
#define UFUNCTION(...)
#define GENERATED_BODY()
#define UMETA(...)
#define HEATLINEMX_API
#define DECLARE_LOG_CATEGORY_EXTERN(...)
#define DEFINE_LOG_CATEGORY(...)
#define UE_LOG(Cat, Verb, Fmt, ...) std::printf("[%s] " Fmt "\n", #Verb, ##__VA_ARGS__)
#define check(x)

namespace ESearchCase { enum Type { CaseSensitive, IgnoreCase }; }

template <typename T> struct TNumericLimits { static T Max() { return std::numeric_limits<T>::max(); } static T Lowest() { return std::numeric_limits<T>::lowest(); } };

struct FMath
{
	template <typename T> static T Clamp(T V, T A, T B) { return V < A ? A : (V > B ? B : V); }
	template <typename T> static T Min(T A, T B) { return A < B ? A : B; }
	template <typename T> static T Max(T A, T B) { return A > B ? A : B; }
	template <typename T> static T Abs(T A) { return A < 0 ? -A : A; }
	template <typename T> static T Square(T A) { return A * A; }
	static float Lerp(float A, float B, float T) { return A + (B - A) * T; }
	static float Sin(float X) { return std::sin(X); }
	static float Cos(float X) { return std::cos(X); }
	static float Tan(float X) { return std::tan(X); }
	static float Asin(float X) { return std::asin(X); }
	static float Atan2(float Y, float X) { return std::atan2(Y, X); }
	static float Exp(float X) { return std::exp(X); }
	static float Sqrt(float X) { return std::sqrt(X); }
	static float Fmod(float X, float Y) { return std::fmod(X, Y); }
	static int32 FloorToInt(float X) { return (int32)std::floor(X); }
	static int32 CeilToInt(float X) { return (int32)std::ceil(X); }
	static int32 RoundToInt(float X) { return (int32)std::floor(X + 0.5f); }
	static float FloorToFloat(float X) { return std::floor(X); }
	static float DegreesToRadians(float D) { return D * PI / 180.f; }
	static float RadiansToDegrees(float R) { return R * 180.f / PI; }
	static bool IsNearlyEqual(float A, float B, float Tol = 1.e-8f) { return std::fabs(A - B) <= Tol; }
	static bool IsNearlyZero(float A, float Tol = 1.e-8f) { return std::fabs(A) <= Tol; }
	static float FRand() { return (float)std::rand() / (float)RAND_MAX; }
	static float FRandRange(float A, float B) { return A + (B - A) * FRand(); }
	static int32 RandRange(int32 A, int32 B) { return A + std::rand() % (B - A + 1); }
	static float SmoothStep(float A, float B, float X) { const float T = Clamp((X - A) / (B - A), 0.f, 1.f); return T * T * (3.f - 2.f * T); }
	static bool IsFinite(float X) { return std::isfinite(X); }
};

template <typename T>
class TArray
{
public:
	std::vector<T> V;
	TArray() = default;
	TArray(std::initializer_list<T> L) : V(L) {}
	int32 Num() const { return (int32)V.size(); }
	bool IsValidIndex(int32 I) const { return I >= 0 && I < Num(); }
	int32 Add(const T& X) { V.push_back(X); return Num() - 1; }
	int32 Add(T&& X) { V.push_back(std::move(X)); return Num() - 1; }
	template <typename... A> int32 Emplace(A&&... Args) { V.emplace_back(std::forward<A>(Args)...); return Num() - 1; }
	void Append(const TArray& O) { V.insert(V.end(), O.V.begin(), O.V.end()); }
	void Reset() { V.clear(); }
	void Empty() { V.clear(); }
	void Reserve(int32 N) { V.reserve(N); }
	void SetNum(int32 N) { V.resize(N); }
	void SetNumZeroed(int32 N) { V.resize(N); }
	void Init(const T& X, int32 N) { V.assign(N, X); }
	T& Last() { return V.back(); }
	const T& Last() const { return V.back(); }
	T Pop() { T X = V.back(); V.pop_back(); return X; }
	void RemoveAt(int32 I) { V.erase(V.begin() + I); }
	void RemoveAtSwap(int32 I) { V[I] = V.back(); V.pop_back(); }
	void Insert(const T& X, int32 I) { V.insert(V.begin() + I, X); }
	int32 Remove(const T& X) { const size_t N = V.size(); V.erase(std::remove(V.begin(), V.end(), X), V.end()); return (int32)(N - V.size()); }
	template <typename P> int32 RemoveAll(P Pred) { const size_t N = V.size(); V.erase(std::remove_if(V.begin(), V.end(), Pred), V.end()); return (int32)(N - V.size()); }
	bool Contains(const T& X) const { return std::find(V.begin(), V.end(), X) != V.end(); }
	int32 IndexOfByKey(const T& X) const { auto It = std::find(V.begin(), V.end(), X); return It == V.end() ? INDEX_NONE : (int32)(It - V.begin()); }
	template <typename P> int32 IndexOfByPredicate(P Pred) const { for (int32 i = 0; i < Num(); ++i) { if (Pred(V[i])) { return i; } } return INDEX_NONE; }
	template <typename P> const T* FindByPredicate(P Pred) const { for (const T& X : V) { if (Pred(X)) { return &X; } } return nullptr; }
	template <typename P> T* FindByPredicate(P Pred) { for (T& X : V) { if (Pred(X)) { return &X; } } return nullptr; }
	template <typename P> void StableSort(P Pred) { std::stable_sort(V.begin(), V.end(), Pred); }
	template <typename P> void Sort(P Pred) { std::sort(V.begin(), V.end(), Pred); }
	void Sort() { std::sort(V.begin(), V.end()); }
	T& operator[](int32 I) { return V[I]; }
	const T& operator[](int32 I) const { return V[I]; }
	T* GetData() { return V.data(); }
	const T* GetData() const { return V.data(); }
	typename std::vector<T>::iterator begin() { return V.begin(); }
	typename std::vector<T>::iterator end() { return V.end(); }
	typename std::vector<T>::const_iterator begin() const { return V.begin(); }
	typename std::vector<T>::const_iterator end() const { return V.end(); }
};

template <typename T>
class TArrayView
{
public:
	T* Data = nullptr;
	int32 Count = 0;
	TArrayView(TArray<T>& A) : Data(A.GetData()), Count(A.Num()) {}
	int32 Num() const { return Count; }
	T& operator[](int32 I) const { return Data[I]; }
	T* begin() const { return Data; }
	T* end() const { return Data + Count; }
};

template <typename Sig> using TFunctionRef = std::function<Sig>;
template <typename Sig> using TFunction = std::function<Sig>;

class FString
{
public:
	std::string S;
	FString() = default;
	FString(const char* C) : S(C ? C : "") {}
	FString(const std::string& X) : S(X) {}
	FString(int32 Count, const char* C) : S(C, Count) {}
	const char* operator*() const { return S.c_str(); }
	bool IsEmpty() const { return S.empty(); }
	int32 Len() const { return (int32)S.size(); }
	bool operator==(const FString& O) const { return S == O.S; }
	bool operator!=(const FString& O) const { return S != O.S; }
	bool operator<(const FString& O) const { return S < O.S; }
	FString operator+(const FString& O) const { return FString(S + O.S); }
	FString& operator+=(const FString& O) { S += O.S; return *this; }
	FString& operator+=(const char* O) { S += O; return *this; }
	void AppendChar(char C) { S.push_back(C); }
	bool StartsWith(const FString& P) const { return S.rfind(P.S, 0) == 0; }
	bool EndsWith(const FString& P) const { return S.size() >= P.S.size() && S.compare(S.size() - P.S.size(), P.S.size(), P.S) == 0; }
	FString Left(int32 N) const { return FString(S.substr(0, std::max(0, N))); }
	FString RightChop(int32 N) const { return N >= Len() ? FString() : FString(S.substr(N)); }
	void LeftChopInline(int32 N) { S.resize(std::max(0, Len() - N)); }
	bool Equals(const FString& O, ESearchCase::Type C = ESearchCase::CaseSensitive) const
	{
		if (C == ESearchCase::CaseSensitive) { return S == O.S; }
		if (S.size() != O.S.size()) { return false; }
		for (size_t i = 0; i < S.size(); ++i) { if (std::tolower(S[i]) != std::tolower(O.S[i])) { return false; } }
		return true;
	}
	FString TrimStartAndEnd() const { size_t A = S.find_first_not_of(" \t"); if (A == std::string::npos) { return FString(); } size_t B = S.find_last_not_of(" \t"); return FString(S.substr(A, B - A + 1)); }
	static FString FromInt(int32 N) { return FString(std::to_string(N)); }
	static FString Printf(const char* Fmt, ...)
	{
		char Buf[4096];
		va_list Args;
		va_start(Args, Fmt);
		vsnprintf(Buf, sizeof(Buf), Fmt, Args);
		va_end(Args);
		return FString(Buf);
	}
};
inline FString operator+(const char* A, const FString& B) { return FString(A) + B; }

template <typename K, typename V>
struct TPair
{
	K Key;
	V Value;
};

template <typename K, typename V>
class TMap
{
public:
	std::vector<TPair<K, V>> Items;
	V* Find(const K& Key) { for (auto& P : Items) { if (P.Key == Key) { return &P.Value; } } return nullptr; }
	const V* Find(const K& Key) const { for (const auto& P : Items) { if (P.Key == Key) { return &P.Value; } } return nullptr; }
	V& Add(const K& Key, const V& Val) { if (V* E = Find(Key)) { *E = Val; return *E; } Items.push_back({Key, Val}); return Items.back().Value; }
	V& FindOrAdd(const K& Key) { if (V* E = Find(Key)) { return *E; } Items.push_back({Key, V()}); return Items.back().Value; }
	bool Contains(const K& Key) const { return Find(Key) != nullptr; }
	int32 Num() const { return (int32)Items.size(); }
	auto begin() { return Items.begin(); }
	auto end() { return Items.end(); }
	auto begin() const { return Items.begin(); }
	auto end() const { return Items.end(); }
};

template <typename T>
class TSet
{
public:
	std::vector<T> Items;
	void Add(const T& X) { if (!Contains(X)) { Items.push_back(X); } }
	bool Contains(const T& X) const { return std::find(Items.begin(), Items.end(), X) != Items.end(); }
	int32 Num() const { return (int32)Items.size(); }
	auto begin() const { return Items.begin(); }
	auto end() const { return Items.end(); }
};

struct FLinearColor
{
	float R = 0, G = 0, B = 0, A = 1;
	FLinearColor() = default;
	FLinearColor(float r, float g, float b, float a = 1.f) : R(r), G(g), B(b), A(a) {}
	static const FLinearColor White;
	static const FLinearColor Black;
	static FLinearColor LerpUsingHSV(const FLinearColor& X, const FLinearColor& Y, float T) { return FLinearColor(FMath::Lerp(X.R, Y.R, T), FMath::Lerp(X.G, Y.G, T), FMath::Lerp(X.B, Y.B, T), FMath::Lerp(X.A, Y.A, T)); }
};
inline const FLinearColor FLinearColor::White(1, 1, 1, 1);
inline const FLinearColor FLinearColor::Black(0, 0, 0, 1);

struct FRandomStream
{
	uint32 Seed = 1;
	void Initialize(int32 S) { Seed = (uint32)S; }
	float FRand() { Seed = Seed * 196314165u + 907633515u; return (float)(Seed >> 8) / 16777216.f; }
	float FRandRange(float A, float B) { return A + (B - A) * FRand(); }
};

class UObject
{
public:
	virtual ~UObject() = default;
	void AddToRoot() {}
};
template <typename T> const T* GetDefault() { static T Instance; return &Instance; }
template <typename T> class TWeakObjectPtr
{
public:
	T* P = nullptr;
	TWeakObjectPtr() = default;
	TWeakObjectPtr(T* In) : P(In) {}
	bool IsValid() const { return P != nullptr; }
	T* Get() const { return P; }
	T* operator->() const { return P; }
};
template <typename T> T* Cast(UObject*) { return nullptr; }
template <typename T> typename std::remove_reference<T>::type&& MoveTemp(T&& X) { return static_cast<typename std::remove_reference<T>::type&&>(X); }
