This are some of system prompts collection that I used to create USSR with the AI
I saved them just in case


Here's your files they are diff in few lines of what you have
After fixing quadratic equation we must add
do(_): loop(_): {conditon expression}
and -e option which evals code in the optarg
And the main problem is porting on Windows fork-exe calls in ussr.c
which must be done platform independable way by
generic launch process interface file process.h and 2 realizations process_win32.c and process_posix.c
Starting from this point we must care about windows portability of whole codebase
And last one change the parser to support `f(_):` calls without argument analog of C `f()`
make the colon : in the end mandatory and empty args "" optional so f(_): f(_): "" are both valid
and f(_) is not because there is no colon at the end
this is important do not confuse f(_) and f(_): there must be gammatical difference
colon in the end is necessary feature to distinguish it from TCL
also change == operator to = because there is set instead of =
Also ensure that scan "REAL;" actually decode floats and not treat them as string

Now we fixed almost everything
we need now
do(_):

loop(_): {conditon expression}

cycle and -e option which evals optarg

And last one change the parser to support `f(_):` calls without argument analog of C `f()`
make the colon : in the end mandatory and empty args "" optional so f(_): f(_): "" are both valid
and f(_) is not because there is no colon at the end
this is important do not confuse f(_) and f(_): there must be gammatical difference
colon in the end is necessary feature to distinguish it from TCL

Now let's concentrate on building state-of-art autocompleter for USSR
It must be fully autonomous from other modules get partial input from bestline
detect Tab key press complete paths filenames commands names commands options using history and ls data
and actual user script source excerpt
it must have an AI completion submodule which asks AI if user pressed tab more than 3 times
using API keys provided by user
It must use and analyze detail empirical environmental data very smart and carefully
It must be OS independable
Using empiric algorithms is a key to success
It works only in REPL mode

Before we start we must add to grammar path detector to run commands with path supporting .
eg.  `./ussr(_): "script.su"`
	`C:\Users\serenity\binary.exe(_): ""`
	
Split `command_name lparent return_var rparent colon` into `command_path slash command_name lparent return_var rparent colon`
command_name is a thing beween lparent and last_slash it can has dots
Structure that contaun commands must have separate command path member

The AI powered autocompeter is the key point where we can demonstrate superiority of AI era in IT

Let's now fix Windows build I restored it to base here's files fix it

Microsoft (R) Program Maintenance Utility Version 14.44.35228.0
Copyright (C) Microsoft Corporation.  All rights reserved.

        cl.exe /nologo /W3 /Zi /Od /D_CRT_SECURE_NO_WARNINGS /DYY_NO_UNISTD_H /Isrc /Isrc\win /c /Fo:src\main.obj src\main.c
main.c
Importing getopt library
C:\Users\Serenity\ussr\src\bestline.h(28): error C2371: 'worstlineHistoryAdd': redefinition; different basic types
C:\Users\Serenity\ussr\src\win/worstline.h(31): note: see declaration of 'worstlineHistoryAdd'
C:\Users\Serenity\ussr\src\bestline.h(36): warning C4028: formal parameter 1 different from declaration
src\main.c(1332): warning C4013: 'worstlineSetCompletionCallback' undefined; assuming extern returning int
src\main.c(1507): warning C4244: 'function': conversion from 'time_t' to 'unsigned int', possible loss of data
NMAKE : fatal error U1077: 'cl.exe /nologo /W3 /Zi /Od /D_CRT_SECURE_NO_WARNINGS /DYY_NO_UNISTD_H /Isrc /Isrc\win /c /Fo:src\main.obj src\main.c' : return code '0x2'
Stop.
PS C:\Users\Serenity\ussr>

{{language
|exec=bytecode
|site=https://github.com/notlibrary/USSR
|gc=yes
|safety=safe
|strength=strong
|checking=dynamic
}}

'''USSR''' ("Unified Shell Script") is a small, command-oriented scripting language: every operation is written as a ''command'' with a name, a return variable, and a list of arguments — <code>name(return_variable): argument argument ...</code> — rather than as a traditional expression-and-statement grammar.

Programs are compiled to a small register-based bytecode and run on a dedicated virtual machine, rather than being interpreted directly from the parse tree. The language includes a lightweight struct/vector object system and '''UNO''' (Unified Object Notation), a compact text format for encoding a struct or vector into a string, a file, or a literal written directly in source.

USSR is implemented in C and is free/open-source software; the reference implementation is available on [https://github.com/notlibrary/USSR GitHub].

[[Category:Programming Languages]]

Now let'd add `template` builtin command which is analog of C printf
It uses the same type tokens syntax as scan
It must pass 99 bottles test:
It must have ';' escape

init(ret): arg_cnt arg_vec [ 
	set(a): 99
	while(b): {a>0} [
		template(_): "INT;STR;INT;STR;STR;INT;STR" a "bottles of beer on the wall\n" a "bottles of beer\n" "Take one down, pass it around\n" {a-1} "bottles of beer on the wall\n\n"
		set(a): {a-1}
	]
]

Use ussr_bytecode.c and ussr.c from library

Now let's create for loop as 3 commands for:(_) {condition}

Example for:(_) {i<100}
set(i): 0
set(i): {i+1}
[ print(_): i ]

After that use OOP system to make 100 doors iterative test solution
Example of OOP
vec(bag): ""
push(_): bag 1
push(_): bag "two"
push(_): bag true
push(_): bag null

len(n): bag
print(_): n       # 4

at(item0): bag 0
at(item1): bag 1
at(item2): bag 2
at(item3): bag 3
print(_): item0   # 1
print(_): item1   # two
print(_): item2   # true
print(_): item3   # null

So using grammar to handle for loops was an overkill
Now we just using usual command syntax
for(_): {condition_expr}
[
	intializer 
	iterator
	body
]
When it compiles the loop it knows that
Initializer executes first one time and iterator last but every iteration and body in between
Keyword is executes when written in code it goes like intializer iterator body

Sample for nesting

for(_):{i<10)}
[
 set(i): 0
 set(i): {i+1}
		 for{j<10}[
		 set(j): 0
		 set(j): {j+1}
		 template(_): "INT; INT" i j
	]
]

