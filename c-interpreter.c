#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#define int long long // work with 64bit target
//#define int intptr_t


int token;            // current token
int token_val;        // value of current token (mainly for number)
char *src, *old_src;  // pointer to source code string;
int poolsize;         // default size of text/data/stack
int line;             // line number


int *text,            // text segment
    *old_text,        // for dump text segment
    *stack;           // stack
char *data;           // data segment

int *current_id,      // current parsed ID
    *symbols;         // symbol table，这可以“看做”是个“结构体数组”
int *idmain;          // the `main` function

int *pc, *bp, *sp, ax, cycle; // virtual machine registers

// instructions
enum { LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,
       OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,
       OPEN, READ, CLOS, PRTF, MALC, MSET, MCMP, EXIT };


/* tokens and classes (operators last and in precedence order)
这些代表的是token被解析之后的对应的分类，同一类的表示符设置同样的属性，
从129开始是因为先前定义了其他枚举变量防止冲突*/
//注意这里面小写的Char 和Int指的是token的类型或者class类型，用于赋值token 和 class
enum {
  Num = 128, Fun, Sys, Glo, Loc, Id, //class
  Char, Else, Enum, If, Int, Return, Sizeof, While, // type
  Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak
};/*最后一行按顺序排列的是运算符的优先级，我们可以看到在后续调用express(Assign)原因是Assign
的运算符的优先级是最低的，根据优先级爬山算法我们可以逐步求解出表达式*/

// fields of identifier
enum {Token, Hash, Name, Type, Class, Value, GType, GClass, GValue, IdSize};

// basetypes of variable/function (本程序只支持 3 种基类型的变量)
//注意这里大写的 CHAR 和 INT 指的是变量或者是函数的返回类型，用于赋值type的枚举量
enum { CHAR, INT, PTR };

int basetype;    // the type of a declaration, make it global for convenience
int expr_type;   // the type of an expression

// function frame
//
// 0: arg 1
// 1: arg 2
// 2: arg 3
// 3: return address
// 4: old bp pointer  <- index_of_bp
// 5: local var 1
// 6: local var 2
int index_of_bp; // index of bp pointer on stack


/* 词法分析
  1. for lexical analysis; 
  2. get the next token; 
  3. will ignore spaces tabs etc.*/
