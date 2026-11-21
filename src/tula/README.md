# Tula

Tula is a small interactive 2D CAD program written in USSR on a Petersburg
canvas. Start it from the repository root with a GLFW-enabled build:

    ussr src/tula/tula.su                 # new drawing; saves to drawing.dxf
    ussr src/tula/tula.su plan.dxf        # opens plan.dxf

The window requests maximized display at startup. `src/tula/examples/hello.su`
builds the same window from a script that includes `tula.su`.

The menu row is above the compact File, Drawing and Snap toolbars. The
**Tools** menu can hide or show each one. Hover over a square button to see
its tool name, or drag a dotted grip horizontally to reorder toolbars and
vertically to move one to another row. Arrow keys pan, `=`/`-` zoom and `F`
fits.

Choose **Line** and click successive points to add connected segments; a
right-click finishes the sequence. **Circle** uses a center and radius point,
**Circle Segment** uses center, radius, then end angle, and **Spline** accepts
three or more points and draws an approximated B-spline (quadratic for three
points, cubic for four or more). All cursor snaps are off by default. The
Snap toolbar independently toggles nearest-point on entity, grid, endpoints
and intersections. The adaptive minor grid starts at 5 units and major lines
are 20 units apart. The drawing uses millimeters and is exported as ASCII DXF
2000.

Enter a filename (defaults to `drawing.dxf`) and click **Save DXF**, or
**Load DXF** to view an existing drawing: lines, circles, arcs, polylines,
points, text and ellipses are rendered and anything else is named in the
status line. The mouse wheel zooms about the cursor, the grid adapts to the
zoom, and **Fit** shows the whole drawing. See `docs/API.md` for the
supported DXF subset. The in-memory geometry is stored
in world coordinates; the canvas is a rendered view, not the source of truth.
Tula supports up to 10,000 entities per drawing.

## Implementation

All of Tula is USSR code in `tula.su`. It keeps the entities, snapping, view
transform and DXF writer in USSR and draws through Petersburg's retained
canvas (`canvasline`, `canvascircle`, ...). Number formatting comes from
`src/libs/strings.su` and square roots from `src/libs/math.su`.

## Tests

`tests/gui/tula_test.su` drives Tula with injected clicks and pointer events,
tests the line, arc and spline tools, and compares exported DXF;
`tests/gui/tula_dxf_test.su` checks DXF loading, rendering, zoom and the grid:

    ussr tests/gui/tula_test.su
    ussr tests/gui/tula_dxf_test.su
    ussr tests/gui/tula_toolbar_test.su
