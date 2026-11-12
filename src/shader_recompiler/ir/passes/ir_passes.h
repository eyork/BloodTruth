// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>

#include "shader_recompiler/ir/basic_block.h"
#include "shader_recompiler/ir/passes/resource_pass.h"
#include "shader_recompiler/ir/program.h"

namespace Shader {
struct Profile;
void InjectClipDistanceAttributes(IR::Program& program, RuntimeInfo& runtime_info);
} // namespace Shader

namespace Shader::IR {
class IREmitter;
}

namespace Shader::Optimization {

// PS4Recomp: the VGPR a cross-lane read reads, recorded by the translator in the instruction's
// flags (index + 1; 0: not recorded). A lane outside EXEC keeps that register's old value.
inline void SetLaneSourceVgpr(const IR::Value& read, IR::VectorReg vgpr) {
    read.Inst()->SetFlags<u32>(static_cast<u32>(vgpr) + 1);
}
inline std::optional<IR::VectorReg> LaneSourceVgpr(const IR::Inst& read) {
    const u32 flags = read.Flags<u32>();
    return flags == 0 ? std::nullopt : std::optional{static_cast<IR::VectorReg>(flags - 1)};
}

void PredicateWave64Pass(IR::Program& program, const RuntimeInfo& runtime_info,
                         const Profile& profile);
void LowerWaveOpsPass(IR::Program& program, const RuntimeInfo& runtime_info,
                      const Profile& profile);
IR::U32 ComputeWaveLane(IR::IREmitter& ir);

void SsaRewritePass(IR::Program& program);
void SsaRepairPass(IR::Program& program);
void PhiSimplificationPass(IR::Program& program);
void InverseBallotEliminationPass(IR::Program& program);
void HoistWave64CrossLaneOpsPass(IR::Program& program, Common::ObjectPool<IR::Block>& block_pool,
                                 Common::ObjectPool<IR::Inst>& inst_pool,
                                 const RuntimeInfo& runtime_info, const Profile& profile);
void LowerWave64BallotPass(IR::Program& program, const RuntimeInfo& runtime_info,
                           const Profile& profile);
void LowerHardwareIntrinsics(IR::Program& program);
void LowerPhisToRegsPass(IR::Program& program);
void DeadCodeEliminationPass(IR::Program& program);
void ConstantPropagationPass(IR::BlockList& program);
void FlattenExtendedUserdataPass(IR::Program& program);
void ReadLaneEliminationPass(IR::Program& program);
ResourceDiscoveryList ResourceDiscoverPass(IR::Program& program, const Profile& profile);
void ResourcePatchingPass(Shader::Info& info, const ResourceDiscoveryList& sharp_usages,
                          const Profile& profile);
void CollectShaderInfoPass(IR::Program& program, const Profile& profile);
void LowerBufferFormatToRaw(IR::Program& program);
void LowerUserClipPlanes(IR::Program& program, const RuntimeInfo& runtime_info);
void LowerFp64ToFp32(IR::Program& program);
void RingAccessElimination(const IR::Program& program, const RuntimeInfo& runtime_info);
void TessellationPreprocess(IR::Program& program, RuntimeInfo& runtime_info);
void HullShaderTransform(IR::Program& program, const RuntimeInfo& runtime_info);
void DomainShaderTransform(const IR::Program& program, const RuntimeInfo& runtime_info);
void SharedMemoryBarrierPass(IR::Program& program, const RuntimeInfo& runtime_info,
                             const Profile& profile);
void SharedMemorySimplifyPass(IR::Program& program, const Profile& profile);
void SharedMemoryToStoragePass(IR::Program& program, const RuntimeInfo& runtime_info,
                               const Profile& profile);

} // namespace Shader::Optimization
