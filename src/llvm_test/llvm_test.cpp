/*
    This file is a part of Tiny-Shading-Language or TSL, an open-source cross
    platform programming shading language.

    Copyright (c) 2020-2020 by Jiayin Cao - All rights reserved.

    TSL is a free software written for educational purpose. Anyone can distribute
    or modify it under the the terms of the GNU General Public License Version 3 as
    published by the Free Software Foundation. However, there is NO warranty that
    all components are functional in a perfect manner. Without even the implied
    warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License along with
    this program. If not, see <http://www.gnu.org/licenses/gpl-3.0.html>.
 */

#include <chrono>
#include <thread>
#include "gtest/gtest.h"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

using namespace llvm;

// This is not an ideal definition since strictly speaking, there could be arm based Windows.
// But I'm not testing it on ARM windows for now, leaving it this way.
#if __aarch64__
    #define TSL_ON_ARM_MAC
#endif

// The purpose of these unit tests is to make sure the LLVM version that it is used to compile TSL supports the basic
// feature needed to implement TSL properly.

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

namespace {
    ExecutionEngine* CreateExecuteEngine(std::unique_ptr<llvm::Module>& module){
#if defined(TSL_ON_ARM_MAC)
        return llvm::EngineBuilder(std::move(module)).setMArch("arm64").create();
#else
        return llvm::EngineBuilder(std::move(module)).create();
#endif
    }

    class LLVM : public testing::Test {
    public:
        LLVM() {
            llvm::InitializeNativeTarget();
            llvm::InitializeNativeTargetAsmPrinter();

            module = std::make_unique<llvm::Module>("my cool jit", context);
            EXPECT_NE((void*)module.get(), (void*)nullptr);
        }

        ~LLVM() = default;

        ExecutionEngine* get_execution_engine() {
            if (nullptr == execute_engine)
                execute_engine = std::unique_ptr<ExecutionEngine>(CreateExecuteEngine(module));
            return execute_engine.get();
        }

        template<class T>
        T get_function(const char* function_name) {
            auto ee = get_execution_engine();
            return (T)ee->getFunctionAddress(function_name);
        }

        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> module = nullptr;
        std::unique_ptr<llvm::ExecutionEngine> execute_engine = nullptr;
    };
}

/*
 * Manually composing a shader like this through LLVM without Flex and Bison
 *    int return_123(){
 *        return 123;
 *    }
 * And it should return 123 when JITed.
 */
TEST_F(LLVM, JIT) {
    Function* function = Function::Create(FunctionType::get(Type::getInt32Ty(context), {}, false), Function::ExternalLinkage, "return_123", module.get());
    BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", function);
    IRBuilder<> builder(bb);
    builder.CreateRet(builder.getInt32(123));

    const auto gv = get_execution_engine()->runFunction(function, {});
    EXPECT_EQ(gv.IntVal, 123);
}

/*
 * Manually trying to call an external function defined in C++.
 *    float my_proxy_function(){
 *        float input_var = 12.0f;
 *        return external_cpp_function(input_var);
 *    }
 * And it should return what external_cpp_function returns when JITed.
 */

extern "C" DLLEXPORT float llvm_test_external_cpp_function(float x) {
    return x * x;
}

TEST_F(LLVM, JIT_Ext_Func) {
    const float input_var = 12.0;

    // create external function prototype
    // this should perfectly match 'llvm_test_external_cpp_function' defined above.
    std::vector<Type*> proto_args(1, Type::getFloatTy(context));
    Function* ext_function = Function::Create(FunctionType::get(Type::getFloatTy(context), proto_args, false), Function::ExternalLinkage, "llvm_test_external_cpp_function", module.get());

    // the main function to be executed
    Function* function = Function::Create(FunctionType::get(Type::getFloatTy(context), {}, false), Function::ExternalLinkage, "my_proxy_function", module.get());
    BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", function);
    IRBuilder<> builder(bb);

    // call the external defined function in C++, llvm_test_external_cpp_function
    std::vector<Value*> args(1, ConstantFP::get(context, APFloat(input_var)));
    Value* value = builder.CreateCall(ext_function, args, "calltmp");

    // return whatever the call returns
    builder.CreateRet(value);

    // execute the jited function
    GenericValue gv = get_execution_engine()->runFunction(function, {});

    const float expected_result = llvm_test_external_cpp_function(input_var);
    EXPECT_EQ(gv.FloatVal, expected_result);
}

