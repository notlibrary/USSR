# Tula

Tula is a small interactive 2D CAD program written in USSR on a Petersburg
canvas. From the repository root, run `ussr src/tula/examples/hello.su` with a
GLFW-enabled build. The window requests maximized display at startup.

The toolbar holds every command: **Line**, **Circle**, **Load DXF**,
**Save DXF**, **Zoom +**, **Zoom -**, **Fit**, and the pan buttons
**Left**/**Right**/**Up**/**Down**. Arrow keys pan, `=`/`-` zoom and `F` fits.

Choose **Line** and click two points on the canvas, or choose **Circle** and
click its center followed by a point on its circumference. Points snap to a
visible 5-unit grid. The fine grid is 5 units and major lines are 20 units
apart. The drawing uses millimeters and is exported as ASCII DXF 2000.

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

`tests/gui/tula_test.su` drives Tula with injected clicks and pointer events
and compares the exported DXF with the expected text;
`tests/gui/tula_dxf_test.su` checks DXF loading, rendering, zoom and the grid:

    ussr tests/gui/tula_test.su
    ussr tests/gui/tula_dxf_test.su
    ussr tests/gui/tula_toolbar_test.su
