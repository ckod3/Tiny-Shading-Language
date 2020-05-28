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

#include "llvm/IR/Verifier.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "compiler_impl.h"
#include "ast.h"
#include "shader_unit_pvt.h"
#include "shading_context.h"
#include "global_module.h"
#include "llvm_util.h"

// a temporary ugly solution for debugging for now
// #define DEBUG_OUTPUT

#ifdef DEBUG_OUTPUT
#include <iostream>
#endif

// Following are some externally defined interface and data structure generated by flex and bison.
struct yy_buffer_state;
typedef yy_buffer_state* YY_BUFFER_STATE;
int yylex_init(void**);
int yyparse(class Tsl_Namespace::TslCompiler_Impl*);
int yylex_destroy(void*);
YY_BUFFER_STATE yy_scan_string(const char* yystr, void* yyscanner);
void makeVerbose(int verbose);

TSL_NAMESPACE_BEGIN

TslCompiler_Impl::TslCompiler_Impl( GlobalModule& global_module ):m_global_module(global_module){
    reset();
}

TslCompiler_Impl::~TslCompiler_Impl() {
}

void TslCompiler_Impl::reset() {
    m_scanner = nullptr;
    m_ast_root = nullptr;

    m_closures_in_shader.clear();
}

void TslCompiler_Impl::push_function(AstNode_FunctionPrototype* node, const bool is_shader) {
    if (is_shader)
        m_ast_root = node;
    else
        m_functions.push_back(node);

#ifdef DEBUG_OUTPUT    
    // node->print();
#endif
}

void TslCompiler_Impl::push_structure_declaration(AstNode_StructDeclaration* structure) {
	m_structures.push_back(structure);

#ifdef DEBUG_OUTPUT
	// structure->print();
#endif
}

void* TslCompiler_Impl::get_scanner() {
    return m_scanner;
}

bool TslCompiler_Impl::compile(const char* source_code, ShaderUnit* su) {
#ifdef DEBUG_OUTPUT
    std::cout << source_code << std::endl;
#endif

    // not verbose for now, this should be properly exported through compiler option later.
    makeVerbose(false);

    // initialize flex scanner
    m_scanner = nullptr;
    yylex_init(&m_scanner);

    // flex and bison parsing
    yy_scan_string(source_code, m_scanner);
    const int parsing_result = yyparse(this);

    // destroy scanner information
    yylex_destroy(m_scanner);

    if( parsing_result != 0 )
		return false;

    auto su_pvt = su->get_shader_unit_data();

    // shader_unit_pvt holds the life time of this module, whenever it is needed by execution engine
    // another module is cloned from this one.
    su_pvt->m_module = std::make_unique<llvm::Module>(su->get_name(), m_llvm_context);
    auto module = su_pvt->m_module.get();
	if(!module)
		return false;

	// if there is a legit shader defined, generate LLVM IR
	if(m_ast_root){
		llvm::IRBuilder<> builder(m_llvm_context);

		LLVM_Compile_Context compile_context;
		compile_context.context = &m_llvm_context;
		compile_context.module = module;
		compile_context.builder = &builder;

        m_global_module.declare_closure_tree_types(m_llvm_context, &compile_context.m_structure_type_maps);
		m_global_module.declare_global_function(compile_context);
        for (auto& closure : m_closures_in_shader) {
            // declare the function first.
            auto function = m_global_module.declare_closure_function(closure, compile_context);

            if (!function) {
                // emit error here, unregistered closure touched
                return false;
            }
            compile_context.m_closures_maps[closure] = function;
        }

		// generate all data structures first
		for( auto& structure : m_structures )
			structure->codegen(compile_context);

        // code gen for all functions
        for( auto& function : m_functions )
            function->codegen(compile_context);

		// generate code for the shader in this module
		su_pvt->m_llvm_function = m_ast_root->codegen(compile_context);
        su_pvt->m_root_function_name = m_ast_root->get_function_name();
        su_pvt->m_global_module = &m_global_module;

        // parse shader parameters, this is for groupping shader units
        m_ast_root->parse_shader_parameters(su_pvt->m_shader_params);

        // keep track of the ast root of this shader unit
        su_pvt->m_ast_root = m_ast_root;

        // it should be safe to assume llvm function has to be generated, otherwise, the shader is invalid.
        if (!su_pvt->m_llvm_function)
            return false;
    }

	return true;
}

