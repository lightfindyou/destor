#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.71])
AC_INIT([destor],[2.0],[fumin@hust.edu.cn])
AM_INIT_AUTOMAKE
#AM_INIT_AUTOMAKE([destor], [2.0])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
CFLAGS="-g3 -O2 -Wall -ggdb"
CXXFLAGS="-ggdb -O2 -Wall -D_GLIBCXX_USE_CXX11_ABI=0 -I/home/xzjin/libtorch_pre/include -I/home/xzjin/libtorch_pre/include/torch/csrc/api/include -L/home/xzjin/libtorch_pre/lib"
#" -lpthread -lcrypto -lglib -lxxhash -lm -lc10 -ltorch -ltorch_cpu"
#LIBS=-lpthread -lcrypto -lglib -lxxhash -lm
#-D_GLIBCXX_USE_CXX11_ABI=0

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([glib-2.0], [g_hash_table_new],[found_glib=yes],[found_glib=no])
    if test "$found_glib" = "no"; then
    AC_MSG_ERROR([*** Working glib library not found ***])
    fi
AC_CHECK_LIB([glib], [g_thread_init],[thread_safe=no],[thread_safe=yes])
    if test "$thread_safe" = "no"; then
    AC_MSG_ERROR([*** The version of glib is too low ***])
    fi
AC_CHECK_LIB([crypto],[SHA1_Update],[found_crypto=yes],[found_crypto=no])
    if test "$found_crypto" = "no"; then
    AC_MSG_ERROR([*** Working crypto library not found ***])
    fi
#AC_CHECK_LIB([mysqlclient],[mysql_init],[found_mysql=yes],[found_mysql=no])
    #if test "$found_mysql" = "no"; then
    #AC_MSG_ERROR([*** Working mysql library not found ***])
    #fi
AC_PROG_RANLIB

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])
AC_CHECK_HEADERS([glib.h],[found_glib_h=yes],[found_glib_h=no])
    if test "$found_glib_h" = "no"; then
    AC_MSG_ERROR([*** Working glib.h header not found ***])
    fi
AC_CHECK_HEADERS([openssl/sha.h], [found_sha_h=yes],[found_sha_h=no])
    if test "$found_sha_h" = "no"; then
    AC_MSG_ERROR([*** Working sha.h header not found ***])
    fi
#AC_CHECK_HEADERS([torch/script.h], [found_torchScript_h=yes],[found_torchScript_h=no])
#    if test "$found_torchScript_h" = "no"; then
#    AC_MSG_ERROR([*** Working torch/script.h header not found ***])
#    fi

#AC_CHECK_HEADERS([mysql/mysql.h], [found_mysql_h=yes],[found_mysql_h=no])
    #if test "$found_mysql_h" = "no"; then
    #AC_MSG_ERROR([*** Working mysql/mysql.h header not found ***])
    #fi

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
#AC_CHECK_FUNCS([gettimeofday memmove memset socket strerror])

#        src/featuring/deepsketch/Makefile
#        src/similariting/deepsketch/Makefile
#        src/similariting/deepsketch/NGT/Makefile

AC_CONFIG_FILES([Makefile
        src/Makefile
        src/utils/Makefile
        src/index/Makefile
        src/recipe/Makefile
        src/storage/Makefile
        src/chunking/Makefile
        src/featuring/Makefile
        src/featuring/deepsketch/Makefile
        src/similariting/Makefile
        src/xdelta3/Makefile
        src/fsl/Makefile])
AC_OUTPUT
