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

#include <memory>
#include <type_traits>
#include "tslversion.h"

TSL_NAMESPACE_BEGIN

using ClosureID = int;
constexpr ClosureID INVALID_CLOSURE_ID = 0;
constexpr ClosureID CLOSURE_ADD = -1;
constexpr ClosureID CLOSURE_MUL = -2;

struct ClosureTreeNodeAdd;
struct ClosureTreeNodeMul;

struct ClosureTreeNodeBase {
    ClosureID   m_id = INVALID_CLOSURE_ID;

	ClosureTreeNodeAdd* as_add_node() {
		return reinterpret_cast<ClosureTreeNodeAdd*>(this);
	}

    ClosureTreeNodeMul* as_mul_node() {
        return reinterpret_cast<ClosureTreeNodeMul*>(this);
    }
};

struct ClosureTreeNodeAdd : public ClosureTreeNodeBase {
    ClosureTreeNodeBase*	m_closure0 = nullptr;
    ClosureTreeNodeBase*	m_closure1 = nullptr;
};

struct ClosureTreeNodeMul : public ClosureTreeNodeBase {
	float m_weight = 1.0f;
    ClosureTreeNodeBase*	m_closure = nullptr;
};

// It is very important to make sure the memory layout is as expected, there is no fancy stuff compiler is trying to do for these data structure.
// Because the same data structure will be generated from LLVM, which will expect this exact memory layout. If there is miss-match, it will crash.
static_assert( sizeof(ClosureTreeNodeBase) == sizeof(ClosureID) , "Invalid Closure Tree Node Size" );
static_assert( sizeof(ClosureTreeNodeAdd) == sizeof(ClosureID) + 4 /* memory padding. */ + sizeof(ClosureTreeNodeBase*) * 2 , "Invalid ClosureTreeNodeAdd Node Size" );
static_assert( sizeof(ClosureTreeNodeMul) == sizeof(ClosureID) + sizeof(float) + sizeof(ClosureTreeNodeBase*), "Invalid ClosureTreeNodeMul Node Size");

struct ClosureTree {
    ClosureTreeNodeBase*	m_root = nullptr;
};

TSL_NAMESPACE_END