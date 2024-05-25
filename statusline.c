#include "mat.h"
#include "syntax.h"

#include <stdio.h>
#include <string.h>

extern struct config E;

void drawStatus(struct abuf *ab)
{
    char status[80], rstatus[80];
    char *language_symbol;

    if (E.current_file_extension == NULL)
        language_symbol = "󰈙";
    else if (strcmp(E.current_file_extension, "c") == 0)
        language_symbol = "";
    else if (strcmp(E.current_file_extension, "cpp") == 0)
        language_symbol = "";
    else if (strcmp(E.current_file_extension, "py") == 0)
        language_symbol = "";
    else
        language_symbol = "󰈙";

    const char *foreground_color = hexToAnsiFore("#9399b2");
    abAppend(ab, foreground_color, strlen(foreground_color));

    int len = snprintf(status, sizeof(status), " %s Mat | %s ", language_symbol, E.current_mode == NORMAL ? "NORMAL" : "INSERT");

    int rlen = snprintf(rstatus, sizeof(rstatus), " %s %s%s - %d/%d ",
                        "", E.current_file_name, E.dirty ? " *" : "", E.cy, E.numRws);

    if (len > E.screenCls)
        len = E.screenCls;

    abAppend(ab, status, len);

    abAppend(ab, "\x1b[m", 3);

    while (len < E.screenCls)
    {
        if (E.screenCls - len == rlen)
        {
            abAppend(ab, foreground_color, strlen(foreground_color));
            abAppend(ab, rstatus, rlen);
            break;
        }
        else
        {
            abAppend(ab, " ", 1);
            len++;
        }
    }

    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}
