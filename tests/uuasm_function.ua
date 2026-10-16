.uuasm 1
.const 0 int 2
.const 1 int 40
.string 0 "amount"
.string 1 "result"
.function increment @increment result 1 amount

JMP _ _ _ @main
increment:
LOAD_VAR R1 _ _ 0
LOAD_CONST R2 _ _ 0
ADD R3 R1 R2 0
STORE_VAR R3 _ _ 1
RET _ _ _ 0

main:
LOAD_CONST R0 _ _ 1
CALL R0 1 R31 0
PRINT R31 _ _ 0
HALT _ _ _ 0