void next() {

    char *current_ch;
    int hash;//用于计算哈希值的临时变量

    /*parse token 不断读取字符
    （事实上正儿八经的词法分析都在if 和 else if里，这里的src++用于跳过空格和不需要处理的字符）
     因此事实上token用于是指向每一个字符的开头，而src则真正用于判断下一个字符*/
    while (token = *src++) 
    {
        
        if (token == '\n') ++line;  //处理换行符,错误处理时打印行号
        else if (token == '#') 
        {
            // skip macro, because we will not support it
            while (*src != 0 && *src != '\n') {src++;}
        }
        else if ((token >= 'a' && token <= 'z') || (token >= 'A' && token <= 'Z') || (token == '_')) 
        {   // parse identifier，处理内置的类型名，或者用户自定义的变量名（统称为 ID）

            //这个其实是取到当前外层while循环中正在执行的字符地址，因为src已经被++了
            current_ch = src - 1;
            
            /*  1. 逐字母计算该符号（ID）的一整个哈希值 */
            hash = token;
            while ((*src >= 'a' && *src <= 'z') || (*src >= 'A' && *src <= 'Z') || (*src >= '0' && *src <= '9') || (*src == '_')) 
            { 
                hash = hash * 147 + *src++;
            }


            /*  2. 每次都用一个指针指向symbol table[] 的首地址，遍历符号表（用current_id作为迭代器） */
            current_id = symbols;
            while (current_id[Token])  //检索symbol_toble 的token属性 (注意这里current_id[Token]应该理解成current_id.token属性）
            {  
                //如果当前identity已经在哈希表中， 并且名字相等（memcmp返回0，表示相等）
                //注意memcmp的第三个参数 “ src - current_ch ” 两个地址相减，其实返回的是一个整数，表示地址相差多少个sizeof(char*)
                if (current_id[Hash] == hash && !memcmp((char *)current_id[Name], current_ch, src - current_ch)) 
                { //找到对应的结构体， 并且 提前return
                    // if(current_id[Token] == Id) {printf("same id , line = %lld\n", line);}

                    token = current_id[Token];
                    return; // 
                }
                //如果这个token不在哈希表，将current_id指针+9，即指向下一个symbol table结构体的首地址
                current_id = current_id + IdSize;
            }


            /* 3. 如果最后遍历完整个symbol_table结构体数组，还是没有找到，就 create and store this new ID  */
            current_id[Name] = (int)current_ch; //current_ch是个地址, 若有需要可以通过Name到./text段找到该token的完整名称
            current_id[Hash] = hash; //自定义类型变量名，存到symbol table中是它的 hash值 和名字
            current_id[Token] = Id; // 若Token == Id ，则为用户自定义类型

            token = current_id[Token];
            return;
        }
        else if (token >= '0' && token <= '9') 
        {
            // parse number, three kinds: dec(123) hex(0x123) oct(017)
            token_val = token - '0';
            if (token_val > 0) 
            {
                //处理十进制数据
                // dec, starts with [1-9]
                while (*src >= '0' && *src <= '9') 
                {
                    token_val = token_val*10 + *src++ - '0';
                }
            } 
            else 
            {
                // starts with 0
                if (*src == 'x' || *src == 'X') 
                {
                    //处理十六进制数据
                    token = *++src;
                    while ((token >= '0' && token <= '9') || (token >= 'a' && token <= 'f') || (token >= 'A' && token <= 'F')) 
                    {
                        //这里对十六进制的处理很妙 ('A' = 01000001 || 'a' = (01100001) & 15 = 00001111) 
                        token_val = token_val * 16 + (token & 15) + (token >= 'A' ? 9 : 0);
                        token = *++src;
                    }
                }
                else 
                {
                    // 处理八进制数据
                    while (*src >= '0' && *src <= '7') 
                    {
                        token_val = token_val*8 + *src++ - '0';
                    }
                }
            }

            token = Num;
            return;
        }
        else if (token == '\'' || token == '"' )
        { //处理字符或者字符串
            // parse string literal, currently, the only supported escape
            // character is '\n', store the string literal into data.
            current_ch = data; 

            //如果是没有遇到是空字符或者没有遇到字符串结尾 "（处理字符串）
            while (*src != 0 && *src != token) 
            {
                token_val = *src++;
                //如果遇到转义字符'\'
                if (token_val == '\\') 
                {
                    token_val = *src++;// 跳过当前'\'
                    if (token_val == 'n') token_val = '\n'; //本程序支持特殊字符'\n' 的处理
                }

                //如果是字符串，在遇到字符串结尾 " 之前，将字符串字面值一个个存入data区
                if (token == '"') 
                    *data++ = token_val;
            }

            src++;

            
            if (token == '"')  //如果是字符串，将字符串首地址存到data区
                token_val = (int)current_ch;
            else   // 如果是单个字符，直接存字符本身，属于是ascall码
                token = Num;

            return;
        }//处理注释
        else if (token == '/') 
        {
            if (*src == '/') 
            {
                // skip comments
                while (*src != 0 && *src != '\n') {++src;}
            } else 
            {
                // divide operator
                token = Div;
                return;
            }
        }//处理其他字符
        else if (token == '=') 
        {
            // parse '==' or '='
            if (*src == '=') 
            {
                src ++;
                token = Eq;
            } else 
            {
                token = Assign;
            }
            return;
        }
        else if (token == '+') 
        {
            // parse '++' or '+' 
            if (*src == '+') 
            {
                src ++;
                token = Inc;
            } else 
            {
                token = Add;
            }
            return;
        }
        else if (token == '-') 
        {
            // parse  '--' or '-'
            if (*src == '-') 
            {
                src ++;
                token = Dec;
            } else 
            {
                token = Sub;
            }
            return;
        }
        else if (token == '!') 
        {
            // parse '!='
            if (*src == '=') 
            {
                src++;
                token = Ne;
            }
            return;
        }
        else if (token == '<') 
        {
            // parse '<=', '<<' or '<'
            if (*src == '=') {
                src ++;
                token = Le;  //less equal
            } else if (*src == '<') {
                src ++;
                token = Shl; //shift left 
            } else {
                token = Lt;  //less than
            }
            return;
        }
        else if (token == '>') 
        {
            // parse '>=', '>>' or '>'
            if (*src == '=') {
                src ++;
                token = Ge;
            } else if (*src == '>') {
                src ++;
                token = Shr;
            } else {
                token = Gt;
            }
            return;
        }
        else if (token == '|') 
        {
            // parse '|' or '||'
            if (*src == '|') {
                src ++;
                token = Lor;   //logical or
            } else {
                token = Or;    //bit or
            }
            return;
        }
        else if (token == '&') 
        {
            // parse '&' and '&&'
            if (*src == '&') {
                src ++;
                token = Lan;  //logical and 
            } else {
                token = And;  //bit and
            }
            return;
        }
        else if (token == '^') 
        {
            token = Xor; 
            return;
        }
        else if (token == '%') 
        {
            token = Mod;
            return;
        }
        else if (token == '*') 
        {
            token = Mul;
            return;
        }
        else if (token == '[') 
        {
            token = Brak;
            return;
        }
        else if (token == '?') 
        {
            token = Cond;
            return;
        }
        else if (token == '~' || token == ';' || token == '{' || token == '}' || token == '(' || token == ')' || token == ']' || token == ',' || token == ':') 
        {
            // directly return the character as token;
            return;
        }
    }
    return;
}


//查看当前id（我统一把他们抽象成单词）和 传入的单词匹配，就往下解析后面的单词
void match(int tk) {
    if (token == tk)  
        next();
    else
    {
        printf("%lld: expected token: %lld\n", line, tk);
        exit(-1);
    }
}





