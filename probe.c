/* probe.c - minimal module for the standard-toolchain control build (CI).
 *
 * Built by Xcode/clang on a macOS runner (see .github/workflows). Purpose:
 * produce a framework produced by the STANDARD Apple toolchain for the same
 * injection/install flow, to separate "our zig build is special" from
 * "the pipeline or platform rejects new components".
 *
 * Kept deliberately minimal: one constructor, one small file append, no
 * game interaction, no threads, no Objective-C.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

__attribute__((visibility("default")))
const char practice_ci_probe_version[] = "ci-probe-2026-09-29";

__attribute__((constructor))
static void ci_probe_ctor(void)
{
    const char *home = getenv("HOME");
    char path[1024];
    FILE *fp;

    if (home == NULL) {
        return;
    }
    if (snprintf(path, sizeof(path), "%s/Documents/practice_ci_probe.log", home) <= 0) {
        return;
    }
    fp = fopen(path, "a");
    if (fp == NULL) {
        return;
    }
    fprintf(fp, "[practice-ci-probe] loaded version=%s ts=%ld\n",
            practice_ci_probe_version, (long)time(NULL));
    fclose(fp);
}
