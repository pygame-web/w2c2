#include "path.h"
#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#if HAS_UNISTD
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif
#if HAS_GETOPT
  #include <getopt.h>
#include <glob.h>

#else
  #include "getopt_impl.h"
#endif /* HAS_GETOPT */

#include "buffer.h"
#include "file.h"
#include "reader.h"
#include "c.h"
#include "stringbuilder.h"

#if HAS_PTHREAD
static char* const optString = "t:f:d:r:pgmch";
#else
static char* const optString = "f:d:r:pgmch";
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
    wasmModuleReaderResult->debug = debug;

    wasmModuleRead(wasmModuleReaderResult, &error);
    if (error != NULL) {
        fprintf(
            stderr,
            "w2c2: failed to read module %s: %s\n",
            path,
            wasmModuleReaderErrorMessage(error)
        );
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
    while (i && modulePath[i] == PATH_SEPARATOR) {
        i--;
    }
    while (i && modulePath[i] != '.' && modulePath[i] != PATH_SEPARATOR) {
        i--;
    }
    ext = i;
    while (i && modulePath[i-1] != PATH_SEPARATOR) {
        i--;
    }

    for (; i < ext; i++) {
        if (!isalnum(modulePath[i])) {
            continue;
        }
        moduleName[j++] = modulePath[i];
    }
    moduleName[j] = '\0';
}

int wasmFunctionIDsCompareHashes(const void* a, const void* b) {
    const WasmFunctionID* functionIDA = a;
    const WasmFunctionID* functionIDB = b;
    return memcmp(functionIDA->hash, functionIDB->hash, SHA1_DIGEST_LENGTH);
}

WasmFunctionIDs wasmSortedFunctionIDs(WasmFunctions functions) {
    WasmFunctionIDs result = emptyWasmFunctionIDs;

    U32 functionIndex = 0;
    for (; functionIndex < functions.count; functionIndex++) {
        WasmFunction function = functions.functions[functionIndex];
        WasmFunctionID functionID = emptyWasmFunctionID;
        memcpy(functionID.hash, function.hash, SHA1_DIGEST_LENGTH);
        functionID.functionIndex = functionIndex;
        if (!wasmFunctionIDsAppend(&result, functionID)) {
            exit(1);
        }
    }

    if (result.length > 1) {
        qsort(
                result.functionIDs,
                result.length,
                sizeof(WasmFunctionID),
                wasmFunctionIDsCompareHashes
        );
    }

    return result;
}

void wasmSplitStaticAndDynamicFunctions(
    WasmFunctionIDs functionIDs,
    WasmFunctionIDs referenceFunctionIDs,
    WasmFunctionIDs *staticFunctions,
    WasmFunctionIDs *dynamicFunctions
) {
    WasmFunctionID* functionID = functionIDs.functionIDs;
    WasmFunctionID* lastFunctionID = functionID + functionIDs.length;

    WasmFunctionID* referenceFunctionID = referenceFunctionIDs.functionIDs;
    WasmFunctionID* lastReferenceFunctionID = referenceFunctionID + referenceFunctionIDs.length;

    while (
        functionID != lastFunctionID
        && referenceFunctionID != lastReferenceFunctionID
    ) {
        int comparisonResult = wasmFunctionIDsCompareHashes(functionID, referenceFunctionID);
        if (comparisonResult < 0) {
            /* Function only exists in the module, and not in the reference module, so it's a dynamic function */
            if (!wasmFunctionIDsAppend(dynamicFunctions, *functionID)) {
                exit(1);
            }

            functionID++;

        } else if (comparisonResult > 0) {
            /* Function only exists in the reference module, and not in the module, so skip it */
            referenceFunctionID++;

        } else {
            /* comparisonResult == 0 */
            /* Function exists in both modules, so it's a static function */
            if (!wasmFunctionIDsAppend(staticFunctions, *functionID)) {
                exit(1);
            }

            functionID++;
            referenceFunctionID++;
        }
    }

    while (functionID != lastFunctionID) {
        /* Identifier only exists in the module, and not in the reference module, so it's a dynamic function */
        if (!wasmFunctionIDsAppend(dynamicFunctions, *functionID)) {
            exit(1);
        }
        functionID++;
    }
}

static
void
cleanImplementationFiles(void) {
    glob_t globbuf;
    size_t pathIndex = 0;
    int globResult = glob("[sd]*.c", GLOB_NOSORT, NULL, &globbuf);
    if (globResult != 0) {
        if (globResult != GLOB_NOMATCH) {
            fprintf(stderr, "w2c2: failed to glob files to clean\n");
        }
        return;
    }

    for (; pathIndex < globbuf.gl_pathc; pathIndex++) {
        int pathCharIndex = 1;
        bool allDigits = true;

        char *path = globbuf.gl_pathv[pathIndex];
        size_t pathLength = strlen(path);
        if (pathLength != W2C2_IMPL_FILENAME_LENGTH) {
            continue;
        }

        for (; pathCharIndex < pathLength - 2; pathCharIndex++) {
            char c = path[pathCharIndex];
            if (c < '0' || c > '9') {
                allDigits = false;
                break;
            }
        }
        if (!allDigits) {
            continue;
        }

        if (remove(path) != 0) {
            fprintf(stderr, "w2c2: failed to remove file %s\n", path);
        }
    }
}