/* 语法分析，这里用了优先级爬山法*/
void expression(int level)  
{
    /*  expressions have various format.
        but majorly can be divided into two parts: unit and operator
        for example `(char) *a[10] = (int *) func(b > 0 ? 10 : 20);
        `a[10]` is an unit while `*` is an operator.
        `func(...)` in total is an unit.
        //我们应该先去解析那些 单元 和 一元运算符
        so we should first parse those unit and unary operators
        //然后再去解析双目运算符
        and then the binary ones
    */
   
    // also the expression can be in the following types:

    // 1. unit_unary ::= unit | unit unary_op | unary_op unit
    // 2. expr ::= unit_unary (bin_op unit_unary ...)


    // unit_unary()
    int *id;
    int tmp;
    int *addr;

    // 处理单目3运算符，前缀单目运算符都是右结合，后缀单目运算符都是左结合
    {
        //错误处理
        if (!token) 
        {
            printf("%lld: unexpected token EOF of expression\n", line);
            exit(-1);
        }
    
        if (token == Num) 
        {//处理立即数
            match(Num);

            // emit code
            *++text = IMM;
            *++text = token_val;
            expr_type = INT;
        }
        else if (token == '"') 
        {//处理字符串常量
            // emit code
            *++text = IMM;
            *++text = token_val;
            match('"');


            /*  为了支持定义在两行的这种风格的字符串定义，做了如下处理
                p = "first line"
                    "second line";
            */
            // store the rest strings
            while (token == '"') {
                match('"');
            }

            // append the end of string character '\0', all the data are default
            // to 0, so just move data one position forward.
            data = (char *)(((int)data + sizeof(int)) & (-sizeof(int)));

            
            expr_type = PTR;
        }
        else if (token == Sizeof) 
        {
            // sizeof is actually an unary operator
            // now only `sizeof(int)`, `sizeof(char)` and `sizeof(*...)` are
            // supported.
            match(Sizeof);
            match('(');
            expr_type = INT;

            if (token == Int) {
                match(Int);
            } else if (token == Char) {
                match(Char);
                expr_type = CHAR;
            }

            //处理指针
            while (token == Mul) {
                match(Mul);
                expr_type = expr_type + PTR;
            }

            match(')');

            // emit code
            *++text = IMM;
            *++text = (expr_type == CHAR) ? sizeof(char) : sizeof(int);

            expr_type = INT;
        }        
        else if (token == Id) 
        {//处理函数，枚举，变量
            // there are several type when occurs to Id
            // but this is unit, so it can only be
            // 1. function call
            // 2. Enum variable
            // 3. global/local variable
            match(Id);

            id = current_id;


            //如果token后面紧接着是 ( 说明是个函数
            if (token == '(') 
            {
                // function call
                match('(');

                // pass in arguments
                tmp = 0; // number of arguments
                while (token != ')') 
                {
                    expression(Assign);
                    *++text = PUSH;
                    tmp ++;

                    if (token == ',') {
                        match(',');
                    }
                }
                match(')');

                // emit code
                if (id[Class] == Sys) {
                    // system functions
                    *++text = id[Value];
                }
                else if (id[Class] == Fun) {
                    // function call
                    *++text = CALL;
                    *++text = id[Value];
                }
                else {
                    printf("%lld: bad function call\n", line);
                    exit(-1);
                }

                // clean the stack for arguments
                if (tmp > 0) {
                    *++text = ADJ;
                    *++text = tmp;
                }
                expr_type = id[Type];
            }

            //如果是个枚举类型
            else if (id[Class] == Num) 
            {
                // enum variable
                *++text = IMM;
                *++text = id[Value];
                expr_type = INT;
            }

            // 如果是个变量名
            else 
            {
                
                if (id[Class] == Loc) {
                    *++text = LEA;
                    *++text = index_of_bp - id[Value];
                }
                else if (id[Class] == Glo) {
                    *++text = IMM;
                    *++text = id[Value];
                }
                else {
                    printf("%lld: undefined variable\n", line);
                    exit(-1);
                }

                // emit code, default behaviour is to load the value of the
                // address which is stored in `ax`
                expr_type = id[Type];
                *++text = (expr_type == CHAR) ? LC : LI;
            }
        }
        else if (token == '(') 
        {   //处理强制类型转换或者括号
           // cast or parenthesis
            match('(');
            if (token == Int || token == Char) {
                tmp = (token == Char) ? CHAR : INT; // cast type
                match(token);
                while (token == Mul) {
                    match(Mul);
                    tmp = tmp + PTR;
                }

                match(')');

                expression(Inc); // cast has precedence as Inc(++)

                expr_type  = tmp;
            } else {
                // normal parenthesis
                expression(Assign);
                match(')');
            }
        }
        else if (token == Mul) 
        { //处理解引用
            // dereference *<addr>
            match(Mul);
            expression(Inc); // dereference has the same precedence as Inc(++)

            if (expr_type >= PTR) {
                expr_type = expr_type - PTR;
            } else {
                printf("%lld: bad dereference\n", line);
                exit(-1);
            }

            *++text = (expr_type == CHAR) ? LC : LI;
        }
        else if (token == And) 
        { //处理取地址运算符
            // get the address of
            match(And);
            expression(Inc); // get the address of
            if (*text == LC || *text == LI) {
                text --;
            } else {
                printf("%lld: bad address of\n", line);
                exit(-1);
            }

            expr_type = expr_type + PTR;
        }
        else if (token == '!') 
        {
            // not
            match('!');
            expression(Inc);

            // emit code, use <expr> == 0
            *++text = PUSH;
            *++text = IMM;
            *++text = 0;
            *++text = EQ;

            expr_type = INT;
        }
        else if (token == '~') 
        {
            // bitwise not
            match('~');
            expression(Inc);

            // emit code, use <expr> XOR -1
            *++text = PUSH;
            *++text = IMM;
            *++text = -1;
            *++text = XOR;

            expr_type = INT;
        }
        else if (token == Add) 
        { // 处理+
            // +var, do nothing
            match(Add);
            expression(Inc);

            expr_type = INT;
        }
        else if (token == Sub) 
        {
            // -var
            match(Sub);

            if (token == Num) {
                *++text = IMM;
                *++text = -token_val;
                match(Num);
            } else {

                *++text = IMM;
                *++text = -1;
                *++text = PUSH;
                expression(Inc);
                *++text = MUL;
            }

            expr_type = INT;
        }    
        else if (token == Inc || token == Dec) 
        { //处理++ 和 --
            tmp = token;
            match(token);
            expression(Inc);
            if (*text == LC) {
                *text = PUSH;  // to duplicate the address
                *++text = LC;
            } else if (*text == LI) {
                *text = PUSH;
                *++text = LI;
            } else {
                printf("%lld: bad lvalue of pre-increment\n", line);
                exit(-1);
            }
            *++text = PUSH;
            *++text = IMM;
            *++text = (expr_type > PTR) ? sizeof(int) : sizeof(char);
            *++text = (tmp == Inc) ? ADD : SUB;
            *++text = (expr_type == CHAR) ? SC : SI;
        }
        else 
        {
            printf("%lld: bad expression\n", line);
            exit(-1);
        }
    }




    //双目运算符和后缀运算符（逆波兰表达式）
    // binary operator and postfix operators.
    {
        //根据运算符的优先级来计算表达式
        while (token >= level) {
            // handle according to current operator's precedence
            tmp = expr_type;
            if (token == Assign) {
                // var = expr;
                match(Assign);
                if (*text == LC || *text == LI) {
                    *text = PUSH; // save the lvalue's pointer
                } else {
                    printf("%lld: bad lvalue in assignment\n", line);
                    exit(-1);
                }
                expression(Assign);

                expr_type = tmp;
                *++text = (expr_type == CHAR) ? SC : SI;
            }
            else if (token == Cond) {
                // expr ? a : b;
                match(Cond);
                *++text = JZ;
                addr = ++text;
                expression(Assign);
                if (token == ':') {
                    match(':');
                } else {
                    printf("%lld: missing colon in conditional\n", line);
                    exit(-1);
                }
                *addr = (int)(text + 3);
                *++text = JMP;
                addr = ++text;
                expression(Cond);
                *addr = (int)(text + 1);
            }
            else if (token == Lor) {
                // logic or
                match(Lor);
                *++text = JNZ;
                addr = ++text;
                expression(Lan);
                *addr = (int)(text + 1);
                expr_type = INT;
            }
            else if (token == Lan) {
                // logic and
                match(Lan);
                *++text = JZ;
                addr = ++text;
                expression(Or);
                *addr = (int)(text + 1);
                expr_type = INT;
            }
            else if (token == Or)  {match(Or); *++text = PUSH;expression(Xor);*++text = OR; expr_type = INT;}
            else if (token == Xor) {match(Xor);*++text = PUSH;expression(And);*++text = XOR;expr_type = INT;}
            else if (token == And) {match(And);*++text = PUSH;expression(Eq); *++text = AND;expr_type = INT;}
            else if (token == Eq)  {match(Eq); *++text = PUSH;expression(Ne); *++text = EQ; expr_type = INT;}
            else if (token == Ne)  {match(Ne); *++text = PUSH;expression(Lt); *++text = NE; expr_type = INT;}
            else if (token == Lt)  {match(Lt); *++text = PUSH;expression(Shl);*++text = LT; expr_type = INT;}
            else if (token == Gt)  {match(Gt); *++text = PUSH;expression(Shl);*++text = GT; expr_type = INT;}
            else if (token == Le)  {match(Le); *++text = PUSH;expression(Shl);*++text = LE; expr_type = INT;}
            else if (token == Ge)  {match(Ge); *++text = PUSH;expression(Shl);*++text = GE; expr_type = INT;}
            else if (token == Shl) {match(Shl);*++text = PUSH;expression(Add);*++text = SHL;expr_type = INT;}
            else if (token == Shr) {match(Shr);*++text = PUSH;expression(Add);*++text = SHR;expr_type = INT;}
            else if (token == Add) {match(Add);*++text = PUSH;expression(Mul);
                expr_type = tmp;
                if (expr_type > PTR)
                {
                    // pointer type, and not `char *`
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int);
                    *++text = MUL;
                }
                *++text = ADD;
            }
            else if (token == Sub) {match(Sub);*++text = PUSH;expression(Mul);
                if (tmp > PTR && tmp == expr_type) {
                    // pointer subtraction
                    *++text = SUB;
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int);
                    *++text = DIV;
                    expr_type = INT;
                } else if (tmp > PTR) {
                    // pointer movement
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int);
                    *++text = MUL;
                    *++text = SUB;
                    expr_type = tmp;
                } else {
                    // numeral subtraction
                    *++text = SUB;
                    expr_type = tmp;
                }
            }
            else if (token == Mul) {match(Mul);*++text = PUSH;expression(Inc);*++text = MUL;expr_type = tmp;}
            else if (token == Div) {match(Div);*++text = PUSH;expression(Inc);*++text = DIV;expr_type = tmp;}
            else if (token == Mod) {match(Mod);*++text = PUSH;expression(Inc);*++text = MOD;expr_type = tmp;}
            else if (token == Inc || token == Dec) {
                // postfix inc(++) and dec(--)
                // we will increase the value to the variable and decrease it
                // on `ax` to get its original value.
                if (*text == LI) {
                    *text = PUSH;
                    *++text = LI;
                }
                else if (*text == LC) {
                    *text = PUSH;
                    *++text = LC;
                }
                else {
                    printf("%lld: bad value in increment\n", line);
                    exit(-1);
                }

                *++text = PUSH;
                *++text = IMM;
                *++text = (expr_type > PTR) ? sizeof(int) : sizeof(char);
                *++text = (token == Inc) ? ADD : SUB;
                *++text = (expr_type == CHAR) ? SC : SI;
                *++text = PUSH;
                *++text = IMM;
                *++text = (expr_type > PTR) ? sizeof(int) : sizeof(char);
                *++text = (token == Inc) ? SUB : ADD;
                match(token);
            }
            else if (token == Brak) {
                // array access var[xx]
                match(Brak);
                *++text = PUSH;
                expression(Assign);
                match(']');

                if (tmp > PTR) {
                    // pointer, `not char *`
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int);
                    *++text = MUL;
                }
                else if (tmp < PTR) {
                    printf("%lld: pointer type expected\n", line);
                    exit(-1);
                }
                expr_type = tmp - PTR;
                *++text = ADD;
                *++text = (expr_type == CHAR) ? LC : LI;
            }
            else {
                printf("%lld: compiler error, token = %lld\n", line, token);
                exit(-1);
            }
        }
    }
    
}

