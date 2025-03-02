# SPDX-FileCopyrightText: 2008 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(INC
  PUBLIC .
  ../makesrna
  ../../../intern/locale
)

set(INC_SYS
)

set(SRC
  intern/blt_lang.cc
  intern/blt_translation.cc

  BLT_lang.hh
  BLT_translation.hh
)

set(LIB
  PRIVATE bf::blenkernel
  PRIVATE bf::blenlib
  PRIVATE bf::dna
  PRIVATE bf::imbuf
  PRIVATE bf::intern::guardedalloc
)

if(WITH_INTERNATIONAL)
  add_definitions(-DWITH_INTERNATIONAL)
  list(APPEND LIB
    bf_intern_locale
  )
endif()

if(WITH_PYTHON)
  add_definitions(-DWITH_PYTHON)
  list(APPEND INC
    ../python
  )
endif()

if(WITH_INPUT_IME)
  add_definitions(-DWITH_INPUT_IME)
endif()

blender_add_lib(bf_blentranslation "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
add_library(bf::blentranslation ALIAS bf_blentranslation)

if(WITH_INTERNATIONAL)
  add_subdirectory(msgfmt)
endif()
