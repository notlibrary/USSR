.uuasm 1
.string 0 "r"
.function f @f r 1 x
JMP _ _ _ @main
f:
RET _ _ _ 0
main:
CALL R0 2 R1 0
HALT _ _ _ 0
