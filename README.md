# my-c-interpreter
This is my c interpreter project 


只支持 // 注释，如果要实现自举，需要将文件中的 /* */ 替换掉
只支持单文件编译，因为要实现自举（自己编译自己），所以本项目源代码也写成是单个文件
不支持宏定义
只支持三种基类型变量 int，char ， enum
支持多级指针复合类型
支持 if else， 不支持else if 语句
支持 while ， 不支持 do while
函数参数类型只支持int和char为基类型的变量

编程风格类似 C89， 所有函数内的变量定义必须在最前面，只支持 // 注释风格

### 基本概念

递归下降法：本来是应该有终结符，但是我们这里用了优先级爬山，将expresstion作为终结符，大大减少代码复杂度
优先级爬山算法：用于中缀表达式求值，本质就是将中缀表达式转后缀表达式和对后缀表达式求值两个步骤合并进行

语法 grammar： 是文法的合集，  
文法 syntax ： 可以用 ENBF 来表示


词法分析（next()函数）：就是将每一个token的属性解析出来并放入symbol table 里面

语法分析就是根据token 查symbol table 并且生成对应的汇编指令,


token 是符号（symbol）的抽象，同一类的符号的Token属性相同（如Char、Int，If，While，...Id（用户自定义符号））



.text 段：源代码编译之后的汇编指令

### 程序整体执行步骤（从main 函数开始）

1. 分配被编译的程序需要的内存空间（虚拟地址空间）
2. 初始化寄存器（*pc，*bp，*sp，ax）
3. 构建 symbol table 整体框架，预处理该编译器支持的内置关键字和数据类型，加入symbol table中
4. 读取被编译程序源文件到缓冲区中进行编译
5. 词法分析（next()函数）：每次解析以 ; 或者 } 为单位的一条语句或者一个程序块（函数）。在这基础上解析程序中的每一个token，并将属性各个token按其属性值填入symble table
   
