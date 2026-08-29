#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# survey.sh <dir> — inventory Win32/MFC/COM constructs in a module before porting.
# Output pairs with the strategy table in
# .claude/skills/port-module/references/win32-porting-playbook.md
[ -d "$1" ] || { echo "usage: $0 <module-dir>"; exit 1; }

survey() {
  n=$(grep -rl --include='*.cpp' --include='*.h' -E "$2" "$1" 2>/dev/null | wc -l | tr -d ' ')
  [ "$n" != "0" ] && printf '%-28s %3s files   -> %s\n' "$3" "$n" "$4"
}

survey "$1" 'CString'                                   'CString'          'REWRITE if <15 uses, else fv_cstring.h'
survey "$1" '_bstr_t|BSTR'                              'BSTR/_bstr_t'     'SEVER (COM); typedef std::string if incidental'
survey "$1" 'VARIANT|SAFEARRAY'                         'VARIANT/SAFEARRAY' 'SEVER; portable twin takes std::vector'
survey "$1" 'wchar_t|WCHAR|WideCharToMultiByte'         'wide strings'     'stay narrow/UTF-8; helpers if converting'
survey "$1" 'CoCreateInstance|IDispatch'                'COM activation'   'SEVER: interface + factory'
survey "$1" 'CreateFileMapping|MapViewOfFile'           'file mapping'     'fv_filemap.h (mmap RAII)'
survey "$1" '\bCreateFile\b'                            'CreateFile'       'REWRITE to fopen unless overlapped/locking'
survey "$1" '\bCFile\b|CStdioFile'                      'CFile'            'REWRITE to FILE*/fstream'
survey "$1" 'FindFirstFile'                             'FindFirstFile'    'SHIM readdir walker'
survey "$1" 'CArray<|CList<|CMap<|CPtrArray|CStringArray' 'MFC containers' 'REWRITE to std:: containers'
survey "$1" 'CRITICAL_SECTION'                          'CRITICAL_SECTION' 'shimmed already'
survey "$1" 'CreateEvent|WaitForSingleObject|CreateMutex' 'events/mutexes' 'fv_event.h when first needed'
survey "$1" 'CreateThread|_beginthread|AfxBeginThread'  'threads'          'per-site: sever, sync, or pthreads guard'
survey "$1" 'Interlocked'                               'Interlocked*'     'SHIM __atomic builtins'
survey "$1" 'GetTickCount|QueryPerformanceCounter|timeGetTime' 'timers'    'SHIM CLOCK_MONOTONIC (keep wraparound)'
survey "$1" 'GetLastError|SetLastError'                 'GetLastError'     'SHIM errno-backed; GUARD ERROR_* compares'
survey "$1" '\bHDC\b|\bCDC\b|BitBlt|StretchBlt'         'GDI drawing'      'SEVER (Phase 5 rendering)'
survey "$1" 'BITMAPINFO|RGBQUAD|HBITMAP'                'DIB structs'      'SHIM byte layouts (data, not GDI)'
survey "$1" 'COLORREF'                                  'COLORREF'         'SHIM DWORD + RGB macros'
survey "$1" 'CRect\b|CPoint\b|CSize\b'                  'CRect/CPoint/CSize' 'SHIM minimal geometry structs'
survey "$1" 'RegOpenKey|RegQueryValue|HKEY_'            'registry'         'GUARD: env var / ctor arg / in-memory map'
survey "$1" 'LoadLibrary|GetProcAddress'                'LoadLibrary'      'SHIM dlopen/dlsym'
survey "$1" 'MessageBox|AfxMessageBox'                  'MessageBox'       'stderr stub; GUARD if result branches'
survey "$1" '_access|_mkdir|_stat\b|_unlink'            'CRT file utils'   'SHIM posix renames'
survey "$1" 'sscanf_s|sprintf_s|strcpy_s|strncpy_s|fopen_s' 'secure CRT'   'shimmed already'
survey "$1" 'GetModuleFileName'                         'GetModuleFileName' 'REWRITE per-case'
exit 0
