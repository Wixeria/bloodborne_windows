/* SPDX-License-Identifier: GPL-2.0-or-later */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

/* A harmless native child: checks argv, both streams and restart exit codes. */
int wmain(int argc, wchar_t **argv) {
    wchar_t state[32768];
    if (!GetEnvironmentVariableW(L"BB_RUNNER_TEST_STATE", state, 32768)) return 99;
    int attempt = 0;
    FILE *file = _wfopen(state, L"r");
    if (file) { (void)fscanf(file, "%d", &attempt); fclose(file); }
    file = _wfopen(state, L"w");
    if (!file) return 98;
    fprintf(file, "%d", ++attempt);
    fclose(file);
    fprintf(stderr, "native stderr attempt %d\n", attempt);
    printf("attempt=%d\n", attempt);
    for (int i = 1; i < argc; ++i) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, NULL, 0, NULL, NULL);
        char *text = malloc((size_t)bytes);
        if (!text) return 97;
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, text, bytes, NULL, NULL);
        printf("arg=");
        for (int n = 0; n < bytes - 1; ++n) printf("%02x", (unsigned char)text[n]);
        printf("\n");
        free(text);
    }
    return attempt == 1 ? 75 : 23;
}