TEST_F(LLVM, External_Call) {
    const float input_var = 12.0;

    // create external function prototype
    // this should perfectly match 'llvm_test_external_cpp_function' defined above.
    std::vector<Type*> proto_args(1, Type::getFloatTy(context));
    Function* ext_function = Function::Create(FunctionType::get(Type::getFloatTy(context), proto_args, false), Function::ExternalLinkage, "llvm_test_external_cpp_function", module.get());

    // the main function to be executed
    Function* function = Function::Create(FunctionType::get(Type::getFloatTy(context), {}, false), Function::ExternalLinkage, "my_proxy_function", module.get());
    BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", function);
    IRBuilder<> builder(bb);

    // call the external defined function in C++, llvm_test_external_cpp_function
    std::vector<Value*> args(1);
    args[0] = ConstantFP::get(context, APFloat(input_var));
    Value* value = builder.CreateCall(ext_function, args, "calltmp");

    // return whatever the call returns
    builder.CreateRet(value);

    // execute the jited function
    std::vector<GenericValue> noargs;
    GenericValue gv = get_execution_engine()->runFunction(function, {});

    const float expected_result = llvm_test_external_cpp_function(input_var);
    EXPECT_EQ(gv.FloatVal, expected_result);
}

TEST_F(LLVM, System_Call) {
    const double input_var = 12.0;

    // create external function prototype
    std::vector<Type*> proto_args(1, Type::getDoubleTy(context));
    Function* ext_function = Function::Create(FunctionType::get(Type::getDoubleTy(context), proto_args, false), Function::ExternalLinkage, "cos", module.get());

    // the main function to be executed
    Function* function = Function::Create(FunctionType::get(Type::getDoubleTy(context), {}, false), Function::ExternalLinkage, "my_proxy_function", module.get());
    BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", function);
    IRBuilder<> builder(bb);

    // call the external defined function in C++, llvm_test_external_cpp_function
    std::vector<Value*> args(1);
    args[0] = ConstantFP::get(context, APFloat(input_var));
    Value* value = builder.CreateCall(ext_function, args, "calltmp");

    // return whatever the call returns
    builder.CreateRet(value);

    // execute the jited function
    std::vector<GenericValue> noargs;
    GenericValue gv = get_execution_engine()->runFunction(function, {});

    const float expected_result = cos(input_var);
    EXPECT_NEAR(gv.DoubleVal/expected_result, 1.0, 0.01);
}

TEST_F(LLVM, Return_One) {
    // the main function to be executed
    Function* function = Function::Create(FunctionType::get(Type::getInt32Ty(context), {}, false), Function::ExternalLinkage, "function", module.get());
    BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", function);
    IRBuilder<> builder(bb);

    // a mutable variable with default value of 1
    Value* num_allocate = llvm::ConstantInt::get(context, llvm::APInt(32, 1));
    Value* allocated = builder.CreateAlloca(Type::getInt32Ty(context), nullptr);
    builder.CreateStore(llvm::ConstantInt::get(context, llvm::APInt(32, 1)), allocated);
    Value* ret = builder.CreateLoad(allocated);

    // return whatever the call returns
    builder.CreateRet(ret);

    // execute the jited function
    auto func = get_function<int(*)()>("function");
    auto ret_value = func();
    EXPECT_EQ(ret_value, 1);
}

TEST_F(LLVM, JIT_Function_Pointer) {
    const float input_var = 12.0;

    // create external function prototype
    // this should perfectly match 'llvm_test_external_cpp_function' defined above.
    std::vector<Type*> proto_args(1, Type::getFloatTy(context));
    Function* ext_function = Function::Create(FunctionType::get(Type::getFloatTy(context), proto_args, false), Function::ExternalLinkage, "llvm_test_external_cpp_function", module.get());

    // the main function to be executed
    Function* function = Function::Create(FunctionType::get(Type::getFloatTy(context), {}, false), Function::ExternalLinkage, "my_proxy_function", module.get());
    BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", function);
    IRBuilder<> builder(bb);

    // call the external defined function in C++, llvm_test_external_cpp_function
    std::vector<Value*> args(1, ConstantFP::get(context, APFloat(input_var)));
    Value* value = builder.CreateCall(ext_function, args, "calltmp");

    // return whatever the call returns
    builder.CreateRet(value);

    // get the function pointer
    const auto shader_func = get_function<float(*)(float)>("my_proxy_function");

    const float returned_value = shader_func(input_var);
    const float expected_result = llvm_test_external_cpp_function(input_var);
    EXPECT_EQ(returned_value, expected_result);
}

/*
 * Create a shader code that has a float pointer as an argument.
 *   void shader_func( float* out_var ){
 *       *out_var = 3.0f;
 *   }
 * Passing the address of a floating point data and expect the correct result.
 */
TEST_F(LLVM, Output_Arg) {
    const float constant_var = 12.0;

    // the main function to be executed
    std::vector<Type*> arg_types(1);
    arg_types[0] = Type::getFloatPtrTy(context);
    FunctionType* function_prototype = FunctionType::get(Type::getVoidTy(context), arg_types, false);

    Function* function = Function::Create(function_prototype, Function::ExternalLinkage, "shader_func", module.get());
    BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", function);
    IRBuilder<> builder(bb);

    // there should be exactly one argument
    EXPECT_EQ(function->arg_size(), 1);

    auto var_ptr = function->args().begin();
    Value* value = ConstantFP::get(context, APFloat(constant_var));
    builder.CreateStore(value, var_ptr);

    builder.CreateRetVoid();

    // call the function compiled by llvm
    const auto shader_func = get_function<void (*)(float*)>("shader_func");
    float local_value = 0.0f;
    shader_func(&local_value);

    EXPECT_EQ(local_value, constant_var);
}

