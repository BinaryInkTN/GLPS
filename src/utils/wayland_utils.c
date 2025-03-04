#include "utils/wayland_utils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>

void set_shader_src_file(const char *file_path, GLuint shader)
{
    if (access(file_path, F_OK) == -1)
    {
        perror("File does not exist");
        return;
    }

    char *source = NULL;
    FILE *fp = fopen(file_path, "r");

    if (fp != NULL)
    {
        if (fseek(fp, 0L, SEEK_END) == 0)
        {
            long bufsize = ftell(fp);
            if (bufsize == -1)
            {
                perror("Couldn't get file size.");
                fclose(fp);
                return;
            }

            source = (char *)malloc(sizeof(char) * (bufsize + 1));
            if (source == NULL)
            {
                perror("Unable to allocate memory for source code.");
                fclose(fp);
                return;
            }

            if (fseek(fp, 0L, SEEK_SET) != 0)
            {
                perror("Error resetting file pointer.");
                free(source);
                fclose(fp);
                return;
            }

            size_t newLen = fread(source, sizeof(char), bufsize, fp);
            if (ferror(fp) != 0)
            {
                perror("Error reading file");
                free(source);
                fclose(fp);
                return;
            }
            else
            {
                source[newLen] = '\0';
            }
        }
        else
        {
            perror("Error seeking to end of file.");
        }

        fclose(fp);
    }
    else
    {
        perror("Error opening file");
        return;
    }

    glShaderSource(shader, 1, (const char *const *)&source, NULL);
   free(source);
}
