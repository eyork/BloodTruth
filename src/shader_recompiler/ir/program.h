// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <unordered_set>
#include "shader_recompiler/frontend/instruction.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/ir/abstract_syntax_list.h"
#include "shader_recompiler/ir/basic_block.h"

namespace Shader::IR {

struct Program {
    explicit Program(Info& info_) : info{info_} {}

    AbstractSyntaxList syntax_list;
    BlockList blocks;
    BlockList post_order_blocks;
    std::vector<Gcn::GcnInst> ins_list;
    // PS4Recomp (predicate_wave64_pass.cpp, GPL-2.0): blocks of predicated regions, merges of the real
    // branches nested in them, ballots that stay per subgroup, and the virtual registers holding the
    // per-lane bits of 64-bit scalar masks.
    std::unordered_set<const Block*> predicated_merges;
    std::unordered_set<const Block*> predicated_blocks;
    std::unordered_set<const Block*> branch_merges;
    std::unordered_set<const Inst*> subgroup_ballots;
    std::unordered_set<const Inst*> lane_mask_phis;
    std::unordered_set<u64> scalar_mask_vregs;
    u32 next_reg_index{};
    Info& info;
};

void DumpProgram(const Program& program, const Info& info, const std::string& type = "",
                 bool force = false);

} // namespace Shader::IR
