#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
#include <inttypes.h>

#define BUFFER_SIZE 20000000

void parse_xcactivitylog(char input[BUFFER_SIZE], FILE *output) {
    const char *p = input;

    if (strncmp(p, "SLF0", 4) != 0) {
        fprintf(stderr, "[ERROR] Invalid SLF header\n");
        exit(1);
    }
    p += 4;

    while (*p != '\0') {
        if (*p >= '0' && *p <= '9' || *p >= 'a' && *p <= 'f') {
            uint64_t value = 0;
            uint64_t hex_value = 0;
            while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
                value = value * 10 + (*p - '0');
                /* hex_value = hex_value * 16 + (*p >= 'a' ? *p - 'a' + 10 : (*p >= 'A' ? *p - 'A' + 10 : *p - '0')); // this is not correct */
                (p)++;
            }

            if (*p == '#') { // integer
                fprintf(output, "[type: \"int\", value: %" PRIu64 "]\n", value);
                p++;
            } else if (*p == '^') { // double
                double double_value = *((double*)&hex_value);
                double_value = __builtin_bswap64(hex_value);  // Convert from little-endian
                fprintf(output, "[type: \"double\", value: %f]\n", double_value);
                p++;
            } else if (*p == '@') { // classInstance with index of declared class
                // since we don't care about these classes yet, just print out the number
                fprintf(output, "[type: \"classInstance\", value: %" PRIu64 "]\n", value);
                p++;
            } else if (*p == '"' || *p == '*') { // string
                p++;
                char *str_value = (char *)malloc(value + 1);
                strncpy(str_value, p, value);
                str_value[value] = '\0';  // Null-terminate the string
                fprintf(output, "[type: \"string\", length: %lu, value: \"%s\"]\n", value, str_value);
                free(str_value);
                p += value;
            } else if (*p == '%') { // className
                p++;
                char *str_value = (char *)malloc(value + 1);
                strncpy(str_value, p, value);
                str_value[value] = '\0';
                p += value;
                fprintf(output, "[type: \"className\", value: \"%s\"]\n", str_value);
                free(str_value);
            } else if (*p == '(') {
                fprintf(output, "[type: \"array\", count: %" PRIu64 "]\n", value);
                p++;
            } else { // unexpected
                fprintf(stderr, "[ERROR] Unexpected token %c after a number.\n", *p);
                exit(1);
            }
        } else if (*p == '-') {
            fprintf(output, "[type: \"null\"]\n");
            p++;
        } else if (*p == '\n') {
            p++;
        } else if (*p == '\0') {
            printf("[EOF]\n");
        } else {
            fprintf(stderr, "[ERROR] Unexpected token '%c'.\n", *p);
            exit(1);
        }
    }
    printf("Finished work: %d\n", *p == '\0');
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.xcactivitylog> <output.txt>\n", argv[0]);
        return 1;
    }

    gzFile gz_input = gzopen(argv[1], "rb");
    if (!gz_input) {
        perror("Failed to open gzipped file");
        return 1;
    }

    FILE *output = fopen(argv[2], "w");
    if (!output) {
        perror("Failed to open output file");
        gzclose(gz_input);
        return 1;
    }

    char *buffer = (char*) malloc(BUFFER_SIZE);
    gzread(gz_input, buffer, BUFFER_SIZE);
    parse_xcactivitylog(buffer, output);

    gzclose(gz_input);
    fclose(output);

    return 0;
}
