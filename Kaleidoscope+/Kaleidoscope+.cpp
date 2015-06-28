#include <cctype>
#include <cstdio>
#include <map>
#include <cstdint>
#include <string>
#include <vector>
#include <iostream>
#include <istream>
#include <set>
#include "Debug.h"
#include "llvm/ADT/APInt.h"
#include <llvm/IR/Value.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Function.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/ExecutionEngine/MCJIT.h>
using namespace llvm;

enum TOK{
	EOF_TOK = -100,

	INT_TOK,
	DOUBLE_TOK,
	IDENTIFIER_TOK,

	DEF_TOK,
	BINARY_TOK,
	UNARY_TOK,
	EXTERN_TOK,

	VAR_TOK,
	IN_TOK,

	IF_TOK,
	THEN_TOK,
	ELSE_TOK,
	FOR_TOK,

};


//�������ƿռ�
std::string get_tok_name(int32_t _tok);
namespace{
	class PrototypeAST;
	class ExprAST;
	class FunctionAST;
}


struct DebugInfo {
	DICompileUnit *TheCU;
	DIType *DblTy;
	std::vector<DIScope *> LexicalBlocks;
	std::map<const PrototypeAST *, DIScope *> FnScopeMap;
	void emitLocation(ExprAST *AST);
	DIType *getDoubleTy();
} KSDbgInfo;

struct SourceCodeLocation{
	int line, col;
};

static SourceCodeLocation CurLoc;
static SourceCodeLocation LexLoc = { 1, 0 };


static std::string getUniqueAnonyName(const char* ss){
	static int index = 0;
	char ret_name[32];

	sprintf(ret_name, "%s%d", ss, index++);
	return std::string(ret_name);
}

static std::string getUniqueMCJITName(const char* ss){
	static int index = 0;
	char ret_name[32];

	sprintf(ret_name, "%s%d", ss, index++);
	return std::string(ret_name);
}

static inline int advance(std::istream& input){
	int ch = input.get();
	if (ch == '\n' || ch == '\r'){
		++LexLoc.line;
	}
	else{
		++LexLoc.col;
	}
	return ch;
}


std::string cur_identifier;
int32_t cur_integer;
double cur_double;

int32_t cur_tok;


int32_t get_tok(std::istream& input){
	static int32_t cur_char = ' ';

	while (isspace(cur_char)) cur_char = advance(input);

	//ͬC/C++, identifier����������ĸ����'_'��ʼ��
	if (isalpha(cur_char) || cur_char == '_'){
		cur_identifier = cur_char;

		while (cur_char = advance(input))
			if (isalnum(cur_char) || cur_char == '_')
				cur_identifier += cur_char;
			else
				break;

		if (cur_identifier == "def")
			return TOK::DEF_TOK;
		else if (cur_identifier == "extern")
			return TOK::EXTERN_TOK;
		else if (cur_identifier == "var")
			return TOK::VAR_TOK;
		else if (cur_identifier == "in")
			return TOK::INT_TOK;
		else if (cur_identifier == "if")
			return TOK::IF_TOK;
		else if (cur_identifier == "else")
			return TOK::ELSE_TOK;
		else if (cur_identifier == "then")
			return TOK::THEN_TOK;
		else if (cur_identifier == "for")
			return TOK::FOR_TOK;
		else if (cur_identifier == "unary")
			return TOK::UNARY_TOK;
		else if (cur_identifier == "binary")
			return TOK::BINARY_TOK;
		else
			return TOK::IDENTIFIER_TOK;
	}


	if (isdigit(cur_char) || cur_char == '.'){
		std::string num_str;
		int32_t is_double = 0;
		do {
			if (cur_char == '.'){
				++is_double;
			}
			num_str += cur_char;
			cur_char = advance(input);
		} while (isdigit(cur_char) || cur_char == '.');

		if (is_double == 0){
			cur_integer = std::stoi(num_str);
			//std::cout << "current integer is "<< cur_integer << std::endl;
			return TOK::INT_TOK;
		}
		else if (is_double == 1){
			//std::cout << "current double is " << cur_double << std::endl;
			cur_double = std::stod(num_str);
			return TOK::DOUBLE_TOK;
		}
		else{
			fprintf(stderr, "Invalid number input\n");
			return get_tok(input);
		}
	}

	if (cur_char == '#'){
		do {
			cur_char = advance(input);
		} while (cur_char != '\n' && cur_char != EOF && cur_char != '\r');

		if (cur_char != EOF)
			return get_tok(input);
	}

	if (cur_char == EOF)
		return TOK::EOF_TOK;

	int32_t this_char = cur_char;
	cur_char = advance(input);

	return this_char;
}

std::string get_tok_name(int32_t _tok){
	switch (_tok)
	{
	case TOK::BINARY_TOK:
		return "binary";
	case TOK::DEF_TOK:
		return "def";
	case TOK::DOUBLE_TOK:
		return std::to_string(cur_double);
	case TOK::ELSE_TOK:
		return "else";
	case TOK::EOF_TOK:
		return "EOF";
	case TOK::EXTERN_TOK:
		return "extern";
	case TOK::FOR_TOK:
		return "for";
	case TOK::IDENTIFIER_TOK:
		return cur_identifier;
	case TOK::IF_TOK:
		return "if";
	case TOK::IN_TOK:
		return "in";
	case TOK::INT_TOK:
		return std::to_string(static_cast<int64_t>(cur_integer));
	case TOK::THEN_TOK:
		return "then";
	case TOK::UNARY_TOK:
		return "unary";
	case TOK::VAR_TOK:
		return "var";
	default:
		return std::string(1, (char)_tok);
	}
}




ExprAST* Error(const char* mesg){
	fprintf(stderr, "Error: %s\n", mesg);
	return nullptr;
}

PrototypeAST* ErrorP(const char *mesg){
	Error(mesg);
	return nullptr;
}

FunctionAST* ErrorF(const char* mesg){
	Error(mesg);
	return nullptr;
}

Value* ErrorV(const char* mesg){
	Error(mesg);
	return nullptr;
}



namespace {
	class ExprAST;
	class Object;

	static std::map<Object*, int32_t> exprast_pool;

	class Object{
	public:
		virtual ~Object(){}
	};

	//AST�ڵ����
	class ExprAST : public Object{
	public:
		virtual Value *Codegen() = 0;
	};

	//represent double value;
	class DoubleValue :public ExprAST{
		double value;
		DoubleValue(double _x) :value(_x){}
	public:
		static DoubleValue* factory(double _x){
			DoubleValue* ret = new DoubleValue(_x);
			exprast_pool[ret] = 1;
			return ret;
		}

		Value* Codegen() override;

	};

