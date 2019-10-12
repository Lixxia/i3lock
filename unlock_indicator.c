/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010 Michael Stapelberg
 *
 * See LICENSE for licensing information
 *
 */
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <xcb/xcb.h>
#include <ev.h>
#include <cairo.h>
#include <cairo/cairo-xcb.h>
#include <time.h>

#include "i3lock.h"
#include "xcb.h"
#include "unlock_indicator.h"
#include "randr.h"
#include "dpi.h"

#define BUTTON_RADIUS 90
#define BUTTON_SPACE (BUTTON_RADIUS + 5)
#define BUTTON_CENTER (BUTTON_RADIUS + 5)
#define BUTTON_DIAMETER (2 * BUTTON_SPACE)
#define TIME_FORMAT_12 "%l:%M %p"
#define TIME_FORMAT_24 "%k:%M"

/*******************************************************************************
 * Variables defined in i3lock.c.
 ******************************************************************************/
static struct ev_periodic *time_redraw_tick;

extern bool debug_mode;

/* The current position in the input buffer. Useful to determine if any
 * characters of the password have already been entered or not. 
 */
int input_position;

/* The lock window. */
extern xcb_window_t win;

/* The current resolution of the X11 root window. */
extern uint32_t last_resolution[2];

/* Whether the unlock indicator is enabled (defaults to true). */
extern bool unlock_indicator;

/* List of pressed modifiers, or NULL if none are pressed. */
extern char *modifier_string;

/* A Cairo surface containing the specified image (-i), if any. */
extern cairo_surface_t *img;

/* Whether the image should be tiled. */
extern bool tile;

/* The background color to use (in hex). */
extern char color[7];

/* Verify color to use (in hex). */
extern char verifycolor[7];

/* Wrong/Error color to use (in hex). */
extern char wrongcolor[7];

/* Idle color to use (in hex). */
extern char idlecolor[7];

/* Use 24 hour time format */
extern bool use24hour;

/* Whether the failed attempts should be displayed. */
extern bool show_failed_attempts;

/* Number of failed unlock attempts. */
extern int failed_attempts;

/*******************************************************************************
 * Variables defined in xcb.c.
 ******************************************************************************/

/* The root screen, to determine the DPI. */
extern xcb_screen_t *screen;

/*******************************************************************************
 * Local variables.
 ******************************************************************************/

/* Cache the screen’s visual, necessary for creating a Cairo context. */
static xcb_visualtype_t *vistype;

/* Maintain the current unlock/PAM state to draw the appropriate unlock
 * indicator. 
 */
unlock_state_t unlock_state;
auth_state_t auth_state;

/* Displayed time text */
static char timetext[100] = {'\0'};

/* Get current time text. */
void get_current_timetext(char *str, uint32_t size) {
    time_t curtime = time(NULL);
    struct tm *tm = localtime(&curtime);
    if (use24hour)
        strftime(str, size, TIME_FORMAT_24, tm);
    else
        strftime(str, size, TIME_FORMAT_12, tm);
}

/*
 * Draws global image with fill color onto a pixmap with the given
 * resolution and returns it.
 */