/*
 * This unit test passes a float pointer and expect an externally c++ defined function to write it.
 *   void shader_func( float* out_var ){
 *       external_func_cpp( out_var );
 *   }
 */

extern "C" DLLEXPORT void external_func_cpp(float* x) {
    *x = 12.0f;
}

TEST_F(LLVM, Passthrough_Pointer) {
    // create external function prototype
    // this should perfectly match 'external_func_cpp' defined above.
    std::vector<Type*> proto_args(1, Type::getFloatPtrTy(context));
    Function* ext_function = Function::Create(FunctionType::get(Type::getVoidTy(context), proto_args, false), Function::ExternalLinkage, "external_func_cpp", module.get());

    // the main function to be executed
    std::vector<Type*> arg_types(1);
    arg_types[0] = Type::getFloatPtrTy(context);
    FunctionType* function_prototype = FunctionType::get(Type::getVoidTy(context), arg_types, false);

    Function* function = Function::Create(function_prototype, Function::ExternalLinkage, "shader_func", module.get());
    BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", function);
    IRBuilder<> builder(bb);

    // there should be exactly one argument
    EXPECT_EQ(function->arg_size(), 1);

    // call the external defined function in C++, llvm_test_external_cpp_function
    std::vector<Value*> args(1);
    args[0] = function->getArg(0);
    builder.CreateCall(ext_function, args);

    builder.CreateRetVoid();

    // call the function compiled by llvm
    const auto shader_func = get_function<void (*)(float*)>("shader_func");
    float local_value = 0.0f;
    shader_func(&local_value);

    EXPECT_EQ(local_value, 12.0f);
}

/*
 * This unit test verifies the method to allow in/out keyword of TSL in LLVM.
 *    float inner_function( in float arg0 , out float arg1 ){
 *          arg0 = 2.0f;
 *          arg1 = 2.0f;
 *    }
 *
 *    float outter_function( out float arg0 , out float arg1 ){
 *        float local_arg0 = 123.0f, local_arg1 = 123.0f;
 *        in_out_test( local_arg0 , local_arg1 );
 *        *arg0 = local_arg0;
 *        *arg1 = local_arg1;
 *    }
 *
 * At the end of the day, arg0 should be 123.0f, arg1 should be 2.0f.
 */

 /*
 extern "C" DLLEXPORT void inner_function(float arg0 , float* arg1) {
     arg0 = 2.0f;
     *arg1 = 2.0f;
 }
 */

TEST_F(LLVM, In_and_Out) {
    // create external function prototype
    // this should perfectly match 'external_func_cpp' defined above.
    std::vector<Type*> proto_args0 = { Type::getFloatTy(context), Type::getFloatPtrTy(context) };
    Function* inner_function = Function::Create(FunctionType::get(Type::getVoidTy(context), proto_args0, false), Function::ExternalLinkage, "inner_function", module.get());

    {
        BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", inner_function);
        IRBuilder<> builder(bb);

        // declare the local variables
        auto local_param0 = builder.CreateAlloca(Type::getFloatTy(context), nullptr);
        auto local_param1 = builder.CreateAlloca(Type::getFloatPtrTy(context), nullptr);

        auto store_inst0 = builder.CreateStore(inner_function->getArg(0), local_param0);
        auto store_inst1 = builder.CreateStore(inner_function->getArg(1), local_param1);

        // arg0 = 2.0f
        Value* constant_value = ConstantFP::get(context, APFloat(2.0f));
        builder.CreateStore(constant_value, local_param0);

        // auto value0 = builder.CreateLoad(local_param0);
        auto value1 = builder.CreateLoad(local_param1);

        //builder.CreateStore( value0 , inner_function->getArg(0) );
        builder.CreateStore(constant_value, value1);
        builder.CreateRetVoid();
    }

    // the main function to be executed
    std::vector<Type*> proto_args1 = { Type::getFloatPtrTy(context), Type::getFloatPtrTy(context) };
    FunctionType* function_prototype = FunctionType::get(Type::getVoidTy(context), proto_args1, false);

    Function* outter_function = Function::Create(function_prototype, Function::ExternalLinkage, "outter_function", module.get());
    {
        BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", outter_function);
        IRBuilder<> builder(bb);

        auto local_param0 = builder.CreateAlloca(Type::getFloatTy(context), nullptr);
        auto local_param1 = builder.CreateAlloca(Type::getFloatTy(context), nullptr);

        auto constant_value = ConstantFP::get(context, APFloat(123.0f));
        auto store_inst0 = builder.CreateStore(constant_value, local_param0);
        auto store_inst1 = builder.CreateStore(constant_value, local_param1);

        auto value0 = builder.CreateLoad(local_param0);

        // Push the parameter stack
        std::vector<Value*> args;
        args.push_back(value0);
        args.push_back(store_inst1->getPointerOperand());
        builder.CreateCall(inner_function, args);

        value0 = builder.CreateLoad(local_param0);
        auto value1 = builder.CreateLoad(local_param1);

        builder.CreateStore(value0, outter_function->getArg(0));
        builder.CreateStore(value1, outter_function->getArg(1));
        builder.CreateRetVoid();
    }

    // call the function compiled by llvm
    const auto shader_func = get_function<void(*)(float*, float*)>("outter_function");
    float local_value0 = 10.0f, local_value1 = 20.0f;
    shader_func(&local_value0, &local_value1);

    EXPECT_EQ(local_value0, 123.0f);
    EXPECT_EQ(local_value1, 2.0f);
}

