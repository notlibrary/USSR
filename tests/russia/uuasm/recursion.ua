.uuasm 1
# Recursive factorial through CALL/RET. The callee's parameter n must be
# restored after each nested call (frames save and restore parameters).
# Expected output: recursion.expect
.const 0 int 1
.const 1 int 6
.const 2 int 10
.string 0 "n"
.string 1 "result"
.string 2 "keep"
.function fact @fact result 1 n

JMP _ _ _ @main

fact:
LOAD_VAR R1 _ _ 0            ; n
LOAD_CONST R2 _ _ 0          ; 1
LE R3 R1 R2 0
JMP_FALSE R3 _ _ @recurse
STORE_VAR R2 _ _ 1           ; result = 1
RET _ _ _ 0
recurse:
SUB R4 R1 R2 0               ; n - 1
CALL R4 1 R5 0               ; R5 = fact(n - 1)
LOAD_VAR R1 _ _ 0            ; n again: must still be this frame's n
MUL R6 R1 R5 0
STORE_VAR R6 _ _ 1
RET _ _ _ 0

main:
LOAD_CONST R7 _ _ 2
STORE_VAR R7 _ _ 0           ; caller's own n = 10, saved across the call
LOAD_CONST R0 _ _ 1
CALL R0 1 R31 0
PRINT R31 _ _ 0              ; 720
LOAD_VAR R8 _ _ 0
PRINT R8 _ _ 0               ; 10 (restored)
LOAD_VAR R9 _ _ 1
PRINT R9 _ _ 0               ; null: result did not exist before the call
HALT _ _ _ 0