xcb_pixmap_t draw_image(uint32_t *resolution) {
    xcb_pixmap_t bg_pixmap = XCB_NONE;
    const double scaling_factor = get_dpi_value() / 96.0;
    int button_diameter_physical = ceil(scaling_factor * BUTTON_DIAMETER);
    DEBUG("scaling_factor is %.f, physical diameter is %d px\n",
          scaling_factor, button_diameter_physical);

    if (!vistype)
        vistype = get_root_visual_type(screen);
    bg_pixmap = create_bg_pixmap(conn, screen, resolution, color);
    /* 
     * Initialize cairo: Create one in-memory surface to render the unlock
     * indicator on, create one XCB surface to actually draw (one or more,
     * depending on the amount of screens) unlock indicators on. 
     */
    cairo_surface_t *output = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, button_diameter_physical, button_diameter_physical);
    cairo_t *ctx = cairo_create(output);

    cairo_surface_t *xcb_output = cairo_xcb_surface_create(conn, bg_pixmap, vistype, resolution[0], resolution[1]);
    cairo_t *xcb_ctx = cairo_create(xcb_output);

    /* Creates color array from command line arguments */
    uint32_t * color_array(char* colorarg) {
        uint32_t *rgb16 = malloc(sizeof(uint32_t)*3);

        char strgroups[3][3] = {{colorarg[0], colorarg[1], '\0'},
                                {colorarg[2], colorarg[3], '\0'},
                                {colorarg[4], colorarg[5], '\0'}};

        for (int i=0; i < 3; i++) {
            rgb16[i] = strtol(strgroups[i], NULL, 16);
        }

        return rgb16;
    }

    /* Sets the color based on argument (color/background, verify, wrong, idle)
     * and type (line, background and fill). Type defines alpha value and tint.
     * Utilizes color_array() and frees after use.
     */
    void set_color(cairo_t *cr, char *colorarg, char colortype) {
        uint32_t *rgb16 = color_array(colorarg);

        switch(colortype) {
            case 'b': /* Background */
                cairo_set_source_rgb(cr, rgb16[0] / 255.0, rgb16[1] / 255.0, rgb16[2] / 255.0);
                break;
            case 'l': /* Line and text */
                cairo_set_source_rgba(cr, rgb16[0] / 255.0, rgb16[1] / 255.0, rgb16[2] / 255.0, 0.8);
                break;
            case 'f': /* Fill */
                /* Use a lighter tint of the user defined color for circle fill */
                for (int i=0; i < 3; i++) {
                    rgb16[i] = ((255 - rgb16[i]) * .5) + rgb16[i];
                }
                cairo_set_source_rgba(cr, rgb16[0] / 255.0, rgb16[1] / 255.0, rgb16[2] / 255.0, 0.2);
                break;
        }
        free(rgb16);
    }

    if (img) {
        if (!tile) {
            cairo_set_source_surface(xcb_ctx, img, 0, 0);
            cairo_paint(xcb_ctx);
        } else {
            /* create a pattern and fill a rectangle as big as the screen */
            cairo_pattern_t *pattern;
            pattern = cairo_pattern_create_for_surface(img);
            cairo_set_source(xcb_ctx, pattern);
            cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
            cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
            cairo_fill(xcb_ctx);
            cairo_pattern_destroy(pattern);
        }
    } else {
        set_color(xcb_ctx,color,'b'); /* If not image, use color to fill background */
        cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
        cairo_fill(xcb_ctx);
    }

    if (unlock_indicator) {
        cairo_scale(ctx, scaling_factor, scaling_factor);
        /* Draw a (centered) circle with transparent background. */
        cairo_set_line_width(ctx, 3.0);
        cairo_arc(ctx,
                  BUTTON_CENTER /* x */,
                  BUTTON_CENTER /* y */,
                  BUTTON_RADIUS /* radius */,
                  0 /* start */,
                  2 * M_PI /* end */);

        /* Use the appropriate color for the different PAM states
         * (currently verifying, wrong password, or idle) 
         */

        void set_auth_color(char colortype) {
            switch (auth_state) {
                case STATE_AUTH_VERIFY:
                    set_color(ctx,verifycolor,colortype);
                    break;
                case STATE_AUTH_LOCK:
                    set_color(ctx,idlecolor,colortype);
                    break;
                case STATE_AUTH_WRONG:
                    set_color(ctx,wrongcolor,colortype);
                    break;
                case STATE_I3LOCK_LOCK_FAILED:
                    set_color(ctx,wrongcolor,colortype);
                    break;
                case STATE_AUTH_IDLE:
                    if (unlock_state == STATE_BACKSPACE_ACTIVE) {
                        set_color(ctx,wrongcolor,colortype);
                    }
                    else {
                        set_color(ctx,idlecolor,colortype);  
                    }
                    break;
            }
        }

        set_auth_color('f');
        cairo_fill_preserve(ctx);

        /* Circle border */
        set_auth_color('l');
        cairo_stroke(ctx);

        /* Display (centered) Time */
        get_current_timetext(timetext, sizeof(timetext));

        /* Text */
        set_auth_color('l');
        cairo_set_font_size(ctx, 32.0);

        cairo_text_extents_t time_extents;
        double time_x, time_y;
        //cairo_select_font_face(ctx, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

        cairo_text_extents(ctx, timetext, &time_extents);
        time_x = BUTTON_CENTER - ((time_extents.width / 2) + time_extents.x_bearing);
        time_y = BUTTON_CENTER - ((time_extents.height / 2) + time_extents.y_bearing);

        cairo_move_to(ctx, time_x, time_y);
        cairo_show_text(ctx, timetext);
        cairo_close_path(ctx);

        if (auth_state == STATE_AUTH_WRONG && (modifier_string != NULL)) {
            cairo_text_extents_t extents;
            double x, y;

            cairo_set_font_size(ctx, 14.0);

            cairo_text_extents(ctx, modifier_string, &extents);
            x = BUTTON_CENTER - ((extents.width / 2) + extents.x_bearing);
            y = BUTTON_CENTER - ((extents.height / 2) + extents.y_bearing) + 28.0;

            cairo_move_to(ctx, x, y);
            cairo_show_text(ctx, modifier_string);
            cairo_close_path(ctx);
        }

        /* After the user pressed any valid key or the backspace key, we
         * highlight a random part of the unlock indicator to confirm this
         * keypress. */
        if (unlock_state == STATE_KEY_ACTIVE ||
            unlock_state == STATE_BACKSPACE_ACTIVE) {
            cairo_set_line_width(ctx, 4);
            cairo_new_sub_path(ctx);
            double highlight_start = (rand() % (int)(2 * M_PI * 100)) / 100.0;
            cairo_arc(ctx,
                      BUTTON_CENTER /* x */,
                      BUTTON_CENTER /* y */,
                      BUTTON_RADIUS /* radius */,
                      highlight_start,
                      highlight_start + (M_PI / 2.5)); 

            /* Set newly drawn lines to erase what they're drawn over */
            cairo_set_operator(ctx,CAIRO_OPERATOR_CLEAR); 
            cairo_stroke(ctx);

            /* Back to normal operator */
            cairo_set_operator(ctx,CAIRO_OPERATOR_OVER); 
            cairo_set_line_width(ctx, 10);

            /* Change color of separators based on backspace/active keypress */
            set_auth_color('l');

            /* Separator 1 */
            cairo_arc(ctx,
                BUTTON_CENTER /* x */,
                BUTTON_CENTER /* y */,
                BUTTON_RADIUS /* radius */,
                highlight_start /* start */,
                highlight_start + (M_PI / 128.0) /* end */);
            cairo_stroke(ctx);

            /* Separator 2 */
            cairo_arc(ctx,
                BUTTON_CENTER /* x */,
                BUTTON_CENTER /* y */,
                BUTTON_RADIUS /* radius */,
                highlight_start + (M_PI / 2.5) /* start */,
                (highlight_start + (M_PI / 2.5)) + (M_PI / 128.0) /* end */);
            cairo_stroke(ctx);
        }
    }

    if (xr_screens > 0) {
        /* Composite the unlock indicator in the middle of each screen. */
        for (int screen = 0; screen < xr_screens; screen++) {
            int x = (xr_resolutions[screen].x + ((xr_resolutions[screen].width / 2) - (button_diameter_physical / 2)));
            int y = (xr_resolutions[screen].y + ((xr_resolutions[screen].height / 2) - (button_diameter_physical / 2)));
            cairo_set_source_surface(xcb_ctx, output, x, y);
            cairo_rectangle(xcb_ctx, x, y, button_diameter_physical, button_diameter_physical);
            cairo_fill(xcb_ctx);
        }
    } else {
        /* We have no information about the screen sizes/positions, so we just
         * place the unlock indicator in the middle of the X root window and
         * hope for the best. */
        int x = (last_resolution[0] / 2) - (button_diameter_physical / 2);
        int y = (last_resolution[1] / 2) - (button_diameter_physical / 2);
        cairo_set_source_surface(xcb_ctx, output, x, y);
        cairo_rectangle(xcb_ctx, x, y, button_diameter_physical, button_diameter_physical);
        cairo_fill(xcb_ctx);
    }

    cairo_surface_destroy(xcb_output);
    cairo_surface_destroy(output);
    cairo_destroy(ctx);
    cairo_destroy(xcb_ctx);
    return bg_pixmap;
}