/* 语法分析和语义分析，生成对应的汇编指令 */
void statement() {
    // there are 6 kinds of statements here:
    // 1. if (...) <statement> [else <statement>]
    // 2. while (...) <statement>
    // 3. { <statement> }
    // 4. return xxx;
    // 5. <empty statement>;
    // 6. expression; (expression end with semicolon)

    int *a, *b; // bess for branch control




    /********     第一种 if() 和 else() 语句     ****************/
            // if (...) <statement> [else <statement>]
            //
            //   if (...)           <cond>
            //                      JZ a
            //     <statement>      <statement>
            //   else:              JMP b
            // a:                 a:
            //     <statement>      <statement>
            // b:                 b:
    if (token == If) 
    {
        match(If);
        match('(');
        //解析表达式
        expression(Assign);  // parse condition
        match(')');

        // emit code for if
        *++text = JZ;
        b = ++text;

        //这里属于是递归调用了statement()解析语句了
        statement();         // parse statement


        if (token == Else) 
        { // parse else
            match(Else);

            // emit code for JMP B
            *b = (int)(text + 3);
            *++text = JMP;
            b = ++text;
            // 这里属于也是递归调用了statement()解析语句
            statement();
        }

        *b = (int)(text + 1);
    }

    /********     第二种 while() 语句     ****************/
            //
            // a:                     a:
            //    while (<cond>)        <cond>
            //                          JZ b
            //     <statement>          <statement>
            //                          JMP a
            // b:                     b:
    else if (token == While) 
    {
        match(While);

        a = text + 1;

        match('(');
        expression(Assign);
        match(')');

        *++text = JZ;
        b = ++text;

        statement();

        *++text = JMP;
        *++text = (int)a;
        *b = (int)(text + 1);
    }



    /********     第三种 { 纯 statement } 语句     ****************/
    else if (token == '{') 
    {
        // { <statement> ... }
        match('{');

        while (token != '}') 
        {
            statement();
        }
        match('}');
    }

/********     第四种 return 语句     ****************/
    else if (token == Return) {
        // return [expression];
        match(Return);

    //如果返回不为空，就解析表达式
        if (token != ';') 
        {
            expression(Assign);
        }

        match(';');

        // emit code for return
        *++text = LEV;
    }

    /********     第五种 空语句     ****************/
    else if (token == ';') 
    {
        // empty statement
        match(';');
    }

/********     第六种 纯表达式 语句     ****************/
    else 
    {
        // a = b; or function_call();
        expression(Assign);
        match(';');
    }
}




