/* main.c
 *
 * Copyright (C) 2017 Bruno Windels
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <gtk/gtk.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <pthread.h>

typedef struct {
  int event_fd;
  pthread_t bg_thread;
} BackgroundThreadData;

void* background_timer(void* user_data) {
  BackgroundThreadData* bg_data = (BackgroundThreadData*)user_data;
  uint64_t buffer = 1;
  while(TRUE) {
    //signal ui thread
    write(bg_data->event_fd, &buffer, sizeof(buffer));
    sleep(1);
  }
  return NULL;
}

typedef struct {
  GtkApplication* gtk_app;
  GtkWidget* countdown_label;
  uint64_t counter;
  int event_fd;
  BackgroundThreadData bg_data;
} GtkTimerApp;

typedef struct {
  GSource source;
  gpointer event_fd_tag;
  int event_fd;
  void *bg_queue_handle;
} BgThreadSignalSource;

gboolean bg_thread_signal_source_prepare(GSource* source, gint* timeout) {
  //check the queue if messages are available
  //   BgThreadSignalSource* event_fd_source = (BgThreadSignalSource*) source;
  // return has_events(event_fd_source->bg_queue_handle);
  *timeout = -1;
  return FALSE;
}

gboolean bg_thread_signal_source_dispatch(GSource* source, GSourceFunc callback, gpointer user_data) {
  BgThreadSignalSource* event_fd_source = (BgThreadSignalSource*) source;

  unsigned events = g_source_query_unix_fd(source, event_fd_source->event_fd_tag);
  if (events & G_IO_HUP || events & G_IO_ERR || events & G_IO_NVAL) {
    return G_SOURCE_REMOVE;
  }

  gboolean result_continue = G_SOURCE_CONTINUE;
  if (events & G_IO_IN) {
    uint64_t buffer;
    //read the buffer so it doesn't overflow
    read(event_fd_source->event_fd, &buffer, sizeof(buffer));

    result_continue = callback(user_data);
  }
  g_source_set_ready_time(source, -1);
  return result_continue;
}

static GSourceFuncs source_funcs = {
  .prepare = bg_thread_signal_source_prepare,
  .check = NULL,
  .dispatch = bg_thread_signal_source_dispatch,
  .finalize = NULL
};
const static GIOCondition wakeup_conditions = G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL;

GSource* bg_thread_signal_source_new(int event_fd) {
  GSource* source = g_source_new(&source_funcs, sizeof(BgThreadSignalSource));
  BgThreadSignalSource* event_fd_source = (BgThreadSignalSource*) source;
  event_fd_source->event_fd = event_fd;
  event_fd_source->event_fd_tag =
    g_source_add_unix_fd(source, event_fd, wakeup_conditions);
  return source;
}


gboolean update_timer(gpointer user_data) {
  GtkTimerApp* timer_app = (GtkTimerApp*)user_data;
  timer_app->counter += 1;
  gchar text[20];
  g_snprintf((gchar*)&text, 20, "%li", timer_app->counter);
  gtk_label_set_text(GTK_LABEL(timer_app->countdown_label), (gchar*)&text);
  return G_SOURCE_CONTINUE;
}

static void
activate (GtkApplication *app,
          gpointer        user_data)
{

  GtkTimerApp* timer_app = (GtkTimerApp*)user_data;
  GtkWidget *window = gtk_application_window_new (app);

  gtk_window_set_title (GTK_WINDOW (window), "gtimer");
  gtk_window_set_default_size (GTK_WINDOW (window), 200, 200);

  /* You can add GTK+ widgets to your window here.
   * See https://developer.gnome.org/ for help.
   */
  timer_app->event_fd = eventfd(0, EFD_NONBLOCK);
	GSource* s = bg_thread_signal_source_new(timer_app->event_fd);
  g_source_attach(s, g_main_context_default());
  g_source_set_callback(s, update_timer, timer_app, NULL);

  timer_app->countdown_label = gtk_label_new("countdown!");
  gtk_container_add(GTK_CONTAINER(window), timer_app->countdown_label);

  gtk_widget_show_all (window);

  timer_app->bg_data.event_fd = timer_app->event_fd;
  pthread_create(&timer_app->bg_data.bg_thread, NULL, background_timer, &timer_app->bg_data );
}

int main(int   argc,
         char *argv[])
{
  GtkApplication* gtk_app = gtk_application_new ("org.gnome.Gtimer", G_APPLICATION_FLAGS_NONE);

  GtkTimerApp timer_app = {
    .gtk_app = gtk_app,
    .counter = 0,
    .countdown_label = NULL
  };

  g_signal_connect(gtk_app, "activate", G_CALLBACK (activate), (gpointer)&timer_app);
  int status = g_application_run (G_APPLICATION (gtk_app), argc, argv);

  //pthread_join(&timer_app->bg_data.bg_thread);

  return status;
}
