/*
 * Copyright [2026] [KamilMalicki]
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <pty.h>
#include <sys/wait.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <vterm.h>

#define MAX_WINDOWS 4
#define GAP 4
#define BORDER_W 2

#define HEX_BG 0xFF000000       
#define HEX_ACTIVE 0xFFFFFFFF    
#define HEX_INACTIVE 0xFF444444  
#define HEX_FG 0xFFDDDDDD       

uint32_t NATIVE_BG, NATIVE_ACTIVE, NATIVE_INACTIVE, NATIVE_FG;

typedef struct {
    int x, y, w, h;
    int pty_fd;
    pid_t pid;
    int cols, rows;
    VTerm *vt;           
    VTermScreen *vts;    
} Window;

Window wins[MAX_WINDOWS];
int win_count = 0;
int focused_idx = -1;

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
char *fbp;
uint8_t *backbuffer;

FT_Library ft;
FT_Face face;
struct termios orig_termios;
int tty_fd;

uint8_t glyph_cache[128][20][10]; 

uint32_t get_native_color(uint32_t hex_argb) {
    if (vinfo.bits_per_pixel == 32) return hex_argb;
    
    if (vinfo.bits_per_pixel == 16) {
        int r = (hex_argb >> 16) & 0xFF;
        int g = (hex_argb >> 8) & 0xFF;
        int b = hex_argb & 0xFF;
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
    
    return hex_argb;
}

uint32_t vterm_color_to_native(VTermScreen *vts, VTermColor *col, uint32_t def_native) {
    if (VTERM_COLOR_IS_DEFAULT_FG(col) || VTERM_COLOR_IS_DEFAULT_BG(col)) return def_native;
    vterm_screen_convert_color_to_rgb(vts, col); 
    uint32_t argb = 0xFF000000 | (col->rgb.red << 16) | (col->rgb.green << 8) | col->rgb.blue;
    return get_native_color(argb);
}

void init_glyph_cache() {
    memset(glyph_cache, 0, sizeof(glyph_cache));
    for (int i = 32; i < 128; i++) {
        if (!FT_Load_Char(face, i, FT_LOAD_RENDER)) {
            FT_Bitmap* bmp = &face->glyph->bitmap;
            int baseline = 14; 
            for (int row = 0; row < bmp->rows; row++) {
                for (int col = 0; col < bmp->width; col++) {
                    if (bmp->buffer[row * bmp->pitch + col] > 64) {
                        int cy = baseline - face->glyph->bitmap_top + row;
                        int cx = face->glyph->bitmap_left + col;
                        if (cx >= 0 && cx < 10 && cy >= 0 && cy < 20) {
                            glyph_cache[i][cy][cx] = 1; 
                        }
                    }
                }
            }
        }
    }
}

void draw_rect(int rx, int ry, int rw, int rh, uint32_t color) {
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > vinfo.xres) rw = vinfo.xres - rx;
    if (ry + rh > vinfo.yres) rh = vinfo.yres - ry;
    if (rw <= 0 || rh <= 0) return;

    int bpp = vinfo.bits_per_pixel / 8;
    
    if (bpp == 4) {
        for (int y = ry; y < ry + rh; y++) {
            uint32_t *row_ptr = (uint32_t*)(backbuffer + (y * finfo.line_length)) + rx;
            for (int x = 0; x < rw; x++) row_ptr[x] = color;
        }
    } else if (bpp == 2) {
        for (int y = ry; y < ry + rh; y++) {
            uint16_t *row_ptr = (uint16_t*)(backbuffer + (y * finfo.line_length)) + rx;
            for (int x = 0; x < rw; x++) row_ptr[x] = color;
        }
    }
}

void draw_char_fast(int x, int y, uint32_t c, uint32_t color) {
    if (c < 32 || c >= 128) return; 
    if (x < 0 || y < 0 || x + 10 > vinfo.xres || y + 20 > vinfo.yres) return;

    int bpp = vinfo.bits_per_pixel / 8;
    
    if (bpp == 4) {
        for (int r = 0; r < 20; r++) {
            uint32_t *row_ptr = (uint32_t*)(backbuffer + ((y + r) * finfo.line_length)) + x;
            for (int col = 0; col < 10; col++) if (glyph_cache[c][r][col]) row_ptr[col] = color; 
        }
    } else if (bpp == 2) {
        for (int r = 0; r < 20; r++) {
            uint16_t *row_ptr = (uint16_t*)(backbuffer + ((y + r) * finfo.line_length)) + x;
            for (int col = 0; col < 10; col++) if (glyph_cache[c][r][col]) row_ptr[col] = color; 
        }
    }
}

void tile_windows() {
    if (win_count == 0) return;
    int aw = vinfo.xres - (2 * GAP), ah = vinfo.yres - GAP;
    
    if (win_count == 1) {
        wins[0].x = GAP; wins[0].y = GAP; wins[0].w = aw; wins[0].h = ah;
    } else {
        int mw = (aw - GAP) * 0.5;
        wins[0].x = GAP; wins[0].y = GAP; wins[0].w = mw; wins[0].h = ah;
        int sw = aw - GAP - mw, sh = (ah - (win_count - 2) * GAP) / (win_count - 1);
        int cy = GAP;
        for (int i = 1; i < win_count; i++) {
            wins[i].x = GAP + mw + GAP; wins[i].y = cy; 
            wins[i].w = sw; wins[i].h = sh; cy += sh + GAP;
        }
    }

    for (int i = 0; i < win_count; i++) {
        wins[i].cols = (wins[i].w - 2 * BORDER_W - 10) / 10; 
        wins[i].rows = (wins[i].h - 2 * BORDER_W - 10) / 20; 
        vterm_set_size(wins[i].vt, wins[i].rows, wins[i].cols);
        struct winsize ws = { wins[i].rows, wins[i].cols, wins[i].w, wins[i].h };
        ioctl(wins[i].pty_fd, TIOCSWINSZ, &ws); 
    }
}

void spawn_terminal() {
    if (win_count >= MAX_WINDOWS) return;
    int master_fd;
    pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1); 
        execlp("/bin/bash", "bash", NULL);
        exit(1);
    }
    
    int flags = fcntl(master_fd, F_GETFL, 0);
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);

    Window *w = &wins[win_count];
    w->pty_fd = master_fd; w->pid = pid;
    w->vt = vterm_new(24, 80);
    vterm_set_utf8(w->vt, 1);
    w->vts = vterm_obtain_screen(w->vt);
    vterm_screen_reset(w->vts, 1);

    focused_idx = win_count++;
    tile_windows();
}

void close_active_terminal() {
    if (win_count <= 0) return;
    kill(wins[focused_idx].pid, SIGKILL);
    close(wins[focused_idx].pty_fd);
    vterm_free(wins[focused_idx].vt); 
    for (int i = focused_idx; i < win_count - 1; i++) wins[i] = wins[i+1];
    win_count--;
    if (focused_idx >= win_count) focused_idx = win_count - 1;
    if (focused_idx < 0) focused_idx = 0;
    tile_windows();
}

void render_all() {
    int bpp = vinfo.bits_per_pixel / 8;
    int total_pixels = (finfo.line_length * vinfo.yres) / bpp;
    
    if (bpp == 4) {
        uint32_t *ptr = (uint32_t*)backbuffer;
        for(int p=0; p<total_pixels; p++) ptr[p] = NATIVE_BG;
    } else if (bpp == 2) {
        uint16_t *ptr = (uint16_t*)backbuffer;
        for(int p=0; p<total_pixels; p++) ptr[p] = NATIVE_BG;
    }

    for (int i = 0; i < win_count; i++) {
        Window *w = &wins[i];
        draw_rect(w->x, w->y, w->w, w->h, (i == focused_idx) ? NATIVE_ACTIVE : NATIVE_INACTIVE);
        draw_rect(w->x + BORDER_W, w->y + BORDER_W, w->w - 2 * BORDER_W, w->h - 2 * BORDER_W, NATIVE_BG);
        
        VTermPos pos;
        for (pos.row = 0; pos.row < w->rows; pos.row++) {
            for (pos.col = 0; pos.col < w->cols; pos.col++) {
                VTermScreenCell cell;
                vterm_screen_get_cell(w->vts, pos, &cell);
                
                int px = w->x + BORDER_W + 5 + (pos.col * 10);
                int py = w->y + BORDER_W + 5 + (pos.row * 20);
                
                uint32_t bg_col = vterm_color_to_native(w->vts, &cell.bg, NATIVE_BG);
                uint32_t fg_col = vterm_color_to_native(w->vts, &cell.fg, NATIVE_FG);
                
                if (cell.attrs.reverse) { uint32_t tmp = bg_col; bg_col = fg_col; fg_col = tmp; }
                
                if (bg_col != NATIVE_BG) draw_rect(px, py, 10, 20, bg_col); 
                if (cell.chars[0]) draw_char_fast(px, py, cell.chars[0], fg_col); 
            }
        }
        
        if (i == focused_idx) {
            VTermState *state = vterm_obtain_state(w->vt);
            VTermPos cpos;
            vterm_state_get_cursorpos(state, &cpos);
            int px = w->x + BORDER_W + 5 + (cpos.col * 10);
            int py = w->y + BORDER_W + 5 + (cpos.row * 20);
            draw_rect(px, py, 10, 20, NATIVE_FG);
        }
    }
    memcpy(fbp, backbuffer, finfo.line_length * vinfo.yres);
}

int main() {
    if (FT_Init_FreeType(&ft) || FT_New_Face(ft, "mono.ttf", 0, &face)) {
        printf("Brak pliku mono.ttf!\n"); return 1;
    }
    FT_Set_Pixel_Sizes(face, 0, 15);
    init_glyph_cache();

    tty_fd = open("/dev/tty", O_RDWR);
    ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
    
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL); 
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); 
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    int s_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, s_flags | O_NONBLOCK);

    int fbfd = open("/dev/fb0", O_RDWR);
    ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
    ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);
    
    NATIVE_BG = get_native_color(HEX_BG);
    NATIVE_ACTIVE = get_native_color(HEX_ACTIVE);
    NATIVE_INACTIVE = get_native_color(HEX_INACTIVE);
    NATIVE_FG = get_native_color(HEX_FG);

    long screensize = finfo.line_length * vinfo.yres;
    fbp = (char *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    backbuffer = (uint8_t*)malloc(screensize);

    spawn_terminal(); 

    int running = 1;
    fd_set read_fds;

    while (running) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds); 
        int max_fd = STDIN_FILENO;
        
        for (int i = 0; i < win_count; i++) { 
            FD_SET(wins[i].pty_fd, &read_fds);
            if (wins[i].pty_fd > max_fd) max_fd = wins[i].pty_fd;
        }

        struct timeval tv = {0, 16000}; 
        select(max_fd + 1, &read_fds, NULL, NULL, &tv);

        int needs_redraw = 0;

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char in_buf[256];
            int in_n;
            while ((in_n = read(STDIN_FILENO, in_buf, sizeof(in_buf))) > 0) {
                char out_buf[256];
                int out_n = 0;
                for (int k = 0; k < in_n; k++) {
                    if (in_buf[k] == 27 && k + 1 < in_n) {
                        char next = in_buf[k+1];
                        if (next == 'q' || next == 'Q') { running = 0; k++; continue; }
                        if (next == 'n' || next == 'N') { spawn_terminal(); needs_redraw = 1; k++; continue; }
                        if (next == 'c' || next == 'C') { close_active_terminal(); needs_redraw = 1; k++; continue; }
                        if (next == '\t')               { if (win_count > 0) focused_idx = (focused_idx + 1) % win_count; needs_redraw = 1; k++; continue; }
                    }
                    out_buf[out_n++] = in_buf[k];
                }
                if (out_n > 0 && win_count > 0) write(wins[focused_idx].pty_fd, out_buf, out_n);
            }
        }

        for (int i = 0; i < win_count; i++) {
            if (FD_ISSET(wins[i].pty_fd, &read_fds)) {
                char buf[8192];
                int n;
                while ((n = read(wins[i].pty_fd, buf, sizeof(buf))) > 0) {
                    vterm_input_write(wins[i].vt, buf, n);
                    needs_redraw = 1;
                }
            }
        }
        
        if (needs_redraw) render_all();
    }

    for(int i=0; i<win_count; i++) { kill(wins[i].pid, SIGKILL); close(wins[i].pty_fd); vterm_free(wins[i].vt); }
    
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios); 
    ioctl(tty_fd, KDSETMODE, KD_TEXT); 
    free(backbuffer);
    close(tty_fd); close(fbfd); munmap(fbp, screensize);
    
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
    
    return 0;
}