//解析函数的形参列表
void function_parameter() {
    int type;
    int params; //形参个数
    params = 0;
    while (token != ')') {
        // int name, ...
        type = INT;
        //注意这里小写的是token的类型，大写的是变量的类型
        if (token == Int) {
            match(Int);
        } else if (token == Char) {
            type = CHAR;
            match(Char);
        }

        // pointer type 这里是解析是否为指针类型
        while (token == Mul) {
            match(Mul);
            type = type + PTR;
        }

        /*******总的来说只支持三种数据类型，int char 和 他们对应的的 指针 类型 *******/
        
        
        // parameter name
        if (token != Id) {
            printf("%lld: bad parameter declaration\n", line);
            exit(-1);
        }
        //将判断类型是局部变量还是全局变量
        if (current_id[Class] == Loc) {
            printf("%lld: duplicate parameter declaration\n", line);
            exit(-1);
        }

        match(Id);

        // store the local variable
        //如果说形参列表中定义的局部变量名称的定义和全局变量名称定义重复，要做如下操作
        //将caller的xxx的值临时存储到Gxxx，预留出空间存储callee的局部变量
        current_id[GClass] = current_id[Class]; current_id[Class]  = Loc;
        current_id[GType]  = current_id[Type];  current_id[Type]   = type;
        current_id[GValue] = current_id[Value]; current_id[Value]  = params++;   // index of current parameter

        if (token == ',') {
            match(',');
        }
    }
    /*为函数调用时给出预留多大的存储空间的信息，
        事实上根据前面的代码可以得出结论，我们函数调用时传递的是变量的地址，
        因为上述代码不管什么类型变量都预留4字节的存储空间*/
    //注意在分析函数形参列表的时候只在栈中预留存储空间，实际还没有赋值，观察汇编代码
    index_of_bp = params+1;
}

