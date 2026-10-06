// Takes still pictures out of the game's own opening movie (game\opening.sfd)
// so that the launcher can show them. Nothing from the game is stored in the
// launcher itself: the pictures are decoded on the player's PC from the
// player's own copy.
//
// opening.sfd is a CRI Sofdec file: an MPEG program stream with MPEG-1 video
// and ADX audio. The video is decoded with pl_mpeg (MIT licence, see
// pl_mpeg_sofdec.h for the three changes this stream needs). Only intra
// pictures are decoded: one per group of pictures, about two a second, which
// is plenty to choose a still from and needs no reference pictures.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg_sofdec.h"

#include "movie_still.h"

typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} Bytes;

static int BytesAppend(Bytes* b, const uint8_t* data, size_t size) {
    if (b->size + size > b->capacity) {
        size_t capacity = b->capacity ? b->capacity * 2 : (1u << 20);
        while (capacity < b->size + size) capacity *= 2;
        uint8_t* grown = (uint8_t*)realloc(b->data, capacity);
        if (!grown) return 0;
        b->data = grown;
        b->capacity = capacity;
    }
    memcpy(b->data + b->size, data, size);
    b->size += size;
    return 1;
}

static uint8_t Clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// Full-range BT.601 (the movie's black is 0, its white 255), chroma taken
// from the nearest sample. Output is 32-bit BGRA, top row first.
static uint8_t* FrameToBgra(const plm_frame_t* f) {
    uint8_t* out = (uint8_t*)malloc((size_t)f->width * f->height * 4);
    if (!out) return NULL;
    for (unsigned y = 0; y < f->height; y++) {
        const uint8_t* ly = f->y.data + (size_t)y * f->y.width;
        const uint8_t* lcb = f->cb.data + (size_t)(y / 2) * f->cb.width;
        const uint8_t* lcr = f->cr.data + (size_t)(y / 2) * f->cr.width;
        uint8_t* o = out + (size_t)y * f->width * 4;
        for (unsigned x = 0; x < f->width; x++) {
            int yy = ly[x] << 16;
            int cb = lcb[x / 2] - 128;
            int cr = lcr[x / 2] - 128;
            o[0] = Clamp8((yy + 116130 * cb + 32768) >> 16);             // B
            o[1] = Clamp8((yy - 22554 * cb - 46802 * cr + 32768) >> 16);  // G
            o[2] = Clamp8((yy + 91881 * cr + 32768) >> 16);              // R
            o[3] = 255;
            o += 4;
        }
    }
    return out;
}

// Decodes one intra picture from "sequence header + picture" bytes.
static int DecodeIntra(const Bytes* header, const uint8_t* picture, size_t picture_size, int sofdec, Rr6Still* still) {
    size_t size = header->size + picture_size;
    uint8_t* memory = (uint8_t*)malloc(size);
    if (!memory) return 0;
    memcpy(memory, header->data, header->size);
    memcpy(memory + header->size, picture, picture_size);

    int ok = 0;
    plm_buffer_t* buffer = plm_buffer_create_with_memory(memory, size, FALSE);
    plm_video_t* video = plm_video_create_with_buffer(buffer, TRUE);
    if (video && plm_video_has_header(video)) {
        plm_video_set_no_delay(video, TRUE);
        plm_video_set_dc_extra_bits(video, sofdec ? 3 : 0);
        plm_frame_t* frame = plm_video_decode(video);
        if (frame && video->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA && frame->width >= 16 && frame->height >= 16) {
            still->bgra = FrameToBgra(frame);
            still->width = (int)frame->width;
            still->height = (int)frame->height;
            ok = still->bgra != NULL;
        }
    }
    if (video) plm_video_destroy(video);
    free(memory);
    return ok;
}

