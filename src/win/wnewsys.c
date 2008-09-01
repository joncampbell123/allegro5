/*         ______   ___    ___ 
 *        /\  _  \ /\_ \  /\_ \ 
 *        \ \ \L\ \\//\ \ \//\ \      __     __   _ __   ___ 
 *         \ \  __ \ \ \ \  \ \ \   /'__`\ /'_ `\/\`'__\/ __`\
 *          \ \ \/\ \ \_\ \_ \_\ \_/\  __//\ \L\ \ \ \//\ \L\ \
 *           \ \_\ \_\/\____\/\____\ \____\ \____ \ \_\\ \____/
 *            \/_/\/_/\/____/\/____/\/____/\/___L\ \/_/ \/___/
 *                                           /\____/
 *                                           \_/__/
 *
 *      New Windows system driver
 *
 *      Based on the X11 OpenGL driver by Elias Pschernig.
 *
 *      Heavily modified by Trent Gamblin.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "allegro5/allegro5.h"
#include "allegro5/internal/aintern.h"
#include "allegro5/internal/aintern_bitmap.h"
#include "allegro5/internal/aintern_memory.h"
#include "allegro5/internal/aintern_system.h"
#include "allegro5/platform/aintwin.h"
#include "allegro5/platform/alplatf.h"

#include "allegro5/winalleg.h"

#include "win_new.h"

#ifndef SCAN_DEPEND
   #include <mmsystem.h>
#endif


static ALLEGRO_SYSTEM_INTERFACE *vt = 0;
static bool using_higher_res_timer;

CRITICAL_SECTION allegro_critical_section;

ALLEGRO_SYSTEM_WIN *_al_win_system;


/* _WinMain:
 *  Entry point for Windows GUI programs, hooked by a macro in alwin.h,
 *  which makes it look as if the application can still have a normal
 *  main() function.
 */
int _WinMain(void *_main, void *hInst, void *hPrev, char *Cmd, int nShow)
{
   int (*mainfunc) (int argc, char *argv[]) = (int (*)(int, char *[]))_main;
   char *argbuf;
   char *cmdline;
   char **argv;
   int argc;
   int argc_max;
   int i, q;

   /* can't use parameter because it doesn't include the executable name */
   cmdline = GetCommandLine();
   i = strlen(cmdline) + 1;
   argbuf = _AL_MALLOC(i);
   memcpy(argbuf, cmdline, i);

   argc = 0;
   argc_max = 64;
   argv = _AL_MALLOC(sizeof(char *) * argc_max);
   if (!argv) {
      _AL_FREE(argbuf);
      return 1;
   }

   i = 0;

   /* parse commandline into argc/argv format */
   while (argbuf[i]) {
      while ((argbuf[i]) && (uisspace(argbuf[i])))
	 i++;

      if (argbuf[i]) {
	 if ((argbuf[i] == '\'') || (argbuf[i] == '"')) {
	    q = argbuf[i++];
	    if (!argbuf[i])
	       break;
	 }
	 else
	    q = 0;

	 argv[argc++] = &argbuf[i];

         if (argc >= argc_max) {
            argc_max += 64;
            argv = _AL_REALLOC(argv, sizeof(char *) * argc_max);
            if (!argv) {
               _AL_FREE(argbuf);
               return 1;
            }
         }

	 while ((argbuf[i]) && ((q) ? (argbuf[i] != q) : (!uisspace(argbuf[i]))))
	    i++;

	 if (argbuf[i]) {
	    argbuf[i] = 0;
	    i++;
	 }
      }
   }

   argv[argc] = NULL;

   /* call the application entry point */
   i = mainfunc(argc, argv);

   _AL_FREE(argv);
   _AL_FREE(argbuf);

   return i;
}




/* Create a new system object for the dummy D3D driver. */
static ALLEGRO_SYSTEM *win_initialize(int flags)
{
   _al_win_system = _AL_MALLOC(sizeof *_al_win_system);
   memset(_al_win_system, 0, sizeof *_al_win_system);

   /* setup general critical section */
   InitializeCriticalSection(&allegro_critical_section);

   // Request a 1ms resolution from our timer
   if (timeBeginPeriod(1) != TIMERR_NOCANDO) {
      using_higher_res_timer = true;
   }
   _al_win_init_time();

   _win_input_init(TRUE);

   _al_win_init_window();

   _al_vector_init(&_al_win_system->system.displays, sizeof (ALLEGRO_SYSTEM_WIN *));

   _al_win_system->system.vt = vt;

#if defined ALLEGRO_CFG_D3D
   if (_al_d3d_init_display() != true)
      return NULL;
#endif
   
   return &_al_win_system->system;
}


static void win_shutdown(void)
{
   /* Disabled because seems to cause deadlocks. */
#if 0
   /* Close all open displays. */
   ALLEGRO_SYSTEM *s = al_system_driver();
   while (_al_vector_size(&s->displays) > 0) {
      ALLEGRO_DISPLAY **dptr = _al_vector_ref(&s->displays, 0);
      ALLEGRO_DISPLAY *d = *dptr;
      _al_destroy_display_bitmaps(d);
      al_destroy_display(d);
   }
#endif

   if (using_higher_res_timer) {
      timeEndPeriod(1);
   }
}


