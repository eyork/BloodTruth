// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <bit>
#include <optional>
#include <cstdlib>
#include <span>
#include <string_view>

#include "common/logging/classes.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/ir/basic_block.h"
#include "shader_recompiler/ir/breadth_first_search.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/program.h"
#include "shader_recompiler/profile.h"

namespace Shader::Optimization {

static bool HalfWaveReadLane() {
    static const bool enabled = [] {
        const char* value = std::getenv("SHADPS4_WAVE64_HALF_READLANE");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

static bool ForceNonUniformLowering() {
    static const bool enabled = [] {
        const char* value = std::getenv("SHADPS4_WAVE64_FORCE_NONUNIFORM");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

static bool IsDivergentCondition(const IR::U1& condition) {
    if (condition.IsImmediate()) {
        return false;
    }
    const IR::Inst* const condition_inst = condition.Inst();
    return IR::BreadthFirstSearch(condition_inst,
                                  [](const IR::Inst* inst) -> std::optional<bool> {
                                      switch (inst->GetOpcode()) {
                                      case IR::Opcode::LaneId:
                                          return true;
                                      case IR::Opcode::GetAttributeU32:
                                          if (inst->Arg(0).Attribute() ==
                                              IR::Attribute::LocalInvocationId) {
                                              return true;
                                          }
                                          break;
                                      default:
                                          break;
                                      }
                                      return std::nullopt;
                                  })
        .value_or(false);
}

static std::vector<IR::Block*> FindUniformBlocks(const IR::Program& program) {
    using Type = IR::AbstractSyntaxNode::Type;

    struct ConditionalScope {
        const IR::Block* merge;
        bool divergent;
    };

    std::vector<IR::Block*> blocks;
    std::vector<ConditionalScope> conditionals;
    std::vector<const IR::Block*> loops;
    u32 divergence_depth{};
    for (const IR::AbstractSyntaxNode& node : program.syntax_list) {
        switch (node.type) {
        case Type::If: {
            const bool divergent = IsDivergentCondition(node.data.if_node.cond);
            conditionals.push_back({node.data.if_node.merge, divergent});
            divergence_depth += static_cast<u32>(divergent);
            break;
        }
        case Type::EndIf:
            ASSERT(!conditionals.empty() && conditionals.back().merge == node.data.end_if.merge);
            divergence_depth -= static_cast<u32>(conditionals.back().divergent);
            conditionals.pop_back();
            break;
        case Type::Loop:
            loops.push_back(node.data.loop.merge);
            break;
        case Type::Repeat:
            if (loops.empty() || loops.back() != node.data.repeat.merge) {
                return {};
            }
            loops.pop_back();
            break;
        case Type::Block:
            if (divergence_depth == 0 && loops.empty()) {
                blocks.push_back(node.data.block);
            }
            break;
        default:
            break;
        }
    }
    if (!conditionals.empty() || !loops.empty()) {
        return {};
    }
    return blocks;
}

static IR::Inst* FindBallotForMaskedBitCount(const IR::Inst& mbcnt) {
    IR::Value value = mbcnt.Arg(0);
    if (value.IsImmediate()) {
        return nullptr;
    }
    IR::Inst* inst = value.Inst();
    if (inst->GetOpcode() != IR::Opcode::CompositeExtractU32x2 || inst->Arg(0).IsImmediate()) {
        return nullptr;
    }
    inst = inst->Arg(0).Inst();
    if (inst->GetOpcode() != IR::Opcode::UnpackUint2x32 || inst->Arg(0).IsImmediate()) {
        return nullptr;
    }
    inst = inst->Arg(0).Inst();
    return inst->GetOpcode() == IR::Opcode::Ballot ? inst : nullptr;
}

static int HoistCrossLaneOpsLevel() {
    static const int level = [] {
        const char* value = std::getenv("SHADPS4_WAVE64_HOIST");
        return value != nullptr ? std::atoi(value) : 0;
    }();
    return level;
}

static thread_local bool hoist_whole_wave_ops{};

static bool HoistWholeWaveOps(u64 pgm_hash) {
    if (HoistCrossLaneOpsLevel() < 2) {
        return false;
    }
    const char* only = std::getenv("SHADPS4_WAVE64_HOIST2_ONLY");
    return only == nullptr || std::string_view{only}.contains(fmt::format("{:x}", pgm_hash));
}

static bool IsMaskUsedAsData(const IR::Inst& ballot) {
    std::vector<const IR::Inst*> worklist{&ballot};
    std::vector<const IR::Inst*> visited;
    while (!worklist.empty()) {
        const IR::Inst* const inst = worklist.back();
        worklist.pop_back();
        if (std::ranges::contains(visited, inst)) {
            continue;
        }
        visited.push_back(inst);
        for (const IR::Use& use : inst->Uses()) {
            switch (use.user->GetOpcode()) {
            case IR::Opcode::InverseBallot:
            case IR::Opcode::SetScalarRegister:
            case IR::Opcode::SetVectorRegister:
            case IR::Opcode::SetVcc:
            case IR::Opcode::SetVccLo:
            case IR::Opcode::SetVccHi:
            case IR::Opcode::SetM0:
                break;
            case IR::Opcode::UnpackUint2x32:
            case IR::Opcode::PackUint2x32:
            case IR::Opcode::CompositeConstructU32x2:
            case IR::Opcode::CompositeExtractU32x2:
            case IR::Opcode::BitwiseAnd32:
            case IR::Opcode::BitwiseAnd64:
            case IR::Opcode::BitwiseOr32:
            case IR::Opcode::BitwiseOr64:
            case IR::Opcode::BitwiseXor32:
            case IR::Opcode::BitwiseXor64:
            case IR::Opcode::BitwiseNot32:
            case IR::Opcode::BitwiseNot64:
            case IR::Opcode::SelectU32:
            case IR::Opcode::SelectU64:
            case IR::Opcode::Phi:
                worklist.push_back(use.user);
                break;
            default:
                return true;
            }
        }
    }
    return false;
}

static bool NeedsWave64Exchange(const IR::Inst& inst) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::ReadLane:
        return hoist_whole_wave_ops && inst.Arg(1).IsImmediate();
    case IR::Opcode::Ballot:
        return IsMaskUsedAsData(inst);
    default:
        return false;
    }
}

static const IR::Inst* SkipBitCasts(const IR::Value& value) {
    if (value.IsImmediate()) {
        return nullptr;
    }
    const IR::Inst* inst = value.Inst();
    while (inst->GetOpcode() == IR::Opcode::BitCastU32F32 ||
           inst->GetOpcode() == IR::Opcode::BitCastF32U32) {
        if (inst->Arg(0).IsImmediate()) {
            return nullptr;
        }
        inst = inst->Arg(0).Inst();
    }
    return inst;
}

static std::optional<u32> ReductionIdentity(IR::Opcode op) {
    switch (op) {
    case IR::Opcode::FPMax32:
        return 0xff800000u;
    case IR::Opcode::FPMin32:
        return 0x7f800000u;
    case IR::Opcode::FPAdd32:
    case IR::Opcode::IAdd32:
    case IR::Opcode::UMax32:
    case IR::Opcode::BitwiseOr32:
    case IR::Opcode::BitwiseXor32:
        return 0u;
    case IR::Opcode::UMin32:
    case IR::Opcode::BitwiseAnd32:
        return 0xffffffffu;
    case IR::Opcode::SMax32:
        return 0x80000000u;
    case IR::Opcode::SMin32:
        return 0x7fffffffu;
    default:
        return std::nullopt;
    }
}

static std::optional<u32> ImmediateBits(const IR::Value& value) {
    if (!value.IsImmediate()) {
        return std::nullopt;
    }
    switch (value.Type()) {
    case IR::Type::U32:
        return value.U32();
    case IR::Type::F32:
        return std::bit_cast<u32>(value.F32());
    default:
        return std::nullopt;
    }
}

// Strips ConditionRef wrappers and double negations, so `not(not(x))` and `x` compare equal.
static IR::Value StripCondition(IR::Value value) {
    while (!value.IsImmediate()) {
        const IR::Inst* const inst = value.Inst();
        if (inst->GetOpcode() == IR::Opcode::ConditionRef) {
            value = inst->Arg(0);
            continue;
        }
        if (inst->GetOpcode() == IR::Opcode::LogicalNot && !inst->Arg(0).IsImmediate() &&
            inst->Arg(0).Inst()->GetOpcode() == IR::Opcode::LogicalNot) {
            value = inst->Arg(0).Inst()->Arg(0);
            continue;
        }
        break;
    }
    return value;
}

struct WholeWaveReduction {
    std::vector<IR::Inst*> insts;
    IR::Inst* select;
    IR::Inst* result;
};

static bool IsDeadRegisterWrite(const IR::Inst& inst) {
    return inst.GetOpcode() == IR::Opcode::SetVectorRegister ||
           inst.GetOpcode() == IR::Opcode::SetScalarRegister;
}

// A whole-wave-mode reduction as GCN compilers emit it: `v = active ? x : identity`, then
// `v = op(v, swizzle_xor(v, k))` steps. On PS4 the swizzles run on all 64 lanes (S_ORN2_SAVEEXEC turns
// the inactive ones on) and the result lands in every lane; the IR has it inside per-lane Ifs.
static std::optional<WholeWaveReduction> MatchWholeWaveReduction(IR::Inst& select) {
    if (select.GetOpcode() != IR::Opcode::SelectF32 && select.GetOpcode() != IR::Opcode::SelectU32) {
        return std::nullopt;
    }
    const auto identity = ImmediateBits(select.Arg(2));
    if (!identity || select.Arg(1).IsImmediate()) {
        return std::nullopt;
    }
    std::vector<IR::Inst*> members{&select};
    IR::Inst* value = &select;
    u32 steps = 0;
    while (true) {
        IR::Inst* step = nullptr;
        for (const IR::Use& use : value->Uses()) {
            IR::Inst* const user = use.user;
            const auto op_identity = ReductionIdentity(user->GetOpcode());
            if (!op_identity || *op_identity != *identity || user->NumArgs() != 2) {
                continue;
            }
            for (u32 arg = 0; arg < 2; ++arg) {
                const IR::Inst* const other = SkipBitCasts(user->Arg(arg));
                if (SkipBitCasts(user->Arg(1 - arg)) == value && other != nullptr &&
                    other->GetOpcode() == IR::Opcode::ShuffleXor &&
                    SkipBitCasts(other->Arg(0)) == value) {
                    step = user;
                }
            }
        }
        if (step == nullptr) {
            break;
        }
        for (u32 arg = 0; arg < 2; ++arg) {
            IR::Inst* inst = step->Arg(arg).Inst();
            while (inst != value) {
                members.push_back(inst);
                inst = inst->Arg(0).Inst();
            }
        }
        members.push_back(step);
        value = step;
        ++steps;
    }
    while (value->UseCount() == 1 &&
           (value->Uses().begin()->user->GetOpcode() == IR::Opcode::BitCastU32F32 ||
            value->Uses().begin()->user->GetOpcode() == IR::Opcode::BitCastF32U32)) {
        value = value->Uses().begin()->user;
        members.push_back(value);
    }
    if (steps == 0) {
        return std::nullopt;
    }
    IR::Block* const block = select.GetParent();
    const size_t num_values = members.size();
    for (size_t i = 0; i < num_values; ++i) {
        IR::Inst* const member = members[i];
        if (member->GetParent() != block) {
            return std::nullopt;
        }
        for (const IR::Use& use : member->Uses()) {
            if (IsDeadRegisterWrite(*use.user) && use.user->GetParent() == block) {
                if (!std::ranges::contains(members, use.user)) {
                    members.push_back(use.user);
                }
            } else if (member != value && !std::ranges::contains(members, use.user)) {
                return std::nullopt;
            }
        }
    }
    std::vector<IR::Inst*> ordered;
    for (IR::Inst& inst : block->Instructions()) {
        if (std::ranges::contains(members, &inst)) {
            ordered.push_back(&inst);
        }
    }
    if (ordered.size() != members.size()) {
        return std::nullopt;
    }
    return WholeWaveReduction{std::move(ordered), &select, value};
}

static IR::Block* FindIfHeader(const IR::AbstractSyntaxNode& if_node) {
    for (IR::Block* const pred : if_node.data.if_node.merge->ImmPredecessors()) {
        if (std::ranges::contains(pred->ImmSuccessors(), if_node.data.if_node.body)) {
            return pred;
        }
    }
    return nullptr;
}

static void RetargetEdge(IR::Block* from, IR::Block* old_to, IR::Block* new_to) {
    std::ranges::replace(from->imm_successors, old_to, new_to);
    std::erase(old_to->imm_predecessors, from);
    new_to->imm_predecessors.push_back(from);
}

static IR::AbstractSyntaxNode MakeBlockNode(IR::Block* block) {
    IR::AbstractSyntaxNode node{};
    node.type = IR::AbstractSyntaxNode::Type::Block;
    node.data.block = block;
    return node;
}

// Closes every conditional enclosing `moved`, executes `moved` in uniform control flow with inactive
// lanes contributing nothing, then reopens the same conditionals for the rest of their bodies.
// `masked_select` (optional) is a Select in `moved` whose condition must also require the lane to be
// inside all the conditionals; `broadcast` (optional) is a moved value made visible to every lane.
static void HoistOutOfConditionals(IR::AbstractSyntaxList& syntax_list, size_t block_index,
                                   std::span<IR::Inst* const> moved, IR::Inst* masked_select,
                                   IR::Inst* broadcast, std::span<const size_t> if_indices,
                                   std::span<IR::Block* const> headers,
                                   Common::ObjectPool<IR::Block>& block_pool,
                                   Common::ObjectPool<IR::Inst>& inst_pool) {
    using Type = IR::AbstractSyntaxNode::Type;
    const size_t depth = if_indices.size();
    IR::Block* const block = syntax_list[block_index].data.block;
    std::vector<IR::U1> conds;
    std::vector<IR::Block*> merges;
    for (const size_t index : if_indices) {
        conds.push_back(syntax_list[index].data.if_node.cond);
        merges.push_back(syntax_list[index].data.if_node.merge);
    }

    auto& insts = block->Instructions();
    const auto inst_it = IR::Block::InstructionList::s_iterator_to(*moved.back());
    IR::Block* const tail = block_pool.Create(inst_pool);
    tail->Instructions().splice(tail->end(), insts, std::next(inst_it), insts.end());
    for (IR::Inst& moved : tail->Instructions()) {
        moved.SetParent(tail);
    }
    tail->imm_successors = std::move(block->imm_successors);
    block->imm_successors.clear();
    for (IR::Block* const succ : tail->imm_successors) {
        std::ranges::replace(succ->imm_predecessors, block, tail);
    }

    std::vector<IR::Block*> closes(depth);
    for (size_t k = 0; k < depth; ++k) {
        closes[k] = block_pool.Create(inst_pool);
    }
    block->AddBranch(closes[depth - 1]);
    for (size_t k = depth - 1; k > 0; --k) {
        closes[k]->AddBranch(closes[k - 1]);
    }
    for (size_t k = 0; k < depth; ++k) {
        RetargetEdge(headers[k], merges[k], closes[k]);
        syntax_list[if_indices[k]].data.if_node.merge = closes[k];
    }

    IR::Block* const hoisted = closes[0];
    for (IR::Inst* const inst : moved) {
        insts.erase(IR::Block::InstructionList::s_iterator_to(*inst));
    }
    {
        IR::IREmitter ir{*hoisted};
        IR::U1 active = conds[0];
        for (size_t k = 1; k < depth; ++k) {
            active = IR::U1{ir.Select(active, conds[k], ir.Imm1(false))};
        }
        if (moved.size() == 1 && moved[0]->GetOpcode() == IR::Opcode::Ballot) {
            moved[0]->SetArg(0, ir.Select(active, IR::U1{moved[0]->Arg(0)}, ir.Imm1(false)));
        }
        if (masked_select != nullptr) {
            masked_select->SetArg(
                0, ir.Select(active, IR::U1{masked_select->Arg(0)}, ir.Imm1(false)));
        }
    }
    for (IR::Inst* const inst : moved) {
        inst->SetParent(hoisted);
        hoisted->Instructions().push_back(*inst);
    }
    if (broadcast != nullptr) {
        IR::Inst* const result = broadcast;
        std::vector<IR::VirtualReg> regs;
        for (const IR::Use& use : result->Uses()) {
            if (use.user->GetOpcode() == IR::Opcode::SetVirtualRegister) {
                regs.push_back(use.user->Arg(0).VirtualReg());
            }
        }
        IR::IREmitter ir{*hoisted};
        for (const IR::VirtualReg reg : regs) {
            ir.SetVirtualReg(reg, IR::Value{result});
        }
    }

    std::vector<IR::AbstractSyntaxNode> nodes;
    for (size_t k = depth; k-- > 0;) {
        IR::AbstractSyntaxNode end_if{};
        end_if.type = Type::EndIf;
        end_if.data.end_if.merge = closes[k];
        nodes.push_back(end_if);
        nodes.push_back(MakeBlockNode(closes[k]));
    }
    IR::Block* opener = hoisted;
    for (size_t k = 0; k < depth; ++k) {
        IR::Block* const body = k + 1 < depth ? block_pool.Create(inst_pool) : tail;
        IR::IREmitter ir{*opener};
        IR::AbstractSyntaxNode if_node{};
        if_node.type = Type::If;
        if_node.data.if_node.cond = ir.ConditionRef(conds[k]);
        if_node.data.if_node.body = body;
        if_node.data.if_node.merge = merges[k];
        opener->AddBranch(body);
        opener->AddBranch(merges[k]);
        nodes.push_back(if_node);
        nodes.push_back(MakeBlockNode(body));
        opener = body;
    }
    syntax_list.insert(syntax_list.begin() + block_index + 1, nodes.begin(), nodes.end());
}

void HoistWave64CrossLaneOpsPass(IR::Program& program, Common::ObjectPool<IR::Block>& block_pool,
                                 Common::ObjectPool<IR::Inst>& inst_pool,
                                 const RuntimeInfo& runtime_info, const Profile& profile) {
    using Type = IR::AbstractSyntaxNode::Type;
    if (HoistCrossLaneOpsLevel() == 0 || program.info.hw_stage != HwStage::Compute ||
        profile.subgroup_size == 64) {
        return;
    }
    const auto [size_x, size_y, size_z] = runtime_info.hw.cs.workgroup_size;
    if (size_x * size_y * size_z <= 32) {
        return;
    }
    if (const char* only = std::getenv("SHADPS4_WAVE64_HOIST_ONLY");
        only != nullptr && !std::string_view{only}.contains(fmt::format("{:x}", program.info.pgm_hash))) {
        return;
    }
    hoist_whole_wave_ops = HoistWholeWaveOps(program.info.pgm_hash);

    auto& syntax_list = program.syntax_list;
    const auto try_hoist = [&] {
        std::vector<size_t> if_indices;
        u32 loop_depth{};
        for (size_t index = 0; index < syntax_list.size(); ++index) {
            const IR::AbstractSyntaxNode& node = syntax_list[index];
            switch (node.type) {
            case Type::If:
                if_indices.push_back(index);
                break;
            case Type::EndIf:
                if_indices.pop_back();
                break;
            case Type::Loop:
                ++loop_depth;
                break;
            case Type::Repeat:
                --loop_depth;
                break;
            case Type::Block: {
                if (if_indices.empty() || loop_depth != 0) {
                    break;
                }
                auto& insts = node.data.block->Instructions();
                std::optional<WholeWaveReduction> reduction;
                if (hoist_whole_wave_ops) {
                    const IR::Value innermost =
                        StripCondition(syntax_list[if_indices.back()].data.if_node.cond);
                    for (IR::Inst& inst : insts) {
                        auto match = MatchWholeWaveReduction(inst);
                        if (match && !innermost.IsImmediate() &&
                            StripCondition(match->select->Arg(0)) == innermost) {
                            reduction = std::move(match);
                            break;
                        }
                    }
                }
                const auto it = std::ranges::find_if(insts, NeedsWave64Exchange);
                if (!reduction && it == insts.end()) {
                    break;
                }
                std::vector<IR::Block*> headers;
                for (const size_t if_index : if_indices) {
                    IR::Block* const header = FindIfHeader(syntax_list[if_index]);
                    if (header == nullptr) {
                        return false;
                    }
                    headers.push_back(header);
                }
                if (reduction) {
                    LOG_INFO(Render_Recompiler,
                             "Hoisting a {}-instruction whole-wave reduction out of {} conditionals "
                             "for wave64 (shader {:#x})",
                             reduction->insts.size(), if_indices.size(), program.info.pgm_hash);
                    HoistOutOfConditionals(syntax_list, index, reduction->insts, reduction->select,
                                           reduction->result, if_indices, headers, block_pool,
                                           inst_pool);
                    return true;
                }
                LOG_INFO(Render_Recompiler,
                         "Hoisting {} out of {} conditionals for wave64 (shader {:#x})",
                         it->GetOpcode(), if_indices.size(), program.info.pgm_hash);
                IR::Inst* const single[]{&*it};
                HoistOutOfConditionals(syntax_list, index, single, nullptr, nullptr, if_indices,
                                       headers, block_pool, inst_pool);
                return true;
            }
            default:
                break;
            }
        }
        return false;
    };
    for (u32 iteration = 0; iteration < 256 && try_hoist(); ++iteration) {
    }
}

void LowerWave64BallotPass(IR::Program& program, const RuntimeInfo& runtime_info,
                           const Profile& profile) {
    if (program.info.hw_stage != HwStage::Compute || profile.subgroup_size == 64) {
        return;
    }

    const auto [size_x, size_y, size_z] = runtime_info.hw.cs.workgroup_size;
    const u32 num_threads = size_x * size_y * size_z;
    if (num_threads <= 32) {
        return;
    }

    std::vector<IR::Inst*> worklist;
    const auto uniform_blocks = FindUniformBlocks(program);
    for (IR::Block* block : program.blocks) {
        const bool is_uniform = std::ranges::contains(uniform_blocks, block);
        const auto push_worklist = [&](IR::Inst& inst) {
            if (is_uniform) {
                worklist.push_back(&inst);
            } else if (ForceNonUniformLowering()) {
                LOG_WARNING(Render_Recompiler,
                            "Lowering {} instruction in non uniform control flow (shader {:#x})",
                            inst.GetOpcode(), program.info.pgm_hash);
                worklist.push_back(&inst);
            } else if (HalfWaveReadLane() && inst.GetOpcode() == IR::Opcode::ReadLane &&
                       inst.Arg(1).U32() >= 32) {
                inst.SetArg(1, IR::Value{inst.Arg(1).U32() & 31});
            } else {
                LOG_WARNING(Render_Recompiler,
                            "{} instruction in non uniform control flow (shader {:#x})",
                            inst.GetOpcode(), program.info.pgm_hash);
            }
        };
        for (IR::Inst& inst : block->Instructions()) {
            if (inst.GetOpcode() == IR::Opcode::ReadLane && inst.Arg(1).IsImmediate()) {
                push_worklist(inst);
            } else if (inst.GetOpcode() == IR::Opcode::Ballot) {
                const auto is_unpack = [](const IR::Use& use) {
                    return use.user->GetOpcode() == IR::Opcode::UnpackUint2x32;
                };
                if (HoistCrossLaneOpsLevel() > 0 ? IsMaskUsedAsData(inst)
                                                 : std::ranges::any_of(inst.Uses(), is_unpack)) {
                    push_worklist(inst);
                }
            } else if (inst.GetOpcode() == IR::Opcode::MaskedBitCount32) {
                IR::Inst* const ballot = FindBallotForMaskedBitCount(inst);
                if (ballot == nullptr || ForceNonUniformLowering() ||
                    std::ranges::contains(uniform_blocks, ballot->GetParent())) {
                    worklist.push_back(&inst);
                }
            }
        }
    }
    if (worklist.empty()) {
        return;
    }

    const u32 scratch_base = Common::AlignUp(runtime_info.hw.cs.shared_memory_size, sizeof(u64));
    const u32 scratch_size =
        (Common::AlignUp(num_threads, 64) / profile.subgroup_size) * sizeof(u32);
    program.info.shared_memory_scratch_size =
        scratch_base + scratch_size - runtime_info.hw.cs.shared_memory_size;

    for (IR::Inst* inst : worklist) {
        LOG_INFO(Render_Recompiler, "Lowering {} instruction for wave64 (shader {:#x})",
                 inst->GetOpcode(), program.info.pgm_hash);
        IR::IREmitter ir{*inst->GetParent(), IR::Block::InstructionList::s_iterator_to(*inst)};
        const IR::U32 invocation_index = ir.GetAttributeU32(IR::Attribute::LocalInvocationIndex);
        const IR::U32 subgroup_id = ir.ShiftRightLogical(invocation_index, ir.Imm32(5));
        if (inst->GetOpcode() == IR::Opcode::Ballot) {
            const IR::U32 mask_low =
                IR::U32{ir.CompositeExtract(ir.UnpackUint2x32(ir.Ballot(IR::U1{inst->Arg(0)})), 0)};
            const IR::U32 offset =
                ir.IAdd(ir.Imm32(scratch_base), ir.ShiftLeftLogical(subgroup_id, ir.Imm32(2u)));
            ir.WriteShared(32, mask_low, offset);
            ir.Barrier();
            const IR::U64 mask = IR::U64{ir.LoadShared(64, false, offset)};
            ir.Barrier();
            inst->ReplaceUsesWithAndRemove(mask);
        } else if (inst->GetOpcode() == IR::Opcode::ReadLane) {
            const IR::U32 lane32 = ir.BitwiseAnd(IR::U32{inst->Arg(1)}, ir.Imm32(31));
            const IR::U32 half = ir.ShiftRightLogical(IR::U32{inst->Arg(1)}, ir.Imm32(5u));
            const IR::U32 offset =
                ir.IAdd(ir.Imm32(scratch_base), ir.ShiftLeftLogical(subgroup_id, ir.Imm32(2u)));
            ir.WriteShared(32, ir.ReadLane(IR::U32{inst->Arg(0)}, lane32), offset);
            ir.Barrier();
            const IR::U32 value =
                IR::U32{ir.LoadShared(32, false,
                                      ir.IAdd(ir.BitwiseAnd(offset, ir.Imm32(~7u)),
                                              ir.ShiftLeftLogical(half, ir.Imm32(2u))))};
            ir.Barrier();
            inst->ReplaceUsesWithAndRemove(value);
        } else if (inst->GetOpcode() == IR::Opcode::MaskedBitCount32) {
            const IR::U32 subgroup_invocation_id = ir.BitwiseAnd(invocation_index, ir.Imm32(63));
            const IR::U64 mask = ir.ISub(
                ir.ShiftLeftLogical(ir.Imm64(u64{1}), subgroup_invocation_id), ir.Imm64(u64{1}));
            const IR::U32 thread_mask{
                ir.CompositeExtract(ir.UnpackUint2x32(mask), inst->Arg(2).U1() ? 1u : 0u)};
            const IR::U32 masked_value{
                ir.BitCount(ir.BitwiseAnd(IR::U32{inst->Arg(0)}, thread_mask))};
            inst->ReplaceUsesWithAndRemove(ir.IAdd(masked_value, IR::U32{inst->Arg(1)}));
        }
    }
}

} // namespace Shader::Optimization
