// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Smoke test for the modern expat pulled in by port/third_party/CMakeLists.txt.
//
// This is an INTEGRATION check, not a test of expat itself (upstream has its
// own suite, which we disable when building): it proves the fetched library
// links, parses, and reports the version we asked for. The first real consumer
// is the S-52 PresLib loader (chartsymbols.xml, plan phase E2); Q12's WMS
// capabilities parsing is the second, and is the reason this is a current
// release rather than the 2012-era copy under `third_party/Expat 2.0.1`.

#include <expat.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

// Mirrors the SAX-consumer shape FalconView already uses portably in
// fvw_core/WMTComponents/GDALXMLReader.cpp — plain std::string/std::vector,
// no MFC, no COM. That file is the model for the E2 loader.
struct Collected {
  std::vector<std::string> elements;
  std::vector<std::string> attrs;
  std::string text;
};

void StartElement(void* ud, const XML_Char* name, const XML_Char** atts) {
  auto* c = static_cast<Collected*>(ud);
  c->elements.push_back(name);
  for (int i = 0; atts[i] != nullptr; i += 2)
    c->attrs.push_back(std::string(atts[i]) + "=" + atts[i + 1]);
}

void CharData(void* ud, const XML_Char* s, int len) {
  static_cast<Collected*>(ud)->text.append(s, len);
}

}  // namespace

TEST(Expat, ParsesNestedElementsAndAttributes) {
  // Shaped like the PresLib rows E2 will read: a lookup with attributes and a
  // nested instruction payload.
  const char kXml[] =
      "<chartsymbols>"
      "  <lookup name=\"DEPARE\" id=\"1\" type=\"Area\">"
      "    <instruction>AC(DEPIT)</instruction>"
      "  </lookup>"
      "</chartsymbols>";

  Collected c;
  XML_Parser p = XML_ParserCreate(nullptr);
  ASSERT_NE(p, nullptr);
  XML_SetUserData(p, &c);
  XML_SetElementHandler(p, StartElement, nullptr);
  XML_SetCharacterDataHandler(p, CharData);

  ASSERT_EQ(XML_STATUS_OK,
            XML_Parse(p, kXml, static_cast<int>(sizeof(kXml) - 1), /*isFinal=*/1))
      << XML_ErrorString(XML_GetErrorCode(p));
  XML_ParserFree(p);

  ASSERT_EQ(c.elements.size(), 3u);
  EXPECT_EQ(c.elements[0], "chartsymbols");
  EXPECT_EQ(c.elements[1], "lookup");
  EXPECT_EQ(c.elements[2], "instruction");

  ASSERT_EQ(c.attrs.size(), 3u);
  EXPECT_EQ(c.attrs[0], "name=DEPARE");
  EXPECT_EQ(c.attrs[1], "id=1");
  EXPECT_EQ(c.attrs[2], "type=Area");

  EXPECT_NE(c.text.find("AC(DEPIT)"), std::string::npos);
}

TEST(Expat, ReportsMalformedXmlRatherThanAborting) {
  const char kBad[] = "<a><b></a>";
  XML_Parser p = XML_ParserCreate(nullptr);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(XML_STATUS_ERROR,
            XML_Parse(p, kBad, static_cast<int>(sizeof(kBad) - 1), 1));
  EXPECT_EQ(XML_GetErrorCode(p), XML_ERROR_TAG_MISMATCH);
  XML_ParserFree(p);
}

TEST(Expat, IsAModernRelease) {
  // Header and linked library must be the same build (the stale-artifact bug
  // class), and must be well clear of the 2012-era in-tree copy, whose
  // expat.h reports 2.1.0. Expressed as a floor, not a literal, so a version
  // bump in port/third_party/CMakeLists.txt does not have to touch this file.
  const XML_Expat_Version v = XML_ExpatVersionInfo();
  EXPECT_EQ(v.major, XML_MAJOR_VERSION);
  EXPECT_EQ(v.minor, XML_MINOR_VERSION);
  EXPECT_EQ(v.micro, XML_MICRO_VERSION);

  const long combined = v.major * 10000L + v.minor * 100L + v.micro;
  EXPECT_GE(combined, 20600L) << "expat older than 2.6.0 — the 2022 CVE "
                                 "cluster predates it; see plan §5.6";
}