int
main(
    int argc,
    char* argv[]
) {
    U32 threadCount = 0;
    char* modulePath = NULL;
    char* referenceModulePath = NULL;
    char* outputPath = NULL;
    U32 functionsPerFile = 0;
    bool pretty = false;
    bool debug = false;
    bool multipleModules = false;
    WasmDataSegmentMode dataSegmentMode = wasmDataSegmentModeArrays;
    char moduleName[PATH_MAX];
    bool clean = false;

    int index = 0;
    int c = -1;

    opterr = 0;

    while ((c = getopt(argc, argv, optString)) != -1) {
        switch (c) {
#if HAS_PTHREAD
            case 't': {
                threadCount = (U32) strtoul(optarg, NULL, 0);
                break;
            }
#endif /* HAS_PTHREAD */
            case 'f': {
                functionsPerFile = (U32) strtoul(optarg, NULL, 0);
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
            case 'm': {
                multipleModules = true;
                break;
            }
            case 'c': {
                clean = true;
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
            case 'r': {
                referenceModulePath = optarg;
                break;
            }
            case 'h': {
                fprintf(
                    stderr,
                    "w2c2\n"
                    "  Compiles a WebAssembly module in binary format to a C source file and header\n"
                    "\n"
                    "usage:\n"
                    "  w2c2 [options] MODULE OUTPUT\n"
                    "\n"
                    "arguments:\n"
                    "  MODULE: Path of binary WebAssembly module\n"
                    "  OUTPUT: Path of output file(s)\n"
                    "\n"
                    "options:\n"
                    "  -h         Print this help message\n"
#if HAS_PTHREAD
                    "  -t N       Number of threads\n"
#endif /* HAS_PTHREAD */
                    "  -f N       Number of functions per file. 0 (default) writes all functions into one file\n"
                    "  -d MODE    Data segment mode. Default: arrays. Use 'help' to print available modes\n"
                    "  -g         Generate debug information (function names using asm(); #line directives based on DWARF, if available)\n"
                    "  -p         Generate pretty code\n"
                    "  -m         Support multiple modules (prefixes function names)\n"
                    "  -r         Reference module\n"
                );
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
    if (threadCount < 1) {
#ifdef _SC_NPROCESSORS_ONLN
        threadCount = (U32) sysconf(_SC_NPROCESSORS_ONLN);
#else
        threadCount = 1;
#endif
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

    if (clean) {
        cleanImplementationFiles();
    }

    {
        WasmModuleReader reader = emptyWasmModuleReader;
        WasmCWriteModuleOptions writeOptions = emptyWasmCWriteModuleOptions;

        WasmFunctionIDs functionIDs = emptyWasmFunctionIDs;

        WasmFunctionIDs staticFunctionIDs = emptyWasmFunctionIDs;
        WasmFunctionIDs dynamicFunctionIDs = emptyWasmFunctionIDs;

        if (!readWasmBinary(modulePath, &reader, debug)) {
            return 1;
        }

        functionIDs = wasmSortedFunctionIDs(reader.module->functions);

        if (referenceModulePath != NULL) {
            WasmModuleReader referenceReader = emptyWasmModuleReader;
            WasmFunctionIDs referenceFunctionIDs = emptyWasmFunctionIDs;

            size_t total = 0;

            if (!readWasmBinary(referenceModulePath, &referenceReader, false)) {
                return 1;
            }

            referenceFunctionIDs = wasmSortedFunctionIDs(referenceReader.module->functions);

            wasmSplitStaticAndDynamicFunctions(
                functionIDs,
                referenceFunctionIDs,
                &staticFunctionIDs,
                &dynamicFunctionIDs
            );

            total = dynamicFunctionIDs.length + staticFunctionIDs.length;

            fprintf(
                stderr,
                "w2c2: %lu of %lu functions are dynamic (%.2f%%)\n",
                dynamicFunctionIDs.length,
                total,
                (float)dynamicFunctionIDs.length / (float)total * 100.0
            );
        } else {
            staticFunctionIDs = functionIDs;
            dynamicFunctionIDs = emptyWasmFunctionIDs;
        }

        if (functionsPerFile == 0) {
            functionsPerFile = reader.module->functions.count;
        }
        if (functionsPerFile == 0) {
            functionsPerFile = UINT32_MAX;
        }

        writeOptions.outputPath = outputPath;
        writeOptions.threadCount = threadCount;
        writeOptions.functionsPerFile = functionsPerFile;
        writeOptions.pretty = pretty;
        writeOptions.debug = debug;
        writeOptions.multipleModules = multipleModules;
        writeOptions.dataSegmentMode = dataSegmentMode;

        if (!wasmCWriteModule(
            reader.module,
            moduleName,
            writeOptions,
            staticFunctionIDs,
            dynamicFunctionIDs
        )) {
            fprintf(stderr, "w2c2: failed to compile\n");
            return 1;
        }
    }

    return 0;
}
