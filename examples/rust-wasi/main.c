#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "../../w2c2_base.h"
#include "../../wasi/wasi.h"
#include "target/wasm32-wasi/debug/rustwasi.h"

void
trap(
    Trap trap
) {
    fprintf(stderr, "TRAP: %s\n", trapDescription(trap));
    abort();
}

wasmMemory*
wasiMemory(
    void* instance
) {
    return rustwasi_memory((rustwasiInstance*)instance);
}

extern char** environ;

/* Main */

int main(int argc, char* argv[]) {
    /* Initialize WASI */
    if (!wasiInit(argc, argv, environ)) {
        fprintf(stderr, "failed to init WASI\n");
        return 1;
    }

    {
        static char* tmpPath = "/tmp";
        int rootFD = open(tmpPath, O_DIRECTORY);
        if (rootFD < 0) {
            fprintf(stderr, "failed to open root path\n");
            return 1;
        }
        {
            WasiPreopen preopen = {tmpPath, rootFD};
            if (!wasiPreopenAdd(preopen, NULL)) {
                fprintf(stderr, "failed to add preopen\n");
                close(rootFD);
                return 1;
            }
        }
    }

    {
        rustwasiInstance instance;
        rustwasiInstantiate(&instance, wasiResolveImport);
        rustwasi_X5Fstart(&instance);
        rustwasiFreeInstance(&instance);
    }

    return 0;
}
