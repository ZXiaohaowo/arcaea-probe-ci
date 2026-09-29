/* practice_bootstrap.c - S1 minimal load-verification dylib.
 *
 * Purpose: prove that a self-built module can be injected into the repackaged
 * IPA and loaded by the sandboxed game process on a non-jailbroken device.
 * This module intentionally does NOT touch game code, timing, audio or UI.
 *
 * Runtime behavior: appends one line to <sandbox>/Documents/practice_bootstrap.log
 * from the dyld constructor, with a fixed version tag and a Unix timestamp.
 * The same tag is exported as a symbol for offline inspection (nm/grep).
 *
 * Safety rules honored here:
 * - No allocation beyond stack buffers, no UIKit, no networking.
 * - Any failure path is silent; this module must never abort the host app.
 */
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <time.h>

#ifndef PRACTICE_BUILD_ID
#define PRACTICE_BUILD_ID "dev"
#endif
#define PRACTICE_BOOTSTRAP_VERSION "s1-minimal-" PRACTICE_BUILD_ID

__attribute__((visibility("default")))
const char practice_bootstrap_version[] = PRACTICE_BOOTSTRAP_VERSION;

static void write_marker(const char *base)
{
    char path[1024];
    FILE *fp;

    if (base == NULL || base[0] == '\0') {
        return;
    }
    if (snprintf(path, sizeof(path), "%s/Documents/practice_bootstrap.log", base) <= 0) {
        return;
    }
    fp = fopen(path, "a");
    if (fp == NULL) {
        return;
    }
    fprintf(fp, "[practice-bootstrap] loaded version=%s ts=%ld\n",
            PRACTICE_BOOTSTRAP_VERSION, (long)time(NULL));
    fclose(fp);
}

__attribute__((constructor))
static void practice_bootstrap_ctor(void)
{
    const char *home = getenv("HOME");

    write_marker(home ? home : "/tmp");
    syslog(LOG_ERR, "[practice-bootstrap] loaded version=%s",
           PRACTICE_BOOTSTRAP_VERSION);
}