/*
 * This is an unit test for making sure there is a way to support global parameter.
 *   float from_cpu;
 *   out float to_cpu;
 *   float shader_func(){
 *       to_cpu = from_cpu + 2.0f;
 *       return from_cpu;
 *   }
 * Passing the address of a floating point data and expect the correct result.
 */
TEST_F(LLVM, Global_Input_And_Ouput) {
    float constant_input = 0.0f;
    float global_output = 0.0f;
    auto input_addr = ConstantInt::get(Type::getInt64Ty(context), uintptr_t(&constant_input));
    auto ptr_input_addr = ConstantExpr::getIntToPtr(input_addr, Type::getFloatPtrTy(context));
    GlobalVariable* global_input_value = new GlobalVariable(*module, Type::getFloatPtrTy(context), true, GlobalValue::ExternalLinkage, ptr_input_addr, "global_input");
    Constant* output_addr = ConstantInt::get(Type::getInt64Ty(context), uintptr_t(&global_output));
    auto ptr_output_addr = ConstantExpr::getIntToPtr(output_addr, Type::getFloatPtrTy(context));
    GlobalVariable* global_output_value = new GlobalVariable(*module, Type::getFloatPtrTy(context), false, GlobalValue::ExternalLinkage, ptr_output_addr, "global_output");

    // the main function to be executed
    FunctionType* function_prototype = FunctionType::get(Type::getFloatTy(context), {}, false);

    Function* function = Function::Create(function_prototype, Function::ExternalLinkage, "shader_func", module.get());
    BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", function);
    IRBuilder<> builder(bb);

    Value* input_value_addr = builder.CreateLoad(global_input_value);
    Value* input_value = builder.CreateLoad(input_value_addr);

    Value* constant_delta = ConstantFP::get(context, APFloat(2.0f));
    Value* add_result = builder.CreateFAdd(input_value, constant_delta);
    Value* output_value_addr = builder.CreateLoad(global_output_value);
    builder.CreateStore(add_result, output_value_addr);

    builder.CreateRet(input_value);

    // call the function compiled by llvm
    const auto shader_func = get_function<float(*)()>("shader_func");

    // imagine the following two instance of function calls are calling shaders twice with two different inputs
    constant_input = 1.0f;
    auto local_value = shader_func();
    EXPECT_EQ(local_value, 1.0f);
    EXPECT_EQ(global_output, local_value + 2.0f);

    constant_input = 13.0f;
    local_value = shader_func();
    EXPECT_EQ(local_value, 13.0f);
    EXPECT_EQ(global_output, local_value + 2.0f);
}

/*
 * This is an unit test for global data struct as input.
 *   struct Global_Structure{
 *       int m_data0;
 *       int m_data1;
 *   };
 *
 *   Global_Structure gs;
 *
 *   float shader_func(){
 *       return gs.m_data0 + gs.m_data1;
 *   }
 */
TEST_F(LLVM, Global_Structure_Input) {
    struct Global_Structure {
        float    m_data0 = 23.0f;
        float     m_data1 = 122.0f;
    };
    Global_Structure gs;

    Type* struct_var_types[] = { Type::getFloatTy(context), Type::getFloatTy(context) };
    auto* struct_type = StructType::create(struct_var_types, "Global_Structure")->getPointerTo();

    Constant* input_addr = ConstantInt::get(Type::getInt64Ty(context), uintptr_t(&gs));
    auto ptr_input_addr = ConstantExpr::getIntToPtr(input_addr, struct_type);
    GlobalVariable* global_struct_value = new GlobalVariable(*module, struct_type, false, GlobalValue::ExternalLinkage, ptr_input_addr, "global_input");

    // the main function to be executed
    FunctionType* function_prototype = FunctionType::get(Type::getFloatTy(context), {}, false);

    Function* function = Function::Create(function_prototype, Function::ExternalLinkage, "shader_func", module.get());
    BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", function);
    IRBuilder<> builder(bb);

    Value* input_value = builder.CreateLoad(global_struct_value);

    auto gep0 = builder.CreateConstGEP2_32(nullptr, input_value, 0, 0);
    auto var0 = builder.CreatePointerCast(gep0, Type::getFloatPtrTy(context));
    auto value0 = builder.CreateLoad(var0);

    auto gep1 = builder.CreateConstGEP2_32(nullptr, input_value, 0, 1);
    auto var1 = builder.CreatePointerCast(gep1, Type::getFloatPtrTy(context));
    auto value1 = builder.CreateLoad(var1);

    auto var = builder.CreateFAdd(value0, value1);
    builder.CreateRet(var);

    // call the function compiled by llvm
    const auto shader_func = get_function<float(*)()>("shader_func");

    const auto local_value = shader_func();
    EXPECT_EQ(local_value, gs.m_data0 + gs.m_data1);
}