/* FIXME: autodetect a driver */
static ALLEGRO_DISPLAY_INTERFACE *win_get_display_driver(void)
{
   int flags = al_get_new_display_flags();
   ALLEGRO_SYSTEM *sys = al_system_driver();
   AL_CONST char *s;

   if (sys->config) {
      s = al_config_get_value(sys->config, "gfx", "driver");
      if (s) {
         if (!stricmp(s, "OPENGL"))
            return _al_display_wgl_driver();
         else if (!stricmp(s, "DIRECT3D") || !stricmp(s, "D3D"))
            return _al_display_d3d_driver();
      }
   }

   if (flags & ALLEGRO_OPENGL) {
#if defined ALLEGRO_CFG_OPENGL
      return _al_display_wgl_driver();
#endif
   }
   else {
#if defined ALLEGRO_CFG_D3D
      return _al_display_d3d_driver();
#endif
   }


   /* FIXME: should default be set in the flags? */
#if defined ALLEGRO_CFG_D3D
   return _al_display_d3d_driver();
#elif defined ALLEGRO_CFG_OPENGL
   return _al_display_wgl_driver();
#else
   #error Neither ALLEGRO_CFG_D3D or ALLEGRO_CFG_OPENGL are defined!
#endif
}

/* FIXME: use the list */
static ALLEGRO_KEYBOARD_DRIVER *win_get_keyboard_driver(void)
{
   return _al_keyboard_driver_list[0].driver;
}

static ALLEGRO_JOYSTICK_DRIVER *win_get_joystick_driver(void)
{
   return _al_joystick_driver_list[0].driver;
}

static int win_get_num_display_modes(void)
{
   int format = al_get_new_display_format();
   int refresh_rate = al_get_new_display_refresh_rate();
   int flags = al_get_new_display_flags();

   if (!(flags & ALLEGRO_FULLSCREEN))
      return -1;

   if (flags & ALLEGRO_OPENGL) {
#if defined ALLEGRO_CFG_OPENGL
      return _al_wgl_get_num_display_modes(format, refresh_rate, flags);
#endif
   }
   else {
#if defined ALLEGRO_CFG_D3D
      return _al_d3d_get_num_display_modes(format, refresh_rate, flags);
#endif
   }

   return 0;
}

static ALLEGRO_DISPLAY_MODE *win_get_display_mode(int index,
   ALLEGRO_DISPLAY_MODE *mode)
{
   int format = al_get_new_display_format();
   int refresh_rate = al_get_new_display_refresh_rate();
   int flags = al_get_new_display_flags();

   if (!(flags & ALLEGRO_FULLSCREEN))
      return NULL;

   if (flags & ALLEGRO_OPENGL) {
#if defined ALLEGRO_CFG_OPENGL
      return _al_wgl_get_display_mode(index, format, refresh_rate, flags, mode);
#endif
   }
   else {
#if defined ALLEGRO_CFG_D3D
      return _al_d3d_get_display_mode(index, format, refresh_rate, flags, mode);
#endif
   }


   return NULL;
}

static int win_get_num_video_adapters(void)
{
   int flags = al_get_new_display_flags();

#if defined ALLEGRO_CFG_OPENGL
   if (flags & ALLEGRO_OPENGL) {
      return _al_wgl_get_num_video_adapters();
   }
#endif

#if defined ALLEGRO_CFG_D3D
   return _al_d3d_get_num_video_adapters();
#endif

   return 0;
}

static void win_get_monitor_info(int adapter, ALLEGRO_MONITOR_INFO *info)
{
   int flags = al_get_new_display_flags();

#if defined ALLEGRO_CFG_OPENGL
   if (flags & ALLEGRO_OPENGL) {
      _al_wgl_get_monitor_info(adapter, info);
   }
#endif

#if defined ALLEGRO_CFG_D3D
   _al_d3d_get_monitor_info(adapter, info);
#endif
}

static bool win_get_cursor_position(int *ret_x, int *ret_y)
{
   POINT p;
   GetCursorPos(&p);
   *ret_x = p.x;
   *ret_y = p.y;
   return true;
}


static ALLEGRO_MOUSE_DRIVER *win_get_mouse_driver(void)
{
   return _al_mouse_driver_list[0].driver;
}


ALLEGRO_SYSTEM_INTERFACE *_al_system_win_driver(void)
{
   if (vt) return vt;

   vt = _AL_MALLOC(sizeof *vt);
   memset(vt, 0, sizeof *vt);

   vt->initialize = win_initialize;
   vt->get_display_driver = win_get_display_driver;
   vt->get_keyboard_driver = win_get_keyboard_driver;
   vt->get_mouse_driver = win_get_mouse_driver;
   vt->get_joystick_driver = win_get_joystick_driver;
   vt->get_num_display_modes = win_get_num_display_modes;
   vt->get_display_mode = win_get_display_mode;
   vt->shutdown_system = win_shutdown;
   vt->get_num_video_adapters = win_get_num_video_adapters;
   vt->get_monitor_info = win_get_monitor_info;
   vt->get_cursor_position = win_get_cursor_position;

   TRACE("ALLEGRO_SYSTEM_INTERFACE created.\n");

   return vt;
}

void _al_register_system_interfaces()
{
   ALLEGRO_SYSTEM_INTERFACE **add;

#if defined ALLEGRO_CFG_D3D || defined ALLEGRO_CFG_OPENGL
   add = _al_vector_alloc_back(&_al_system_interfaces);
   *add = _al_system_win_driver();
#endif
}

