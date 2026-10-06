// Still pictures from the game's opening movie. See movie_still.c.
#ifndef RR6_MOVIE_STILL_H
#define RR6_MOVIE_STILL_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width;
    int height;
    uint8_t* bgra;  // width * height * 4 bytes, top row first; NULL when not found
} Rr6Still;

// Reads `file` (an MPEG program stream, opened for binary reading) once from
// the start. For each entry of `pictures` (ascending; counted in stream order,
// 30 per second in opening.sfd) it decodes the first picture of the first
// group of pictures that starts at or after it. Returns how many stills were
// produced. The file is not closed.
int rr6_movie_stills(FILE* file, const int* pictures, int count, Rr6Still* stills);

void rr6_movie_free(Rr6Still* still);

#ifdef __cplusplus
}
#endif

#endif
