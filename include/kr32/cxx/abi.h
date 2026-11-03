#pragma once

// KR32 C++ ABI revision 1 (M9).
//
// This header is the machine-checkable companion to docs/abi.md. It contains
// only stable layout/calling-convention constants: it does not depend on the
// compiler implementation, the front end, or any host C++ ABI. The values here
// are the design contract that later lexer/parser/codegen/runtime work must
// follow; changing one requires a docs/abi.md change and an ABI revision.
//
// KR32 is ILP32 and little-endian. Nothing in this header implies binary
// compatibility with MSVC, GCC, Clang, or the host standard library.

#include <cstdint>

namespace kr32::cxx::abi {

// ---------------------------------------------------------------------------
// ABI revision and target data model
// ---------------------------------------------------------------------------

inline constexpr std::uint32_t kAbiVersion = 1;

inline constexpr std::uint32_t kByteBits = 8;
inline constexpr std::uint32_t kCharSize = 1;
inline constexpr std::uint32_t kCharAlign = 1;
inline constexpr std::uint32_t kBoolSize = 1;
inline constexpr std::uint32_t kBoolAlign = 1;
inline constexpr std::uint32_t kIntSize = 4;
inline constexpr std::uint32_t kIntAlign = 4;
inline constexpr std::uint32_t kPointerSize = 4;
inline constexpr std::uint32_t kPointerAlign = 4;
inline constexpr std::uint32_t kReferenceSize = kPointerSize;
inline constexpr std::uint32_t kReferenceAlign = kPointerAlign;
inline constexpr std::uint32_t kSizeTSize = kPointerSize;
inline constexpr std::uint32_t kSizeTAlign = kPointerAlign;

// Scalar floating point. `long double` is an alias for `double` in ABI
// revision 1: same size, alignment, representation, and mangling.
inline constexpr std::uint32_t kFloatSize = 4;
inline constexpr std::uint32_t kFloatAlign = 4;
inline constexpr std::uint32_t kDoubleSize = 8;
inline constexpr std::uint32_t kDoubleAlign = 8;
inline constexpr std::uint32_t kLongDoubleSize = kDoubleSize;
inline constexpr std::uint32_t kLongDoubleAlign = kDoubleAlign;

// Size of one general stack slot. FP values on the stack occupy a whole
// 8-byte slot when they are 8 bytes wide (see kVarargSlotSize).
inline constexpr std::uint32_t kStackSlotSize = 4;

// Public call boundary alignment. SP is 16-byte aligned immediately before a
// CALL. CALL pushes a 4-byte return address, so a callee normally enters with
// SP % 16 == 12 and realigns in its prologue before making nested calls.
inline constexpr std::uint32_t kPublicCallAlignment = 16;
inline constexpr std::uint32_t kReturnAddressSize = 4;
inline constexpr std::uint32_t kCalleeEntrySpRemainder =
    kPublicCallAlignment - kReturnAddressSize;

// ---------------------------------------------------------------------------
// General-purpose registers
// ---------------------------------------------------------------------------

inline constexpr std::uint32_t kGprCount = 16;
inline constexpr std::uint32_t kIntegerArgumentFirst = 0;
inline constexpr std::uint32_t kIntegerArgumentCount = 4;
inline constexpr std::uint32_t kIntegerReturnRegister = 0;
inline constexpr std::uint32_t kFramePointerRegister = 7;

// `R11` is the C++ thread pointer (TP). It is reserved and preserved across
// calls and context switches; it is not available to the general register
// allocator in C++ translation units. C code uses only R0-R7 today and never
// sets or depends on R11.
inline constexpr std::uint32_t kThreadPointerRegister = 11;
inline constexpr const char* kThreadPointerRegisterName = "R11";

// ---------------------------------------------------------------------------
// Floating-point registers
// ---------------------------------------------------------------------------

// Each FPR is a 64-bit IEEE-754 storage unit. `float` values occupy bits 31..0
// with the upper 32 bits zero; `double` occupies all 64 bits.
inline constexpr std::uint32_t kFprCount = 16;
inline constexpr std::uint32_t kFprSize = 8;
inline constexpr std::uint32_t kFprArgumentFirst = 0;
inline constexpr std::uint32_t kFprArgumentCount = 8;
inline constexpr std::uint32_t kFprReturnFirst = 0;
inline constexpr std::uint32_t kFprReturnCount = 2;
inline constexpr std::uint32_t kFprCalleeSavedFirst = kFprArgumentCount;
inline constexpr std::uint32_t kFprCalleeSavedCount =
    kFprCount - kFprCalleeSavedFirst;

// ---------------------------------------------------------------------------
// Aggregates, varargs, and the soft-float-free call model
// ---------------------------------------------------------------------------

// Trivial aggregates up to 8 bytes can travel in integer/FPR registers.
// Aggregates larger than 8 bytes are memory class in the first ABI revision
// (passed through an invisible pointer; returned through a hidden sret
// pointer). FP-only aggregates up to 16 bytes may return in F0/F1.
inline constexpr std::uint32_t kAggregateRegisterBytes = 8;
inline constexpr std::uint32_t kFpAggregateRegisterBytes = 16;

// Variadic arguments are stack-passed in 8-byte, 8-byte-aligned slots.
// `float` promotes to `double`; small integer types promote to `int`. A
// va_list is a 4-byte pointer to the first variadic slot and advances by one
// slot per va_arg. Named arguments keep the normal register ABI.
inline constexpr std::uint32_t kVarargSlotSize = 8;
inline constexpr std::uint32_t kVarargSlotAlignment = 8;

// ---------------------------------------------------------------------------
// Tiles, matrices, and the transform stack
// ---------------------------------------------------------------------------

inline constexpr std::uint32_t kTileCount = 16;
inline constexpr std::uint32_t kTileRows = 4;
inline constexpr std::uint32_t kTileColumns = 4;
inline constexpr std::uint32_t kTileScalarBytes = kFloatSize;
inline constexpr std::uint32_t kTileBytes =
    kTileRows * kTileColumns * kTileScalarBytes;
inline constexpr std::uint32_t kMatrixAlignment = 16;

inline constexpr std::uint32_t kTransformStackEntryBytes = kTileBytes;
inline constexpr std::uint32_t kTransformStackAlignment = 64;

// ---------------------------------------------------------------------------
// Name mangling
// ---------------------------------------------------------------------------

inline constexpr const char* kMangledNamePrefix = "_Z";

// ---------------------------------------------------------------------------
// Virtual dispatch and RTTI layout (single inheritance only in revision 1)
// ---------------------------------------------------------------------------
//
//   vptr -> address point
//           [ -8 ] pointer to type_info
//           [ -4 ] offset-to-top (0 with single inheritance)
//           [  0 ] first virtual function slot
//
// Slot order follows declaration order; overrides replace the base slot. A
// virtual destructor occupies two consecutive slots (complete D1, deleting D0).

inline constexpr std::int32_t kVtableTypeInfoOffset = -8;
inline constexpr std::int32_t kVtableOffsetToTopOffset = -4;
inline constexpr std::int32_t kVtableAddressPointOffset = 0;

// type_info: [0] vtable pointer, [4] mangled-name pointer. The single-
// inheritance form (__si_class_type_info) adds [8] a base type_info pointer.
inline constexpr std::uint32_t kTypeInfoVtableOffset = 0;
inline constexpr std::uint32_t kTypeInfoNameOffset = 4;
inline constexpr std::uint32_t kTypeInfoBaseOffset = 8;

// ---------------------------------------------------------------------------
// Helper predicates and arithmetic
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr bool is_gpr_register(std::uint32_t index) {
    return index < kGprCount;
}

[[nodiscard]] constexpr bool is_integer_argument_register(std::uint32_t index) {
    return index < kIntegerArgumentCount;
}

[[nodiscard]] constexpr bool is_fpr_register(std::uint32_t index) {
    return index < kFprCount;
}

[[nodiscard]] constexpr bool is_fpr_argument_register(std::uint32_t index) {
    return index < kFprArgumentCount;
}

[[nodiscard]] constexpr bool is_fpr_return_register(std::uint32_t index) {
    return index < kFprReturnCount;
}

[[nodiscard]] constexpr bool is_fpr_callee_saved_register(std::uint32_t index) {
    return index >= kFprCalleeSavedFirst && index < kFprCount;
}

[[nodiscard]] constexpr bool is_fpr_caller_saved_register(std::uint32_t index) {
    return index < kFprCalleeSavedFirst;
}

[[nodiscard]] constexpr bool is_tile_register(std::uint32_t index) {
    return index < kTileCount;
}

[[nodiscard]] constexpr bool is_thread_pointer_register(std::uint32_t index) {
    return index == kThreadPointerRegister;
}

[[nodiscard]] constexpr bool fits_in_fpr_arguments(std::uint32_t count) {
    return count <= kFprArgumentCount;
}

// True when a non-FP-only aggregate exceeds the register-class threshold.
// FP-only aggregates up to kFpAggregateRegisterBytes are classified separately
// and non-trivial types are always memory class.
[[nodiscard]] constexpr bool is_aggregate_memory_class(std::uint32_t size) {
    return size > kAggregateRegisterBytes;
}

[[nodiscard]] constexpr std::uint32_t align_up(std::uint32_t value,
                                               std::uint32_t alignment) {
    return (value + alignment - 1U) / alignment * alignment;
}

// TCB layout from TP (R11); exact offsets frozen in M9.10.
inline constexpr std::uint32_t kTcbSelf = 0;
inline constexpr std::uint32_t kTcbThreadId = 4;
inline constexpr std::uint32_t kTcbStackBase = 8;
inline constexpr std::uint32_t kTcbStackLimit = 12;
inline constexpr std::uint32_t kTcbTransformStack = 16;
inline constexpr std::uint32_t kTcbFprSave = 20;
inline constexpr std::uint32_t kTcbCurrentException = 24;
inline constexpr std::uint32_t kTcbReserved = 28;
inline constexpr std::uint32_t kTcbTls = 32;
inline constexpr std::uint32_t kTcbHeaderSize = 160;
inline constexpr std::uint32_t kFprContextSize = 128;
inline constexpr std::uint32_t kTcbBlockSize = kTcbHeaderSize + kFprContextSize;

} // namespace kr32::cxx::abi
