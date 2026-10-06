// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The benchmarks' recorded baseline (bench/baseline.txt): each row's
// figure on a named machine, printed beside today's with their ratio.
// A record, never a gate: wall time on another machine, or a loaded
// one, is not comparable (requirements, section 5; conventions, 14).

#ifndef MAUL_AUDIO_BENCH_BASELINE_H
#define MAUL_AUDIO_BENCH_BASELINE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The baseline's figure for a row, or 0 when it has none.
static double BaselineOf(const char* key)
{
    FILE* file = fopen(MAUD_BENCH_DIR "/baseline.txt", "r");
    if (file == nullptr)
    {
        return 0.0;
    }
    char line[256];
    double value = 0.0;
    size_t length = strlen(key);
    while (fgets(line, sizeof(line), file) != nullptr)
    {
        if (strncmp(line, key, length) == 0 && line[length] == ' ')
        {
            value = strtod(line + length + 1, nullptr);
            break;
        }
    }
    fclose(file);
    return value;
}

// Prints a row's key and figure, for the baseline file, and today's
// figure against the baseline's: lower is better for times, higher for
// rates.
static void Against(const char* key, double today, bool higherIsBetter)
{
    double base = BaselineOf(key);
    if (base <= 0.0)
    {
        printf("    %s %.4g (no baseline)\n", key, today);
        return;
    }
    double speed = higherIsBetter ? today / base : base / today;
    printf("    %s %.4g, baseline %.4g: %.2fx the baseline's speed\n", key, today, base, speed);
}

#endif // MAUL_AUDIO_BENCH_BASELINE_H
