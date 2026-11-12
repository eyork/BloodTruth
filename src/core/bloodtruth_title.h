// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "common/types.h"

namespace Core::BloodTruth {

void OnEbootLoaded(VAddr base, u64 size);
void Tick();

} // namespace Core::BloodTruth
