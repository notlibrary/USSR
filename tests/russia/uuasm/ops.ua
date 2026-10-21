.uuasm 1
# Value, arithmetic, logic, variable, hash, vector, string and branch
# opcodes. Expected output: ops.expect
.const 0 int 7
.const 1 int 3
.const 2 real 2.5
.const 3 string "ab"
.const 4 string "cd\t!"
.const 5 bool true
.const 6 null
.const 7 int 0
.const 8 int 1
.const 9 int 5
.string 0 "x"
.string 1 "key"
.string 2 "vec"
.string 3 "push"
.string 4 "len"
.string 5 "[10,20,30]"
.string 6 "never_set"
.string 7 "v"

LOAD_CONST R1 _ _ 0          ; 7
LOAD_CONST R2 _ _ 1          ; 3
LOAD_CONST R4 _ _ 2          ; 2.5
LOAD_CONST R5 _ _ 5          ; true
LOAD_CONST R7 _ _ 7          ; 0
LOAD_CONST R8 _ _ 8          ; 1
ADD R3 R1 R2 0
PRINT R3 _ _ 0
SUB R3 R1 R2 0
PRINT R3 _ _ 0
MUL R3 R1 R2 0
PRINT R3 _ _ 0
DIV R3 R1 R2 0
PRINT R3 _ _ 0
MOD R3 R1 R2 0
PRINT R3 _ _ 0
NEG R3 R1 _ 0
PRINT R3 _ _ 0
ADD R3 R1 R4 0
PRINT R3 _ _ 0
EQ R3 R1 R2 0
PRINT R3 _ _ 0
NE R3 R1 R2 0
PRINT R3 _ _ 0
LT R3 R1 R2 0
PRINT R3 _ _ 0
LE R3 R2 R1 0
PRINT R3 _ _ 0
GT R3 R1 R2 0
PRINT R3 _ _ 0
GE R3 R2 R1 0
PRINT R3 _ _ 0
LAND R3 R5 R7 0
PRINT R3 _ _ 0
LOR R3 R5 R7 0
PRINT R3 _ _ 0
XOR R3 R1 R2 0
PRINT R3 _ _ 0
BAND R3 R1 R2 0
PRINT R3 _ _ 0
BOR R3 R1 R2 0
PRINT R3 _ _ 0
SHL R3 R1 R8 0
PRINT R3 _ _ 0
SHR R3 R1 R8 0
PRINT R3 _ _ 0

# strings and variables
LOAD_CONST R0 _ _ 3
LOAD_CONST R1 _ _ 4
CONCAT R2 _ 2 0
PRINT R2 _ _ 0
SET R2 _ _ 0
LOAD_VAR R10 _ _ 0
PRINT R10 _ _ 0
GET R11 _ _ 0
PRINT R11 _ _ 0
LOAD_VAR R12 _ _ 6
PRINT R12 _ _ 0
LOAD_CONST R6 _ _ 6
PRINT R6 _ _ 0

# hash namespace
LOAD_CONST R3 _ _ 9
STORE_HASH R3 _ _ 1
LOAD_HASH R13 _ _ 1
PRINT R13 _ _ 0

# vectors: there is no register move, so values reach R0 through a variable
OOP R14 0 _ 2
STORE_VAR R14 _ _ 7
LOAD_VAR R0 _ _ 7
LOAD_CONST R1 _ _ 0
OOP R15 2 _ 3
LOAD_VAR R0 _ _ 7
OOP R15 1 _ 4
PRINT R15 _ _ 0
VECTOR_GET R16 R14 R7 0
PRINT R16 _ _ 0
LOAD_CONST R9 _ _ 9
VECTOR_SET R14 R7 R9 0
VECTOR_GET R16 R14 R7 0
PRINT R16 _ _ 0
DECODE_UNO R17 _ _ 5
VECTOR_GET R18 R17 R8 0
PRINT R18 _ _ 0

# branches
JMP_FALSE R7 _ _ @zero_is_false
PRINT R7 _ _ 0
zero_is_false:
LOAD_CONST R0 _ _ 3
JMP_TRUE R0 _ _ @nonempty_is_true
PRINT R7 _ _ 0
nonempty_is_true:
PRINT R0 _ _ 0
NOP _ _ _ 0
HALT _ _ _ 0