/*
 * This is an unit test for global data struct as input.
 *   struct Global_Structure{
 *       int m_data0;
 *       out int m_data1;
 *   };
 *
 *   Global_Structure gs;
 *
 *   void shader_func(){
 *       gs.m_data1 = gs.m_data0 * 2.0f;
 *   }
 */
TEST_F(LLVM, Global_Structure_Output) {
    struct Global_Structure {
        float    m_data0 = 23.0f;
        float     m_data1 = 122.0f;
    };
    Global_Structure gs;

    Type* struct_var_types[] = { Type::getFloatTy(context), Type::getFloatTy(context) };
    auto* struct_type = StructType::create(struct_var_types, "Global_Structure")->getPointerTo();

    Constant* input_addr = ConstantInt::get(Type::getInt64Ty(context), uintptr_t(&gs));
    auto ptr_input_addr = ConstantExpr::getIntToPtr(input_addr, struct_type);
    GlobalVariable* global_struct_value = new GlobalVariable(*module, struct_type, false, GlobalValue::ExternalLinkage, ptr_input_addr, "global_input");

    // the main function to be executed
    FunctionType* function_prototype = FunctionType::get(Type::getVoidTy(context), {}, false);

    Function* function = Function::Create(function_prototype, Function::ExternalLinkage, "shader_func", module.get());
    BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", function);
    IRBuilder<> builder(bb);

    Value* input_value = builder.CreateLoad(global_struct_value);

    auto gep0 = builder.CreateConstGEP2_32(nullptr, input_value, 0, 0);
    auto var0 = builder.CreatePointerCast(gep0, Type::getFloatPtrTy(context));
    auto value0 = builder.CreateLoad(var0);

    Value* constant_multi = ConstantFP::get(context, APFloat(2.0f));
    auto double_value0 = builder.CreateFMul(value0, constant_multi);

    auto gep1 = builder.CreateConstGEP2_32(nullptr, input_value, 0, 1);
    auto var1 = builder.CreatePointerCast(gep1, Type::getFloatPtrTy(context));
    builder.CreateStore(double_value0, var1);

    builder.CreateRetVoid();

    // call the function compiled by llvm
    const auto shader_func = get_function<void(*)()>("shader_func");

    shader_func();

    EXPECT_EQ(gs.m_data1, 2.0f * gs.m_data0);
}

/*
 * This is an unit test for global data struct as input.
 *   struct Global_Structure{
 *       int m_data0;
 *       int m_data1;
 *   };
 *
 *   float shader_func(){
 *       Global_Structure gs;
 *       gs.m_data0 = 34.0f;
 *       gs.m_data1 = 32.0f;
 *       return gs.m_data0 + gs.m_data1;
 *   }
 */
TEST_F(LLVM, Local_Structure) {
    Type* struct_var_types[] = { Type::getFloatTy(context), Type::getFloatTy(context) };
    auto* struct_type = StructType::create(struct_var_types, "Global_Structure");

    // the main function to be executed
    FunctionType* function_prototype = FunctionType::get(Type::getFloatTy(context), {}, false);

    Function* function = Function::Create(function_prototype, Function::ExternalLinkage, "shader_func", module.get());
    BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", function);
    IRBuilder<> builder(bb);

    // allocate an instance of struct
    Value* num_allocate = llvm::ConstantInt::get(context, llvm::APInt(64, 1));
    AllocaInst* allocainst = builder.CreateAlloca(struct_type, num_allocate, "local_instance");

    Value* constant0 = ConstantFP::get(context, APFloat(34.0f));
    auto gep0 = builder.CreateConstGEP2_32(nullptr, allocainst, 0, 0);
    auto var0 = builder.CreatePointerCast(gep0, Type::getFloatPtrTy(context));
    auto store_inst0 = builder.CreateStore(constant0, var0);

    Value* constant1 = ConstantFP::get(context, APFloat(32.0f));
    auto gep1 = builder.CreateConstGEP2_32(nullptr, allocainst, 0, 1);
    auto var1 = builder.CreatePointerCast(gep1, Type::getFloatPtrTy(context));
    auto store_inst1 = builder.CreateStore(constant1, var1);

    auto var = builder.CreateFAdd(store_inst0->getValueOperand(), store_inst1->getValueOperand());
    builder.CreateRet(var);

    // call the function compiled by llvm
    const auto shader_func = get_function<float(*)()>("shader_func");

    const auto local_value = shader_func();
    EXPECT_EQ(local_value, 34.0f + 32.0f);
}

