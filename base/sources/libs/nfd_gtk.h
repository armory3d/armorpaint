#pragma once

// GTK+ 3 loaded at runtime

#define G_DISABLE_CAST_CHECKS
#include <dlfcn.h>
#include <gtk/gtk.h>
#include <stdbool.h>

#define IRON_GTK_FUNCS(X)                             \
	X(g_free)                                         \
	X(g_main_context_iteration)                       \
	X(g_main_context_pending)                         \
	X(g_slist_free)                                   \
	X(g_slist_length)                                 \
	X(gtk_dialog_run)                                 \
	X(gtk_disable_setlocale)                          \
	X(gtk_events_pending)                             \
	X(gtk_file_chooser_add_filter)                    \
	X(gtk_file_chooser_dialog_new)                    \
	X(gtk_file_chooser_get_filename)                  \
	X(gtk_file_chooser_get_filenames)                 \
	X(gtk_file_chooser_set_current_folder)            \
	X(gtk_file_chooser_set_do_overwrite_confirmation) \
	X(gtk_file_chooser_set_select_multiple)           \
	X(gtk_file_filter_add_pattern)                    \
	X(gtk_file_filter_new)                            \
	X(gtk_file_filter_set_name)                       \
	X(gtk_init)                                       \
	X(gtk_init_check)                                 \
	X(gtk_main_iteration)                             \
	X(gtk_message_dialog_new)                         \
	X(gtk_widget_destroy)                             \
	X(gtk_widget_realize)                             \
	X(gtk_window_set_title)

#undef g_free

#define IRON_GTK_PTR(f) static __typeof__(f) *iron_dl_##f;
IRON_GTK_FUNCS(IRON_GTK_PTR)
#undef IRON_GTK_PTR

// Returns false when GTK+ 3 is not installed
static __attribute__((unused)) bool iron_gtk_load(void) {
	static int loaded = 0; // 0 - not tried, 1 - ok, -1 - failed
	if (loaded != 0) {
		return loaded == 1;
	}
	loaded    = -1;
	void *lib = dlopen("libgtk-3.so.0", RTLD_NOW | RTLD_GLOBAL);
	if (lib == NULL) {
		return false;
	}
#define IRON_GTK_SYM(f)                            \
	iron_dl_##f = (__typeof__(f) *)dlsym(lib, #f); \
	if (iron_dl_##f == NULL) {                     \
		return false;                              \
	}
	IRON_GTK_FUNCS(IRON_GTK_SYM)
#undef IRON_GTK_SYM
	loaded = 1;
	return true;
}

#define g_free                                         iron_dl_g_free
#define g_main_context_iteration                       iron_dl_g_main_context_iteration
#define g_main_context_pending                         iron_dl_g_main_context_pending
#define g_slist_free                                   iron_dl_g_slist_free
#define g_slist_length                                 iron_dl_g_slist_length
#define gtk_dialog_run                                 iron_dl_gtk_dialog_run
#define gtk_disable_setlocale                          iron_dl_gtk_disable_setlocale
#define gtk_events_pending                             iron_dl_gtk_events_pending
#define gtk_file_chooser_add_filter                    iron_dl_gtk_file_chooser_add_filter
#define gtk_file_chooser_dialog_new                    iron_dl_gtk_file_chooser_dialog_new
#define gtk_file_chooser_get_filename                  iron_dl_gtk_file_chooser_get_filename
#define gtk_file_chooser_get_filenames                 iron_dl_gtk_file_chooser_get_filenames
#define gtk_file_chooser_set_current_folder            iron_dl_gtk_file_chooser_set_current_folder
#define gtk_file_chooser_set_do_overwrite_confirmation iron_dl_gtk_file_chooser_set_do_overwrite_confirmation
#define gtk_file_chooser_set_select_multiple           iron_dl_gtk_file_chooser_set_select_multiple
#define gtk_file_filter_add_pattern                    iron_dl_gtk_file_filter_add_pattern
#define gtk_file_filter_new                            iron_dl_gtk_file_filter_new
#define gtk_file_filter_set_name                       iron_dl_gtk_file_filter_set_name
#define gtk_init                                       iron_dl_gtk_init
#define gtk_init_check                                 iron_dl_gtk_init_check
#define gtk_main_iteration                             iron_dl_gtk_main_iteration
#define gtk_message_dialog_new                         iron_dl_gtk_message_dialog_new
#define gtk_widget_destroy                             iron_dl_gtk_widget_destroy
#define gtk_widget_realize                             iron_dl_gtk_widget_realize
#define gtk_window_set_title                           iron_dl_gtk_window_set_title
