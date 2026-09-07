// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#import "PPPoint+Internal.h"

#include <string>

NSString* const PPPointShapeCircle = @"circle";
NSString* const PPPointShapeSquare = @"square";
NSString* const PPPointShapeTriangle = @"triangle";
NSString* const PPPointShapeDiamond = @"diamond";
NSString* const PPPointShapeCross = @"cross";
NSString* const PPPointShapeStar = @"star";

NSArray<NSString*>* PPPointShapeNames(void) {
  // `fv::ToString(PointShape)`'s own spellings, in `PointShape`'s own order.
  // Written out rather than looped over the enum because the enum has no count
  // and an ObjC file inventing one would be the sort of thing that silently
  // stops covering a seventh shape.
  return @[
    PPPointShapeCircle, PPPointShapeSquare, PPPointShapeTriangle,
    PPPointShapeDiamond, PPPointShapeCross, PPPointShapeStar
  ];
}

namespace {

NSString* Str(const std::string& s) {
  NSString* out = [NSString stringWithUTF8String:s.c_str()];
  // A column that is not valid UTF-8 comes back nil, and a nil in a `copy`
  // property is a crash three screens later. Empty is the same thing a missing
  // column means, so it is the honest fallback.
  return out != nil ? out : @"";
}

std::string Cxx(NSString* _Nullable s) {
  if (s == nil) return std::string();
  const char* utf8 = s.UTF8String;
  return utf8 != nullptr ? std::string(utf8) : std::string();
}

}  // namespace

@implementation PPPointSymbol

- (instancetype)initWithPointSymbol:(const fv::PointSymbol&)symbol {
  self = [super init];
  if (self == nil) return nil;
  _symbolId = symbol.id;
  _name = Str(symbol.name);
  // COPIED, not referenced: `NSData dataWithBytes:` takes its own copy, which
  // is what makes this value safe to hold on the main thread while the render
  // queue edits the palette out from under it.
  _imageData = symbol.image.empty()
                   ? [NSData data]
                   : [NSData dataWithBytes:symbol.image.data()
                                    length:symbol.image.size()];
  return self;
}

- (NSString*)description {
  return [NSString stringWithFormat:@"<PPPointSymbol %lld %@ %lu bytes>",
                                    (long long)_symbolId, _name,
                                    (unsigned long)_imageData.length];
}

@end

@implementation PPMapPoint

- (instancetype)initWithPointId:(int64_t)pointId
                           name:(NSString*)name
                     coordinate:(PPGeoPoint)coordinate
                          shape:(NSString*)shape
                         sizePx:(double)sizePx
                       colorHex:(NSString*)colorHex
                       symbolId:(int64_t)symbolId
                       category:(NSString*)category
                    elevationFt:(double)elevationFt
                        remarks:(NSString*)remarks
                          phone:(NSString*)phone
                            url:(NSString*)url {
  self = [super init];
  if (self == nil) return nil;
  _pointId = pointId;
  _name = [name copy] ?: @"";
  _coordinate = coordinate;
  _shape = [shape copy] ?: PPPointShapeCircle;
  // A marker of zero width is one nobody can see or press. The document's own
  // default is 9; the app's points are 22, and that number arrives from
  // whoever made the point rather than from here.
  _sizePx = sizePx > 0.0 ? sizePx : 9.0;
  _colorHex = [colorHex copy] ?: @"#c82828";
  _symbolId = symbolId;
  _category = [category copy] ?: @"";
  _elevationFt = elevationFt;
  _remarks = [remarks copy] ?: @"";
  _phone = [phone copy] ?: @"";
  _url = [url copy] ?: @"";
  return self;
}

- (instancetype)initWithMapPoint:(const fv::MapPoint&)point {
  return [self initWithPointId:point.id
                          name:Str(point.name)
                    coordinate:PPGeoPointMake(point.position.lat,
                                              point.position.lon)
                         shape:Str(fv::ToString(point.shape))
                        sizePx:point.size_px
                      colorHex:Str(fv::PointColorToString(point.color))
                      symbolId:point.symbol_id
                      category:Str(point.category)
                   elevationFt:point.elevation_ft
                       remarks:Str(point.remarks)
                         phone:Str(point.phone)
                           url:Str(point.url)];
}

- (fv::MapPoint)mapPoint {
  fv::MapPoint p;
  p.id = _pointId;
  p.name = Cxx(_name);
  p.position = fv::GeoPoint{_coordinate.latitude, _coordinate.longitude};
  p.shape = fv::PointShapeFromString(Cxx(_shape));
  p.size_px = _sizePx;
  // The fallback is the document's own default red, and it is the same one
  // `ReadFile` uses: a colour string nobody can parse must not become black on
  // a dark chart.
  p.color = fv::PointColorFromString(Cxx(_colorHex),
                                     fv::FvColor{200, 40, 40, 255});
  p.symbol_id = _symbolId;
  p.category = Cxx(_category);
  p.elevation_ft = _elevationFt;
  p.remarks = Cxx(_remarks);
  p.phone = Cxx(_phone);
  p.url = Cxx(_url);
  return p;
}

- (nullable NSURL*)dialURL {
  NSMutableString* digits = [NSMutableString string];
  for (NSUInteger i = 0; i < _phone.length; ++i) {
    const unichar c = [_phone characterAtIndex:i];
    if (c >= '0' && c <= '9') {
      [digits appendFormat:@"%C", c];
    } else if (c == '+' && digits.length == 0) {
      // Only in the leading position: a `+` in the middle of a number is
      // punctuation somebody typed, not a country code.
      [digits appendString:@"+"];
    }
  }
  // A "number" with no digits in it is a note in the wrong column. Two is the
  // floor rather than one because a single stray digit in a remark is far more
  // likely than a one-digit telephone.
  NSUInteger count = 0;
  for (NSUInteger i = 0; i < digits.length; ++i) {
    const unichar c = [digits characterAtIndex:i];
    if (c >= '0' && c <= '9') ++count;
  }
  if (count < 2) return nil;
  return [NSURL URLWithString:[@"tel:" stringByAppendingString:digits]];
}

- (nullable NSURL*)webURL {
  NSString* text = [_url stringByTrimmingCharactersInSet:
                             NSCharacterSet.whitespaceAndNewlineCharacterSet];
  if (text.length == 0) return nil;

  NSURL* url = [NSURL URLWithString:text];
  if (url != nil && url.scheme.length > 0) return url;

  // A bare host gets `https://`, because "kiawahresort.com" is what a person
  // types and `NSURL` returns it as a schemeless relative path that opens
  // nothing. Anything with a scheme took the branch above.
  //
  // It has to look like a host first. Without that guard a telephone number
  // typed into the website field becomes `https://8435550199`, offered as a
  // link and opening nothing. A dot with something either side is the
  // cheapest test separating a host from a number, a note or a half-typed
  // word. It gates whether the shell offers a link, not what the document may
  // hold: the text is stored verbatim and still shown in the editor.
  const NSRange dot = [text rangeOfString:@"."];
  if (dot.location == NSNotFound || dot.location == 0 ||
      dot.location + 1 >= text.length)
    return nil;
  return [NSURL URLWithString:[@"https://" stringByAppendingString:text]];
}

- (NSString*)description {
  return [NSString stringWithFormat:@"<PPMapPoint %lld %@ %.5f,%.5f>",
                                    (long long)_pointId, _name,
                                    _coordinate.latitude,
                                    _coordinate.longitude];
}

@end
