#include <stdio.h>
#include <ctype.h>
#include <libgen.h>
#include <limits.h>

#ifdef HAS_GETOPT
  #include <getopt.h>
#else
  #include "getopt_impl.h"
#endif /* HAS_GETOPT */

#include "buffer.h"
#include "file.h"
#include "reader.h"
#include "c.h"
#include "stringbuilder.h"

#if HAS_PTHREAD
static char* const optString = "j:f:d:pgh";
#else
static char* const optString = "f:d:pgh";
#endif /* HAS_PTHREAD */

static
bool
readWasmBinary(
    const char* path,
    WasmModuleReader* wasmModuleReaderResult,
    bool debug
) {
    WasmModuleReaderError* error = NULL;
    Buffer buffer = readFile(path);
    if (buffer.data == NULL) {
        fprintf(stderr, "w2c2: failed to read file %s\n", path);
        return false;
    }

    wasmModuleReaderResult->buffer = buffer;

    wasmModuleRead(wasmModuleReaderResult, debug, &error);
    if (error != NULL) {
        fprintf(stderr, "w2c2: failed to read module: %s\n", wasmModuleReaderErrorMessage(error));
        return false;
    }

    return true;
}

static
void
getPathModuleName(
    char* moduleName,
    char* modulePath
) {
    int j = 0;
    size_t ext;
    size_t i = strlen(modulePath)-1;
    while (i && modulePath[i] == '/') i--;
    while (i && modulePath[i] != '.' && modulePath[i] != '/') i--;
    ext = i;
    while (i && modulePath[i-1] != '/') i--;

    for (; i < ext; i++) {
        if (!isalnum(modulePath[i])) {
            continue;
        }
        moduleName[j++] = modulePath[i];
    }
    moduleName[j] = '\0';
}

int
main(
    int argc,
    char* argv[]
) {
    U32 jobCount = 1;
    char* modulePath = NULL;
    char* outputPath = NULL;
    U32 functionsPerFile = 10;
    bool pretty = false;
    bool debug = false;
    WasmDataSegmentMode dataSegmentMode = wasmDataSegmentModeArrays;
    char moduleName[PATH_MAX];

    int index = 0;
    int c = -1;

    opterr = 0;

    while ((c = getopt(argc, argv, optString)) != -1) {
        switch (c) {
#if HAS_PTHREAD
            case 'j': {
                jobCount = strtoul(optarg, NULL, 0);
                break;
            }
#endif /* HAS_PTHREAD */
            case 'f': {
                functionsPerFile = strtoul(optarg, NULL, 0);
                break;
            }
            case 'p': {
                pretty = true;
                break;
            }
            case 'g': {
                debug = true;
                break;
            }
            case 'd': {
                if (strcmp(optarg, "arrays") == 0) {
                    dataSegmentMode = wasmDataSegmentModeArrays;
                } else if (strcmp(optarg, "gnu-ld") == 0) {
                    dataSegmentMode = wasmDataSegmentModeGNULD;
                } else if (strcmp(optarg, "sectcreate1") == 0) {
                    dataSegmentMode = wasmDataSegmentModeSectcreate1;
                } else if (strcmp(optarg, "sectcreate2") == 0) {
                    dataSegmentMode = wasmDataSegmentModeSectcreate2;
                } else if (strcmp(optarg, "help") == 0) {
                    fprintf(
                        stderr,
                        "Supported data segment modes are:\n"
                        "arrays         Writes each data segment as a C array\n"
                        "gnu-ld         All data segments are embedded into a data section using GNU LD\n"
                        "sectcreate1    All data segments are embedded into a data section using sectcreate\n"
                        "               and accessed using asm (modern Mach-O LD)\n"
                        "sectcreate2    All data segments are embedded into a data section using sectcreate\n"
                        "               and accessed using Mach-O getsectdata (older Mach-O LD)\n"
                    );
                    return EXIT_SUCCESS;
                } else {
                    fprintf(
                        stderr,
                        "w2c2: unsupported data segment mode '%s'. Use 'help' to print available modes\n",
                        optarg
                    );
                    return EXIT_FAILURE;
                }
                break;
            }
            case 'h': {
                fprintf(stderr, "w2c2\n");
                fprintf(stderr, "  Compiles a WebAssembly module in binary format to a C source file and header\n");
                fprintf(stderr, "\n");
                fprintf(stderr, "usage:\n");
                fprintf(stderr, "  w2c2 [options] MODULE OUTPUT\n");
                fprintf(stderr, "\n");
                fprintf(stderr, "arguments:\n");
                fprintf(stderr, "  MODULE: Path of binary WebAssembly module\n");
                fprintf(stderr, "  OUTPUT: Path of output file(s)\n");
                fprintf(stderr, "\n");
                fprintf(stderr, "options:\n");
                fprintf(stderr, "  -h         Print this help message\n");
#if HAS_PTHREAD
                fprintf(stderr, "  -j N       Number of jobs (>1 enables parallel compilation)\n");
#endif /* HAS_PTHREAD */
                fprintf(stderr, "  -f N       Number of functions per file when parallel compilation is enabled\n");
                fprintf(stderr, "  -d MODE    Data segment mode. Default: arrays. Use 'help' to print available modes\n");
                fprintf(stderr, "  -g         Generate debug information (#line directives) based on DWARF\n");
                fprintf(stderr, "  -p         Generate pretty code\n");
                return 0;
            }
            case '?': {
                if (isprint(optopt)) {
                    fprintf(stderr, "w2c2: unknown option `-%c'.\n", optopt);
                } else {
                    fprintf(
                        stderr,
                        "w2c2: unknown option character `\\x%x'.\n",
                        optopt
                    );
                }
                return 1;
            }
            default:
                abort();
        }
    }

#if HAS_PTHREAD
    if (jobCount < 1) {
        fprintf(stderr, "w2c2: expected jobCount >= 1, got %d\n", jobCount);
        return 1;
    }
#endif /* HAS_PTHREAD */

    index = optind;

    /* Module argument */

    if (index >= argc) {
        fprintf(
            stderr,
            "w2c2: expected filename argument.\n"
            "Try '-h' for more information.\n"
        );
        return 1;
    }
    modulePath = argv[index++];

    /* Output path argument */

    if (index >= argc) {
        fprintf(
            stderr,
            "w2c2: expected output path argument.\n"
            "Try '-h' for more information.\n"
        );
        return 1;
    }
    outputPath = argv[index++];

    getPathModuleName(moduleName, modulePath);

    {
        WasmModuleReader reader = emptyWasmModuleReader;
        WasmCWriteModuleOptions writeOptions = emptyWasmCWriteModuleOptions;

        if (!readWasmBinary(modulePath, &reader, debug)) {
            return 1;
        }

        if (jobCount == 1) {
            functionsPerFile = reader.module->functions.count;
        }

        writeOptions.outputPath = outputPath;
        writeOptions.jobCount = jobCount;
        writeOptions.functionsPerFile = functionsPerFile;
        writeOptions.pretty = pretty;
        writeOptions.debug = debug;
        writeOptions.dataSegmentMode = dataSegmentMode;

        if (!wasmCWriteModule(reader.module, moduleName, writeOptions)) {
            fprintf(stderr, "w2c2: failed to compile\n");
            return 1;
        }
    }

    return 0;
}