	//represent integer value;
	class IntegerValue :public ExprAST{
		int32_t value;
		IntegerValue(int32_t _x) :value(_x){}
	public:
		static IntegerValue* factory(int32_t _x){
			IntegerValue *ret = new IntegerValue(_x);
			exprast_pool[ret] = 1;
			return ret;
		}
		Value* Codegen()override;

	};


	//represent vaiable;
	class VariableExprAST :public ExprAST{
		std::string name;
		VariableExprAST(std::string _x) :name(_x){}
	public:
		static VariableExprAST* factory(std::string _x){
			VariableExprAST* ret = new VariableExprAST(_x);
			exprast_pool[ret] = 1;
			return ret;
		}
		std::string getName()const { return name; }
		Value* Codegen()override;
	};


	//represent unary operation, contains unary operator and expression operand
	class UnaryExpAST :public ExprAST{
		char unary_op;
		ExprAST* expr;
		UnaryExpAST(char _x, ExprAST* _y) :unary_op(_x), expr(_y){}
	public:
		static UnaryExpAST* factory(char _x, ExprAST* _y){
			UnaryExpAST* ret = new UnaryExpAST(_x, _y);
			exprast_pool[ret] = 1;
			return ret;
		}
		Value* Codegen()override;
	};


	//represent binary operation, contains bianry operater and two operand;
	class BinaryExprAST :public ExprAST{
		char binary_op;
		ExprAST* lhs, *rhs;
		BinaryExprAST(char _x, ExprAST* _y, ExprAST* _z) :binary_op(_x), lhs(_y), rhs(_z){}
	public:
		static BinaryExprAST* factory(char _x, ExprAST* _y, ExprAST* _z){
			BinaryExprAST* ret = new BinaryExprAST(_x, _y, _z);
			exprast_pool[ret] = 1;
			return ret;
		}
		Value* Codegen()override;
	};

	//represent function call node;
	class CallExprAST :public ExprAST{
		std::string func_name;
		std::vector<ExprAST*> func_args;
		CallExprAST(std::string _x, std::vector<ExprAST*> _y) : func_name(_x), func_args(_y){}
	public:
		static CallExprAST* factory(std::string _x, const std::vector<ExprAST*>& _y){
			CallExprAST* ret = new CallExprAST(_x, _y);
			exprast_pool[ret] = 1;
			return ret;
		}
		Value* Codegen()override;
	};

	class IfExprAST :public ExprAST{
		ExprAST* ifexpr, *thenexpr, *elseexpr;
		IfExprAST(ExprAST* _x, ExprAST* _y, ExprAST* _z) : ifexpr(_x), thenexpr(_y), elseexpr(_z){}
	public:
		static IfExprAST* factory(ExprAST* _x, ExprAST* _y, ExprAST* _z){
			IfExprAST* ret = new IfExprAST(_x, _y, _z);
			exprast_pool[ret] = 1;
			return ret;
		}
		Value* Codegen()override;
	};

	class ForExprAST :public ExprAST{
		std::string var_name;
		ExprAST* start, *end, *step, *body;
		ForExprAST(std::string _name, ExprAST* _x, ExprAST* _y, ExprAST* _z, ExprAST* _w) : var_name(_name), start(_x), end(_y), step(_z), body(_w){}
	public:
		static ForExprAST* factory(std::string _name, ExprAST* _x, ExprAST* _y, ExprAST* _z, ExprAST* _w){
			ForExprAST* ret = new ForExprAST(_name, _x, _y, _z, _w);
			exprast_pool[ret] = 1;
			return ret;
		}
		Value* Codegen()override;
	};

	class VarExprAST :public ExprAST{
		std::vector<std::pair<std::string, ExprAST*>> vars;
		ExprAST* body;
		VarExprAST(const std::vector<std::pair<std::string, ExprAST*>>& _x, ExprAST* _y) :vars(_x), body(_y){}
	public:
		static VarExprAST* factory(const std::vector<std::pair<std::string, ExprAST*>>& _x, ExprAST* _y){
			VarExprAST* ret = new VarExprAST(_x, _y);
			exprast_pool[ret] = 1;
			return ret;
		}
		Value* Codegen()override;
	};


	class PrototypeAST : public Object{
		std::string func_name;
		std::vector<std::string> func_args;
		int32_t is_operator;	//whether is a function or unary operator or binary operator
		int32_t precedence;

		PrototypeAST(std::string _name, const std::vector<std::string> _args, int32_t _x, int32_t _y) :func_name(_name), func_args(_args), is_operator(_x), precedence(_y){}

	public:
		static PrototypeAST* factory(std::string _name, const std::vector<std::string> _args, int32_t _x = 0, int32_t _y = 0){
			PrototypeAST* ret = new PrototypeAST(_name, _args, _x, _y);
			exprast_pool[ret] = 1;
			return ret;
		}

		bool isUnary()const{ return is_operator == 1; }
		bool isBinary()const{ return is_operator == 2; }
		bool isFunction()const{ return !is_operator; }

		char getOperatorName()const{
			assert(isUnary() || isBinary());
			return func_name[0];
		}

		int32_t getBinaryProceence()const{
			assert(isBinary());
			return precedence;
		}

		Function* Codegen();
		void CreateArgumentAllocas(Function* func);

	};


	class FunctionAST :public Object{
		PrototypeAST* func_proto;
		ExprAST* body;
		FunctionAST(PrototypeAST* _x, ExprAST* _y) : func_proto(_x), body(_y){}
	public:

		static FunctionAST* factory(PrototypeAST* _x, ExprAST* _y){
			FunctionAST* ret = new FunctionAST(_x, _y);
			exprast_pool[ret] = 1;
			return ret;
		}

		Function* Codegen();
	};


	class MCJITHelper{
	protected:
		typedef std::vector<ExecutionEngine*> EngineVecType;
		typedef std::vector<Module*> ModuleVecType;

	private:
		LLVMContext& context;
		Module* openModule;
		EngineVecType engines;
		ModuleVecType modules;

	public:
		MCJITHelper(LLVMContext& ctx) :context(ctx), openModule(nullptr){}
		Module* getModuleForNewFunction();
		Function* getFunction(const std::string& name);
		void* getPointerToFunction(Function* func);
		void* getSymbolAddress(const std::string& name);
		void dump();
	};

	class HelpingMemoryManage : public SectionMemoryManager{
		HelpingMemoryManage(const HelpingMemoryManage&) = delete;
		void operator=(const HelpingMemoryManage&) = delete;

		MCJITHelper* jithelper;

	public:
		HelpingMemoryManage(MCJITHelper* given_helper) :jithelper(given_helper){}
		//jithelperָ����ڴ沢����HelpingMemoryManage����
		~HelpingMemoryManage() override{};

