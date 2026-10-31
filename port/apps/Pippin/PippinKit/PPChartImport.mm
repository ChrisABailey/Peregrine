// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPChartImport.h"

#import "PPMap.h"

#include <memory>
#include <string>
#include <vector>

#include "PPChartStore.h"

static NSArray<NSString*>* StringArray(const std::vector<std::string>& v) {
  NSMutableArray<NSString*>* out = [NSMutableArray arrayWithCapacity:v.size()];
  for (const std::string& u : v) [out addObject:[NSString stringWithUTF8String:u.c_str()]];
  return [out copy];
}

@implementation PPChartSet

- (instancetype)initWithResult:(const pippin::ChartImportResult&)r {
  self = [super init];
  if (self == nil) return nil;
  _directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:r.set_dir.c_str()]
                             isDirectory:YES];
  _cellCount = static_cast<NSInteger>(r.cell_count);
  NSMutableArray<NSNumber*>* bands = [NSMutableArray array];
  for (int b : r.bands) [bands addObject:@(b)];
  _bands = [bands copy];
  _unreadableCells = StringArray(r.unreadable);
  _cancelledCells = StringArray(r.cancelled);
  _outdatedCells = StringArray(r.outdated);
  _updatesApplied = static_cast<NSInteger>(r.updates_applied);
  _updatesFailed = static_cast<NSInteger>(r.updates_failed);
  _bounds.southWest = PPGeoPointMake(r.bounds.ll.lat, r.bounds.ll.lon);
  _bounds.northEast = PPGeoPointMake(r.bounds.ur.lat, r.bounds.ur.lon);
  return self;
}

@end

@implementation PPChartUpdateCheck

- (instancetype)initWithComparison:(const fv::EncCatalogComparison&)c {
  self = [super init];
  if (self == nil) return nil;
  _regionID = c.region.empty() ? nil : [NSString stringWithUTF8String:c.region.c_str()];
  _editionCells = StringArray(c.new_editions);
  _updatedCells = StringArray(c.new_updates);
  _cancelledCells = StringArray(c.cancelled);
  _addedCells = StringArray(c.added);
  _upToDate = c.up_to_date();
  return self;
}

@end

@implementation PPChartStore {
  std::unique_ptr<pippin::ChartStore> _store;
}

- (instancetype)initWithDirectoryURL:(NSURL*)url {
  self = [super init];
  if (self == nil) return nil;
  _store = std::make_unique<pippin::ChartStore>(url.fileSystemRepresentation ?: "");
  return self;
}

- (void)setCatalogDirectoryURL:(NSURL*)url {
  _catalogDirectoryURL = [url copy];
  _store->SetCatalogDir(url.fileSystemRepresentation ?: "");
}

- (nullable NSURL*)currentSetURL {
  const std::string dir = _store->CurrentSet();
  if (dir.empty()) return nil;
  return [NSURL fileURLWithPath:[NSString stringWithUTF8String:dir.c_str()]
                    isDirectory:YES];
}

- (nullable PPChartSet*)importZipAtURL:(NSURL*)zipURL
                              progress:(BOOL (^_Nullable)(double))progress
                                 error:(NSError**)error {
  pippin::ChartImportResult result;
  const fv::Status s = _store->Import(
      zipURL.fileSystemRepresentation ?: "", &result,
      [progress](double f) { return progress == nil || progress(f) ? true : false; });
  if (!s.ok()) {
    if (error) {
      NSString* message = [NSString stringWithUTF8String:s.message.c_str()];
      *error = [NSError errorWithDomain:PPErrorDomain
                                   code:PPErrorChartImportFailed
                               userInfo:@{NSLocalizedDescriptionKey : message}];
    }
    return nil;
  }
  return [[PPChartSet alloc] initWithResult:result];
}

- (nullable PPChartUpdateCheck*)checkForUpdatesWithCatalogAtURL:(NSURL*)catalogURL
                                                          error:(NSError**)error {
  fv::EncCatalogComparison comparison;
  const fv::Status s =
      _store->CheckForUpdates(catalogURL.fileSystemRepresentation ?: "", &comparison);
  if (!s.ok()) {
    if (error) {
      NSString* message = [NSString stringWithUTF8String:s.message.c_str()];
      *error = [NSError errorWithDomain:PPErrorDomain
                                   code:PPErrorChartImportFailed
                               userInfo:@{NSLocalizedDescriptionKey : message}];
    }
    return nil;
  }
  return [[PPChartUpdateCheck alloc] initWithComparison:comparison];
}

- (void)removeStaleSets {
  _store->RemoveStaleSets();
}

@end
