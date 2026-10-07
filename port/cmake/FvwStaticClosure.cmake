# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# fvw_collect_static_libs(<target> <out_var>) — the static libraries in a
# target's link closure, for merging into the one archive an Xcode app links
# (Pippin's libpippin_core.a, Peregrine's libperegrine_core.a).
# Walk the link graph and collect every static library in it. CMake has no
# "give me the transitive closure" query, so this is the closure — visited
# set and all — computed at configure time. Anything that is not a static
# library we build (an imported .tbd like SQLite, a `-framework` flag, an
# INTERFACE shim like fv_compat) is NOT an archive and is skipped: those are
# link-line arguments, recorded below for the Xcode target to repeat.
function(fvw_collect_static_libs target out_var)
  set(_seen "")
  set(_stack "${target}")
  set(_libs "")
  while(_stack)
    list(POP_FRONT _stack _t)
    if(NOT TARGET ${_t})
      continue()
    endif()
    if(_t IN_LIST _seen)
      continue()
    endif()
    list(APPEND _seen ${_t})
    get_target_property(_type ${_t} TYPE)
    if(_type STREQUAL "STATIC_LIBRARY")
      list(APPEND _libs ${_t})
    endif()
    foreach(_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
      get_target_property(_deps ${_t} ${_prop})
      if(_deps)
        foreach(_d IN LISTS _deps)
          # Strip the $<LINK_ONLY:...> wrapper PRIVATE deps arrive in.
          string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" _d "${_d}")
          if(TARGET ${_d})
            list(APPEND _stack ${_d})
          endif()
        endforeach()
      endif()
    endforeach()
  endwhile()
  set(${out_var} "${_libs}" PARENT_SCOPE)
endfunction()
