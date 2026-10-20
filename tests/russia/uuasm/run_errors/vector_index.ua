.uuasm 1
.string 0 "[1,2]"
.const 0 int 5
DECODE_UNO R0 _ _ 0
LOAD_CONST R1 _ _ 0
VECTOR_GET R2 R0 R1 0
HALT _ _ _ 0
