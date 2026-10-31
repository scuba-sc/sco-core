// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "sco/game/services.h"
#include <cstdlib>
#include <thread>
int main() {
    if (sco::game::services::Start() != sco::Result::Unavailable) std::abort();
    std::thread other([] {
        if (sco::game::services::Start() != sco::Result::Unavailable) std::abort();
    });
    other.join();
}