		//������SectionMemoryManage��getSymbolAddress������������ʱ�Ƕ���HelpingMemoryManage�����Module����Symbol����
		//�����Ĭ�ϵ�SectionMemoryManage��û���ҵ��ñ�ʾ������ͨ������jithelperָ���MCJITHelper��enginers
		uint64_t getSymbolAddress(const std::string& name) override;
	};


	uint64_t HelpingMemoryManage::getSymbolAddress(const std::string& name){
		uint64_t address = SectionMemoryManager::getSymbolAddress(name);

		if (address) return address;

		address = (uint64_t)this->jithelper->getSymbolAddress(name);

		if (address){
			return address;
		}
		else
			return 0;
	}


	Module* MCJITHelper::getModuleForNewFunction(){
		if (this->openModule)
			return this->openModule;

		std::string module_name = getUniqueMCJITName("cool_mcjit_module_");

		Module* newModule = new Module(module_name, context);
		/////////////////////////////////////////////////////////////////////////
		//****************************** ����ƽ̨�����趨 ************************
		//ԭʼ��targettripleΪi686-pc-windows-msvc
		//The PassManager::run call causes the MC code generation mechanisms to emit a complete relocatable binary object image (either in either ELF or MachO format, depending on the target)
		//���ͨ��mcjit���У���Ҫ�趨���ɵĿ��ض�λĿ�����Ϊelf
		//////////////////////////////////////////////////////////////////////////
		newModule->setTargetTriple("i686-pc-windows-msvc-elf");
		modules.push_back(newModule);
		openModule = newModule;
		return openModule;
	}

	//�����е�module�в���ָ����ʾ����Funtion*;
	Function* MCJITHelper::getFunction(const std::string& name){
		std::cout << "All the functions in the modules vector" << std::endl;
		for (auto mod_ptr : this->modules) {
			for (auto iter = mod_ptr->begin(); iter != mod_ptr->end(); ++iter) {
				auto strRef = (*iter).getName();
				std::cout << std::string(strRef.data(), strRef.size()) << " ";
			}

		}
		DEBUG_CERR("Function in modules traverse complete\n");


		auto from = modules.begin(), to = modules.end();
		while (from != to) {
			Function* find_func = (*from)->getFunction(name);
			fprintf(stderr, "find the position %d for function %s\n", find_func, name.c_str());
			fprintf(stderr, "openModule is %d\n", openModule);
			fprintf(stderr, "current Module is %d\n", (*from));
			if (find_func){
				if (*from == openModule){
					return find_func;
				}
				assert(openModule != nullptr);

				Function* pf = openModule->getFunction(name);

				//��������ض���
				if (pf && !pf->empty()){
					ErrorV("redefinition of function across modules");
					return nullptr;
				}

				//���
				if (!pf){
					fprintf(stderr, "creating ExternalLinkage for function %s\n", name.c_str());
					pf = Function::Create(find_func->getFunctionType(), Function::ExternalLinkage, name, openModule);
					return pf;
				}
			}
			++from;
		}

		fprintf(stderr, "Could not find the function %s \n", name.c_str());
		return nullptr;
	}

	void* MCJITHelper::getPointerToFunction(Function* func){
		auto from = engines.begin(), to = engines.end();

		//�Ƿ��Ѿ���JIT
		while (from != to) {
			void* func_ptr = (void*)(*from)->getFunctionAddress(func->getName());
			if (func_ptr){
				return func_ptr;
			}
		}

		//���û����engines���ҵ������ڽ�openModule JIT,Ȼ�������н���Ѱ��

		if (openModule){
			//����ExcutionEngine����ʧ�ܵ�ԭ��
			std::string Errstr;
			ExecutionEngine *newEngine
				= EngineBuilder(std::unique_ptr <Module>(openModule))
				.setErrorStr(&Errstr)
				.setMCJITMemoryManager(std::unique_ptr<HelpingMemoryManage>(new HelpingMemoryManage(this))).create();

			//�������ʧ��
			if (!newEngine){
				fprintf(stderr, "Could not creat ExecutionEngine %s\n", Errstr.c_str());
				return nullptr;
			}

			//���ExcutionEngine����ɹ����򴴽�һ��FunctionPassManage����openModule�ڵ�Function�����Ż������ݸ��µ�ExcutionEngine
			auto *theFpm = new legacy::FunctionPassManager(openModule);
			//typename legacy::FunctionPassManager *current_fpm = new legacy::FunctionPassManager(openModule);

			//�趨�Ż���������
			//openModule����functionPassManage����������newEngine
			openModule->setDataLayout(newEngine->getDataLayout());

			theFpm->add(createBasicAliasAnalysisPass());

			theFpm->add(createPromoteMemoryToRegisterPass());

			theFpm->add(createInstructionCombiningPass());

			theFpm->add(createReassociatePass());

			theFpm->add(createCFGSimplificationPass());
			theFpm->doInitialization();

			auto last = openModule->end();
			for (auto it = openModule->begin(); it != last; ++it) {
				theFpm->run(*it);
			}

			//�ǹ����ڶ��ڴ��еģ�
			delete theFpm;

			//���е�openModule�е�Function���Ѿ����Ż�����ע�ᵽ��newEngine���ˣ�
			openModule = nullptr;

			engines.push_back(newEngine);
			newEngine->finalizeObject();
			return (void*)newEngine->getFunctionAddress(func->getName());
		}
		return nullptr;
	}

	void* MCJITHelper::getSymbolAddress(const std::string& name){
		auto from = engines.begin(), to = engines.end();

		while (from != to) {
			void* func_address = (void*)(*from)->getFunctionAddress(name);
			if (func_address){
				return func_address;
			}
			++from;
		}
		return nullptr;
	}

	void MCJITHelper::dump(){
		for (auto module_ptr : modules) {
			module_ptr->dump();
		}
	}

}
//end anonymous namespace


//****************************************
//GlobalVariabls for llvm
//****************************************

static Module *TheModule;
static IRBuilder<> Builder(getGlobalContext());
static std::map<std::string, AllocaInst*> namedValues;
static legacy::FunctionPassManager *TheFuncPM;
static ExecutionEngine *TheExecutionEngine;
static MCJITHelper* theHelper;




/******************************************************/
/*******************   Parser *************************/
/******************************************************/
int32_t get_next_tok(std::istream& input){
	cur_tok = get_tok(input);
	DEBUG_TOKEN(get_tok_name(cur_tok));
	return cur_tok;
}

static std::map<char, int32_t> binary_op_precedence;

static int32_t get_precedence(char bi_op){
	//printf("%d\n", bi_op);
	//assert(isascii(bi_op));
	int32_t precedence = binary_op_precedence[bi_op];
	if (precedence <= 0){
		return -1;
	}
	return precedence;
}



