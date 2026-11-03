#include "doctest.h"

#include "kr32/cxx/abi.h"

#include <cstdint>
#include <string>

namespace abi = kr32::cxx::abi;

// The ABI is a design contract; these static_asserts make an accidental
// constant change a compile error in the host test suite.

// Data model: KR32 is ILP32 and little-endian.
static_assert(abi::kAbiVersion == 1);
static_assert(abi::kCharSize == 1);
static_assert(abi::kCharAlign == 1);
static_assert(abi::kBoolSize == 1);
static_assert(abi::kBoolAlign == 1);
static_assert(abi::kIntSize == 4);
static_assert(abi::kIntAlign == 4);
static_assert(abi::kPointerSize == 4);
static_assert(abi::kPointerAlign == 4);
static_assert(abi::kReferenceSize == abi::kPointerSize);
static_assert(abi::kReferenceAlign == abi::kPointerAlign);
static_assert(abi::kSizeTSize == abi::kPointerSize);

// Scalar FP layout.
static_assert(abi::kFloatSize == 4);
static_assert(abi::kFloatAlign == 4);
static_assert(abi::kDoubleSize == 8);
static_assert(abi::kDoubleAlign == 8);
static_assert(abi::kLongDoubleSize == abi::kDoubleSize);
static_assert(abi::kLongDoubleAlign == abi::kDoubleAlign);

// Host scalar representations must agree with the target constants. (The host
// pointer size deliberately is not asserted: KR32 is ILP32 on a 64-bit host.)
static_assert(sizeof(float) == abi::kFloatSize);
static_assert(alignof(float) == abi::kFloatAlign);
static_assert(sizeof(double) == abi::kDoubleSize);
static_assert(alignof(double) == abi::kDoubleAlign);

// Public call boundary: SP is 16-byte aligned before CALL and 12-byte offset
// after the 4-byte return address is pushed.
static_assert(abi::kStackSlotSize == 4);
static_assert(abi::kPublicCallAlignment == 16);
static_assert(abi::kReturnAddressSize == 4);
static_assert(abi::kCalleeEntrySpRemainder ==
              abi::kPublicCallAlignment - abi::kReturnAddressSize);
static_assert(abi::kPublicCallAlignment % abi::kDoubleAlign == 0);
static_assert(abi::kPublicCallAlignment % abi::kStackSlotSize == 0);

// General-purpose register roles.
static_assert(abi::kGprCount == 16);
static_assert(abi::kIntegerArgumentFirst == 0);
static_assert(abi::kIntegerArgumentCount == 4);
static_assert(abi::kIntegerReturnRegister == 0);
static_assert(abi::kFramePointerRegister == 7);
static_assert(abi::kThreadPointerRegister == 11);
static_assert(!abi::is_integer_argument_register(abi::kThreadPointerRegister));
static_assert(!abi::is_integer_argument_register(abi::kFramePointerRegister));
static_assert(abi::is_thread_pointer_register(abi::kThreadPointerRegister));

// FP register file: F0..F7 arguments/returns and caller-saved, F8..F15
// callee-saved.
static_assert(abi::kFprCount == 16);
static_assert(abi::kFprSize == 8);
static_assert(abi::kFprArgumentFirst == 0);
static_assert(abi::kFprArgumentCount == 8);
static_assert(abi::kFprReturnFirst == 0);
static_assert(abi::kFprReturnCount == 2);
static_assert(abi::kFprCalleeSavedFirst == abi::kFprArgumentCount);
static_assert(abi::kFprCalleeSavedCount == 8);
static_assert(abi::kFprArgumentCount + abi::kFprCalleeSavedCount ==
              abi::kFprCount);
static_assert(abi::kFprReturnCount <= abi::kFprArgumentCount);

// Aggregate classification, varargs, and FP slot sizes.
static_assert(abi::kAggregateRegisterBytes == 8);
static_assert(abi::kFpAggregateRegisterBytes == 16);
static_assert(abi::kAggregateRegisterBytes <= abi::kFpAggregateRegisterBytes);
static_assert(abi::kVarargSlotSize == 8);
static_assert(abi::kVarargSlotAlignment == 8);
static_assert(abi::kVarargSlotSize >= abi::kDoubleSize);
static_assert(abi::kVarargSlotAlignment >= abi::kDoubleAlign);
static_assert(abi::kVarargSlotSize % abi::kVarargSlotAlignment == 0);

// Tiles, matrices, and the transform stack.
static_assert(abi::kTileCount == 16);
static_assert(abi::kTileRows == 4);
static_assert(abi::kTileColumns == 4);
static_assert(abi::kTileScalarBytes == 4);
static_assert(abi::kTileBytes == 64);
static_assert(abi::kTileBytes ==
              abi::kTileRows * abi::kTileColumns * abi::kTileScalarBytes);
