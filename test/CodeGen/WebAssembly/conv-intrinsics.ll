; RUN: llc < %s -asm-verbose=false -disable-wasm-fallthrough-return-opt -disable-wasm-explicit-locals | FileCheck %s

; Test that basic memory operations assemble as expected with 32-bit addresses.

target datalayout = "e-m:e-p:32:32-i64:64-n32:64-S128"
target triple = "wasm32-unknown-unknown-wasm"

declare i32 @llvm.wasm.trapping.ftoui.i32.f32(float) nounwind readonly
declare i64 @llvm.wasm.trapping.ftoui.i64.f32(float) nounwind readonly
declare i32 @llvm.wasm.trapping.ftoui.i32.f64(double) nounwind readonly
declare i64 @llvm.wasm.trapping.ftoui.i64.f64(double) nounwind readonly
declare i32 @llvm.wasm.trapping.ftosi.i32.f32(float) nounwind readonly
declare i64 @llvm.wasm.trapping.ftosi.i64.f32(float) nounwind readonly
declare i32 @llvm.wasm.trapping.ftosi.i32.f64(double) nounwind readonly
declare i64 @llvm.wasm.trapping.ftosi.i64.f64(double) nounwind readonly

declare i32 @llvm.wasm.trapping.ftoui.i32.f128(fp128) nounwind readonly

; CHECK-LABEL: ftoi_intrin:
; CHECK-NEXT: .param f32, f64{{$}}
; CHECK-NEXT: i32.trunc_u/f32 $drop=, $0{{$}}
; CHECK-NEXT: i64.trunc_u/f32 $drop=, $0{{$}}
; CHECK-NEXT: i32.trunc_u/f64 $drop=, $1{{$}}
; CHECK-NEXT: i64.trunc_u/f64 $drop=, $1{{$}}
; CHECK-NEXT: i32.trunc_s/f32 $drop=, $0{{$}}
; CHECK-NEXT: i64.trunc_s/f32 $drop=, $0{{$}}
; CHECK-NEXT: i32.trunc_s/f64 $drop=, $1{{$}}
; CHECK-NEXT: i64.trunc_s/f64 $drop=, $1{{$}}
define void @ftoi_intrin(float %float, double %double, fp128 %long_double) {
  %f32toui32 = call i32 @llvm.wasm.trapping.ftoui.i32.f32(float %float)
  %f32toui64 = call i64 @llvm.wasm.trapping.ftoui.i64.f32(float %float)
  %f64toui32 = call i32 @llvm.wasm.trapping.ftoui.i32.f64(double %double)
  %f64toui64 = call i64 @llvm.wasm.trapping.ftoui.i64.f64(double %double)
  %f32tosi32 = call i32 @llvm.wasm.trapping.ftosi.i32.f32(float %float)
  %f32tosi64 = call i64 @llvm.wasm.trapping.ftosi.i64.f32(float %float)
  %f64tosi32 = call i32 @llvm.wasm.trapping.ftosi.i32.f64(double %double)
  %f64tosi64 = call i64 @llvm.wasm.trapping.ftosi.i64.f64(double %double)
  ret void
}
