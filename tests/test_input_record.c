#include "input_record.h"

#include <stdio.h>
#include <string.h>

static int count_lines(FILE *file, const char *needle)
{
    char line[2048];
    int count = 0;
    while (fgets(line, sizeof line, file))
        if (strstr(line, needle)) count++;
    return count;
}

int main(void)
{
    const char *path = "scratch/test-input-record.jsonl";
    unsigned char keyboard[256] = {0};
    unsigned char mouse[20] = {0};
    unsigned char pad[272] = {0};
    FILE *file;
    int keyboard_lines, mouse_lines, pad_lines;

    if (!input_record_start(path)) return 1;
    input_record_keyboard(keyboard, sizeof keyboard, 1, 0.1);
    input_record_keyboard(keyboard, sizeof keyboard, 2, 0.2);
    keyboard[0x11] = 0x80;
    input_record_keyboard(keyboard, sizeof keyboard, 3, 0.3);
    input_record_mouse(mouse, sizeof mouse, 3, 0.3);
    pad[48] = 0x80;
    input_record_gamepad(0, "test-\"pad\\one", pad, sizeof pad, 3, 0.3);
    input_record_report();

    file = fopen(path, "r");
    if (!file) return 1;
    keyboard_lines = count_lines(file, "\"type\":\"keyboard\"");
    rewind(file);
    mouse_lines = count_lines(file, "\"type\":\"mouse\"");
    rewind(file);
    pad_lines = count_lines(file, "\"type\":\"gamepad\"");
    rewind(file);
    if (count_lines(file, "\"persistent_id\":\"test-\\\"pad\\\\one\"") != 1) {
        fprintf(stderr, "input record: persistent ID is not valid escaped JSON\n");
        fclose(file);
        return 1;
    }
    fclose(file);
    if (keyboard_lines != 2 || mouse_lines != 1 || pad_lines != 1) {
        fprintf(stderr, "input record: got keyboard=%d mouse=%d gamepad=%d; "
                        "expected change-only 2/1/1\n",
                keyboard_lines, mouse_lines, pad_lines);
        return 1;
    }
    printf("input record: initial states recorded and unchanged polls omitted\n");
    return 0;
}
