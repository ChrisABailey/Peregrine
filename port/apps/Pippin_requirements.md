# Pippin #
An IOS app using Peregrine to provide Cycling and Walking Navigation, and a moving map for rides and runs.

## Requirements ##
- Runs on Current IOS
- Requires no server (but could possibly access existing servers for as a gazateer or information lookup)
- Uses only local stored Open Street Map data for display and routing
- Allows user to create and edit routes
- Uses moving map functionality to show location relative to planned routes
- Calculates basic stats about cuttent trip including elapsed time and distance, current speed, remaining distance and predicted arrival time based on current speed

## general program flow (Suggestions - Not hard requirements)
- Program opens with map showing only Kiawah Island in OSM
  - *(Changed by Chris 2026-08-19, built as P12)*: the opening view FILLS the screen with
    map — cover, not contain. The pack's box is landscape and a phone is portrait, so
    fitting all of it left background bands above and below; the island's long axis now
    runs off the sides and one pinch brings the whole of it back.
- Current GPS location shown on map (but no auto center)
- round "route" button on lower left round "start" button on lower right
- Click route starts a new route 
  - User choose start locatatin with options to use current location, or click on map or in the future type a POI name
  - user chooses end location (similiar options to start)
  - User optionally adds via point
  - User chooses mode of transportation, cycle / walk 
  - Pressing OK dismisses Dialog and calculates the path
- Clicking route when route alread exists just allows user to change info 
- In the initial version clicking gps button immediatly centers on current location and auto-rotates the map automatticaly starts the display of trip information:
    - Elapsed time, current speed, distance traveled, distanct to end, arival time
    - if route had been created and it is not displayed on the map, zoom out until to see the current location and the start point.
- clicking GPS again exits GPS mode

## Initial Area of interest
- Kiawah Island only
- Long term goal to add additional data-sets for other Aeras of interest

## Additional (optional) features
- Points files with local points of interest and ability to use use points as snap points for the route (Start = Ruddy Turnstone, End = Boardwalk 12)
  - *(Built as P9 + P19, 2026-08-25.)* Snapping is a capability of the OVERLAY interface
    (`fv::app::SnapTo`) rather than a points feature: a pick that overlaps ANY overlay's feature
    takes that feature's exact lat/lon, and an overlay opts in by implementing the interface.
    The points and the route's waypoints both do. Verified on the simulator to ten decimals.
- compass icon in top right showing nort and toggle on/off the auto-rotate feature
- Allow users to add and edit points 
- Record and save trips as GPX files
- export data through a share button
- When picking a route point on the map, the confirm button names the nearest road or path
  instead of printing a lat/lon: "Use Flyaway Drive", or "Use cycleway" where the path has no
  name. If no road or path usable by the chosen mode is within the maximum distance it reads
  "No usable path" — but the point can still be chosen.
