.uuasm 1
.const 0 int 4
.const 1 int 1
.const 2 int 0
.string 0 "counter"
.string 1 "sum"

LOAD_CONST R0 _ _ 0
STORE_VAR R0 _ _ 0
LOAD_CONST R1 _ _ 2
STORE_VAR R1 _ _ 1

loop:
LOAD_VAR R2 _ _ 0
LOAD_CONST R3 _ _ 2
EQ R4 R2 R3 0
JMP_TRUE R4 _ _ @done
LOAD_VAR R1 _ _ 1
ADD R1 R1 R2 0
STORE_VAR R1 _ _ 1
LOAD_CONST R3 _ _ 1
SUB R2 R2 R3 0
STORE_VAR R2 _ _ 0
JMP _ _ _ @loop

done:
PRINT R1 _ _ 0
HALT _ _ _ 0