/*
 * Fill the result with a structure allocated in LLVM.
 *
 *   out ClosureTree closure_tree;
 *   void fake_shader(){
 *       // this is not the real thing, it is just a test
 *         closure_tree = make_closure<Lambert>();
 *   }
 */
TEST_F(LLVM, Closure_Tree_Output) {
    // declare 'malloc' function in llvm module
    std::vector<Type*> proto_args(1, Type::getInt32Ty(context));
    Function* malloc_function = Function::Create(FunctionType::get(Type::getInt32PtrTy(context), proto_args, false), Function::ExternalLinkage, "malloc", module.get());

    // data type for closure_tree
    Type* closure_tree_var_types[] = { Type::getInt64PtrTy(context) };
    auto closure_tree_type = StructType::create(closure_tree_var_types, "ClosureTree");
    auto closure_tree_type_ptr = closure_tree_type->getPointerTo();

    // data type for ClosureTreeNodeBase
    Type* closure_tree_node_base_var_types[] = { Type::getInt32Ty(context) };
    auto closure_tree_node_base = StructType::create(closure_tree_node_base_var_types, "ClosureTreeNodeBase");
    auto closure_tree_node_base_ptr = closure_tree_node_base->getPointerTo();

    struct FakeClosureNode {
        unsigned int m_id = 0;
    };
    struct FakeClosureTree {
        FakeClosureNode* m_root = nullptr;
    };
    // The result closure tree to be extracted
    FakeClosureTree ct;

    // Create the global parameter for shader to access
    Constant* input_addr = ConstantInt::get(Type::getInt64Ty(context), uintptr_t(&ct));
    auto ptr_input_addr = ConstantExpr::getIntToPtr(input_addr, closure_tree_type_ptr);
    GlobalVariable* global_struct_value = new GlobalVariable(*module, closure_tree_type_ptr, false, GlobalValue::ExternalLinkage, ptr_input_addr, "closure_tree");

    // the main function to be executed
    FunctionType* function_prototype = FunctionType::get(Type::getVoidTy(context), {}, false);

    Function* function = Function::Create(function_prototype, Function::ExternalLinkage, "fake_shader", module.get());
    BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", function);
    IRBuilder<> builder(bb);

    // Load the pointer to the global structure
    Value* input_value = builder.CreateLoad(global_struct_value);

    // Get the 'm_root' parameter from ClosureTree
    auto gep0 = builder.CreateConstGEP2_32(nullptr, input_value, 0, 0);
    auto var0 = builder.CreatePointerCast(gep0, closure_tree_node_base_ptr->getPointerTo());

    // Allocate memory on heap
    std::vector<Value*> args(1, ConstantInt::get(context, APInt(32, sizeof(FakeClosureNode))));
    Value* value = builder.CreateCall(malloc_function, args, "malloc");
    Value* allocainst = builder.CreatePointerCast(value, closure_tree_node_base_ptr);

    // Store the lambert id, pretend it to be 1024
    Value* constant0 = ConstantInt::get(context, APInt(32, 1024));
    auto gep1 = builder.CreateConstGEP2_32(nullptr, allocainst, 0, 0);
    builder.CreateStore(constant0, gep1);

    // Store the allocated address to m_root
    builder.CreateStore(allocainst, var0);

    // It is done
    builder.CreateRetVoid();

    // call the function compiled by llvm
    const auto shader_func = get_function<void(*)()>("fake_shader");

    // shader execution.
    shader_func();

    // We should have a valid root and it should have an id that is 1024
    EXPECT_NE((void*)ct.m_root, (void*)nullptr);
    EXPECT_EQ(ct.m_root->m_id, 1024);

    // Clear the memory allocated
    free(ct.m_root);
}

/*
 * Since llvm::context is not thread-safe, meaning each thread needs to have a local copy of llvm::context.
 * This unit test is to verify a function defined in one unit test can be used in modules defined with other llvm::context.
 * This is the foundation of multi-thread compliation of TSL.
 */