static FunctionAST* ParseToplevelExpr(std::istream& input);
static ExprAST* ParseExpression(std::istream& input);
static PrototypeAST* ParseExtern(std::istream& input);
static ExprAST* ParseIdentifierExpr(std::istream& input);
static ExprAST* ParseUnary(std::istream& input);
static ExprAST* ParseBinaryopRHS(std::istream& input, int32_t expr_prec, ExprAST* lhs);
static ExprAST* ParsePrimary(std::istream& input);
static ExprAST* ParseVarExpr(std::istream& input);
static ExprAST* ParseForExpr(std::istream& input);
static ExprAST* ParseIfExpr(std::istream& input);
static ExprAST* ParseNumber(std::istream& input);
static ExprAST* ParseParenExpr(std::istream& input);


static PrototypeAST* ParsePrototype(std::istream& input);
static FunctionAST* ParseDefinition(std::istream& input);



//toplevelexpr ::= expression
//�����������װΪ������������
static FunctionAST* ParseToplevelExpr(std::istream& input){
	srand((unsigned int)time(nullptr));
	if (ExprAST* expr = ParseExpression(input)){
		std::string name = getUniqueAnonyName("anony_func_");
		PrototypeAST* anonymous = PrototypeAST::factory(name, std::vector<std::string>(), 0, 0);
		return FunctionAST::factory(anonymous, expr);
	}
	return nullptr;
}


//��ʾʽ��unary��ʽ��binoprhs��ʽ���
//expression ::= unary binoprhs
static ExprAST* ParseExpression(std::istream& input){
	ExprAST* lhs = ParseUnary(input);
	if (lhs == nullptr){
		return nullptr;
	}
	return ParseBinaryopRHS(input, 0, lhs);
}

//unary��ʽ�������ǰtoken����Ϊoperator���򷵻�unaryExpAST, ����ǰunary��ʽΪһ��primary��ʽ��û��unary operator
//unary
//	:: = primary
//	:: = op unary
static ExprAST* ParseUnary(std::istream& input){
	//�����ǰ��token��Ϊoperator����ǰ���ʽΪһ��Primary��ʽ
	if (!isascii(cur_tok) || cur_tok == '(' || cur_tok == ','){
		return ParsePrimary(input);
	}

	char op = cur_tok;
	get_next_tok(input);
	std::cout << "unary op is " << (op) << std::endl;
	if (ExprAST* expr = ParseUnary(input)){
		return UnaryExpAST::factory(op, expr);
	}
	return nullptr;
}


//binoprhs���Կ��ܴ��ڵ�binary operator ExpAST���н���
//�����ǰ��binary operator�����ڻ���precedenceС��ǰһ��binary operator��precedence����ֱ�ӷ���ǰһ��exprAST:lhs
//�����ǰbinary operator��precedence����ǰһ��binary operator��precedence��
//binoprhs
//	:: =
//	:: = ('bi_op' unary)*
static ExprAST* ParseBinaryopRHS(std::istream& input, int32_t expr_prec, ExprAST* lhs){
	while (true)
	{
		int32_t cur_prec = get_precedence(cur_tok);

		if (cur_prec < expr_prec){
			return lhs;
		}

		int32_t biop = cur_tok;

		get_next_tok(input);

		ExprAST* rhs = ParseUnary(input);
		if (rhs == nullptr){
			return nullptr;
		}

		int32_t nxt_prec = get_precedence(cur_tok);

		if (cur_prec < nxt_prec){
			rhs = ParseBinaryopRHS(input, expr_prec + 1, rhs);
			if (rhs == nullptr){
				return nullptr;
			}
		}

		lhs = BinaryExprAST::factory(biop, lhs, rhs);
	}
}

//primary��ʽ��������ʽ
//primary
//	:: = identifierexpr
//	:: = numberexpr
//	:: = parenexpr
//	:: = ifexpr
//	:: = forexpr
//	:: = varexpr
//

static ExprAST* ParsePrimary(std::istream& input){
	switch (cur_tok)
	{
	case TOK::DOUBLE_TOK:
	case TOK::INT_TOK:
		return ParseNumber(input);
	case '(':
		return ParseParenExpr(input);
	case TOK::IDENTIFIER_TOK:
		return ParseIdentifierExpr(input);
	case TOK::FOR_TOK:
		return ParseForExpr(input);
	case TOK::IF_TOK:
		return ParseIfExpr(input);
	case TOK::VAR_TOK:
		return ParseVarExpr(input);
	default:
		get_next_tok(input);	//eat invalid token
		return Error("ParsePrimary: Invalid tok");
	}
}

//���ű��ʽ��ʽ
//parenexpr :: = '(' expression ')'

static ExprAST* ParseParenExpr(std::istream& input){
	get_next_tok(input); //eat '('
	ExprAST* ret = ParseExpression(input);
	if (ret == nullptr){
		return nullptr;
	}

	if (cur_tok != ')'){
		return Error("ParseParenExpr: expect ')' at last");
	}
	get_next_tok(input);//eat ')'

	return ret;
}


//�����Ǳ�����Ҳ�����Ǻ�������
//identifer normal form
//identifierexpr:
//	::=identifer
//	::=indeiffer ('expression*')

static ExprAST* ParseIdentifierExpr(std::istream& input){

	std::string var_name = cur_identifier;	//����var_name, ��һ�����ܳԵ�Ŀǰ��cur_identifier��string
	get_next_tok(input);//eat indetifer_tok;

	if (cur_tok != '('){
		return VariableExprAST::factory(cur_identifier);
	}

	std::vector<ExprAST*> func_args;

	get_next_tok(input);	//eat '('
	if (cur_tok != ')'){
		while (1){
			ExprAST* expr = ParseExpression(input);

			if (!expr){
				return Error("ParseIdentifier: Error in parsing function arguments");
			}
			func_args.push_back(expr);

			if (cur_tok == ')'){
				break;
			}
			else if (cur_tok != ','){
				return Error("ParseIdentifier: Expect ',' in arguments parsing");
			}
			get_next_tok(input);	//eat ','
		}
	}

	get_next_tok(input);	//eat last ')';
	CallExprAST* func_call = CallExprAST::factory(var_name, func_args);
	return func_call;
}


static ExprAST* ParseNumber(std::istream& input){
	if (cur_tok == TOK::INT_TOK){
		get_next_tok(input);
		return DoubleValue::factory((double)cur_integer);
		//return IntegerValue::factory(cur_integer);
	}
	else if (cur_tok == TOK::DOUBLE_TOK){
		get_next_tok(input);
		return DoubleValue::factory(cur_double);
	}
	else
		return Error("ParseNumber: Error token given");
}



