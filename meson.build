project('gtk-timer', 'c')
sources = ['src/main.c']
gtkdep = dependency('gtk+-3.0')
thread_dep = dependency('threads')
executable('gtk-timer', sources, dependencies: [gtkdep, thread_dep])