bool TslCompiler_Impl::resolve(ShaderUnit* su) {
    auto su_pvt = su->get_shader_unit_data();
    if (!su_pvt)
        return false;

    auto sg = dynamic_cast<ShaderGroup*>(su);
    auto module = su_pvt->m_module.get();

    // the resolving behavior is different for shader group and shader unit.
    if (sg) {
        // if no root shader setup yet, return false
        if (sg->m_root_shader_unit_name == "")
            return false;

        // if we can't find the root shader, it should return false
        if (0 == sg->m_shader_units.count(sg->m_root_shader_unit_name))
            return false;

        // essentially, this is a topological sort
        std::unordered_set<const ShaderUnit*>   visited_shader_units;
        std::unordered_set<const ShaderUnit*>   current_shader_units_being_visited;

        // get the root shader
        auto root_shader = sg->m_shader_units[sg->m_root_shader_unit_name];

        // allocate the shader module for this shader group
        sg->m_shader_unit_data->m_module = std::make_unique<llvm::Module>(sg->get_name(), m_llvm_context);
        module = sg->m_shader_unit_data->m_module.get();

        llvm::IRBuilder<> builder(m_llvm_context);

        LLVM_Compile_Context compile_context;
        compile_context.context = &m_llvm_context;
        compile_context.module = sg->m_shader_unit_data->m_module.get();
        compile_context.builder = &builder;

        // dependency modules
        std::vector<std::unique_ptr<llvm::Module>>  modules;
        modules.push_back(CloneModule(*(m_global_module.get_closure_module())));

        std::unordered_map<std::string, llvm::Function*>    m_shader_unit_llvm_function;
        // pre-declare all shader interfaces
        for (auto& shader_unit : sg->m_shader_units) {
            auto su_pvt = shader_unit.second->get_shader_unit_data();
            
#ifdef DEBUG_OUTPUT
            su_pvt->m_module->print(llvm::errs(), nullptr);
#endif

            modules.push_back(llvm::CloneModule(*su_pvt->m_module));
            auto function = su_pvt->m_ast_root->declare_shader(compile_context);

            m_shader_unit_llvm_function[shader_unit.second->get_name()] = function;
        }

        // this will generate a duplicated name, but I'll live with it for now.
        const auto func_name = sg->get_name() + "_shader_wrapper";
        auto function = root_shader->get_shader_unit_data()->m_ast_root->declare_shader(compile_context, true, true, func_name);

        // create a separate code block
        llvm::BasicBlock* wrapper_shader_entry = llvm::BasicBlock::Create(m_llvm_context, "entry", function);
        builder.SetInsertPoint(wrapper_shader_entry);

        // variable mapping keeps track of variables to bridge shader units
        VarMapping var_mapping;

        // generate wrapper shader source code.
        const auto ret = generate_shader_source(compile_context, sg, root_shader, visited_shader_units, current_shader_units_being_visited, var_mapping, m_shader_unit_llvm_function);
        if (!ret)
            return false;

        // copy out the output
        int i = 0;
        for (auto& arg : root_shader->get_shader_unit_data()->m_shader_params) {
            const auto name = arg.m_name;
            const auto is_input = !arg.m_is_output;

            if (is_input)
                continue;

            auto src_ptr = var_mapping[root_shader->get_name()][name];
            auto dest = function->getArg(i);
            auto src = builder.CreateLoad(src_ptr);
            builder.CreateStore(src, dest);
        }
        builder.CreateRetVoid();

#ifdef DEBUG_OUTPUT
        module->print(llvm::errs(), nullptr);
#endif

        // get the function pointer through execution engine
        sg->m_shader_unit_data->m_execution_engine = std::unique_ptr<llvm::ExecutionEngine>(llvm::EngineBuilder(std::move(sg->m_shader_unit_data->m_module)).create());

        auto execution_engine = sg->m_shader_unit_data->m_execution_engine.get();

        // setup data layout
        module->setDataLayout(execution_engine->getTargetMachine()->createDataLayout());

        // push all dependent modules
        for (auto& module : modules)
            execution_engine->addModule(std::move(module));

        // resolve the function pointer
        sg->m_shader_unit_data->m_function_pointer = execution_engine->getFunctionAddress(func_name);

        return true;
    } else {
        if (!su_pvt->m_module || !su_pvt->m_llvm_function)
            return false;

        // get the function pointer through execution engine
        su_pvt->m_execution_engine = std::unique_ptr<llvm::ExecutionEngine>(llvm::EngineBuilder(std::move(su_pvt->m_module)).create());

        auto execution_engine = su_pvt->m_execution_engine.get();

        // setup data layout
        module->setDataLayout(execution_engine->getTargetMachine()->createDataLayout());

#ifdef DEBUG_OUTPUT
        module->print(llvm::errs(), nullptr);
#endif

        // make sure to link the global closure model
        auto cloned_module = CloneModule(*(m_global_module.get_closure_module()));
        execution_engine->addModule(std::move(cloned_module));

        // resolve the function pointer
        const auto& function_name = su_pvt->m_root_function_name;
        su_pvt->m_function_pointer = execution_engine->getFunctionAddress(function_name);
    }

    // optimization pass, this is pretty cool because I don't have to implement those sophisticated optimization algorithms.
    if (su->allow_optimization()) {
        // Create a new pass manager attached to it.
        su_pvt->m_fpm = std::make_unique<llvm::legacy::FunctionPassManager>(module);

        // Do simple "peephole" optimizations and bit-twiddling optzns.
        su_pvt->m_fpm->add(llvm::createInstructionCombiningPass());
        // Re-associate expressions.
        su_pvt->m_fpm->add(llvm::createReassociatePass());
        // Eliminate Common SubExpressions.
        su_pvt->m_fpm->add(llvm::createGVNPass());
        // Simplify the control flow graph (deleting unreachable blocks, etc).
        su_pvt->m_fpm->add(llvm::createCFGSimplificationPass());

        su_pvt->m_fpm->doInitialization();

        su_pvt->m_fpm->run(*su_pvt->m_llvm_function);
    }

    // make sure the function is valid
    if (su->allow_verification() && !llvm::verifyFunction(*su_pvt->m_llvm_function, &llvm::errs()))
        return false;

    return true;
}