//forexpr ����ѭ�����
//
//forexpr :: = 'for' identifier '=' expr ','  (identifier �� = �� expr)*; expr(',' expr) ? 'in' expression
static ExprAST* ParseForExpr(std::istream& input){
	get_next_tok(input);
	if (cur_tok != TOK::IDENTIFIER_TOK){
		return Error("ParseForExpr: error in for expression, expect a variable name");
	}
	std::string var_name = cur_identifier;

	get_next_tok(input); //eat identifer
	if (cur_tok != '='){
		return Error("ParseForExpr: error in for expression, expect '='");
	}

	get_next_tok(input);//eat '='


	ExprAST *start = ParseExpression(input);
	if (start == nullptr){
		return nullptr;
	}

	if (cur_tok != ','){
		return Error("ParseForExpr: expect ',' after start expression");
	}

	get_next_tok(input); //eat ','

	ExprAST *end = ParseExpression(input);

	if (end == nullptr){
		return nullptr;
	}

	//step if a optional
	ExprAST* step = nullptr;
	if (cur_tok == ','){
		get_next_tok(input); //eat ','
		ExprAST* step = ParseExpression(input);
		if (step == nullptr){
			return nullptr;
		}
	}

	if (cur_tok != TOK::IN_TOK){
		return Error("ParseForExpr: expect 'in' in for expression");
	}

	get_next_tok(input);	//eat 'in'

	ExprAST* block = ParseExpression(input);

	if (block == nullptr){
		return nullptr;
	}

	return ForExprAST::factory(var_name, start, end, step, block);

}

//'def' ���庯��
//definition :: = 'def' prototype expression
static FunctionAST* ParseDefinition(std::istream& input){
	get_next_tok(input); //eat 'def'

	PrototypeAST *func_proto = ParsePrototype(input);
	if (!func_proto){
		return nullptr;
	}

	ExprAST* body = ParseExpression(input);
	if (!body){
		return nullptr;
	}

	return FunctionAST::factory(func_proto, body);
}

//����ԭ�ͣ� ������unary operator�� binary operator Ҳ��������ͨ����
//prototype
//	:: = id '('id* ')'
//	:: = binary LETTER number ? (id, id)
//	:: = unary LETTER(id)
static PrototypeAST* ParsePrototype(std::istream& input){

	switch (cur_tok)
	{
	case TOK::IDENTIFIER_TOK://��������
	{
		std::string func_name = cur_identifier;
		get_next_tok(input);//eat identifier;

		if (cur_tok != '('){
			return ErrorP("ParsePrototype: expect '(' at begin of function definition");
		}
		get_next_tok(input);
		std::vector<std::string> func_args;

		if (cur_tok != ')'){
			while (1) {
				if (cur_tok == TOK::IDENTIFIER_TOK){
					func_args.push_back(cur_identifier);
					get_next_tok(input);
				}
				else
					break;
			}
		}

		if (cur_tok != ')'){
			return ErrorP("ParsePrototype: expect ')' at end of function prototype");
		}
		get_next_tok(input);
		return PrototypeAST::factory(func_name, func_args);
	}
	case TOK::UNARY_TOK:
	{
		get_next_tok(input);//eat unary

		std::string invalid_char = "[](){},/\\";
		//if the cur_tok is visited and not a alpha or digit or brace, it is valid
		if (cur_tok < 127 && cur_tok > 32 && !isalnum(cur_tok) && invalid_char.find(cur_tok) == std::string::npos){
			char op = cur_tok;
			get_next_tok(input);	//eat op

			if (cur_tok != '('){
				return ErrorP("ParsePrototype: expect '(' in unary definition");
			}

			get_next_tok(input);	//eat '('

			if (cur_tok != TOK::IDENTIFIER_TOK){
				return ErrorP("ParsePrototype: expect indentifier in unary arguments parsing");
			}

			std::string func_args = cur_identifier;

			get_next_tok(input);	//eat identifer;


			if (cur_tok != ')'){
				return ErrorP("ParsePrototype: expect ')' at end of unary aguments parsing");
			}

			get_next_tok(input);	//eat ')'

			return PrototypeAST::factory(std::string(1, op), std::vector<std::string>(1, func_args), 1);
		}
		else
			return ErrorP("ParsePrototype: Invalid operater character in unary defination");

	}
	case TOK::BINARY_TOK:
	{
		get_next_tok(input);	//eat binary token;

		std::string invalid_char = "[](){},/\\";
		//if the cur_tok is visited and not a alpha or digit or brace, it is valid
		if (cur_tok < 127 && cur_tok > 32 && !isalnum(cur_tok) && invalid_char.find(cur_tok) == std::string::npos){
			char op = cur_tok;
			get_next_tok(input);	//eat op
			if (cur_tok != TOK::INT_TOK){
				return ErrorP("ParsePrototype: expect a binary opeator precedence");
			}

			int32_t biop_prec = cur_integer;

			get_next_tok(input);	//eat integer;

			if (cur_tok != '('){
				return ErrorP("ParsePrototype: expect '(' in binary operator definition");
			}

			get_next_tok(input);	//eat '('

			std::vector<std::string> func_args;
			if (cur_tok != TOK::IDENTIFIER_TOK){
				return ErrorP("ParsePrototype: expect indentifier in binary arguments parser");
			}

			func_args.push_back(cur_identifier);

			get_next_tok(input);	//eat identifer;

			if (cur_tok != TOK::IDENTIFIER_TOK){
				return ErrorP("ParsePrototype: expect indentifier in binary arguments parser");
			}

			func_args.push_back(cur_identifier);

			get_next_tok(input);	//eat identifier;

			if (cur_tok != ')'){
				return ErrorP("ParsePrototype: expect ')' at end of binary operator aguments parser");
			}

			get_next_tok(input);	//eat ')'

			return PrototypeAST::factory(std::string(1, op), func_args, 2, biop_prec);
		}
		else
			return ErrorP("ParsePrototype: Invalid operater character in unary defination");
	}
	default:
		return ErrorP("ParsePrototype: Invalid syntax in definition");
	}

}


//external ��ʽ�����ⲿ����, ��extern�ؼ��ʿ�ʼ
//external :: = 'extern' prototype

static PrototypeAST* ParseExtern(std::istream& input){
	get_next_tok(input);//eat extern;
	return ParsePrototype(input);
}


//ifelseexpr �������ʽ
//
//ifexpr :: = 'if' expression 'then' expression 'else' expression

