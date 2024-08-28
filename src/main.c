#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
#include <inttypes.h>
#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

const int BUFFER_SIZE = 100000000; // 100 MB is easy for modern computers

void find_latest_file(const char *directory, char *latest_file, time_t *latest_mtime, const char *project_name) {
    DIR *dir = opendir(directory);
    if (!dir) {
        fprintf(stderr, "Unable to access directory: %s\n", directory);
        exit(1);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // this is how we find our folder
        if (project_name != NULL) {
            if (strstr(directory, project_name) == NULL) {
                if (strstr(entry->d_name, project_name) == NULL) {
                    continue;
                }
            } else if (strstr(directory, "Logs") == NULL) {
                if (strstr(entry->d_name, "Logs") == NULL) {
                    continue;
                }
            } else if (strstr(directory, "Build") == NULL) {
                if (strstr(entry->d_name, "Build") == NULL) {
                    continue;
                }
            }
        }

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

        struct stat file_stat;
        if (stat(path, &file_stat) == -1) {
            /* fprintf(stderr, "Unable to read: %s\n", path); */
            continue;
        }

        if (S_ISDIR(file_stat.st_mode)) {
            find_latest_file(path, latest_file, latest_mtime, project_name);
        } else if (S_ISREG(file_stat.st_mode) && strstr(entry->d_name, ".xcactivitylog") != NULL) {
            if (file_stat.st_mtime > *latest_mtime) {
                *latest_mtime = file_stat.st_mtime;
                strcpy(latest_file, path);
            }
        }
    }

    closedir(dir);
}