void function_body() {
    // type func_name (...) {...}
    //                   -->|   |<--

    // ... {
    // 1. local declarations
    // 2. statements
    // }

    int pos_local; // position of local variables on the stack.
    int type;
    pos_local = index_of_bp;

    //只函数体内的局部变量定义在最前面
    while (token == Int || token == Char) {
        // local variable declaration, just like global ones.
        //先拿到变量的基础类型，int 或者 char
        basetype = (token == Int) ? INT : CHAR;
        match(token);
        
        //在基础类型之后，再进一步判断是否属于指针类型
        while (token != ';') 
        {
            type = basetype;
            
            /* 错误处理 */
            while (token == Mul) {
                match(Mul);
                type = type + PTR;
            }
            if (token != Id) {
                // invalid declaration
                printf("%lld: bad local declaration\n", line);
                exit(-1);
            }
            if (current_id[Class] == Loc) {
                // identifier exists
                printf("%lld: duplicate local declaration\n", line);
                exit(-1);
            }
            match(Id);

            //如果说函数体中定义的局部变量名称的定义和全局变量名称定义重复，要做如下操作
            //将caller的xxx的值临时存储到Gxxx，预留出空间存储callee的局部变量
            current_id[GClass] = current_id[Class]; current_id[Class]  = Loc;
            current_id[GType]  = current_id[Type];  current_id[Type]   = type;
            current_id[GValue] = current_id[Value]; current_id[Value]  = ++pos_local;   // index of current parameter

            if (token == ',') {
                match(',');
            }

        }
        match(';');
    }

    //这里跟现代c语言汇编有点不太一样，这里是将函数体内的局部变量也直接当成形参列表中的局部变量一样处理
    //现代c语言汇编是将形参列表的变量和函数体的两个变量放入两个不同的栈帧
    // save the stack size for local variables
    *++text = ENT;
    *++text = pos_local - index_of_bp;

    // statements(递归下降法)
    while (token != '}') 
    {
        //语句分析
        statement();
    }

    // emit code for leaving the sub function
    *++text = LEV; //将函数返回指令放入内存
}

void function_declaration() {
    //从左括号开始解析
    // type func_name (...) {...}
    //               | this part

    match('(');
    function_parameter();
    match(')');
    match('{');
    function_body();
    //match('}');

    // unwind local variable declarations for all local variables.
    /*函数返回之后之后，将 Gxxx的变量转移回xxx */
    current_id = symbols;
    while (current_id[Token]) {
        if (current_id[Class] == Loc) {
            current_id[Class] = current_id[GClass];
            current_id[Type]  = current_id[GType];
            current_id[Value] = current_id[GValue];
        }
        current_id = current_id + IdSize;
    }
}

//解析枚举变量  parse enum [id] { a = 1, b = 3, ...} 大括号里面的东西 
void enum_declaration() {
    //注意这里这样定义和赋值分离是有原因的
    int i;
    i = 0;

    //枚举变量可能有多个
    while (token != '}') 
    {
        //判断当前单词是否是变量名 id
        if (token != Id) 
        {
            printf("%lld: bad enum identifier %lld\n", line, token);
            exit(-1);
        }

        //如果是id匹配，读取下一个单词
        next();

        //判断是否是等于号
        if (token == Assign) 
        {
            //如果是，取得下一个单词
            next();
            //判断是否是数字
            if (token != Num) 
            {
                printf("%lld: bad enum initializer\n", line);
                exit(-1);
            }
            //到这里如果匹配成功
            i = token_val;
            next();
        }

        //这就是语义分析的意义所在，确定结构体的另外三个成员属性 Class Type Value
        current_id[Class] = Num;
        current_id[Type] = INT;
        current_id[Value] = i++;

        //如果遇到逗号没结束，接着往下读
        if (token == ',') {
            next();
        }
    }
}