static ExprAST* ParseIfExpr(std::istream& input){
	get_next_tok(input); //eat 'if'

	ExprAST* ifexpr, *thenexpr, *elseexpr;

	ifexpr = ParseExpression(input);

	if (ifexpr == nullptr){
		return nullptr;
	}


	if (cur_tok != TOK::THEN_TOK){
		return Error("ParseIfExpr: Invalid syntax, expect 'then'");
	}

	get_next_tok(input);	//eat 'then'

	thenexpr = ParseExpression(input);

	if (thenexpr == nullptr){
		return nullptr;
	}

	//std::cout << "Parsing if" << std::endl;
	//std::cout << get_tok_name(cur_tok) << std::endl;
	if (cur_tok != TOK::ELSE_TOK){
		return Error("ParseIfExpr: Invalid syntax, expect 'else'");
	}
	get_next_tok(input); //eat 'else'
	elseexpr = ParseExpression(input);

	if (elseexpr == nullptr){
		return nullptr;
	}

	get_next_tok(input);
	return IfExprAST::factory(ifexpr, thenexpr, elseexpr);
}


//����������, ��var��ʼ, ����һ�����Ƕ���������ԡ�in���������壬��Щ������ʹ���롮in��������expression��
//
//varexpr :: = 'var' identifier('=' expression) ? (',' identifier('=' expression) ? )* 'in' expression
static ExprAST* ParseVarExpr(std::istream& input){
	get_next_tok(input);//eat 'var'

	std::vector<std::pair<std::string, ExprAST*>> variables;
	if (cur_tok != TOK::IDENTIFIER_TOK){
		return Error("ParseVarExpr: invalid syntax, expect indentifier at beigin of var expression");
	}

	while (true) {
		std::string var_name = cur_identifier;
		get_next_tok(input);
		ExprAST* init_expr = nullptr;
		//�����ĳ�ʼ���ǿ�ѡ�ģ�
		if (cur_tok == '='){
			init_expr = ParseExpression(input);
			if (init_expr == nullptr){
				return nullptr;
			}
		}
		variables.push_back(std::make_pair(var_name, init_expr));
		get_next_tok(input);
		if (cur_tok == ','){
			get_next_tok(input);
			if (cur_tok != TOK::IDENTIFIER_TOK){
				return Error("ParseVarExpr: expect identifer");
			}
		}
		else if (cur_tok == TOK::IN_TOK)
			break;
		else
			return Error("ParseVarExpr: Invalid syntax");
	}

	get_next_tok(input);//eat in;

	ExprAST* body = ParseExpression(input);

	if (body == nullptr){
		return nullptr;
	}

	return VarExprAST::factory(variables, body);
}

//********************************************
//			code generation
//********************************************



//creat an alloca instruction in the entry block of the function
//��ջ�ϴ洢������ʹ�ñ����ɱ��޸�
static AllocaInst* CreateEntryBlockAlloca(Function *TheFunction, const std::string &VarName, Type* _type){

	//
	IRBuilder<> tmpB(&TheFunction->getEntryBlock(), TheFunction->getEntryBlock().begin());
	return tmpB.CreateAlloca(_type, 0, VarName.c_str());
}


Value* DoubleValue::Codegen(){
	return ConstantFP::get(getGlobalContext(), APFloat(value));
}

Value* IntegerValue::Codegen(){
	//return ConstantInt::get(getGlobalContext(), APInt(32, value, true));
	return ConstantFP::get(getGlobalContext(), APFloat((double)value));
}

Value* VariableExprAST::Codegen(){
	Value* _val = namedValues[name];
	if (!_val){
		return nullptr;
	}
	return Builder.CreateLoad(_val, name);
}

Value* UnaryExpAST::Codegen(){
	//��ú�����������ִ�У���
	Value *unary_val = expr->Codegen();
	if (!unary_val){
		return nullptr;
	}

	//���unary function��ַ;
	Function* func_address = TheModule->getFunction(std::string("unary") + unary_op);

	if (func_address == nullptr){
		return ErrorV("UnaryExpAST: couldn't find the unary opeartor function");
	}


	return Builder.CreateCall(func_address, unary_val, "unop");
}

Value*  BinaryExprAST::Codegen(){

	DEBUG_CERR("BinaryExprAST codegen\n");
	if (binary_op == '='){
		if (VariableExprAST* left_expr = dynamic_cast<VariableExprAST*>(lhs)){
			Value* right_val = rhs->Codegen();
			if (right_val == nullptr){
				return nullptr;
			}

			Value* variable = namedValues[left_expr->getName()];

			if (variable == nullptr){
				return ErrorV("BinaryExprAST codegen: No such variable");
			}
			Builder.CreateStore(right_val, variable);
			return right_val;
		}
		else
			return ErrorV("binary_op '=' need identifer at left ");
	}

	Value *left_val = lhs->Codegen();

	Value *right_val = rhs->Codegen();
	if (left_val == nullptr || right_val == nullptr){
		return nullptr;
	}

	switch (binary_op)
	{
	case '+':
		return Builder.CreateFAdd(left_val, right_val, "addtmp");
	case '-':
		return Builder.CreateFSub(left_val, right_val, "subtmp");
	case '*':
		return Builder.CreateFMul(left_val, right_val, "multmp");
	case '/':
		return Builder.CreateFDiv(left_val, right_val, "divtmp");
	case '<':
	{
		/*std::cout << "left double? " << (left_val->getType()->isDoubleTy()) << std::endl;
		std::cout << "right double >" << (right_val->getType()->isDoubleTy()) << std::endl;
		std::cout << "typeid of left is" << (left_val->getType()->getTypeID()) << std::endl;*/

		left_val = Builder.CreateFCmpULT(left_val, right_val, "cmplesstmp");
		return Builder.CreateUIToFP(left_val, Type::getDoubleTy(getGlobalContext()), "booltmp");
	}
	default:
		break;
	}

	Function* func_address = theHelper->getFunction(std::string("binary") + binary_op);

	if (func_address == nullptr){
		return ErrorV("BinaryExprAST codegen: couldn't find the binary operator");
	}
	Value* func_args[] = { left_val, right_val };
	return Builder.CreateCall(func_address, func_args, "calltmp");
}


Value* CallExprAST::Codegen(){

	Function* call_func = theHelper->getFunction(this->func_name);

	DEBUG_CERR("Get function success\n");

	if (call_func == nullptr){
		return ErrorV("unknown function referenced");
	}


	if (call_func->arg_size() != func_args.size()){
		return ErrorV("Incorrect arguments passed");
	}

	std::vector<Value*> args;

	for (auto expr : func_args) {
		Value* cur_arg = expr->Codegen();
		if (!cur_arg){
			return nullptr;
		}
		args.push_back(cur_arg);
	}

	DEBUG_CERR("Arguments initialization success\n");

	if (call_func->empty()){
		fprintf(stderr, "It is empty\n");
	}

	return Builder.CreateCall(call_func, args, "calltmp");
}