TEST_F(LLVM, Cross_Context) {
    {
        llvm::LLVMContext context0;
        auto float_type = Type::getFloatTy(context0);
        auto float_ptr_type = Type::getFloatPtrTy(context0);
        std::unique_ptr<llvm::Module> module0 = std::make_unique<llvm::Module>("module 1", context0);

        // define a simple function that returns something random, like 123.
        std::vector<Type*> arg_types(1);
        arg_types[0] = float_ptr_type;
        Function* one_function = Function::Create(FunctionType::get(Type::getVoidTy(context0), arg_types, false), Function::ExternalLinkage, "one_function", module0.get());

        BasicBlock* bb = BasicBlock::Create(context0, "EntryBlock", one_function);
        IRBuilder<> builder(bb);
        auto constant = ConstantFP::get(context0, APFloat(123.0f));
        auto arg = one_function->getArg(0);
        builder.CreateStore(constant, arg);
        builder.CreateRetVoid();

        {
            llvm::LLVMContext& context1 = context0;
            std::unique_ptr<llvm::Module> module = std::make_unique<llvm::Module>("module 2", context1);

            // define a simple function that returns something random, like 123.
            Function* one_function = Function::Create(FunctionType::get(Type::getVoidTy(context1), arg_types, false), Function::ExternalLinkage, "one_function", module.get());

            // this function does nothing but to call the one defined above.
            Function* anothere_function = Function::Create(FunctionType::get(float_type, {}, false), Function::ExternalLinkage, "another_function", module.get());

            BasicBlock* bb = BasicBlock::Create(context1, "EntryBlock", anothere_function);
            IRBuilder<> builder(bb);
            auto value = builder.CreateAlloca(float_type);
            std::vector<llvm::Value*> args(1, value);
            builder.CreateCall(one_function, args);
            auto val = builder.CreateLoad(value);
            builder.CreateRet(val);
            
            // call 'another_function', expect 123 to be returned.
            std::unique_ptr<llvm::ExecutionEngine> execute_engine = std::unique_ptr<ExecutionEngine>(CreateExecuteEngine(module));
            execute_engine->addModule(CloneModule(*module0));

            const auto shader_func = (float(*)())execute_engine->getFunctionAddress("another_function");
            const auto ret = shader_func();
            EXPECT_EQ(ret, 123.0f);
        }

    }
}

/*
 * An unit test to verify LLVM is also stable in a multi-threaded environment.
 * This unit test doesn't verify all cases, I may need to come up with a better unit test for it later.
 */
TEST_F(LLVM, Multi_Thread_Compiling) {
    auto thread_task = []() {
        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> module = nullptr;

        module = std::make_unique<llvm::Module>("my cool jit", context);
        EXPECT_NE((void*)module.get(), (void*)nullptr);

        // create external function prototype
        // this should perfectly match 'external_func_cpp' defined above.
        std::vector<Type*> proto_args0 = { Type::getFloatTy(context), Type::getFloatPtrTy(context) };
        Function* inner_function = Function::Create(FunctionType::get(Type::getVoidTy(context), proto_args0, false), Function::ExternalLinkage, "inner_function", module.get());

        {
            BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", inner_function);
            IRBuilder<> builder(bb);

            // declare the local variables
            auto local_param0 = builder.CreateAlloca(Type::getFloatTy(context), nullptr);
            auto local_param1 = builder.CreateAlloca(Type::getFloatPtrTy(context), nullptr);

            auto store_inst0 = builder.CreateStore(inner_function->getArg(0), local_param0);
            auto store_inst1 = builder.CreateStore(inner_function->getArg(1), local_param1);

            // arg0 = 2.0f
            Value* constant_value = ConstantFP::get(context, APFloat(2.0f));
            builder.CreateStore(constant_value, local_param0);

            auto value1 = builder.CreateLoad(local_param1);

            builder.CreateStore(constant_value, value1);
            builder.CreateRetVoid();
        }

        // the main function to be executed
        std::vector<Type*> proto_args1 = { Type::getFloatPtrTy(context), Type::getFloatPtrTy(context) };
        FunctionType* function_prototype = FunctionType::get(Type::getVoidTy(context), proto_args1, false);

        Function* outter_function = Function::Create(function_prototype, Function::ExternalLinkage, "outter_function", module.get());
        {
            BasicBlock* bb = BasicBlock::Create(context, "EntryBlock", outter_function);
            IRBuilder<> builder(bb);

            auto local_param0 = builder.CreateAlloca(Type::getFloatTy(context), nullptr);
            auto local_param1 = builder.CreateAlloca(Type::getFloatTy(context), nullptr);

            auto constant_value = ConstantFP::get(context, APFloat(123.0f));
            auto store_inst0 = builder.CreateStore(constant_value, local_param0);
            auto store_inst1 = builder.CreateStore(constant_value, local_param1);

            auto value0 = builder.CreateLoad(local_param0);

            // Push the parameter stack
            std::vector<Value*> args;
            args.push_back(value0);
            args.push_back(store_inst1->getPointerOperand());
            builder.CreateCall(inner_function, args);

            value0 = builder.CreateLoad(local_param0);
            auto value1 = builder.CreateLoad(local_param1);

            builder.CreateStore(value0, outter_function->getArg(0));
            builder.CreateStore(value1, outter_function->getArg(1));
            builder.CreateRetVoid();
        }

        // call the function compiled by llvm
        std::unique_ptr<llvm::ExecutionEngine> execute_engine = std::unique_ptr<ExecutionEngine>(CreateExecuteEngine(module));
        const auto shader_func = (void(*)(float*, float*))execute_engine->getFunctionAddress("outter_function");

        float local_value0 = 10.0f, local_value1 = 20.0f;
        shader_func(&local_value0, &local_value1);

        EXPECT_EQ(local_value0, 123.0f);
        EXPECT_EQ(local_value1, 2.0f);
    };

    std::vector<std::thread> threads(16);
    for (int i = 0; i < 16; ++i){
        threads[i] = std::thread([&]() { thread_task(); });
    }

    // making sure all threads are done
    std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { thread.join(); });
}