/* Calls draw_image on a new pixmap and swaps that with the current pixmap */
void redraw_screen(void) {
    DEBUG("redraw_screen(unlock_state = %d, auth_state = %d)\n", unlock_state, auth_state);
    xcb_pixmap_t bg_pixmap = draw_image(last_resolution);
    xcb_change_window_attributes(conn, win, XCB_CW_BACK_PIXMAP, (uint32_t[1]){bg_pixmap});
    /* XXX: Possible optimization: Only update the area in the middle of the
     * screen instead of the whole screen. */
    xcb_clear_area(conn, 0, win, 0, 0, last_resolution[0], last_resolution[1]);
    xcb_free_pixmap(conn, bg_pixmap);
    xcb_flush(conn);
}

/* Always show unlock indicator. */

void clear_indicator(void) {
    unlock_state = STATE_KEY_PRESSED;
    redraw_screen();
}

/* Periodic redraw for clock updates - taken from github.com/ravinrabbid/i3lock-clock */

/* Schedule redraw even if time jumps. */
static ev_tstamp time_redraw_reschedule_cb(ev_periodic *w, ev_tstamp now) {
    char current[sizeof(timetext)] = {'\0'};
    get_current_timetext(current, sizeof(current));
    if (strcmp(current, timetext) != 0) {
        return now;
    }

    time_t t;
    struct tm tm;
    time(&t);
    localtime_r(&t, &tm);
    return now + (60 - tm.tm_sec);
}

static void time_redraw_cb(struct ev_loop *loop, ev_periodic *w, int revents) {
    redraw_screen();
}

void start_time_redraw_tick(struct ev_loop* main_loop) {
    if (time_redraw_tick) {
        return;
    }
    /* When there is no memory, we just don’t have a timeout. We cannot
     * exit() here, since that would effectively unlock the screen. */
    if (!(time_redraw_tick = calloc(sizeof(struct ev_periodic), 1)))
        return;
    ev_periodic_init(time_redraw_tick, time_redraw_cb, 0, 0, time_redraw_reschedule_cb);
    ev_periodic_start(main_loop, time_redraw_tick);
}