Value* IfExprAST::Codegen(){
	Value* cond_val = ifexpr->Codegen();
	if (!cond_val){
		return nullptr;
	}
	cond_val = Builder.CreateFCmpONE(cond_val, ConstantFP::get(getGlobalContext(), APFloat(0.0)), "ifcond");

	Function* theFunc = Builder.GetInsertBlock()->getParent();

	BasicBlock* thenBB = BasicBlock::Create(getGlobalContext(), "then", theFunc);
	BasicBlock* elseBB = BasicBlock::Create(getGlobalContext(), "else");

	BasicBlock* mergeBB = BasicBlock::Create(getGlobalContext(), "ifcond");

	Builder.CreateCondBr(cond_val, thenBB, elseBB);

	Builder.SetInsertPoint(thenBB);
	Value* then_val = thenexpr->Codegen();

	if (then_val == nullptr){
		return nullptr;
	}

	//������������֧��ת
	Builder.CreateBr(mergeBB);
	//then expresssion ��codegen���ܻᴴ�����basicBlock�����then expression���
	//������basic block���ܲ���ԭ����thenBB��
	thenBB = Builder.GetInsertBlock();
	theFunc->getBasicBlockList().push_back(elseBB);

	Builder.SetInsertPoint(elseBB);

	Value* else_val = elseexpr->Codegen();

	if (else_val == nullptr){
		return nullptr;
	}

	Builder.CreateBr(mergeBB);

	elseBB = Builder.GetInsertBlock();

	theFunc->getBasicBlockList().push_back(mergeBB);

	Builder.SetInsertPoint(mergeBB);

	PHINode *if_phi_node = Builder.CreatePHI(Type::getDoubleTy(getGlobalContext()), 2, "iftmp");
	if_phi_node->addIncoming(then_val, thenBB);
	if_phi_node->addIncoming(else_val, elseBB);
	return if_phi_node;
}


Value* ForExprAST::Codegen(){
	// Output this as:
	//   var = alloca double
	//   ...
	//   start = startexpr
	//   store start -> var
	//   goto loop
	// loop:
	//   ...
	//   bodyexpr
	//   ...
	// loopend:
	//   step = stepexpr
	//   endcond = endexpr
	//
	//   curvar = load var
	//   nextvar = curvar + step
	//   store nextvar -> var
	//   br endcond, loop, endloop
	// outloop:
	Function* theFunc = Builder.GetInsertBlock()->getParent();
	Value* startVal = start->Codegen();

	if (startVal == nullptr){
		return nullptr;
	}


	AllocaInst *var_alloca = CreateEntryBlockAlloca(theFunc, var_name, startVal->getType());
	Builder.CreateStore(startVal, var_alloca);

	BasicBlock *loopBB = BasicBlock::Create(getGlobalContext(), "loop", theFunc);
	Builder.CreateBr(loopBB);
	Builder.SetInsertPoint(loopBB);
	//restore the original value
	AllocaInst* old_value = namedValues[var_name];
	namedValues[var_name] = var_alloca;
	//for expression ��Զ����һ��double 0, ����Ҫ��¼body��Value*
	if (this->body->Codegen() == nullptr){
		return nullptr;
	}

	Value* step_value;

	if (step){
		step_value = step->Codegen();
		if (step_value == nullptr){
			return ErrorV("ForExprAST codegen error");
		}
	}
	else
		step_value = ConstantFP::get(getGlobalContext(), APFloat(1.0));

	Value* end_val = end->Codegen();

	if (end_val == nullptr){
		return ErrorV("ForExprAST codegen error");
	}

	Value* cur_val = Builder.CreateLoad(var_alloca, var_name.c_str());
	Value* next_val = Builder.CreateFAdd(cur_val, step_value, "nextvar");
	Builder.CreateStore(next_val, var_alloca);

	end_val = Builder.CreateFCmpONE(end_val, ConstantFP::get(getGlobalContext(), APFloat(0.0)), "loopcond");

	BasicBlock *afterBB = BasicBlock::Create(getGlobalContext(), "afterloop", theFunc);

	Builder.CreateCondBr(end_val, loopBB, afterBB);

	Builder.SetInsertPoint(afterBB);


	//restore original value;
	if (old_value){
		namedValues[var_name] = old_value;
	}
	else{
		namedValues.erase(var_name);
	}
	return Constant::getNullValue(Type::getDoubleTy(getGlobalContext()));
}


//varexpr ::= 'var' identifier ('=' expression)?(',' identifier ('=' expression)?)* 'in' expression
Value* VarExprAST::Codegen(){
	Function* theFunc = Builder.GetInsertBlock()->getParent();
	std::vector<AllocaInst*> old_bindings;
	for (unsigned i = 0; i != this->vars.size(); ++i) {
		old_bindings.push_back(namedValues[vars[i].first]);
		ExprAST* init = vars[i].second;
		Value* init_val;
		if (init){
			init_val = init->Codegen();
			if (init_val == nullptr){
				return nullptr;
			}

		}
		else{
			init_val = ConstantFP::get(getGlobalContext(), APFloat(1.0));
		}
		AllocaInst* cur_alloca = CreateEntryBlockAlloca(theFunc, vars[i].first, init_val->getType());
		Builder.CreateStore(init_val, cur_alloca);
		namedValues[vars[i].first] = cur_alloca;
	}

	Value* body_val = this->body->Codegen();

	if (body_val == nullptr){
		return nullptr;
	}

	for (size_t i = 0; i < vars.size(); i++){
		namedValues[vars[i].first] = old_bindings[i];
	}
	return body_val;
}


Function* PrototypeAST::Codegen(){

	std::vector<Type*> array_type(this->func_args.size(), Type::getDoubleTy(getGlobalContext()));

	FunctionType *Func_type = FunctionType::get(Type::getDoubleTy(getGlobalContext()), array_type, false);


	std::cout << "Current register function name is " << func_name << std::endl;


	Module *current_module = theHelper->getModuleForNewFunction();

	Function* func = Function::Create(Func_type, Function::ExternalLinkage, func_name, current_module);

	if (func == nullptr){
		Error("Fail to construct a function proto");
		return nullptr;
	}

	if (func->getName() != func_name){
		func->eraseFromParent();

		//��theHelper���б�JIT(Modules vector)����û��JIT(openModule)��Modules�в���func��
		func = theHelper->getFunction(func_name);

		if (!func->empty()){
			ErrorF("redefinition of function");
			return nullptr;
		}

		if (func->arg_size() != func_args.size()){
			ErrorF("Differenct arguments given");
			return nullptr;
		}
	}
	unsigned Idx = 0;
	for (Function::arg_iterator AI = func->arg_begin(); Idx != this->func_args.size();
		++AI, ++Idx)
		AI->setName(this->func_args[Idx]);

	return func;
}

