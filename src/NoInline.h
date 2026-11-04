// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#if defined(_MSC_VER)
#define VIBERA_NOINLINE __declspec(noinline)
#else
#define VIBERA_NOINLINE __attribute__((noinline))
#endif
