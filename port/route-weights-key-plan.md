<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Route weights from the map key (RW)

**Status: built RW1–RW5, 2026-10-25.** Designed with Chris in conversation; see `port/PORTING.md` for open notes.
Open questions are at the end.

## Goal

The rider adjusts the router's preferences from Pippin's map key. Tapping a row opens a slider
that moves that kind of way between "Most preferred" and "Never". Adjustments are per profile,
persist, and apply to the next plan (and re-plan a route on screen).

## Starting point

- The key (LG1–LG4) already prices every row with `routing::CostOfTags` and shows
  `LegendTierWord` / `LegendSentence` / `LegendTermsLine` (`port/Routing/fv_route_legend.*`,
  `PPMap.mm` legend loop, `LegendSheet.swift`).
- The router applies the profile at query time; there are no contraction hierarchies
  (`fv_router.h`). Changing a weight costs nothing beyond the next search.
- A row's cost is often a product of terms. City bike profile, Atlanta Streets key:

  | Row | Specimen | Cost |
  |---|---|---|
  | Moderate road | tertiary | 1.5 |
  | Painted bike lane | tertiary + `cycleway:right=lane` | 1.5 × 0.6 = 0.90 |
  | Protected track | tertiary + `cycleway:right=track` | 1.5 × 0.45 = 0.675 |
  | Shared-lane marking | residential + `shared_lane` | 0.8 × 0.9 = 0.72 |
  | Local route | residential + route network | 0.8 × 0.9 = 0.72 |
  | Unpaved trail | cycleway + gravel (rough) | 0.6 × 1.6 = 0.96 |

  The specimen shows one combination; on the map the same painted lane on a busy road is
  2.5 × 0.6 = 1.5 (Acceptable) and on an arterial 4.0 × 0.6 = 2.4 (Avoided).

## Decisions

| Id | Question | Decision |
|---|---|---|
| RW-D1 | What does a row's slider change? | One profile setting: the one the row's label names. "Moderate road" → `class_weight[tertiary]`; "Painted bike lane" → `bike_lane_factor[lane]`, not tertiary. Rows sharing a setting move together. |
| RW-D2 | One slider kind or two? | Two. **Road types** (class settings) use the tier scale: Most preferred · Preferred · Acceptable · Avoided · Last resort · Never. **Modifiers** (bike lane, track, shared lane, route network, surface, golf) use a relative scale: Much better · Better · A little better · No difference · Worse · Much worse, meaning "compared with the same road without it". A modifier has no Never. |
| RW-D3 | Key layout on a phone | Two sections: **Road types** first, then **Lanes, routes and surfaces**. Rows are an accordion; tapping one expands its slider, one open at a time. Road-type pills show the tier word; modifier pills show the modifier word. |
| RW-D4 | Showing a modifier's effect on roads not in the specimen | The expanded modifier row has an "On the map this becomes" line: the resulting tier pill on each road class it can sit on (quiet street, moderate, busy, arterial). It updates as road-type sliders move. A modifier row whose result changes because of a road-type slider flashes briefly. |
| RW-D5 | Modifier slider semantics | Sets the modifier's own value, not a target tier for the row. Order of adjustments does not matter (factors multiply). Solving for a row tier was rejected: the result would depend on the other sliders at that moment and would move lanes on other roads unseen. |
| RW-D6 | Stop values and the default | Each stop stands for one factor (class: 0.6 / 0.9 / 1.5 / 3.5 / 8 / excluded; modifier: 0.45 / 0.6 / 0.9 / 1.0 / 1.6 / 2.5). The stop holding the shipped value restores that exact value, not the stand-in, so an untouched profile is identical to the rule file. A "default" mark sits over the shipped stop. |
| RW-D7 | Rows without a slider | Legal exclusions (motorway for bikes, `bicycle=no`) show a lock and their reason. Beach links to the route sheet's "Use beach" choice (it has the tide check) instead of a second control. |
| RW-D8 | Never on a road type | Allowed, with an inline warning that a ride needing that road ends in a straight leg. |
| RW-D9 | Making adjustments visible | Changed rows carry an accent dot; the header reads "Routing for Bike · N adjusted · Reset all"; the scope picker gains **Adjusted**. The route sheet shows a "Custom preferences (N)" chip beside Avoid hills that opens the key on Adjusted. |
| RW-D10 | Storage | Per-profile override map (setting id → stop) in UserDefaults. The rule file is never written. Overrides reach the planner as a field in the planner options beside `avoid_hills`, applied to a copy of the profile, so Peregrine and the GTK shell can use the same API. |
| RW-D11 | Feedback after a change | If a route is on screen, re-plan on closing the sheet and show a notice with the distance change and the main road it now avoids; the old line stays ghosted for a few seconds. |
| RW-D12 | ETA | Preferences change the cost only, not the reported time; the expanded row says so once. |
| RW-D13 | Reference for the row sentences | Factor 1.0, "the shortest way", replaces `legend_baseline` (a residential street). The quiet-street reference moved when its own slider moved and then contradicted the pills: with residential at 3.5, Busy road (2.5) read "40% further to use this" under an Avoided pill; with residential at Never every sentence vanished. Factor 1.0 never moves and matches the tier bounds (Preferred ≤ 1.0 always reads "to stay on this", anything above reads "to avoid"), and the ratio equals the factor in the terms line. |
| RW-D14 | Sentence wording | × throughout, at most two decimals (today's sentences switch from % to × at 2×). Below 1.0: "Pippin will ride up to 1.25× as far as the shortest way to stay on this." Exactly 1.0: "Pippin neither seeks this out nor avoids it." Above 1.0: "Pippin uses this only when the alternative is more than 2.5× as far." Excluded: unchanged. Modifier rows compare with the same road without the modifier: "…up to 1.7× as far to use a road with a painted lane instead of the same road without one." A time profile with varying speed (Car) says "as long"; the verb follows the profile's metric. |

## Steps

| Step | Scope |
|---|---|
| RW1 | `fv_route_legend`: sentences against factor 1.0 with RW-D14 wording; `CostTerm` carries a setting id and class/modifier kind; a legend entry's adjustable setting (the label's term, or the class when it is the only term) and whether it is locked. Update `RouteLegend.*` expected strings. Changes text the desktop and GTK legends show; GTK gets it from the next sync, not an edit there. |
| RW2 | Planner options gain the override map (RW-D10); `ApplyProfile` applies it to a copy. Tests: an untouched map is bit-identical to the rule file; Never on a class excludes it; a modifier override changes only arcs carrying it. |
| RW3 | `CostOfTags` per road class for a modifier's "applies to" line (RW-D4); PippinKit facade for overrides, stops and the line. |
| RW4 | `LegendSheet.swift`: two sections, accordion, both slider kinds, default mark, adjusted dot, Adjusted scope, Reset all, side-effect flash. |
| RW5 | Persistence per profile, route sheet chip, re-plan notice with ghosted old line. |

## Open questions

Answered by Chris 2026-10-25:

1. Modifier swatches: drawn over their specimen road, as today.
2. Modifier end stops: Much worse ×2.5 is the limit; keeping bikes off unpaved ways entirely is a class setting.
3. Adjustable settings and locks are declared per profile in the rule file, not inferred from class exclusions. Today's inferred lock (RW1/RW3) is an interim stand-in. Implemented in RW4 (`key_locks`).
4. "Bikes designated" is not shown in the key.
5. A row whose specimen carries two or more modifiers (unpaved cycleway on a signed route) has no slider. It shows its tier and terms line and names the settings behind it ("set by Rough surface and Local route"); each name links to that setting's own row.
