// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The benchmarks' recorded baseline (bench/baseline.txt): each row's
// figure on a named machine, printed beside today's with their ratio,
// and its budget, what a host's frame or block allows the part, which
// today's figure is checked against.
// A record, never a gate: wall time on another machine, or a loaded
// one, is not comparable (requirements, section 5; conventions, 14).

#ifndef MAUL_AUDIO_BENCH_BASELINE_H
#define MAUL_AUDIO_BENCH_BASELINE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A row's baseline figure and its budget (each 0 when the row has none):
// "key figure [budget]".
static void RowOf(const char* key, double* figureOut, double* budgetOut)
{
    *figureOut = 0.0;
    *budgetOut = 0.0;
    FILE* file = fopen(MAUD_BENCH_DIR "/baseline.txt", "r");
    if (file == nullptr)
    {
        return;
    }
    char line[256];
    size_t length = strlen(key);
    while (fgets(line, sizeof(line), file) != nullptr)
    {
        if (strncmp(line, key, length) == 0 && line[length] == ' ')
        {
            char* end = nullptr;
            *figureOut = strtod(line + length + 1, &end);
            *budgetOut = strtod(end, nullptr);
            break;
        }
    }
    fclose(file);
}

// Prints a row's key and figure, for the baseline file, today's figure
// against the baseline's, and whether it is within the row's budget:
// lower is better for times, higher for rates, whose budget is a floor.
static void Against(const char* key, double today, bool higherIsBetter)
{
    double base = 0.0;
    double budget = 0.0;
    RowOf(key, &base, &budget);
    if (base <= 0.0)
    {
        printf("    %s %.4g (no baseline)\n", key, today);
    }
    else
    {
        double speed = higherIsBetter ? today / base : base / today;
        printf("    %s %.4g, baseline %.4g: %.2fx the baseline's speed\n", key, today, base, speed);
    }
    if (budget > 0.0)
    {
        bool within = higherIsBetter ? today >= budget : today <= budget;
        printf("    %s %s its budget of %.4g\n", key, within ? "within" : "PAST", budget);
    }
}

#endif // MAUL_AUDIO_BENCH_BASELINE_H