void parse_xcactivitylog(char *input, FILE *output, bool output_full_log) {
    // flags to see which step we are on
    bool found_diagnostic_activity_log_message = false;
    bool next_string_is_message = false;
    bool next_string_is_file = false;
    bool next_string_is_log_type = false;
    bool next_int_is_line = false;
    bool next_int_is_column = false;

    // number of encountered LogMessage in the row
    // we reset message collection if we see too many
    int found_log_messages = 0;
    
    // gathering data
    char classes[10][100];
    int classes_found = 1;
    
    char messages[10][1000]; // collected LogMessage strings
    int messages_count = 0;
    char *file_name = NULL;
    uint64_t line = 0;
    uint64_t column = 0;
    
    if (strncmp(input, "SLF0", 4) != 0) {
        fprintf(stderr, "[ERROR] Invalid SLF header\n");
        exit(1);
    }
    int i = 4; // skipping header

    while (i < BUFFER_SIZE) {
        if ((*(input + i) >= '0' && *(input + i) <= '9') || (*(input + i) >= 'a' && *(input + i) <= 'f')) {
            uint64_t value = 0;
            // to not fail when encountering doubles, we also parse these
            while ((*(input + i) >= '0' && *(input + i) <= '9') || (*(input + i) >= 'a' && *(input + i) <= 'f') || (*(input + i) >= 'A' && *(input + i) <= 'F')) {
                *(input + i) = *(input + i);
                value = value * 10 + (*(input + i) - '0');
                i++;
            }

            if (*(input + i) == '#') { // integer
                i++;
                if (output_full_log) {
                    fprintf(output, "[type: \"int\", value: %" PRIu64 "]\n", value);
                }

                if (next_int_is_line) {
                    next_int_is_line = false;
                    next_int_is_column = true;
                    line = value;
                } else if (next_int_is_column) {
                    next_int_is_column = false;
                    column = value;
                }
            } else if (*(input + i) == '^') { // double
                i++;
                if (output_full_log) {
                    fprintf(output, "[type: \"double\", value: not parsed]\n");
                }
            } else if (*(input + i) == '%') { // className
                i++;
                strncpy(&classes[classes_found][0], &input[i], value);
                classes[classes_found][value] = '\0';

                if (output_full_log) {
                    fprintf(output, "[type: \"className\", length: %llu, index: %d, value: \"%s\"]\n", value, classes_found, classes[classes_found]);
                }
                classes_found++;
                i += value;
            } else if (*(input + i) == '@') { // classInstance
                i++;
                char *class_name = &classes[value][0];

                if (strcmp(class_name, "IDEDiagnosticActivityLogMessage") == 0) {
                    found_diagnostic_activity_log_message = true;
                    next_string_is_message = true;
                    next_string_is_log_type = false;
                    next_string_is_file = false;
                    found_log_messages++;
                    if (found_log_messages > 5) {
                        found_log_messages = 0;
                        messages_count = 0;
                    }
                } else if (found_diagnostic_activity_log_message && strcmp(class_name, "DVTTextDocumentLocation") == 0) {
                    next_string_is_file = true;
                    next_int_is_line = true;
                } else {
                    next_string_is_file = false;
                    next_int_is_line = false;
                }

                if (output_full_log) {
                    fprintf(output, "[type: \"classInstance\", value: \"%s\"]\n", class_name);
                }
            } else if (*(input + i) == '"' || *(input + i) == '*') { // string
                i++;

                if (value <= 1000) {
                    char str_value[1000] = {0};
                    strncpy(str_value, &input[i], value);

                    if (found_diagnostic_activity_log_message) {
                        if (next_string_is_message) {
                            messages[messages_count][0] = *str_value;
                            messages_count++;
                            next_string_is_message = false;
                        } else if (next_string_is_file) {
                            file_name = str_value;
                            next_string_is_file = false;
                            next_string_is_log_type = true;
                        } else if (next_string_is_log_type) {
                            next_string_is_log_type = false;

                            if (strcmp(str_value, "Swift Compiler Error") == 0) {
                                printf("%s:%llu:%llu\n", file_name, line + 1, column + 1);

                                for (int i = 0; i < messages_count; i++) {
                                    printf("%s\n", &messages[i][0]);
                                }
                                printf("\n");
                                found_diagnostic_activity_log_message = false;
                                messages_count = 0;
                                found_log_messages = 0;
                            } else if (strcmp(str_value, "Swift Compiler Notice") != 0) {
                                found_diagnostic_activity_log_message = false;
                                messages_count = 0;
                                found_log_messages = 0;
                            }
                        }
                    }
                    if (output_full_log) {
                        fprintf(output, "[type: \"string\", length: %llu, value: \"%s\"]\n", value, str_value);
                    }
                } else {
                    if (output_full_log) {
                        fprintf(output, "[type: \"string\", length: %llu, value skipped]\n", value);
                    }
                }

                i += value;
            } else if (*(input + i) == '(') {
                if (output_full_log) {
                    fprintf(output, "[type: \"array\", count: %" PRIu64 "]\n", value);
                }
                i++;
            } else { // unexpected
                fprintf(stderr, "[ERROR] Unexpected token '%c' (%d) after a number (%llu) at position %d.\n", *(input+i), (uint8_t)*(input+i), value, i);
                exit(1);
            }
        } else if (*(input + i) == '-') {
            if (output_full_log) {
                fprintf(output, "[type: \"null\"]\n");
            }
            i++;
        } else if (*(input + i) == '\n') {
            i++;
        } else if (*(input + i) == '\0' || *(input + i) == 4) {
            // end of file
            break;
        } else {
            fprintf(stderr, "[ERROR] Unexpected token '%c' (%d) at position %d.\n", *(input+i), (uint8_t)*(input+i), i);
            exit(1);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0) {
        printf("Usage: %s <project_name> [-d]\n\n", argv[0]);
        printf("Latest modified found inside will by analysed (~/Library/Developer/Xcode/DerivedData/[project_name]*/Logs/Build/)\n\n");
        printf("Pass flag -d to dump the whole parsed log as log-dump.txt\n");
        exit(1);
    }

    bool output_full_log = false;
    if (argc >= 3 && strcmp(argv[2], "-d") == 0) {
        output_full_log = true;
    }

    char path[250];
    const char *home_dir = getenv("HOME");
    sprintf(path, "%s/Library/Developer/Xcode/DerivedData", home_dir);

    char *project_name = argv[1];

    char latest_file[1024] = {0};
    time_t latest_mtime = 0;
    find_latest_file(path, latest_file, &latest_mtime, project_name);
    if (strlen(latest_file) > 0) {
        if (output_full_log) {
            printf("Found file: %s\n", latest_file);
        }
    } else {
        printf("No .xcactivitylog files found.\n");
    }

    gzFile gz_input = gzopen(latest_file, "rb");
    if (!gz_input) {
        perror("Failed to open gzipped file");
        return 1;
    }

    FILE *output = NULL;
    if (output_full_log) {
        output = fopen("log-dump.txt", "w");
        if (!output) {
            perror("Failed to open output file");
            gzclose(gz_input);
            return 1;
        }
    }

    char *buffer = (char*) malloc(BUFFER_SIZE);
    gzread(gz_input, buffer, BUFFER_SIZE);
    parse_xcactivitylog(buffer, output, output_full_log);

    gzclose(gz_input);
    if (output_full_log) {
        fclose(output);
    }

    return 0;
}



// TODO: proper parsing of these classes
//
/* [type: "classInstance", value: "IDEDiagnosticActivityLogMessage"] */
/* [type: "string", length: 21, value: "Expected '}' in class"] */
/* [type: "null"] */
/* [type: "int", value: 746354466] */
/* [type: "int", value: 18446744073709551615] */
/* [type: "int", value: 0] */
/* [type: "array", count: 1] */
/*     [type: "classInstance", value: "IDEDiagnosticActivityLogMessage"] */
/*     [type: "string", length: 25, value: "To match this opening '{'"] */
/*     [type: "null"] */
/*     [type: "int", value: 746354466] */
/*     [type: "int", value: 18446744073709551615] */
/*     [type: "int", value: 0] */
/* arr [type: "null"] */
/*     [type: "int", value: 0] */
/*     [type: "string", length: 27, value: "com.apple.dt.IDE.diagnostic"] */
/*         [type: "classInstance", value: "DVTTextDocumentLocation"] */
/*         [type: "string", length: 177, value: "file:///Users/iaroslav.erokhin/Documents/Check24/ios-pod-mobile-sim/Pod/Classes/CheckoutShared/Features/Checkout/InputManagement/Data%20Sources/CheckoutIBANInputDataSource.swift"] */
/*         [type: "double", value: not parsed] */
/*         [type: "int", value: 10] */
/*         [type: "int", value: 69] */
/*         [type: "int", value: 10] */
/*         [type: "int", value: 69] */
/*         [type: "int", value: 18446744073709551615] */
/*         [type: "int", value: 0] */
/*         [type: "int", value: 0] */
/*         [type: "string", length: 21, value: "Swift Compiler Notice"] */
/*         [type: "array", count: 0] */
/*         [type: "null"] */

/* [type: "int", value: 2] */
/* [type: "string", length: 27, value: "com.apple.dt.IDE.diagnostic"] */
/* [type: "classInstance", value: "DVTTextDocumentLocation"] */
/* [type: "string", length: 177, value: "file:///Users/iaroslav.erokhin/Documents/Check24/ios-pod-mobile-sim/Pod/Classes/CheckoutShared/Features/Checkout/InputManagement/Data%20Sources/CheckoutIBANInputDataSource.swift"] */
/* [type: "double", value: not parsed] */
/* [type: "int", value: 12] */
/* [type: "int", value: 4] */
/* [type: "int", value: 12] */
/* [type: "int", value: 4] */
/* [type: "int", value: 18446744073709551615] */
/* [type: "int", value: 0] */
/* [type: "int", value: 0] */
/* [type: "string", length: 20, value: "Swift Compiler Error"] */
/* [type: "array", count: 0] */
/* [type: "null"] */


// TODO: keep modified time of last build to not open old ones after problem was solved (how does that evne happen? we still have the latest build report which is ok)
