// Copyright [2012] <GTRI>
#ifndef NETNMEA_NETNMEADLL_H_
#define NETNMEA_NETNMEADLL_H_


#if !defined(NETNMEA_DLL)
#  if defined(WIN32) && defined(BUILD_NETNMEA)
#    define NETNMEA_DLL     __declspec(dllexport)
#  else if defined(WIN32)
#    define NETNMEA_DLL     __declspec(dllimport)
#  endif
#  if !defined(WIN32)
#    define NETNMEA_DLL
#  endif
#endif

#endif  // NETNMEA_NETNMEADLL_H_