bool TslCompiler_Impl::generate_shader_source(  LLVM_Compile_Context& context, ShaderGroup* sg, ShaderUnit* su, std::unordered_set<const ShaderUnit*>& visited, std::unordered_set<const ShaderUnit*>& being_visited, 
                                                VarMapping& var_mapping, const std::unordered_map<std::string, llvm::Function*>& function_mapping ) {
    // cycles detected, incorrect shader setup!!!
    if (being_visited.count(su))
        return false;

    // avoid generating code for this shader unit again
    if (visited.count(su))
        return true;

    // push shader unit in cache so that we can detect cycles
    being_visited.insert(su);
    visited.insert(su);

    // check shader unit it depends on
    const std::string name = su->get_name();
    if (sg->m_shader_unit_connections.count(name)) {
        const auto& dependencies = sg->m_shader_unit_connections[name];
        for (const auto& dep : dependencies) {
            const auto& dep_shader_unit_name = dep.second.first;

            // if an undefined shader unit is assigned, simply quit the process
            if (sg->m_shader_units.count(name) == 0)
                return false;

            const auto dep_shader_unit = sg->m_shader_units[dep_shader_unit_name];
            if (!generate_shader_source(context, sg, dep_shader_unit, visited, being_visited, var_mapping, function_mapping))
                return false;
        }
    }

    // generate the shader code for this shader unit
    std::vector<llvm::Value*> args;
    for (auto& arg : su->get_shader_unit_data()->m_shader_params) {
        const auto name = arg.m_name;
        const auto type = arg.m_type;
        const auto is_input = !arg.m_is_output;

        if (is_input) {
            auto& connections = sg->m_shader_unit_connections;
            if (connections.count(su->get_name())) {
                auto& connection = connections[su->get_name()];
                if (connection.count(name)) {
                    const auto& source = connection[name];
                    auto var = var_mapping[source.first][source.second];
                    auto loaded_var = context.builder->CreateLoad(var);
                    args.push_back(loaded_var);
                }
            }
            else {
                llvm::Type* llvm_type = nullptr;
                switch (type) {
                case ShaderArgumentTypeEnum::TSL_TYPE_CLOSURE:
                    llvm_type = get_int_32_ptr_ty(context);
                    break;
                case ShaderArgumentTypeEnum::TSL_TYPE_INT:
                    llvm_type = get_int_32_ty(context);
                    break;
                case ShaderArgumentTypeEnum::TSL_TYPE_FLOAT:
                    llvm_type = get_float_ty(context);
                    break;
                case ShaderArgumentTypeEnum::TSL_TYPE_BOOL:
                    llvm_type = get_int_1_ty(context);
                    break;
                default:
                    // not supported yet
                    break;
                }
            }
        } else {
            llvm::Type* llvm_type = nullptr;
            switch (type) {
            case ShaderArgumentTypeEnum::TSL_TYPE_CLOSURE:
                llvm_type = get_int_32_ptr_ty(context);
                break;
            case ShaderArgumentTypeEnum::TSL_TYPE_INT:
                llvm_type = get_int_32_ty(context);
                break;
            case ShaderArgumentTypeEnum::TSL_TYPE_FLOAT:
                llvm_type = get_float_ty(context);
                break;
            case ShaderArgumentTypeEnum::TSL_TYPE_BOOL:
                llvm_type = get_int_1_ty(context);
                break;
            default:
                // not supported yet
                break;
            }

            auto output_var = context.builder->CreateAlloca(llvm_type, nullptr, name);
            var_mapping[su->get_name()][name] = output_var;
            args.push_back(output_var);
        }
    }

    // make the call
    const auto it = function_mapping.find(su->get_name());
    auto function = it == function_mapping.end() ? nullptr : it->second;
    context.builder->CreateCall(function, args);

    // erase the shader unit from being visited
    being_visited.erase(su);

    return true;
}

TSL_NAMESPACE_END
