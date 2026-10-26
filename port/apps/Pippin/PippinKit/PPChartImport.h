// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * PPChartImport.h — installs NOAA ENC zips into the app's chart store.
 *
 * Wraps `pippin::ChartStore` (PPChartStore.h). Not tied to any queue: an
 * import is slow and runs off the main thread; the map picks up the new set
 * with `-[PPMap openChartsAtURL:error:]`.
 */
#import <Foundation/Foundation.h>
#import <PippinKit/PPGeometry.h>

NS_ASSUME_NONNULL_BEGIN

/// What an import installed.
NS_SWIFT_SENDABLE
@interface PPChartSet : NSObject

- (instancetype)init NS_UNAVAILABLE;

@property(nonatomic, readonly) NSURL *directoryURL;
/// Cells drawn: readable and not cancelled.
@property(nonatomic, readonly) NSInteger cellCount;
/// Usage bands present, ascending (1 overview … 6 berthing).
@property(nonatomic, readonly) NSArray<NSNumber *> *bands;
/// Cells that could not be read, relative to `directoryURL`.
@property(nonatomic, readonly) NSArray<NSString *> *unreadableCells;
/// Cell names an update withdrew; they are not drawn.
@property(nonatomic, readonly) NSArray<NSString *> *cancelledCells;
/// Cell names drawn without some of their update files.
@property(nonatomic, readonly) NSArray<NSString *> *outdatedCells;
/// Update files applied, and ones that did not apply, over every cell.
@property(nonatomic, readonly) NSInteger updatesApplied;
@property(nonatomic, readonly) NSInteger updatesFailed;
/// Union of the cells' coverage.
@property(nonatomic, readonly) PPGeoBounds bounds;

@end

/// What NOAA's product catalog says about the installed set.
NS_SWIFT_SENDABLE
@interface PPChartUpdateCheck : NSObject

- (instancetype)init NS_UNAVAILABLE;

/// NOAA region id the set was compared as ("FL", "07CGD"); nil when the
/// catalog shares no cell with it.
@property(nonatomic, readonly, copy, nullable) NSString *regionID;
/// Cell names with a newer edition, with later updates, cancelled by NOAA
/// but still drawn, and listed for the region but not installed.
@property(nonatomic, readonly) NSArray<NSString *> *editionCells;
@property(nonatomic, readonly) NSArray<NSString *> *updatedCells;
@property(nonatomic, readonly) NSArray<NSString *> *cancelledCells;
@property(nonatomic, readonly) NSArray<NSString *> *addedCells;
/// Re-downloading the region would change nothing drawn.
@property(nonatomic, readonly) BOOL upToDate;

@end

/// The installed chart sets under one directory.
NS_SWIFT_SENDABLE
@interface PPChartStore : NSObject

- (instancetype)init NS_UNAVAILABLE;
- (instancetype)initWithDirectoryURL:(NSURL *)url NS_DESIGNATED_INITIALIZER;

/// The S-57 catalogue used to build a set's named-object table at import
/// (`PPMap.chartCatalogURL`). Without it the table is built on first search.
@property(nonatomic, copy, nullable) NSURL *catalogDirectoryURL;

/// The installed set's directory, or nil when none is installed.
@property(nonatomic, readonly, nullable) NSURL *currentSetURL;

/// Unpacks, indexes and installs `zipURL`. `progress` gets a fraction in
/// [0, 1] on the calling thread; returning NO cancels. On failure the
/// previous set stays installed.
- (nullable PPChartSet *)importZipAtURL:(NSURL *)zipURL
                               progress:(BOOL (^_Nullable)(double fraction))progress
                                  error:(NSError **)error
    NS_SWIFT_NAME(importZip(at:progress:));

/// Compares the installed set with NOAA's ENCProdCat.xml at `catalogURL`.
/// Fails when no set is installed or the catalog cannot be read.
- (nullable PPChartUpdateCheck *)checkForUpdatesWithCatalogAtURL:(NSURL *)catalogURL
                                                           error:(NSError **)error
    NS_SWIFT_NAME(checkForUpdates(catalogAt:));

/// Deletes every set but the current one; call after the map has opened it.
- (void)removeStaleSets;

@end

NS_ASSUME_NONNULL_END
