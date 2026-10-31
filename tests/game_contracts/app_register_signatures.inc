// SPDX-License-Identifier: GPL-3.0-only
// Game-only test cases; included by the corresponding test when the game pack is enabled.
static sco::SigRegistrationResult RegisterForTest(sco::SignatureRegistry& registry) { ++g_registerCalls; return sco::game::RegisterGameSignatures(registry); }