//解析整个源代码 枚举 | 变量 | 函数 的语义
void global_declaration() {
    // global_declaration ::= enum_decl | variable_decl | function_decl
    //
    // enum_decl ::= 'enum' [id] '{' id ['=' 'num'] {',' id ['=' 'num'} '}'
    //
    // variable_decl ::= type {'*'} id { ',' {'*'} id } ';'
    //
    // function_decl ::= type {'*'} id '(' parameter_decl ')' '{' body_decl '}'
    /*
        用 EBNF范式 来描述 EBNF 的语法（这里很有趣）
        生成式 = 生成式名 '=' [ 表达式 ] [';'] ;
        表达式 = 选择项 { '|' 选择项 } ;
        选择项 = 条目 { 条目 } ;
        条目   = 生成式名 | 标记 [ '…' 标记 ] | 分组 | 可选项 | 重复项 ;
        分组   = '(' 表达式 ')' ;
        可选项 = '[' 表达式 ']' ;
        重复项 = '{' 表达式 '}' ;
    */

    int type; // tmp, actual type for variable
    int i; // tmp

    // basetype = INT;  // 默认值

    // 1. 基类型为枚举
    if (token == Enum) {
        // enum [id] { a = 10, b = 20, ... }
        match(Enum);
        if (token != '{') {
            match(Id); // skip the [id] part （跳过enmu 的name）
        }
        if (token == '{') {
            // parse the assign part
            match('{');
            enum_declaration();
            match('}');
        }
        match(';');
        basetype = Enum;
        return;
    }
    // 2. 基类型为整型
    else if (token == Int) {
        match(Int);
        basetype = INT;
    }
    // 3. 基类型为字符型，注意字符其实也是一个纯数字哦
    else if (token == Char) {
        match(Char);
        basetype = CHAR;
    }


    /* 每次解析以 ; 或 } 结尾的一个语句或者语句块（函数） */
    while (token != ';' && token != '}') 
    {
        type = basetype; 

        /* 1. 支持多级指针这种复合类型 */
        while (token == Mul) 
        {
            match(Mul); 
            type = type + PTR;
        }

        /* 2. 紧跟着必须出现变量名，否则错误，提前返回 */
        if (token != Id) 
        {
            // invalid declaration
            printf("%lld: bad global declaration\n", line);
            exit(-1);
        }

        /* 3. 重复定义错误处理，提前返回 */
        if (current_id[Class]) 
        {
            // identifier exists
            printf("%lld: duplicate global declaration\n", line);
            exit(-1);
        }

        /* 4. 匹配该变量 */
        match(Id);

        /* 5. 确定该变量的类型 */
        current_id[Type] = type;

        /* 6. 如果紧跟id 出现了左括号，则应该被解析成一个函数 */
        if (token == '(') 
        {
            current_id[Class] = Fun;
            current_id[Value] = (int)(text + 1); // the memory address of function
            function_declaration();
        } 
        else 
        {
            //当判断完所有分支后，就定义这个变量，从data区把数据取出来
            // variable declaration
            current_id[Class] = Glo; // global variable
            current_id[Value] = (int)data; // assign memory address
            data = data + sizeof(int);
        }

        if (token == ',') {
            match(',');
        }
    }
}


/*main entrance for parser.*/
void program() 
{
    /* 没一轮循环都会解析一整条语句或者一整个语句块 */
    next();           
    while (token > 0)  
    {
        global_declaration();
        next();
    }
}