/*
 * Multi-thread shader execution.
 * Normally, this feature is the foundation to support global input and also make it compatible with mult-thread shader execution.
 *
 * For some reason, GlobalVariable with local thread storage is not functional as epxected on Windows, but it does work on Ubuntu.
 * However, due to the inconsistant behavior across multiple platforms, I will have to seek for other solution to support global parameters.
 */
TEST_F(LLVM, DISABLED_Multi_Thread_Execution) {
    //setCurrentDebugType("jit");
    //llvm::DebugFlag = true;

    const float defualt_value = 12.0f;
    auto int_address = uintptr_t(&defualt_value);
    Constant* input_addr = ConstantInt::get(Type::getInt64Ty(context), int_address);
    auto addr = ConstantExpr::getIntToPtr(input_addr, Type::getFloatPtrTy(context));
    GlobalVariable* global_input_value = new GlobalVariable(*module, Type::getFloatPtrTy(context), false, GlobalValue::InternalLinkage, addr, "global_input", nullptr);
    
    global_input_value->setThreadLocal(true);

    Function* set_constant_function = Function::Create(FunctionType::get(Type::getVoidTy(context), {Type::getInt32PtrTy(context)}, false), Function::ExternalLinkage, "set_global_input", module.get());
    {
        BasicBlock* bb = BasicBlock::Create(context, "set_global_input_EntryBlock", set_constant_function);
        IRBuilder<> builder(bb);

        Value* value = set_constant_function->getArg(0);
        Value* allocainst = builder.CreatePointerCast(value, Type::getFloatPtrTy(context));

        builder.CreateStore(allocainst, global_input_value);
        builder.CreateRetVoid();
    }
    
    Function* shader_function = Function::Create(FunctionType::get(Type::getFloatTy(context), {}, false), Function::ExternalLinkage, "shader_function", module.get());
    {
        BasicBlock* bb = BasicBlock::Create(context, "shader_function_EntryBlock", shader_function);
        IRBuilder<> builder(bb);
        Value* ptr_value = builder.CreateLoad(global_input_value);
        Value* value = builder.CreateLoad(ptr_value);
        builder.CreateRet(value);
    }

    std::unique_ptr<llvm::ExecutionEngine> execute_engine = std::unique_ptr<ExecutionEngine>(CreateExecuteEngine(module));

    const auto set_constant = (void(*)(float*))execute_engine->getFunctionAddress("set_global_input");
    const auto shader = (float(*)())execute_engine->getFunctionAddress("shader_function");

    float ret = shader();
    EXPECT_EQ(ret, 12.0f);

    std::vector<std::thread> threads(16);
    for (int i = 0; i < 16; ++i) {
        threads[i] = std::thread([&](int tid) {
            int k = 0;
            while( k++ < 100 ){
                float tmp = (float)i * 1024.0f + (float) k * 13.0f;
                set_constant(&tmp);

                // Intentionally creating some time bubble between setting and using the value in the hope that other threads will set this global value too
                // This will increase the risk of breaking the unit test if the global value is not thread safe.
                // ideally, it shouldn't affect the logic.
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                auto ret = shader();
                EXPECT_EQ(ret, tmp);
            }
        }, i);
    }

    // making sure all threads are done
    std::for_each(threads.begin(), threads.end(), [](std::thread& thread) { thread.join(); });
}

/*
 * For some reason, GlobalVariable with local thread storage is not functional as epxected on Windows, but it does work on Ubuntu.
 * However, due to the inconsistant behavior across multiple platforms, I will have to seek for other solution to support global parameters.
 */
TEST_F(LLVM, DISABLED_LTS_GlobalVariable) {
    setCurrentDebugType("jit");
    llvm::DebugFlag = true;

    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();

    Constant* init_value = Constant::getAllOnesValue(Type::getInt32Ty(context));// ConstantInt::get(Type::getInt32Ty(context), APInt(32, 12));

    GlobalVariable* bsp = new GlobalVariable(*module, Type::getInt32Ty(context), false, GlobalValue::InternalLinkage, init_value, "bsp", 0);

    // uncommenting this line will lead to crash in this function ( shader() )
    bsp->setThreadLocal(true);
    bsp->setInitializer(init_value);

    auto DL = module->getDataLayout();
    Align MaxAlignment(std::max(DL.getABITypeAlignment(Type::getInt32Ty(context)), DL.getABITypeAlignment(Type::getInt32Ty(context))));
    bsp->setAlignment(MaxAlignment);

    llvm::FunctionType* funcType = llvm::FunctionType::get(Type::getInt32Ty(context), {}, false);
    llvm::Function* mainFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "main", module.get());

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entrypoint", mainFunc);
    IRBuilder<> builder(bb);
    builder.CreateRet(builder.CreateLoad(bsp));

    auto TheExecutionEngine = std::unique_ptr<ExecutionEngine>(CreateExecuteEngine(module));
    const auto shader = (int(*)())TheExecutionEngine->getFunctionAddress("main");
    
    EXPECT_EQ(shader(), -1);
}