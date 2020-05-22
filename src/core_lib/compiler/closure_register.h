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

#pragma once

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <string>
#include <mutex>
#include <memory>
#include "tslversion.h"
#include "closure.h"

TSL_NAMESPACE_BEGIN

class ClosureRegister {
public:
    // initialize the register
    bool init();

    // I prefer this way of registering a lot than the below one, need to follow this
    //template<class T>
    //ClosureID register_closure_type(const std::string& name);

    ClosureID register_closure_type(const std::string& name, ClosureVarList& mapping, int structure_size);

    // get global closure maker module
    llvm::Module* get_closure_module();

private:
    /**< a container holding all closures ids. */
    std::unordered_map<std::string, ClosureID>  m_closures;
    /**< current allocated closure id. */
    int m_current_closure_id = INVALID_CLOSURE_ID + 1;
    /**< a mutex to make sure access to closure container is thread-safe. */
    std::mutex m_closure_mutex;

    llvm::LLVMContext               m_llvm_context;
    std::unique_ptr<llvm::Module>   m_module;
};

TSL_NAMESPACE_END