static_assert(abi::kMatrixAlignment == 16);
static_assert(abi::kTransformStackEntryBytes == abi::kTileBytes);
static_assert(abi::kTransformStackAlignment == 64);
static_assert(abi::kMatrixAlignment <= abi::kTransformStackAlignment);

// Vtable and RTTI direction.
static_assert(abi::kVtableTypeInfoOffset == -8);
static_assert(abi::kVtableOffsetToTopOffset == -4);
static_assert(abi::kVtableAddressPointOffset == 0);
static_assert(abi::kVtableTypeInfoOffset < abi::kVtableOffsetToTopOffset);
static_assert(abi::kVtableOffsetToTopOffset < abi::kVtableAddressPointOffset);
static_assert(abi::kTypeInfoVtableOffset == 0);
static_assert(abi::kTypeInfoNameOffset == 4);
static_assert(abi::kTypeInfoBaseOffset == 8);
static_assert(abi::kTypeInfoVtableOffset < abi::kTypeInfoNameOffset);
static_assert(abi::kTypeInfoNameOffset < abi::kTypeInfoBaseOffset);

TEST_CASE("KR32 C++ ABI revision is one") {
    CHECK(abi::kAbiVersion == 1);
}

TEST_CASE("register-class predicates cover the FP register file") {
    for (std::uint32_t index = 0; index < abi::kFprCount; ++index) {
        const bool argument = abi::is_fpr_argument_register(index);
        const bool callee_saved = abi::is_fpr_callee_saved_register(index);
        const bool caller_saved = abi::is_fpr_caller_saved_register(index);

        CHECK((argument || callee_saved));
        CHECK_FALSE((argument && callee_saved));
        CHECK(caller_saved == argument);
        CHECK(abi::is_fpr_register(index));
    }

    CHECK_FALSE(abi::is_fpr_register(abi::kFprCount));
    CHECK_FALSE(abi::is_fpr_argument_register(abi::kFprArgumentCount));
    CHECK_FALSE(abi::is_fpr_callee_saved_register(abi::kFprCalleeSavedFirst - 1));
    CHECK(abi::is_fpr_callee_saved_register(abi::kFprCalleeSavedFirst));
    CHECK(abi::is_fpr_callee_saved_register(abi::kFprCount - 1));
}

TEST_CASE("integer argument registers and reserved registers") {
    for (std::uint32_t index = 0; index < abi::kIntegerArgumentCount; ++index) {
        CHECK(abi::is_integer_argument_register(index));
    }
    CHECK_FALSE(abi::is_integer_argument_register(abi::kIntegerArgumentCount));
    CHECK(abi::is_thread_pointer_register(abi::kThreadPointerRegister));
    CHECK_FALSE(abi::is_thread_pointer_register(abi::kFramePointerRegister));
    CHECK(abi::is_gpr_register(abi::kThreadPointerRegister));
    CHECK(abi::is_gpr_register(abi::kGprCount - 1));
    CHECK_FALSE(abi::is_gpr_register(abi::kGprCount));
}

TEST_CASE("tile registers are caller-saved and bounded") {
    for (std::uint32_t index = 0; index < abi::kTileCount; ++index) {
        CHECK(abi::is_tile_register(index));
    }
    CHECK_FALSE(abi::is_tile_register(abi::kTileCount));
}

TEST_CASE("aggregate memory class and FPR argument capacity") {
    CHECK_FALSE(abi::is_aggregate_memory_class(0));
    CHECK_FALSE(abi::is_aggregate_memory_class(abi::kAggregateRegisterBytes));
    CHECK(abi::is_aggregate_memory_class(abi::kAggregateRegisterBytes + 1));
    CHECK(abi::is_aggregate_memory_class(abi::kTileBytes));

    CHECK(abi::fits_in_fpr_arguments(0));
    CHECK(abi::fits_in_fpr_arguments(abi::kFprArgumentCount));
    CHECK_FALSE(abi::fits_in_fpr_arguments(abi::kFprArgumentCount + 1));
}

TEST_CASE("align_up rounds to the requested alignment") {
    CHECK(abi::align_up(0, 4) == 0);
    CHECK(abi::align_up(1, 4) == 4);
    CHECK(abi::align_up(4, 4) == 4);
    CHECK(abi::align_up(5, 4) == 8);
    CHECK(abi::align_up(1, 8) == 8);
    CHECK(abi::align_up(8, 8) == 8);
    CHECK(abi::align_up(9, 8) == 16);
    CHECK(abi::align_up(17, 16) == 32);
    CHECK(abi::align_up(16, 16) == 16);
}

TEST_CASE("matrix and transform-stack layout is consistent") {
    CHECK(abi::kTileBytes == 64);
    CHECK(abi::kTransformStackEntryBytes == abi::kTileBytes);
    CHECK(abi::kTransformStackAlignment % abi::kMatrixAlignment == 0);
}

TEST_CASE("mangled-name prefix identifies C++ symbols") {
    CHECK(std::string(abi::kMangledNamePrefix) == "_Z");
}