/* 自定义指令集的行为 */
/* the entrance for virtual machine; used to interpret target instructions.*/
int eval() 
{

    int op; //op means operantion code of intruction 
    int *tmp;
    while (1)
    {
        /*attention !!! this is necessary, 
        this pc++ is because of after readring operation, the pc need to go to next address !!!
        */
        op = *pc++;

        /***  The following codes are simulate "save & load" instruction  ***/
        // load immediate value to ax, and pc++ later
        if (op == IMM)       {ax = *pc++;} 
        // load address for arguments.
        else if (op == LEA)  {ax = (int)(bp + *pc++);} 
        // load character to ax, address in ax (Register Direct Addressing)
        else if (op == LC)   {ax = *(char*)ax;} 
         // load integer to ax, address in ax (Register Direct Addressing)     
        else if (op == LI)   {ax = *(int*)ax;}
        // save character to address, value in ax, address on stack       
        else if (op == SC)   {ax = *(char*)*sp++ = ax;}
        // save integer to address, value in ax, address on stack
        else if (op == SI)   {*(int*)*sp++ = ax;}       
        // push the value of ax onto the stack
        else if (op == PUSH) {*--sp = ax;} 


        /***  The following codes are simulate JMP instruction  ***/
        // jump to the address
        else if (op == JMP)  {pc = (int*)*pc;}
        // jump if ax is zero
        else if (op == JZ)   {pc = ax ? pc + 1 : (int*)*pc;} 
        // jump if ax is not zero
        else if (op == JNZ)  {pc = ax ? (int*)*pc : pc + 1;}


        /***  The following codes are simulate call subroutines instruction  ***/
        // call subroutine,firstly, push current address into stack, then jump to subroutine
        else if (op == CALL) {*--sp = (int)(pc+1); pc = (int*)*pc;}
        // return from subroutine (actually we will use LEV intruction to replace RET)
        //else if (op == RET)  {pc = (int *)*sp++;} 

        // make new stack frame
        else if (op == ENT)  {*--sp = (int)bp; bp = sp; sp = sp - *pc++;}
        // add esp, <size>
        else if (op == ADJ)  {sp = sp + *pc++;}  
        // restore call frame and PC
        else if (op == LEV)  {sp = bp; bp = (int*)*sp++; pc = (int*)*sp++;} 



        /***  The following codes are calculating instruction  ***/
        else if (op == OR)  ax = *sp++ | ax;
        else if (op == XOR) ax = *sp++ ^ ax;
        else if (op == AND) ax = *sp++ & ax;
        else if (op == EQ)  ax = *sp++ == ax;
        else if (op == NE)  ax = *sp++ != ax;
        else if (op == LT)  ax = *sp++ < ax;
        else if (op == LE)  ax = *sp++ <= ax;
        else if (op == GT)  ax = *sp++ >  ax;
        else if (op == GE)  ax = *sp++ >= ax;
        else if (op == SHL) ax = *sp++ << ax;
        else if (op == SHR) ax = *sp++ >> ax;
        else if (op == ADD) ax = *sp++ + ax;
        else if (op == SUB) ax = *sp++ - ax;
        else if (op == MUL) ax = *sp++ * ax;
        else if (op == DIV) ax = *sp++ / ax;
        else if (op == MOD) ax = *sp++ % ax;

        

        
        /***  The following codes are system call instruction  ***/
        else if (op == EXIT) { printf("exit(%lld)", *sp); return *sp;}
        else if (op == OPEN) { ax = open((char*)sp[1], sp[0]); }
        else if (op == CLOS) { ax = close(*sp);}
        else if (op == READ) { ax = read(sp[2], (char*)sp[1], *sp); }
        else if (op == PRTF) { tmp = sp + pc[1]; ax = printf((char*)tmp[-1], tmp[-2], tmp[-3], tmp[-4], tmp[-5], tmp[-6]); }
        else if (op == MALC) { ax = (int)malloc(*sp);}
        else if (op == MSET) { ax = (int)memset((char*)sp[2], sp[1], *sp);}
        else if (op == MCMP) { ax = memcmp((char*)sp[2], (char*)sp[1], *sp);}


        else
        {
            printf("unknown instruction:%lld\n", op);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i, fd;
    int *tmp;
    argc--; // get the first argument except file name
    argv++;

    poolsize = 256 * 1024; // arbitrary size
    line = 1;


    /* 1. 分配程序运行所需的虚拟地址空间 text/data/stack/symbols */
    if (!(text = old_text = malloc(poolsize))) 
    {
        printf("could not malloc(%lld) for text area\n", poolsize);
        return -1;
    }
    if (!(data = malloc(poolsize))) 
    {
        printf("could not malloc(%lld) for data area\n", poolsize);
        return -1;
    }
    if (!(stack = malloc(poolsize))) 
    {
        printf("could not malloc(%lld) for stack area\n", poolsize);
        return -1;
    }
    if (!(symbols = malloc(poolsize))) 
    {
        printf("could not malloc(%lld) for symbol table\n", poolsize);
        return -1;
    }

    //initialize with all 0
    memset(text, 0, poolsize);
    memset(data, 0, poolsize);
    memset(stack, 0, poolsize);
    memset(symbols, 0, poolsize);


    /* 2. 初始化栈基指针bp，栈顶指针sp(向下增长)，通用寄存器ax */
    bp = sp = (int *)((int)stack + poolsize);
    ax = 0;

    /****************************************************************************************/
    /***************   The preceding code is used for initialization   *********************/
    /****************************************************************************************/

    /* 0.构建 symbol table 整体框架，预处理该编译器支持的内置关键字和数据类型，加入symbol table中 */
    src = "char else enum if int return sizeof while " // keywords
          "open read close printf malloc memset memcmp exit void main"; // library function
    // add keywords to symbol table
    i = Char;
    while (i <= While) {
        next();
        current_id[Token] = i++;
    }
    // add library to symbol table
    i = OPEN;
    while (i <= EXIT) {
        next();
        current_id[Class] = Sys;
        current_id[Type] = INT;
        current_id[Value] = i++;
    }
    next(); current_id[Token] = Char; // handle void type
    next(); idmain = current_id; // 记录main函数在symbo table中的位置（本质是 main入口地址）



    /* 1. 打开源文件 */
    if ((fd = open(*argv, 0)) < 0) 
    {
        printf("could not open(%s)\n", *argv);
        return -1;
    }

    /* 2. 分配src缓冲区 虚拟地址空间 */
    if (!(src = old_src = malloc(poolsize))) 
    {
        printf("could not malloc(%lld) for source area\n", poolsize);
        return -1;
    }

    /* 3. 将源文件读到 src缓冲区 */
    if ((i = read(fd, src, poolsize-1)) <= 0) 
    {
        printf("read() returned %lld\n", i);
        return -1;
    }
    src[i] = 0; // add EOF character
    close(fd);

    /* 4. 编译 */
    program();


    /* 5. 找到main函数入口 */
    pc = (int *)idmain[Value];
    if(!pc)
    {
        printf("main() not defined\n");
        return -1;
    }
    /* main函数是最先被压栈的函数 */
    sp = (int *)((int)stack + poolsize);
    *--sp = EXIT; // call exit if main returns
    *--sp = PUSH; tmp = sp;
    *--sp = argc;
    *--sp = (int)argv;
    *--sp = (int)tmp;

    
    return eval();
}


void test1_eval()
{
    int i = 0;
    text[i++] = IMM;
    text[i++] = 10;
    text[i++] = PUSH;
    text[i++] = IMM;
    text[i++] = 20;
    text[i++] = ADD;
    text[i++] = PUSH;
    text[i++] = EXIT;
    pc = text;
}