int rr6_movie_stills(FILE* file, const int* pictures, int count, Rr6Still* stills) {
    for (int i = 0; i < count; i++) memset(&stills[i], 0, sizeof(stills[i]));
    if (!file || count <= 0) return 0;

    plm_buffer_t* file_buffer = plm_buffer_create_with_file(file, FALSE);
    plm_demux_t* demux = plm_demux_create(file_buffer, TRUE);
    if (!demux) return 0;

    Bytes es = {0};       // video elementary stream, a sliding window
    Bytes header = {0};   // most recent sequence header (carries the quantiser matrices)
    size_t scan = 0;      // next position in `es` to look for a start code
    int coded = 0;        // pictures seen so far, in stream order
    int sofdec = 0;       // "TMPGEXS" user data seen: 11-bit DC values
    int next = 0;         // index of the next still wanted
    int capturing = 0;
    size_t capture_start = 0;
    int capture_pictures = 0;
    int found = 0;
    int ended = 0;

    while (next < count && !ended) {
        plm_packet_t* packet = plm_demux_decode(demux);
        if (!packet) {
            ended = 1;
        } else {
            if (packet->type != PLM_DEMUX_PACKET_VIDEO_1) continue;
            if (!BytesAppend(&es, packet->data, packet->length)) break;
        }

        // A start code is only handled once everything that may be read after
        // it has arrived (the longest case is a sequence header, 140 bytes).
        const size_t lookahead = ended ? 4 : 160;
        while (scan + lookahead <= es.size && next < count) {
            const uint8_t* p = (const uint8_t*)memchr(es.data + scan, 0, es.size - lookahead - scan + 1);
            if (!p) {
                scan = es.size - lookahead + 1;
                break;
            }
            size_t i = (size_t)(p - es.data);
            if (es.data[i + 1] != 0 || es.data[i + 2] != 1) {
                scan = i + 1;
                continue;
            }
            const uint8_t code = es.data[i + 3];
            const size_t available = es.size - i;
            scan = i + 4;

            const int boundary = code == 0xB3 || code == 0xB8 || code == 0xB7;
            if (capturing && (boundary || (code == 0x00 && ++capture_pictures == 2))) {
                // The wanted group's first picture is complete.
                if (header.size && DecodeIntra(&header, es.data + capture_start, i - capture_start, sofdec, &stills[next])) {
                    found++;
                }
                next++;
                capturing = 0;
                if (code == 0x00) coded++;
            } else if (code == 0x00) {
                coded++;
            }

            if (code == 0xB3 && available >= 12) {
                // 62 fixed bits, then "load intra matrix" and, after the
                // optional 64 bytes, "load non-intra matrix".
                const int load_intra = (es.data[i + 11] >> 1) & 1;
                const size_t flag_at = i + 11 + (load_intra ? 64 : 0);
                if (flag_at < es.size) {
                    const int load_non_intra = es.data[flag_at] & 1;
                    const size_t length = 12 + 64u * (size_t)(load_intra + load_non_intra);
                    if (length <= available) {
                        header.size = 0;
                        BytesAppend(&header, es.data + i, length);
                    }
                }
            } else if (code == 0xB2) {
                const size_t limit = available < 40 ? available : 40;
                for (size_t k = 4; k + 7 <= limit; k++) {
                    if (!memcmp(es.data + i + k, "TMPGEXS", 7)) sofdec = 1;
                }
            } else if (code == 0xB8 && !capturing && next < count && coded >= pictures[next]) {
                capturing = 1;
                capture_start = i;
                capture_pictures = 0;
            }
        }

        if (ended && capturing && next < count) {
            if (header.size && DecodeIntra(&header, es.data + capture_start, es.size - capture_start, sofdec, &stills[next])) {
                found++;
            }
            next++;
            capturing = 0;
        }

        // Keep the window small while nothing is being collected.
        if (!capturing && scan > (1u << 20)) {
            memmove(es.data, es.data + scan, es.size - scan);
            es.size -= scan;
            scan = 0;
        }
    }

    free(es.data);
    free(header.data);
    plm_demux_destroy(demux);
    return found;
}

void rr6_movie_free(Rr6Still* still) {
    if (still) {
        free(still->bgra);
        memset(still, 0, sizeof(*still));
    }
}
