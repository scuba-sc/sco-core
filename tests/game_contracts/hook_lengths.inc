// SPDX-License-Identifier: GPL-3.0-only
    // CSystem::Quit: mov [rsp+10h], rbx ; mov [rsp+18h], rsi ; mov [rsp+20h], rdi ; push rbp
    static const uint8_t kQuit[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
                                     0x48, 0x89, 0x7C, 0x24, 0x20, 0x55 };
    CHECK(H::StolenLength(kQuit, 5) == 5);
    CHECK(H::StolenLength(kQuit, 14) == 15);
    CHECK(H::StolenLength(kQuit, 16) == 16);
