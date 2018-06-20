; RUN: opt %s -S -internalize -internalize-public-api-list=main -std-link-opts | FileCheck %s

target triple = "wasm32-unknown-unknown-wasm"

@Msg = hidden global [32 x i8] zeroinitializer, align 16
@main.Parms = internal global [64 x i8] zeroinitializer, align 16

; Function Attrs: nounwind
define hidden void @main() local_unnamed_addr {
entry:
  %call150 = call i8* @strcat(
    i8* getelementptr inbounds ([64 x i8], [64 x i8]* @main.Parms, i32 0, i32 0),
    i8* getelementptr inbounds ([32 x i8], [32 x i8]* @Msg, i32 0, i32 0))
  ret void
}

declare hidden i8* @strcpy(i8* noalias returned, i8* noalias) local_unnamed_addr
declare hidden i32 @strlen(i8* %s) local_unnamed_addr


; CHECK: define{{.*}}i8* @strcat(i8*{{.*}}%dest, i8*{{.*}}%src)
; Function Attrs: noinline nounwind optsize
define hidden i8* @strcat(i8* noalias returned %dest, i8* noalias %src) local_unnamed_addr #0 {
entry:
  %call = tail call i32 @strlen(i8* %dest)
  %add.ptr = getelementptr inbounds i8, i8* %dest, i32 %call
  %call1 = tail call i8* @strcpy(i8* %add.ptr, i8* %src)
  ret i8* %dest
}

attributes #0 = { noinline nounwind optsize }