void PrototypeAST::CreateArgumentAllocas(Function *func){

	Function::arg_iterator arg_iter = func->arg_begin();
	Type* default_type = Type::getDoubleTy(getGlobalContext());
	for (uint32_t i = 0; i != func_args.size(); ++i) {
		AllocaInst *cur_alloca = CreateEntryBlockAlloca(func, func_args[i], default_type);
		Builder.CreateStore(arg_iter++, cur_alloca);
		namedValues[func_args[i]] = cur_alloca;
	}
}

Function *FunctionAST::Codegen(){
	namedValues.clear();
	Function* theFunc = this->func_proto->Codegen();
	if (theFunc == nullptr){
		DEBUG_CERR("Failed in Prototype generation");
		return nullptr;
	}
	DEBUG_CERR("Function prototype generation successs\n");

	BasicBlock *BB = BasicBlock::Create(getGlobalContext(), "entry", theFunc);
	Builder.SetInsertPoint(BB);

	this->func_proto->CreateArgumentAllocas(theFunc);

	if (Value *ret_value = this->body->Codegen()){



		if (this->func_proto->isBinary()){
			binary_op_precedence[func_proto->getOperatorName()] = func_proto->getBinaryProceence();
		}
		Builder.CreateRet(ret_value);
		verifyFunction(*theFunc);

		return theFunc;
	}

	DEBUG_CERR("Failed in function body codegen\n");
	theFunc->eraseFromParent();
	return nullptr;
}


static void HandleDefinition(std::istream& input){
	std::cout << "Handing definition" << std::endl;
	if (FunctionAST* func_ast = ParseDefinition(input)){
		if (Function* func = func_ast->Codegen()){
			fprintf(stderr, "Read the function definition:");
			func->dump();
		}
		else{
			fprintf(stderr, "failed in FunctionAST codegen");
		}
	}
	else{
		fprintf(stderr, "Invalid definition syntax");
		get_next_tok(input);
	}
}


static void HandleExtern(std::istream& input){
	if (PrototypeAST* proto = ParseExtern(input)){
		if (Function* func = proto->Codegen()){
			fprintf(stderr, "Read extern: ");
			func->dump();
		}
	}
	else{
		get_next_tok(input);
	}
}

static void HandleToplevelExpression(std::istream& input){
	typedef double(*anony_func_type)();
	if (FunctionAST *top_func_expr = ParseToplevelExpr(input)){
		if (Function* top_func = top_func_expr->Codegen()){
			anony_func_type anony_func = (anony_func_type)theHelper->getPointerToFunction(top_func);
			fprintf(stderr, "Evaluated to %lf\n", anony_func());
		}
	}
	else{
		get_next_tok(input);
	}
}


static void mainloop(std::istream& input){
	fprintf(stderr, "ready>");
	get_next_tok(input);
	while (true) {
		switch (cur_tok)
		{
		case TOK::DEF_TOK:
			HandleDefinition(input);
			break;
		case TOK::EXTERN_TOK:
			HandleExtern(input);
			break;
		case ';':
			get_next_tok(input);
			break;
		case TOK::EOF_TOK:
			fprintf(stderr, "token eof");
			return;
		default:
			HandleToplevelExpression(input);
			break;
		}
		fprintf(stderr, "ready>");
	}
}

//===----------------------------------------------------------------------===//
// "Library" functions that can be "extern'd" from user code.
//===----------------------------------------------------------------------===//
/// putchard - putchar that takes a double and returns 0.
extern "C" double putchard(double X) {
	putchar((char)X);
	return 0;
}
/// printd - printf that takes a double prints it as "%f\n", returning 0.
double printd(double X) {
	printf("%f\n", X);
	return 0;
}

void init_buildin_operator(){
	binary_op_precedence['='] = 2;
	binary_op_precedence['<'] = 10;
	binary_op_precedence['+'] = 20;
	binary_op_precedence['-'] = 20;
	binary_op_precedence['*'] = 40;
	binary_op_precedence['/'] = 40;
}

int main(){

	///InitializeNativeTarget - The main program should call this function to
	/// initialize the native target corresponding to the host.  This is useful 
	/// for JIT applications to ensure that the target gets linked in correctly.
	/// It is legal for a client to make multiple calls to this function.
	InitializeNativeTarget();

	/// InitializeNativeTargetAsmPrinter - The main program should call
	/// this function to initialize the native target asm printer.
	InitializeNativeTargetAsmPrinter();

	/// InitializeNativeTargetAsmParser - The main program should call
	/// this function to initialize the native target asm parser.
	InitializeNativeTargetAsmParser();

	LLVMContext &Context = getGlobalContext();


	llvm::sys::DynamicLibrary::AddSymbol("printd", &printd);

	init_buildin_operator();

	theHelper = new MCJITHelper(Context);

	std::unique_ptr<Module> Owner = make_unique<Module>("my cool jit", Context);
	TheModule = Owner.get();
	TheModule->setTargetTriple("i686-pc-windows-msvc-elf");
	// Create the JIT.  This takes ownership of the module.
	std::string ErrStr;
	TheExecutionEngine =
		EngineBuilder(std::move(Owner))
		.setErrorStr(&ErrStr)
		.setMCJITMemoryManager(llvm::make_unique<SectionMemoryManager>())
		.create();



	if (!TheExecutionEngine){
		fprintf(stderr, "Could not create ExecutionEngine: %s\n", ErrStr.c_str());
		exit(1);
	}


	// Prime the first token.
	//	fprintf(stderr, "ready> ");
	//get_next_tok(std::cin);
	// Make the module, which holds all the code.


	/*if (TheExecutionEngine->isSymbolSearchingDisabled ()){
	fprintf(stdout, "SymbolSearchingDisabled\n");
	}
	else{
	fprintf(stdout, "Able\n");
	}
	*/


	legacy::FunctionPassManager OurFPM(TheModule);

	TheModule->setDataLayout(TheExecutionEngine->getDataLayout());

	OurFPM.add(createBasicAliasAnalysisPass());
	// Promote allocas to registers.
	OurFPM.add(createPromoteMemoryToRegisterPass());
	// Do simple "peephole" optimizations and bit-twiddling optzns.
	OurFPM.add(createInstructionCombiningPass());
	// Reassociate expressions.
	OurFPM.add(createReassociatePass());
	// Eliminate Common SubExpressions.
	OurFPM.add(createGVNPass());
	// Simplify the control flow graph (deleting unreachable blocks, etc).
	OurFPM.add(createCFGSimplificationPass());
	OurFPM.doInitialization();
	// Set the global so the code gen can use this.
	TheFuncPM = &OurFPM;
	// Run the main "interpreter loop" now.
	mainloop(std::cin);
	TheFuncPM = 0;
	// Print out all of the generated code.
	TheModule->dump();



	std::cin.get();
	std::cin.get();
	std::cin.get();
	return 0;
}