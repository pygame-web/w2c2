#include "../w2c2/w2c2_base.h"
#include "wasi.h"
#include <stdio.h>
#include <limits.h>
#include "mac.h"

extern char** environ;

extern
bool
resolvePath(
    char* directory,
    char* path,
    U32 pathLength,
    char result[PATH_MAX]
);

void
testResolvePath(
    char* directory,
    char* path,
    char* expected,
    bool valid
) {
    char result[PATH_MAX];
    if (resolvePath(directory, path, strlen(path), result) != valid) {
        fprintf(stderr, "FAIL: resolvePath(%s, %s): should succeed\n", directory, path);
        return;
    }

    if (!valid) {
        return;
    }

    if (strcmp(result, expected) != 0) {
        fprintf(stderr, "FAIL: resolvePath(%s, %s): %s != %s\n", directory, path, result, expected);
        return;
    }

    fprintf(stderr, "OK: resolvePath(%s, %s) == %s\n", directory, path, expected);
}

void
testMacToPosixPath(
    char* path,
    char* expected
) {
    char* result = NULL;
    char tmp[PATH_MAX];
    strcpy(tmp, path);

    result = macToPosixPath(tmp);

    if (strcmp(result, expected) != 0) {
        fprintf(stderr, "FAIL: macToPosixPath(%s): %s != %s\n", path, result, expected);
        return;
    }

    fprintf(stderr, "OK: macToPosixPath(%s) == %s\n", path, expected);
}

void
testPosixToMacPath(
    char* path,
    char* expected
) {
    char* result = NULL;
    char tmp[PATH_MAX];
    strcpy(tmp, path);

    result = posixToMacPath(tmp);

    if (strcmp(result, expected) != 0) {
        fprintf(stderr, "FAIL: posixToMacPath(%s): %s != %s\n", path, result, expected);
        return;
    }

    fprintf(stderr, "OK: posixToMacPath(%s) == %s\n", path, expected);
}

/* Unused but expected by the WASI implementation */
wasmMemory* wasiMemory(void* instance) {
    return NULL;
}

int
main(int argc, char* argv[]) {
    wasmMemory m;
    wasmMemoryAllocate(&m, 2, 65535);
    if (!wasiInit(argc, argv, environ)) {
        fprintf(stderr, "failed to initialize WASI\n");
        return 1;
    }

    testResolvePath("/", "", "", false);
    testResolvePath("/", "/bar", "/bar", true);
    testResolvePath("/", "bar", "/bar", true);
    testResolvePath("/foo", "", "", false);
    testResolvePath("/foo", "/bar", "/bar", true);
    testResolvePath("/foo", "bar", "/foo/bar", true);
    testResolvePath("/foo/bar", "", "", false);
    testResolvePath("/foo/bar", "/baz/qux", "/baz/qux", true);
    testResolvePath("/foo/bar", "baz/qux", "/foo/bar/baz/qux", true);

    testPosixToMacPath(
        "../../Volume/../../../foo/bar/../../more/yes/../last/..",
        ":::Volume::::foo:bar:::more:yes::last::"
    );
    testPosixToMacPath(
        "Volume/../../../foo/bar/../../more/yes/../last/..",
        ":Volume::::foo:bar:::more:yes::last::"
    );
    testPosixToMacPath(
        "./Volume/../../../foo/bar/../../more/yes/../last/..",
        ":Volume::::foo:bar:::more:yes::last::"
    );
    testPosixToMacPath(
        "/Volume/../../../foo/bar/../../more/yes/../last/../..",
        "Volume::::foo:bar:::more:yes::last:::"
    );

    testMacToPosixPath(
        ":Volume::::foo:bar:::more:yes::last::",
        "./Volume/../../../foo/bar/../../more/yes/../last/../"
    );
    testMacToPosixPath(
        "::Volume::::foo:bar:::more:yes::last::",
        "./../Volume/../../../foo/bar/../../more/yes/../last/../"
    );
    testMacToPosixPath(
        ":::Volume::::foo:bar:::more:yes::last::",
        "./../../Volume/../../../foo/bar/../../more/yes/../last/../"
    );
    testMacToPosixPath(
        "Volume::::foo:bar:::more:yes::last:::",
        "/Volume/../../../foo/bar/../../more/yes/../last/../../"
    );

    return 0;
}