Also continue must jump to iterator in such configuration
and initializer and iterator are not optional

Now the problem we face there is no get set support for vectors
we must do it through builtin get set commands with 2 parameters
i.e set(vec): index value get(vec): index output

When it's done you can rewrite 100doors_iterative.su with newly generated
nested loops and vectors features

We must tightly integrate vectors for loos and get set to make 100 doors feasible

The problem that for loops not working neither nested nor simple

nordom@SERENITY:/mnt/c/users/serenity/ussr$ ./ussr tests/100doors_iterative.su
USSR compiler: for requires exactly initializer, iterator, and body commands
USSR compiler error: failed to compile body of function 'init'
USSR compiler: compilation failednordom@SERENITY:/mnt/c/users/serenity/ussr$

USSR compiler: compilation failednordom@SERENITY:/mnt/c/users/serenity/ussr$ ./ussr tests/nested_loop.su
USSR: syntax error at line 5
USSR parser: syntax errornordom@SERENITY:/mnt/c/users/serenity/ussr$ more tests/nested_loop.su
init(ret): arg_cnt arg_vec [
for(_):{i<10)} [
set(i): 0
set(i): {i+1}
		 for(_): {j<10} [
		 set(j): 0
		 set(j): {j+1}
		 template(_): "INT; INT" i j
		]
	]
]
nordom@SERENITY:/mnt/c/users/serenity/ussr$

I putted in archive whole needed files let's debug
May be it should run intializer before body condition check?

Here'a 4 failing ussr quines
use template output to make working version

#Compact USSR quine
set(c): " "
set(a): " "
set(n): " "
concat(c): " " ":"
concat(a): " " "\""
concat(n): "" "\n"
set(q): "concat(o): \"set(c): \" a a n \"set(a): \" a a n \"set(n): \" a a n \"concat(c): \" a a \" \":\"\" n \"concat(a): \" a a \" \"\\\"\"\" n \"concat(n): \" a a \" \"\\\\n\"\" n \"set(q): \" a q a n \"eval(ret): q\" n \"print(out): o\""
eval(ret): q
print(out): o

#Compact USSR quine 2
set(c): ""
set(a): ""
set(n): ""
concat(c): "" ":"
concat(a): "" "\""
concat(n): "" "\n"
set(q): "set(c): \"\""
concat(o): q n "set(a): \"\"" n "set(n): \"\"" n "concat(c): \"\" \":\"" n "concat(a): \"\" \"\\\"\"" n "concat(n): \"\" \"\\n\"" n "set(q): " a q a n "concat(o): q n \"set(a): \\\"\\\"\" n \"set(n): \\\"\\\"\" n \"concat(c): \\\"\\\" \\\":\\\"\" n \"concat(a): \\\"\\\" \\\"\\\\\\\"\\\"\" n \"concat(n): \\\"\\\" \\\"\\\\n\\\"\" n \"set(q): \" a q a n \"print(out): o\"" n "print(out): o"
print(out): o

$define A() {"$define"}
$define B() {" A() {}"}
eval A() + B()

#4 - Self-building USSR quine
set(a): "set(a): "
set(b): "concat(a): a "
set(c): "print(out): a"
concat(a): a "\""
concat(a): a a
concat(a): a "\""
concat(a): a "\n"
concat(a): a b
concat(a): a "\""
concat(a): a a
concat(a): a "\""
concat(a): a "\n"
concat(a): a c
print(out): a

Now let's finish off the conditionals 
We need `else elif` commands and `choose option default` commands analog of C `switch case default`
we need break supporting break out of option expression
Example choose
choose(_): command
option(_): 1
break():
option(_): 2
[
	print(_): "push"
	break(_)
]
option(_): 3
default(_):
break(_):


Example elif else
if(_): { a>b }
elif(_): {a > 100}
else(_):
print(_): "a>b; a<100"

Also add two tests one for choose and one for elif else

Note with this two and do loop we can now construct Duff's device

Now the conditionals fail the test

# choose / option / default / break test
set(a): 2

choose(_): a
option(_): 1
print(_): "one"
option(_): 2
print(_): "two"
break()
option(_): 3
print(_): "three"
default(_): " "
print(_): "default"

USSR parser: syntax errornordom@SERENITY:/mnt/c/users/serenity/ussr$ ./ussr tests/choose.su
USSR: syntax error at line 12
USSR parser: syntax errornordom@SERENITY:/mnt/c/users/serenity/ussr$

Find the bug with choose and fix it

Now the problem that command chaining and piping are giant and unusable
to solve it we introduce new feature "Advanced control block"
`@[ commands_list ]`
it's grammatically and lexical distinct from usual block it stats with at symbol @ and 
filled with list of commands which require list of commands as parameter so chaining with piping becomes trivial 
```USSR
@[ 
	chain(_):
    file(_): "ps_output.txt"
]
[
	ps(_): "-aux"
	grep(_): "ussr"
]
```
Also we may add another reflexive list manipulation commands i.e `filter` to separate system commands from builtin
reverse each e.t.c. implement it as you wish
Do not forget to add the test chain.su

It works perfectly fine