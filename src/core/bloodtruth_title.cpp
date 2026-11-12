// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>

#include "common/elf_info.h"
#include "common/logging/log.h"
#include "core/bloodtruth_title.h"

namespace Core::BloodTruth {

namespace {

std::atomic<VAddr> g_base{0};

template <typename T>
T Read(VAddr address) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

template <typename T>
void Write(VAddr address, T value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(T));
}

// The eye size is clamped to a highest multiple of 960x1080 taken from a pair of floats in the
// image (eboot 0x13f043d..0x13f0460): 1.5 on PS4 Pro, 1.2 on a base PS4. Render-scale settings
// above that ceiling have no effect. SHADPS4_BT_VR_SCALE=<1.0..3.0> sets both entries.
void PatchVrScaleCeiling(VAddr base, u64 size) {
    constexpr u64 ScaleTable = 0x226ef48;
    constexpr std::array<float, 2> Console{1.5f, 1.2f};
    static const float scale = [] {
        const char* value = std::getenv("SHADPS4_BT_VR_SCALE");
        const float wanted = value != nullptr ? static_cast<float>(std::atof(value)) : 0.0f;
        return wanted >= 1.0f && wanted <= 3.0f ? wanted : 0.0f;
    }();
    if (ScaleTable + sizeof(Console) > size) {
        return;
    }
    const auto table = Read<std::array<float, 2>>(base + ScaleTable);
    if (table != Console) {
        LOG_WARNING(Core, "CUSA11108: VR scale table holds {} and {}, expected 1.5 and 1.2; "
                          "left unchanged",
                    table[0], table[1]);
        return;
    }
    if (scale == 0.0f) {
        LOG_INFO(Core, "CUSA11108: VR eye scale ceiling {:.2f} (Pro) / {:.2f} (base)", table[0],
                 table[1]);
        return;
    }
    Write(base + ScaleTable, std::array<float, 2>{scale, scale});
    LOG_INFO(Core, "CUSA11108: VR eye scale ceiling {:.2f}, up to {}x{} per eye", scale,
             static_cast<int>(960 * scale), static_cast<int>(1080 * scale));
}

// The RenderConfig byte "Enable GPU culling" (eboot 0x2cf5af6). With it set, the DrawCulling
// compute results hide terrain, sky and many props; cleared, the game culls on the CPU.
// SHADPS4_BT_NO_GPU_CULLING=0 keeps the game's GPU culling.
void DisableGpuCulling(VAddr base) {
    static const bool enabled = [] {
        const char* value = std::getenv("SHADPS4_BT_NO_GPU_CULLING");
        return value == nullptr || value[0] != '0';
    }();
    if (!enabled) {
        return;
    }
    constexpr u64 EnableGpuCulling = 0x2cf5af6;
    if (Read<u8>(base + EnableGpuCulling) == 0) {
        return;
    }
    Write<u8>(base + EnableGpuCulling, 0);
    static std::atomic<u32> logged{0};
    if (logged.fetch_add(1, std::memory_order_relaxed) < 4) {
        LOG_INFO(Core, "CUSA11108: GPU culling turned off, the game culls on the CPU");
    }
}

} // namespace

void OnEbootLoaded(VAddr base, u64 size) {
    if (Common::ElfInfo::Instance().GameSerial() != "CUSA11108") {
        return;
    }
    g_base.store(base, std::memory_order_release);
    PatchVrScaleCeiling(base, size);
}

void Tick() {
    const VAddr base = g_base.load(std::memory_order_acquire);
    if (base == 0) {
        return;
    }
    DisableGpuCulling(base);
}

} // namespace Core::BloodTruth
